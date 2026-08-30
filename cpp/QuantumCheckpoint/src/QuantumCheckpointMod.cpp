#include "CheckpointSchema.hpp"

#include <array>
#include <atomic>
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
    } // namespace

    class QuantumCheckpointMod final : public CppUserModBase
    {
      public:
        QuantumCheckpointMod()
        {
            ModName = STR("QuantumCheckpoint");
            ModVersion = STR("0.5.0-dev");
            ModDescription = STR("Read-only battle inventory groundwork for persistent checkpoints");
            ModAuthors = STR("zaofenMachine and contributors");
            ModIntendedSDKVersion = STR("3.0.1");
        }

        auto on_program_start() -> void override
        {
            UE4SSProgram::get_program().register_keydown_event(
                Input::Key::F1,
                {Input::ModifierKey::CONTROL},
                []() { g_export_requested.store(true, std::memory_order_release); });

            Output::send<LogLevel::Verbose>(
                STR("[QuantumCheckpoint] Loaded read-only C++ inventory prototype; Ctrl+F1 requests an export.\n"));
        }

        auto on_unreal_init() -> void override
        {
            g_unreal_ready.store(true, std::memory_order_release);
            Output::send<LogLevel::Verbose>(
                STR("[QuantumCheckpoint] Unreal reflection is ready.\n"));
        }

        auto on_update() -> void override
        {
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
