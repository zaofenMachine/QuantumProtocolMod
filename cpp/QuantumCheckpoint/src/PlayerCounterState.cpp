#include "PlayerCounterState.hpp"
#include "PlayerAttackState.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

namespace QuantumCheckpoint
{
    namespace
    {
        auto split(std::string_view text, char delimiter) -> std::vector<std::string_view>
        {
            std::vector<std::string_view> result{};
            for (;;)
            {
                const auto end = text.find(delimiter);
                result.push_back(text.substr(0,end));
                if (end == std::string_view::npos) return result;
                text.remove_prefix(end + 1);
            }
        }

        auto integer(std::string_view text, std::int32_t& value) -> bool
        {
            const auto [end,error] = std::from_chars(text.data(),text.data() + text.size(),value);
            return !text.empty() && error == std::errc{} && end == text.data() + text.size()
                && text == std::to_string(value);
        }
    }

    auto validate_player_counter_state(const PlayerCounterState& state, std::string& error) -> bool
    {
        error.clear();
        if (state.generic < 0 || state.generic > PlayerCounterRestoreMaximum)
        {
            error = "generic counter exceeds the bounded restore slice";
            return false;
        }
        std::vector<PlayerStatModifier> tags{};
        std::int64_t positive{},negative{};
        for (const auto& counter : state.special)
        {
            tags.push_back({counter.tag,counter.count,-1,0});
            if (counter.count > 0) positive += counter.count;
            else negative += counter.count;
        }
        if (!validate_player_stat_modifiers(tags,error)) return false;
        if (positive > PlayerCounterRestoreMaximum || negative < -PlayerCounterRestoreMaximum)
        {
            error = "special counter replay totals exceed the bounded restore slice";
            return false;
        }
        return true;
    }

    auto player_counter_restore_order(const PlayerCounterState& state) -> std::vector<std::size_t>
    {
        std::vector<std::size_t> result{};
        for (std::size_t index{}; index < state.special.size(); ++index) result.push_back(index);
        std::stable_sort(result.begin(),result.end(),[&](auto a,auto b) {
            return (state.special[a].count < 0) < (state.special[b].count < 0);
        });
        return result;
    }

    auto serialize_player_counter_states(const std::vector<PlayerCounterState>& states) -> std::string
    {
        std::string result{};
        for (const auto& state : states)
        {
            if (!result.empty()) result += ';';
            result += std::to_string(state.generic);
            auto counters = state.special;
            std::sort(counters.begin(),counters.end(),[](const auto& a,const auto& b) { return a.tag < b.tag; });
            for (const auto& counter : counters)
                result += '|' + counter.tag + ',' + std::to_string(counter.count);
        }
        return result;
    }

    auto parse_player_counter_states(std::string_view text, std::string& error)
        -> std::optional<std::vector<PlayerCounterState>>
    {
        error.clear();
        if (text.empty() || text.size() > 65536)
        {
            error = "field counter records are empty or too large";
            return std::nullopt;
        }
        const auto records = split(text,';');
        if (records.size() > 10)
        {
            error = "field counter records exceed ten combat slots";
            return std::nullopt;
        }
        std::vector<PlayerCounterState> result{};
        for (const auto record : records)
        {
            const auto values = split(record,'|');
            PlayerCounterState state{};
            if (values.size() > 33 || !integer(values.front(),state.generic))
            {
                error = "field counter record has invalid generic count or too many tags";
                return std::nullopt;
            }
            for (std::size_t index = 1; index < values.size(); ++index)
            {
                const auto fields = split(values[index],',');
                PlayerSpecialCounter counter{};
                if (fields.size() != 2 || !integer(fields[1],counter.count))
                {
                    error = "field special counter record has invalid fields";
                    return std::nullopt;
                }
                counter.tag = fields[0];
                state.special.push_back(std::move(counter));
            }
            if (!validate_player_counter_state(state,error)) return std::nullopt;
            result.push_back(std::move(state));
        }
        if (serialize_player_counter_states(result) != text)
        {
            error = "field counter records are not canonical";
            return std::nullopt;
        }
        return result;
    }
}
