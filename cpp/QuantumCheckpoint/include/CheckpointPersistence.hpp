#pragma once

#include "CheckpointSchema.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace QuantumCheckpoint
{
    auto route_c_payload_checksum(const RouteCCheckpoint& checkpoint) -> std::string;
    auto serialize_route_c_checkpoint(RouteCCheckpoint checkpoint) -> std::string;
    auto parse_route_c_checkpoint(std::string_view json, std::string& error)
        -> std::optional<RouteCCheckpoint>;
    auto validate_route_c_checkpoint(const RouteCCheckpoint& checkpoint, std::string& error)
        -> bool;
} // namespace QuantumCheckpoint
