#pragma once
#include "PlayerAttackState.hpp"

namespace QuantumCheckpoint
{
    struct PlayerHealthState
    {
        std::int32_t base_health{};
        std::int32_t max_health{};
        std::vector<PlayerStatModifier> modifiers{};
        bool operator==(const PlayerHealthState&) const = default;
    };

    // This slice requires native +0x120 adjustment to be zero. Current card
    // health remains in playerFieldStates and may legally exceed this maximum.
    auto validate_player_health_state(const PlayerHealthState& state, std::string& error) -> bool;
    // HAND-only semantic policy; the caller supplies the reflected selected
    // immutable definition HP. Schemas 1/2 require equality; schema 3 permits
    // nonnegative base gains. Live ownership, current=max, and layout membership
    // proof remain runtime checks, outside this pure scalar/modifier validator.
    auto validate_player_hand_health_for_definition(const PlayerHealthState& state,
        std::int32_t definition_health, int hand_schema, std::string& error) -> bool;
    auto serialize_player_health_states(const std::vector<PlayerHealthState>& states) -> std::string;
    auto parse_player_health_states(std::string_view text, std::string& error)
        -> std::optional<std::vector<PlayerHealthState>>;
}
