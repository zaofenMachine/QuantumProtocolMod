#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace QuantumCheckpoint
{
    inline constexpr int SchemaVersion = 5;
    inline constexpr int RouteCSchemaVersion = 2;
    inline constexpr std::string_view RouteCCheckpointKind = "route-c-substage-restart";
    inline constexpr std::string_view RouteCSupportedMode = "DUNGEON";
    inline constexpr std::size_t RouteCMaximumFileBytes = 2U * 1024U * 1024U;

    struct PropertySnapshot
    {
        std::string name{};
        std::string value{};
    };

    struct ObjectSnapshot
    {
        std::string role{};
        std::string full_name{};
        std::vector<PropertySnapshot> properties{};
    };

    struct BattleInventory
    {
        int schema_version{SchemaVersion};
        std::string captured_at_utc{};
        std::vector<ObjectSnapshot> objects{};
    };

    // Route C intentionally stores a semantic substage restart, not a dump of
    // live Unreal objects. The Unreal text fields are named and bounded pieces
    // of data imported through their reflected property types during restore.
    struct RouteCCheckpoint
    {
        int schema_version{RouteCSchemaVersion};
        std::string kind{RouteCCheckpointKind};
        std::string captured_at_utc{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string mode{RouteCSupportedMode};
        std::string source_level_name{};
        std::string active_character_info{};
        std::string active_stage_info{};
        std::string active_decklist{};
        std::string active_storage{};
        std::string loot_drops{};
        std::string deck_run{};
        std::int32_t player_health{};
        std::int32_t player_max_health{};
        std::int32_t wave_index{};
        std::string spawner_class{};
        std::uint32_t spawner_class_size{};
        std::string payload_checksum{};
    };
} // namespace QuantumCheckpoint
