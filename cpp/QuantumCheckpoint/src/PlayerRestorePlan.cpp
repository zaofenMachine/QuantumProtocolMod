#include "PlayerRestorePlan.hpp"
#include "CheckpointPersistence.hpp"

#include <algorithm>

namespace QuantumCheckpoint
{
    auto plan_player_hand_staging(
        const ExactPlayerFieldCheckpoint& layout,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::size_t hand_limit,
        std::string& error) -> std::optional<PlayerHandStagingPlan>
    {
        error.clear();
        const auto keys = [&](const std::string& text)
            -> std::optional<std::vector<std::string>> {
            const auto cards = split_route_c_unreal_array(text, error);
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
        const auto deck = keys(layout.player_deck), hand = keys(layout.player_hand);
        const auto trash = keys(layout.player_trash), field = keys(layout.player_field);
        if ((layout.schema_version != 2 && layout.schema_version != 3) || !deck || !hand || !trash || !field
            || hand_limit == 0 || hand_limit > 16 || hand->size() > hand_limit
            || candidates.empty() || candidates.size() > 128)
        {
            error = "hand staging requires native-order arrays and a valid hand capacity";
            return std::nullopt;
        }
        const auto unsupported_overlap = [](const auto& left, const auto& right) {
            return std::any_of(left.begin(), left.end(), [&](const auto& key) {
                return std::find(right.begin(), right.end(), key) != right.end()
                    && !supports_plain_player_field_card(
                        std::string_view{key}.substr(0, key.find('@')));
            });
        };
        if (unsupported_overlap(*hand, *trash) || unsupported_overlap(*hand, *field)
            || unsupported_overlap(*trash, *field))
        {
            error = "special-card overlap is outside the guarded player layout";
            return std::nullopt;
        }
        std::vector<std::string> expected = *deck;
        expected.insert(expected.end(), trash->begin(), trash->end());
        expected.insert(expected.end(), field->begin(), field->end());
        expected.insert(expected.end(), hand->rbegin(), hand->rend());
        std::vector<std::size_t> deck_indices{}, hand_indices{};
        for (std::size_t index{}; index < candidates.size(); ++index)
        {
            const auto& card = candidates[index];
            if (card.identity.empty() || card.location > 1)
            {
                error = "hand staging found an invalid candidate";
                return std::nullopt;
            }
            (card.location == 0 ? hand_indices : deck_indices).push_back(index);
        }
        if (hand_indices.size() > hand_limit)
        {
            error = "native initial hand exceeds its public capacity";
            return std::nullopt;
        }
        auto reunited = deck_indices;
        reunited.insert(reunited.end(), hand_indices.rbegin(), hand_indices.rend());
        if (expected.size() != reunited.size())
        {
            error = "hand staging changed the complete player card count";
            return std::nullopt;
        }
        for (std::size_t index{}; index < reunited.size(); ++index)
        {
            if (candidates[reunited[index]].identity != expected[index])
            {
                error = "native startup does not reconstruct the saved fixed-order sequence";
                return std::nullopt;
            }
        }
        PlayerHandStagingPlan plan{};
        for (auto index = hand_indices.rbegin(); index != hand_indices.rend(); ++index)
            plan.moves.push_back({*index, 1});
        for (std::size_t index{}; index < hand->size(); ++index)
            plan.moves.push_back({reunited[reunited.size() - 1 - index], 0});
        plan.before_field_move_count = plan.moves.size();
        if (!field->empty() && hand->size() == hand_limit)
            --plan.before_field_move_count;
        return plan;
    }

    static auto plan_player_cards_restore(
        const ExactPlayerFieldCheckpoint& checkpoint,
        const std::vector<PlayerRestoreCandidate>& candidates,
        bool require_field,
        std::string& error,
        std::size_t hand_limit = 0) -> std::optional<PlayerFieldRestorePlan>
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
        if (!deck || !hand || !trash || !field || (require_field && field->empty()))
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
        const auto unsupported_overlap = [&](const auto& left, const auto& right) {
            return std::any_of(left.begin(), left.end(), [&](const auto& key) {
                return std::find(right.begin(), right.end(), key) != right.end()
                    && (checkpoint.schema_version == 1
                        || !supports_plain_player_field_card(
                            std::string_view{key}.substr(0, key.find('@'))));
            });
        };
        // Native-order captures identify which copies remain in HAND/DECK.
        // Plain field cards can then receive distinct trash/field assignments;
        // per-slot health and turn-active state are applied after native creation.
        // Keep the legacy sorted-view guard and special-card overlap boundary.
        if (unsupported_overlap(*hand, *field) || unsupported_overlap(*hand, *trash)
            || unsupported_overlap(*field, *trash))
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
        const auto reserve_zone = [&](const auto& expected, std::uint8_t location) {
            std::size_t cursor{};
            for (const auto& key : expected)
            {
                if (checkpoint.schema_version == 1)
                {
                    if (!select(key, location)) return false;
                    continue;
                }
                // Schema 2 candidates follow each zone's native order. Reserve
                // a subsequence, not the first equal key anywhere in the array.
                while (cursor < candidates.size()
                       && (used[cursor] || candidates[cursor].location != location
                           || candidates[cursor].identity != key)) ++cursor;
                if (cursor == candidates.size()) return false;
                used[cursor++] = true;
            }
            return true;
        };
        if (!reserve_zone(*deck, 1))
        {
            error = "staged deck cannot retain every saved deck identity in order";
            return std::nullopt;
        }
        if (!reserve_zone(*hand, 0))
        {
            error = "staged hand cannot retain every saved hand identity in order";
            return std::nullopt;
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
        const auto hand_count = static_cast<std::size_t>(std::count_if(
            candidates.begin(), candidates.end(), [](const auto& card) { return card.location == 0; }));
        const bool has_staging_room = hand_limit > hand_count && hand_limit <= 16;
        if (!field->empty() && !has_staging_room
            && !frees_hand_slot(plan.trash_candidates) && !frees_hand_slot(plan.field_candidates))
        {
            error = "native initial hand has no movable target to free a guarded staging slot";
            return std::nullopt;
        }
        return plan;
    }

    auto plan_player_field_restore(
        const ExactPlayerFieldCheckpoint& checkpoint,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::string& error,
        std::size_t hand_limit) -> std::optional<PlayerFieldRestorePlan>
    {
        return plan_player_cards_restore(checkpoint, candidates, true, error, hand_limit);
    }

    auto plan_player_trash_restore(
        const ExactPlayerTrashCheckpoint& checkpoint,
        const std::vector<PlayerRestoreCandidate>& candidates,
        std::string& error) -> std::optional<PlayerFieldRestorePlan>
    {
        ExactPlayerFieldCheckpoint layout{};
        layout.schema_version = checkpoint.schema_version;
        layout.player_deck = checkpoint.player_deck;
        layout.player_hand = checkpoint.player_hand;
        layout.player_trash = checkpoint.player_trash;
        layout.player_field = "()";
        return plan_player_cards_restore(layout, candidates, false, error);
    }
}
