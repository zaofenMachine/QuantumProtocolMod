#include "PlayerRestorePlan.hpp"
#include "CheckpointPersistence.hpp"

#include <algorithm>

namespace QuantumCheckpoint
{
    auto plan_player_field_restore(
        const ExactPlayerFieldCheckpoint& checkpoint,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::string& error) -> std::optional<PlayerFieldRestorePlan>
    {
        error.clear();
        const auto keys = [&](const std::string& array)
            -> std::optional<std::vector<std::string>> {
            const auto cards = split_route_c_unreal_array(array, error);
            if (!cards) return std::nullopt;
            std::vector<std::string> result{};
            for (const auto& card : *cards)
            {
                auto key = exact_card_identity_key_from_instance(card, error);
                if (!key) return std::nullopt;
                result.push_back(std::move(*key));
            }
            return result;
        };
        const auto deck = keys(checkpoint.player_deck);
        const auto hand = keys(checkpoint.player_hand);
        const auto trash = keys(checkpoint.player_trash);
        const auto field = keys(checkpoint.player_field);
        if (!deck || !hand || !trash || !field || field->empty())
        {
            error = "invalid player-field plan arrays: " + error;
            return std::nullopt;
        }
        if (candidates.size() > 128
            || candidates.size() != deck->size() + hand->size() + trash->size() + field->size()
            || std::any_of(candidates.begin(), candidates.end(), [](const auto& card) {
                return card.location > 1 || card.identity.empty();
            }))
        {
            error = "player-field plan has an invalid staged card count or location";
            return std::nullopt;
        }
        const auto overlaps = [](const auto& left, const auto& right) {
            return std::any_of(left.begin(), left.end(), [&](const auto& key) {
                return std::find(right.begin(), right.end(), key) != right.end();
            });
        };
        // Keep the existing hand/field guard and apply it to the new trash
        // combination. Same-identity copies within a single destination are
        // supported; cross-destination dynamic state needs separate evidence.
        if (overlaps(*hand, *field) || overlaps(*hand, *trash) || overlaps(*field, *trash))
        {
            error = "shared hand/field/trash identity is outside the guarded player layout";
            return std::nullopt;
        }

        std::vector<bool> used(candidates.size());
        const auto select = [&](const std::string& key, std::uint8_t location)
            -> std::optional<std::size_t> {
            for (std::size_t index{}; index < candidates.size(); ++index)
            {
                if (!used[index] && candidates[index].identity == key
                    && candidates[index].location == location)
                {
                    used[index] = true;
                    return index;
                }
            }
            return std::nullopt;
        };
        // Reserve the cards that must stay in DECK/HAND first. Otherwise an
        // equivalent card in DECK can be moved while startup overflow is left
        // in HAND, silently changing the final zone counts.
        for (const auto& key : *deck)
        {
            if (!select(key, 1))
            {
                error = "staged deck cannot retain every saved deck identity";
                return std::nullopt;
            }
        }
        for (const auto& key : *hand)
        {
            if (!select(key, 0))
            {
                error = "staged hand cannot retain every saved hand identity";
                return std::nullopt;
            }
        }
        PlayerFieldRestorePlan plan{};
        const auto extras = [&](const auto& expected, auto& output) {
            for (const auto& key : expected)
            {
                auto index = select(key, 0);
                if (!index) index = select(key, 1);
                if (!index) return false;
                output.push_back(*index);
            }
            return true;
        };
        if (!extras(*trash, plan.trash_candidates) || !extras(*field, plan.field_candidates)
            || std::find(used.begin(), used.end(), false) != used.end())
        {
            error = "staged cards do not match every saved trash/field identity and upgrade";
            return std::nullopt;
        }
        const auto frees_hand_slot = [&](const auto& indices) {
            return std::any_of(indices.begin(), indices.end(), [&](auto index) {
                return candidates[index].location == 0;
            });
        };
        if (!frees_hand_slot(plan.trash_candidates) && !frees_hand_slot(plan.field_candidates))
        {
            error = "native initial hand has no movable target to free a guarded staging slot";
            return std::nullopt;
        }
        return plan;
    }
}
