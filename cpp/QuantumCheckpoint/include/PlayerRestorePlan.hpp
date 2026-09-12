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

    struct PlayerHandStagingMove
    {
        std::size_t candidate{};
        std::uint8_t destination{};
    };

    struct PlayerHandStagingPlan
    {
        std::vector<PlayerHandStagingMove> moves{};
        // If a full saved hand needs a FIELD staging slot, finish this prefix,
        // restore FIELD/TRASH, then execute the remaining final hand move.
        std::size_t before_field_move_count{};
    };

    auto plan_player_hand_staging(
        const ExactPlayerFieldCheckpoint& layout,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::size_t hand_limit,
        std::string& error) -> std::optional<PlayerHandStagingPlan>;

    auto plan_player_field_restore(
        const ExactPlayerFieldCheckpoint& checkpoint,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::string& error,
        std::size_t hand_limit = 0) -> std::optional<PlayerFieldRestorePlan>;

    auto plan_player_trash_restore(
        const ExactPlayerTrashCheckpoint& checkpoint,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::string& error) -> std::optional<PlayerFieldRestorePlan>;
}
