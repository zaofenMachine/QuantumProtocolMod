#include "CheckpointPersistence.hpp"
#include "PlayerAttackState.hpp"
#include "PlayerHealthState.hpp"
#include "PlayerCounterState.hpp"
#include "EffectQualificationPolicy.hpp"

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
            explicit FlatJsonParser(std::string_view input,
                                    std::size_t maximum_fields = RouteCMaximumFileBytes,
                                    bool strict_json = false)
                : m_input(input), m_maximum_fields(maximum_fields), m_strict_json(strict_json) {}

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
                    if (values.size() > m_maximum_fields)
                    {
                        error = "checkpoint JSON exceeds its field limit";
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
                       && (m_strict_json
                           ? m_input[m_position] == ' ' || m_input[m_position] == '\t'
                             || m_input[m_position] == '\r' || m_input[m_position] == '\n'
                           : std::isspace(static_cast<unsigned char>(m_input[m_position]))))
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
                if (m_strict_json && std::to_string(value) != m_input.substr(start, m_position - start))
                {
                    error = "checkpoint integer is not canonical JSON";
                    return std::nullopt;
                }
                return value;
            }

            std::string_view m_input{};
            std::size_t m_position{};
            std::size_t m_maximum_fields{};
            bool m_strict_json{};
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

        auto validate_hand_health_checksum_link(std::size_t hand_size,
                                                std::string_view checksum,
                                                bool required,
                                                std::string& error) -> bool
        {
            if (!required)
            {
                if (checksum.empty()) return true;
                error = "legacy player layout cannot claim hand-health dependency coverage";
                return false;
            }
            if (hand_size > 7 || (hand_size == 0 ? !checksum.empty() : !is_hex_digest(checksum, 16)))
            {
                error = "player layout requires a hand-health checksum for one to seven hand cards and an empty checksum for an empty hand";
                return false;
            }
            return true;
        }

        auto validate_qualification_policy(std::string_view policy, int schema_version,
                                           std::string_view layout_kind,
                                           std::string& error) -> bool
        {
            const auto expected = qualification::qualification_policy_for_layout(layout_kind, schema_version);
            if (expected ? policy == *expected : policy.empty()) return true;
            error = expected ? "player qualification layout requires exact policy: " + std::string{*expected}
                             : "legacy player layout cannot claim qualification coverage";
            return false;
        }

        auto validate_off_field_base_health_records(std::size_t zone_size,
                                                    std::string_view records,
                                                    bool covered,
                                                    std::string& error) -> bool
        {
            if (!covered)
            {
                if (records.empty()) return true;
                error = "legacy player layout cannot claim DECK/TRASH base-health coverage";
                return false;
            }
            const auto states = parse_player_off_field_base_health_states(records, error);
            if (!states || states->size() != zone_size)
            {
                error = "player layout DECK/TRASH base-health records are invalid or misaligned: " + error;
                return false;
            }
            return true;
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

        auto reject_inline_qualification_fields(
            const std::unordered_map<std::string, JsonValue>& values, std::string& error) -> bool
        {
            for (const auto& [name, value] : values)
            {
                const bool dynamic_card = name.size() > 4 && name.starts_with("card")
                    && std::isdigit(static_cast<unsigned char>(name[4]));
                if (dynamic_card || name == "cardCount" || name == "handCardCount"
                    || name == "deckCardCount" || name == "trashCardCount" || name == "fieldCardCount"
                    || name == "layoutKind" || name == "layoutPayloadChecksum" || name == "cards"
                    || name.starts_with("playerQualification")
                    || (name.starts_with("qualification") && name != "qualificationPolicy"))
                {
                    error = "player layout JSON contains reserved Q1 qualification fields: " + name;
                    return false;
                }
            }
            return true;
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
            if (!std::in_range<Number>(value))
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

    auto parse_exact_player_field_states(std::string_view value, std::string& error)
        -> std::optional<std::vector<ExactPlayerFieldCardState>>
    {
        error.clear();
        if (value.empty() || value.size() > 4096)
        {
            error = "exact player-field state list is empty or too large";
            return std::nullopt;
        }
        const auto parse_int = [&](std::string_view token)
            -> std::optional<std::int32_t> {
            std::int32_t parsed{};
            const auto result = std::from_chars(
                token.data(), token.data() + token.size(), parsed);
            if (token.empty() || result.ec != std::errc{}
                || result.ptr != token.data() + token.size())
            {
                return std::nullopt;
            }
            return parsed;
        };

        std::vector<ExactPlayerFieldCardState> states{};
        std::size_t record_start{};
        while (record_start < value.size())
        {
            const auto record_end = value.find(';', record_start);
            const auto record = value.substr(
                record_start,
                (record_end == std::string_view::npos ? value.size() : record_end)
                    - record_start);
            std::array<std::string_view, 4> fields{};
            std::size_t field_start{};
            bool malformed{};
            for (std::size_t index{}; index < fields.size(); ++index)
            {
                const auto field_end = record.find(',', field_start);
                if ((index + 1 < fields.size()
                     && field_end == std::string_view::npos)
                    || (index + 1 == fields.size()
                        && field_end != std::string_view::npos))
                {
                    malformed = true;
                    break;
                }
                fields[index] = record.substr(
                    field_start,
                    (field_end == std::string_view::npos ? record.size() : field_end)
                        - field_start);
                field_start = field_end == std::string_view::npos
                    ? record.size() : field_end + 1;
            }
            const auto row = malformed ? std::nullopt : parse_int(fields[0]);
            const auto index = malformed ? std::nullopt : parse_int(fields[1]);
            const auto health = malformed ? std::nullopt : parse_int(fields[2]);
            const auto turn_active = malformed ? std::nullopt : parse_int(fields[3]);
            if (!row || !index || !health || !turn_active || *row < 0 || *row > 1
                || *index < 0 || *index > 4 || *health <= 0 || *health > 10000
                || (*turn_active != 0 && *turn_active != 1))
            {
                error = "exact player-field state record is invalid";
                return std::nullopt;
            }
            const ExactPlayerFieldCardState state{
                .row = *row,
                .index = *index,
                .current_health = *health,
                .turn_active = *turn_active != 0,
            };
            if (!states.empty())
            {
                const auto& previous = states.back();
                if (state.row < previous.row
                    || (state.row == previous.row && state.index <= previous.index))
                {
                    error = "exact player-field states are not in unique slot order";
                    return std::nullopt;
                }
            }
            states.push_back(state);
            if (states.size() > 10)
            {
                error = "exact player-field state list contains too many cards";
                return std::nullopt;
            }
            if (record_end == std::string_view::npos)
            {
                break;
            }
            record_start = record_end + 1;
            if (record_start == value.size())
            {
                error = "exact player-field state list has a trailing separator";
                return std::nullopt;
            }
        }
        return states;
    }

    auto exact_player_zones_startup_decklist(std::string_view active_decklist,
                                             std::string_view player_deck,
                                             std::string_view player_hand,
                                             std::string& error,
                                             bool allow_single_generated_apple,
                                             std::size_t* additional_card_count)
        -> std::optional<std::string>
    {
        error.clear();
        if (additional_card_count) *additional_card_count = 0;
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
        if ((deck_elements->empty() && hand_elements->empty())
            || deck_elements->size() + hand_elements->size() > 128)
        {
            error = "exact player startup requires between 1 and 128 cards";
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
            auto with_copy = active_counts;
            ++with_copy["naturalApple@0"];
            if (!allow_single_generated_apple
                || !active_counts.contains("naturalApple@0")
                || !active_counts.contains("naturalSpring@0")
                || deck_cards.size() + hand_cards.size() != active_total + 1
                || with_copy != exact_counts)
            {
                error = "exact player zones do not match the active deck card multiset"
                    + std::string{allow_single_generated_apple ? " or its single generated apple extension" : ""};
                return std::nullopt;
            }
            // Both ordinary startup and Spring construct a new native card via
            // DF55F0. The caller restores the original active Decklist after this
            // temporary load; the additional Apple remains a battle card only.
            if (additional_card_count) *additional_card_count = 1;
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

    auto exact_player_trash_startup_decklist(std::string_view active_decklist,
                                             std::string_view player_deck,
                                             std::string_view player_hand,
                                             std::string_view player_trash,
                                             std::string& error,
                                             bool allow_single_generated_apple,
                                             std::size_t* additional_card_count)
        -> std::optional<std::string>
    {
        error.clear();
        if (additional_card_count) *additional_card_count = 0;
        auto deck = split_route_c_unreal_array(player_deck, error);
        if (!deck)
        {
            error = "exact player-trash deck is invalid: " + error;
            return std::nullopt;
        }
        auto trash = split_route_c_unreal_array(player_trash, error);
        if (!trash || trash->empty())
        {
            error = "exact player trash is empty or invalid: " + error;
            return std::nullopt;
        }
        auto hand = split_route_c_unreal_array(player_hand, error);
        if (!hand || deck->size() + hand->size() + trash->size() > 128)
        {
            error = "exact player-trash hand is invalid or total cards exceed 128: " + error;
            return std::nullopt;
        }

        // Native startup draws from the end of this fixed array. Leave the captured
        // trash immediately above the captured deck, draw the reversed hand, and move
        // only those staged trash identities after the battle becomes stable.
        std::string staged_deck{"("};
        bool first = true;
        for (const auto* zone : {&*deck, &*trash})
        {
            for (const auto& card : *zone)
            {
                if (!first)
                {
                    staged_deck += ',';
                }
                first = false;
                staged_deck += card;
            }
        }
        staged_deck += ')';
        return exact_player_zones_startup_decklist(
            active_decklist, staged_deck, player_hand, error, allow_single_generated_apple, additional_card_count);
    }

    auto exact_player_trash_staging_matches(std::string_view expected_deck,
                                            std::string_view expected_hand,
                                            std::string_view expected_trash,
                                            std::string_view live_deck,
                                            std::string_view live_hand,
                                            std::string_view live_trash,
                                            std::string& error,
                                            bool native_order) -> bool
    {
        error.clear();
        auto parse = [&](std::string_view value, std::string_view label)
            -> std::optional<std::vector<std::string>> {
            std::string array_error{};
            auto elements = split_route_c_unreal_array(value, array_error);
            if (!elements)
            {
                error = std::string{label} + " is invalid: " + array_error;
            }
            return elements;
        };

        const auto saved_deck = parse(expected_deck, "saved deck");
        const auto saved_hand = parse(expected_hand, "saved hand");
        const auto saved_trash = parse(expected_trash, "saved trash");
        const auto staged_deck = parse(live_deck, "live deck");
        const auto staged_hand = parse(live_hand, "live hand");
        const auto staged_trash = parse(live_trash, "live trash");
        if (!saved_deck || !saved_hand || !saved_trash
            || !staged_deck || !staged_hand || !staged_trash)
        {
            return false;
        }

        // A card that has been played can contain runtime-only/default-expanded
        // CardInfoInstance fields that native startup does not reproduce verbatim.
        // Staging only needs to prove that the same ordered card identities are
        // present; exact instance text is verified again for the final non-field
        // zones after the native moves have completed.
        const auto identity_keys = [&](const std::vector<std::string>& cards,
                                       std::string_view label)
            -> std::optional<std::vector<std::string>> {
            std::vector<std::string> keys{};
            keys.reserve(cards.size());
            for (const auto& card : cards)
            {
                std::string card_error{};
                auto parsed = exact_card_from_instance(card, card_error);
                if (!parsed)
                {
                    error = std::string{label} + " contains an invalid card identity: "
                        + card_error;
                    return std::nullopt;
                }
                keys.push_back(ordered_card_key(*parsed));
            }
            return keys;
        };

        const auto saved_deck_keys = identity_keys(*saved_deck, "saved deck");
        const auto saved_hand_keys = identity_keys(*saved_hand, "saved hand");
        const auto saved_trash_keys = identity_keys(*saved_trash, "saved extras");
        const auto staged_deck_keys = identity_keys(*staged_deck, "live deck");
        const auto staged_hand_keys = identity_keys(*staged_hand, "live hand");
        const auto staged_trash_keys = identity_keys(*staged_trash, "live trash");
        if (!saved_deck_keys || !saved_hand_keys || !saved_trash_keys
            || !staged_deck_keys || !staged_hand_keys || !staged_trash_keys)
        {
            return false;
        }

        const auto expected_total = saved_deck_keys->size() + saved_hand_keys->size()
            + saved_trash_keys->size();
        const auto live_total = staged_deck_keys->size() + staged_hand_keys->size()
            + staged_trash_keys->size();
        if (live_total != expected_total)
        {
            error = "staged player-card total does not match the checkpoint";
            return false;
        }
        if (!staged_trash_keys->empty())
        {
            error = "live trash is not empty before native staging moves";
            return false;
        }
        if (staged_hand_keys->size() < saved_hand_keys->size())
        {
            error = "live hand is shorter than the saved hand";
            return false;
        }
        const auto hand_overflow_count = staged_hand_keys->size() - saved_hand_keys->size();
        if (hand_overflow_count > saved_trash_keys->size())
        {
            error = "live hand overflow is larger than the saved trash";
            return false;
        }
        const auto trash_in_deck_count = saved_trash_keys->size() - hand_overflow_count;

        if (native_order)
        {
            // Native startup pops extras from the deck's back after drawing the
            // saved hand. Their temporary hand suffix is therefore reversed.
            auto expected_staged_deck = *saved_deck_keys;
            expected_staged_deck.insert(expected_staged_deck.end(), saved_trash_keys->begin(),
                                       saved_trash_keys->begin() + trash_in_deck_count);
            auto expected_staged_hand = *saved_hand_keys;
            expected_staged_hand.insert(expected_staged_hand.end(), saved_trash_keys->rbegin(),
                                       saved_trash_keys->rbegin() + hand_overflow_count);
            if (*staged_deck_keys != expected_staged_deck
                || *staged_hand_keys != expected_staged_hand)
            {
                error = "native staging changed the saved deck/hand order or reversed extras suffix";
                return false;
            }
            return true;
        }

        // Controller getters expose a stable sorted view, not insertion order. The
        // overflow suffix drawn from the staged extras can therefore interleave with
        // the saved hand. Require an order-preserving merge of both sequences.
        const auto hand_columns = hand_overflow_count + 1;
        std::vector<bool> hand_reachable(
            (saved_hand_keys->size() + 1) * hand_columns);
        hand_reachable[0] = true;
        for (std::size_t hand_index{}; hand_index <= saved_hand_keys->size(); ++hand_index)
        {
            for (std::size_t extra_index{}; extra_index <= hand_overflow_count;
                 ++extra_index)
            {
                if (!hand_reachable[hand_index * hand_columns + extra_index])
                {
                    continue;
                }
                const auto live_index = hand_index + extra_index;
                if (hand_index < saved_hand_keys->size()
                    && (*staged_hand_keys)[live_index] == (*saved_hand_keys)[hand_index])
                {
                    hand_reachable[(hand_index + 1) * hand_columns + extra_index] = true;
                }
                if (extra_index < hand_overflow_count
                    && (*staged_hand_keys)[live_index]
                        == (*saved_trash_keys)[trash_in_deck_count + extra_index])
                {
                    hand_reachable[hand_index * hand_columns + extra_index + 1] = true;
                }
            }
        }
        if (!hand_reachable[
                saved_hand_keys->size() * hand_columns + hand_overflow_count])
        {
            error = "live hand is not an order-preserving merge of the saved hand and "
                "staged extras suffix";
            return false;
        }
        if (staged_deck_keys->size()
            != saved_deck_keys->size() + trash_in_deck_count)
        {
            error = "live deck size does not match the staged deck/trash merge";
            return false;
        }

        // The game's sorted deck view is a stable merge of the saved deck and the
        // undrawn trash prefix. Preserve the order of both input sequences, while
        // accepting either branch when duplicate identities make the merge ambiguous.
        const auto columns = trash_in_deck_count + 1;
        std::vector<bool> reachable((saved_deck_keys->size() + 1) * columns);
        reachable[0] = true;
        for (std::size_t deck_index{}; deck_index <= saved_deck_keys->size(); ++deck_index)
        {
            for (std::size_t trash_index{}; trash_index <= trash_in_deck_count;
                 ++trash_index)
            {
                if (!reachable[deck_index * columns + trash_index])
                {
                    continue;
                }
                const auto live_index = deck_index + trash_index;
                if (deck_index < saved_deck_keys->size()
                    && (*staged_deck_keys)[live_index] == (*saved_deck_keys)[deck_index])
                {
                    reachable[(deck_index + 1) * columns + trash_index] = true;
                }
                if (trash_index < trash_in_deck_count
                    && (*staged_deck_keys)[live_index]
                        == (*saved_trash_keys)[trash_index])
                {
                    reachable[deck_index * columns + trash_index + 1] = true;
                }
            }
        }
        if (!reachable[saved_deck_keys->size() * columns + trash_in_deck_count])
        {
            error = "live deck is not an order-preserving merge of the saved deck and trash";
            return false;
        }
        return true;
    }

    auto exact_player_field_startup_decklist(std::string_view active_decklist,
                                             std::string_view player_deck,
                                             std::string_view player_hand,
                                             std::string_view player_trash,
                                             std::string_view player_field,
                                             std::string& error,
                                             bool allow_single_generated_apple,
                                             std::size_t* additional_card_count)
        -> std::optional<std::string>
    {
        error.clear();
        if (additional_card_count) *additional_card_count = 0;
        auto deck = split_route_c_unreal_array(player_deck, error);
        auto hand = split_route_c_unreal_array(player_hand, error);
        auto trash = split_route_c_unreal_array(player_trash, error);
        auto field = split_route_c_unreal_array(player_field, error);
        if (!deck || !hand || !trash || !field
            || field->empty()
            || deck->size() + hand->size() + trash->size() + field->size() > 128)
        {
            error = "exact player-field zones are invalid or exceed 128 cards: " + error;
            return std::nullopt;
        }
        std::string staged_deck{"("};
        bool first = true;
        for (const auto* zone : {&*deck, &*trash, &*field})
        {
            for (const auto& card : *zone)
            {
                if (!first)
                {
                    staged_deck += ',';
                }
                first = false;
                staged_deck += card;
            }
        }
        staged_deck += ')';
        return exact_player_zones_startup_decklist(
            active_decklist, staged_deck, player_hand, error, allow_single_generated_apple, additional_card_count);
    }

    auto exact_player_field_staging_matches(std::string_view expected_deck,
                                            std::string_view expected_hand,
                                            std::string_view expected_trash,
                                            std::string_view expected_field,
                                            std::string_view live_deck,
                                            std::string_view live_hand,
                                            std::string_view live_trash,
                                            std::string& error) -> bool
    {
        error.clear();
        const auto keys = [&](std::string_view array) -> std::optional<std::vector<std::string>> {
            auto cards = split_route_c_unreal_array(array, error);
            if (!cards || cards->size() > 128) return std::nullopt;
            std::vector<std::string> result{};
            for (const auto& instance : *cards)
            {
                const auto card = exact_card_from_instance(instance, error);
                if (!card) return std::nullopt;
                result.push_back(ordered_card_key(*card));
            }
            return result;
        };
        const auto deck = keys(expected_deck);
        const auto hand = keys(expected_hand);
        const auto trash = keys(expected_trash);
        const auto field = keys(expected_field);
        const auto staged_deck = keys(live_deck);
        const auto staged_hand = keys(live_hand);
        const auto staged_trash = keys(live_trash);
        if (!deck || !hand || !trash || !field || field->empty()
            || !staged_deck || !staged_hand || !staged_trash || !staged_trash->empty())
        {
            error = "field staging arrays are invalid or live trash is not empty: " + error;
            return false;
        }

        // FIELD is serialized by destination slot, whereas the native controller
        // sorts temporarily drawn cards. Extra cards have no required temporary
        // order: they will be selected individually and placed into exact slots.
        // Still prove the complete multiset and the relative order of every card
        // that must remain in DECK/HAND. Final zones are checked verbatim afterward.
        const auto subsequence = [](const auto& expected, const auto& live) {
            std::size_t index{};
            for (const auto& key : live)
            {
                if (index < expected.size() && expected[index] == key) ++index;
            }
            return index == expected.size();
        };
        if (!subsequence(*deck, *staged_deck) || !subsequence(*hand, *staged_hand))
        {
            error = "field staging changed the retained deck/hand order";
            return false;
        }
        std::unordered_map<std::string, std::int32_t> expected_counts{};
        std::unordered_map<std::string, std::int32_t> live_counts{};
        for (const auto* zone : {&*deck, &*hand, &*trash, &*field})
        {
            for (const auto& key : *zone) ++expected_counts[key];
        }
        for (const auto* zone : {&*staged_deck, &*staged_hand})
        {
            for (const auto& key : *zone) ++live_counts[key];
        }
        if (expected_counts != live_counts)
        {
            error = "field staging changed card identities, upgrades, or multiplicities";
            return false;
        }
        return true;
    }

    auto exact_card_identity_key_from_instance(std::string_view card_instance,
                                               std::string& error)
        -> std::optional<std::string>
    {
        error.clear();
        auto card = exact_card_from_instance(card_instance, error);
        if (!card)
        {
            return std::nullopt;
        }
        return ordered_card_key(*card);
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

        if (checkpoint.schema_version >= 3)
            append_hash_bytes(hash, checkpoint.player_hand_health_checksum);
        if (checkpoint.schema_version >= ExactPlayerZonesBaseHealthSchemaVersion)
            append_hash_bytes(hash, checkpoint.player_deck_base_health_states);
        if (checkpoint.schema_version >= ExactPlayerZonesQualificationSchemaVersion)
            append_hash_bytes(hash, checkpoint.qualification_policy);

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
               << "  \"playerHand\": \"" << json_escape(checkpoint.player_hand) << "\",\n";
        if (checkpoint.schema_version >= 3)
            output << "  \"playerHandHealthChecksum\": \""
                   << json_escape(checkpoint.player_hand_health_checksum) << "\",\n";
        if (checkpoint.schema_version >= ExactPlayerZonesBaseHealthSchemaVersion)
            output << "  \"playerDeckBaseHealthStates\": \""
                   << json_escape(checkpoint.player_deck_base_health_states) << "\",\n";
        if (checkpoint.schema_version >= ExactPlayerZonesQualificationSchemaVersion
            || !checkpoint.qualification_policy.empty())
            output << "  \"qualificationPolicy\": \""
                   << json_escape(checkpoint.qualification_policy) << "\",\n";
        output << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_exact_player_zones_checkpoint(const ExactPlayerZonesCheckpoint& checkpoint,
                                                std::string& error) -> bool
    {
        if (checkpoint.schema_version < 1 || checkpoint.schema_version > ExactPlayerZonesSchemaVersion)
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
        if (!deck || (checkpoint.schema_version == 1 && deck->empty()))
        {
            error = "exact player deck is invalid: " + array_error;
            return false;
        }
        array_error.clear();
        const auto hand = split_route_c_unreal_array(checkpoint.player_hand, array_error);
        if (!hand || (checkpoint.schema_version == 1 && hand->empty())
            || (deck->empty() && hand->empty()) || deck->size() + hand->size() > 128)
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
        if (!validate_hand_health_checksum_link(hand->size(), checkpoint.player_hand_health_checksum,
                                               checkpoint.schema_version >= 3, error)) return false;
        if (!validate_off_field_base_health_records(deck->size(), checkpoint.player_deck_base_health_states,
                checkpoint.schema_version >= ExactPlayerZonesBaseHealthSchemaVersion, error)) return false;
        if (!validate_qualification_policy(checkpoint.qualification_policy,
                checkpoint.schema_version, checkpoint.kind, error)) return false;
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
        if (!reject_inline_qualification_fields(*values, error)) return std::nullopt;
#define READ_ZONES_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_ZONES_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_ZONES_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version < 1 || checkpoint.schema_version > ExactPlayerZonesSchemaVersion)
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
        if (checkpoint.schema_version >= 3)
        {
            READ_ZONES_STRING(player_hand_health_checksum, "playerHandHealthChecksum");
        }
        else if (values->contains("playerHandHealthChecksum"))
        {
            error = "legacy player-zones JSON contains a newer hand-health dependency";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= ExactPlayerZonesBaseHealthSchemaVersion)
        {
            READ_ZONES_STRING(player_deck_base_health_states, "playerDeckBaseHealthStates");
        }
        else if (values->contains("playerDeckBaseHealthStates"))
        {
            error = "legacy player-zones JSON contains newer DECK base-health records";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= ExactPlayerZonesQualificationSchemaVersion)
        {
            READ_ZONES_STRING(qualification_policy, "qualificationPolicy");
        }
        else if (values->contains("qualificationPolicy"))
        {
            error = "legacy player-zones JSON contains a qualification policy";
            return std::nullopt;
        }
        READ_ZONES_STRING(payload_checksum, "payloadChecksum");

#undef READ_ZONES_INTEGER
#undef READ_ZONES_STRING

        if (!validate_exact_player_zones_checkpoint(checkpoint, error))
        {
            return std::nullopt;
        }
        return checkpoint;
    }

    auto exact_player_trash_payload_checksum(const ExactPlayerTrashCheckpoint& checkpoint)
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
        append_hash_bytes(hash, checkpoint.player_trash);

        if (checkpoint.schema_version >= 4)
            append_hash_bytes(hash, checkpoint.player_hand_health_checksum);
        if (checkpoint.schema_version >= ExactPlayerTrashBaseHealthSchemaVersion)
        {
            append_hash_bytes(hash, checkpoint.player_deck_base_health_states);
            append_hash_bytes(hash, checkpoint.player_trash_base_health_states);
        }
        if (checkpoint.schema_version >= ExactPlayerTrashQualificationSchemaVersion)
            append_hash_bytes(hash, checkpoint.qualification_policy);

        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_exact_player_trash_checkpoint(ExactPlayerTrashCheckpoint checkpoint)
        -> std::string
    {
        checkpoint.payload_checksum = exact_player_trash_payload_checksum(checkpoint);
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
               << "  \"playerTrash\": \"" << json_escape(checkpoint.player_trash) << "\",\n";
        if (checkpoint.schema_version >= 4)
            output << "  \"playerHandHealthChecksum\": \""
                   << json_escape(checkpoint.player_hand_health_checksum) << "\",\n";
        if (checkpoint.schema_version >= ExactPlayerTrashBaseHealthSchemaVersion)
            output << "  \"playerDeckBaseHealthStates\": \""
                   << json_escape(checkpoint.player_deck_base_health_states) << "\",\n"
                   << "  \"playerTrashBaseHealthStates\": \""
                   << json_escape(checkpoint.player_trash_base_health_states) << "\",\n";
        if (checkpoint.schema_version >= ExactPlayerTrashQualificationSchemaVersion
            || !checkpoint.qualification_policy.empty())
            output << "  \"qualificationPolicy\": \""
                   << json_escape(checkpoint.qualification_policy) << "\",\n";
        output << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_exact_player_trash_checkpoint(const ExactPlayerTrashCheckpoint& checkpoint,
                                                std::string& error) -> bool
    {
        if (checkpoint.schema_version < 1 || checkpoint.schema_version > ExactPlayerTrashSchemaVersion)
        {
            error = "unsupported exact player-trash schema version";
            return false;
        }
        if (checkpoint.kind != ExactPlayerTrashCheckpointKind)
        {
            error = "checkpoint kind is not exact player trash";
            return false;
        }
        if (checkpoint.captured_at_utc.empty()
            || !is_hex_digest(checkpoint.route_c_payload_checksum, 16))
        {
            error = "exact player trash has invalid Route C linkage";
            return false;
        }
        if (!is_sha256(checkpoint.game_executable_sha256)
            || checkpoint.game_executable_size == 0)
        {
            error = "exact player trash has an invalid game executable fingerprint";
            return false;
        }
        if (checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256 || checkpoint.wave_index < 0
            || checkpoint.wave_index > 1000)
        {
            error = "exact player trash has invalid level or wave data";
            return false;
        }

        std::string array_error{};
        const auto deck = split_route_c_unreal_array(checkpoint.player_deck, array_error);
        if (!deck || (checkpoint.schema_version == 1 && deck->empty()))
        {
            error = "exact player-trash deck is invalid: " + array_error;
            return false;
        }
        array_error.clear();
        const auto hand = split_route_c_unreal_array(checkpoint.player_hand, array_error);
        if (!hand || (checkpoint.schema_version == 1 && hand->empty()))
        {
            error = "exact player-trash hand is invalid: " + array_error;
            return false;
        }
        array_error.clear();
        const auto trash = split_route_c_unreal_array(checkpoint.player_trash, array_error);
        if (!trash || trash->empty()
            || deck->size() + hand->size() + trash->size() > 128)
        {
            error = "exact player trash is empty, invalid, or total cards exceed 128: "
                + array_error;
            return false;
        }
        for (const auto* zone : {&*deck, &*hand, &*trash})
        {
            for (const auto& element : *zone)
            {
                array_error.clear();
                if (!exact_card_from_instance(element, array_error))
                {
                    error = "exact player-trash zone contains an invalid card: "
                        + array_error;
                    return false;
                }
            }
        }
        std::unordered_map<std::string, bool> hand_identities{};
        for (const auto& element : *hand)
        {
            array_error.clear();
            const auto card = exact_card_from_instance(element, array_error);
            if (!card)
            {
                error = "exact player-trash hand identity is invalid: " + array_error;
                return false;
            }
            hand_identities.emplace(ordered_card_key(*card), true);
        }
        for (const auto& element : *trash)
        {
            array_error.clear();
            const auto card = exact_card_from_instance(element, array_error);
            if (!card)
            {
                error = "exact player-trash trash identity is invalid: " + array_error;
                return false;
            }
            if (hand_identities.contains(ordered_card_key(*card))
                && (checkpoint.schema_version == 1 || !supports_plain_player_field_card(card->tag)))
            {
                error = "exact player trash shares a card identity with the hand; "
                    "the guarded mixed-zone staging slice would be ambiguous";
                return false;
            }
        }
        if (!validate_hand_health_checksum_link(hand->size(), checkpoint.player_hand_health_checksum,
                                               checkpoint.schema_version >= 4, error)) return false;
        if (!validate_off_field_base_health_records(deck->size(), checkpoint.player_deck_base_health_states,
                checkpoint.schema_version >= ExactPlayerTrashBaseHealthSchemaVersion, error)
            || !validate_off_field_base_health_records(trash->size(), checkpoint.player_trash_base_health_states,
                checkpoint.schema_version >= ExactPlayerTrashBaseHealthSchemaVersion, error)) return false;
        if (!validate_qualification_policy(checkpoint.qualification_policy,
                checkpoint.schema_version, checkpoint.kind, error)) return false;
        if (checkpoint.payload_checksum != exact_player_trash_payload_checksum(checkpoint))
        {
            error = "exact player-trash payload checksum does not match";
            return false;
        }
        return true;
    }

    auto parse_exact_player_trash_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactPlayerTrashCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "exact player-trash file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }
        auto values = FlatJsonParser{json}.parse(error);
        if (!values)
        {
            return std::nullopt;
        }

        ExactPlayerTrashCheckpoint checkpoint{};
        if (!reject_inline_qualification_fields(*values, error)) return std::nullopt;
#define READ_TRASH_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_TRASH_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_TRASH_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version < 1 || checkpoint.schema_version > ExactPlayerTrashSchemaVersion)
        {
            error = "unsupported exact player-trash schema version";
            return std::nullopt;
        }
        READ_TRASH_STRING(kind, "kind");
        READ_TRASH_STRING(captured_at_utc, "capturedAtUtc");
        READ_TRASH_STRING(route_c_payload_checksum, "routeCPayloadChecksum");
        READ_TRASH_STRING(game_executable_sha256, "gameExecutableSha256");
        READ_TRASH_INTEGER(game_executable_size, "gameExecutableSize", std::uint64_t);
        READ_TRASH_STRING(source_level_name, "sourceLevelName");
        READ_TRASH_INTEGER(wave_index, "waveIndex", std::int32_t);
        READ_TRASH_STRING(player_deck, "playerDeck");
        READ_TRASH_STRING(player_hand, "playerHand");
        READ_TRASH_STRING(player_trash, "playerTrash");
        if (checkpoint.schema_version >= 4)
        {
            READ_TRASH_STRING(player_hand_health_checksum, "playerHandHealthChecksum");
        }
        else if (values->contains("playerHandHealthChecksum"))
        {
            error = "legacy player-trash JSON contains a newer hand-health dependency";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= ExactPlayerTrashBaseHealthSchemaVersion)
        {
            READ_TRASH_STRING(player_deck_base_health_states, "playerDeckBaseHealthStates");
            READ_TRASH_STRING(player_trash_base_health_states, "playerTrashBaseHealthStates");
        }
        else if (values->contains("playerDeckBaseHealthStates") || values->contains("playerTrashBaseHealthStates"))
        {
            error = "legacy player-trash JSON contains newer DECK/TRASH base-health records";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= ExactPlayerTrashQualificationSchemaVersion)
        {
            READ_TRASH_STRING(qualification_policy, "qualificationPolicy");
        }
        else if (values->contains("qualificationPolicy"))
        {
            error = "legacy player-trash JSON contains a qualification policy";
            return std::nullopt;
        }
        READ_TRASH_STRING(payload_checksum, "payloadChecksum");

#undef READ_TRASH_INTEGER
#undef READ_TRASH_STRING

        if (!validate_exact_player_trash_checkpoint(checkpoint, error))
        {
            return std::nullopt;
        }
        return checkpoint;
    }

    auto exact_player_field_payload_checksum(const ExactPlayerFieldCheckpoint& checkpoint)
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
        append_hash_bytes(hash, checkpoint.player_trash);
        append_hash_bytes(hash, checkpoint.player_field);
        append_hash_bytes(hash, checkpoint.player_field_states);
        if (checkpoint.schema_version >= 3)
            append_hash_bytes(hash, checkpoint.player_field_attack_states);
        if (checkpoint.schema_version >= 4)
            append_hash_bytes(hash, checkpoint.player_field_health_states);
        if (checkpoint.schema_version >= 5)
            append_hash_bytes(hash, checkpoint.player_field_counter_states);

        if (checkpoint.schema_version >= 7)
            append_hash_bytes(hash, checkpoint.player_hand_health_checksum);
        if (checkpoint.schema_version >= ExactPlayerFieldBaseHealthSchemaVersion)
        {
            append_hash_bytes(hash, checkpoint.player_deck_base_health_states);
            append_hash_bytes(hash, checkpoint.player_trash_base_health_states);
        }
        if (checkpoint.schema_version >= ExactPlayerFieldQualificationSchemaVersion)
            append_hash_bytes(hash, checkpoint.qualification_policy);

        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_exact_player_field_checkpoint(ExactPlayerFieldCheckpoint checkpoint)
        -> std::string
    {
        checkpoint.payload_checksum = exact_player_field_payload_checksum(checkpoint);
        std::ostringstream output{};
        output << "{\n"
               << "  \"schemaVersion\": " << checkpoint.schema_version << ",\n"
               << "  \"kind\": \"" << json_escape(checkpoint.kind) << "\",\n"
               << "  \"capturedAtUtc\": \""
               << json_escape(checkpoint.captured_at_utc) << "\",\n"
               << "  \"routeCPayloadChecksum\": \""
               << json_escape(checkpoint.route_c_payload_checksum) << "\",\n"
               << "  \"gameExecutableSha256\": \""
               << json_escape(checkpoint.game_executable_sha256) << "\",\n"
               << "  \"gameExecutableSize\": " << checkpoint.game_executable_size << ",\n"
               << "  \"sourceLevelName\": \""
               << json_escape(checkpoint.source_level_name) << "\",\n"
               << "  \"waveIndex\": " << checkpoint.wave_index << ",\n"
               << "  \"playerDeck\": \"" << json_escape(checkpoint.player_deck)
               << "\",\n"
               << "  \"playerHand\": \"" << json_escape(checkpoint.player_hand)
               << "\",\n"
               << "  \"playerTrash\": \"" << json_escape(checkpoint.player_trash)
               << "\",\n"
               << "  \"playerField\": \"" << json_escape(checkpoint.player_field)
               << "\",\n"
               << "  \"playerFieldStates\": \""
               << json_escape(checkpoint.player_field_states) << "\",\n";
        if (checkpoint.schema_version >= 3)
            output << "  \"playerFieldAttackStates\": \""
                   << json_escape(checkpoint.player_field_attack_states) << "\",\n";
        if (checkpoint.schema_version >= 4)
            output << "  \"playerFieldHealthStates\": \""
                   << json_escape(checkpoint.player_field_health_states) << "\",\n";
        if (checkpoint.schema_version >= 5)
            output << "  \"playerFieldCounterStates\": \""
                   << json_escape(checkpoint.player_field_counter_states) << "\",\n";
        if (checkpoint.schema_version >= 7)
            output << "  \"playerHandHealthChecksum\": \""
                   << json_escape(checkpoint.player_hand_health_checksum) << "\",\n";
        if (checkpoint.schema_version >= ExactPlayerFieldBaseHealthSchemaVersion)
            output << "  \"playerDeckBaseHealthStates\": \""
                   << json_escape(checkpoint.player_deck_base_health_states) << "\",\n"
                   << "  \"playerTrashBaseHealthStates\": \""
                   << json_escape(checkpoint.player_trash_base_health_states) << "\",\n";
        if (checkpoint.schema_version >= ExactPlayerFieldQualificationSchemaVersion
            || !checkpoint.qualification_policy.empty())
            output << "  \"qualificationPolicy\": \""
                   << json_escape(checkpoint.qualification_policy) << "\",\n";
        output << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_exact_player_field_checkpoint(
        const ExactPlayerFieldCheckpoint& checkpoint, std::string& error) -> bool
    {
        if (checkpoint.schema_version < 1 || checkpoint.schema_version > ExactPlayerFieldSchemaVersion)
        {
            error = "unsupported exact player-field schema version";
            return false;
        }
        if (checkpoint.kind != ExactPlayerFieldCheckpointKind)
        {
            error = "checkpoint kind is not exact player field";
            return false;
        }
        if (checkpoint.captured_at_utc.empty()
            || !is_hex_digest(checkpoint.route_c_payload_checksum, 16))
        {
            error = "exact player field has invalid Route C linkage";
            return false;
        }
        if (!is_sha256(checkpoint.game_executable_sha256)
            || checkpoint.game_executable_size == 0)
        {
            error = "exact player field has an invalid game executable fingerprint";
            return false;
        }
        if (checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256 || checkpoint.wave_index < 0
            || checkpoint.wave_index > 1000)
        {
            error = "exact player field has invalid level or wave data";
            return false;
        }

        std::string array_error{};
        const auto deck = split_route_c_unreal_array(checkpoint.player_deck, array_error);
        const auto hand = split_route_c_unreal_array(checkpoint.player_hand, array_error);
        const auto trash = split_route_c_unreal_array(checkpoint.player_trash, array_error);
        const auto field = split_route_c_unreal_array(checkpoint.player_field, array_error);
        const auto states = parse_exact_player_field_states(
            checkpoint.player_field_states, array_error);
        if (!deck || !hand || !trash || !field || !states)
        {
            error = "exact player-field zones or aligned states are invalid: " + array_error;
            return false;
        }
        if (field->empty()
            || (checkpoint.schema_version == 1 && (deck->empty() || hand->empty())))
        {
            error = "exact player-field capture requires a field and valid versioned deck/hand bounds";
            return false;
        }
        if (states->size() != field->size())
        {
            error = "exact player-field card and state counts do not match";
            return false;
        }
        if (checkpoint.schema_version >= 3)
        {
            const auto attacks = parse_player_attack_states(checkpoint.player_field_attack_states, array_error);
            if (!attacks || attacks->size() != field->size())
            {
                error = "exact player-field attack states are invalid or misaligned: " + array_error;
                return false;
            }
        }
        else if (!checkpoint.player_field_attack_states.empty())
        {
            error = "legacy player-field schema cannot claim attack modifier coverage";
            return false;
        }
        if (checkpoint.schema_version >= 4)
        {
            const auto health = parse_player_health_states(checkpoint.player_field_health_states, array_error);
            const auto attack = parse_player_attack_states(checkpoint.player_field_attack_states, array_error);
            if (!health || !attack || health->size() != field->size())
            {
                error = "exact player-field health states are invalid or misaligned: " + array_error;
                return false;
            }
            for (std::size_t index{}; index < health->size(); ++index)
            {
                auto all_modifiers = (*attack)[index].modifiers;
                all_modifiers.insert(all_modifiers.end(),(*health)[index].modifiers.begin(),(*health)[index].modifiers.end());
                if (!validate_player_stat_modifiers(all_modifiers, error)) return false;
            }
        }
        else if (!checkpoint.player_field_health_states.empty())
        {
            error = "legacy player-field schema cannot claim health modifier coverage";
            return false;
        }
        if (checkpoint.schema_version >= 5)
        {
            const auto counters = parse_player_counter_states(checkpoint.player_field_counter_states,array_error);
            if (!counters || counters->size() != field->size())
            {
                error = "exact player-field counters are invalid or misaligned: " + array_error;
                return false;
            }
        }
        else if (!checkpoint.player_field_counter_states.empty())
        {
            error = "legacy player-field schema cannot claim counter coverage";
            return false;
        }
        if (deck->size() + hand->size() + trash->size() + field->size() > 128)
        {
            error = "exact player-field capture exceeds 128 cards";
            return false;
        }

        for (const auto& element : *hand)
        {
            auto card = exact_card_from_instance(element, array_error);
            if (!card)
            {
                error = "exact player-field hand contains an invalid card: " + array_error;
                return false;
            }
        }
        for (const auto* zone : {&*deck, &*trash, &*field})
        {
            for (const auto& element : *zone)
            {
                auto card = exact_card_from_instance(element, array_error);
                if (!card)
                {
                    error = "exact player-field zone contains an invalid card: "
                        + array_error;
                    return false;
                }
                if (zone == &*field && !supports_plain_player_field_card(card->tag))
                {
                    error = "exact player field contains a card outside the first guarded "
                        "plain-fruit slice";
                    return false;
                }
            }
        }
        if (!validate_hand_health_checksum_link(hand->size(), checkpoint.player_hand_health_checksum,
                                               checkpoint.schema_version >= 7, error)) return false;
        if (!validate_off_field_base_health_records(deck->size(), checkpoint.player_deck_base_health_states,
                checkpoint.schema_version >= ExactPlayerFieldBaseHealthSchemaVersion, error)
            || !validate_off_field_base_health_records(trash->size(), checkpoint.player_trash_base_health_states,
                checkpoint.schema_version >= ExactPlayerFieldBaseHealthSchemaVersion, error)) return false;
        if (!validate_qualification_policy(checkpoint.qualification_policy,
                checkpoint.schema_version, checkpoint.kind, error)) return false;
        if (checkpoint.payload_checksum != exact_player_field_payload_checksum(checkpoint))
        {
            error = "exact player-field payload checksum does not match";
            return false;
        }
        return true;
    }

    auto parse_exact_player_field_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactPlayerFieldCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "exact player-field file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }
        auto values = FlatJsonParser{json}.parse(error);
        if (!values)
        {
            return std::nullopt;
        }

        ExactPlayerFieldCheckpoint checkpoint{};
        if (!reject_inline_qualification_fields(*values, error)) return std::nullopt;
#define READ_FIELD_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_FIELD_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_FIELD_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version < 1 || checkpoint.schema_version > ExactPlayerFieldSchemaVersion)
        {
            error = "unsupported exact player-field schema version";
            return std::nullopt;
        }
        READ_FIELD_STRING(kind, "kind");
        READ_FIELD_STRING(captured_at_utc, "capturedAtUtc");
        READ_FIELD_STRING(route_c_payload_checksum, "routeCPayloadChecksum");
        READ_FIELD_STRING(game_executable_sha256, "gameExecutableSha256");
        READ_FIELD_INTEGER(game_executable_size, "gameExecutableSize", std::uint64_t);
        READ_FIELD_STRING(source_level_name, "sourceLevelName");
        READ_FIELD_INTEGER(wave_index, "waveIndex", std::int32_t);
        READ_FIELD_STRING(player_deck, "playerDeck");
        READ_FIELD_STRING(player_hand, "playerHand");
        READ_FIELD_STRING(player_trash, "playerTrash");
        READ_FIELD_STRING(player_field, "playerField");
        READ_FIELD_STRING(player_field_states, "playerFieldStates");
        if (checkpoint.schema_version >= 3)
        {
            READ_FIELD_STRING(player_field_attack_states, "playerFieldAttackStates");
        }
        else if (values->contains("playerFieldAttackStates"))
        {
            error = "legacy player-field JSON contains a newer attack record";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= 4)
        {
            READ_FIELD_STRING(player_field_health_states, "playerFieldHealthStates");
        }
        else if (values->contains("playerFieldHealthStates"))
        {
            error = "legacy player-field JSON contains a newer health record";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= 5)
        {
            READ_FIELD_STRING(player_field_counter_states, "playerFieldCounterStates");
        }
        else if (values->contains("playerFieldCounterStates"))
        {
            error = "legacy player-field JSON contains a newer counter record";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= 7)
        {
            READ_FIELD_STRING(player_hand_health_checksum, "playerHandHealthChecksum");
        }
        else if (values->contains("playerHandHealthChecksum"))
        {
            error = "legacy player-field JSON contains a newer hand-health dependency";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= ExactPlayerFieldBaseHealthSchemaVersion)
        {
            READ_FIELD_STRING(player_deck_base_health_states, "playerDeckBaseHealthStates");
            READ_FIELD_STRING(player_trash_base_health_states, "playerTrashBaseHealthStates");
        }
        else if (values->contains("playerDeckBaseHealthStates") || values->contains("playerTrashBaseHealthStates"))
        {
            error = "legacy player-field JSON contains newer DECK/TRASH base-health records";
            return std::nullopt;
        }
        if (checkpoint.schema_version >= ExactPlayerFieldQualificationSchemaVersion)
        {
            READ_FIELD_STRING(qualification_policy, "qualificationPolicy");
        }
        else if (values->contains("qualificationPolicy"))
        {
            error = "legacy player-field JSON contains a qualification policy";
            return std::nullopt;
        }
        READ_FIELD_STRING(payload_checksum, "payloadChecksum");

#undef READ_FIELD_INTEGER
#undef READ_FIELD_STRING

        if (!validate_exact_player_field_checkpoint(checkpoint, error))
        {
            return std::nullopt;
        }
        return checkpoint;
    }

    namespace
    {
        // Q1 deliberately uses bounded dynamic flat JSON keys, sharing the existing
        // string/integer parser. No new generic JSON dependency or F1 text format.
        auto qualification_flags_text(const std::vector<std::uint8_t>& flags) -> std::string
        {
            std::string result{};
            for (const auto flag : flags)
            {
                if (!result.empty()) result += ',';
                result += std::to_string(flag);
            }
            return result;
        }

        auto parse_qualification_flags(std::string_view text, std::string& error)
            -> std::optional<std::vector<std::uint8_t>>
        {
            std::vector<std::uint8_t> result{};
            if (text.empty()) return result;
            if (text.size() > 15 || text.size() % 2 == 0)
            {
                error = "qualification flags are not canonical unique values 0..7";
                return std::nullopt;
            }
            for (std::size_t index{}; index < text.size(); index += 2)
            {
                if (text[index] < '0' || text[index] > '7'
                    || (index != 0 && text[index - 1] != ',')
                    || (!result.empty() && text[index] - '0' <= result.back()))
                {
                    error = "qualification flags are not canonical unique values 0..7";
                    return std::nullopt;
                }
                result.push_back(static_cast<std::uint8_t>(text[index] - '0'));
            }
            return result;
        }

        auto qualification_zone_counts(const ExactPlayerQualificationCheckpoint& checkpoint)
            -> std::array<std::size_t, 4>
        {
            std::array<std::size_t, 4> counts{};
            for (const auto& card : checkpoint.cards)
                if (card.native_location >= 0 && card.native_location < 4)
                    ++counts[static_cast<std::size_t>(card.native_location)];
            return counts;
        }

        auto qualification_struct_fields(std::string_view text, std::string& error)
            -> std::optional<std::unordered_map<std::string, std::string_view>>
        {
            if (text.size() < 2 || text.front() != '(' || text.back() != ')')
            {
                error = "qualification CardInfoInstance is not a complete struct";
                return std::nullopt;
            }
            text = text.substr(1, text.size() - 2);
            std::unordered_map<std::string, std::string_view> fields{};
            std::size_t start{};
            int depth{};
            bool quoted{}, escaped{};
            for (std::size_t position{}; position <= text.size(); ++position)
            {
                const auto character = position == text.size() ? '\0' : text[position];
                if (quoted)
                {
                    if (position == text.size()) break;
                    if (escaped) escaped = false;
                    else if (character == '\\') escaped = true;
                    else if (character == '"') quoted = false;
                    continue;
                }
                if (character == '"') { quoted = true; continue; }
                if (character == '(')
                {
                    if (++depth > 64) break;
                    continue;
                }
                if (character == ')')
                {
                    if (--depth < 0) break;
                    continue;
                }
                if (position != text.size() && (character != ',' || depth != 0)) continue;
                if (depth != 0) break;
                const auto field = text.substr(start, position - start);
                const auto separator = field.find('=');
                if (separator == std::string_view::npos || separator == 0
                    || separator + 1 == field.size()) break;
                const auto name = field.substr(0, separator);
                if (!std::all_of(name.begin(), name.end(), [](unsigned char value) {
                        return std::isalnum(value) || value == '_';
                    }) || !fields.emplace(std::string{name}, field.substr(separator + 1)).second) break;
                if (position == text.size()) return fields;
                start = position + 1;
            }
            error = "qualification CardInfoInstance contains malformed or duplicate fields";
            return std::nullopt;
        }

        auto qualification_card_key(std::string_view instance, std::string& error)
            -> std::optional<std::string>
        {
            if (instance.empty() || instance.size() > 256U * 1024U
                || std::any_of(instance.begin(), instance.end(), [](unsigned char value) {
                    return value < 0x20;
                }))
            {
                error = "qualification CardInfoInstance is empty, oversized or contains controls";
                return std::nullopt;
            }
            const auto fields = qualification_struct_fields(instance, error);
            if (!fields) return std::nullopt;
            if (!fields->contains("CardInfo"))
            {
                error = "qualification CardInfoInstance is missing CardInfo";
                return std::nullopt;
            }
            const auto info = qualification_struct_fields(fields->at("CardInfo"), error);
            if (!info) return std::nullopt;
            if (!info->contains("Tag"))
            {
                error = "qualification CardInfoInstance is missing Tag";
                return std::nullopt;
            }
            const auto tag = info->at("Tag");
            if (tag.size() < 3 || tag.front() != '"' || tag.back() != '"'
                || !std::all_of(tag.begin() + 1, tag.end() - 1, [](unsigned char value) {
                    return std::isalnum(value) || value == '_';
                }))
            {
                error = "qualification CardInfoInstance has an invalid Tag";
                return std::nullopt;
            }
            std::int32_t upgrade{};
            if (const auto found = fields->find("upgradeLevel"); found != fields->end())
            {
                const auto value = found->second;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), upgrade);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
                    || upgrade < 0 || upgrade > 1000 || std::to_string(upgrade) != value)
                {
                    error = "qualification CardInfoInstance has an invalid upgradeLevel";
                    return std::nullopt;
                }
            }
            return std::string{tag.substr(1, tag.size() - 2)} + "@" + std::to_string(upgrade);
        }

        template <typename Layout>
        auto qualification_metadata_matches(const ExactPlayerQualificationCheckpoint& checkpoint,
                                              const Layout& layout, bool required,
                                              std::string& error) -> bool
        {
            const auto expected_policy = qualification::qualification_policy_for_layout(layout.kind, layout.schema_version);
            if (!required || !expected_policy || layout.qualification_policy != *expected_policy
                || checkpoint.qualification_policy != layout.qualification_policy
                || checkpoint.layout_kind != layout.kind
                || checkpoint.layout_payload_checksum != layout.payload_checksum
                || checkpoint.captured_at_utc != layout.captured_at_utc
                || checkpoint.route_c_payload_checksum != layout.route_c_payload_checksum
                || checkpoint.game_executable_sha256 != layout.game_executable_sha256
                || checkpoint.game_executable_size != layout.game_executable_size
                || checkpoint.source_level_name != layout.source_level_name
                || checkpoint.wave_index != layout.wave_index)
            {
                error = "qualification proof does not bind the selected required player layout";
                return false;
            }
            return true;
        }

        auto qualification_cards_match(const ExactPlayerQualificationCheckpoint& checkpoint,
                                       const std::array<std::string_view, 4>& arrays,
                                       const std::vector<ExactPlayerFieldCardState>& field_states,
                                       std::string& error) -> bool
        {
            std::size_t offset{};
            for (std::size_t zone{}; zone < arrays.size(); ++zone)
            {
                const auto cards = split_route_c_unreal_array(arrays[zone], error);
                if (!cards) return false;
                if (zone == 3 && cards->size() != field_states.size()) return false;
                for (std::size_t index{}; index < cards->size(); ++index)
                {
                    if (offset >= checkpoint.cards.size())
                    {
                        error = "qualification proof omits a selected layout card";
                        return false;
                    }
                    const auto& card = checkpoint.cards[offset++];
                    if (card.native_location != static_cast<std::int32_t>(zone)
                        || card.card_info_instance != (*cards)[index]
                        || (zone != 3 && card.native_index != static_cast<std::int32_t>(index))
                        || (zone == 3 && (card.row != field_states[index].row
                                          || card.index != field_states[index].index)))
                    {
                        error = "qualification card order, full CardInfoInstance or FIELD slot disagrees with layout";
                        return false;
                    }
                }
            }
            if (offset != checkpoint.cards.size())
            {
                error = "qualification proof contains extra selected layout cards";
                return false;
            }
            return true;
        }
    }

    auto exact_player_qualification_payload_checksum(
        const ExactPlayerQualificationCheckpoint& checkpoint) -> std::string
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
        append_hash_bytes(hash, checkpoint.layout_kind);
        append_hash_bytes(hash, checkpoint.layout_payload_checksum);
        append_hash_bytes(hash, checkpoint.qualification_policy);
        append_hash_number(hash, checkpoint.cards.size());
        for (const auto count : qualification_zone_counts(checkpoint)) append_hash_number(hash, count);
        for (const auto& card : checkpoint.cards)
        {
            append_hash_number(hash, card.native_location);
            append_hash_number(hash, card.native_index);
            append_hash_number(hash, card.row);
            append_hash_number(hash, card.index);
            append_hash_bytes(hash, card.card_info_instance);
            append_hash_number(hash, card.effects.size());
            for (const auto& effect : card.effects)
            {
                append_hash_number(hash, effect.ordinal);
                append_hash_bytes(hash, effect.factory_key);
                append_hash_bytes(hash, effect.tag);
                append_hash_number(hash, effect.type);
                append_hash_number(hash, effect.flags.size());
                for (const auto flag : effect.flags) append_hash_number(hash, flag);
                append_hash_number(hash, effect.used_once);
                append_hash_bytes(hash, effect.profile_id);
            }
        }
        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_exact_player_qualification_checkpoint(ExactPlayerQualificationCheckpoint checkpoint)
        -> std::string
    {
        checkpoint.payload_checksum = exact_player_qualification_payload_checksum(checkpoint);
        std::ostringstream output{};
        output << "{\n";
        const auto string_field = [&](std::string_view name, std::string_view value) {
            output << "  \"" << name << "\": \"" << json_escape(value) << "\",\n";
        };
        const auto number_field = [&](std::string_view name, auto value) {
            output << "  \"" << name << "\": " << value << ",\n";
        };
        number_field("schemaVersion", checkpoint.schema_version);
        string_field("kind", checkpoint.kind);
        string_field("capturedAtUtc", checkpoint.captured_at_utc);
        string_field("routeCPayloadChecksum", checkpoint.route_c_payload_checksum);
        string_field("gameExecutableSha256", checkpoint.game_executable_sha256);
        number_field("gameExecutableSize", checkpoint.game_executable_size);
        string_field("sourceLevelName", checkpoint.source_level_name);
        number_field("waveIndex", checkpoint.wave_index);
        string_field("layoutKind", checkpoint.layout_kind);
        string_field("layoutPayloadChecksum", checkpoint.layout_payload_checksum);
        string_field("qualificationPolicy", checkpoint.qualification_policy);
        number_field("cardCount", checkpoint.cards.size());
        const auto counts = qualification_zone_counts(checkpoint);
        constexpr std::array<std::string_view, 4> count_names{
            "handCardCount", "deckCardCount", "trashCardCount", "fieldCardCount"};
        for (std::size_t zone{}; zone < counts.size(); ++zone) number_field(count_names[zone], counts[zone]);
        for (std::size_t index{}; index < checkpoint.cards.size(); ++index)
        {
            const auto prefix = "card" + std::to_string(index);
            const auto& card = checkpoint.cards[index];
            number_field(prefix + "NativeLocation", card.native_location);
            number_field(prefix + "NativeIndex", card.native_index);
            number_field(prefix + "Row", card.row);
            number_field(prefix + "Index", card.index);
            string_field(prefix + "CardInfoInstance", card.card_info_instance);
            number_field(prefix + "EffectCount", card.effects.size());
            for (std::size_t ordinal{}; ordinal < card.effects.size(); ++ordinal)
            {
                const auto effect_prefix = prefix + "Effect" + std::to_string(ordinal);
                const auto& effect = card.effects[ordinal];
                number_field(effect_prefix + "Ordinal", effect.ordinal);
                string_field(effect_prefix + "FactoryKey", effect.factory_key);
                string_field(effect_prefix + "Tag", effect.tag);
                number_field(effect_prefix + "Type", effect.type);
                string_field(effect_prefix + "Flags", qualification_flags_text(effect.flags));
                number_field(effect_prefix + "UsedOnce", effect.used_once);
                string_field(effect_prefix + "ProfileId", effect.profile_id);
            }
        }
        output << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n}\n";
        return output.str();
    }

    auto validate_exact_player_qualification_checkpoint(
        const ExactPlayerQualificationCheckpoint& checkpoint, std::string& error) -> bool
    {
        const auto expected_schema = qualification::qualification_proof_schema_for_policy(checkpoint.qualification_policy);
        if (!expected_schema || checkpoint.schema_version != *expected_schema
            || checkpoint.kind != ExactPlayerQualificationCheckpointKind)
        {
            error = "unsupported or mismatched exact player-qualification policy/schema/kind";
            return false;
        }
        if (checkpoint.captured_at_utc.empty() || checkpoint.captured_at_utc.size() > 128
            || !is_hex_digest(checkpoint.route_c_payload_checksum, 16)
            || !is_hex_digest(checkpoint.layout_payload_checksum, 16)
            || !is_sha256(checkpoint.game_executable_sha256) || checkpoint.game_executable_size == 0
            || checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256 || checkpoint.wave_index < 0
            || checkpoint.wave_index > 1000)
        {
            error = "exact player qualification has invalid metadata or layout linkage";
            return false;
        }
        const auto counts = qualification_zone_counts(checkpoint);
        if (checkpoint.cards.empty() || checkpoint.cards.size() > ExactPlayerQualificationMaximumCards
            || counts[0] > 7 || counts[3] > 10
            || (checkpoint.layout_kind == ExactPlayerZonesCheckpointKind
                ? counts[2] != 0 || counts[3] != 0
                : checkpoint.layout_kind == ExactPlayerTrashCheckpointKind
                    ? counts[2] == 0 || counts[3] != 0
                    : checkpoint.layout_kind == ExactPlayerFieldCheckpointKind ? counts[3] == 0 : true))
        {
            error = "qualification cards violate the selected layout kind or card bounds";
            return false;
        }
        std::array<std::int32_t, 3> next_index{};
        std::int32_t previous_zone{-1}, previous_slot{-1};
        for (const auto& card : checkpoint.cards)
        {
            if (card.native_location < 0 || card.native_location > 3
                || card.native_location < previous_zone)
            {
                error = "qualification cards have invalid or unordered native locations";
                return false;
            }
            previous_zone = card.native_location;
            if (card.native_location == 3)
            {
                if (card.native_index != -1 || card.row < 0 || card.row > 1
                    || card.index < 0 || card.index > 4)
                {
                    error = "qualification FIELD cards have invalid, duplicate or unordered slots";
                    return false;
                }
                const auto slot = card.row * 5 + card.index;
                if (slot <= previous_slot)
                {
                    error = "qualification FIELD cards have duplicate or unordered slots";
                    return false;
                }
                previous_slot = slot;
            }
            else if (card.row != -1 || card.index != -1
                     || card.native_index != next_index[static_cast<std::size_t>(card.native_location)]++)
            {
                error = "qualification native indexes are not unique and contiguous or contain FIELD slots";
                return false;
            }
            const auto key = qualification_card_key(card.card_info_instance, error);
            if (!key) return false;
            const auto expected_count = qualification::qualification_card_effect_count(*key, checkpoint.qualification_policy);
            if (!expected_count || card.effects.size() != *expected_count
                || card.effects.size() > ExactPlayerQualificationMaximumEffectsPerCard)
            {
                error = "unsupported-profile: qualification card effect count has no reviewed exact profile";
                return false;
            }
            for (std::size_t ordinal{}; ordinal < card.effects.size(); ++ordinal)
            {
                const auto& effect = card.effects[ordinal];
                if (effect.ordinal != static_cast<std::int32_t>(ordinal)
                    || effect.type < 0 || effect.type > 255 || effect.flags.size() > 8)
                {
                    error = "qualification effect ordinal, type or flag count is invalid";
                    return false;
                }
                int previous_flag{-1};
                for (const auto flag : effect.flags)
                {
                    if (flag > 7 || flag <= previous_flag)
                    {
                        error = "qualification flags are not canonical unique values 0..7";
                        return false;
                    }
                    previous_flag = flag;
                    if (flag == 0 && checkpoint.qualification_policy == ExactPlayerQualificationPolicy)
                    {
                        error = "unsupported-once: flags-baseline-v1 cannot admit ONCE";
                        return false;
                    }
                }
                if (effect.used_once < 0 || effect.used_once > 1)
                {
                    error = "qualification usedOnce is outside 0..1";
                    return false;
                }
                if (effect.used_once != 0 && checkpoint.qualification_policy == ExactPlayerQualificationPolicy)
                {
                    error = "unsupported-used: flags-baseline-v1 requires zero used";
                    return false;
                }
                const auto profile = qualification::lookup_effect_qualification_profile(
                    *key, ordinal, effect.factory_key, effect.tag, effect.type, checkpoint.qualification_policy);
                if (!profile || effect.profile_id != profile->id)
                {
                    error = "unsupported-profile: qualification effect identity or profile id is not reviewed";
                    return false;
                }
                if (!profile->permits_used_once_replay
                    && std::find(effect.flags.begin(), effect.flags.end(), std::uint8_t{0}) != effect.flags.end())
                {
                    error = "unsupported-once: only the exact B base Bow profile admits ONCE";
                    return false;
                }
                if (effect.used_once != 0 && !profile->permits_used_once_replay)
                {
                    error = checkpoint.qualification_policy == ExactPlayerQualificationPolicy
                        ? "unsupported-used: flags-baseline-v1 requires zero used"
                        : "unsupported-used: non-Bow profiles require zero used";
                    return false;
                }
                if (!std::equal(effect.flags.begin(), effect.flags.end(), profile->flags.begin(), profile->flags.end()))
                {
                    error = "flags-profile-mismatch: qualification flags disagree with reviewed defaults";
                    return false;
                }
            }
        }
        if (checkpoint.payload_checksum != exact_player_qualification_payload_checksum(checkpoint))
        {
            error = "exact player-qualification payload checksum does not match";
            return false;
        }
        return true;
    }

    auto parse_exact_player_qualification_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactPlayerQualificationCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "exact player-qualification file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }
        constexpr auto maximum_fields = 17 + ExactPlayerQualificationMaximumCards
            * (6 + 7 * ExactPlayerQualificationMaximumEffectsPerCard);
        auto values = FlatJsonParser{json, maximum_fields, true}.parse(error);
        if (!values) return std::nullopt;
        ExactPlayerQualificationCheckpoint checkpoint{};
        std::size_t consumed{};
        const auto read_string = [&](std::string& target, std::string_view name) {
            auto value = required_string(*values, name, error);
            if (!value) return false;
            ++consumed;
            target = std::move(*value);
            return true;
        };
        const auto read_integer = [&]<typename Number>(Number& target, std::string_view name) {
            const auto value = required_integer<Number>(*values, name, error);
            if (!value) return false;
            ++consumed;
            target = *value;
            return true;
        };
        if (!read_integer(checkpoint.schema_version, "schemaVersion")
            || !read_string(checkpoint.kind, "kind")
            || !read_string(checkpoint.captured_at_utc, "capturedAtUtc")
            || !read_string(checkpoint.route_c_payload_checksum, "routeCPayloadChecksum")
            || !read_string(checkpoint.game_executable_sha256, "gameExecutableSha256")
            || !read_integer(checkpoint.game_executable_size, "gameExecutableSize")
            || !read_string(checkpoint.source_level_name, "sourceLevelName")
            || !read_integer(checkpoint.wave_index, "waveIndex")
            || !read_string(checkpoint.layout_kind, "layoutKind")
            || !read_string(checkpoint.layout_payload_checksum, "layoutPayloadChecksum")
            || !read_string(checkpoint.qualification_policy, "qualificationPolicy")
            || !read_string(checkpoint.payload_checksum, "payloadChecksum")) return std::nullopt;
        std::size_t card_count{};
        std::array<std::size_t, 4> counts{};
        constexpr std::array<std::string_view, 4> count_names{
            "handCardCount", "deckCardCount", "trashCardCount", "fieldCardCount"};
        if (!read_integer(card_count, "cardCount")) return std::nullopt;
        for (std::size_t zone{}; zone < counts.size(); ++zone)
        {
            if (!read_integer(counts[zone], count_names[zone])) return std::nullopt;
            if (counts[zone] > ExactPlayerQualificationMaximumCards)
            {
                error = "qualification zone card count exceeds 128";
                return std::nullopt;
            }
        }
        if (card_count > ExactPlayerQualificationMaximumCards
            || card_count != counts[0] + counts[1] + counts[2] + counts[3])
        {
            error = "qualification card count exceeds 128 or disagrees with explicit zone counts";
            return std::nullopt;
        }
        checkpoint.cards.reserve(card_count);
        for (std::size_t index{}; index < card_count; ++index)
        {
            const auto prefix = "card" + std::to_string(index);
            ExactPlayerQualificationCard card{};
            std::size_t effect_count{};
            if (!read_integer(card.native_location, prefix + "NativeLocation")
                || !read_integer(card.native_index, prefix + "NativeIndex")
                || !read_integer(card.row, prefix + "Row") || !read_integer(card.index, prefix + "Index")
                || !read_string(card.card_info_instance, prefix + "CardInfoInstance")
                || !read_integer(effect_count, prefix + "EffectCount")) return std::nullopt;
            if (effect_count > ExactPlayerQualificationMaximumEffectsPerCard)
            {
                error = "qualification card effect count exceeds 64";
                return std::nullopt;
            }
            card.effects.reserve(effect_count);
            for (std::size_t ordinal{}; ordinal < effect_count; ++ordinal)
            {
                const auto effect_prefix = prefix + "Effect" + std::to_string(ordinal);
                ExactPlayerQualificationEffect effect{};
                std::string flags{};
                if (!read_integer(effect.ordinal, effect_prefix + "Ordinal")
                    || !read_string(effect.factory_key, effect_prefix + "FactoryKey")
                    || !read_string(effect.tag, effect_prefix + "Tag")
                    || !read_integer(effect.type, effect_prefix + "Type")
                    || !read_string(flags, effect_prefix + "Flags")
                    || !read_integer(effect.used_once, effect_prefix + "UsedOnce")
                    || !read_string(effect.profile_id, effect_prefix + "ProfileId")) return std::nullopt;
                auto parsed = parse_qualification_flags(flags, error);
                if (!parsed) return std::nullopt;
                effect.flags = std::move(*parsed);
                card.effects.push_back(std::move(effect));
            }
            checkpoint.cards.push_back(std::move(card));
        }
        if (values->size() != consumed)
        {
            error = "qualification JSON contains extra or malformed dynamic fields";
            return std::nullopt;
        }
        if (counts != qualification_zone_counts(checkpoint))
        {
            error = "qualification explicit zone counts disagree with card records";
            return std::nullopt;
        }
        if (!validate_exact_player_qualification_checkpoint(checkpoint, error)) return std::nullopt;
        return checkpoint;
    }

    auto validate_exact_player_qualification_binding(const ExactPlayerQualificationCheckpoint& checkpoint,
                                                      const ExactPlayerZonesCheckpoint& layout,
                                                      std::string& error) -> bool
    {
        return validate_exact_player_qualification_checkpoint(checkpoint, error)
            && validate_exact_player_zones_checkpoint(layout, error)
            && qualification_metadata_matches(checkpoint, layout,
                layout.schema_version >= ExactPlayerZonesQualificationSchemaVersion, error)
            && qualification_cards_match(checkpoint, {layout.player_hand, layout.player_deck, "()", "()"}, {}, error);
    }

    auto validate_exact_player_qualification_binding(const ExactPlayerQualificationCheckpoint& checkpoint,
                                                      const ExactPlayerTrashCheckpoint& layout,
                                                      std::string& error) -> bool
    {
        return validate_exact_player_qualification_checkpoint(checkpoint, error)
            && validate_exact_player_trash_checkpoint(layout, error)
            && qualification_metadata_matches(checkpoint, layout,
                layout.schema_version >= ExactPlayerTrashQualificationSchemaVersion, error)
            && qualification_cards_match(checkpoint,
                {layout.player_hand, layout.player_deck, layout.player_trash, "()"}, {}, error);
    }

    auto validate_exact_player_qualification_binding(const ExactPlayerQualificationCheckpoint& checkpoint,
                                                      const ExactPlayerFieldCheckpoint& layout,
                                                      std::string& error) -> bool
    {
        if (!validate_exact_player_qualification_checkpoint(checkpoint, error)
            || !validate_exact_player_field_checkpoint(layout, error)
            || !qualification_metadata_matches(checkpoint, layout,
                layout.schema_version >= ExactPlayerFieldQualificationSchemaVersion, error)) return false;
        const auto states = parse_exact_player_field_states(layout.player_field_states, error);
        return states && qualification_cards_match(checkpoint,
            {layout.player_hand, layout.player_deck, layout.player_trash, layout.player_field}, *states, error);
    }

    auto exact_player_hand_health_payload_checksum(
        const ExactPlayerHandHealthCheckpoint& checkpoint) -> std::string
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
        append_hash_bytes(hash, checkpoint.player_hand);
        append_hash_bytes(hash, checkpoint.player_hand_health_states);
        if (checkpoint.schema_version >= 2)
            append_hash_bytes(hash, checkpoint.player_hand_counter_states);

        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_exact_player_hand_health_checkpoint(
        ExactPlayerHandHealthCheckpoint checkpoint) -> std::string
    {
        checkpoint.payload_checksum = exact_player_hand_health_payload_checksum(checkpoint);
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
               << "  \"playerHand\": \"" << json_escape(checkpoint.player_hand) << "\",\n"
               << "  \"playerHandHealthStates\": \""
               << json_escape(checkpoint.player_hand_health_states) << "\",\n";
        if (checkpoint.schema_version >= 2)
            output << "  \"playerHandCounterStates\": \""
                   << json_escape(checkpoint.player_hand_counter_states) << "\",\n";
        output << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_exact_player_hand_health_checkpoint(
        const ExactPlayerHandHealthCheckpoint& checkpoint, std::string& error) -> bool
    {
        if (checkpoint.schema_version < 1 || checkpoint.schema_version > ExactPlayerHandHealthSchemaVersion)
        {
            error = "unsupported exact player-hand-health schema version";
            return false;
        }
        if (checkpoint.kind != ExactPlayerHandHealthCheckpointKind)
        {
            error = "checkpoint kind is not exact player hand health";
            return false;
        }
        if (checkpoint.captured_at_utc.empty()
            || !is_hex_digest(checkpoint.route_c_payload_checksum, 16))
        {
            error = "exact player hand health has invalid Route C linkage";
            return false;
        }
        if (!is_sha256(checkpoint.game_executable_sha256)
            || checkpoint.game_executable_size == 0)
        {
            error = "exact player hand health has an invalid game executable fingerprint";
            return false;
        }
        if (checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256 || checkpoint.wave_index < 0
            || checkpoint.wave_index > 1000)
        {
            error = "exact player hand health has invalid level or wave data";
            return false;
        }

        std::string state_error{};
        const auto hand = split_route_c_unreal_array(checkpoint.player_hand, state_error);
        if (!hand || hand->empty() || hand->size() > 7)
        {
            error = "exact player-hand-health capture requires one to seven hand cards: "
                + state_error;
            return false;
        }
        const auto health = parse_player_health_states(
            checkpoint.player_hand_health_states, state_error);
        if (!health || health->size() != hand->size())
        {
            error = "exact player-hand-health states are invalid or misaligned: "
                + state_error;
            return false;
        }
        for (std::size_t index{}; index < hand->size(); ++index)
        {
            if (!exact_card_from_instance((*hand)[index], state_error))
            {
                error = "exact player-hand-health hand contains an invalid card: " + state_error;
                return false;
            }
            for (const auto& modifier : (*health)[index].modifiers)
            {
                if (modifier.amount < 0)
                {
                    error = "exact player-hand-health restoration requires nonnegative HEALTH modifiers";
                    return false;
                }
            }
        }
        if (checkpoint.schema_version >= 2)
        {
            const auto counters = parse_player_counter_states(
                checkpoint.player_hand_counter_states, state_error);
            if (!counters || counters->size() != hand->size())
            {
                error = "exact player-hand counter states are invalid or misaligned: " + state_error;
                return false;
            }
            for (const auto& state : *counters)
            {
                if (!state.special.empty())
                {
                    error = "exact player-hand restoration only supports generic counters";
                    return false;
                }
            }
        }
        else if (!checkpoint.player_hand_counter_states.empty())
        {
            error = "legacy player-hand-health schema cannot claim counter coverage";
            return false;
        }
        if (checkpoint.payload_checksum != exact_player_hand_health_payload_checksum(checkpoint))
        {
            error = "exact player-hand-health payload checksum does not match";
            return false;
        }
        return true;
    }

    auto parse_exact_player_hand_health_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactPlayerHandHealthCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "exact player-hand-health file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }
        auto values = FlatJsonParser{json}.parse(error);
        if (!values) return std::nullopt;

        ExactPlayerHandHealthCheckpoint checkpoint{};
#define READ_HAND_HEALTH_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_HAND_HEALTH_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_HAND_HEALTH_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version < 1 || checkpoint.schema_version > ExactPlayerHandHealthSchemaVersion)
        {
            error = "unsupported exact player-hand-health schema version";
            return std::nullopt;
        }
        READ_HAND_HEALTH_STRING(kind, "kind");
        READ_HAND_HEALTH_STRING(captured_at_utc, "capturedAtUtc");
        READ_HAND_HEALTH_STRING(route_c_payload_checksum, "routeCPayloadChecksum");
        READ_HAND_HEALTH_STRING(game_executable_sha256, "gameExecutableSha256");
        READ_HAND_HEALTH_INTEGER(game_executable_size, "gameExecutableSize", std::uint64_t);
        READ_HAND_HEALTH_STRING(source_level_name, "sourceLevelName");
        READ_HAND_HEALTH_INTEGER(wave_index, "waveIndex", std::int32_t);
        READ_HAND_HEALTH_STRING(player_hand, "playerHand");
        READ_HAND_HEALTH_STRING(player_hand_health_states, "playerHandHealthStates");
        if (checkpoint.schema_version >= 2)
        {
            READ_HAND_HEALTH_STRING(player_hand_counter_states, "playerHandCounterStates");
        }
        else if (values->contains("playerHandCounterStates"))
        {
            error = "legacy player-hand-health JSON contains a newer counter record";
            return std::nullopt;
        }
        READ_HAND_HEALTH_STRING(payload_checksum, "payloadChecksum");

#undef READ_HAND_HEALTH_INTEGER
#undef READ_HAND_HEALTH_STRING

        if (!validate_exact_player_hand_health_checkpoint(checkpoint, error))
            return std::nullopt;
        return checkpoint;
    }

    auto exact_character_charge_payload_checksum(
        const ExactCharacterChargeCheckpoint& checkpoint) -> std::string
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
        append_hash_number(hash, checkpoint.charge);
        append_hash_number(hash, checkpoint.requirement);

        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_exact_character_charge_checkpoint(ExactCharacterChargeCheckpoint checkpoint)
        -> std::string
    {
        checkpoint.payload_checksum = exact_character_charge_payload_checksum(checkpoint);
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
               << "  \"charge\": " << checkpoint.charge << ",\n"
               << "  \"requirement\": " << checkpoint.requirement << ",\n"
               << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_exact_character_charge_checkpoint(
        const ExactCharacterChargeCheckpoint& checkpoint, std::string& error) -> bool
    {
        if (checkpoint.schema_version != ExactCharacterChargeSchemaVersion)
        {
            error = "unsupported exact character-charge schema version";
            return false;
        }
        if (checkpoint.kind != ExactCharacterChargeCheckpointKind)
        {
            error = "checkpoint kind is not exact character charge";
            return false;
        }
        if (checkpoint.captured_at_utc.empty()
            || !is_hex_digest(checkpoint.route_c_payload_checksum, 16)
            || !is_sha256(checkpoint.game_executable_sha256)
            || checkpoint.game_executable_size == 0)
        {
            error = "exact character charge has invalid linkage or executable data";
            return false;
        }
        if (checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256 || checkpoint.wave_index < 0
            || checkpoint.wave_index > 1000)
        {
            error = "exact character charge has invalid level or wave data";
            return false;
        }
        if (checkpoint.charge < 0 || checkpoint.requirement <= 0
            || checkpoint.charge >= checkpoint.requirement
            || checkpoint.requirement > 10000)
        {
            error = "exact character charge must be below its positive requirement";
            return false;
        }
        if (checkpoint.payload_checksum != exact_character_charge_payload_checksum(checkpoint))
        {
            error = "exact character-charge payload checksum does not match";
            return false;
        }
        return true;
    }

    auto parse_exact_character_charge_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactCharacterChargeCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "exact character-charge file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }
        auto values = FlatJsonParser{json}.parse(error);
        if (!values)
        {
            return std::nullopt;
        }

        ExactCharacterChargeCheckpoint checkpoint{};
#define READ_CHARGE_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_CHARGE_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_CHARGE_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version != ExactCharacterChargeSchemaVersion)
        {
            error = "unsupported exact character-charge schema version";
            return std::nullopt;
        }
        READ_CHARGE_STRING(kind, "kind");
        READ_CHARGE_STRING(captured_at_utc, "capturedAtUtc");
        READ_CHARGE_STRING(route_c_payload_checksum, "routeCPayloadChecksum");
        READ_CHARGE_STRING(game_executable_sha256, "gameExecutableSha256");
        READ_CHARGE_INTEGER(game_executable_size, "gameExecutableSize", std::uint64_t);
        READ_CHARGE_STRING(source_level_name, "sourceLevelName");
        READ_CHARGE_INTEGER(wave_index, "waveIndex", std::int32_t);
        READ_CHARGE_INTEGER(charge, "charge", std::int32_t);
        READ_CHARGE_INTEGER(requirement, "requirement", std::int32_t);
        READ_CHARGE_STRING(payload_checksum, "payloadChecksum");

#undef READ_CHARGE_INTEGER
#undef READ_CHARGE_STRING

        if (!validate_exact_character_charge_checkpoint(checkpoint, error))
        {
            return std::nullopt;
        }
        return checkpoint;
    }

    auto exact_turn_progress_payload_checksum(const ExactTurnProgressCheckpoint& checkpoint)
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
        append_hash_number(hash, checkpoint.card_engine_turn_count);
        append_hash_number(hash, checkpoint.player_draw_delay);
        append_hash_number(hash, checkpoint.wave_alert_counter);
        if (checkpoint.player_can_click_to_draw >= 0)
        {
            append_hash_number(hash, checkpoint.player_can_click_to_draw);
        }
        if (checkpoint.schema_version >= 2)
        {
            append_hash_number(hash, checkpoint.player_draw_base);
            append_hash_number(hash, checkpoint.player_draw_adjustment);
        }

        std::ostringstream output{};
        output << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    auto serialize_exact_turn_progress_checkpoint(ExactTurnProgressCheckpoint checkpoint)
        -> std::string
    {
        checkpoint.payload_checksum = exact_turn_progress_payload_checksum(checkpoint);
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
               << "  \"cardEngineTurnCount\": "
               << checkpoint.card_engine_turn_count << ",\n"
               << "  \"playerDrawDelay\": " << checkpoint.player_draw_delay << ",\n"
               << "  \"waveAlertCounter\": " << checkpoint.wave_alert_counter << ",\n";
        if (checkpoint.player_can_click_to_draw >= 0)
        {
            output << "  \"playerCanClickToDraw\": "
                   << checkpoint.player_can_click_to_draw << ",\n";
        }
        if (checkpoint.schema_version >= 2)
        {
            output << "  \"playerDrawBase\": " << checkpoint.player_draw_base << ",\n"
                   << "  \"playerDrawAdjustment\": " << checkpoint.player_draw_adjustment << ",\n";
        }
        output << "  \"payloadChecksum\": \"" << checkpoint.payload_checksum << "\"\n"
               << "}\n";
        return output.str();
    }

    auto validate_exact_turn_progress_checkpoint(
        const ExactTurnProgressCheckpoint& checkpoint, std::string& error) -> bool
    {
        if (checkpoint.schema_version != 1
            && checkpoint.schema_version != ExactTurnProgressSchemaVersion)
        {
            error = "unsupported exact turn-progress schema version";
            return false;
        }
        if (checkpoint.kind != ExactTurnProgressCheckpointKind)
        {
            error = "checkpoint kind is not exact turn progress";
            return false;
        }
        if (checkpoint.captured_at_utc.empty()
            || !is_hex_digest(checkpoint.route_c_payload_checksum, 16)
            || !is_sha256(checkpoint.game_executable_sha256)
            || checkpoint.game_executable_size == 0)
        {
            error = "exact turn progress has invalid linkage or executable data";
            return false;
        }
        if (checkpoint.source_level_name.empty() || checkpoint.source_level_name == "None"
            || checkpoint.source_level_name.size() > 256 || checkpoint.wave_index < 0
            || checkpoint.wave_index > 1000)
        {
            error = "exact turn progress has invalid level or wave data";
            return false;
        }
        if (checkpoint.card_engine_turn_count < 0
            || checkpoint.card_engine_turn_count > 1'000'000
            || checkpoint.player_draw_delay < 0
            || checkpoint.player_draw_delay > 1'000'000
            || checkpoint.wave_alert_counter < 0
            || checkpoint.wave_alert_counter > 1'000'000
            || checkpoint.player_can_click_to_draw < -1
            || checkpoint.player_can_click_to_draw > 1)
        {
            error = "exact turn-progress value is outside the supported range";
            return false;
        }
        if (checkpoint.payload_checksum != exact_turn_progress_payload_checksum(checkpoint))
        {
            error = "exact turn-progress payload checksum does not match";
            return false;
        }
        if (checkpoint.schema_version >= 2
            && (checkpoint.player_draw_base < 0 || checkpoint.player_draw_base > 100'000
                || checkpoint.player_draw_adjustment < -100'000
                || checkpoint.player_draw_adjustment > 100'000
                || static_cast<std::int64_t>(checkpoint.player_draw_base)
                        + checkpoint.player_draw_adjustment != checkpoint.player_draw_delay))
        {
            error = "native draw components are out of range or disagree with the displayed delay";
            return false;
        }
        return true;
    }

    auto parse_exact_turn_progress_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactTurnProgressCheckpoint>
    {
        if (json.empty() || json.size() > RouteCMaximumFileBytes)
        {
            error = "exact turn-progress file is empty or exceeds the 2 MiB limit";
            return std::nullopt;
        }
        auto values = FlatJsonParser{json}.parse(error);
        if (!values)
        {
            return std::nullopt;
        }

        ExactTurnProgressCheckpoint checkpoint{};
#define READ_TURN_PROGRESS_STRING(Field, JsonName) \
        do { auto value = required_string(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = std::move(*value); } while (false)
#define READ_TURN_PROGRESS_INTEGER(Field, JsonName, Type) \
        do { auto value = required_integer<Type>(*values, JsonName, error); if (!value) return std::nullopt; checkpoint.Field = *value; } while (false)

        READ_TURN_PROGRESS_INTEGER(schema_version, "schemaVersion", int);
        if (checkpoint.schema_version != 1
            && checkpoint.schema_version != ExactTurnProgressSchemaVersion)
        {
            error = "unsupported exact turn-progress schema version";
            return std::nullopt;
        }
        READ_TURN_PROGRESS_STRING(kind, "kind");
        READ_TURN_PROGRESS_STRING(captured_at_utc, "capturedAtUtc");
        READ_TURN_PROGRESS_STRING(route_c_payload_checksum, "routeCPayloadChecksum");
        READ_TURN_PROGRESS_STRING(game_executable_sha256, "gameExecutableSha256");
        READ_TURN_PROGRESS_INTEGER(game_executable_size, "gameExecutableSize", std::uint64_t);
        READ_TURN_PROGRESS_STRING(source_level_name, "sourceLevelName");
        READ_TURN_PROGRESS_INTEGER(wave_index, "waveIndex", std::int32_t);
        READ_TURN_PROGRESS_INTEGER(
            card_engine_turn_count, "cardEngineTurnCount", std::int32_t);
        READ_TURN_PROGRESS_INTEGER(player_draw_delay, "playerDrawDelay", std::int32_t);
        READ_TURN_PROGRESS_INTEGER(
            wave_alert_counter, "waveAlertCounter", std::int32_t);
        if (values->contains("playerCanClickToDraw"))
        {
            READ_TURN_PROGRESS_INTEGER(
                player_can_click_to_draw,
                "playerCanClickToDraw",
                std::int32_t);
        }
        if (checkpoint.schema_version >= 2)
        {
            READ_TURN_PROGRESS_INTEGER(player_draw_base, "playerDrawBase", std::int32_t);
            READ_TURN_PROGRESS_INTEGER(player_draw_adjustment, "playerDrawAdjustment", std::int32_t);
        }
        READ_TURN_PROGRESS_STRING(payload_checksum, "payloadChecksum");

#undef READ_TURN_PROGRESS_INTEGER
#undef READ_TURN_PROGRESS_STRING

        if (!validate_exact_turn_progress_checkpoint(checkpoint, error))
        {
            return std::nullopt;
        }
        return checkpoint;
    }
} // namespace QuantumCheckpoint
