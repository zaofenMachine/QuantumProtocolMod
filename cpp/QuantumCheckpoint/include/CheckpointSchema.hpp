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
    inline constexpr int ExactSpawnPlanSchemaVersion = 1;
    inline constexpr std::string_view ExactSpawnPlanCheckpointKind =
        "route-c-exact-spawn-plan";
    // These fixed minimum versions attest that capture checked ordered native
    // effect membership against each player's selected CardInfo upgrade effects
    // across DECK/HAND/TRASH/FIELD. They do not attest effect private state/history.
    // Keep them independent of later increases to the supported schema maxima.
    inline constexpr int ExactPlayerZonesEffectMembershipSchemaVersion = 4;
    inline constexpr int ExactPlayerTrashEffectMembershipSchemaVersion = 5;
    inline constexpr int ExactPlayerFieldEffectMembershipSchemaVersion = 8;
    // Fixed minima for native-index DECK/TRASH base-HP records in the selected
    // layout. Older schemas retain their default-only off-field numeric policy.
    inline constexpr int ExactPlayerZonesBaseHealthSchemaVersion = 5;
    inline constexpr int ExactPlayerTrashBaseHealthSchemaVersion = 6;
    inline constexpr int ExactPlayerFieldBaseHealthSchemaVersion = 9;
    // These fixed minima require a checksummed Q1 qualification dependency marker.
    // Do not move the historical membership/base-health minima above.
    inline constexpr int ExactPlayerZonesQualificationSchemaVersion = 6;
    inline constexpr int ExactPlayerTrashQualificationSchemaVersion = 7;
    inline constexpr int ExactPlayerFieldQualificationSchemaVersion = 10;
    // Phase39 A remains schema1. Phase40 B has an independent policy/schema;
    // never change the meaning or checksum order of an existing A record.
    inline constexpr int ExactPlayerQualificationBaselineSchemaVersion = 1;
    inline constexpr int ExactPlayerQualificationBowOnceSchemaVersion = 2;
    inline constexpr int ExactPlayerZonesBowOnceQualificationSchemaVersion = 7;
    inline constexpr int ExactPlayerTrashBowOnceQualificationSchemaVersion = 8;
    inline constexpr int ExactPlayerFieldBowOnceQualificationSchemaVersion = 11;
    // F12 adds one base Bow in PLAYER FIELD; Q2's effect contract is unchanged.
    inline constexpr int ExactPlayerFieldBowPlacementSchemaVersion = 12;
    // F13 adds exactly two base Bows, both in FIELD, under the same Q2 policy.
    inline constexpr int ExactPlayerFieldBowPairPlacementSchemaVersion = 13;
    inline constexpr int ExactPlayerQualificationSchemaVersion = 2;
    inline constexpr std::string_view ExactPlayerQualificationCheckpointKind =
        "route-c-exact-player-qualification";
    inline constexpr std::string_view ExactPlayerQualificationPolicy = "flags-baseline-v1";
    inline constexpr std::string_view ExactPlayerQualificationBowOncePolicy =
        "flags-baseline-bow-once-v2";
    inline constexpr std::size_t ExactPlayerQualificationMaximumCards = 128;
    inline constexpr std::size_t ExactPlayerQualificationMaximumEffectsPerCard = 64;
    inline constexpr int ExactPlayerZonesSchemaVersion = 7;
    inline constexpr std::string_view ExactPlayerZonesCheckpointKind =
        "route-c-exact-player-zones";
    inline constexpr int ExactPlayerTrashSchemaVersion = 8;
    inline constexpr std::string_view ExactPlayerTrashCheckpointKind =
        "route-c-exact-player-trash";
    inline constexpr int ExactPlayerFieldSchemaVersion = 13;
    inline constexpr std::string_view ExactPlayerFieldCheckpointKind =
        "route-c-exact-player-field";
    // Fixed minimum for replaying HAND base-HP gains; keep older schemas default-only.
    inline constexpr int ExactPlayerHandBaseHealthSchemaVersion = 3;
    inline constexpr int ExactPlayerHandHealthSchemaVersion = 3;
    inline constexpr std::string_view ExactPlayerHandHealthCheckpointKind =
        "route-c-exact-player-hand-health";
    // Schema1 records numeric charge only; it cannot attest saved allowance.
    inline constexpr int ExactCharacterChargeLegacySchemaVersion = 1;
    inline constexpr int ExactCharacterChargeAllowanceSchemaVersion = 2;
    inline constexpr int ExactCharacterChargeObservedAllowanceSchemaVersion = 3;
    inline constexpr int ExactCharacterChargeSchemaVersion = 3;
    inline constexpr std::string_view ExactCharacterChargeCheckpointKind =
        "route-c-exact-character-charge";
    inline constexpr std::string_view ExactCharacterAllowancePolicy = "observed-disallowed-v1";
    inline constexpr std::string_view ExactCharacterObservedAllowancePolicy = "observed-allowance-match-v1";
    inline constexpr int ExactTurnProgressSchemaVersion = 2;
    inline constexpr std::string_view ExactTurnProgressCheckpointKind =
        "route-c-exact-turn-progress";
    inline constexpr std::string_view RouteCSupportedMode = "DUNGEON";
    inline constexpr std::size_t RouteCMaximumFileBytes = 2U * 1024U * 1024U;

    constexpr auto supports_plain_player_field_card(std::string_view tag) -> bool
    {
        return tag == "naturalApple" || tag == "naturalLemon" || tag == "naturalSpring";
    }

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

    // The exact layer is deliberately separate from the proven Route C payload.
    // A missing, stale, or rejected supplement must never invalidate the semantic
    // substage checkpoint it references.
    struct ExactSpawnPlanCheckpoint
    {
        int schema_version{ExactSpawnPlanSchemaVersion};
        std::string kind{ExactSpawnPlanCheckpointKind};
        std::string captured_at_utc{};
        std::string route_c_payload_checksum{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string source_level_name{};
        std::int32_t wave_index{};
        std::string spawner_class{};
        std::uint32_t spawner_class_size{};
        std::string spawn_list{};
        std::string payload_checksum{};
    };

    // Player-zone schema 2 stores native array order: deck back is the next draw,
    // hand is left-to-right, and trash is insertion order. Schema 1 contains only
    // metadata-sorted controller output and cannot recover these original orders.
    struct ExactPlayerZonesCheckpoint
    {
        int schema_version{ExactPlayerZonesSchemaVersion};
        std::string kind{ExactPlayerZonesCheckpointKind};
        std::string captured_at_utc{};
        std::string route_c_payload_checksum{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string source_level_name{};
        std::int32_t wave_index{};
        std::string player_deck{};
        std::string player_hand{};
        // Latest schemas require the linked hand-health payload for nonempty HAND.
        std::string player_hand_health_checksum{};
        // One base=max, modifier-free HEALTH record per native DECK index.
        // An empty zone has an empty record string. Legacy schemas leave it empty.
        std::string player_deck_base_health_states{};
        // Empty by default: capture must explicitly attest the reviewed policy.
        std::string qualification_policy{};
        std::string payload_checksum{};
    };

    // This remains separate from ExactPlayerZonesCheckpoint so the proven clean
    // Deck/Hand startup path stays compatible. It is emitted only when the player
    // trash is non-empty and no player FIELD/PENDING card requires reconstruction.
    struct ExactPlayerTrashCheckpoint
    {
        int schema_version{ExactPlayerTrashSchemaVersion};
        std::string kind{ExactPlayerTrashCheckpointKind};
        std::string captured_at_utc{};
        std::string route_c_payload_checksum{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string source_level_name{};
        std::int32_t wave_index{};
        std::string player_deck{};
        std::string player_hand{};
        std::string player_trash{};
        std::string player_hand_health_checksum{};
        std::string player_deck_base_health_states{};
        std::string player_trash_base_health_states{};
        std::string qualification_policy{};
        std::string payload_checksum{};
    };

    struct ExactPlayerFieldCardState
    {
        std::int32_t row{};
        std::int32_t index{};
        std::int32_t current_health{};
        bool turn_active{};
    };

    // This supersedes both clean player-zone supplements when a supported player
    // FIELD is present. Card instances and compact field-state records are aligned
    // by deterministic row/index order.
    struct ExactPlayerFieldCheckpoint
    {
        // Capture explicitly selects F12/F13 for their distinct Bow FIELD slices.
        int schema_version{ExactPlayerFieldBowOnceQualificationSchemaVersion};
        std::string kind{ExactPlayerFieldCheckpointKind};
        std::string captured_at_utc{};
        std::string route_c_payload_checksum{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string source_level_name{};
        std::int32_t wave_index{};
        std::string player_deck{};
        std::string player_hand{};
        std::string player_trash{};
        std::string player_field{};
        std::string player_field_states{};
        std::string player_field_attack_states{};
        std::string player_field_health_states{};
        std::string player_field_counter_states{};
        std::string player_hand_health_checksum{};
        std::string player_deck_base_health_states{};
        std::string player_trash_base_health_states{};
        std::string qualification_policy{};
        std::string payload_checksum{};
    };

    struct ExactPlayerQualificationEffect
    {
        std::int32_t ordinal{};
        std::string factory_key{};
        std::string tag{};
        std::int32_t type{};
        std::vector<std::uint8_t> flags{};
        std::int32_t used_once{};
        std::string profile_id{};
        auto operator==(const ExactPlayerQualificationEffect&) const -> bool = default;
    };

    struct ExactPlayerQualificationCard
    {
        // Native locations: HAND=0, DECK=1, TRASH=2, FIELD=3. Records are in this
        // order; each D/H/T index is contiguous. FIELD uses row/index order.
        std::int32_t native_location{};
        std::int32_t native_index{-1};
        std::int32_t row{-1};
        std::int32_t index{-1};
        std::string card_info_instance{};
        std::vector<ExactPlayerQualificationEffect> effects{};
        auto operator==(const ExactPlayerQualificationCard&) const -> bool = default;
    };

    // Portable whole-player proof. Actor/effect GUIDs and addresses are excluded.
    // The selected layout hashes its required policy; Q1 hashes that layout's
    // checksum, so the dependency is cycle-free.
    struct ExactPlayerQualificationCheckpoint
    {
        int schema_version{ExactPlayerQualificationSchemaVersion};
        std::string kind{ExactPlayerQualificationCheckpointKind};
        std::string captured_at_utc{};
        std::string route_c_payload_checksum{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string source_level_name{};
        std::int32_t wave_index{};
        std::string layout_kind{};
        std::string layout_payload_checksum{};
        std::string qualification_policy{};
        std::vector<ExactPlayerQualificationCard> cards{};
        std::string payload_checksum{};
    };

    // Each health record belongs to the same native hand-array index, including
    // duplicate card identities. This slice has only nonnegative HEALTH modifiers;
    // capture and runtime verification require current health to equal the saved
    // maximum. The existing positive base/max health bounds remain in force.
    // Schema 2 also preserves one bounded generic-counter record per native hand
    // index; special counters are outside this slice. Schema 1 leaves it empty.
    // Schema 3 reuses base_health to preserve nonnegative base-HP gains relative
    // to the selected immutable definition. Schemas 1/2 require definition base HP.
    struct ExactPlayerHandHealthCheckpoint
    {
        int schema_version{ExactPlayerHandHealthSchemaVersion};
        std::string kind{ExactPlayerHandHealthCheckpointKind};
        std::string captured_at_utc{};
        std::string route_c_payload_checksum{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string source_level_name{};
        std::int32_t wave_index{};
        std::string player_hand{};
        std::string player_hand_health_states{};
        std::string player_hand_counter_states{};
        std::string payload_checksum{};
    };

    struct ExactCharacterChargeCheckpoint
    {
        int schema_version{ExactCharacterChargeSchemaVersion};
        std::string kind{ExactCharacterChargeCheckpointKind};
        std::string captured_at_utc{};
        std::string route_c_payload_checksum{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string source_level_name{};
        std::int32_t wave_index{};
        std::int32_t charge{};
        std::int32_t requirement{};
        // Empty is unknown/unobserved, never a default False source proof.
        std::string ability_allowance{};
        std::string allowance_policy{};
        std::string payload_checksum{};
    };

    struct ExactTurnProgressCheckpoint
    {
        int schema_version{ExactTurnProgressSchemaVersion};
        std::string kind{ExactTurnProgressCheckpointKind};
        std::string captured_at_utc{};
        std::string route_c_payload_checksum{};
        std::string game_executable_sha256{};
        std::uint64_t game_executable_size{};
        std::string source_level_name{};
        std::int32_t wave_index{};
        std::int32_t card_engine_turn_count{};
        std::int32_t player_draw_delay{};
        std::int32_t wave_alert_counter{};
        // -1 is the legacy schema-1 sentinel. New captures persist 0/1 while
        // remaining checksum-compatible with existing schema-1 supplements.
        std::int32_t player_can_click_to_draw{-1};
        // Schema 2 preserves both inputs. The displayed sum alone cannot identify
        // draw readiness: the native predicate checks only player_draw_base == 0.
        std::int32_t player_draw_base{};
        std::int32_t player_draw_adjustment{};
        std::string payload_checksum{};
    };
} // namespace QuantumCheckpoint
