#include "CheckpointPersistence.hpp"

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

        auto is_sha256(std::string_view value) -> bool
        {
            if (value.size() != 64)
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
    } // namespace

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
            || checkpoint.deck_run.empty())
        {
            error = "checkpoint is missing required reflected run data";
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
} // namespace QuantumCheckpoint
