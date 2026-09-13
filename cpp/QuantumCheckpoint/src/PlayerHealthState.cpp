#include "PlayerHealthState.hpp"
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
