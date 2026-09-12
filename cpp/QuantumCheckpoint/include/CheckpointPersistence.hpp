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
    auto exact_player_zones_payload_checksum(const ExactPlayerZonesCheckpoint& checkpoint)
        -> std::string;
    auto serialize_exact_player_zones_checkpoint(ExactPlayerZonesCheckpoint checkpoint)
        -> std::string;
    auto parse_exact_player_zones_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactPlayerZonesCheckpoint>;
    auto validate_exact_player_zones_checkpoint(const ExactPlayerZonesCheckpoint& checkpoint,
                                                std::string& error) -> bool;
    auto exact_player_zones_startup_decklist(std::string_view active_decklist,
                                             std::string_view player_deck,
                                             std::string_view player_hand,
                                             std::string& error)
        -> std::optional<std::string>;
    auto exact_player_trash_payload_checksum(const ExactPlayerTrashCheckpoint& checkpoint)
        -> std::string;
    auto serialize_exact_player_trash_checkpoint(ExactPlayerTrashCheckpoint checkpoint)
        -> std::string;
    auto parse_exact_player_trash_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactPlayerTrashCheckpoint>;
    auto validate_exact_player_trash_checkpoint(const ExactPlayerTrashCheckpoint& checkpoint,
                                                std::string& error) -> bool;
    auto exact_player_trash_startup_decklist(std::string_view active_decklist,
                                             std::string_view player_deck,
                                             std::string_view player_hand,
                                             std::string_view player_trash,
                                             std::string& error)
        -> std::optional<std::string>;
    auto exact_player_trash_staging_matches(std::string_view expected_deck,
                                            std::string_view expected_hand,
                                            std::string_view expected_trash,
                                            std::string_view live_deck,
                                            std::string_view live_hand,
                                            std::string_view live_trash,
                                            std::string& error) -> bool;
    auto exact_player_field_payload_checksum(const ExactPlayerFieldCheckpoint& checkpoint)
        -> std::string;
    auto serialize_exact_player_field_checkpoint(ExactPlayerFieldCheckpoint checkpoint)
        -> std::string;
    auto parse_exact_player_field_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactPlayerFieldCheckpoint>;
    auto validate_exact_player_field_checkpoint(
        const ExactPlayerFieldCheckpoint& checkpoint, std::string& error) -> bool;
    auto exact_player_field_startup_decklist(std::string_view active_decklist,
                                             std::string_view player_deck,
                                             std::string_view player_hand,
                                             std::string_view player_trash,
                                             std::string_view player_field,
                                             std::string& error)
        -> std::optional<std::string>;
    auto exact_player_field_staging_matches(std::string_view expected_deck,
                                            std::string_view expected_hand,
                                            std::string_view expected_trash,
                                            std::string_view expected_field,
                                            std::string_view live_deck,
                                            std::string_view live_hand,
                                            std::string_view live_trash,
                                            std::string& error) -> bool;
    auto parse_exact_player_field_states(std::string_view value, std::string& error)
        -> std::optional<std::vector<ExactPlayerFieldCardState>>;
    auto exact_card_identity_key_from_instance(std::string_view card_instance,
                                               std::string& error)
        -> std::optional<std::string>;
    auto exact_character_charge_payload_checksum(
        const ExactCharacterChargeCheckpoint& checkpoint) -> std::string;
    auto serialize_exact_character_charge_checkpoint(ExactCharacterChargeCheckpoint checkpoint)
        -> std::string;
    auto parse_exact_character_charge_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactCharacterChargeCheckpoint>;
    auto validate_exact_character_charge_checkpoint(
        const ExactCharacterChargeCheckpoint& checkpoint, std::string& error) -> bool;
    auto exact_turn_progress_payload_checksum(const ExactTurnProgressCheckpoint& checkpoint)
        -> std::string;
    auto serialize_exact_turn_progress_checkpoint(ExactTurnProgressCheckpoint checkpoint)
        -> std::string;
    auto parse_exact_turn_progress_checkpoint(std::string_view json, std::string& error)
        -> std::optional<ExactTurnProgressCheckpoint>;
    auto validate_exact_turn_progress_checkpoint(
        const ExactTurnProgressCheckpoint& checkpoint, std::string& error) -> bool;
} // namespace QuantumCheckpoint
