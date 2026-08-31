#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "CheckpointSchema.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Input/Handler.hpp>
#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
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
        std::atomic_bool g_unreal_ready{false};

        constexpr std::array<std::string_view, 17> RelevantClassPrefixes{
            "BP_CardEngine_C ",
            "BP_BottomBar_C ",
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

        constexpr std::array<StringViewType, 3> GameInstanceGetters{
            STR("getCurrentDeckRun"),
            STR("getActiveDecklistInstances"),
            STR("getActiveStorage"),
        };

        constexpr std::array<StringViewType, 1> CardGroupGetters{
            STR("getCardInstanceListSorted"),
        };

        constexpr std::array<StringViewType, 6> InGameCardGetters{
            STR("getTag"),
            STR("getId"),
            STR("getCurrentTurnCounter"),
            STR("getCurrentHealth"),
            STR("getCardLocation"),
            STR("getCardInfoInstance"),
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
        constexpr std::uintptr_t CurrentHealthGetterThunkRva = 0x102D100;
        constexpr std::uintptr_t CurrentTurnGetterThunkRva = 0x102D130;
        constexpr std::uintptr_t NativeCurrentTurnGetterRva = 0xE323C0;
        constexpr std::uintptr_t SetCurrentHealthRva = 0xE522E0;
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

            std::vector<uint8_t> parameters(parameter_size, 0);
            object->ProcessEvent(function, parameters.data());

            FString exported{};
            auto* return_value = return_property->ContainerPtrToValuePtr<void>(parameters.data());
            return_property->ExportTextItem(exported, return_value, nullptr, object, 0);
            std::string value = to_string(exported.GetCharArray());
            return_property->DestroyValue_InContainer(parameters.data());

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
            const auto* state = read_native_value<const void*>(
                object,
                InGameCardStatePointerOffset);
            if (!state)
            {
                snapshot.properties.push_back({
                    .name = "nativeDiagnostic:stateObject",
                    .value = "null",
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
                const std::string role = classify(full_name);
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
                    append_getters(snapshot, object, GameInstanceGetters);
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
                auto inventory = collect_inventory();
                if (inventory.objects.empty())
                {
                    Output::send<LogLevel::Warning>(
                        STR("[QuantumCheckpoint] No active battle objects found; report not written.\n"));
                    return;
                }

                const auto report_path = write_inventory(inventory);
                Output::send<LogLevel::Verbose>(
                    STR("[QuantumCheckpoint] Wrote read-only inventory with {} objects: {}\n"),
                    inventory.objects.size(),
                    report_path.wstring());
            }
            catch (const std::exception& error)
            {
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

                std::array<wchar_t, 32768> executable_path{};
                const auto path_length = GetModuleFileNameW(
                    module, executable_path.data(), static_cast<DWORD>(executable_path.size()));
                if (path_length == 0 || path_length >= executable_path.size()
                    || std::filesystem::file_size(executable_path.data()) != ExpectedGameExecutableSize)
                {
                    result.status = "refused";
                    result.reason = "game executable size does not match the validated build";
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
                Output::send<LogLevel::Error>(
                    STR("[QuantumCheckpoint] Timed health restore failed before completion: {}\n"),
                    to_wstring(error.what()));
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
                    std::array<wchar_t, 32768> executable_path{};
                    const auto path_length = GetModuleFileNameW(
                        module, executable_path.data(), static_cast<DWORD>(executable_path.size()));
                    if (path_length == 0 || path_length >= executable_path.size()
                        || std::filesystem::file_size(executable_path.data()) != ExpectedGameExecutableSize)
                    {
                        result.status = "refused";
                        result.reason = "game executable size does not match the validated build";
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
                Output::send<LogLevel::Error>(
                    STR("[QuantumCheckpoint] Timed turn restore failed before completion: {}\n"),
                    to_wstring(error.what()));
            }
        }
    } // namespace

    class QuantumCheckpointMod final : public CppUserModBase
    {
      public:
        QuantumCheckpointMod()
        {
            ModName = STR("QuantumCheckpoint");
            ModVersion = STR("0.8.0-dev");
            ModDescription = STR("Battle inventory exporter and guarded native write experiment");
            ModAuthors = STR("zaofenMachine and contributors");
            ModIntendedSDKVersion = STR("3.0.1");
        }

        auto on_program_start() -> void override
        {
            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F1,
                {Input::ModifierKey::CONTROL},
                []() { g_export_requested.store(true, std::memory_order_release); });

            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F12,
                {Input::ModifierKey::CONTROL, Input::ModifierKey::SHIFT},
                []() { g_health_write_probe_requested.store(true, std::memory_order_release); });

            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F9,
                {Input::ModifierKey::CONTROL, Input::ModifierKey::SHIFT},
                []() { g_turn_write_probe_requested.store(true, std::memory_order_release); });

            Output::send<LogLevel::Verbose>(
                STR("[QuantumCheckpoint] Loaded C++ prototype; Ctrl+F1 exports, Ctrl+Shift+F12 tests health, Ctrl+Shift+F9 tests turn count.\n"));
        }

        auto on_unreal_init() -> void override
        {
            g_unreal_ready.store(true, std::memory_order_release);
            Output::send<LogLevel::Verbose>(
                STR("[QuantumCheckpoint] Unreal reflection is ready.\n"));
        }

        auto on_update() -> void override
        {
            finish_health_write_probe_if_due();
            finish_turn_write_probe_if_due();

            if (g_export_requested.exchange(false, std::memory_order_acq_rel))
            {
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
