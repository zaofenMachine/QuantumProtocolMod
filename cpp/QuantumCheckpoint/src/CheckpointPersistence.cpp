#include "CheckpointPersistence.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

namespace QuantumCheckpoint
{
    namespace
    {
        using JsonValue = std::variant<std::string, std::int64_t>;

        auto json_escape(std::string_view value) -> std::string
        {
            std::string escaped{};
            escaped.reserve(value.size() + 16);
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\b': escaped += "\\b"; break;
                case '\f': escaped += "\\f"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default:
                    if (character < 0x20)
                    {
                        std::ostringstream encoded{};
                        encoded << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                << static_cast<int>(character);
                        escaped += encoded.str();
                    }
                    else
                    {
                        escaped.push_back(static_cast<char>(character));
                    }
                }
            }
            return escaped;
        }

        auto append_utf8(std::string& output, std::uint32_t codepoint) -> bool
        {
            if (codepoint <= 0x7F)
            {
                output.push_back(static_cast<char>(codepoint));
            }
            else if (codepoint <= 0x7FF)
            {
                output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
            {
                return false;
            }
            else if (codepoint <= 0xFFFF)
            {
                output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else
            {
                return false;
            }
            return true;
        }

        class FlatJsonParser
        {
          public:
            explicit FlatJsonParser(std::string_view input) : m_input(input) {}

            auto parse(std::string& error)
                -> std::optional<std::unordered_map<std::string, JsonValue>>
            {
                std::unordered_map<std::string, JsonValue> values{};
                skip_whitespace();
                if (!consume('{'))
                {
                    error = "checkpoint JSON must start with an object";
                    return std::nullopt;
                }

                skip_whitespace();
                if (consume('}'))
                {
                    error = "checkpoint JSON object is empty";
                    return std::nullopt;
                }

                while (m_position < m_input.size())
                {
                    auto key = parse_string(error);
                    if (!key)
                    {
                        return std::nullopt;
                    }
                    skip_whitespace();
                    if (!consume(':'))
                    {
                        error = "expected ':' after checkpoint field name";
                        return std::nullopt;
                    }
                    skip_whitespace();

                    JsonValue value{};
                    if (peek() == '"')
                    {
                        auto parsed = parse_string(error);
                        if (!parsed)
                        {
                            return std::nullopt;
                        }
                        value = std::move(*parsed);
                    }
                    else
                    {
                        auto parsed = parse_integer(error);
                        if (!parsed)
                        {
                            return std::nullopt;
                        }
                        value = *parsed;
                    }

                    if (!values.emplace(std::move(*key), std::move(value)).second)
                    {
                        error = "checkpoint JSON contains a duplicate field";
                        return std::nullopt;
                    }

                    skip_whitespace();
                    if (consume('}'))
                    {
                        skip_whitespace();
                        if (m_position != m_input.size())
                        {
                            error = "unexpected data after checkpoint JSON object";
                            return std::nullopt;
                        }
                        return values;
                    }
                    if (!consume(','))
                    {
                        error = "expected ',' between checkpoint fields";
                        return std::nullopt;
                    }
                    skip_whitespace();
                }

                error = "checkpoint JSON object was not closed";
                return std::nullopt;
            }

          private:
            auto peek() const -> char
            {
                return m_position < m_input.size() ? m_input[m_position] : '\0';
            }

            auto consume(char expected) -> bool
            {
                if (peek() != expected)
                {
                    return false;
                }
                ++m_position;
                return true;
            }

            auto skip_whitespace() -> void
            {
                while (m_position < m_input.size()
                       && std::isspace(static_cast<unsigned char>(m_input[m_position])))
                {
                    ++m_position;
                }
            }

            auto parse_string(std::string& error) -> std::optional<std::string>
            {
                if (!consume('"'))
                {
                    error = "expected a JSON string";
                    return std::nullopt;
                }

                std::string output{};
                while (m_position < m_input.size())
                {
                    const char character = m_input[m_position++];
                    if (character == '"')
                    {
                        return output;
                    }
                    if (static_cast<unsigned char>(character) < 0x20)
                    {
                        error = "unescaped control character in JSON string";
                        return std::nullopt;
                    }
                    if (character != '\\')
                    {
                        output.push_back(character);
                        continue;
                    }

                    if (m_position >= m_input.size())
                    {
                        error = "unterminated JSON escape";
                        return std::nullopt;
                    }
                    const char escaped = m_input[m_position++];
                    switch (escaped)
                    {
                    case '"': output.push_back('"'); break;
                    case '\\': output.push_back('\\'); break;
                    case '/': output.push_back('/'); break;
                    case 'b': output.push_back('\b'); break;
                    case 'f': output.push_back('\f'); break;
                    case 'n': output.push_back('\n'); break;
                    case 'r': output.push_back('\r'); break;
                    case 't': output.push_back('\t'); break;
                    case 'u':
                    {
                        if (m_position + 4 > m_input.size())
                        {
                            error = "short Unicode escape in JSON string";
                            return std::nullopt;
                        }
                        std::uint32_t codepoint{};
                        const auto begin = m_input.data() + m_position;
                        const auto result = std::from_chars(begin, begin + 4, codepoint, 16);
                        if (result.ec != std::errc{} || result.ptr != begin + 4
                            || !append_utf8(output, codepoint))
                        {
                            error = "invalid Unicode escape in JSON string";
                            return std::nullopt;
                        }
                        m_position += 4;
                        break;
                    }
                    default:
                        error = "invalid JSON escape";
                        return std::nullopt;
                    }
                }

                error = "unterminated JSON string";
                return std::nullopt;
            }

            auto parse_integer(std::string& error) -> std::optional<std::int64_t>
            {
                const auto start = m_position;
                if (peek() == '-')
                {
                    ++m_position;
                }
                const auto digits = m_position;
                while (m_position < m_input.size()
                       && std::isdigit(static_cast<unsigned char>(m_input[m_position])))
                {
                    ++m_position;
                }
                if (digits == m_position)
                {
                    error = "checkpoint fields must be strings or integers";
                    return std::nullopt;
                }

                std::int64_t value{};
                const auto begin = m_input.data() + start;
                const auto end = m_input.data() + m_position;
                const auto result = std::from_chars(begin, end, value);
                if (result.ec != std::errc{} || result.ptr != end)
                {
                    error = "checkpoint integer is out of range";
                    return std::nullopt;
                }
                return value;
            }

            std::string_view m_input{};
            std::size_t m_position{};
        };

        auto append_hash_bytes(std::uint64_t& hash, std::string_view value) -> void
        {
            constexpr std::uint64_t prime = 1099511628211ULL;
            const auto size = static_cast<std::uint64_t>(value.size());
            for (std::size_t index = 0; index < sizeof(size); ++index)
            {
                hash ^= static_cast<std::uint8_t>(size >> (index * 8));
                hash *= prime;
            }
            for (const unsigned char byte : value)
            {
                hash ^= byte;
                hash *= prime;
            }
        }

        template <typename Number>
        auto append_hash_number(std::uint64_t& hash, Number value) -> void
        {
            append_hash_bytes(hash, std::to_string(value));
        }

        auto is_hex_digest(std::string_view value, std::size_t expected_size) -> bool
        {
            if (value.size() != expected_size)
            {
                return false;
            }
            for (const unsigned char character : value)
            {
                if (!std::isxdigit(character))
                {
                    return false;
                }
            }
            return true;
        }

        auto is_sha256(std::string_view value) -> bool
        {
            return is_hex_digest(value, 64);
        }

        auto required_string(const std::unordered_map<std::string, JsonValue>& values,
                             std::string_view name, std::string& error)
            -> std::optional<std::string>
        {
            const auto found = values.find(std::string{name});
            if (found == values.end() || !std::holds_alternative<std::string>(found->second))
            {
                error = "missing or invalid string field: " + std::string{name};
                return std::nullopt;
            }
            return std::get<std::string>(found->second);
        }

        template <typename Number>
        auto required_integer(const std::unordered_map<std::string, JsonValue>& values,
                              std::string_view name, std::string& error)
            -> std::optional<Number>
        {
            const auto found = values.find(std::string{name});
            if (found == values.end() || !std::holds_alternative<std::int64_t>(found->second))
            {
                error = "missing or invalid integer field: " + std::string{name};
                return std::nullopt;
            }
            const auto value = std::get<std::int64_t>(found->second);
            if (value < static_cast<std::int64_t>(std::numeric_limits<Number>::min())
                || static_cast<std::uint64_t>(value)
                    > static_cast<std::uint64_t>(std::numeric_limits<Number>::max()))
            {
                error = "integer field is out of range: " + std::string{name};
                return std::nullopt;
            }
            return static_cast<Number>(value);
        }

        auto parenthesized_field_span(std::string_view text, std::string_view marker,
                                      std::string& error)
            -> std::optional<std::pair<std::size_t, std::size_t>>
        {
            const auto marker_position = text.find(marker);
            if (marker_position == std::string_view::npos)
            {
                error = "missing Unreal field: " + std::string{marker};
                return std::nullopt;
            }
            const auto value_start = marker_position + marker.size();
            if (value_start >= text.size() || text[value_start] != '(')
            {
                error = "Unreal field is not parenthesized: " + std::string{marker};
                return std::nullopt;
            }

            std::int32_t depth{};
            bool in_quotes{};
            bool escaped{};
            for (auto index = value_start; index < text.size(); ++index)
            {
                const auto character = text[index];
                if (in_quotes)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (character == '\\')
                    {
                        escaped = true;
                    }
                    else if (character == '"')
                    {
                        in_quotes = false;
                    }
                    continue;
                }
                if (character == '"')
                {
                    in_quotes = true;
                }
                else if (character == '(')
                {
                    ++depth;
                }
                else if (character == ')')
                {
                    if (--depth == 0)
                    {
                        return std::pair{value_start, index + 1};
                    }
                    if (depth < 0)
                    {
                        break;
                    }
                }
            }
            error = "Unreal field is unterminated: " + std::string{marker};
            return std::nullopt;
        }

        auto quoted_field(std::string_view text, std::string_view marker,
                          std::string& error) -> std::optional<std::string>
        {
            const auto position = text.find(marker);
            if (position == std::string_view::npos)
            {
                error = "card entry is missing field: " + std::string{marker};
                return std::nullopt;
            }
            const auto begin = position + marker.size();
            const auto end = text.find('"', begin);
            if (end == std::string_view::npos || end == begin)
            {
                error = "card entry has an invalid quoted field: " + std::string{marker};
                return std::nullopt;
            }
            const auto value = text.substr(begin, end - begin);
            for (const unsigned char character : value)
            {
                if (!(std::isalnum(character) || character == '_'))
                {
                    error = "card tag contains an unsupported character";
                    return std::nullopt;
                }
            }
            return std::string{value};
        }

        auto integer_field_or_default(std::string_view text, std::string_view marker,
                                      std::int32_t fallback, std::string& error)
            -> std::optional<std::int32_t>
        {
            const auto position = text.find(marker);
            if (position == std::string_view::npos)
            {
                return fallback;
            }
            const auto begin = position + marker.size();
            auto end = begin;
            while (end < text.size()
                   && (std::isdigit(static_cast<unsigned char>(text[end]))
                       || (end == begin && text[end] == '-')))
            {
                ++end;
            }
            std::int32_t value{};
            const auto parsed = std::from_chars(text.data() + begin, text.data() + end, value);
            if (begin == end || parsed.ec != std::errc{} || parsed.ptr != text.data() + end)
            {
                error = "card entry has an invalid integer field: " + std::string{marker};
                return std::nullopt;
            }
            return value;
        }

        struct OrderedCard
        {
            std::string tag{};
            std::int32_t upgrade_level{};
        };

        auto exact_card_from_instance(std::string_view text, std::string& error)
            -> std::optional<OrderedCard>
        {
            auto tag = quoted_field(text, "Tag=\"", error);
            auto upgrade = integer_field_or_default(text, "upgradeLevel=", 0, error);
            if (!tag || !upgrade || *upgrade < 0 || *upgrade > 1000)
            {
                if (error.empty())
                {
                    error = "card upgrade level is outside the supported range";
                }
                return std::nullopt;
            }
            return OrderedCard{.tag = std::move(*tag), .upgrade_level = *upgrade};
        }

        auto decklist_card_from_entry(std::string_view text, std::int32_t& count,
                                      std::string& error) -> std::optional<OrderedCard>
        {
            auto tag = quoted_field(text, "cardName=\"", error);
            auto parsed_count = integer_field_or_default(text, "count=", 1, error);
            auto upgrade = integer_field_or_default(text, "upgradeLevel=", 0, error);
            if (!tag || !parsed_count || !upgrade || *parsed_count <= 0
                || *parsed_count > 128 || *upgrade < 0 || *upgrade > 1000)
            {
                if (error.empty())
                {
                    error = "DecklistCard count or upgrade level is outside the supported range";
                }
                return std::nullopt;
            }
            count = *parsed_count;
            return OrderedCard{.tag = std::move(*tag), .upgrade_level = *upgrade};
        }

        auto ordered_card_key(const OrderedCard& card) -> std::string
        {
            return card.tag + "@" + std::to_string(card.upgrade_level);
        }

        auto decklist_card_text(const OrderedCard& card) -> std::string
        {
            std::string result{"(cardName=\""};
            result += card.tag;
            result += "\",count=1";
            if (card.upgrade_level != 0)
            {
                result += ",upgradeLevel=" + std::to_string(card.upgrade_level);
            }
            result += ')';
            return result;
        }
    } // namespace

    auto route_c_startup_decklist(std::string_view active_decklist) -> std::string
    {
        // During an active dungeon getActiveDecklist() contains the already-expanded
        // dungeon-tool cards in cardList while retaining the dungeonTools recipe. Passing that
        // value unchanged through setActiveDecklist() before a fresh battle makes native startup
        // append those tools a second time. Preserve cardList and temporarily hide the recipe.
        constexpr std::string_view marker{"dungeonTools="};
        const auto marker_position = active_decklist.find(marker);
        if (marker_position == std::string_view::npos)
        {
            return std::string{active_decklist};
        }
        const auto value_start = marker_position + marker.size();
        if (value_start >= active_decklist.size() || active_decklist[value_start] != '(')
        {
            throw std::runtime_error{
                "Route C active deck exposed an invalid dungeonTools field"};
        }

        std::size_t value_end = std::string_view::npos;
        std::int32_t depth{};
        bool in_quotes{};
        bool escaped{};
        for (auto index = value_start; index < active_decklist.size(); ++index)
        {
            const auto character = active_decklist[index];
            if (in_quotes)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (character == '\\')
                {
                    escaped = true;
                }
                else if (character == '"')
                {
                    in_quotes = false;
                }
                continue;
            }
            if (character == '"')
            {
                in_quotes = true;
            }
            else if (character == '(')
            {
                ++depth;
            }
            else if (character == ')')
            {
                --depth;
                if (depth == 0)
                {
                    value_end = index + 1;
                    break;
                }
                if (depth < 0)
                {
                    break;
                }
            }
        }
        if (value_end == std::string_view::npos || in_quotes || depth != 0)
        {
            throw std::runtime_error{
                "Route C active deck exposed an unterminated dungeonTools field"};
        }

        std::string startup_decklist{active_decklist};
        startup_decklist.replace(value_start, value_end - value_start, "()");
        return startup_decklist;
    }

    auto split_route_c_unreal_array(std::string_view value, std::string& error)
        -> std::optional<std::vector<std::string>>
    {
        const auto trim = [](std::string_view text) {
            while (!text.empty()
                   && std::isspace(static_cast<unsigned char>(text.front())))
            {
                text.remove_prefix(1);
            }
            while (!text.empty()
                   && std::isspace(static_cast<unsigned char>(text.back())))
            {
                text.remove_suffix(1);
            }
            return text;
        };

        value = trim(value);
        if (value.size() < 2 || value.front() != '(' || value.back() != ')')
        {
            error = "reflected lootDrops is not an Unreal array";
            return std::nullopt;
        }

        const auto contents = trim(value.substr(1, value.size() - 2));
        if (contents.empty())
        {
            return std::vector<std::string>{};
        }

        std::vector<std::string> elements{};
        std::size_t element_start{};
        std::int32_t depth{};
        bool in_quotes{};
        bool escaped{};
        for (std::size_t index{}; index <= contents.size(); ++index)
        {
            const bool at_end = index == contents.size();
            const auto character = at_end ? '\0' : contents[index];
            if (!at_end && in_quotes)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (character == '\\')
                {
                    escaped = true;
                }
                else if (character == '"')
                {
                    in_quotes = false;
                }
                continue;
            }
            if (!at_end && character == '"')
            {
                in_quotes = true;
                continue;
            }
            if (!at_end && character == '(')
            {
                ++depth;
                continue;
            }
            if (!at_end && character == ')')
            {
                if (--depth < 0)
                {
                    error = "reflected lootDrops has unbalanced parentheses";
                    return std::nullopt;
                }
                continue;
            }
            if (!at_end && (character != ',' || depth != 0))
            {
                continue;
            }

            auto element = trim(contents.substr(element_start, index - element_start));
            if (element.empty())
            {
                error = "reflected lootDrops contains an empty element";
                return std::nullopt;
            }
            if (element.front() != '(' || element.back() != ')')
            {
                error = "reflected lootDrops contains a non-struct element";
                return std::nullopt;
            }
            elements.emplace_back(element);
            if (elements.size() > 1024)
            {
                error = "reflected lootDrops contains too many elements";
                return std::nullopt;
            }
            element_start = index + 1;
        }

        if (in_quotes || escaped || depth != 0)
        {
            error = "reflected lootDrops is unterminated or unbalanced";
            return std::nullopt;
        }
        return elements;
    }

    auto exact_player_zones_startup_decklist(std::string_view active_decklist,
                                             std::string_view player_deck,
                                             std::string_view player_hand,
                                             std::string& error)
        -> std::optional<std::string>
    {
        error.clear();
        auto startup = route_c_startup_decklist(active_decklist);
        auto card_list_span = parenthesized_field_span(startup, "cardList=", error);
        if (!card_list_span)
        {
            return std::nullopt;
        }

        auto deck_elements = split_route_c_unreal_array(player_deck, error);
        if (!deck_elements)
        {
            error = "exact player deck is invalid: " + error;
            return std::nullopt;
        }
        auto hand_elements = split_route_c_unreal_array(player_hand, error);
        if (!hand_elements)
        {
            error = "exact player hand is invalid: " + error;
            return std::nullopt;
        }
        if (deck_elements->empty() || hand_elements->empty()
            || deck_elements->size() + hand_elements->size() > 128)
        {
            error = "exact player zones require a non-empty deck and hand with at most 128 cards";
            return std::nullopt;
        }

        std::vector<OrderedCard> deck_cards{};
        std::vector<OrderedCard> hand_cards{};
        std::unordered_map<std::string, std::int32_t> exact_counts{};
        const auto append_exact = [&](const std::vector<std::string>& source,
                                      std::vector<OrderedCard>& destination) -> bool {
            destination.reserve(source.size());
            for (const auto& element : source)
            {
                auto card = exact_card_from_instance(element, error);
                if (!card)
                {
                    return false;
                }
                ++exact_counts[ordered_card_key(*card)];
                destination.push_back(std::move(*card));
            }
            return true;
        };
        if (!append_exact(*deck_elements, deck_cards)
            || !append_exact(*hand_elements, hand_cards))
        {
            return std::nullopt;
        }

        const auto active_card_list = std::string_view{startup}.substr(
            card_list_span->first,
            card_list_span->second - card_list_span->first);
        auto active_elements = split_route_c_unreal_array(active_card_list, error);
        if (!active_elements)
        {
            error = "active Decklist.cardList is invalid: " + error;
            return std::nullopt;
        }
        std::unordered_map<std::string, std::int32_t> active_counts{};
        std::size_t active_total{};
        for (const auto& element : *active_elements)
        {
            std::int32_t count{};
            auto card = decklist_card_from_entry(element, count, error);
            if (!card)
            {
                return std::nullopt;
            }
            active_counts[ordered_card_key(*card)] += count;
            active_total += static_cast<std::size_t>(count);
        }
        if (active_total != deck_cards.size() + hand_cards.size()
            || active_counts != exact_counts)
        {
            error = "exact player zones do not match the active deck card multiset";
            return std::nullopt;
        }

        // The guarded fixed-order experiment models native draws as TArray::Pop().
        // Its post-startup zone comparison decides whether this orientation is valid.
        std::vector<OrderedCard> fixed_order{};
        fixed_order.reserve(deck_cards.size() + hand_cards.size());
        fixed_order.insert(fixed_order.end(), deck_cards.begin(), deck_cards.end());
        fixed_order.insert(fixed_order.end(), hand_cards.rbegin(), hand_cards.rend());
        std::string replacement{"("};
        for (std::size_t index{}; index < fixed_order.size(); ++index)
        {
            if (index != 0)
            {
                replacement += ',';
            }
            replacement += decklist_card_text(fixed_order[index]);
        }
        replacement += ')';
        startup.replace(
            card_list_span->first,
            card_list_span->second - card_list_span->first,
            replacement);

        constexpr std::string_view fixed_marker{"fixedOrder="};
        const auto fixed_position = startup.find(fixed_marker);
        if (fixed_position == std::string::npos)
        {
            startup.insert(card_list_span->first + replacement.size(), ",fixedOrder=True");
        }
        else
        {
            const auto value_start = fixed_position + fixed_marker.size();
            const auto value_end = startup.find_first_of(",)", value_start);
            if (value_end == std::string::npos || value_end == value_start)
            {
                error = "active Decklist.fixedOrder field is invalid";
                return std::nullopt;
            }
            startup.replace(value_start, value_end - value_start, "True");
        }
        return startup;
    }

    auto route_c_payload_checksum(const RouteCCheckpoint& checkpoint) -> std::string
    {
        std::uint64_t hash = 14695981039346656037ULL;
        append_hash_number(hash, checkpoint.schema_version);
        append_hash_bytes(hash, checkpoint.kind);
        append_hash_bytes(hash, checkpoint.captured_at_utc);
        append_hash_bytes(hash, checkpoint.game_executable_sha256);
        append_hash_number(hash, checkpoint.game_executable_size);
        append_hash_bytes(hash, checkpoint.mode);
        append_hash_bytes(hash, checkpoint.source_level_name);
        append_hash_bytes(hash, checkpoint.active_character_info);
        append_hash_bytes(hash, checkpoint.active_stage_info);
        append_hash_bytes(hash, checkpoint.active_decklist);
        append_hash_bytes(hash, checkpoint.active_storage);
        append_hash_bytes(hash, checkpoint.loot_drops);
        append_hash_bytes(hash, checkpoint.deck_run);
        append_hash_number(hash, checkpoint.player_health);
        append_hash_number(hash, checkpoint.player_max_health);
        append_hash_number(hash, checkpoint.wave_index);
        append_hash_bytes(hash, checkpoint.spawner_class);
        append_hash_number(hash, checkpoint.spawner_class_size);

        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_route_c_checkpoint(RouteCCheckpoint checkpoint) -> std::string
    {
        checkpoint.payload_checksum = route_c_payload_checksum(checkpoint);
        std::ostringstream output{};
        output << "{\n"
               << "  \"schemaVersion\": " << checkpoint.schema_version << ",\n"
               << "  \"kind\": \"" << json_escape(checkpoint.kind) << "\",\n"
               << "  \"capturedAtUtc\": \"" << json_escape(checkpoint.captured_at_utc) << "\",\n"
               << "  \"gameExecutableSha256\": \""
               << json_escape(checkpoint.game_executable_sha256) << "\",\n"
               << "  \"gameExecutableSize\": " << checkpoint.game_executable_size << ",\n"
               << "  \"mode\": \"" << json_escape(checkpoint.mode) << "\",\n"
               << "  \"sourceLevelName\": \"" << json_escape(checkpoint.source_level_name) << "\",\n"
               << "  \"activeCharacterInfo\": \""
               << json_escape(checkpoint.active_character_info) << "\",\n"
               << "  \"activeStageInfo\": \"" << json_escape(checkpoint.active_stage_info) << "\",\n"
               << "  \"activeDecklist\": \"" << json_escape(checkpoint.active_decklist) << "\",\n"
               << "  \"activeStorage\": \"" << json_escape(checkpoint.active_storage) << "\",\n"
               << "  \"lootDrops\": \"" << json_escape(checkpoint.loot_drops) << "\",\n"
               << "  \"deckRun\": \"" << json_escape(checkpoint.deck_run) << "\",\n"
               << "  \"playerHealth\": " << checkpoint.player_health << ",\n"
               << "  \"playerMaxHealth\": " << checkpoint.player_max_health << ",\n"
               << "  \"waveIndex\": " << checkpoint.wave_index << ",\n"
               << "  \"spawnerClass\": \"" << json_escape(checkpoint.spawner_class) << "\",\n"
               << "  \"spawnerClassSize\": " << checkpoint.spawner_class_size << ",\n"
               << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_route_c_checkpoint(const RouteCCheckpoint& checkpoint, std::string& error)
        -> bool
    {
        if (checkpoint.schema_version != RouteCSchemaVersion)
        {
            error = "unsupported Route C checkpoint schema version";
            return false;
        }
        if (checkpoint.kind != RouteCCheckpointKind)
        {
            error = "checkpoint kind is not Route C";
            return false;
        }
        if (!is_sha256(checkpoint.game_executable_sha256)
            || checkpoint.game_executable_size == 0)
        {
            error = "checkpoint game executable fingerprint is invalid";
            return false;
        }
        if (checkpoint.mode != RouteCSupportedMode)
        {
            error = "Route C currently supports DUNGEON mode only";
            return false;
        }
        if (checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256)
        {
            error = "checkpoint source level name is invalid";
            return false;
        }
        if (checkpoint.active_character_info.empty() || checkpoint.active_stage_info.empty()
            || checkpoint.active_decklist.empty() || checkpoint.active_storage.empty()
            || checkpoint.loot_drops.empty() || checkpoint.deck_run.empty())
        {
            error = "checkpoint is missing required reflected run data";
            return false;
        }
        std::string loot_error{};
        if (!split_route_c_unreal_array(checkpoint.loot_drops, loot_error))
        {
            error = "checkpoint lootDrops is invalid: " + loot_error;
            return false;
        }
        if (checkpoint.player_health <= 0 || checkpoint.player_max_health <= 0
            || checkpoint.player_health > checkpoint.player_max_health
            || checkpoint.player_max_health > 10000)
        {
            error = "checkpoint player health is outside the supported range";
            return false;
        }
        if (checkpoint.wave_index < 0 || checkpoint.wave_index > 1000)
        {
            error = "checkpoint wave index is outside the supported range";
            return false;
        }
        if (checkpoint.spawner_class.empty() || checkpoint.spawner_class.size() > 256
            || checkpoint.spawner_class_size < 0x270 || checkpoint.spawner_class_size > 0x280)
        {
            error = "checkpoint spawner is not a stateless ordinary spawner";
            return false;
        }
        if (checkpoint.payload_checksum != route_c_payload_checksum(checkpoint))
        {
            error = "checkpoint payload checksum does not match";
            return false;
        }
        return true;
    }

    auto parse_route_c_checkpoint(std::string_view json, std::string& error)
        -> std::optional<RouteCCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "checkpoint file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }

        auto values = FlatJsonParser{json}.parse(error);
        if (!values)
        {
            return std::nullopt;
        }

        RouteCCheckpoint checkpoint{};
#define READ_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version != RouteCSchemaVersion)
        {
            error = "unsupported Route C checkpoint schema version";
            return std::nullopt;
        }
        READ_STRING(kind, "kind");
        READ_STRING(captured_at_utc, "capturedAtUtc");
        READ_STRING(game_executable_sha256, "gameExecutableSha256");
        READ_INTEGER(game_executable_size, "gameExecutableSize", std::uint64_t);
        READ_STRING(mode, "mode");
        READ_STRING(source_level_name, "sourceLevelName");
        READ_STRING(active_character_info, "activeCharacterInfo");
        READ_STRING(active_stage_info, "activeStageInfo");
        READ_STRING(active_decklist, "activeDecklist");
        READ_STRING(active_storage, "activeStorage");
        READ_STRING(loot_drops, "lootDrops");
        READ_STRING(deck_run, "deckRun");
        READ_INTEGER(player_health, "playerHealth", std::int32_t);
        READ_INTEGER(player_max_health, "playerMaxHealth", std::int32_t);
        READ_INTEGER(wave_index, "waveIndex", std::int32_t);
        READ_STRING(spawner_class, "spawnerClass");
        READ_INTEGER(spawner_class_size, "spawnerClassSize", std::uint32_t);
        READ_STRING(payload_checksum, "payloadChecksum");

#undef READ_INTEGER
#undef READ_STRING

        if (!validate_route_c_checkpoint(checkpoint, error))
        {
            return std::nullopt;
        }
        return checkpoint;
    }

    auto exact_spawn_plan_payload_checksum(const ExactSpawnPlanCheckpoint& checkpoint)
        -> std::string
    {
        std::uint64_t hash = 14695981039346656037ULL;
        append_hash_number(hash, checkpoint.schema_version);
        append_hash_bytes(hash, checkpoint.kind);
        append_hash_bytes(hash, checkpoint.captured_at_utc);
        append_hash_bytes(hash, checkpoint.route_c_payload_checksum);
        append_hash_bytes(hash, checkpoint.game_executable_sha256);
        append_hash_number(hash, checkpoint.game_executable_size);
        append_hash_bytes(hash, checkpoint.source_level_name);
        append_hash_number(hash, checkpoint.wave_index);
        append_hash_bytes(hash, checkpoint.spawner_class);
        append_hash_number(hash, checkpoint.spawner_class_size);
        append_hash_bytes(hash, checkpoint.spawn_list);

        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_exact_spawn_plan_checkpoint(ExactSpawnPlanCheckpoint checkpoint)
        -> std::string
    {
        checkpoint.payload_checksum = exact_spawn_plan_payload_checksum(checkpoint);
        std::ostringstream output{};
        output << "{\n"
               << "  \"schemaVersion\": " << checkpoint.schema_version << ",\n"
               << "  \"kind\": \"" << json_escape(checkpoint.kind) << "\",\n"
               << "  \"capturedAtUtc\": \"" << json_escape(checkpoint.captured_at_utc)
               << "\",\n"
               << "  \"routeCPayloadChecksum\": \""
               << json_escape(checkpoint.route_c_payload_checksum) << "\",\n"
               << "  \"gameExecutableSha256\": \""
               << json_escape(checkpoint.game_executable_sha256) << "\",\n"
               << "  \"gameExecutableSize\": " << checkpoint.game_executable_size << ",\n"
               << "  \"sourceLevelName\": \"" << json_escape(checkpoint.source_level_name)
               << "\",\n"
               << "  \"waveIndex\": " << checkpoint.wave_index << ",\n"
               << "  \"spawnerClass\": \"" << json_escape(checkpoint.spawner_class)
               << "\",\n"
               << "  \"spawnerClassSize\": " << checkpoint.spawner_class_size << ",\n"
               << "  \"spawnList\": \"" << json_escape(checkpoint.spawn_list) << "\",\n"
               << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_exact_spawn_plan_checkpoint(const ExactSpawnPlanCheckpoint& checkpoint,
                                              std::string& error) -> bool
    {
        if (checkpoint.schema_version != ExactSpawnPlanSchemaVersion)
        {
            error = "unsupported exact spawn-plan schema version";
            return false;
        }
        if (checkpoint.kind != ExactSpawnPlanCheckpointKind)
        {
            error = "checkpoint kind is not an exact spawn plan";
            return false;
        }
        if (checkpoint.captured_at_utc.empty()
            || !is_hex_digest(checkpoint.route_c_payload_checksum, 16))
        {
            error = "exact spawn plan has invalid Route C linkage";
            return false;
        }
        if (!is_sha256(checkpoint.game_executable_sha256)
            || checkpoint.game_executable_size == 0)
        {
            error = "exact spawn plan game executable fingerprint is invalid";
            return false;
        }
        if (checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256)
        {
            error = "exact spawn plan source level name is invalid";
            return false;
        }
        if (checkpoint.wave_index < 0 || checkpoint.wave_index > 1000)
        {
            error = "exact spawn plan wave index is outside the supported range";
            return false;
        }
        if (checkpoint.spawner_class.empty() || checkpoint.spawner_class.size() > 256
            || checkpoint.spawner_class_size < 0x270
            || checkpoint.spawner_class_size > 0x280)
        {
            error = "exact spawn plan does not describe an ordinary spawner";
            return false;
        }
        std::string array_error{};
        const auto waves = split_route_c_unreal_array(checkpoint.spawn_list, array_error);
        if (!waves || waves->empty()
            || static_cast<std::size_t>(checkpoint.wave_index) >= waves->size())
        {
            error = "exact spawnList is invalid or does not contain the saved wave: "
                + array_error;
            return false;
        }
        if (checkpoint.payload_checksum != exact_spawn_plan_payload_checksum(checkpoint))
        {
            error = "exact spawn-plan payload checksum does not match";
            return false;
        }
        return true;
    }

    auto parse_exact_spawn_plan_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactSpawnPlanCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "exact spawn-plan file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }

        auto values = FlatJsonParser{json}.parse(error);
        if (!values)
        {
            return std::nullopt;
        }

        ExactSpawnPlanCheckpoint checkpoint{};
#define READ_EXACT_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_EXACT_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_EXACT_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version != ExactSpawnPlanSchemaVersion)
        {
            error = "unsupported exact spawn-plan schema version";
            return std::nullopt;
        }
        READ_EXACT_STRING(kind, "kind");
        READ_EXACT_STRING(captured_at_utc, "capturedAtUtc");
        READ_EXACT_STRING(route_c_payload_checksum, "routeCPayloadChecksum");
        READ_EXACT_STRING(game_executable_sha256, "gameExecutableSha256");
        READ_EXACT_INTEGER(game_executable_size, "gameExecutableSize", std::uint64_t);
        READ_EXACT_STRING(source_level_name, "sourceLevelName");
        READ_EXACT_INTEGER(wave_index, "waveIndex", std::int32_t);
        READ_EXACT_STRING(spawner_class, "spawnerClass");
        READ_EXACT_INTEGER(spawner_class_size, "spawnerClassSize", std::uint32_t);
        READ_EXACT_STRING(spawn_list, "spawnList");
        READ_EXACT_STRING(payload_checksum, "payloadChecksum");

#undef READ_EXACT_INTEGER
#undef READ_EXACT_STRING

        if (!validate_exact_spawn_plan_checkpoint(checkpoint, error))
        {
            return std::nullopt;
        }
        return checkpoint;
    }

    auto exact_player_zones_payload_checksum(const ExactPlayerZonesCheckpoint& checkpoint)
        -> std::string
    {
        std::uint64_t hash = 14695981039346656037ULL;
        append_hash_number(hash, checkpoint.schema_version);
        append_hash_bytes(hash, checkpoint.kind);
        append_hash_bytes(hash, checkpoint.captured_at_utc);
        append_hash_bytes(hash, checkpoint.route_c_payload_checksum);
        append_hash_bytes(hash, checkpoint.game_executable_sha256);
        append_hash_number(hash, checkpoint.game_executable_size);
        append_hash_bytes(hash, checkpoint.source_level_name);
        append_hash_number(hash, checkpoint.wave_index);
        append_hash_bytes(hash, checkpoint.player_deck);
        append_hash_bytes(hash, checkpoint.player_hand);

        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_exact_player_zones_checkpoint(ExactPlayerZonesCheckpoint checkpoint)
        -> std::string
    {
        checkpoint.payload_checksum = exact_player_zones_payload_checksum(checkpoint);
        std::ostringstream output{};
        output << "{\n"
               << "  \"schemaVersion\": " << checkpoint.schema_version << ",\n"
               << "  \"kind\": \"" << json_escape(checkpoint.kind) << "\",\n"
               << "  \"capturedAtUtc\": \"" << json_escape(checkpoint.captured_at_utc)
               << "\",\n"
               << "  \"routeCPayloadChecksum\": \""
               << json_escape(checkpoint.route_c_payload_checksum) << "\",\n"
               << "  \"gameExecutableSha256\": \""
               << json_escape(checkpoint.game_executable_sha256) << "\",\n"
               << "  \"gameExecutableSize\": " << checkpoint.game_executable_size << ",\n"
               << "  \"sourceLevelName\": \"" << json_escape(checkpoint.source_level_name)
               << "\",\n"
               << "  \"waveIndex\": " << checkpoint.wave_index << ",\n"
               << "  \"playerDeck\": \"" << json_escape(checkpoint.player_deck) << "\",\n"
               << "  \"playerHand\": \"" << json_escape(checkpoint.player_hand) << "\",\n"
               << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_exact_player_zones_checkpoint(const ExactPlayerZonesCheckpoint& checkpoint,
                                                std::string& error) -> bool
    {
        if (checkpoint.schema_version != ExactPlayerZonesSchemaVersion)
        {
            error = "unsupported exact player-zones schema version";
            return false;
        }
        if (checkpoint.kind != ExactPlayerZonesCheckpointKind)
        {
            error = "checkpoint kind is not exact player zones";
            return false;
        }
        if (checkpoint.captured_at_utc.empty()
            || !is_hex_digest(checkpoint.route_c_payload_checksum, 16))
        {
            error = "exact player zones have invalid Route C linkage";
            return false;
        }
        if (!is_sha256(checkpoint.game_executable_sha256)
            || checkpoint.game_executable_size == 0)
        {
            error = "exact player zones have an invalid game executable fingerprint";
            return false;
        }
        if (checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256 || checkpoint.wave_index < 0
            || checkpoint.wave_index > 1000)
        {
            error = "exact player zones have invalid level or wave data";
            return false;
        }

        std::string array_error{};
        const auto deck = split_route_c_unreal_array(checkpoint.player_deck, array_error);
        if (!deck || deck->empty())
        {
            error = "exact player deck is invalid: " + array_error;
            return false;
        }
        array_error.clear();
        const auto hand = split_route_c_unreal_array(checkpoint.player_hand, array_error);
        if (!hand || hand->empty() || deck->size() + hand->size() > 128)
        {
            error = "exact player hand is invalid: " + array_error;
            return false;
        }
        for (const auto* zone : {&*deck, &*hand})
        {
            for (const auto& element : *zone)
            {
                array_error.clear();
                if (!exact_card_from_instance(element, array_error))
                {
                    error = "exact player zone contains an invalid card: " + array_error;
                    return false;
                }
            }
        }
        if (checkpoint.payload_checksum != exact_player_zones_payload_checksum(checkpoint))
        {
            error = "exact player-zones payload checksum does not match";
            return false;
        }
        return true;
    }

    auto parse_exact_player_zones_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactPlayerZonesCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "exact player-zones file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }
        auto values = FlatJsonParser{json}.parse(error);
        if (!values)
        {
            return std::nullopt;
        }

        ExactPlayerZonesCheckpoint checkpoint{};
#define READ_ZONES_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_ZONES_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_ZONES_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version != ExactPlayerZonesSchemaVersion)
        {
            error = "unsupported exact player-zones schema version";
            return std::nullopt;
        }
        READ_ZONES_STRING(kind, "kind");
        READ_ZONES_STRING(captured_at_utc, "capturedAtUtc");
        READ_ZONES_STRING(route_c_payload_checksum, "routeCPayloadChecksum");
        READ_ZONES_STRING(game_executable_sha256, "gameExecutableSha256");
        READ_ZONES_INTEGER(game_executable_size, "gameExecutableSize", std::uint64_t);
        READ_ZONES_STRING(source_level_name, "sourceLevelName");
        READ_ZONES_INTEGER(wave_index, "waveIndex", std::int32_t);
        READ_ZONES_STRING(player_deck, "playerDeck");
        READ_ZONES_STRING(player_hand, "playerHand");
        READ_ZONES_STRING(payload_checksum, "payloadChecksum");

#undef READ_ZONES_INTEGER
#undef READ_ZONES_STRING

        if (!validate_exact_player_zones_checkpoint(checkpoint, error))
        {
            return std::nullopt;
        }
        return checkpoint;
    }
} // namespace QuantumCheckpoint
