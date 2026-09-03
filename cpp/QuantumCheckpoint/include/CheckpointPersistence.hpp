#pragma once

#include "CheckpointSchema.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace QuantumCheckpoint
{
    auto route_c_payload_checksum(const RouteCCheckpoint& checkpoint) -> std::string;
    auto serialize_route_c_checkpoint(RouteCCheckpoint checkpoint) -> std::string;
    auto parse_route_c_checkpoint(std::string_view json, std::string& error)
        -> std::optional<RouteCCheckpoint>;
    auto validate_route_c_checkpoint(const RouteCCheckpoint& checkpoint, std::string& error)
        -> bool;
    auto route_c_startup_decklist(std::string_view active_decklist) -> std::string;
    auto split_route_c_unreal_array(std::string_view value, std::string& error)
        -> std::optional<std::vector<std::string>>;
    auto exact_spawn_plan_payload_checksum(const ExactSpawnPlanCheckpoint& checkpoint)
        -> std::string;
    auto serialize_exact_spawn_plan_checkpoint(ExactSpawnPlanCheckpoint checkpoint)
        -> std::string;
    auto parse_exact_spawn_plan_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactSpawnPlanCheckpoint>;
    auto validate_exact_spawn_plan_checkpoint(const ExactSpawnPlanCheckpoint& checkpoint,
                                              std::string& error) -> bool;
} // namespace QuantumCheckpoint
