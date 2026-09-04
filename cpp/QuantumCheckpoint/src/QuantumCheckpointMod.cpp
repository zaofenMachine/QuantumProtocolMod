#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "CheckpointSchema.hpp"
#include "CheckpointPersistence.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <Windows.h>
#include <bcrypt.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Input/Handler.hpp>
#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FOutputDevice.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace QuantumCheckpoint
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        std::atomic_bool g_export_requested{false};
        std::atomic_bool g_health_write_probe_requested{false};
        std::atomic_bool g_turn_write_probe_requested{false};
        std::atomic_bool g_route_c_save_requested{false};
        std::atomic_bool g_route_c_load_requested{false};
        std::atomic_bool g_unreal_ready{false};

        struct ExecutableFingerprint
        {
            std::filesystem::path path{};
            std::uint64_t size{};
            std::string sha256{};
        };

        enum class RouteCRestorePhase
        {
            AwaitingWaveIntercept,
            AwaitingBattleInfrastructure,
            AwaitingStableBattle,
            AwaitingVerification,
            AwaitingPostRestoreStability,
        };

        auto route_c_restore_phase_name(RouteCRestorePhase phase) -> std::string_view
        {
            switch (phase)
            {
            case RouteCRestorePhase::AwaitingWaveIntercept:
                return "wave-intercept";
            case RouteCRestorePhase::AwaitingBattleInfrastructure:
                return "battle-infrastructure";
            case RouteCRestorePhase::AwaitingStableBattle:
                return "stable-battle";
            case RouteCRestorePhase::AwaitingVerification:
                return "verification";
            case RouteCRestorePhase::AwaitingPostRestoreStability:
                return "post-restore-stability";
            }
            return "unknown";
        }

        struct PendingRouteCRestore
        {
            RouteCCheckpoint checkpoint{};
            std::optional<ExactSpawnPlanCheckpoint> exact_spawn_plan{};
            std::optional<ExactPlayerZonesCheckpoint> exact_player_zones{};
            std::string exact_player_startup_decklist{};
            std::vector<std::string> loot_drops{};
            RouteCRestorePhase phase{RouteCRestorePhase::AwaitingWaveIntercept};
            std::chrono::steady_clock::time_point started_at{};
            std::chrono::steady_clock::time_point phase_ready_at{};
            UObject* intercepted_spawner{};
            const void* intercepted_world{};
            bool auto_spawn_suppressed{};
            bool game_instance_reimported_at_card_engine_begin_play{};
            bool game_instance_reimported_at_begin_play{};
            bool character_card_slot_observed{};
            std::optional<std::int32_t> character_card_charge{};
            std::optional<std::int32_t> character_card_charge_requirement{};
            std::optional<std::string> character_ability_ok{};
            std::optional<std::chrono::steady_clock::time_point> empty_player_state_since{};
            std::optional<std::chrono::steady_clock::time_point>
                exact_player_zone_mismatch_since{};
            std::string original_auto_spawn{};
            std::string original_spawn_list{};
            std::string exact_spawn_plan_status{"unavailable"};
            std::string exact_spawn_plan_reason{};
            std::string exact_player_zones_status{"unavailable"};
            std::string exact_player_zones_reason{};
            bool active_decklist_restored_after_exact_startup{};
            std::string interception_error{};
            std::string last_diagnostic{};
            std::chrono::steady_clock::time_point next_diagnostic_at{};
            std::string status{"running"};
            std::string reason{};
        };

        struct PendingRouteCCapture
        {
            UObject* spawner{};
            std::int32_t wave_index{};
            std::chrono::steady_clock::time_point ready_at{};
        };

        struct RouteCWaveObservation
        {
            UObject* spawner{};
            const void* world{};
            std::int32_t wave_index{};
        };

        std::optional<ExecutableFingerprint> g_executable_fingerprint{};
        std::optional<PendingRouteCRestore> g_pending_route_c_restore{};
        std::optional<RouteCCheckpoint> g_last_completed_route_c_checkpoint{};
        std::optional<PendingRouteCCapture> g_pending_route_c_capture{};
        std::optional<RouteCWaveObservation> g_route_c_wave_observation{};
        std::chrono::steady_clock::time_point g_next_route_c_wave_poll{};
        bool g_adopt_next_route_c_wave_without_capture{};
        bool g_route_c_travel_occurred_in_process{};
        bool g_begin_play_callbacks_registered{};
        UFunction* g_spawn_wave_index_function{};
        UFunction* g_spawn_next_wave_function{};
        UClass* g_spawn_controller_class{};
        std::optional<std::pair<int, int>> g_spawn_wave_index_hook_ids{};
        std::optional<std::pair<int, int>> g_spawn_next_wave_hook_ids{};

        auto is_live_instance(std::string_view full_name, std::string_view role) -> bool;
        auto classify(std::string_view full_name) -> std::string;
        auto object_class_name(UObject* object) -> std::string;

        constexpr std::array<std::string_view, 18> RelevantClassPrefixes{
            "BP_CardEngine_C ",
            "BP_BottomBar_C ",
            "BP_ControllerCharacterCardSlot_C ",
            "BP_ControllerDeck_C ",
            "BP_ControllerHand_C ",
            "BP_ControllerStorage_C ",
            "BP_ControllerPendingCards_C ",
            "BP_ControllerEnemyPending_C ",
            "BP_ControllerBoard_C ",
            "BP_ControllerTrash_C ",
            "BP_InGameCard_C ",
            "GI_Quantum_C ",
            "CardPlacementComponent ",
            "BP_FieldSlot_C ",
            "BP_CardEffectDisplay_C ",
            "BP_GenericCounterDisplay_C ",
            "BP_SpecialCounterDisplay_C ",
            "UMG_CardEffectDisplay_C ",
        };

        constexpr std::array<StringViewType, 60> CandidateProperties{
            STR("currentWaveIndex"),
            STR("lastWaveIndex"),
            STR("currentTurnCountdown"),
            STR("waveIndex"),
            STR("waveNumber"),
            STR("waveCountdownPenalty"),
            STR("currentWaveAlertCounter"),
            STR("amountPerWaveAlertLevel"),
            STR("maxWaveAlertStacks"),
            STR("autoSpawn"),
            STR("spawnList"),
            STR("currentHealth"),
            STR("maxHealth"),
            STR("health"),
            STR("currentGameState"),
            STR("isEnemyBoardPenaltyOn"),
            STR("startingHandSize"),
            STR("playtime"),
            STR("deckRun"),
            STR("deckrunId"),
            STR("deck"),
            STR("hand"),
            STR("storage"),
            STR("trash"),
            STR("pending"),
            STR("levelName"),
            STR("levelTag"),
            STR("levelToLoad"),
            STR("CurrentLevel"),
            STR("sourceLevelName"),
            STR("secondaryLevelToGoto"),
            STR("lastLevelChangeType"),
            STR("activeCharacterInfo"),
            STR("activeStageInfo"),
            STR("cardInstanceList"),
            STR("cardInstances"),
            STR("cardInfoInstance"),
            STR("CardInfoInstance"),
            STR("cardLocation"),
            STR("currentTurnCounter"),
            STR("turnCounter"),
            STR("CardPlacementComponent"),
            STR("cardOverlayGenericCounters"),
            STR("cardOverlaySpecialCounters"),
            STR("cardOverlayEffects"),
            STR("mCardEffectWidget"),
            STR("EffectType"),
            STR("activationBlockers"),
            STR("isAutomationHighlighted"),
            STR("cardModifiers"),
            STR("modifiers"),
            STR("tag"),
            STR("Tag"),
            STR("id"),
            STR("Id"),
            STR("upgradeLevel"),
            STR("rowType"),
            STR("SlotIndex"),
            STR("boardSide"),
        };

        constexpr std::array<StringViewType, 5> GameInstanceGetters{
            STR("getCurrentDeckRun"),
            STR("getActiveDecklistInstances"),
            STR("getActiveStorage"),
            STR("getLootDrops"),
            STR("getLootDropInstances"),
        };

        constexpr std::array<StringViewType, 3> PostTravelSafeGameInstanceGetters{
            STR("getActiveStorage"),
            STR("getLootDrops"),
            STR("getLootDropInstances"),
        };

        constexpr std::array<StringViewType, 1> CardGroupGetters{
            STR("getCardInstanceListSorted"),
        };

        constexpr std::array<StringViewType, 4> CharacterCardSlotGetters{
            STR("getCardInstanceListSorted"),
            STR("getCurrentCharacterCardCharge"),
            STR("getAmountPerCharacterCard"),
            STR("isCharacterAbilityOk"),
        };

        constexpr std::array<StringViewType, 7> InGameCardGetters{
            STR("getTag"),
            STR("getId"),
            STR("getCurrentTurnCounter"),
            STR("getCurrentHealth"),
            STR("getCardLocation"),
            STR("getCardInfoInstance"),
            STR("isTurnActive"),
        };

        constexpr std::array<StringViewType, 3> CardEngineGetters{
            STR("getTurnCount"),
            STR("getCurrentMaxTurnCountdown"),
            STR("getCurrentHealth"),
        };

        constexpr std::array<StringViewType, 1> PlacementComponentGetters{
            STR("getPlacedFieldSlot"),
        };

        constexpr std::array<StringViewType, 1> FieldSlotGetters{
            STR("getPlacementInfo"),
        };

        constexpr std::array<StringViewType, 1> EffectDisplayGetters{
            STR("getEffectActionState"),
        };

        constexpr std::array<StringViewType, 1> CounterDisplayGetters{
            STR("getCurrentCounters"),
        };

        // Native diagnostics for Quantum-Win64-Shipping.exe SHA-256
        // 0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6.
        // These are read-only corroboration fields, not a supported checkpoint format.
        constexpr std::size_t InGameCardStatePointerOffset = 0x228;
        constexpr std::size_t CardStateBaseHealthOffset = 0x118;
        constexpr std::size_t CardStateCurrentHealthOffset = 0x11C;
        constexpr std::size_t CardStateTurnAdjustmentOffset = 0x194;
        constexpr std::size_t CardStateTurnBaseOffset = 0x198;
        constexpr std::uintmax_t ExpectedGameExecutableSize = 82'718'720;
        constexpr std::string_view ExpectedGameExecutableSha256 =
            "0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6";
        constexpr std::uintptr_t CurrentHealthGetterThunkRva = 0x102D100;
        constexpr std::uintptr_t CurrentTurnGetterThunkRva = 0x102D130;
        constexpr std::uintptr_t CardEngineCurrentHealthGetterThunkRva = 0x1019930;
        constexpr std::uintptr_t CardEngineMaxHealthGetterThunkRva = 0x1019CD0;
        constexpr std::uintptr_t NativeCurrentTurnGetterRva = 0xE323C0;
        constexpr std::uintptr_t SetCurrentHealthRva = 0xE522E0;
        constexpr std::size_t CardEngineHealthStatePointerOffset = 0x268;
        constexpr std::size_t PlayerStateCurrentHealthOffset = 0x1C;
        constexpr std::size_t PlayerStateMaxHealthOffset = 0x20;
        constexpr auto TimedHealthProbeHold = std::chrono::milliseconds{1000};
        constexpr auto TimedTurnProbeHold = std::chrono::milliseconds{1000};
        constexpr std::array<std::uint8_t, 7> SetCurrentHealthSignature{
            0x89, 0x91, 0x1C, 0x01, 0x00, 0x00, 0xC3,
        };
        constexpr std::array<std::uint8_t, 13> NativeCurrentTurnGetterSignature{
            0x8B, 0x81, 0x98, 0x01, 0x00, 0x00,
            0x03, 0x81, 0x94, 0x01, 0x00, 0x00,
            0xC3,
        };

        struct HealthWriteProbeResult
        {
            std::string status{"not-run"};
            std::string reason{};
            std::string card_full_name{};
            std::string card_tag{};
            std::string card_id{};
            std::string card_location{};
            std::uintptr_t state_address{};
            std::int32_t base_health{};
            std::int32_t before_private{};
            std::int32_t before_getter{};
            std::int32_t test_value{};
            std::int32_t during_private{};
            std::int32_t during_getter{};
            std::int64_t requested_hold_milliseconds{};
            std::int64_t actual_hold_milliseconds{};
            bool restore_identity_validated{};
            std::int32_t before_restore_private{};
            std::int32_t restored_private{};
            std::int32_t restored_getter{};
        };

        using SetCurrentHealthFunction = void(__fastcall*)(const void*, std::int32_t);

        struct HealthRestoreGuard
        {
            SetCurrentHealthFunction setter{};
            const void* state{};
            std::int32_t value{};
            bool active{true};

            ~HealthRestoreGuard()
            {
                if (active && setter && state)
                {
                    setter(state, value);
                }
            }
        };

        struct PendingHealthWriteProbe
        {
            HealthWriteProbeResult result{};
            UObject* card{};
            const void* state{};
            SetCurrentHealthFunction setter{};
            std::chrono::steady_clock::time_point started_at{};
            std::chrono::steady_clock::time_point restore_after{};
        };

        std::optional<PendingHealthWriteProbe> g_pending_health_write_probe{};

        struct TurnWriteProbeResult
        {
            std::string status{"not-run"};
            std::string reason{};
            std::string card_full_name{};
            std::string card_location{};
            std::uintptr_t state_address{};
            std::int32_t before_base{};
            std::int32_t before_adjustment{};
            std::int64_t before_computed{};
            std::int32_t before_getter{};
            std::int32_t test_adjustment{};
            std::int64_t test_computed{};
            std::int32_t during_base{};
            std::int32_t during_adjustment{};
            std::int64_t during_computed{};
            std::int64_t requested_hold_milliseconds{};
            std::int64_t actual_hold_milliseconds{};
            bool restore_identity_validated{};
            std::int32_t before_restore_base{};
            std::int32_t before_restore_adjustment{};
            std::int64_t before_restore_computed{};
            std::int32_t restored_base{};
            std::int32_t restored_adjustment{};
            std::int64_t restored_computed{};
            std::int32_t restored_getter{};
        };

        struct Int32FieldRestoreGuard
        {
            const void* state{};
            std::size_t offset{};
            std::int32_t value{};
            bool active{true};

            ~Int32FieldRestoreGuard()
            {
                if (active && state)
                {
                    std::memcpy(
                        static_cast<std::byte*>(const_cast<void*>(state)) + offset,
                        &value,
                        sizeof(value));
                }
            }
        };

        struct PendingTurnWriteProbe
        {
            TurnWriteProbeResult result{};
            UObject* card{};
            const void* state{};
            std::chrono::steady_clock::time_point started_at{};
            std::chrono::steady_clock::time_point restore_after{};
        };

        std::optional<PendingTurnWriteProbe> g_pending_turn_write_probe{};

        auto json_escape(std::string_view value) -> std::string
        {
            std::string escaped{};
            escaped.reserve(value.size() + 16);
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\b': escaped += "\\b"; break;
                case '\f': escaped += "\\f"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default:
                    if (character < 0x20)
                    {
                        std::ostringstream encoded{};
                        encoded << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                << static_cast<int>(character);
                        escaped += encoded.str();
                    }
                    else
                    {
                        escaped.push_back(static_cast<char>(character));
                    }
                }
            }
            return escaped;
        }

        auto utc_timestamp() -> std::string
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t value = std::chrono::system_clock::to_time_t(now);
            std::tm utc{};
            gmtime_s(&utc, &value);

            std::ostringstream stream{};
            stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            return stream.str();
        }

        auto filename_timestamp() -> std::string
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t value = std::chrono::system_clock::to_time_t(now);
            std::tm utc{};
            gmtime_s(&utc, &value);

            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;

            std::ostringstream stream{};
            stream << std::put_time(&utc, "%Y%m%d-%H%M%S") << '-'
                   << std::setw(3) << std::setfill('0') << milliseconds.count();
            return stream.str();
        }

        struct BCryptAlgorithmHandle
        {
            BCRYPT_ALG_HANDLE value{};
            ~BCryptAlgorithmHandle()
            {
                if (value)
                {
                    BCryptCloseAlgorithmProvider(value, 0);
                }
            }
        };

        struct BCryptHashHandle
        {
            BCRYPT_HASH_HANDLE value{};
            ~BCryptHashHandle()
            {
                if (value)
                {
                    BCryptDestroyHash(value);
                }
            }
        };

        auto sha256_file(const std::filesystem::path& path) -> std::string
        {
            BCryptAlgorithmHandle algorithm{};
            if (BCryptOpenAlgorithmProvider(
                    &algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            {
                throw std::runtime_error{"Unable to open the Windows SHA-256 provider"};
            }

            DWORD object_size{};
            DWORD hash_size{};
            DWORD bytes_written{};
            if (BCryptGetProperty(
                    algorithm.value,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&object_size),
                    sizeof(object_size),
                    &bytes_written,
                    0) < 0
                || BCryptGetProperty(
                    algorithm.value,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hash_size),
                    sizeof(hash_size),
                    &bytes_written,
                    0) < 0
                || hash_size != 32)
            {
                throw std::runtime_error{"Unable to query the Windows SHA-256 provider"};
            }

            std::vector<UCHAR> hash_object(object_size);
            std::vector<UCHAR> hash(hash_size);
            BCryptHashHandle hash_handle{};
            if (BCryptCreateHash(
                    algorithm.value,
                    &hash_handle.value,
                    hash_object.data(),
                    static_cast<ULONG>(hash_object.size()),
                    nullptr,
                    0,
                    0) < 0)
            {
                throw std::runtime_error{"Unable to create the SHA-256 state"};
            }

            std::ifstream input{path, std::ios::binary};
            if (!input)
            {
                throw std::runtime_error{"Unable to open the game executable for hashing"};
            }
            std::array<char, 1024 * 1024> buffer{};
            while (input)
            {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = input.gcount();
                if (count > 0
                    && BCryptHashData(
                        hash_handle.value,
                        reinterpret_cast<PUCHAR>(buffer.data()),
                        static_cast<ULONG>(count),
                        0) < 0)
                {
                    throw std::runtime_error{"Unable to hash the game executable"};
                }
            }
            if (!input.eof())
            {
                throw std::runtime_error{"Unable to finish reading the game executable"};
            }
            if (BCryptFinishHash(
                    hash_handle.value, hash.data(), static_cast<ULONG>(hash.size()), 0) < 0)
            {
                throw std::runtime_error{"Unable to finish the game executable SHA-256"};
            }

            std::ostringstream output{};
            output << std::hex << std::uppercase << std::setfill('0');
            for (const auto byte : hash)
            {
                output << std::setw(2) << static_cast<unsigned>(byte);
            }
            return output.str();
        }

        auto executable_fingerprint() -> const ExecutableFingerprint&
        {
            if (g_executable_fingerprint)
            {
                return *g_executable_fingerprint;
            }

            const auto module = GetModuleHandleW(L"Quantum-Win64-Shipping.exe");
            if (!module)
            {
                throw std::runtime_error{"Game executable module was not found"};
            }
            std::array<wchar_t, 32768> executable_path{};
            const auto path_length = GetModuleFileNameW(
                module, executable_path.data(), static_cast<DWORD>(executable_path.size()));
            if (path_length == 0 || path_length >= executable_path.size())
            {
                throw std::runtime_error{"Game executable path could not be resolved"};
            }

            ExecutableFingerprint fingerprint{};
            fingerprint.path = std::filesystem::path{executable_path.data()};
            fingerprint.size = std::filesystem::file_size(fingerprint.path);
            fingerprint.sha256 = sha256_file(fingerprint.path);
            g_executable_fingerprint.emplace(std::move(fingerprint));
            return *g_executable_fingerprint;
        }

        auto fingerprint_matches_supported_game(const ExecutableFingerprint& fingerprint) -> bool
        {
            return fingerprint.size == ExpectedGameExecutableSize
                && fingerprint.sha256 == ExpectedGameExecutableSha256;
        }

        auto export_property_text(UObject* object, StringViewType property_name)
            -> std::optional<std::string>
        {
            if (!object)
            {
                return std::nullopt;
            }
            auto* property = object->GetPropertyByNameInChain(property_name.data());
            if (!property)
            {
                return std::nullopt;
            }

            FString exported{};
            auto* value = property->ContainerPtrToValuePtr<void>(object);
            property->ExportTextItem(exported, value, value, object, 0);
            return to_string(exported.GetCharArray());
        }

        auto import_property_text(UObject* object, StringViewType property_name,
                                  std::string_view value) -> void
        {
            if (!object)
            {
                throw std::runtime_error{"Cannot import a property on a null object"};
            }
            auto* property = object->GetPropertyByNameInChain(property_name.data());
            if (!property)
            {
                throw std::runtime_error{
                    "Required reflected property was not found: " + to_string(property_name)};
            }
            const auto wide_value = to_wstring(value);
            FOutputDevice errors{};
            if (!property->ImportText(
                    wide_value.c_str(),
                    property->ContainerPtrToValuePtr<void>(object),
                    0,
                    object,
                    &errors))
            {
                throw std::runtime_error{
                    "Unreal rejected reflected property text: " + to_string(property_name)};
            }
        }

        struct ReflectedArgument
        {
            StringViewType name{};
            std::string_view value{};
        };

        struct ReflectedParameterBuffer
        {
            std::vector<std::uint8_t> bytes{};
            std::vector<FProperty*> initialized{};

            explicit ReflectedParameterBuffer(UFunction* function)
                : bytes(static_cast<std::size_t>(function->GetParmsSize()), 0)
            {
                for (auto* property : function->ForEachProperty())
                {
                    if (property->HasAnyPropertyFlags(CPF_Parm))
                    {
                        property->InitializeValue_InContainer(bytes.data());
                        initialized.push_back(property);
                    }
                }
            }

            ~ReflectedParameterBuffer()
            {
                for (auto iterator = initialized.rbegin(); iterator != initialized.rend(); ++iterator)
                {
                    (*iterator)->DestroyValue_InContainer(bytes.data());
                }
            }
        };

        auto call_reflected(UObject* object, StringViewType function_name,
                            std::initializer_list<ReflectedArgument> arguments = {}) -> void
        {
            if (!object)
            {
                throw std::runtime_error{"Cannot call a reflected function on a null object"};
            }
            auto* function = object->GetFunctionByNameInChain(function_name.data());
            if (!function)
            {
                throw std::runtime_error{
                    "Required reflected function was not found: " + to_string(function_name)};
            }

            ReflectedParameterBuffer parameters{function};
            for (const auto& argument : arguments)
            {
                auto* property = function->GetPropertyByNameInChain(argument.name.data());
                if (!property || !property->HasAnyPropertyFlags(CPF_Parm)
                    || property->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    throw std::runtime_error{
                        "Required reflected function argument was not found: "
                        + to_string(argument.name)};
                }
                if (argument.value == "()" && property->IsA<FArrayProperty>())
                {
                    // UE 4.27 exports an empty TArray return as empty text. Route C persists
                    // that value canonically as "()", but importing "()" into an array of
                    // structs can construct one default element. The parameter buffer already
                    // owns a correctly initialized zero-length TArray, so leave it untouched.
                    continue;
                }
                const auto wide_value = to_wstring(argument.value);
                FOutputDevice errors{};
                if (!property->ImportText(
                        wide_value.c_str(),
                        property->ContainerPtrToValuePtr<void>(parameters.bytes.data()),
                        0,
                        object,
                        &errors))
                {
                    throw std::runtime_error{
                        "Unreal rejected reflected function argument: "
                        + to_string(argument.name)};
                }
            }

            object->ProcessEvent(function, parameters.bytes.empty() ? nullptr : parameters.bytes.data());
        }

        struct RouteCBattleObjects
        {
            UObject* game_instance{};
            UObject* card_engine{};
            UObject* spawner{};
            UObject* bottom_bar{};
            UObject* character_card_slot{};
        };

        struct RouteCPlayerZoneObjects
        {
            UObject* deck{};
            UObject* hand{};
        };

        auto reflected_object_property(UObject* owner, StringViewType property_name) -> UObject*
        {
            if (!owner)
            {
                return nullptr;
            }
            auto* property = owner->GetPropertyByNameInChain(property_name.data());
            if (!property || !property->IsA<FObjectProperty>())
            {
                return nullptr;
            }
            auto** storage = property->ContainerPtrToValuePtr<UObject*>(owner);
            auto* object = storage ? *storage : nullptr;
            if (!object || object->IsUnreachable()
                || object->HasAnyFlags(
                    static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed)))
            {
                return nullptr;
            }
            return object;
        }

        auto is_route_c_spawn_controller(UObject* object) -> bool
        {
            return object && g_spawn_controller_class && object->IsA(g_spawn_controller_class)
                && object->GetPropertyByNameInChain(STR("currentWaveIndex"))
                && object->GetFunctionByNameInChain(STR("spawnWaveIndex"));
        }

        auto find_route_c_objects(const void* preferred_world = nullptr) -> RouteCBattleObjects
        {
            RouteCBattleObjects result{};
            UObjectGlobals::ForEachUObject([&](UObject* object,
                                                [[maybe_unused]] int32_t object_index,
                                                [[maybe_unused]] int32_t chunk_index) {
                if (!object || object->IsUnreachable()
                    || object->HasAnyFlags(
                        static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed)))
                {
                    return LoopAction::Continue;
                }
                const std::string full_name = to_string(object->GetFullName());
                const std::string role = classify(full_name);
                if (role == "GI_Quantum_C" && full_name.contains("/Engine/Transient."))
                {
                    result.game_instance = object;
                }
                else if (role == "BP_CardEngine_C" && is_live_instance(full_name, role)
                         && (!preferred_world
                             || static_cast<const void*>(object->GetWorld()) == preferred_world))
                {
                    result.card_engine = object;
                }
                else if ((role == "Spawner_C" || is_route_c_spawn_controller(object))
                         && is_live_instance(full_name, "Spawner_C")
                         && (!preferred_world
                             || static_cast<const void*>(object->GetWorld()) == preferred_world))
                {
                    result.spawner = object;
                }
                else if (role == "BP_BottomBar_C" && is_live_instance(full_name, role)
                         && (!preferred_world
                             || static_cast<const void*>(object->GetWorld()) == preferred_world))
                {
                    result.bottom_bar = object;
                }
                else if (role == "BP_ControllerCharacterCardSlot_C"
                         && is_live_instance(full_name, role)
                         && (!preferred_world
                             || static_cast<const void*>(object->GetWorld()) == preferred_world))
                {
                    result.character_card_slot = object;
                }
                return LoopAction::Continue;
            });

            // Native dungeon spawners (for example NeskaraDungeon1Spawner) do not use a
            // Blueprint "*_Spawner_C" class name. CardEngine owns the authoritative pointer,
            // so prefer that exact controller over name-based discovery.
            if (result.card_engine)
            {
                if (auto* anchored_spawner = reflected_object_property(
                        result.card_engine, STR("mEnemySpawnController"));
                    is_route_c_spawn_controller(anchored_spawner)
                    && (!preferred_world
                        || static_cast<const void*>(anchored_spawner->GetWorld()) == preferred_world))
                {
                    result.spawner = anchored_spawner;
                }

                if (auto* anchored_bottom_bar = reflected_object_property(
                        result.card_engine, STR("mHUDBottomBar"));
                    anchored_bottom_bar
                    && (!preferred_world
                        || static_cast<const void*>(anchored_bottom_bar->GetWorld()) == preferred_world))
                {
                    result.bottom_bar = anchored_bottom_bar;
                }
            }
            return result;
        }

        auto find_route_c_player_zone_objects(const void* preferred_world)
            -> RouteCPlayerZoneObjects
        {
            RouteCPlayerZoneObjects result{};
            UObjectGlobals::ForEachUObject([&](UObject* object,
                                                [[maybe_unused]] int32_t object_index,
                                                [[maybe_unused]] int32_t chunk_index) {
                if (!object || object->IsUnreachable()
                    || object->HasAnyFlags(
                        static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed)))
                {
                    return LoopAction::Continue;
                }
                const auto full_name = to_string(object->GetFullName());
                const auto role = classify(full_name);
                if ((role != "BP_ControllerDeck_C" && role != "BP_ControllerHand_C")
                    || !is_live_instance(full_name, role)
                    || (preferred_world
                        && static_cast<const void*>(object->GetWorld()) != preferred_world))
                {
                    return LoopAction::Continue;
                }
                if (role == "BP_ControllerDeck_C")
                {
                    result.deck = object;
                }
                else if (role == "BP_ControllerHand_C")
                {
                    result.hand = object;
                }
                return LoopAction::Continue;
            });
            return result;
        }

        auto find_route_c_unsafe_companion() -> std::optional<std::string>
        {
            constexpr std::array<std::string_view, 5> unsupported_classes{
                "BP_academyIntroFilter_C ",
                "BP_AuroraTutorialFilter_C ",
                "BP_introTutorialFilter_C ",
                "BP_BossEntranceTutorial_C ",
                "BP_BossWaystoneWatcher_C ",
            };

            std::optional<std::string> result{};
            UObjectGlobals::ForEachUObject([&](UObject* object,
                                                [[maybe_unused]] int32_t object_index,
                                                [[maybe_unused]] int32_t chunk_index) {
                if (!object || result)
                {
                    return result ? LoopAction::Break : LoopAction::Continue;
                }
                const std::string full_name = to_string(object->GetFullName());
                if (!full_name.contains(".PersistentLevel."))
                {
                    return LoopAction::Continue;
                }
                for (const auto class_prefix : unsupported_classes)
                {
                    if (full_name.starts_with(class_prefix))
                    {
                        result = object_class_name(object);
                        return LoopAction::Break;
                    }
                }
                return LoopAction::Continue;
            });
            return result;
        }

        auto object_class_name(UObject* object) -> std::string
        {
            if (!object)
            {
                return {};
            }
            const std::string full_name = to_string(object->GetFullName());
            const auto separator = full_name.find(' ');
            return separator == std::string::npos ? full_name : full_name.substr(0, separator);
        }

        auto route_c_checkpoint_path() -> std::filesystem::path
        {
            const auto mods_directory = std::filesystem::path{
                UE4SSProgram::get_program().get_mods_directory()};
            return mods_directory / STR("QuantumCheckpoint") / STR("Checkpoint")
                / STR("route-c.json");
        }

        auto exact_spawn_plan_checkpoint_path() -> std::filesystem::path
        {
            const auto mods_directory = std::filesystem::path{
                UE4SSProgram::get_program().get_mods_directory()};
            return mods_directory / STR("QuantumCheckpoint") / STR("Checkpoint")
                / STR("route-c-exact-spawn-plan.json");
        }

        auto exact_player_zones_checkpoint_path() -> std::filesystem::path
        {
            const auto mods_directory = std::filesystem::path{
                UE4SSProgram::get_program().get_mods_directory()};
            return mods_directory / STR("QuantumCheckpoint") / STR("Checkpoint")
                / STR("route-c-exact-player-zones.json");
        }

        auto append_route_c_trace(std::string_view event) noexcept -> void
        {
            try
            {
                const auto mods_directory = std::filesystem::path{
                    UE4SSProgram::get_program().get_mods_directory()};
                const auto path = mods_directory / STR("QuantumCheckpoint")
                    / STR("route-c-trace.log");
                const HANDLE file = CreateFileW(
                    path.c_str(),
                    FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                    nullptr);
                if (file == INVALID_HANDLE_VALUE)
                {
                    return;
                }

                SYSTEMTIME utc{};
                GetSystemTime(&utc);
                std::array<char, 512> line{};
                const int length = std::snprintf(
                    line.data(),
                    line.size(),
                    "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu %.*s\r\n",
                    static_cast<unsigned>(utc.wYear),
                    static_cast<unsigned>(utc.wMonth),
                    static_cast<unsigned>(utc.wDay),
                    static_cast<unsigned>(utc.wHour),
                    static_cast<unsigned>(utc.wMinute),
                    static_cast<unsigned>(utc.wSecond),
                    static_cast<unsigned>(utc.wMilliseconds),
                    static_cast<unsigned long>(GetCurrentProcessId()),
                    static_cast<unsigned long>(GetCurrentThreadId()),
                    static_cast<int>(std::min(event.size(), line.size() / 2)),
                    event.data());
                if (length > 0)
                {
                    const DWORD bytes_to_write = static_cast<DWORD>(
                        std::min<std::size_t>(static_cast<std::size_t>(length), line.size() - 1));
                    DWORD bytes_written{};
                    WriteFile(file, line.data(), bytes_to_write, &bytes_written, nullptr);
                    FlushFileBuffers(file);
                }
                CloseHandle(file);
            }
            catch (...)
            {
                // This trace is a last-resort crash breadcrumb and must never escape into UE4SS.
            }
        }

        auto append_route_c_trace_failure(std::string_view event, std::string_view reason) noexcept
            -> void
        {
            try
            {
                std::string line{event};
                line += " reason=";
                for (const char character : reason)
                {
                    line.push_back(character == '\r' || character == '\n' ? ' ' : character);
                }
                append_route_c_trace(line);
            }
            catch (...)
            {
            }
        }

        auto write_file_atomically(const std::filesystem::path& path, std::string_view contents)
            -> void
        {
            if (contents.empty() || contents.size() > RouteCMaximumFileBytes)
            {
                throw std::runtime_error{"Route C checkpoint exceeds the 2 MiB safety limit"};
            }
            std::filesystem::create_directories(path.parent_path());
            auto temporary = path;
            temporary += STR(".tmp");
            auto backup = path;
            backup += STR(".bak");

            std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
            if (!output)
            {
                throw std::runtime_error{"Unable to open the temporary Route C checkpoint"};
            }
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output)
            {
                throw std::runtime_error{"Unable to finish writing the Route C checkpoint"};
            }
            output.close();

            std::error_code ignored{};
            if (std::filesystem::exists(path))
            {
                std::filesystem::copy_file(
                    path, backup, std::filesystem::copy_options::overwrite_existing, ignored);
            }
            if (!MoveFileExW(
                    temporary.c_str(),
                    path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::filesystem::remove(temporary, ignored);
                throw std::runtime_error{"Atomic Route C checkpoint replacement failed"};
            }
        }

        auto read_route_c_checkpoint() -> RouteCCheckpoint
        {
            append_route_c_trace("restore.read.begin");
            const auto path = route_c_checkpoint_path();
            std::error_code file_error{};
            const bool exists = std::filesystem::exists(path, file_error);
            if (file_error)
            {
                append_route_c_trace("restore.read.path-error");
                throw std::runtime_error{"The Route C checkpoint path could not be inspected"};
            }
            if (!exists)
            {
                append_route_c_trace("restore.read.missing");
                throw std::runtime_error{"No Route C checkpoint exists; save one first"};
            }

            const auto size = std::filesystem::file_size(path, file_error);
            if (file_error)
            {
                append_route_c_trace("restore.read.size-error");
                throw std::runtime_error{"The Route C checkpoint size could not be read"};
            }
            if (size == 0 || size > RouteCMaximumFileBytes)
            {
                append_route_c_trace("restore.read.invalid-size");
                throw std::runtime_error{"Route C checkpoint is empty or exceeds the 2 MiB limit"};
            }
            std::ifstream input{path, std::ios::binary};
            if (!input)
            {
                append_route_c_trace("restore.read.open-failed");
                throw std::runtime_error{"Unable to open the Route C checkpoint"};
            }
            std::string contents(static_cast<std::size_t>(size), '\0');
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!input)
            {
                append_route_c_trace("restore.read.incomplete");
                throw std::runtime_error{"Unable to read the complete Route C checkpoint"};
            }

            append_route_c_trace("restore.read.parse");
            std::string error{};
            auto checkpoint = parse_route_c_checkpoint(contents, error);
            if (!checkpoint)
            {
                append_route_c_trace("restore.read.rejected");
                throw std::runtime_error{"Route C checkpoint was rejected: " + error};
            }
            append_route_c_trace("restore.read.complete");
            return std::move(*checkpoint);
        }

        auto try_read_exact_spawn_plan_checkpoint(const RouteCCheckpoint& route_c,
                                                  std::string& reason)
            -> std::optional<ExactSpawnPlanCheckpoint>
        {
            try
            {
                const auto path = exact_spawn_plan_checkpoint_path();
                std::error_code file_error{};
                if (!std::filesystem::exists(path, file_error))
                {
                    reason = file_error
                        ? "exact spawn-plan path could not be inspected"
                        : "exact spawn-plan supplement does not exist";
                    append_route_c_trace_failure(
                        "restore.exact-spawn-plan.unavailable", reason);
                    return std::nullopt;
                }

                const auto size = std::filesystem::file_size(path, file_error);
                if (file_error || size == 0 || size > RouteCMaximumFileBytes)
                {
                    reason = "exact spawn-plan supplement has an invalid size";
                    append_route_c_trace_failure(
                        "restore.exact-spawn-plan.rejected", reason);
                    return std::nullopt;
                }

                std::ifstream input{path, std::ios::binary};
                if (!input)
                {
                    reason = "exact spawn-plan supplement could not be opened";
                    append_route_c_trace_failure(
                        "restore.exact-spawn-plan.rejected", reason);
                    return std::nullopt;
                }
                std::string contents(static_cast<std::size_t>(size), '\0');
                input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
                if (!input)
                {
                    reason = "exact spawn-plan supplement could not be read completely";
                    append_route_c_trace_failure(
                        "restore.exact-spawn-plan.rejected", reason);
                    return std::nullopt;
                }

                std::string parse_error{};
                auto exact = parse_exact_spawn_plan_checkpoint(contents, parse_error);
                if (!exact)
                {
                    reason = "exact spawn-plan supplement was rejected: " + parse_error;
                    append_route_c_trace_failure(
                        "restore.exact-spawn-plan.rejected", reason);
                    return std::nullopt;
                }
                if (exact->route_c_payload_checksum != route_c.payload_checksum
                    || exact->game_executable_sha256 != route_c.game_executable_sha256
                    || exact->game_executable_size != route_c.game_executable_size
                    || exact->source_level_name != route_c.source_level_name
                    || exact->wave_index != route_c.wave_index
                    || exact->spawner_class != route_c.spawner_class
                    || exact->spawner_class_size != route_c.spawner_class_size)
                {
                    reason = "exact spawn-plan supplement does not match the Route C checkpoint";
                    append_route_c_trace_failure(
                        "restore.exact-spawn-plan.stale", reason);
                    return std::nullopt;
                }

                reason = "linked exact spawn-plan supplement loaded";
                append_route_c_trace("restore.exact-spawn-plan.loaded");
                return exact;
            }
            catch (const std::exception& error)
            {
                reason = std::string{"exact spawn-plan supplement was ignored: "} + error.what();
                append_route_c_trace_failure(
                    "restore.exact-spawn-plan.rejected", reason);
                return std::nullopt;
            }
        }

        auto try_read_exact_player_zones_checkpoint(const RouteCCheckpoint& route_c,
                                                    std::string& reason)
            -> std::optional<ExactPlayerZonesCheckpoint>
        {
            try
            {
                const auto path = exact_player_zones_checkpoint_path();
                std::error_code file_error{};
                if (!std::filesystem::exists(path, file_error))
                {
                    reason = file_error
                        ? "exact player-zones path could not be inspected"
                        : "exact player-zones supplement does not exist";
                    append_route_c_trace_failure(
                        "restore.exact-player-zones.unavailable", reason);
                    return std::nullopt;
                }
                const auto size = std::filesystem::file_size(path, file_error);
                if (file_error || size == 0 || size > RouteCMaximumFileBytes)
                {
                    reason = "exact player-zones supplement has an invalid size";
                    append_route_c_trace_failure(
                        "restore.exact-player-zones.rejected", reason);
                    return std::nullopt;
                }
                std::ifstream input{path, std::ios::binary};
                if (!input)
                {
                    reason = "exact player-zones supplement could not be opened";
                    append_route_c_trace_failure(
                        "restore.exact-player-zones.rejected", reason);
                    return std::nullopt;
                }
                std::string contents(static_cast<std::size_t>(size), '\0');
                input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
                if (!input)
                {
                    reason = "exact player-zones supplement could not be read completely";
                    append_route_c_trace_failure(
                        "restore.exact-player-zones.rejected", reason);
                    return std::nullopt;
                }

                std::string parse_error{};
                auto exact = parse_exact_player_zones_checkpoint(contents, parse_error);
                if (!exact)
                {
                    reason = "exact player-zones supplement was rejected: " + parse_error;
                    append_route_c_trace_failure(
                        "restore.exact-player-zones.rejected", reason);
                    return std::nullopt;
                }
                if (exact->route_c_payload_checksum != route_c.payload_checksum
                    || exact->game_executable_sha256 != route_c.game_executable_sha256
                    || exact->game_executable_size != route_c.game_executable_size
                    || exact->source_level_name != route_c.source_level_name
                    || exact->wave_index != route_c.wave_index)
                {
                    reason = "exact player-zones supplement does not match the Route C checkpoint";
                    append_route_c_trace_failure(
                        "restore.exact-player-zones.stale", reason);
                    return std::nullopt;
                }
                reason = "linked exact player-zones supplement loaded";
                append_route_c_trace("restore.exact-player-zones.loaded");
                return exact;
            }
            catch (const std::exception& error)
            {
                reason = std::string{"exact player-zones supplement was ignored: "}
                    + error.what();
                append_route_c_trace_failure(
                    "restore.exact-player-zones.rejected", reason);
                return std::nullopt;
            }
        }

        auto parse_int32(std::string_view value) -> std::optional<std::int32_t>
        {
            std::int32_t parsed{};
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
            {
                return std::nullopt;
            }
            return parsed;
        }

        auto address_is_writable(const void* address, std::size_t size) -> bool
        {
            MEMORY_BASIC_INFORMATION memory{};
            if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory)
                || memory.State != MEM_COMMIT
                || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            {
                return false;
            }

            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            const auto region_begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
            const auto region_end = region_begin + memory.RegionSize;
            if (begin < region_begin || begin + size < begin || begin + size > region_end)
            {
                return false;
            }

            const auto protection = memory.Protect & 0xFF;
            return protection == PAGE_READWRITE
                || protection == PAGE_WRITECOPY
                || protection == PAGE_EXECUTE_READWRITE
                || protection == PAGE_EXECUTE_WRITECOPY;
        }

        auto address_is_readable(const void* address, std::size_t size) -> bool
        {
            MEMORY_BASIC_INFORMATION memory{};
            if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory)
                || memory.State != MEM_COMMIT
                || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            const auto region_begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
            const auto region_end = region_begin + memory.RegionSize;
            return begin >= region_begin && begin + size >= begin && begin + size <= region_end;
        }

        auto format_address(std::uintptr_t address) -> std::string
        {
            std::ostringstream stream{};
            stream << "0x" << std::hex << std::uppercase << address;
            return stream.str();
        }

        auto is_live_instance(std::string_view full_name, std::string_view role) -> bool
        {
            if (full_name.contains("Default__"))
            {
                return false;
            }

            if (role == "UMG_CardEffectDisplay_C")
            {
                return full_name.contains(":PersistentLevel.")
                    || full_name.contains("/Engine/Transient.");
            }

            if (!full_name.contains(":PersistentLevel."))
            {
                return false;
            }

            const bool child_actor_state = role == "BP_FieldSlot_C"
                || role == "BP_CardEffectDisplay_C"
                || role == "BP_GenericCounterDisplay_C"
                || role == "BP_SpecialCounterDisplay_C";
            return child_actor_state || !full_name.contains("_GEN_VARIABLE");
        }

        auto classify(std::string_view full_name) -> std::string
        {
            for (const auto prefix : RelevantClassPrefixes)
            {
                if (full_name.starts_with(prefix))
                {
                    return std::string{prefix.substr(0, prefix.size() - 1)};
                }
            }

            if (full_name.contains("Spawner_C "))
            {
                return "Spawner_C";
            }
            return {};
        }

        auto export_zero_argument_getter(UObject* object, StringViewType function_name)
            -> std::optional<PropertySnapshot>
        {
            auto* function = object->GetFunctionByNameInChain(function_name.data());
            if (!function)
            {
                return std::nullopt;
            }

            auto* return_property = function->GetReturnProperty();
            if (!return_property)
            {
                return std::nullopt;
            }

            for (auto* property : function->ForEachProperty())
            {
                if (property->HasAnyPropertyFlags(CPF_Parm)
                    && !property->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    return std::nullopt;
                }
            }

            const auto parameter_size = static_cast<std::size_t>(function->GetParmsSize());
            if (parameter_size == 0)
            {
                return std::nullopt;
            }

            Output::send<LogLevel::Verbose>(
                STR("[QuantumCheckpoint] Read-only call {} on {}\n"),
                function_name,
                object->GetFullName());

            ReflectedParameterBuffer parameters{function};
            object->ProcessEvent(function, parameters.bytes.data());

            FString exported{};
            auto* return_value = return_property->ContainerPtrToValuePtr<void>(
                parameters.bytes.data());
            return_property->ExportTextItem(exported, return_value, nullptr, object, 0);
            std::string value = to_string(exported.GetCharArray());
            if (value.empty() && return_property->IsA<FArrayProperty>())
            {
                // UE 4.27 exports an empty TArray return value as an empty string. Keep a
                // non-empty, importable representation so an empty storage/bench remains
                // distinguishable from a missing function or return property.
                value = "()";
            }

            return PropertySnapshot{
                .name = "getter:" + to_string(function_name),
                .value = std::move(value),
            };
        }

        auto export_function_pointer(UObject* object, StringViewType function_name)
            -> std::optional<PropertySnapshot>
        {
            auto* function = object->GetFunctionByNameInChain(function_name.data());
            if (!function)
            {
                return std::nullopt;
            }

            const auto address = reinterpret_cast<std::uintptr_t>(function->GetFuncPtr());
            std::ostringstream value{};
            value << "0x" << std::hex << std::uppercase << address;
            return PropertySnapshot{
                .name = "functionPointer:" + to_string(function_name),
                .value = value.str(),
            };
        }

        template <typename ValueType>
        auto read_native_value(const void* base, std::size_t offset) -> ValueType
        {
            ValueType value{};
            std::memcpy(
                &value,
                static_cast<const std::byte*>(base) + offset,
                sizeof(ValueType));
            return value;
        }

        template <typename ValueType>
        auto write_native_value(const void* base, std::size_t offset, const ValueType& value) -> void
        {
            std::memcpy(
                static_cast<std::byte*>(const_cast<void*>(base)) + offset,
                &value,
                sizeof(ValueType));
        }

        auto append_private_card_state_diagnostics(ObjectSnapshot& snapshot, UObject* object) -> void
        {
            const auto& fingerprint = executable_fingerprint();
            if (!fingerprint_matches_supported_game(fingerprint)
                || !address_is_readable(
                    static_cast<const std::byte*>(static_cast<const void*>(object))
                        + InGameCardStatePointerOffset,
                    sizeof(void*)))
            {
                snapshot.properties.push_back({
                    .name = "nativeDiagnostic:status",
                    .value = "refused; unsupported executable or unreadable card state pointer",
                });
                return;
            }
            const auto* state = read_native_value<const void*>(
                object,
                InGameCardStatePointerOffset);
            if (!state
                || !address_is_readable(
                    static_cast<const std::byte*>(state) + CardStateBaseHealthOffset,
                    sizeof(std::int32_t))
                || !address_is_readable(
                    static_cast<const std::byte*>(state) + CardStateCurrentHealthOffset,
                    sizeof(std::int32_t))
                || !address_is_readable(
                    static_cast<const std::byte*>(state) + CardStateTurnAdjustmentOffset,
                    sizeof(std::int32_t))
                || !address_is_readable(
                    static_cast<const std::byte*>(state) + CardStateTurnBaseOffset,
                    sizeof(std::int32_t)))
            {
                snapshot.properties.push_back({
                    .name = "nativeDiagnostic:stateObject",
                    .value = "null-or-unreadable",
                });
                return;
            }

            std::ostringstream state_address{};
            state_address << "0x" << std::hex << std::uppercase
                          << reinterpret_cast<std::uintptr_t>(state);
            snapshot.properties.push_back({
                .name = "nativeDiagnostic:stateObject",
                .value = state_address.str(),
            });

            const auto base_health = read_native_value<std::int32_t>(state, CardStateBaseHealthOffset);
            const auto current_health = read_native_value<std::int32_t>(state, CardStateCurrentHealthOffset);
            const auto turn_adjustment = read_native_value<std::int32_t>(state, CardStateTurnAdjustmentOffset);
            const auto turn_base = read_native_value<std::int32_t>(state, CardStateTurnBaseOffset);
            snapshot.properties.push_back({"nativeDiagnostic:baseHealth", std::to_string(base_health)});
            snapshot.properties.push_back({"nativeDiagnostic:currentHealth", std::to_string(current_health)});
            snapshot.properties.push_back({"nativeDiagnostic:turnAdjustment", std::to_string(turn_adjustment)});
            snapshot.properties.push_back({"nativeDiagnostic:turnBase", std::to_string(turn_base)});
            snapshot.properties.push_back({
                "nativeDiagnostic:computedTurnCounter",
                std::to_string(turn_adjustment + turn_base),
            });
        }

        template <std::size_t Size>
        auto append_getters(ObjectSnapshot& snapshot, UObject* object,
                            const std::array<StringViewType, Size>& getters) -> void
        {
            for (const auto getter : getters)
            {
                if (auto exported = export_zero_argument_getter(object, getter))
                {
                    snapshot.properties.push_back(std::move(*exported));
                }
                if (auto pointer = export_function_pointer(object, getter))
                {
                    snapshot.properties.push_back(std::move(*pointer));
                }
            }
        }

        auto required_text(std::optional<std::string> value, std::string_view label)
            -> std::string
        {
            if (!value || value->empty())
            {
                throw std::runtime_error{
                    "Required Route C value was not available: " + std::string{label}};
            }
            return std::move(*value);
        }

        auto required_getter_text(UObject* object, StringViewType function_name) -> std::string
        {
            auto value = export_zero_argument_getter(object, function_name);
            if (!value || value->value.empty())
            {
                throw std::runtime_error{
                    "Required Route C getter was not available: " + to_string(function_name)};
            }
            return std::move(value->value);
        }

        auto capture_route_c_checkpoint(std::optional<PendingRouteCCapture> expected = std::nullopt)
            -> std::filesystem::path
        {
            append_route_c_trace("capture.begin");
            if (g_pending_route_c_restore || g_pending_health_write_probe
                || g_pending_turn_write_probe)
            {
                throw std::runtime_error{"A restore or native write probe is already active"};
            }

            const auto objects = find_route_c_objects();
            append_route_c_trace(objects.game_instance
                                     ? "capture.object.game-instance.found"
                                     : "capture.object.game-instance.missing");
            append_route_c_trace(objects.card_engine
                                     ? "capture.object.card-engine.found"
                                     : "capture.object.card-engine.missing");
            append_route_c_trace(objects.spawner
                                     ? "capture.object.spawn-controller.found"
                                     : "capture.object.spawn-controller.missing");
            append_route_c_trace(objects.bottom_bar
                                     ? "capture.object.bottom-bar.found"
                                     : "capture.object.bottom-bar.missing");
            if (!objects.game_instance || !objects.card_engine || !objects.spawner
                || !objects.bottom_bar)
            {
                throw std::runtime_error{"A complete active battle was not found"};
            }
            if (expected && expected->spawner != objects.spawner)
            {
                throw std::runtime_error{"The wave-start spawner was replaced before capture"};
            }
            if (const auto unsafe_companion = find_route_c_unsafe_companion())
            {
                throw std::runtime_error{
                    "Route C refuses a tutorial or boss encounter companion: "
                    + *unsafe_companion};
            }

            const auto game_state = required_text(
                export_property_text(objects.card_engine, STR("currentGameState")),
                "CardEngine.currentGameState");
            if (game_state != "OPEN")
            {
                throw std::runtime_error{"The battle is not in the stable OPEN state"};
            }
            const auto active_selection = required_text(
                export_property_text(objects.card_engine, STR("mActiveCardSelectionPrompt")),
                "CardEngine.mActiveCardSelectionPrompt");
            const auto active_placement = required_text(
                export_property_text(objects.card_engine, STR("mActiveCardPlacementPrompt")),
                "CardEngine.mActiveCardPlacementPrompt");
            if (active_selection != "None" || active_placement != "None")
            {
                throw std::runtime_error{"A card prompt is active; checkpoint capture is not stable"};
            }

            RouteCCheckpoint checkpoint{};
            checkpoint.captured_at_utc = utc_timestamp();
            const auto& fingerprint = executable_fingerprint();
            if (!fingerprint_matches_supported_game(fingerprint))
            {
                throw std::runtime_error{
                    "Route C is disabled for this unvalidated game executable"};
            }
            checkpoint.game_executable_sha256 = fingerprint.sha256;
            checkpoint.game_executable_size = fingerprint.size;
            checkpoint.source_level_name = required_text(
                export_property_text(objects.game_instance, STR("sourceLevelName")),
                "GameInstance.sourceLevelName");
            checkpoint.active_character_info = required_text(
                export_property_text(objects.game_instance, STR("activeCharacterInfo")),
                "GameInstance.activeCharacterInfo");
            checkpoint.active_stage_info = required_text(
                export_property_text(objects.game_instance, STR("activeStageInfo")),
                "GameInstance.activeStageInfo");
            if (!checkpoint.active_stage_info.contains("Type=DUNGEON")
                || checkpoint.active_stage_info.contains("Type=DUNGEON_EVENT"))
            {
                throw std::runtime_error{
                    "Route C supports ordinary DUNGEON battles only"};
            }

            append_route_c_trace("capture.get-active-decklist.begin");
            checkpoint.active_decklist = route_c_startup_decklist(
                required_getter_text(objects.game_instance, STR("getActiveDecklist")));
            append_route_c_trace("capture.get-active-decklist.complete");
            append_route_c_trace("capture.get-active-storage.begin");
            checkpoint.active_storage = required_getter_text(
                objects.game_instance, STR("getActiveStorage"));
            append_route_c_trace("capture.get-active-storage.complete");
            append_route_c_trace("capture.get-loot-drops.begin");
            checkpoint.loot_drops = required_getter_text(
                objects.game_instance, STR("getLootDrops"));
            append_route_c_trace("capture.get-loot-drops.complete");
            append_route_c_trace("capture.get-current-deck-run.begin");
            checkpoint.deck_run = required_getter_text(
                objects.game_instance, STR("getCurrentDeckRun"));
            append_route_c_trace("capture.get-current-deck-run.complete");

            const auto health_text = required_getter_text(
                objects.card_engine, STR("getCurrentHealth"));
            const auto max_health_text = required_getter_text(
                objects.card_engine, STR("getMaxHealth"));
            const auto health = parse_int32(health_text);
            const auto max_health = parse_int32(max_health_text);
            if (!health || !max_health)
            {
                throw std::runtime_error{"Player health getters did not return integers"};
            }
            checkpoint.player_health = *health;
            checkpoint.player_max_health = *max_health;

            const auto wave_text = required_text(
                export_property_text(objects.spawner, STR("currentWaveIndex")),
                "SpawnController.currentWaveIndex");
            const auto wave = parse_int32(wave_text);
            if (!wave)
            {
                throw std::runtime_error{"Spawner wave index was not an integer"};
            }
            checkpoint.wave_index = *wave;
            if (expected && checkpoint.wave_index != expected->wave_index)
            {
                throw std::runtime_error{"Spawner wave changed before automatic capture"};
            }

            checkpoint.spawner_class = object_class_name(objects.spawner);
            checkpoint.spawner_class_size = static_cast<std::uint32_t>(
                objects.spawner->GetClassPrivate()->GetPropertiesSize());
            checkpoint.payload_checksum = route_c_payload_checksum(checkpoint);
            std::string validation_error{};
            if (!validate_route_c_checkpoint(checkpoint, validation_error))
            {
                throw std::runtime_error{
                    "Route C checkpoint validation failed: " + validation_error};
            }

            std::optional<ExactSpawnPlanCheckpoint> exact_spawn_plan{};
            try
            {
                append_route_c_trace("capture.exact-spawn-plan.prepare.begin");
                ExactSpawnPlanCheckpoint exact{};
                exact.captured_at_utc = checkpoint.captured_at_utc;
                exact.route_c_payload_checksum = checkpoint.payload_checksum;
                exact.game_executable_sha256 = checkpoint.game_executable_sha256;
                exact.game_executable_size = checkpoint.game_executable_size;
                exact.source_level_name = checkpoint.source_level_name;
                exact.wave_index = checkpoint.wave_index;
                exact.spawner_class = checkpoint.spawner_class;
                exact.spawner_class_size = checkpoint.spawner_class_size;
                exact.spawn_list = required_text(
                    export_property_text(objects.spawner, STR("spawnList")),
                    "SpawnController.spawnList");
                exact.payload_checksum = exact_spawn_plan_payload_checksum(exact);
                std::string exact_validation_error{};
                if (!validate_exact_spawn_plan_checkpoint(exact, exact_validation_error))
                {
                    throw std::runtime_error{exact_validation_error};
                }
                exact_spawn_plan = std::move(exact);
                append_route_c_trace("capture.exact-spawn-plan.prepare.complete");
            }
            catch (const std::exception& error)
            {
                // Route C is the stable fallback. Failure to capture its optional exact
                // supplement must not prevent the semantic checkpoint from being committed.
                append_route_c_trace_failure(
                    "capture.exact-spawn-plan.skipped", error.what());
            }

            std::optional<ExactPlayerZonesCheckpoint> exact_player_zones{};
            try
            {
                append_route_c_trace("capture.exact-player-zones.prepare.begin");
                const auto zones = find_route_c_player_zone_objects(
                    static_cast<const void*>(objects.card_engine->GetWorld()));
                if (!zones.deck || !zones.hand)
                {
                    throw std::runtime_error{
                        "live player deck and hand controllers were not found"};
                }
                append_route_c_trace("capture.exact-player-zones.controllers.found");
                ExactPlayerZonesCheckpoint exact{};
                exact.captured_at_utc = checkpoint.captured_at_utc;
                exact.route_c_payload_checksum = checkpoint.payload_checksum;
                exact.game_executable_sha256 = checkpoint.game_executable_sha256;
                exact.game_executable_size = checkpoint.game_executable_size;
                exact.source_level_name = checkpoint.source_level_name;
                exact.wave_index = checkpoint.wave_index;
                append_route_c_trace("capture.exact-player-zones.get-deck.begin");
                exact.player_deck = required_getter_text(
                    zones.deck, STR("getCardInstanceListSorted"));
                append_route_c_trace("capture.exact-player-zones.get-deck.complete");
                append_route_c_trace("capture.exact-player-zones.get-hand.begin");
                exact.player_hand = required_getter_text(
                    zones.hand, STR("getCardInstanceListSorted"));
                append_route_c_trace("capture.exact-player-zones.get-hand.complete");
                exact.payload_checksum = exact_player_zones_payload_checksum(exact);
                std::string exact_validation_error{};
                if (!validate_exact_player_zones_checkpoint(exact, exact_validation_error))
                {
                    throw std::runtime_error{exact_validation_error};
                }
                if (!exact_player_zones_startup_decklist(
                        checkpoint.active_decklist,
                        exact.player_deck,
                        exact.player_hand,
                        exact_validation_error))
                {
                    throw std::runtime_error{exact_validation_error};
                }
                exact_player_zones = std::move(exact);
                append_route_c_trace("capture.exact-player-zones.prepare.complete");
            }
            catch (const std::exception& error)
            {
                append_route_c_trace_failure(
                    "capture.exact-player-zones.skipped", error.what());
            }

            const auto path = route_c_checkpoint_path();
            append_route_c_trace("capture.write.begin");
            write_file_atomically(path, serialize_route_c_checkpoint(checkpoint));
            append_route_c_trace("capture.complete");
            if (exact_spawn_plan)
            {
                try
                {
                    append_route_c_trace("capture.exact-spawn-plan.write.begin");
                    write_file_atomically(
                        exact_spawn_plan_checkpoint_path(),
                        serialize_exact_spawn_plan_checkpoint(std::move(*exact_spawn_plan)));
                    append_route_c_trace("capture.exact-spawn-plan.write.complete");
                }
                catch (const std::exception& error)
                {
                    append_route_c_trace_failure(
                    "capture.exact-spawn-plan.write.failed", error.what());
                }
            }
            if (exact_player_zones)
            {
                try
                {
                    append_route_c_trace("capture.exact-player-zones.write.begin");
                    write_file_atomically(
                        exact_player_zones_checkpoint_path(),
                        serialize_exact_player_zones_checkpoint(
                            std::move(*exact_player_zones)));
                    append_route_c_trace("capture.exact-player-zones.write.complete");
                }
                catch (const std::exception& error)
                {
                    append_route_c_trace_failure(
                        "capture.exact-player-zones.write.failed", error.what());
                }
            }
            return path;
        }

        auto write_route_c_restore_report(const PendingRouteCRestore& restore)
            -> std::filesystem::path
        {
            const auto mods_directory = std::filesystem::path{
                UE4SSProgram::get_program().get_mods_directory()};
            const auto path = mods_directory / STR("QuantumCheckpoint") / STR("Reports") /
                (STR("route-c-restore-") + to_wstring(filename_timestamp()) + STR(".json"));
            std::ostringstream output{};
            output << "{\n"
                   << "  \"schemaVersion\": 1,\n"
                   << "  \"kind\": \"route-c-restore-report\",\n"
                   << "  \"capturedAtUtc\": \"" << json_escape(utc_timestamp()) << "\",\n"
                   << "  \"status\": \"" << json_escape(restore.status) << "\",\n"
                   << "  \"reason\": \"" << json_escape(restore.reason) << "\",\n"
                   << "  \"sourceLevelName\": \""
                   << json_escape(restore.checkpoint.source_level_name) << "\",\n"
                   << "  \"waveIndex\": " << restore.checkpoint.wave_index << ",\n"
                   << "  \"spawnerClass\": \""
                   << json_escape(restore.checkpoint.spawner_class) << "\",\n"
                   << "  \"lootDropCount\": " << restore.loot_drops.size() << ",\n"
                   << "  \"gameInstanceReimportedAtCardEngineBeginPlay\": "
                   << (restore.game_instance_reimported_at_card_engine_begin_play
                           ? "true"
                           : "false")
                   << ",\n"
                   << "  \"gameInstanceReimportedAtBeginPlay\": "
                   << (restore.game_instance_reimported_at_begin_play ? "true" : "false")
                   << ",\n"
                   << "  \"characterCardSlotObserved\": "
                   << (restore.character_card_slot_observed ? "true" : "false") << ",\n"
                   << "  \"characterCardCharge\": ";
            if (restore.character_card_charge)
            {
                output << *restore.character_card_charge;
            }
            else
            {
                output << "null";
            }
            output << ",\n  \"characterCardChargeRequirement\": ";
            if (restore.character_card_charge_requirement)
            {
                output << *restore.character_card_charge_requirement;
            }
            else
            {
                output << "null";
            }
            output << ",\n  \"characterAbilityOk\": ";
            if (restore.character_ability_ok)
            {
                output << "\"" << json_escape(*restore.character_ability_ok) << "\"";
            }
            else
            {
                output << "null";
            }
            output << ",\n"
                   << "  \"exactSpawnPlanSupplementPresent\": "
                   << (restore.exact_spawn_plan ? "true" : "false") << ",\n"
                   << "  \"exactSpawnPlanStatus\": \""
                   << json_escape(restore.exact_spawn_plan_status) << "\",\n"
                   << "  \"exactSpawnPlanReason\": \""
                   << json_escape(restore.exact_spawn_plan_reason) << "\",\n"
                   << "  \"exactPlayerZonesSupplementPresent\": "
                   << (restore.exact_player_zones ? "true" : "false") << ",\n"
                   << "  \"exactPlayerZonesStatus\": \""
                   << json_escape(restore.exact_player_zones_status) << "\",\n"
                   << "  \"exactPlayerZonesReason\": \""
                   << json_escape(restore.exact_player_zones_reason) << "\",\n"
                   << "  \"activeDecklistRestoredAfterExactStartup\": "
                   << (restore.active_decklist_restored_after_exact_startup
                           ? "true" : "false") << ",\n"
                   << "  \"targetHealth\": " << restore.checkpoint.player_health << "\n"
                   << "}\n";
            write_file_atomically(path, output.str());
            return path;
        }

        auto finish_route_c_restore(std::string status, std::string reason) -> void
        {
            if (!g_pending_route_c_restore)
            {
                return;
            }
            auto completed = std::move(*g_pending_route_c_restore);
            completed.status = std::move(status);
            completed.reason = std::move(reason);

            if (completed.auto_spawn_suppressed && !completed.original_auto_spawn.empty())
            {
                try
                {
                    const auto live_objects = find_route_c_objects();
                    if (!live_objects.spawner
                        || object_class_name(live_objects.spawner)
                            != completed.checkpoint.spawner_class)
                    {
                        throw std::runtime_error{
                            "the controlled SpawnController is no longer the live battle object"};
                    }
                    import_property_text(live_objects.spawner,
                                         STR("autoSpawn"),
                                         completed.original_auto_spawn);
                    completed.auto_spawn_suppressed = false;
                }
                catch (...)
                {
                    if (completed.status == "passed")
                    {
                        completed.status = "failed";
                        completed.reason =
                            "restore completed but SpawnController.autoSpawn could not be restored";
                    }
                }
            }

            // The replacement battle must not overwrite the checkpoint merely because its
            // Spawner is a new object. The next stable observation becomes the baseline; a
            // subsequent genuine wave change can auto-save normally.
            g_adopt_next_route_c_wave_without_capture = true;
            if (completed.status == "passed")
            {
                // A passed restore has already verified the live deck and storage against this
                // exact payload. Reuse that known-good state as rollback input for another load
                // in the same process; repeatedly calling getActiveDecklist here can deadlock in
                // the game's stale post-travel DeckRun state.
                g_last_completed_route_c_checkpoint = completed.checkpoint;
            }
            g_pending_route_c_restore.reset();
            try
            {
                const auto path = write_route_c_restore_report(completed);
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Route C restore {}: {}; report: {}\n"),
                    to_wstring(completed.status),
                    to_wstring(completed.reason),
                    path.wstring());
            }
            catch (const std::exception& error)
            {
                Output::send<LogLevel::Error>(
                    STR("[QuantumCheckpoint] Route C restore {} but its report failed: {}\n"),
                    to_wstring(completed.status),
                    to_wstring(error.what()));
            }
        }

        auto begin_route_c_restore() -> void
        {
            append_route_c_trace("restore.begin");
            if (g_pending_route_c_restore || g_pending_health_write_probe
                || g_pending_turn_write_probe)
            {
                throw std::runtime_error{"A restore or native write probe is already active"};
            }
            auto checkpoint = read_route_c_checkpoint();
            append_route_c_trace("restore.checkpoint-loaded");
            const auto& fingerprint = executable_fingerprint();
            if (!fingerprint_matches_supported_game(fingerprint)
                || checkpoint.game_executable_size != fingerprint.size
                || checkpoint.game_executable_sha256 != fingerprint.sha256)
            {
                throw std::runtime_error{
                    "Route C checkpoint belongs to a different game executable"};
            }
            std::string exact_spawn_plan_reason{};
            auto exact_spawn_plan = try_read_exact_spawn_plan_checkpoint(
                checkpoint, exact_spawn_plan_reason);
            std::string exact_player_zones_reason{};
            auto exact_player_zones = try_read_exact_player_zones_checkpoint(
                checkpoint, exact_player_zones_reason);

            const auto objects = find_route_c_objects();
            if (!objects.game_instance)
            {
                throw std::runtime_error{"The live Quantum GameInstance was not found"};
            }

            const auto original_character = required_text(
                export_property_text(objects.game_instance, STR("activeCharacterInfo")),
                "original GameInstance.activeCharacterInfo");
            const auto original_stage = required_text(
                export_property_text(objects.game_instance, STR("activeStageInfo")),
                "original GameInstance.activeStageInfo");
            const auto original_source_level = required_text(
                export_property_text(objects.game_instance, STR("sourceLevelName")),
                "original GameInstance.sourceLevelName");
            std::string original_deck{};
            std::string original_storage{};
            if (g_last_completed_route_c_checkpoint
                && g_last_completed_route_c_checkpoint->payload_checksum
                    == checkpoint.payload_checksum)
            {
                original_deck = g_last_completed_route_c_checkpoint->active_decklist;
                original_storage = g_last_completed_route_c_checkpoint->active_storage;
                append_route_c_trace(
                    "restore.original-run-state.reused-last-verified-checkpoint");
            }
            else
            {
                append_route_c_trace("restore.original-deck.begin");
                original_deck = required_getter_text(
                    objects.game_instance, STR("getActiveDecklist"));
                append_route_c_trace("restore.original-deck.complete");
                append_route_c_trace("restore.original-storage.begin");
                original_storage = required_getter_text(
                    objects.game_instance, STR("getActiveStorage"));
                append_route_c_trace("restore.original-storage.complete");
            }
            auto startup_decklist = route_c_startup_decklist(checkpoint.active_decklist);
            if (exact_player_zones)
            {
                std::string fixed_order_error{};
                auto fixed_order = exact_player_zones_startup_decklist(
                    checkpoint.active_decklist,
                    exact_player_zones->player_deck,
                    exact_player_zones->player_hand,
                    fixed_order_error);
                if (fixed_order)
                {
                    startup_decklist = std::move(*fixed_order);
                    exact_player_zones_reason =
                        "linked exact player zones prepared a fixed-order startup deck";
                    append_route_c_trace("restore.exact-player-zones.prepared");
                }
                else
                {
                    exact_player_zones_reason =
                        "exact player-zones supplement was ignored: " + fixed_order_error;
                    append_route_c_trace_failure(
                        "restore.exact-player-zones.rejected",
                        exact_player_zones_reason);
                    exact_player_zones.reset();
                }
            }
            std::string loot_error{};
            auto loot_drops = split_route_c_unreal_array(checkpoint.loot_drops, loot_error);
            if (!loot_drops)
            {
                throw std::runtime_error{
                    "Route C checkpoint lootDrops could not be prepared: " + loot_error};
            }

            const auto rollback_game_instance = [&]() {
                const auto attempt = [](auto&& operation) {
                    try
                    {
                        operation();
                    }
                    catch (...)
                    {
                    }
                };
                attempt([&]() {
                    import_property_text(
                        objects.game_instance, STR("activeCharacterInfo"), original_character);
                });
                attempt([&]() {
                    import_property_text(
                        objects.game_instance, STR("activeStageInfo"), original_stage);
                });
                attempt([&]() {
                    import_property_text(
                        objects.game_instance, STR("sourceLevelName"), original_source_level);
                });
                attempt([&]() {
                    call_reflected(objects.game_instance,
                                   STR("setActiveDecklist"),
                                   {{STR("newDecklist"), original_deck}});
                });
                attempt([&]() {
                    call_reflected(objects.game_instance,
                                   STR("updateBench"),
                                   {{STR("newCards"), original_storage}});
                });
            };

            try
            {
                append_route_c_trace("restore.game-instance-import.begin");
                import_property_text(objects.game_instance,
                                     STR("activeCharacterInfo"),
                                     checkpoint.active_character_info);
                import_property_text(objects.game_instance,
                                     STR("activeStageInfo"),
                                     checkpoint.active_stage_info);
                import_property_text(objects.game_instance,
                                     STR("sourceLevelName"),
                                     checkpoint.source_level_name);
                call_reflected(objects.game_instance,
                               STR("setActiveDecklist"),
                               {{STR("newDecklist"), startup_decklist}});
                if (startup_decklist != checkpoint.active_decklist)
                {
                    append_route_c_trace(
                        "restore.startup-deck.dungeon-tools-suppressed");
                }
                call_reflected(objects.game_instance,
                               STR("updateBench"),
                               {{STR("newCards"), checkpoint.active_storage}});
                append_route_c_trace("restore.game-instance-import.complete");
            }
            catch (...)
            {
                rollback_game_instance();
                throw;
            }

            const auto now = std::chrono::steady_clock::now();
            const bool exact_spawn_plan_available = exact_spawn_plan.has_value();
            const bool exact_player_zones_available = exact_player_zones.has_value();
            g_pending_route_c_capture.reset();
            g_pending_route_c_restore.emplace(PendingRouteCRestore{
                .checkpoint = std::move(checkpoint),
                .exact_spawn_plan = std::move(exact_spawn_plan),
                .exact_player_zones = std::move(exact_player_zones),
                .exact_player_startup_decklist = startup_decklist,
                .loot_drops = std::move(*loot_drops),
                .phase = RouteCRestorePhase::AwaitingWaveIntercept,
                .started_at = now,
                .phase_ready_at = now,
                .exact_spawn_plan_status = exact_spawn_plan_available
                    ? "pending" : "unavailable",
                .exact_spawn_plan_reason = std::move(exact_spawn_plan_reason),
                .exact_player_zones_status = exact_player_zones_available
                    ? "pending" : "unavailable",
                .exact_player_zones_reason = std::move(exact_player_zones_reason),
            });

            try
            {
                append_route_c_trace("restore.reload-battle-area.begin");
                call_reflected(objects.game_instance, STR("reloadBattleArea"));
                g_route_c_travel_occurred_in_process = true;
                append_route_c_trace("restore.reload-battle-area.returned");
            }
            catch (...)
            {
                g_pending_route_c_restore.reset();
                rollback_game_instance();
                throw;
            }
            Output::send<LogLevel::Verbose>(
                STR("[QuantumCheckpoint] Route C reload requested; waiting for native startup to select wave {}.\n"),
                g_pending_route_c_restore->checkpoint.wave_index);
        }

        auto apply_route_c_health(const RouteCCheckpoint& checkpoint,
                                  const RouteCBattleObjects& objects) -> void
        {
            const auto current_text = required_getter_text(
                objects.card_engine, STR("getCurrentHealth"));
            const auto max_text = required_getter_text(objects.card_engine, STR("getMaxHealth"));
            const auto current = parse_int32(current_text);
            const auto maximum = parse_int32(max_text);
            if (!current || !maximum || *maximum <= 0)
            {
                throw std::runtime_error{"Restored player health getters were invalid"};
            }
            const auto target = std::clamp(checkpoint.player_health, 1, *maximum);
            if (target > *current)
            {
                const auto amount = std::to_string(target - *current);
                call_reflected(
                    objects.card_engine, STR("healHealth"), {{STR("Amount"), amount}});
            }
            else if (target < *current)
            {
                if (!fingerprint_matches_supported_game(executable_fingerprint()))
                {
                    throw std::runtime_error{
                        "Downward player-health restore is disabled for this game executable"};
                }
                const auto module = GetModuleHandleW(L"Quantum-Win64-Shipping.exe");
                auto* health_getter = objects.card_engine->GetFunctionByNameInChain(
                    STR("getCurrentHealth"));
                auto* max_health_getter = objects.card_engine->GetFunctionByNameInChain(
                    STR("getMaxHealth"));
                if (!module || !health_getter || !max_health_getter
                    || reinterpret_cast<std::uintptr_t>(health_getter->GetFuncPtr())
                        != reinterpret_cast<std::uintptr_t>(module)
                            + CardEngineCurrentHealthGetterThunkRva
                    || reinterpret_cast<std::uintptr_t>(max_health_getter->GetFuncPtr())
                        != reinterpret_cast<std::uintptr_t>(module)
                            + CardEngineMaxHealthGetterThunkRva
                    || objects.card_engine->GetClassPrivate()->GetPropertiesSize()
                        < CardEngineHealthStatePointerOffset + sizeof(void*)
                    || !address_is_readable(
                        static_cast<const std::byte*>(
                            static_cast<const void*>(objects.card_engine))
                            + CardEngineHealthStatePointerOffset,
                        sizeof(void*)))
                {
                    throw std::runtime_error{
                        "Player-health native state layout did not pass the executable gate"};
                }

                const auto* state = read_native_value<const void*>(
                    objects.card_engine, CardEngineHealthStatePointerOffset);
                if (!state
                    || !address_is_writable(
                        static_cast<const std::byte*>(state) + PlayerStateCurrentHealthOffset,
                        sizeof(std::int32_t))
                    || !address_is_readable(
                        static_cast<const std::byte*>(state) + PlayerStateMaxHealthOffset,
                        sizeof(std::int32_t)))
                {
                    throw std::runtime_error{
                        "Player-health native state is null or not safely accessible"};
                }

                const auto native_current = read_native_value<std::int32_t>(
                    state, PlayerStateCurrentHealthOffset);
                const auto native_maximum = read_native_value<std::int32_t>(
                    state, PlayerStateMaxHealthOffset);
                if (native_current != *current || native_maximum != *maximum)
                {
                    append_route_c_trace_failure(
                        "restore.player-health.native-state-mismatch",
                        "getter=" + std::to_string(*current) + "/"
                            + std::to_string(*maximum) + " native="
                            + std::to_string(native_current) + "/"
                            + std::to_string(native_maximum));
                    throw std::runtime_error{
                        "Player-health native state disagrees with the reflected getters"};
                }

                const auto restore_original_health = [&]() {
                    write_native_value(state, PlayerStateCurrentHealthOffset, native_current);
                    try
                    {
                        import_property_text(objects.bottom_bar,
                                             STR("currentHealth"),
                                             std::to_string(native_current));
                        call_reflected(objects.card_engine,
                                       STR("OnHealthChanged"),
                                       {{STR("Delta"), "0"},
                                        {STR("changeType"), "IN_GAME"}});
                    }
                    catch (...)
                    {
                    }
                };

                try
                {
                    write_native_value(state, PlayerStateCurrentHealthOffset, target);
                    import_property_text(
                        objects.bottom_bar, STR("currentHealth"), std::to_string(target));
                    // The authoritative value is already set. A zero-delta notification asks the
                    // Blueprint/UI layer to refresh without applying damage a second time.
                    call_reflected(objects.card_engine,
                                   STR("OnHealthChanged"),
                                   {{STR("Delta"), "0"},
                                    {STR("changeType"), "IN_GAME"}});

                    const auto verified_health = parse_int32(required_getter_text(
                        objects.card_engine, STR("getCurrentHealth")));
                    const auto verified_bottom_bar = parse_int32(required_text(
                        export_property_text(objects.bottom_bar, STR("currentHealth")),
                        "BottomBar.currentHealth after restore"));
                    if (verified_health != target || verified_bottom_bar != target)
                    {
                        throw std::runtime_error{
                            "Downward player-health restore did not verify in native state and UI"};
                    }
                }
                catch (...)
                {
                    restore_original_health();
                    throw;
                }
            }
        }

        auto verify_exact_spawn_plan_or_rollback(PendingRouteCRestore& restore,
                                                 UObject* spawner) -> void
        {
            if (!restore.exact_spawn_plan
                || (restore.exact_spawn_plan_status != "applied"
                    && restore.exact_spawn_plan_status != "verified"))
            {
                return;
            }

            const auto live_spawn_list = export_property_text(spawner, STR("spawnList"));
            if (live_spawn_list
                && *live_spawn_list == restore.exact_spawn_plan->spawn_list)
            {
                if (restore.exact_spawn_plan_status != "verified")
                {
                    restore.exact_spawn_plan_status = "verified";
                    restore.exact_spawn_plan_reason =
                        "exact future spawn plan remained equal after native wave startup";
                    append_route_c_trace("restore.exact-spawn-plan.verified");
                }
                return;
            }

            const std::string failure = live_spawn_list
                ? "exact spawnList changed after import"
                : "exact spawnList was unavailable during verification";
            if (restore.original_spawn_list.empty())
            {
                throw std::runtime_error{
                    "exact spawn-plan verification failed without rollback material"};
            }

            import_property_text(
                spawner, STR("spawnList"), restore.original_spawn_list);
            const auto rolled_back = required_text(
                export_property_text(spawner, STR("spawnList")),
                "rolled-back SpawnController.spawnList");
            if (rolled_back != restore.original_spawn_list)
            {
                throw std::runtime_error{
                    "exact spawn-plan verification failed and rollback did not verify"};
            }
            restore.exact_spawn_plan_status = "failed-rolled-back";
            restore.exact_spawn_plan_reason = failure;
            append_route_c_trace_failure(
                "restore.exact-spawn-plan.verification-failed-rolled-back", failure);
        }

        auto try_apply_exact_spawn_plan(PendingRouteCRestore& restore,
                                        UObject* spawner) -> bool
        {
            if (!restore.exact_spawn_plan
                || restore.exact_spawn_plan_status != "pending")
            {
                return true;
            }

            const auto original = export_property_text(spawner, STR("spawnList"));
            if (!original || original->empty())
            {
                return false;
            }

            restore.original_spawn_list = *original;
            try
            {
                append_route_c_trace("restore.exact-spawn-plan.import.begin");
                import_property_text(
                    spawner, STR("spawnList"), restore.exact_spawn_plan->spawn_list);
                const auto verified = required_text(
                    export_property_text(spawner, STR("spawnList")),
                    "imported SpawnController.spawnList");
                if (verified != restore.exact_spawn_plan->spawn_list)
                {
                    throw std::runtime_error{
                        "imported exact spawnList did not verify"};
                }
                restore.exact_spawn_plan_status = "applied";
                restore.exact_spawn_plan_reason =
                    "exact future spawn plan imported before controlled wave startup";
                append_route_c_trace("restore.exact-spawn-plan.import.complete");
                return true;
            }
            catch (const std::exception& error)
            {
                try
                {
                    import_property_text(
                        spawner, STR("spawnList"), restore.original_spawn_list);
                    const auto rolled_back = required_text(
                        export_property_text(spawner, STR("spawnList")),
                        "rolled-back SpawnController.spawnList");
                    if (rolled_back != restore.original_spawn_list)
                    {
                        throw std::runtime_error{"spawnList rollback did not verify"};
                    }
                    restore.exact_spawn_plan_status = "failed-rolled-back";
                    restore.exact_spawn_plan_reason = error.what();
                    append_route_c_trace_failure(
                        "restore.exact-spawn-plan.failed-rolled-back",
                        restore.exact_spawn_plan_reason);
                    return true;
                }
                catch (const std::exception& rollback_error)
                {
                    restore.exact_spawn_plan_status = "rollback-failed";
                    restore.exact_spawn_plan_reason =
                        std::string{error.what()} + "; rollback: "
                        + rollback_error.what();
                    throw std::runtime_error{
                        "exact spawn-plan write could not be rolled back"};
                }
            }
        }

        auto verify_exact_player_zones(PendingRouteCRestore& restore,
                                       const RouteCBattleObjects& objects,
                                       std::chrono::steady_clock::time_point now) -> bool
        {
            if (!restore.exact_player_zones
                || restore.exact_player_zones_status != "pending")
            {
                return true;
            }

            const auto fail_without_zone_write = [&](std::string reason) {
                if (now - restore.started_at <= std::chrono::seconds{5})
                {
                    return false;
                }
                call_reflected(objects.game_instance,
                               STR("setActiveDecklist"),
                               {{STR("newDecklist"), restore.checkpoint.active_decklist}});
                restore.active_decklist_restored_after_exact_startup = true;
                restore.exact_player_zones_status = "failed-no-zone-observation";
                restore.exact_player_zones_reason = std::move(reason);
                append_route_c_trace_failure(
                    "restore.exact-player-zones.failed-no-zone-observation",
                    restore.exact_player_zones_reason);
                return true;
            };

            const auto zones = find_route_c_player_zone_objects(
                static_cast<const void*>(objects.card_engine->GetWorld()));
            if (!zones.deck || !zones.hand)
            {
                return fail_without_zone_write(
                    "player deck and hand controllers were unavailable after five seconds");
            }
            const auto actual_deck = export_zero_argument_getter(
                zones.deck, STR("getCardInstanceListSorted"));
            const auto actual_hand = export_zero_argument_getter(
                zones.hand, STR("getCardInstanceListSorted"));
            if (!actual_deck || !actual_hand)
            {
                return fail_without_zone_write(
                    "player deck and hand getters were unavailable after five seconds");
            }

            std::string array_error{};
            const auto expected_deck = split_route_c_unreal_array(
                restore.exact_player_zones->player_deck, array_error);
            const auto expected_hand = split_route_c_unreal_array(
                restore.exact_player_zones->player_hand, array_error);
            const auto live_deck = split_route_c_unreal_array(
                actual_deck->value, array_error);
            const auto live_hand = split_route_c_unreal_array(
                actual_hand->value, array_error);
            if (!expected_deck || !expected_hand || !live_deck || !live_hand)
            {
                throw std::runtime_error{
                    "exact player-zone verification exposed an invalid Unreal array"};
            }

            const auto expected_total = expected_deck->size() + expected_hand->size();
            const auto live_total = live_deck->size() + live_hand->size();
            if (live_total != expected_total
                && now - restore.started_at <= std::chrono::seconds{5})
            {
                return false;
            }

            const bool exact_match = live_total == expected_total
                && actual_deck->value == restore.exact_player_zones->player_deck
                && actual_hand->value == restore.exact_player_zones->player_hand;

            if (!exact_match)
            {
                if (!restore.exact_player_zone_mismatch_since)
                {
                    restore.exact_player_zone_mismatch_since = now;
                    append_route_c_trace(
                        "restore.exact-player-zones.awaiting-stable-order");
                    return false;
                }
                if (now - *restore.exact_player_zone_mismatch_since
                    <= std::chrono::seconds{2})
                {
                    return false;
                }
            }

            append_route_c_trace("restore.exact-player-zones.active-deck-rollback.begin");
            call_reflected(objects.game_instance,
                           STR("setActiveDecklist"),
                           {{STR("newDecklist"), restore.checkpoint.active_decklist}});
            restore.active_decklist_restored_after_exact_startup = true;
            append_route_c_trace("restore.exact-player-zones.active-deck-rollback.complete");

            if (exact_match)
            {
                restore.exact_player_zones_status = "verified";
                restore.exact_player_zones_reason =
                    "fixed-order native startup reproduced the saved deck and hand";
                append_route_c_trace("restore.exact-player-zones.verified");
            }
            else
            {
                restore.exact_player_zones_status = "mismatch-semantic-fallback";
                restore.exact_player_zones_reason =
                    "fixed-order startup preserved the card multiset but not the saved zone order";
                append_route_c_trace_failure(
                    "restore.exact-player-zones.mismatch-semantic-fallback",
                    "expectedDeck=" + restore.exact_player_zones->player_deck
                        + " actualDeck=" + actual_deck->value
                        + " expectedHand=" + restore.exact_player_zones->player_hand
                        + " actualHand=" + actual_hand->value);
            }
            return true;
        }

        auto update_route_c_restore() -> void
        {
            if (!g_pending_route_c_restore)
            {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - g_pending_route_c_restore->started_at > std::chrono::seconds{90})
            {
                finish_route_c_restore("failed", "restore timed out before verification");
                return;
            }
            if (now < g_pending_route_c_restore->phase_ready_at)
            {
                return;
            }

            try
            {
                auto& restore = *g_pending_route_c_restore;
                if (!restore.interception_error.empty())
                {
                    throw std::runtime_error{restore.interception_error};
                }
                if (restore.phase == RouteCRestorePhase::AwaitingWaveIntercept)
                {
                    return;
                }
                // Object discovery and reflected getters are deliberately sampled rather than
                // run on every frame while the replacement PersistentLevel is being assembled.
                restore.phase_ready_at = now + std::chrono::milliseconds{100};

                const auto objects = find_route_c_objects();
                const auto game_state = objects.card_engine
                    ? export_property_text(objects.card_engine, STR("currentGameState"))
                    : std::nullopt;
                const auto wave_text = objects.spawner
                    ? export_property_text(objects.spawner, STR("currentWaveIndex"))
                    : std::nullopt;
                const auto wave = wave_text ? parse_int32(*wave_text) : std::nullopt;
                const auto auto_spawn = objects.spawner
                    ? export_property_text(objects.spawner, STR("autoSpawn"))
                    : std::nullopt;

                std::optional<std::int32_t> health{};
                std::optional<std::int32_t> maximum{};
                if (objects.card_engine)
                {
                    if (const auto value = export_zero_argument_getter(
                            objects.card_engine, STR("getCurrentHealth")))
                    {
                        health = parse_int32(value->value);
                    }
                    if (const auto value = export_zero_argument_getter(
                            objects.card_engine, STR("getMaxHealth")))
                    {
                        maximum = parse_int32(value->value);
                    }
                }

                std::optional<std::int32_t> character_card_charge{};
                std::optional<std::int32_t> character_card_charge_requirement{};
                std::optional<std::string> character_ability_ok{};
                if (objects.character_card_slot)
                {
                    restore.character_card_slot_observed = true;
                    if (const auto value = export_zero_argument_getter(
                            objects.character_card_slot,
                            STR("getCurrentCharacterCardCharge")))
                    {
                        character_card_charge = parse_int32(value->value);
                        restore.character_card_charge = character_card_charge;
                    }
                    if (const auto value = export_zero_argument_getter(
                            objects.character_card_slot,
                            STR("getAmountPerCharacterCard")))
                    {
                        character_card_charge_requirement = parse_int32(value->value);
                        restore.character_card_charge_requirement =
                            character_card_charge_requirement;
                    }
                    if (const auto value = export_zero_argument_getter(
                            objects.character_card_slot,
                            STR("isCharacterAbilityOk")))
                    {
                        character_ability_ok = value->value;
                        restore.character_ability_ok = character_ability_ok;
                    }
                }

                std::ostringstream diagnostic{};
                diagnostic << "phase=" << route_c_restore_phase_name(restore.phase)
                           << " gi=" << (objects.game_instance ? "yes" : "no")
                           << " cardEngine=" << static_cast<const void*>(objects.card_engine)
                           << " spawner=" << static_cast<const void*>(objects.spawner)
                           << " beginPlaySpawner="
                           << static_cast<const void*>(restore.intercepted_spawner)
                           << " sameSpawner="
                           << (objects.spawner && objects.spawner == restore.intercepted_spawner
                                   ? "yes"
                                   : "no")
                           << " sameWorld="
                           << (objects.spawner && restore.intercepted_world
                                   && static_cast<const void*>(objects.spawner->GetWorld())
                                       == restore.intercepted_world
                                   ? "yes"
                                   : "no")
                           << " bottomBar=" << (objects.bottom_bar ? "yes" : "no")
                           << " characterCardSlot="
                           << static_cast<const void*>(objects.character_card_slot)
                           << " characterCharge="
                           << (character_card_charge
                                   ? std::to_string(*character_card_charge)
                                   : "unavailable")
                           << "/"
                           << (character_card_charge_requirement
                                   ? std::to_string(*character_card_charge_requirement)
                                   : "unavailable")
                           << " characterAbilityOk="
                           << (character_ability_ok ? *character_ability_ok : "unavailable")
                           << " state=" << (game_state ? *game_state : "unavailable")
                           << " wave=" << (wave_text ? *wave_text : "unavailable")
                           << " autoSpawn="
                           << (auto_spawn ? *auto_spawn : "unavailable")
                           << " health="
                           << (health ? std::to_string(*health) : "unavailable") << "/"
                           << (maximum ? std::to_string(*maximum) : "unavailable");
                const auto diagnostic_text = diagnostic.str();
                if (diagnostic_text != restore.last_diagnostic
                    || now >= restore.next_diagnostic_at)
                {
                    append_route_c_trace_failure("restore.observe", diagnostic_text);
                    restore.last_diagnostic = diagnostic_text;
                    restore.next_diagnostic_at = now + std::chrono::seconds{5};
                }

                if (!objects.game_instance || !objects.card_engine || !objects.spawner
                    || !objects.bottom_bar)
                {
                    return;
                }
                if (object_class_name(objects.spawner) != restore.checkpoint.spawner_class)
                {
                    return;
                }

                if (restore.phase == RouteCRestorePhase::AwaitingBattleInfrastructure)
                {
                    // The unrestricted scan is needed because some HUD/CardEngine objects are
                    // not reported in the BeginPlay actor's world early in travel. Do not,
                    // however, let a still-reachable object from the previous PersistentLevel
                    // become the controlled spawner.
                    if (objects.spawner != restore.intercepted_spawner)
                    {
                        return;
                    }
                    if (!auto_spawn || (*auto_spawn != "True" && *auto_spawn != "False"))
                    {
                        return;
                    }
                    if (*auto_spawn != "False")
                    {
                        import_property_text(objects.spawner, STR("autoSpawn"), "False");
                        const auto suppressed = required_text(
                            export_property_text(objects.spawner, STR("autoSpawn")),
                            "controlled SpawnController.autoSpawn");
                        if (suppressed != "False")
                        {
                            throw std::runtime_error{
                                "SpawnController.autoSpawn suppression did not verify"};
                        }
                    }
                    restore.intercepted_spawner = objects.spawner;
                    restore.intercepted_world = objects.spawner->GetWorld();
                    restore.auto_spawn_suppressed = true;

                    if (!try_apply_exact_spawn_plan(restore, objects.spawner))
                    {
                        if (now - restore.started_at <= std::chrono::seconds{5})
                        {
                            return;
                        }
                        restore.exact_spawn_plan_status = "failed-no-write";
                        restore.exact_spawn_plan_reason =
                            "replacement SpawnController did not expose spawnList within five seconds";
                        append_route_c_trace_failure(
                            "restore.exact-spawn-plan.failed-no-write",
                            restore.exact_spawn_plan_reason);
                    }

                    if (!wave)
                    {
                        return;
                    }
                    const auto expected_seed = restore.checkpoint.wave_index - 1;
                    if (*wave != expected_seed && *wave != restore.checkpoint.wave_index)
                    {
                        throw std::runtime_error{
                            "SpawnController changed away from the controlled Route C seed"};
                    }
                    append_route_c_trace_failure(
                        "restore.controlled-wave.awaiting-native-start",
                        "seed=" + std::to_string(*wave) + " target="
                            + std::to_string(restore.checkpoint.wave_index));
                    restore.phase = RouteCRestorePhase::AwaitingStableBattle;
                    restore.phase_ready_at = now + std::chrono::milliseconds{100};
                    return;
                }

                if (objects.spawner != restore.intercepted_spawner)
                {
                    throw std::runtime_error{
                        "The controlled SpawnController was replaced during restore"};
                }

                if (!game_state || *game_state != "OPEN" || !wave)
                {
                    return;
                }
                if (!wave || *wave != restore.checkpoint.wave_index)
                {
                    if (*wave < restore.checkpoint.wave_index
                        && restore.phase == RouteCRestorePhase::AwaitingStableBattle)
                    {
                        return;
                    }
                    throw std::runtime_error{"Restored wave index does not match the checkpoint"};
                }

                if (restore.phase == RouteCRestorePhase::AwaitingStableBattle
                    && maximum && *maximum == 0)
                {
                    if (!restore.empty_player_state_since)
                    {
                        restore.empty_player_state_since = now;
                        append_route_c_trace(
                            "restore.player-initialization.empty-state-observed");
                        return;
                    }
                    if (now - *restore.empty_player_state_since > std::chrono::seconds{5})
                    {
                        throw std::runtime_error{
                            "Native player initialization remained empty after BeginPlay re-import"};
                    }
                    return;
                }

                if (!verify_exact_player_zones(restore, objects, now))
                {
                    return;
                }

                if (restore.exact_spawn_plan_status == "pending")
                {
                    restore.exact_spawn_plan_status = "failed-no-write";
                    restore.exact_spawn_plan_reason =
                        "SpawnController BeginPlay completed without applying the exact supplement";
                    append_route_c_trace_failure(
                        "restore.exact-spawn-plan.failed-no-write",
                        restore.exact_spawn_plan_reason);
                }
                verify_exact_spawn_plan_or_rollback(restore, objects.spawner);

                if (restore.phase == RouteCRestorePhase::AwaitingStableBattle)
                {
                    if (!health || !maximum || *health <= 0 || *maximum <= 0)
                    {
                        return;
                    }
                    if (*maximum != restore.checkpoint.player_max_health)
                    {
                        throw std::runtime_error{
                            "Native startup produced a different maximum health"};
                    }
                    append_route_c_trace_failure(
                        "restore.loot-drops.begin",
                        "count=" + std::to_string(restore.loot_drops.size()));
                    call_reflected(objects.game_instance, STR("clearLootDrops"));
                    try
                    {
                        for (const auto& loot_drop : restore.loot_drops)
                        {
                            call_reflected(objects.game_instance,
                                           STR("addLootDrop"),
                                           {{STR("newLoot"), loot_drop}});
                        }
                    }
                    catch (...)
                    {
                        try
                        {
                            call_reflected(objects.game_instance, STR("clearLootDrops"));
                        }
                        catch (...)
                        {
                        }
                        throw;
                    }
                    append_route_c_trace("restore.loot-drops.complete");
                    apply_route_c_health(restore.checkpoint, objects);
                    restore.phase = RouteCRestorePhase::AwaitingVerification;
                    restore.phase_ready_at = now + std::chrono::milliseconds{750};
                    return;
                }

                if (!health || !maximum
                    || *maximum != restore.checkpoint.player_max_health
                    || *health != std::clamp(restore.checkpoint.player_health, 1, *maximum))
                {
                    throw std::runtime_error{
                        "Restored player health or maximum health did not verify"};
                }
                const auto restored_deck = required_getter_text(
                    objects.game_instance, STR("getActiveDecklist"));
                const auto canonical_restored_deck = route_c_startup_decklist(restored_deck);
                if (canonical_restored_deck != restore.checkpoint.active_decklist)
                {
                    append_route_c_trace_failure(
                        "restore.verification.deck-mismatch",
                        "expectedLength="
                            + std::to_string(restore.checkpoint.active_decklist.size())
                            + " actualLength=" + std::to_string(restored_deck.size())
                            + " canonicalActualLength="
                            + std::to_string(canonical_restored_deck.size()));
                    throw std::runtime_error{"Restored active deck did not verify"};
                }
                const auto restored_storage = required_getter_text(
                    objects.game_instance, STR("getActiveStorage"));
                if (restored_storage != restore.checkpoint.active_storage)
                {
                    append_route_c_trace_failure(
                        "restore.verification.storage-mismatch",
                        "expected=" + restore.checkpoint.active_storage
                            + " actual=" + restored_storage);
                    throw std::runtime_error{"Restored active storage did not verify"};
                }
                const auto restored_loot_drops = required_getter_text(
                    objects.game_instance, STR("getLootDrops"));
                if (restored_loot_drops != restore.checkpoint.loot_drops)
                {
                    append_route_c_trace_failure(
                        "restore.verification.loot-drops-mismatch",
                        "expected=" + restore.checkpoint.loot_drops
                            + " actual=" + restored_loot_drops);
                    throw std::runtime_error{"Restored new loot drops did not verify"};
                }

                if (restore.phase == RouteCRestorePhase::AwaitingVerification)
                {
                    if (restore.auto_spawn_suppressed)
                    {
                        import_property_text(objects.spawner,
                                             STR("autoSpawn"),
                                             restore.original_auto_spawn);
                        const auto restored = required_text(
                            export_property_text(objects.spawner, STR("autoSpawn")),
                            "restored SpawnController.autoSpawn");
                        if (restored != restore.original_auto_spawn)
                        {
                            throw std::runtime_error{
                                "SpawnController.autoSpawn restoration did not verify"};
                        }
                        restore.auto_spawn_suppressed = false;
                    }
                    restore.phase = RouteCRestorePhase::AwaitingPostRestoreStability;
                    restore.phase_ready_at = now + std::chrono::seconds{3};
                    append_route_c_trace(
                        "restore.post-restore-stability.begin");
                    return;
                }

                finish_route_c_restore(
                    "passed",
                    restore.exact_spawn_plan_status == "verified"
                            && restore.exact_player_zones_status == "verified"
                        ? "ordinary substage semantics, exact future spawn plan, and exact initial player zones were restored"
                        : restore.exact_spawn_plan_status == "verified"
                            ? "ordinary substage semantics and the exact future spawn plan were restored"
                            : "ordinary substage, player deck, storage, new loot drops, and health were restored");
            }
            catch (const std::exception& error)
            {
                finish_route_c_restore("failed", error.what());
            }
        }

        auto update_route_c_capture() -> void
        {
            if (!g_pending_route_c_capture || g_pending_route_c_restore
                || std::chrono::steady_clock::now() < g_pending_route_c_capture->ready_at)
            {
                return;
            }
            auto capture = *g_pending_route_c_capture;
            g_pending_route_c_capture.reset();
            try
            {
                const auto path = capture_route_c_checkpoint(capture);
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Route C checkpoint saved for wave {}: {}\n"),
                    capture.wave_index,
                    path.wstring());
            }
            catch (const std::exception& error)
            {
                append_route_c_trace_failure("auto-capture.refused", error.what());
                Output::send<LogLevel::Warning>(
                    STR("[QuantumCheckpoint] Automatic Route C checkpoint refused: {}\n"),
                    to_wstring(error.what()));
            }
        }

        auto route_c_actor_begin_play_pre(AActor* actor) -> void
        {
            try
            {
                if (!actor || !g_pending_route_c_restore
                    || (g_pending_route_c_restore->phase
                            != RouteCRestorePhase::AwaitingWaveIntercept
                        && g_pending_route_c_restore->phase
                            != RouteCRestorePhase::AwaitingBattleInfrastructure))
                {
                    return;
                }
                auto& restore = *g_pending_route_c_restore;
                auto* candidate = static_cast<UObject*>(actor);
                const auto candidate_name = to_string(candidate->GetFullName());
                const auto candidate_role = classify(candidate_name);
                if (candidate_role == "BP_CardEngine_C"
                    && is_live_instance(candidate_name, candidate_role)
                    && !restore.game_instance_reimported_at_card_engine_begin_play)
                {
                    // Character setup happens during CardEngine BeginPlay, earlier than the
                    // SpawnController point used for deck initialization. Put the semantic run
                    // data back before that lifecycle runs so the native character ability slot
                    // can be constructed normally. The later Spawner re-import remains the guard
                    // against old-world EndPlay clearing the GameInstance after this point.
                    append_route_c_trace(
                        "restore.card-engine-begin-play.game-instance-reimport.begin");
                    auto* game_instance = find_route_c_objects().game_instance;
                    if (!game_instance)
                    {
                        throw std::runtime_error{
                            "Quantum GameInstance was unavailable during CardEngine BeginPlay"};
                    }
                    import_property_text(game_instance,
                                         STR("activeCharacterInfo"),
                                         restore.checkpoint.active_character_info);
                    import_property_text(game_instance,
                                         STR("activeStageInfo"),
                                         restore.checkpoint.active_stage_info);
                    import_property_text(game_instance,
                                         STR("sourceLevelName"),
                                         restore.checkpoint.source_level_name);
                    call_reflected(game_instance,
                                   STR("setActiveDecklist"),
                                   {{STR("newDecklist"),
                                     restore.exact_player_zones
                                         ? restore.exact_player_startup_decklist
                                         : route_c_startup_decklist(
                                             restore.checkpoint.active_decklist)}});
                    call_reflected(game_instance,
                                   STR("updateBench"),
                                   {{STR("newCards"),
                                     restore.checkpoint.active_storage}});
                    restore.game_instance_reimported_at_card_engine_begin_play = true;
                    append_route_c_trace(
                        "restore.card-engine-begin-play.game-instance-reimport.complete");
                    return;
                }

                auto* spawner = candidate;
                if (!is_route_c_spawn_controller(spawner)
                    || object_class_name(spawner) != restore.checkpoint.spawner_class)
                {
                    return;
                }

                const auto initial_text = export_property_text(
                    spawner, STR("currentWaveIndex"));
                const auto initial_wave = initial_text
                    ? parse_int32(*initial_text)
                    : std::nullopt;
                if (!initial_wave || *initial_wave < -1 || *initial_wave > 1000)
                {
                    throw std::runtime_error{
                        "SpawnController BeginPlay exposed an invalid initial wave index"};
                }
                const auto original_auto_spawn = required_text(
                    export_property_text(spawner, STR("autoSpawn")),
                    "SpawnController BeginPlay autoSpawn");
                if (original_auto_spawn != "True" && original_auto_spawn != "False")
                {
                    throw std::runtime_error{
                        "SpawnController BeginPlay exposed an invalid autoSpawn value"};
                }

                // When reloadBattleArea() is invoked from an already restored battle, the old
                // CardEngine's EndPlay can clear the GameInstance deck after the pre-travel
                // import. Reapply the semantic run data after old-world teardown but before the
                // new controller starts its delayed native player initialization.
                append_route_c_trace(
                    "restore.spawn-controller-begin-play.game-instance-reimport.begin");
                auto* game_instance = find_route_c_objects().game_instance;
                if (!game_instance)
                {
                    throw std::runtime_error{
                        "Quantum GameInstance was unavailable during SpawnController BeginPlay"};
                }
                import_property_text(game_instance,
                                     STR("activeCharacterInfo"),
                                     restore.checkpoint.active_character_info);
                import_property_text(game_instance,
                                     STR("activeStageInfo"),
                                     restore.checkpoint.active_stage_info);
                import_property_text(game_instance,
                                     STR("sourceLevelName"),
                                     restore.checkpoint.source_level_name);
                call_reflected(game_instance,
                               STR("setActiveDecklist"),
                               {{STR("newDecklist"),
                                 restore.exact_player_zones
                                     ? restore.exact_player_startup_decklist
                                     : restore.checkpoint.active_decklist}});
                call_reflected(game_instance,
                               STR("updateBench"),
                               {{STR("newCards"),
                                 restore.checkpoint.active_storage}});
                restore.game_instance_reimported_at_begin_play = true;
                append_route_c_trace(
                    "restore.spawn-controller-begin-play.game-instance-reimport.complete");

                import_property_text(spawner, STR("autoSpawn"), "False");
                const auto verified_auto_spawn = required_text(
                    export_property_text(spawner, STR("autoSpawn")),
                    "suppressed SpawnController BeginPlay autoSpawn");
                if (verified_auto_spawn != "False")
                {
                    throw std::runtime_error{
                        "SpawnController BeginPlay autoSpawn suppression did not verify"};
                }
                const auto controlled_seed = static_cast<std::int64_t>(
                    restore.checkpoint.wave_index) - 1;
                if (controlled_seed < -1 || controlled_seed > 1000)
                {
                    throw std::runtime_error{
                        "SpawnController BeginPlay controlled wave seed exceeded its safe range"};
                }
                import_property_text(spawner,
                                     STR("currentWaveIndex"),
                                     std::to_string(controlled_seed));
                const auto verified_seed = parse_int32(required_text(
                    export_property_text(spawner, STR("currentWaveIndex")),
                    "controlled SpawnController BeginPlay wave seed"));
                if (!verified_seed || *verified_seed != controlled_seed)
                {
                    throw std::runtime_error{
                        "SpawnController BeginPlay controlled wave seed did not verify"};
                }

                if (restore.original_auto_spawn.empty())
                {
                    restore.original_auto_spawn = original_auto_spawn;
                }
                restore.intercepted_spawner = spawner;
                restore.intercepted_world = spawner->GetWorld();
                restore.auto_spawn_suppressed = true;
                restore.phase = RouteCRestorePhase::AwaitingBattleInfrastructure;
                restore.phase_ready_at = std::chrono::steady_clock::now();
                append_route_c_trace_failure(
                    "restore.spawn-controller-begin-play.auto-spawn-suppressed",
                    "currentWaveIndex=" + std::to_string(*initial_wave)
                        + " controlledSeed=" + std::to_string(*verified_seed)
                        + " originalAutoSpawn=" + original_auto_spawn);
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Route C suppressed SpawnController BeginPlay autoSpawn and set wave seed {} -> {}; target is {}.\n"),
                    *initial_wave,
                    *verified_seed,
                    restore.checkpoint.wave_index);
            }
            catch (const std::exception& error)
            {
                if (g_pending_route_c_restore)
                {
                    g_pending_route_c_restore->interception_error = error.what();
                }
                append_route_c_trace_failure(
                    "restore.spawn-controller-begin-play.failed", error.what());
            }
            catch (...)
            {
                if (g_pending_route_c_restore)
                {
                    g_pending_route_c_restore->interception_error =
                        "unknown exception during SpawnController BeginPlay autoSpawn suppression";
                }
                append_route_c_trace(
                    "restore.spawn-controller-begin-play.failed-unknown");
            }
        }

        auto route_c_actor_begin_play_post(AActor* actor) -> void
        {
            try
            {
                if (!actor || !g_pending_route_c_restore
                    || g_pending_route_c_restore->intercepted_spawner != actor)
                {
                    return;
                }
                const auto wave = export_property_text(
                    actor, STR("currentWaveIndex"));
                const auto auto_spawn = export_property_text(
                    actor, STR("autoSpawn"));
                append_route_c_trace_failure(
                    "restore.spawn-controller-begin-play.completed",
                    std::string{"currentWaveIndex="}
                        + (wave ? *wave : "unavailable") + " autoSpawn="
                        + (auto_spawn ? *auto_spawn : "unavailable"));
            }
            catch (...)
            {
                append_route_c_trace(
                    "restore.spawn-controller-begin-play.post-failed");
            }
        }

        auto find_live_route_c_card_engine() -> UObject*
        {
            UObject* result{};
            static const FName card_engine_class_name{STR("BP_CardEngine_C")};
            UObjectGlobals::ForEachUObject([&](UObject* object,
                                                [[maybe_unused]] int32_t object_index,
                                                [[maybe_unused]] int32_t chunk_index) {
                if (!object || object->IsUnreachable()
                    || object->HasAnyFlags(
                        static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed)))
                {
                    return LoopAction::Continue;
                }
                auto* object_class = object->GetClassPrivate();
                if (!object_class
                    || !object_class->GetNamePrivate().Equals(card_engine_class_name))
                {
                    return LoopAction::Continue;
                }
                const auto full_name = to_string(object->GetFullName());
                if (is_live_instance(full_name, "BP_CardEngine_C") && object->GetWorld())
                {
                    // A previous PersistentLevel can remain reachable briefly while travel is
                    // completing. UObject iteration order puts the newly-created live instance
                    // last, matching the full battle-object discovery used during capture.
                    result = object;
                }
                return LoopAction::Continue;
            });
            return result;
        }

        auto update_route_c_wave_poll() -> void
        {
            const auto now = std::chrono::steady_clock::now();
            if (!g_unreal_ready.load(std::memory_order_acquire)
                || now < g_next_route_c_wave_poll)
            {
                return;
            }
            g_next_route_c_wave_poll = now + std::chrono::milliseconds{750};

            // Restore owns battle travel and wave selection while it is active. Resuming the
            // observer afterwards also makes the replacement spawner a fresh observation.
            if (g_pending_route_c_restore)
            {
                g_route_c_wave_observation.reset();
                return;
            }

            auto* card_engine = find_live_route_c_card_engine();
            auto* spawner = reflected_object_property(
                card_engine, STR("mEnemySpawnController"));
            if (!card_engine || !is_route_c_spawn_controller(spawner))
            {
                g_route_c_wave_observation.reset();
                return;
            }

            const auto game_state = export_property_text(
                card_engine, STR("currentGameState"));
            const auto wave_text = export_property_text(
                spawner, STR("currentWaveIndex"));
            const auto wave = wave_text ? parse_int32(*wave_text) : std::nullopt;
            if (!game_state || *game_state != "OPEN" || !wave || *wave < 0 || *wave > 1000)
            {
                // Preserve the previous stable observation across a short transition. Once the
                // new wave reaches OPEN, its changed index is detected exactly once.
                return;
            }

            const auto world = static_cast<const void*>(spawner->GetWorld());
            if (g_route_c_wave_observation
                && g_route_c_wave_observation->spawner == spawner
                && g_route_c_wave_observation->world == world
                && g_route_c_wave_observation->wave_index == *wave)
            {
                return;
            }

            g_route_c_wave_observation.emplace(RouteCWaveObservation{
                .spawner = spawner,
                .world = world,
                .wave_index = *wave,
            });
            if (g_adopt_next_route_c_wave_without_capture)
            {
                g_adopt_next_route_c_wave_without_capture = false;
                append_route_c_trace("auto-capture.skipped.restore-settle");
                return;
            }
            g_pending_route_c_capture.emplace(PendingRouteCCapture{
                .spawner = spawner,
                .wave_index = *wave,
                .ready_at = now + std::chrono::milliseconds{1500},
            });
            append_route_c_trace("auto-capture.scheduled.wave-poll");
            Output::send<LogLevel::Verbose>(
                STR("[QuantumCheckpoint] Route C observed stable wave {}; checkpoint capture scheduled.\n"),
                *wave);
        }

        auto route_c_process_event_pre(UObject* context, UFunction* function, void* parameters)
            -> void
        {
            if (!g_pending_route_c_restore || !function || function != g_spawn_wave_index_function
                || !context || !parameters || !is_route_c_spawn_controller(context))
            {
                return;
            }
            auto* wave_property = function->GetPropertyByNameInChain(STR("waveIndex"));
            if (!wave_property)
            {
                return;
            }
            auto* wave_index = wave_property->ContainerPtrToValuePtr<std::int32_t>(parameters);
            if (!wave_index)
            {
                return;
            }
            append_route_c_trace_failure(
                "restore.native.spawn-wave-index.pre",
                "waveIndex=" + std::to_string(*wave_index));
        }

        auto route_c_process_event_post(UObject* context, UFunction* function, void* parameters)
            -> void
        {
            if (!function || !context || !is_route_c_spawn_controller(context))
            {
                return;
            }
            std::optional<std::int32_t> wave_index{};
            std::string_view trigger{};
            if (function == g_spawn_wave_index_function && parameters)
            {
                auto* wave_property = function->GetPropertyByNameInChain(STR("waveIndex"));
                auto* value = wave_property
                    ? wave_property->ContainerPtrToValuePtr<std::int32_t>(parameters)
                    : nullptr;
                if (value)
                {
                    wave_index = *value;
                    trigger = "spawn-wave-index";
                }
            }
            else if (function == g_spawn_next_wave_function)
            {
                const auto value = export_property_text(context, STR("currentWaveIndex"));
                if (value)
                {
                    wave_index = parse_int32(*value);
                    trigger = "spawn-next-wave";
                }
            }

            if (!wave_index || *wave_index < 0 || *wave_index > 1000)
            {
                return;
            }
            if (g_pending_route_c_restore)
            {
                append_route_c_trace_failure(
                    trigger == "spawn-next-wave"
                        ? "restore.native.spawn-next-wave.post"
                        : "restore.native.spawn-wave-index.post",
                    "currentWaveIndex=" + std::to_string(*wave_index));
                return;
            }
            g_pending_route_c_capture.emplace(PendingRouteCCapture{
                .spawner = context,
                .wave_index = *wave_index,
                .ready_at = std::chrono::steady_clock::now() + std::chrono::milliseconds{1500},
            });
            append_route_c_trace(
                trigger == "spawn-next-wave"
                    ? "auto-capture.scheduled.spawn-next-wave"
                    : "auto-capture.scheduled.spawn-wave-index");
        }

        auto route_c_native_spawn_wave_pre(
            UnrealScriptFunctionCallableContext& context,
            [[maybe_unused]] void* custom_data) -> void
        {
            route_c_process_event_pre(
                context.Context,
                context.TheStack.CurrentNativeFunction(),
                context.TheStack.Locals());
        }

        auto route_c_native_spawn_wave_post(
            UnrealScriptFunctionCallableContext& context,
            [[maybe_unused]] void* custom_data) -> void
        {
            route_c_process_event_post(
                context.Context,
                context.TheStack.CurrentNativeFunction(),
                context.TheStack.Locals());
        }

        auto route_c_native_spawn_next_pre(
            [[maybe_unused]] UnrealScriptFunctionCallableContext& context,
            [[maybe_unused]] void* custom_data) -> void
        {
        }

        auto route_c_native_spawn_next_post(
            UnrealScriptFunctionCallableContext& context,
            [[maybe_unused]] void* custom_data) -> void
        {
            route_c_process_event_post(
                context.Context,
                context.TheStack.CurrentNativeFunction(),
                context.TheStack.Locals());
        }

        auto collect_inventory() -> BattleInventory
        {
            BattleInventory inventory{};
            inventory.captured_at_utc = utc_timestamp();

            UObjectGlobals::ForEachUObject([&](UObject* object, [[maybe_unused]] int32_t object_index,
                                                [[maybe_unused]] int32_t chunk_index) {
                if (!object)
                {
                    return LoopAction::Continue;
                }

                const std::string full_name = to_string(object->GetFullName());
                std::string role = classify(full_name);
                if (role.empty() && is_route_c_spawn_controller(object))
                {
                    role = "Spawner_C";
                }
                const bool game_instance = role == "GI_Quantum_C";
                if (role.empty() || (!game_instance && !is_live_instance(full_name, role)))
                {
                    return LoopAction::Continue;
                }

                ObjectSnapshot snapshot{.role = role, .full_name = full_name};
                for (const auto property_name : CandidateProperties)
                {
                    auto* property = object->GetPropertyByNameInChain(property_name.data());
                    if (!property)
                    {
                        continue;
                    }

                    FString exported{};
                    property->ExportTextItem(
                        exported,
                        property->ContainerPtrToValuePtr<void>(object),
                        nullptr,
                        nullptr,
                        0);
                    snapshot.properties.push_back(PropertySnapshot{
                        .name = to_string(property_name),
                        .value = to_string(exported.GetCharArray()),
                    });
                }

                if (role == "GI_Quantum_C" && full_name.contains("/Engine/Transient."))
                {
                    if (g_route_c_travel_occurred_in_process)
                    {
                        snapshot.properties.push_back({
                            "nativeDiagnostic:postTravelComplexGettersSkipped",
                            "getCurrentDeckRun,getActiveDecklistInstances",
                        });
                        append_getters(snapshot, object, PostTravelSafeGameInstanceGetters);
                    }
                    else
                    {
                        append_getters(snapshot, object, GameInstanceGetters);
                    }
                }
                else if (role == "BP_ControllerCharacterCardSlot_C")
                {
                    append_getters(snapshot, object, CharacterCardSlotGetters);
                }
                else if (role.starts_with("BP_Controller") && role != "BP_ControllerBoard_C"
                         && role != "BP_FieldSlot_C")
                {
                    append_getters(snapshot, object, CardGroupGetters);
                }
                else if (role == "BP_InGameCard_C")
                {
                    append_private_card_state_diagnostics(snapshot, object);
                    append_getters(snapshot, object, InGameCardGetters);
                }
                else if (role == "BP_CardEngine_C")
                {
                    append_getters(snapshot, object, CardEngineGetters);
                }
                else if (role == "CardPlacementComponent")
                {
                    append_getters(snapshot, object, PlacementComponentGetters);
                }
                else if (role == "BP_FieldSlot_C")
                {
                    append_getters(snapshot, object, FieldSlotGetters);
                }
                else if (role == "BP_CardEffectDisplay_C")
                {
                    append_getters(snapshot, object, EffectDisplayGetters);
                }
                else if (role == "BP_GenericCounterDisplay_C"
                         || role == "BP_SpecialCounterDisplay_C")
                {
                    append_getters(snapshot, object, CounterDisplayGetters);
                }
                inventory.objects.push_back(std::move(snapshot));
                return LoopAction::Continue;
            });

            return inventory;
        }

        auto write_inventory(const BattleInventory& inventory) -> std::filesystem::path
        {
            const auto mods_directory = std::filesystem::path{
                UE4SSProgram::get_program().get_mods_directory()};
            const auto report_directory = mods_directory / STR("QuantumCheckpoint") / STR("Reports");
            std::filesystem::create_directories(report_directory);

            const auto report_path = report_directory /
                (STR("battle-inventory-") + to_wstring(filename_timestamp()) + STR(".json"));
            auto temporary_path = report_path;
            temporary_path += STR(".tmp");

            std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
            if (!output)
            {
                throw std::runtime_error{"Unable to open temporary inventory report"};
            }

            output << "{\n"
                   << "  \"schemaVersion\": " << inventory.schema_version << ",\n"
                   << "  \"kind\": \"read-only-battle-inventory\",\n"
                   << "  \"capturedAtUtc\": \"" << json_escape(inventory.captured_at_utc) << "\",\n"
                   << "  \"objectCount\": " << inventory.objects.size() << ",\n"
                   << "  \"objects\": [\n";

            for (std::size_t object_index = 0; object_index < inventory.objects.size(); ++object_index)
            {
                const auto& object = inventory.objects[object_index];
                output << "    {\n"
                       << "      \"role\": \"" << json_escape(object.role) << "\",\n"
                       << "      \"fullName\": \"" << json_escape(object.full_name) << "\",\n"
                       << "      \"properties\": {";

                for (std::size_t property_index = 0; property_index < object.properties.size(); ++property_index)
                {
                    const auto& property = object.properties[property_index];
                    output << (property_index == 0 ? "\n" : ",\n")
                           << "        \"" << json_escape(property.name) << "\": \""
                           << json_escape(property.value) << "\"";
                }

                if (!object.properties.empty())
                {
                    output << '\n';
                }
                output << "      }\n"
                       << "    }" << (object_index + 1 == inventory.objects.size() ? "\n" : ",\n");
            }

            output << "  ]\n}\n";
            output.flush();
            if (!output)
            {
                throw std::runtime_error{"Unable to finish writing inventory report"};
            }
            output.close();

            std::filesystem::rename(temporary_path, report_path);
            return report_path;
        }

        auto export_inventory() -> void
        {
            try
            {
                append_route_c_trace("inventory-export.begin");
                auto inventory = collect_inventory();
                if (inventory.objects.empty())
                {
                    append_route_c_trace("inventory-export.skipped-empty");
                    Output::send<LogLevel::Warning>(
                        STR("[QuantumCheckpoint] No active battle objects found; report not written.\n"));
                    return;
                }

                const auto report_path = write_inventory(inventory);
                append_route_c_trace("inventory-export.complete");
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Wrote read-only inventory with {} objects: {}\n"),
                    inventory.objects.size(),
                    report_path.wstring());
            }
            catch (const std::exception& error)
            {
                append_route_c_trace_failure("inventory-export.failed", error.what());
                Output::send<LogLevel::Error>(
                    STR("[QuantumCheckpoint] Inventory export failed: {}\n"),
                    to_wstring(error.what()));
            }
        }

        auto write_health_probe_report(const HealthWriteProbeResult& result) -> std::filesystem::path
        {
            const auto mods_directory = std::filesystem::path{
                UE4SSProgram::get_program().get_mods_directory()};
            const auto report_directory = mods_directory / STR("QuantumCheckpoint") / STR("Reports");
            std::filesystem::create_directories(report_directory);

            const auto report_path = report_directory /
                (STR("health-write-probe-") + to_wstring(filename_timestamp()) + STR(".json"));
            auto temporary_path = report_path;
            temporary_path += STR(".tmp");

            std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
            if (!output)
            {
                throw std::runtime_error{"Unable to open temporary health probe report"};
            }

            output << "{\n"
                   << "  \"schemaVersion\": 1,\n"
                   << "  \"kind\": \"guarded-timed-native-health-write-probe\",\n"
                   << "  \"capturedAtUtc\": \"" << json_escape(utc_timestamp()) << "\",\n"
                   << "  \"status\": \"" << json_escape(result.status) << "\",\n"
                   << "  \"reason\": \"" << json_escape(result.reason) << "\",\n"
                   << "  \"cardFullName\": \"" << json_escape(result.card_full_name) << "\",\n"
                   << "  \"cardTag\": \"" << json_escape(result.card_tag) << "\",\n"
                   << "  \"cardId\": \"" << json_escape(result.card_id) << "\",\n"
                   << "  \"cardLocation\": \"" << json_escape(result.card_location) << "\",\n"
                   << "  \"stateAddress\": \"" << format_address(result.state_address) << "\",\n"
                   << "  \"baseHealth\": " << result.base_health << ",\n"
                   << "  \"beforePrivate\": " << result.before_private << ",\n"
                   << "  \"beforeGetter\": " << result.before_getter << ",\n"
                   << "  \"testValue\": " << result.test_value << ",\n"
                   << "  \"duringPrivate\": " << result.during_private << ",\n"
                   << "  \"reflectedGetterCalledDuringTemporaryWrite\": false,\n"
                   << "  \"duringGetter\": " << result.during_getter << ",\n"
                   << "  \"requestedHoldMilliseconds\": "
                   << result.requested_hold_milliseconds << ",\n"
                   << "  \"actualHoldMilliseconds\": "
                   << result.actual_hold_milliseconds << ",\n"
                   << "  \"restoreIdentityValidated\": "
                   << (result.restore_identity_validated ? "true" : "false") << ",\n"
                   << "  \"beforeRestorePrivate\": " << result.before_restore_private << ",\n"
                   << "  \"restoredPrivate\": " << result.restored_private << ",\n"
                   << "  \"restoredGetter\": " << result.restored_getter << "\n"
                   << "}\n";
            output.flush();
            if (!output)
            {
                throw std::runtime_error{"Unable to finish writing health probe report"};
            }
            output.close();
            std::filesystem::rename(temporary_path, report_path);
            return report_path;
        }

        auto run_health_write_probe() -> void
        {
            HealthWriteProbeResult result{};
            result.requested_hold_milliseconds = TimedHealthProbeHold.count();
            try
            {
                if (g_pending_health_write_probe || g_pending_turn_write_probe)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[QuantumCheckpoint] A timed native write probe is already active; health request ignored.\n"));
                    return;
                }

                const auto module = GetModuleHandleW(L"Quantum-Win64-Shipping.exe");
                if (!module)
                {
                    result.status = "refused";
                    result.reason = "game executable module was not found";
                    const auto path = write_health_probe_report(result);
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Health write probe refused: {}; report: {}\n"),
                        to_wstring(result.reason), path.wstring());
                    return;
                }

                if (!fingerprint_matches_supported_game(executable_fingerprint()))
                {
                    result.status = "refused";
                    result.reason = "game executable SHA-256 does not match the validated build";
                    const auto path = write_health_probe_report(result);
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Health write probe refused: {}; report: {}\n"),
                        to_wstring(result.reason), path.wstring());
                    return;
                }

                const auto module_base = reinterpret_cast<std::uintptr_t>(module);
                const auto* setter_bytes = reinterpret_cast<const std::uint8_t*>(
                    module_base + SetCurrentHealthRva);
                if (!std::equal(
                        SetCurrentHealthSignature.begin(),
                        SetCurrentHealthSignature.end(),
                        setter_bytes))
                {
                    result.status = "refused";
                    result.reason = "native health setter signature does not match the validated build";
                    const auto path = write_health_probe_report(result);
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Health write probe refused: {}; report: {}\n"),
                        to_wstring(result.reason), path.wstring());
                    return;
                }

                UObject* target{};
                const void* target_state{};
                std::int32_t target_base_health{};
                std::int32_t target_current_health{};

                UObjectGlobals::ForEachUObject([&](UObject* object, [[maybe_unused]] int32_t object_index,
                                                    [[maybe_unused]] int32_t chunk_index) {
                    if (!object)
                    {
                        return LoopAction::Continue;
                    }

                    const std::string full_name = to_string(object->GetFullName());
                    if (classify(full_name) != "BP_InGameCard_C"
                        || !is_live_instance(full_name, "BP_InGameCard_C"))
                    {
                        return LoopAction::Continue;
                    }

                    auto* health_getter = object->GetFunctionByNameInChain(STR("getCurrentHealth"));
                    if (!health_getter
                        || reinterpret_cast<std::uintptr_t>(health_getter->GetFuncPtr())
                            != module_base + CurrentHealthGetterThunkRva)
                    {
                        return LoopAction::Continue;
                    }

                    if (target)
                    {
                        return LoopAction::Continue;
                    }

                    const auto* state = read_native_value<const void*>(
                        object, InGameCardStatePointerOffset);
                    if (!state
                        || !address_is_writable(
                            static_cast<const std::byte*>(state) + CardStateCurrentHealthOffset,
                            sizeof(std::int32_t)))
                    {
                        return LoopAction::Continue;
                    }

                    const auto base_health = read_native_value<std::int32_t>(
                        state, CardStateBaseHealthOffset);
                    const auto current_health = read_native_value<std::int32_t>(
                        state, CardStateCurrentHealthOffset);
                    if (base_health <= 0 || base_health > 10'000
                        || current_health <= 0 || current_health >= base_health)
                    {
                        return LoopAction::Continue;
                    }

                    target = object;
                    target_state = state;
                    target_base_health = base_health;
                    target_current_health = current_health;
                    return LoopAction::Continue;
                });

                if (!target || !target_state)
                {
                    result.status = "refused";
                    result.reason = "no live damaged card passed all safety checks";
                }
                else
                {
                    result.card_full_name = to_string(target->GetFullName());
                    result.card_location = "not-read; complex location getter intentionally avoided";
                    result.state_address = reinterpret_cast<std::uintptr_t>(target_state);
                    result.base_health = target_base_health;
                    result.before_private = target_current_health;
                    result.test_value = target_current_health + 1;

                    const auto before_getter = export_zero_argument_getter(
                        target, STR("getCurrentHealth"));
                    const auto before_value = before_getter
                        ? parse_int32(before_getter->value)
                        : std::nullopt;
                    if (!before_value || *before_value != result.before_private)
                    {
                        result.status = "refused";
                        result.reason = "current health getter did not match private state before write";
                    }
                    else
                    {
                        result.before_getter = *before_value;
                        const auto set_current_health = reinterpret_cast<SetCurrentHealthFunction>(
                            module_base + SetCurrentHealthRva);
                        HealthRestoreGuard restore_guard{
                            set_current_health, target_state, result.before_private};

                        set_current_health(target_state, result.test_value);
                        result.during_private = read_native_value<std::int32_t>(
                            target_state, CardStateCurrentHealthOffset);
                        result.during_getter = -1;
                        if (result.during_private != result.test_value)
                        {
                            result.status = "failed";
                            result.reason = "temporary private health write did not read back";
                        }
                        else
                        {
                            const auto started_at = std::chrono::steady_clock::now();
                            g_pending_health_write_probe.emplace(PendingHealthWriteProbe{
                                .result = std::move(result),
                                .card = target,
                                .state = target_state,
                                .setter = set_current_health,
                                .started_at = started_at,
                                .restore_after = started_at + TimedHealthProbeHold,
                            });
                            restore_guard.active = false;
                            Output::send<LogLevel::Verbose>(
                                STR("[QuantumCheckpoint] Timed health write probe holding +1 HP for {} ms; do not interact.\n"),
                                TimedHealthProbeHold.count());
                            return;
                        }
                    }
                }

                const auto path = write_health_probe_report(result);
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Health write probe {}: {}; report: {}\n"),
                    to_wstring(result.status), to_wstring(result.reason), path.wstring());
            }
            catch (const std::exception& error)
            {
                Output::send<LogLevel::Error>(
                    STR("[QuantumCheckpoint] Health write probe failed before report completion: {}\n"),
                    to_wstring(error.what()));
            }
        }

        auto finish_health_write_probe_if_due() -> void
        {
            if (!g_pending_health_write_probe
                || std::chrono::steady_clock::now()
                    < g_pending_health_write_probe->restore_after)
            {
                return;
            }

            auto& pending = *g_pending_health_write_probe;
            auto& result = pending.result;
            try
            {
                result.actual_hold_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - pending.started_at).count();

                bool identity_validated{};
                UObjectGlobals::ForEachUObject([&](UObject* object, [[maybe_unused]] int32_t object_index,
                                                    [[maybe_unused]] int32_t chunk_index) {
                    if (object != pending.card)
                    {
                        return LoopAction::Continue;
                    }

                    const std::string full_name = to_string(object->GetFullName());
                    if (classify(full_name) == "BP_InGameCard_C"
                        && is_live_instance(full_name, "BP_InGameCard_C")
                        && read_native_value<const void*>(object, InGameCardStatePointerOffset)
                            == pending.state
                        && address_is_writable(
                            static_cast<const std::byte*>(pending.state)
                                + CardStateCurrentHealthOffset,
                            sizeof(std::int32_t)))
                    {
                        identity_validated = true;
                    }
                    return LoopAction::Continue;
                });
                result.restore_identity_validated = identity_validated;

                if (!identity_validated)
                {
                    result.status = "failed";
                    result.reason = "target identity changed during hold; restore was not attempted";
                }
                else
                {
                    result.before_restore_private = read_native_value<std::int32_t>(
                        pending.state, CardStateCurrentHealthOffset);
                    if (result.before_restore_private != result.test_value)
                    {
                        result.status = "failed";
                        result.reason = "health changed independently during hold; value was not overwritten";
                        result.restored_private = result.before_restore_private;
                    }
                    else
                    {
                        HealthRestoreGuard restore_guard{
                            pending.setter, pending.state, result.before_private};
                        pending.setter(pending.state, result.before_private);
                        restore_guard.active = false;
                        result.restored_private = read_native_value<std::int32_t>(
                            pending.state, CardStateCurrentHealthOffset);

                        const auto restored_getter = export_zero_argument_getter(
                            pending.card, STR("getCurrentHealth"));
                        const auto restored_value = restored_getter
                            ? parse_int32(restored_getter->value)
                            : std::nullopt;
                        result.restored_getter = restored_value.value_or(-1);

                        const bool passed = result.restored_private == result.before_private
                            && restored_value == result.before_private;
                        result.status = passed ? "passed" : "failed";
                        result.reason = passed
                            ? "temporary health write survived the timed hold and was restored"
                            : "timed health restore verification did not match";
                    }
                }

                HealthWriteProbeResult completed_result = result;
                g_pending_health_write_probe.reset();
                const auto path = write_health_probe_report(completed_result);
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Timed health write probe {}: {}; report: {}\n"),
                    to_wstring(completed_result.status),
                    to_wstring(completed_result.reason),
                    path.wstring());
            }
            catch (const std::exception& error)
            {
                auto failed_result = result;
                failed_result.status = "failed";
                failed_result.reason = "exception during timed health restore: "
                    + std::string{error.what()};
                if (pending.setter && pending.state
                    && address_is_writable(
                        static_cast<const std::byte*>(pending.state)
                            + CardStateCurrentHealthOffset,
                        sizeof(std::int32_t))
                    && read_native_value<std::int32_t>(
                        pending.state, CardStateCurrentHealthOffset) == result.test_value)
                {
                    pending.setter(pending.state, result.before_private);
                    failed_result.restored_private = result.before_private;
                }
                g_pending_health_write_probe.reset();
                try
                {
                    const auto path = write_health_probe_report(failed_result);
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Timed health restore failed safely: {}; report: {}\n"),
                        to_wstring(error.what()),
                        path.wstring());
                }
                catch (...)
                {
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Timed health restore and report both failed: {}\n"),
                        to_wstring(error.what()));
                }
            }
        }

        auto write_turn_probe_report(const TurnWriteProbeResult& result) -> std::filesystem::path
        {
            const auto mods_directory = std::filesystem::path{
                UE4SSProgram::get_program().get_mods_directory()};
            const auto report_directory = mods_directory / STR("QuantumCheckpoint") / STR("Reports");
            std::filesystem::create_directories(report_directory);

            const auto report_path = report_directory /
                (STR("turn-write-probe-") + to_wstring(filename_timestamp()) + STR(".json"));
            auto temporary_path = report_path;
            temporary_path += STR(".tmp");

            std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
            if (!output)
            {
                throw std::runtime_error{"Unable to open temporary turn probe report"};
            }

            output << "{\n"
                   << "  \"schemaVersion\": 1,\n"
                   << "  \"kind\": \"guarded-timed-native-turn-write-probe\",\n"
                   << "  \"capturedAtUtc\": \"" << json_escape(utc_timestamp()) << "\",\n"
                   << "  \"status\": \"" << json_escape(result.status) << "\",\n"
                   << "  \"reason\": \"" << json_escape(result.reason) << "\",\n"
                   << "  \"cardFullName\": \"" << json_escape(result.card_full_name) << "\",\n"
                   << "  \"cardLocation\": \"" << json_escape(result.card_location) << "\",\n"
                   << "  \"stateAddress\": \"" << format_address(result.state_address) << "\",\n"
                   << "  \"beforeBase\": " << result.before_base << ",\n"
                   << "  \"beforeAdjustment\": " << result.before_adjustment << ",\n"
                   << "  \"beforeComputed\": " << result.before_computed << ",\n"
                   << "  \"beforeGetter\": " << result.before_getter << ",\n"
                   << "  \"testAdjustment\": " << result.test_adjustment << ",\n"
                   << "  \"testComputed\": " << result.test_computed << ",\n"
                   << "  \"duringBase\": " << result.during_base << ",\n"
                   << "  \"duringAdjustment\": " << result.during_adjustment << ",\n"
                   << "  \"duringComputed\": " << result.during_computed << ",\n"
                   << "  \"reflectedGetterCalledDuringTemporaryWrite\": false,\n"
                   << "  \"requestedHoldMilliseconds\": "
                   << result.requested_hold_milliseconds << ",\n"
                   << "  \"actualHoldMilliseconds\": "
                   << result.actual_hold_milliseconds << ",\n"
                   << "  \"restoreIdentityValidated\": "
                   << (result.restore_identity_validated ? "true" : "false") << ",\n"
                   << "  \"beforeRestoreBase\": " << result.before_restore_base << ",\n"
                   << "  \"beforeRestoreAdjustment\": " << result.before_restore_adjustment << ",\n"
                   << "  \"beforeRestoreComputed\": " << result.before_restore_computed << ",\n"
                   << "  \"restoredBase\": " << result.restored_base << ",\n"
                   << "  \"restoredAdjustment\": " << result.restored_adjustment << ",\n"
                   << "  \"restoredComputed\": " << result.restored_computed << ",\n"
                   << "  \"restoredGetter\": " << result.restored_getter << "\n"
                   << "}\n";
            output.flush();
            if (!output)
            {
                throw std::runtime_error{"Unable to finish writing turn probe report"};
            }
            output.close();
            std::filesystem::rename(temporary_path, report_path);
            return report_path;
        }

        auto run_turn_write_probe() -> void
        {
            TurnWriteProbeResult result{};
            result.requested_hold_milliseconds = TimedTurnProbeHold.count();
            try
            {
                if (g_pending_health_write_probe || g_pending_turn_write_probe)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[QuantumCheckpoint] A timed native write probe is already active; turn request ignored.\n"));
                    return;
                }

                const auto module = GetModuleHandleW(L"Quantum-Win64-Shipping.exe");
                if (!module)
                {
                    result.status = "refused";
                    result.reason = "game executable module was not found";
                }
                else
                {
                    if (!fingerprint_matches_supported_game(executable_fingerprint()))
                    {
                        result.status = "refused";
                        result.reason = "game executable SHA-256 does not match the validated build";
                    }
                    else
                    {
                        const auto module_base = reinterpret_cast<std::uintptr_t>(module);
                        const auto* getter_bytes = reinterpret_cast<const std::uint8_t*>(
                            module_base + NativeCurrentTurnGetterRva);
                        if (!std::equal(
                                NativeCurrentTurnGetterSignature.begin(),
                                NativeCurrentTurnGetterSignature.end(),
                                getter_bytes))
                        {
                            result.status = "refused";
                            result.reason = "native turn getter signature does not match the validated build";
                        }
                        else
                        {
                            UObject* target{};
                            const void* target_state{};
                            std::int32_t target_base{};
                            std::int32_t target_adjustment{};
                            std::int32_t target_computed{};

                            UObjectGlobals::ForEachUObject([&](UObject* object,
                                                                [[maybe_unused]] int32_t object_index,
                                                                [[maybe_unused]] int32_t chunk_index) {
                                if (!object)
                                {
                                    return LoopAction::Continue;
                                }

                                const std::string full_name = to_string(object->GetFullName());
                                if (classify(full_name) != "BP_InGameCard_C"
                                    || !is_live_instance(full_name, "BP_InGameCard_C"))
                                {
                                    return LoopAction::Continue;
                                }

                                auto* turn_getter = object->GetFunctionByNameInChain(
                                    STR("getCurrentTurnCounter"));
                                if (!turn_getter
                                    || reinterpret_cast<std::uintptr_t>(turn_getter->GetFuncPtr())
                                        != module_base + CurrentTurnGetterThunkRva)
                                {
                                    return LoopAction::Continue;
                                }

                                const auto* state = read_native_value<const void*>(
                                    object, InGameCardStatePointerOffset);
                                if (!state
                                    || !address_is_writable(
                                        static_cast<const std::byte*>(state)
                                            + CardStateTurnAdjustmentOffset,
                                        sizeof(std::int32_t))
                                    || !address_is_writable(
                                        static_cast<const std::byte*>(state)
                                            + CardStateTurnBaseOffset,
                                        sizeof(std::int32_t)))
                                {
                                    return LoopAction::Continue;
                                }

                                const auto turn_base = read_native_value<std::int32_t>(
                                    state, CardStateTurnBaseOffset);
                                const auto turn_adjustment = read_native_value<std::int32_t>(
                                    state, CardStateTurnAdjustmentOffset);
                                const auto computed_wide = static_cast<std::int64_t>(turn_base)
                                    + static_cast<std::int64_t>(turn_adjustment);
                                if (turn_base < -1000 || turn_base > 1000
                                    || turn_adjustment < -1000 || turn_adjustment >= 1000
                                    || computed_wide <= 0 || computed_wide >= 1000)
                                {
                                    return LoopAction::Continue;
                                }

                                const auto computed = static_cast<std::int32_t>(computed_wide);
                                if (!target || computed > target_computed)
                                {
                                    target = object;
                                    target_state = state;
                                    target_base = turn_base;
                                    target_adjustment = turn_adjustment;
                                    target_computed = computed;
                                }
                                return LoopAction::Continue;
                            });

                            if (!target || !target_state)
                            {
                                result.status = "refused";
                                result.reason = "no live card with a positive turn counter passed all safety checks";
                            }
                            else
                            {
                                result.card_full_name = to_string(target->GetFullName());
                                result.card_location = "not-read; complex location getter intentionally avoided";
                                result.state_address = reinterpret_cast<std::uintptr_t>(target_state);
                                result.before_base = target_base;
                                result.before_adjustment = target_adjustment;
                                result.before_computed = target_computed;
                                result.test_adjustment = target_adjustment + 1;
                                result.test_computed = target_computed + 1;

                                const auto before_getter = export_zero_argument_getter(
                                    target, STR("getCurrentTurnCounter"));
                                const auto before_value = before_getter
                                    ? parse_int32(before_getter->value)
                                    : std::nullopt;
                                if (!before_value || *before_value != result.before_computed)
                                {
                                    result.status = "refused";
                                    result.reason = "current turn getter did not match private state before write";
                                }
                                else
                                {
                                    result.before_getter = *before_value;
                                    Int32FieldRestoreGuard restore_guard{
                                        target_state,
                                        CardStateTurnAdjustmentOffset,
                                        result.before_adjustment};

                                    write_native_value(
                                        target_state,
                                        CardStateTurnAdjustmentOffset,
                                        result.test_adjustment);
                                    result.during_base = read_native_value<std::int32_t>(
                                        target_state, CardStateTurnBaseOffset);
                                    result.during_adjustment = read_native_value<std::int32_t>(
                                        target_state, CardStateTurnAdjustmentOffset);
                                    result.during_computed = static_cast<std::int64_t>(
                                        result.during_base) + result.during_adjustment;
                                    if (result.during_base != result.before_base
                                        || result.during_adjustment != result.test_adjustment
                                        || result.during_computed != result.test_computed)
                                    {
                                        result.status = "failed";
                                        result.reason = "temporary private turn write did not read back";
                                    }
                                    else
                                    {
                                        const auto started_at = std::chrono::steady_clock::now();
                                        g_pending_turn_write_probe.emplace(PendingTurnWriteProbe{
                                            .result = std::move(result),
                                            .card = target,
                                            .state = target_state,
                                            .started_at = started_at,
                                            .restore_after = started_at + TimedTurnProbeHold,
                                        });
                                        restore_guard.active = false;
                                        Output::send<LogLevel::Verbose>(
                                            STR("[QuantumCheckpoint] Timed turn write probe holding +1 turn for {} ms; do not interact.\n"),
                                            TimedTurnProbeHold.count());
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }

                const auto path = write_turn_probe_report(result);
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Turn write probe {}: {}; report: {}\n"),
                    to_wstring(result.status), to_wstring(result.reason), path.wstring());
            }
            catch (const std::exception& error)
            {
                Output::send<LogLevel::Error>(
                    STR("[QuantumCheckpoint] Turn write probe failed before report completion: {}\n"),
                    to_wstring(error.what()));
            }
        }

        auto finish_turn_write_probe_if_due() -> void
        {
            if (!g_pending_turn_write_probe
                || std::chrono::steady_clock::now()
                    < g_pending_turn_write_probe->restore_after)
            {
                return;
            }

            auto& pending = *g_pending_turn_write_probe;
            auto& result = pending.result;
            try
            {
                result.actual_hold_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - pending.started_at).count();

                bool identity_validated{};
                UObjectGlobals::ForEachUObject([&](UObject* object,
                                                    [[maybe_unused]] int32_t object_index,
                                                    [[maybe_unused]] int32_t chunk_index) {
                    if (object != pending.card)
                    {
                        return LoopAction::Continue;
                    }

                    const std::string full_name = to_string(object->GetFullName());
                    if (classify(full_name) == "BP_InGameCard_C"
                        && is_live_instance(full_name, "BP_InGameCard_C")
                        && read_native_value<const void*>(object, InGameCardStatePointerOffset)
                            == pending.state
                        && address_is_writable(
                            static_cast<const std::byte*>(pending.state)
                                + CardStateTurnAdjustmentOffset,
                            sizeof(std::int32_t))
                        && address_is_writable(
                            static_cast<const std::byte*>(pending.state)
                                + CardStateTurnBaseOffset,
                            sizeof(std::int32_t)))
                    {
                        identity_validated = true;
                    }
                    return LoopAction::Continue;
                });
                result.restore_identity_validated = identity_validated;

                if (!identity_validated)
                {
                    result.status = "failed";
                    result.reason = "target identity changed during hold; restore was not attempted";
                }
                else
                {
                    result.before_restore_base = read_native_value<std::int32_t>(
                        pending.state, CardStateTurnBaseOffset);
                    result.before_restore_adjustment = read_native_value<std::int32_t>(
                        pending.state, CardStateTurnAdjustmentOffset);
                    result.before_restore_computed = static_cast<std::int64_t>(
                        result.before_restore_base) + result.before_restore_adjustment;
                    if (result.before_restore_base != result.before_base
                        || result.before_restore_adjustment != result.test_adjustment
                        || result.before_restore_computed != result.test_computed)
                    {
                        result.status = "failed";
                        result.reason = "turn state changed independently during hold; values were not overwritten";
                        result.restored_base = result.before_restore_base;
                        result.restored_adjustment = result.before_restore_adjustment;
                        result.restored_computed = result.before_restore_computed;
                    }
                    else
                    {
                        Int32FieldRestoreGuard restore_guard{
                            pending.state,
                            CardStateTurnAdjustmentOffset,
                            result.before_adjustment};
                        write_native_value(
                            pending.state,
                            CardStateTurnAdjustmentOffset,
                            result.before_adjustment);
                        restore_guard.active = false;

                        result.restored_base = read_native_value<std::int32_t>(
                            pending.state, CardStateTurnBaseOffset);
                        result.restored_adjustment = read_native_value<std::int32_t>(
                            pending.state, CardStateTurnAdjustmentOffset);
                        result.restored_computed = static_cast<std::int64_t>(
                            result.restored_base) + result.restored_adjustment;

                        const auto restored_getter = export_zero_argument_getter(
                            pending.card, STR("getCurrentTurnCounter"));
                        const auto restored_value = restored_getter
                            ? parse_int32(restored_getter->value)
                            : std::nullopt;
                        result.restored_getter = restored_value.value_or(-1001);

                        const bool passed = result.restored_base == result.before_base
                            && result.restored_adjustment == result.before_adjustment
                            && result.restored_computed == result.before_computed
                            && restored_value == result.before_computed;
                        result.status = passed ? "passed" : "failed";
                        result.reason = passed
                            ? "temporary turn write survived the timed hold and was restored"
                            : "timed turn restore verification did not match";
                    }
                }

                TurnWriteProbeResult completed_result = result;
                g_pending_turn_write_probe.reset();
                const auto path = write_turn_probe_report(completed_result);
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Timed turn write probe {}: {}; report: {}\n"),
                    to_wstring(completed_result.status),
                    to_wstring(completed_result.reason),
                    path.wstring());
            }
            catch (const std::exception& error)
            {
                auto failed_result = result;
                failed_result.status = "failed";
                failed_result.reason = "exception during timed turn restore: "
                    + std::string{error.what()};
                if (pending.state
                    && address_is_writable(
                        static_cast<const std::byte*>(pending.state)
                            + CardStateTurnAdjustmentOffset,
                        sizeof(std::int32_t))
                    && read_native_value<std::int32_t>(
                        pending.state, CardStateTurnAdjustmentOffset)
                        == result.test_adjustment)
                {
                    write_native_value(
                        pending.state,
                        CardStateTurnAdjustmentOffset,
                        result.before_adjustment);
                    failed_result.restored_adjustment = result.before_adjustment;
                    failed_result.restored_computed = static_cast<std::int64_t>(
                        result.before_base) + result.before_adjustment;
                }
                g_pending_turn_write_probe.reset();
                try
                {
                    const auto path = write_turn_probe_report(failed_result);
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Timed turn restore failed safely: {}; report: {}\n"),
                        to_wstring(error.what()),
                        path.wstring());
                }
                catch (...)
                {
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Timed turn restore and report both failed: {}\n"),
                        to_wstring(error.what()));
                }
            }
        }
    } // namespace

    class QuantumCheckpointMod final : public CppUserModBase
    {
      public:
        QuantumCheckpointMod()
        {
            ModName = STR("QuantumCheckpoint");
            ModVersion = STR("0.12.1-dev");
            ModDescription = STR("Route C checkpoint with optional exact-state supplements");
            ModAuthors = STR("zaofenMachine and contributors");
            ModIntendedSDKVersion = STR("3.0.1");
        }

        ~QuantumCheckpointMod() override
        {
            try
            {
                if (g_spawn_next_wave_function && g_spawn_next_wave_hook_ids)
                {
                    UObjectGlobals::UnregisterHook(
                        g_spawn_next_wave_function, *g_spawn_next_wave_hook_ids);
                }
                if (g_spawn_wave_index_function && g_spawn_wave_index_hook_ids)
                {
                    UObjectGlobals::UnregisterHook(
                        g_spawn_wave_index_function, *g_spawn_wave_index_hook_ids);
                }
            }
            catch (...)
            {
            }
            g_spawn_next_wave_hook_ids.reset();
            g_spawn_wave_index_hook_ids.reset();
        }

        auto on_program_start() -> void override
        {
            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F1,
                {Input::ModifierKey::CONTROL},
                []() {
                    append_route_c_trace("hotkey.export.received");
                    g_export_requested.store(true, std::memory_order_release);
                });

            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F12,
                {Input::ModifierKey::CONTROL, Input::ModifierKey::SHIFT},
                []() { g_health_write_probe_requested.store(true, std::memory_order_release); });

            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F9,
                {Input::ModifierKey::CONTROL, Input::ModifierKey::SHIFT},
                []() { g_turn_write_probe_requested.store(true, std::memory_order_release); });

            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F5,
                {Input::ModifierKey::CONTROL, Input::ModifierKey::SHIFT},
                []() {
                    append_route_c_trace("hotkey.save.received");
                    g_route_c_save_requested.store(true, std::memory_order_release);
                });

            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F6,
                {Input::ModifierKey::CONTROL, Input::ModifierKey::SHIFT},
                []() {
                    append_route_c_trace("hotkey.load.received");
                    g_route_c_load_requested.store(true, std::memory_order_release);
                });

            Output::send<LogLevel::Verbose>(
                STR("[QuantumCheckpoint] Loaded Route C prototype; waves auto-save in supported dungeons, Ctrl+Shift+F5 saves, Ctrl+Shift+F6 restores, Ctrl+F1 exports.\n"));
        }

        auto on_unreal_init() -> void override
        {
            g_spawn_wave_index_function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr,
                nullptr,
                STR("/Script/Quantum.SpawnController:spawnWaveIndex"));
            g_spawn_next_wave_function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr,
                nullptr,
                STR("/Script/Quantum.SpawnController:spawnNextWave"));
            g_spawn_controller_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr,
                nullptr,
                STR("/Script/Quantum.SpawnController"));
            try
            {
                Hook::RegisterBeginPlayPreCallback(route_c_actor_begin_play_pre);
                Hook::RegisterBeginPlayPostCallback(route_c_actor_begin_play_post);
                g_begin_play_callbacks_registered = true;
                append_route_c_trace("spawn-controller-begin-play-hooks.ready");
            }
            catch (const std::exception& error)
            {
                append_route_c_trace_failure(
                    "spawn-controller-begin-play-hooks.failed", error.what());
            }
            g_unreal_ready.store(true, std::memory_order_release);
            if (g_spawn_wave_index_function && g_spawn_next_wave_function
                && g_spawn_controller_class)
            {
                try
                {
                    g_spawn_wave_index_hook_ids = UObjectGlobals::RegisterHook(
                        g_spawn_wave_index_function,
                        route_c_native_spawn_wave_pre,
                        route_c_native_spawn_wave_post,
                        nullptr);
                    g_spawn_next_wave_hook_ids = UObjectGlobals::RegisterHook(
                        g_spawn_next_wave_function,
                        route_c_native_spawn_next_pre,
                        route_c_native_spawn_next_post,
                        nullptr);
                    append_route_c_trace("native-wave-hooks.ready");
                    Output::send<LogLevel::Verbose>(
                        STR("[QuantumCheckpoint] Unreal reflection and native Route C wave hooks are ready.\n"));
                }
                catch (const std::exception& error)
                {
                    append_route_c_trace_failure("native-wave-hooks.failed", error.what());
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Route C native wave hook registration failed: {}\n"),
                        to_wstring(error.what()));
                }
            }
            else
            {
                Output::send<LogLevel::Error>(
                    STR("[QuantumCheckpoint] Route C disabled: a required SpawnController function was not found.\n"));
            }
        }

        auto on_update() -> void override
        {
            try
            {
                update_route_c_restore();
                update_route_c_capture();
                update_route_c_wave_poll();
                finish_health_write_probe_if_due();
                finish_turn_write_probe_if_due();

                if (g_route_c_save_requested.exchange(false, std::memory_order_acq_rel))
                {
                    append_route_c_trace("manual-save.dispatch");
                    try
                    {
                        const auto path = capture_route_c_checkpoint();
                        Output::send<LogLevel::Verbose>(
                            STR("[QuantumCheckpoint] Manual Route C checkpoint saved: {}\n"),
                            path.wstring());
                    }
                    catch (const std::exception& error)
                    {
                        append_route_c_trace_failure("manual-save.refused", error.what());
                        Output::send<LogLevel::Warning>(
                            STR("[QuantumCheckpoint] Manual Route C checkpoint refused: {}\n"),
                            to_wstring(error.what()));
                    }
                }

                if (g_route_c_load_requested.exchange(false, std::memory_order_acq_rel))
                {
                    append_route_c_trace("manual-load.dispatch");
                    try
                    {
                    if (!g_spawn_controller_class || !g_begin_play_callbacks_registered)
                        {
                            throw std::runtime_error{
                                "Route C SpawnController BeginPlay hook is unavailable"};
                        }
                        begin_route_c_restore();
                    }
                    catch (const std::exception& error)
                    {
                        append_route_c_trace("manual-load.refused");
                        Output::send<LogLevel::Error>(
                            STR("[QuantumCheckpoint] Route C restore request refused: {}\n"),
                            to_wstring(error.what()));
                    }
                }

                if (g_export_requested.exchange(false, std::memory_order_acq_rel))
                {
                    append_route_c_trace("inventory-export.dispatch");
                    if (g_unreal_ready.load(std::memory_order_acquire))
                    {
                        export_inventory();
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[QuantumCheckpoint] Unreal is not initialized; export ignored.\n"));
                    }
                }

                if (g_health_write_probe_requested.exchange(false, std::memory_order_acq_rel))
                {
                    if (g_unreal_ready.load(std::memory_order_acquire))
                    {
                        run_health_write_probe();
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[QuantumCheckpoint] Unreal is not initialized; health write probe ignored.\n"));
                    }
                }

                if (g_turn_write_probe_requested.exchange(false, std::memory_order_acq_rel))
                {
                    if (g_unreal_ready.load(std::memory_order_acquire))
                    {
                        run_turn_write_probe();
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[QuantumCheckpoint] Unreal is not initialized; turn write probe ignored.\n"));
                    }
                }
            }
            catch (const std::exception& error)
            {
                append_route_c_trace("on-update.unhandled-standard-exception");
                try
                {
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] Unhandled update error was contained: {}\n"),
                        to_wstring(error.what()));
                }
                catch (...)
                {
                    append_route_c_trace("on-update.error-reporting-failed");
                }
            }
            catch (...)
            {
                append_route_c_trace("on-update.unhandled-unknown-exception");
                try
                {
                    Output::send<LogLevel::Error>(
                        STR("[QuantumCheckpoint] An unknown update error was contained.\n"));
                }
                catch (...)
                {
                    append_route_c_trace("on-update.unknown-error-reporting-failed");
                }
            }
        }
    };
} // namespace QuantumCheckpoint

#define QUANTUM_CHECKPOINT_API __declspec(dllexport)

extern "C"
{
    QUANTUM_CHECKPOINT_API RC::CppUserModBase* start_mod()
    {
        return new QuantumCheckpoint::QuantumCheckpointMod();
    }

    QUANTUM_CHECKPOINT_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
