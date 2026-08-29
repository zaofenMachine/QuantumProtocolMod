#pragma once

#include <string>
#include <vector>

namespace QuantumCheckpoint
{
    inline constexpr int SchemaVersion = 1;

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
} // namespace QuantumCheckpoint
