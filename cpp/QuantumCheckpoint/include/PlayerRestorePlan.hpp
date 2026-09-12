#pragma once

#include "CheckpointSchema.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace QuantumCheckpoint
{
    // References are supplied by the game thread; this plan contains no UObject
    // pointers and is completely validated before the first native card move.
    // For schema 2, candidates must preserve native relative order within each zone.
    struct PlayerRestoreCandidate
    {
        std::string identity{};
        std::uint8_t location{}; // Native HAND = 0, DECK = 1.
    };

    struct PlayerFieldRestorePlan
    {
        std::vector<std::size_t> trash_candidates{}; // Saved trash order.
        std::vector<std::size_t> field_candidates{}; // Saved field slot order.
    };

    auto plan_player_field_restore(
        const ExactPlayerFieldCheckpoint& checkpoint,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::string& error) -> std::optional<PlayerFieldRestorePlan>;

    auto plan_player_trash_restore(
        const ExactPlayerTrashCheckpoint& checkpoint,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::string& error) -> std::optional<PlayerFieldRestorePlan>;
}
