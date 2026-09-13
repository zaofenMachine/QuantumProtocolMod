#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace QuantumCheckpoint
{
    struct PlayerStatModifier
    {
        std::string tag{};
        std::int32_t amount{};
        std::int32_t limit{-1};
        // Bit N represents ESTAT_MODIFIER_FLAGS value N, including NONE (0).
        std::uint8_t flags{};
        bool operator==(const PlayerStatModifier&) const = default;
    };
    using PlayerAttackModifier = PlayerStatModifier;

    struct PlayerAttackState
    {
        std::int32_t base_attack{};
        std::int32_t current_attack{};
        std::vector<PlayerStatModifier> modifiers{};
        bool operator==(const PlayerAttackState&) const = default;
    };

    auto validate_player_attack_state(const PlayerAttackState& state, std::string& error) -> bool;
    auto validate_player_stat_modifiers(const std::vector<PlayerStatModifier>& modifiers, std::string& error) -> bool;
    // Native add normalizes negative modifiers immediately. Replay non-negative
    // entries first so a valid later buff cannot arrive after a debuff was clipped.
    auto player_attack_restore_order(const PlayerAttackState& state) -> std::vector<std::size_t>;
    auto player_stat_restore_order(const std::vector<PlayerStatModifier>& modifiers) -> std::vector<std::size_t>;
    // Field cards remain aligned with the existing row/index ordered field list.
    // Each record: base,current|tag,amount,limit,flagMask[|...];next card.
    auto serialize_player_attack_states(const std::vector<PlayerAttackState>& states) -> std::string;
    auto parse_player_attack_states(std::string_view text, std::string& error)
        -> std::optional<std::vector<PlayerAttackState>>;
}
