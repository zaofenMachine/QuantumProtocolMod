#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace QuantumCheckpoint
{
    // Counter updates create/remove display actors per unit. Bound both positive
    // and negative replay totals, including intermediate states.
    constexpr std::int32_t PlayerCounterRestoreMaximum = 256;

    struct PlayerSpecialCounter
    {
        std::string tag{};
        std::int32_t count{};
        auto operator==(const PlayerSpecialCounter&) const -> bool = default;
    };

    struct PlayerCounterState
    {
        std::int32_t generic{};
        std::vector<PlayerSpecialCounter> special{};
        auto operator==(const PlayerCounterState&) const -> bool = default;
    };

    auto validate_player_counter_state(const PlayerCounterState&, std::string& error) -> bool;
    auto player_counter_restore_order(const PlayerCounterState&) -> std::vector<std::size_t>;
    auto serialize_player_counter_states(const std::vector<PlayerCounterState>&) -> std::string;
    auto parse_player_counter_states(std::string_view, std::string& error)
        -> std::optional<std::vector<PlayerCounterState>>;
}
