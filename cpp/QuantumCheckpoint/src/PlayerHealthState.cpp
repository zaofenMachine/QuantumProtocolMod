#include "PlayerHealthState.hpp"
#include "CheckpointSchema.hpp"
#include <utility>

namespace QuantumCheckpoint
{
    auto validate_player_health_state(const PlayerHealthState& state, std::string& error) -> bool
    {
        // Reuse the canonical scalar/modifier grammar and its tag/limit/flag
        // bounds. A positive maximum rules out the attack parser's zero clamp.
        if (state.base_health <= 0 || state.max_health <= 0)
        {
            error = "field health restoration requires positive base and effective maximum";
            return false;
        }
        return validate_player_attack_state({state.base_health, state.max_health, state.modifiers}, error);
    }

    auto validate_player_hand_health_for_definition(const PlayerHealthState& state,
        std::int32_t definition_health, int hand_schema, std::string& error) -> bool
    {
        error.clear();
        if (hand_schema < 1 || hand_schema > ExactPlayerHandHealthSchemaVersion)
        {
            error = "unsupported HAND health schema for definition validation";
            return false;
        }
        if (!validate_player_health_state({definition_health, definition_health, {}}, error))
        {
            error = "HAND definition health is outside supported positive bounds: " + error;
            return false;
        }
        if (!validate_player_health_state(state, error)) return false;
        for (const auto& modifier : state.modifiers)
        {
            if (modifier.amount < 0)
            {
                error = "HAND health requires nonnegative HEALTH modifiers";
                return false;
            }
        }
        if (hand_schema < ExactPlayerHandBaseHealthSchemaVersion)
        {
            if (state.base_health != definition_health)
            {
                error = "legacy HAND health requires base HP equal to its selected definition";
                return false;
            }
        }
        else if (state.base_health < definition_health)
        {
            error = "HAND base HP below its selected definition is unsupported";
            return false;
        }
        return true;
    }

    auto serialize_player_health_states(const std::vector<PlayerHealthState>& states) -> std::string
    {
        std::vector<PlayerAttackState> records{};
        for (const auto& state : states) records.push_back({state.base_health,state.max_health,state.modifiers});
        return serialize_player_attack_states(records);
    }

    auto parse_player_health_states(std::string_view text, std::string& error)
        -> std::optional<std::vector<PlayerHealthState>>
    {
        const auto records = parse_player_attack_states(text, error);
        if (!records) return std::nullopt;
        std::vector<PlayerHealthState> result{};
        for (const auto& record : *records)
        {
            PlayerHealthState state{record.base_attack,record.current_attack,record.modifiers};
            if (!validate_player_health_state(state, error)) return std::nullopt;
            result.push_back(std::move(state));
        }
        return result;
    }
}
