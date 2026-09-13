#include "PlayerAttackState.hpp"

#include <algorithm>
#include <charconv>
#include <set>

namespace QuantumCheckpoint
{
    namespace
    {
        auto parts(std::string_view text, char delimiter) -> std::vector<std::string_view>
        {
            std::vector<std::string_view> result{};
            for (;;)
            {
                const auto end = text.find(delimiter);
                result.push_back(text.substr(0, end));
                if (end == std::string_view::npos) return result;
                text.remove_prefix(end + 1);
            }
        }

        auto integer(std::string_view text, std::int32_t& result) -> bool
        {
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
            return !text.empty() && error == std::errc{} && end == text.data() + text.size()
                && text == std::to_string(result);
        }

        auto ascii_key(std::string_view tag) -> std::optional<std::string>
        {
            if (tag.empty() || tag.size() > 128) return std::nullopt;
            std::string result{};
            for (const unsigned char ch : tag)
            {
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                    || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-'))
                    return std::nullopt;
                result += static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
            }
            if (result == "none") return std::nullopt;
            return result;
        }
    }

    auto validate_player_attack_state(const PlayerAttackState& state, std::string& error) -> bool
    {
        error.clear();
        if (state.base_attack < 0 || state.base_attack > 100000
            || state.current_attack < 0 || state.current_attack > 100000 || state.modifiers.size() > 32)
        {
            error = "player attack state exceeds supported scalar or modifier bounds";
            return false;
        }
        std::set<std::string> keys{};
        std::int64_t computed = state.base_attack;
        for (const auto& modifier : state.modifiers)
        {
            const auto key = ascii_key(modifier.tag);
            if (!key || !keys.insert(*key).second || modifier.amount < -100000 || modifier.amount > 100000
                || modifier.limit < -1 || modifier.limit > 100000 || modifier.flags > 7
                || (modifier.limit > 0 && modifier.amount > modifier.limit))
            {
                error = "player attack modifier has an invalid tag, duplicate FName, amount, limit, or flags";
                return false;
            }
            computed += modifier.amount;
        }
        if (std::max<std::int64_t>(0, computed) != state.current_attack)
        {
            error = "player attack value disagrees with base and saved modifiers";
            return false;
        }
        return true;
    }

    auto player_attack_restore_order(const PlayerAttackState& state) -> std::vector<std::size_t>
    {
        std::vector<std::size_t> order{};
        for (std::size_t index{}; index < state.modifiers.size(); ++index) order.push_back(index);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
            return (state.modifiers[a].amount < 0) < (state.modifiers[b].amount < 0);
        });
        return order;
    }

    auto serialize_player_attack_states(const std::vector<PlayerAttackState>& states) -> std::string
    {
        std::string result{};
        for (const auto& state : states)
        {
            if (!result.empty()) result += ';';
            result += std::to_string(state.base_attack) + ',' + std::to_string(state.current_attack);
            auto modifiers = state.modifiers;
            std::sort(modifiers.begin(), modifiers.end(), [](const auto& a, const auto& b) { return a.tag < b.tag; });
            for (const auto& modifier : modifiers)
                result += '|' + modifier.tag + ',' + std::to_string(modifier.amount) + ','
                    + std::to_string(modifier.limit) + ',' + std::to_string(modifier.flags);
        }
        return result;
    }

    auto parse_player_attack_states(std::string_view text, std::string& error)
        -> std::optional<std::vector<PlayerAttackState>>
    {
        error.clear();
        if (text.empty() || text.size() > 65536)
        {
            error = "player field attack records are empty or too large";
            return std::nullopt;
        }
        const auto records = parts(text, ';');
        if (records.size() > 10)
        {
            error = "player field attack records exceed ten combat slots";
            return std::nullopt;
        }
        std::vector<PlayerAttackState> result{};
        for (const auto record : records)
        {
            PlayerAttackState state{};
            const auto values = parts(record, '|');
            const auto scalars = parts(values.front(), ',');
            if (scalars.size() != 2 || !integer(scalars[0], state.base_attack)
                || !integer(scalars[1], state.current_attack) || values.size() > 33)
            {
                error = "player field attack record has invalid scalar fields";
                return std::nullopt;
            }
            for (std::size_t index = 1; index < values.size(); ++index)
            {
                const auto fields = parts(values[index], ',');
                PlayerAttackModifier modifier{};
                std::int32_t flags{};
                if (fields.size() != 4 || !integer(fields[1], modifier.amount)
                    || !integer(fields[2], modifier.limit) || !integer(fields[3], flags) || flags < 0 || flags > 7)
                {
                    error = "player field attack modifier has invalid fields";
                    return std::nullopt;
                }
                modifier.tag = fields[0];
                modifier.flags = static_cast<std::uint8_t>(flags);
                state.modifiers.push_back(std::move(modifier));
            }
            if (!validate_player_attack_state(state, error)) return std::nullopt;
            result.push_back(std::move(state));
        }
        if (serialize_player_attack_states(result) != text)
        {
            error = "player field attack records are not canonical";
            return std::nullopt;
        }
        return result;
    }
}
