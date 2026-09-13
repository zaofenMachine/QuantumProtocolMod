#include "CheckpointPersistence.hpp"
#include "PlayerRestorePlan.hpp"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <string>

namespace
{
    auto sample_checkpoint() -> QuantumCheckpoint::RouteCCheckpoint
    {
        return {
            .captured_at_utc = "2026-08-31T14:00:00Z",
            .game_executable_sha256 =
                "0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6",
            .game_executable_size = 82'718'720,
            .source_level_name = "testDungeon",
            .active_character_info = "(Tag=\"esper\")",
            .active_stage_info = "(DisplayName=INVTEXT(\"测试\"),Type=DUNGEON)",
            .active_decklist = "(deckTag=\"esperDungeon1\",cardList=((cardName=\"a\",count=2)))",
            .active_storage = "((CardInfo=(Tag=\"a\")))",
            .loot_drops =
                "((ID=(A=1,B=2,C=3,D=4),rarity=1,CardInfo=(Tag=\"loot,a\")),"
                "(ID=(A=5,B=6,C=7,D=8),rarity=2,CardInfo=(Tag=\"loot(b)\")))",
            .deck_run = "(characterTag=\"esper\",deck=((cardTag=\"a\",count=2)))",
            .player_health = 7,
            .player_max_health = 9,
            .wave_index = 3,
            .spawner_class = "BP_CrossSpawner_C",
            .spawner_class_size = 0x280,
        };
    }

    auto require(bool condition, const char* message) -> void
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(1);
        }
    }

    auto sample_exact_spawn_plan(const QuantumCheckpoint::RouteCCheckpoint& route_c)
        -> QuantumCheckpoint::ExactSpawnPlanCheckpoint
    {
        return {
            .captured_at_utc = route_c.captured_at_utc,
            .route_c_payload_checksum = route_c.payload_checksum,
            .game_executable_sha256 = route_c.game_executable_sha256,
            .game_executable_size = route_c.game_executable_size,
            .source_level_name = route_c.source_level_name,
            .wave_index = 1,
            .spawner_class = route_c.spawner_class,
            .spawner_class_size = route_c.spawner_class_size,
            .spawn_list =
                "((cardsToSpawn=((cardName=\"enemy,a\",placement=(Index=1)))),"
                "(cardsToSpawn=((cardName=\"enemy(b)\",placement=(Index=2)))))",
        };
    }

    auto sample_exact_player_zones(const QuantumCheckpoint::RouteCCheckpoint& route_c)
        -> QuantumCheckpoint::ExactPlayerZonesCheckpoint
    {
        return {
            .captured_at_utc = route_c.captured_at_utc,
            .route_c_payload_checksum = route_c.payload_checksum,
            .game_executable_sha256 = route_c.game_executable_sha256,
            .game_executable_size = route_c.game_executable_size,
            .source_level_name = route_c.source_level_name,
            .wave_index = route_c.wave_index,
            .player_deck = "((CardInfo=(Tag=\"a\")))",
            .player_hand = "((CardInfo=(Tag=\"a\")))",
        };
    }

    auto sample_exact_player_trash(const QuantumCheckpoint::RouteCCheckpoint& route_c)
        -> QuantumCheckpoint::ExactPlayerTrashCheckpoint
    {
        return {
            .captured_at_utc = route_c.captured_at_utc,
            .route_c_payload_checksum = route_c.payload_checksum,
            .game_executable_sha256 = route_c.game_executable_sha256,
            .game_executable_size = route_c.game_executable_size,
            .source_level_name = route_c.source_level_name,
            .wave_index = route_c.wave_index,
            .player_deck = "((CardInfo=(Tag=\"a\")))",
            .player_hand = "((CardInfo=(Tag=\"b\"),upgradeLevel=1))",
            .player_trash = "((CardInfo=(Tag=\"a\")))",
        };
    }

    auto sample_exact_player_field(const QuantumCheckpoint::RouteCCheckpoint& route_c)
        -> QuantumCheckpoint::ExactPlayerFieldCheckpoint
    {
        return {
            .schema_version = 2,
            .captured_at_utc = route_c.captured_at_utc,
            .route_c_payload_checksum = route_c.payload_checksum,
            .game_executable_sha256 = route_c.game_executable_sha256,
            .game_executable_size = route_c.game_executable_size,
            .source_level_name = route_c.source_level_name,
            .wave_index = route_c.wave_index,
            .player_deck = "((CardInfo=(Tag=\"a\")))",
            .player_hand = "((CardInfo=(Tag=\"b\")))",
            .player_trash = "((CardInfo=(Tag=\"c\")))",
            .player_field =
                "((CardInfo=(Tag=\"naturalApple\")),"
                "(CardInfo=(Tag=\"naturalLemon\")))",
            .player_field_states = "0,0,1,0;1,3,2,1",
        };
    }

    auto sample_exact_character_charge(const QuantumCheckpoint::RouteCCheckpoint& route_c)
        -> QuantumCheckpoint::ExactCharacterChargeCheckpoint
    {
        return {
            .captured_at_utc = route_c.captured_at_utc,
            .route_c_payload_checksum = route_c.payload_checksum,
            .game_executable_sha256 = route_c.game_executable_sha256,
            .game_executable_size = route_c.game_executable_size,
            .source_level_name = route_c.source_level_name,
            .wave_index = route_c.wave_index,
            .charge = 4,
            .requirement = 6,
        };
    }

    auto sample_exact_turn_progress(const QuantumCheckpoint::RouteCCheckpoint& route_c)
        -> QuantumCheckpoint::ExactTurnProgressCheckpoint
    {
        return {
            .captured_at_utc = route_c.captured_at_utc,
            .route_c_payload_checksum = route_c.payload_checksum,
            .game_executable_sha256 = route_c.game_executable_sha256,
            .game_executable_size = route_c.game_executable_size,
            .source_level_name = route_c.source_level_name,
            .wave_index = route_c.wave_index,
            .card_engine_turn_count = 5,
            .player_draw_delay = 4,
            .wave_alert_counter = 5,
            .player_can_click_to_draw = 1,
            .player_draw_base = 4,
            .player_draw_adjustment = 0,
        };
    }
}

int main()
{
    using namespace QuantumCheckpoint;

    auto original = sample_checkpoint();
    const auto json = serialize_route_c_checkpoint(original);
    std::string error{};
    const auto parsed = parse_route_c_checkpoint(json, error);
    require(parsed.has_value(), error.c_str());
    require(parsed->source_level_name == original.source_level_name, "source level round trip");
    require(parsed->active_stage_info == original.active_stage_info, "UTF-8 and quotes round trip");
    require(parsed->active_decklist == original.active_decklist, "deck round trip");
    require(parsed->loot_drops == original.loot_drops, "loot drops round trip");
    require(parsed->wave_index == original.wave_index, "wave round trip");
    require(parsed->payload_checksum == route_c_payload_checksum(*parsed), "checksum round trip");

    auto empty_storage = sample_checkpoint();
    empty_storage.active_storage = "()";
    error.clear();
    const auto parsed_empty_storage = parse_route_c_checkpoint(
        serialize_route_c_checkpoint(empty_storage), error);
    require(parsed_empty_storage.has_value(), "empty storage array is accepted");
    require(parsed_empty_storage->active_storage == "()", "empty storage array round trip");

    const std::string runtime_deck =
        "(deckTag=\"esperDungeon1\",cardList=((cardName=\"spDeckEdit\",count=1),"
        "(cardName=\"spStorageHit\",count=1)),dungeonTools=((cardName=\"spDeckEdit\","
        "count=1),(cardName=\"spStorageHit\",count=1)),infiniteOk=True)";
    const std::string expected_startup_deck =
        "(deckTag=\"esperDungeon1\",cardList=((cardName=\"spDeckEdit\",count=1),"
        "(cardName=\"spStorageHit\",count=1)),dungeonTools=(),infiniteOk=True)";
    require(route_c_startup_decklist(runtime_deck) == expected_startup_deck,
            "dungeon tool recipe is suppressed during native startup");
    require(route_c_startup_decklist(expected_startup_deck) == expected_startup_deck,
            "empty dungeon tool recipe remains stable");
    const std::string deck_without_tools =
        "(deckTag=\"custom\",cardList=((cardName=\"quoted(\\\")card\",count=1)))";
    require(route_c_startup_decklist(deck_without_tools) == deck_without_tools,
            "deck without dungeon tools remains unchanged");

    error.clear();
    const auto empty_loot = split_route_c_unreal_array("()", error);
    require(empty_loot.has_value() && empty_loot->empty(), "empty loot array is accepted");
    error.clear();
    const auto split_loot = split_route_c_unreal_array(original.loot_drops, error);
    require(split_loot.has_value() && split_loot->size() == 2,
            "nested loot structs are split into two elements");
    require((*split_loot)[0].find("Tag=\"loot,a\"") != std::string::npos,
            "quoted comma remains in the first loot element");
    require((*split_loot)[1].find("Tag=\"loot(b)\"") != std::string::npos,
            "quoted parentheses remain in the second loot element");
    error.clear();
    require(!split_route_c_unreal_array("((ID=(A=1)),)", error),
            "trailing empty loot element is rejected");
    error.clear();
    require(!split_route_c_unreal_array("((ID=(A=1))", error),
            "unbalanced loot array is rejected");

    auto corrupted = json;
    const auto health = corrupted.find("\"playerHealth\": 7");
    require(health != std::string::npos, "test fixture contains health");
    corrupted.replace(health, std::string{"\"playerHealth\": 7"}.size(), "\"playerHealth\": 6");
    error.clear();
    require(!parse_route_c_checkpoint(corrupted, error), "corrupted payload is rejected");
    require(error.find("checksum") != std::string::npos, "corruption reports checksum error");

    auto legacy = sample_checkpoint();
    legacy.schema_version = 1;
    error.clear();
    require(!parse_route_c_checkpoint(serialize_route_c_checkpoint(legacy), error),
            "legacy schema is rejected");
    require(error.find("schema version") != std::string::npos,
            "legacy schema reports a version error before missing fields");

    auto unsupported = sample_checkpoint();
    unsupported.mode = "INFINITE";
    error.clear();
    require(!parse_route_c_checkpoint(serialize_route_c_checkpoint(unsupported), error),
            "unsupported mode is rejected");

    auto stateful_spawner = sample_checkpoint();
    stateful_spawner.spawner_class_size = 0x289;
    error.clear();
    require(!parse_route_c_checkpoint(serialize_route_c_checkpoint(stateful_spawner), error),
            "stateful spawner is rejected");

    original.payload_checksum = route_c_payload_checksum(original);
    auto exact = sample_exact_spawn_plan(original);
    const auto exact_json = serialize_exact_spawn_plan_checkpoint(exact);
    error.clear();
    const auto parsed_exact = parse_exact_spawn_plan_checkpoint(exact_json, error);
    require(parsed_exact.has_value(), error.c_str());
    require(parsed_exact->route_c_payload_checksum == original.payload_checksum,
            "exact spawn plan preserves Route C linkage");
    require(parsed_exact->spawn_list == exact.spawn_list, "exact spawn list round trip");
    require(parsed_exact->payload_checksum == exact_spawn_plan_payload_checksum(*parsed_exact),
            "exact spawn-plan checksum round trip");

    auto missing_saved_wave = exact;
    missing_saved_wave.wave_index = 2;
    missing_saved_wave.payload_checksum = exact_spawn_plan_payload_checksum(missing_saved_wave);
    error.clear();
    require(!parse_exact_spawn_plan_checkpoint(
                serialize_exact_spawn_plan_checkpoint(missing_saved_wave), error),
            "exact spawn list must contain the saved wave");

    auto corrupt_exact = exact_json;
    const auto first_enemy = corrupt_exact.find("enemy,a");
    require(first_enemy != std::string::npos, "exact fixture contains first enemy");
    corrupt_exact.replace(first_enemy, std::string{"enemy,a"}.size(), "enemy,z");
    error.clear();
    require(!parse_exact_spawn_plan_checkpoint(corrupt_exact, error),
            "corrupted exact spawn plan is rejected");
    require(error.find("checksum") != std::string::npos,
            "exact spawn-plan corruption reports checksum error");

    auto zones_route = original;
    zones_route.active_decklist =
        "(deckTag=\"fixed-test\",cardList=((cardName=\"a\",count=2)))";
    zones_route.payload_checksum = route_c_payload_checksum(zones_route);
    auto zones = sample_exact_player_zones(zones_route);
    const auto zones_json = serialize_exact_player_zones_checkpoint(zones);
    error.clear();
    const auto parsed_zones = parse_exact_player_zones_checkpoint(zones_json, error);
    require(parsed_zones.has_value(), error.c_str());
    require(parsed_zones->player_deck == zones.player_deck, "exact player deck round trip");
    require(parsed_zones->player_hand == zones.player_hand, "exact player hand round trip");
    require(parsed_zones->payload_checksum == exact_player_zones_payload_checksum(*parsed_zones),
            "exact player-zones checksum round trip");
    auto legacy_zones = zones;
    legacy_zones.schema_version = 1;
    const auto parsed_legacy_zones = parse_exact_player_zones_checkpoint(
        serialize_exact_player_zones_checkpoint(legacy_zones), error);
    require(parsed_zones->schema_version == 2 && parsed_legacy_zones
                && parsed_legacy_zones->schema_version == 1,
            "native and legacy-sorted zone formats remain distinguishable");
    auto mislabeled_order = zones;
    mislabeled_order.schema_version = 1;
    require(!validate_exact_player_zones_checkpoint(mislabeled_order, error),
            "changing an order schema without updating its checksum is rejected");

    error.clear();
    const auto fixed_startup = exact_player_zones_startup_decklist(
        zones_route.active_decklist, zones.player_deck, zones.player_hand, error);
    require(fixed_startup.has_value(), error.c_str());
    require(*fixed_startup
                == "(deckTag=\"fixed-test\",cardList=((cardName=\"a\",count=1),"
                   "(cardName=\"a\",count=1)),fixedOrder=True)",
            "exact player zones create an expanded fixed-order startup deck");

    const std::string ordered_active =
        "(deckTag=\"ordered\",cardList=((cardName=\"a\",count=2),"
        "(cardName=\"b\",count=1,upgradeLevel=1)),fixedOrder=False,"
        "dungeonTools=((cardName=\"tool\",count=1)))";
    const std::string ordered_deck = "((CardInfo=(Tag=\"a\")))";
    const std::string ordered_hand =
        "((CardInfo=(Tag=\"b\"),upgradeLevel=1),(CardInfo=(Tag=\"a\")))";
    error.clear();
    const auto ordered_startup = exact_player_zones_startup_decklist(
        ordered_active, ordered_deck, ordered_hand, error);
    require(ordered_startup.has_value(), error.c_str());
    require(*ordered_startup
                == "(deckTag=\"ordered\",cardList=((cardName=\"a\",count=1),"
                   "(cardName=\"a\",count=1),(cardName=\"b\",count=1,upgradeLevel=1)),"
                   "fixedOrder=True,dungeonTools=())",
            "fixed-order startup preserves the deck then reverses the hand draw order");

    error.clear();
    require(!exact_player_zones_startup_decklist(
                zones_route.active_decklist,
                zones.player_deck,
                "((CardInfo=(Tag=\"different\")))",
                error),
            "exact player zones reject a different card multiset");

    auto trash_route = original;
    trash_route.active_decklist =
        "(deckTag=\"trash-test\",cardList=((cardName=\"a\",count=2),"
        "(cardName=\"b\",count=1,upgradeLevel=1)),fixedOrder=False)";
    trash_route.payload_checksum = route_c_payload_checksum(trash_route);
    auto trash = sample_exact_player_trash(trash_route);
    const auto trash_json = serialize_exact_player_trash_checkpoint(trash);
    error.clear();
    const auto parsed_trash = parse_exact_player_trash_checkpoint(trash_json, error);
    require(parsed_trash.has_value(), error.c_str());
    require(parsed_trash->player_trash == trash.player_trash,
            "exact player trash round trip");
    require(parsed_trash->payload_checksum
                == exact_player_trash_payload_checksum(*parsed_trash),
            "exact player-trash checksum round trip");
    auto legacy_trash = trash;
    legacy_trash.schema_version = 1;
    const auto parsed_legacy_trash = parse_exact_player_trash_checkpoint(
        serialize_exact_player_trash_checkpoint(legacy_trash), error);
    require(parsed_trash->schema_version == 2 && parsed_legacy_trash
                && parsed_legacy_trash->schema_version == 1,
            "legacy trash is read without claiming native order");

    const auto card_array = [](std::initializer_list<std::string_view> tags) {
        std::string result{"("};
        for (const auto tag : tags)
        {
            if (result.size() > 1) result += ',';
            result += "(CardInfo=(Tag=\"" + std::string{tag} + "\"))";
        }
        return result + ')';
    };
    require(exact_player_trash_staging_matches(
                card_array({"e", "a"}), card_array({"f", "b"}), card_array({"c", "d", "g"}),
                card_array({"e", "a", "c"}), card_array({"f", "b", "g", "d"}), "()", error, true),
            "native staging retains unsorted zones and reverses popped trash overflow");
    require(!exact_player_trash_staging_matches(
                card_array({"e", "a"}), card_array({"f", "b"}), card_array({"c", "d", "g"}),
                card_array({"e", "a", "c"}), card_array({"f", "b", "d", "g"}), "()", error, true),
            "native staging rejects forward-ordered overflow with the same card multiset");
    require(!exact_player_trash_staging_matches(
                card_array({"e", "a"}), card_array({"f", "b"}), card_array({"c", "d", "g"}),
                card_array({"a", "e", "c"}), card_array({"f", "b", "g", "d"}), "()", error, true),
            "native staging rejects a deck permutation that changes future draw order");

    auto ordered_trash = trash;
    ordered_trash.player_deck = card_array({"b", "a"});
    ordered_trash.player_hand = card_array({"h"});
    ordered_trash.player_trash = card_array({"a"});
    const std::vector<PlayerRestoreCandidate> ordered_trash_candidates{
        {"a@0", 1}, {"b@0", 1}, {"a@0", 1}, {"h@0", 0}};
    const auto ordered_trash_plan = plan_player_trash_restore(
        ordered_trash, ordered_trash_candidates, error);
    require(ordered_trash_plan
                && ordered_trash_plan->trash_candidates == std::vector<std::size_t>{0},
            "trash planner retains the ordered deck subsequence across duplicate identities");
    ordered_trash.player_deck = card_array({"a", "a", "b"});
    ordered_trash.player_trash = "()";
    require(!plan_player_trash_restore(ordered_trash, ordered_trash_candidates, error),
            "native plan rejects retained order absent from the staged deck");

    error.clear();
    const auto trash_startup = exact_player_trash_startup_decklist(
        trash_route.active_decklist,
        trash.player_deck,
        trash.player_hand,
        trash.player_trash,
        error);
    require(trash_startup.has_value(), error.c_str());
    require(*trash_startup
                == "(deckTag=\"trash-test\",cardList=((cardName=\"a\",count=1),"
                   "(cardName=\"a\",count=1),(cardName=\"b\",count=1,upgradeLevel=1)),"
                   "fixedOrder=True)",
            "player trash is staged above the exact deck and below the reversed hand");

    error.clear();
    require(exact_player_trash_staging_matches(
                "((CardInfo=(Tag=\"a\")),(CardInfo=(Tag=\"e\")))",
                "((CardInfo=(Tag=\"b\")),(CardInfo=(Tag=\"f\")))",
                "((CardInfo=(Tag=\"c\")),(CardInfo=(Tag=\"d\")),"
                "(CardInfo=(Tag=\"g\")))",
                "((CardInfo=(Tag=\"a\")),(CardInfo=(Tag=\"c\")),"
                "(CardInfo=(Tag=\"d\")),(CardInfo=(Tag=\"e\")))",
                "((CardInfo=(Tag=\"g\")),(CardInfo=(Tag=\"b\")),"
                "(CardInfo=(Tag=\"f\")))",
                "()",
                error),
            error.c_str());

    error.clear();
    require(exact_player_trash_staging_matches(
                "((CardInfo=(Tag=\"a\")))",
                "((CardInfo=(Tag=\"b\")))",
                "((CardInfo=(Tag=\"naturalApple\",Level=1,rarityTier=1),"
                "upgradeLevel=0))",
                "((CardInfo=(Tag=\"a\")))",
                "((CardInfo=(Tag=\"naturalApple\",usageType=STARTER),"
                "upgradeLevel=0),(CardInfo=(Tag=\"b\")))",
                "()",
                error),
            "staging compares semantic card identity across runtime normalization");

    require(!exact_player_trash_staging_matches(
                "((CardInfo=(Tag=\"a\")),(CardInfo=(Tag=\"e\")))",
                "((CardInfo=(Tag=\"b\")),(CardInfo=(Tag=\"f\")))",
                "((CardInfo=(Tag=\"c\")),(CardInfo=(Tag=\"d\")),"
                "(CardInfo=(Tag=\"g\")))",
                "((CardInfo=(Tag=\"a\")),(CardInfo=(Tag=\"d\")),"
                "(CardInfo=(Tag=\"c\")),(CardInfo=(Tag=\"e\")))",
                "((CardInfo=(Tag=\"g\")),(CardInfo=(Tag=\"b\")),"
                "(CardInfo=(Tag=\"f\")))",
                "()",
                error),
            "mixed deck/hand staging rejects a reordered deck/trash merge");

    error.clear();
    require(exact_card_identity_key_from_instance(
                "(CardInfo=(Tag=\"b\"),upgradeLevel=1)", error)
                == std::optional<std::string>{"b@1"},
            "runtime card instance exposes a stable tag and upgrade identity key");

    auto empty_trash = trash;
    empty_trash.player_trash = "()";
    error.clear();
    require(!parse_exact_player_trash_checkpoint(
                serialize_exact_player_trash_checkpoint(empty_trash), error),
            "empty trash is outside the independent player-trash slice");

    auto overlapping_hand_trash = trash;
    overlapping_hand_trash.player_trash =
        "((CardInfo=(Tag=\"b\"),upgradeLevel=1))";
    error.clear();
    require(!parse_exact_player_trash_checkpoint(
                serialize_exact_player_trash_checkpoint(overlapping_hand_trash), error),
            "a shared hand/trash identity is outside the guarded mixed-zone slice");
    require(error.find("ambiguous") != std::string::npos,
            "shared hand/trash identity reports the staging ambiguity");
    auto native_overlap_trash = trash;
    native_overlap_trash.player_hand = card_array({"naturalApple"});
    native_overlap_trash.player_trash = card_array({"naturalApple"});
    require(parse_exact_player_trash_checkpoint(
                serialize_exact_player_trash_checkpoint(native_overlap_trash), error).has_value(),
            "native-order trash captures allow separate plain-fruit copies in hand and trash");
    const std::vector<PlayerRestoreCandidate> native_overlap_candidates{
        {"naturalApple@0", 0}, {"naturalApple@0", 0}, {"a@0", 1}};
    const auto native_overlap_plan = plan_player_trash_restore(
        native_overlap_trash, native_overlap_candidates, error);
    require(native_overlap_plan && native_overlap_plan->trash_candidates == std::vector<std::size_t>{1},
            "first ordered hand copy remains in hand while overflow copy moves to trash");
    native_overlap_trash.schema_version = 1;
    require(!parse_exact_player_trash_checkpoint(
                serialize_exact_player_trash_checkpoint(native_overlap_trash), error),
            "legacy sorted trash captures retain their cross-zone guard");

    auto corrupt_trash = trash_json;
    const auto trash_card = corrupt_trash.find("\"playerTrash\"");
    require(trash_card != std::string::npos, "player-trash fixture contains its zone");
    const auto trash_tag = corrupt_trash.find("Tag=\\\"a\\\"", trash_card);
    require(trash_tag != std::string::npos, "player-trash fixture contains its card tag");
    corrupt_trash[trash_tag + 6] = 'z';
    error.clear();
    require(!parse_exact_player_trash_checkpoint(corrupt_trash, error),
            "corrupted exact player trash is rejected");
    require(error.find("checksum") != std::string::npos,
            "player-trash corruption reports checksum error");

    auto field_route = original;
    field_route.active_decklist =
        "(deckTag=\"field-test\",cardList=((cardName=\"a\",count=1),"
        "(cardName=\"b\",count=1),(cardName=\"c\",count=1),"
        "(cardName=\"naturalApple\",count=1),"
        "(cardName=\"naturalLemon\",count=1)))";
    field_route.payload_checksum = route_c_payload_checksum(field_route);
    auto field = sample_exact_player_field(field_route);
    const auto field_json = serialize_exact_player_field_checkpoint(field);
    error.clear();
    const auto parsed_field = parse_exact_player_field_checkpoint(field_json, error);
    require(parsed_field.has_value(), error.c_str());
    require(parsed_field->player_field == field.player_field,
            "exact player field round trip");
    require(parsed_field->payload_checksum
                == exact_player_field_payload_checksum(*parsed_field),
            "exact player-field checksum round trip");
    auto legacy_field = field;
    legacy_field.schema_version = 1;
    const auto parsed_legacy_field = parse_exact_player_field_checkpoint(
        serialize_exact_player_field_checkpoint(legacy_field), error);
    require(parsed_field->schema_version == 2 && parsed_legacy_field
                && parsed_legacy_field->schema_version == 1,
            "legacy field layouts keep their sorted-order interpretation");
    auto native_field = field;
    native_field.player_deck = card_array({"z", "a"});
    native_field.player_hand = card_array({"y", "b"});
    const auto parsed_native_field = parse_exact_player_field_checkpoint(
        serialize_exact_player_field_checkpoint(native_field), error);
    require(parsed_native_field && parsed_native_field->player_deck == native_field.player_deck
                && parsed_native_field->player_hand == native_field.player_hand,
            "native order survives persistence without metadata sorting");
    auto swapped_native_field = native_field;
    swapped_native_field.player_deck = card_array({"a", "z"});
    require(exact_player_field_payload_checksum(swapped_native_field)
                != exact_player_field_payload_checksum(native_field),
            "native deck order contributes to the integrity checksum");

    error.clear();
    const auto field_states = parse_exact_player_field_states(
        field.player_field_states, error);
    require(field_states && field_states->size() == 2,
            "aligned player-field states parse");
    require((*field_states)[0].row == 0 && (*field_states)[0].index == 0
                && (*field_states)[0].current_health == 1
                && !(*field_states)[0].turn_active,
            "front player-field state parses exactly");
    require((*field_states)[1].row == 1 && (*field_states)[1].index == 3
                && (*field_states)[1].current_health == 2
                && (*field_states)[1].turn_active,
            "back player-field state parses exactly");

    error.clear();
    const auto field_startup = exact_player_field_startup_decklist(
        field_route.active_decklist,
        field.player_deck,
        field.player_hand,
        field.player_trash,
        field.player_field,
        error);
    require(field_startup.has_value(), error.c_str());
    require(field_startup->find("fixedOrder=True") != std::string::npos,
            "player-field startup enables fixed order");
    const std::string generated_active =
        "(cardList=((cardName=\"naturalApple\",count=2),(cardName=\"naturalSpring\",count=1),"
        "(cardName=\"genericBattery\",count=1)),fixedOrder=False)";
    const auto generated_field = card_array({"naturalApple","naturalSpring"});
    const auto generated_hand = card_array({"genericBattery","naturalApple"});
    const auto generated_trash = card_array({"naturalApple"});
    require(!exact_player_field_startup_decklist(generated_active,"()",generated_hand,
                generated_trash,generated_field,error),
            "legacy startup still rejects an additional generated copy");
    std::size_t additional_card_count = 999;
    const auto generated_startup = exact_player_field_startup_decklist(generated_active,"()",
        generated_hand,generated_trash,generated_field,error,true,&additional_card_count);
    require(generated_startup && generated_startup->find("fixedOrder=True") != std::string::npos,
            "schema-6 startup accepts exactly one additional unupgraded Apple with Apple and Spring in the active deck");
    require(additional_card_count == 1,"generated startup explicitly reports its temporary extra card");
    require(!exact_player_field_startup_decklist(generated_active,"()",generated_hand,
                card_array({"naturalApple","naturalApple"}),generated_field,error,true,&additional_card_count),
            "two additional copies remain outside the tested extension");
    require(additional_card_count == 0,"rejected startup cannot retain a previous extra-card count");
    require(!exact_player_field_startup_decklist(generated_active,"()",generated_hand,
                card_array({"naturalCherry"}),generated_field,error,true),
            "an additional unsupported identity cannot pass the generated Apple guard");
    require(!exact_player_field_startup_decklist(generated_active,"()",card_array({"naturalApple"}),
                generated_trash,generated_field,error,true),
            "a missing original Battery cannot be hidden by an extra Apple");
    require(!exact_player_field_startup_decklist(
                "(cardList=((cardName=\"naturalApple\",count=2),(cardName=\"genericBattery\",count=1)))",
                "()",generated_hand,generated_trash,card_array({"naturalApple"}),error,true),
            "additional Apple requires a tested Spring source in the original active deck");
    require(exact_player_field_startup_decklist(generated_active,"()",card_array({"genericBattery"}),
                generated_trash,generated_field,error,true,&additional_card_count).has_value(),
            "schema-6 also retains the ordinary complete active multiset");
    require(additional_card_count == 0,"ordinary startup requires no extra-card normalization");
    require(field_startup->find("cardName=\"naturalApple\"")
                < field_startup->find("cardName=\"naturalLemon\""),
            "player-field startup preserves placement order below the saved hand");

    error.clear();
    require(exact_player_field_staging_matches(
                field.player_deck,
                field.player_hand,
                field.player_trash,
                field.player_field,
                "((CardInfo=(Tag=\"a\")))",
                "((CardInfo=(Tag=\"c\")),(CardInfo=(Tag=\"naturalApple\")),"
                "(CardInfo=(Tag=\"naturalLemon\")),(CardInfo=(Tag=\"b\")))",
                "()",
                error),
            error.c_str());

    error.clear();
    require(exact_player_field_staging_matches(
                field.player_deck,
                field.player_hand,
                field.player_trash,
                field.player_field,
                "((CardInfo=(Tag=\"a\")))",
                "((CardInfo=(Tag=\"b\")),(CardInfo=(Tag=\"c\")),"
                "(CardInfo=(Tag=\"naturalApple\")),"
                "(CardInfo=(Tag=\"naturalLemon\")))",
                "()",
                error),
            "player-field staging accepts sorted interleaving of saved hand and extras");

    auto ambiguous_field = field;
    // Actor enumeration order differs from both controller order and saved slots.
    const std::vector<PlayerRestoreCandidate> mixed_candidates{
        {"naturalLemon@0", 0}, {"a@0", 1}, {"b@0", 0},
        {"c@0", 0}, {"naturalApple@0", 1},
    };
    const auto mixed_plan = plan_player_field_restore(field, mixed_candidates, error);
    require(mixed_plan.has_value(), error.c_str());
    require(mixed_plan->trash_candidates == std::vector<std::size_t>{3}
                && mixed_plan->field_candidates == std::vector<std::size_t>{4, 0},
            "mixed layout assigns unique native candidates in saved trash and slot order");

    auto duplicate_deck_trash = field;
    duplicate_deck_trash.player_deck = "((CardInfo=(Tag=\"c\")))";
    auto duplicated_candidates = mixed_candidates;
    duplicated_candidates[1].identity = "c@0";
    const auto reserved_plan = plan_player_field_restore(
        duplicate_deck_trash, duplicated_candidates, error);
    require(reserved_plan && reserved_plan->trash_candidates == std::vector<std::size_t>{3},
            "saved deck copy is reserved before selecting equivalent hand overflow for trash");

    auto double_trash = field;
    double_trash.player_trash = "((CardInfo=(Tag=\"c\")),(CardInfo=(Tag=\"c\")))";
    auto double_candidates = mixed_candidates;
    double_candidates.push_back({"c@0", 1});
    const auto double_plan = plan_player_field_restore(double_trash, double_candidates, error);
    require(double_plan && double_plan->trash_candidates == std::vector<std::size_t>{3, 5},
            "same-identity trash copies in hand and deck each receive a distinct target");

    auto trash_frees_hand = mixed_candidates;
    trash_frees_hand[0].location = 1;
    require(plan_player_field_restore(field, trash_frees_hand, error).has_value(),
            "trash movement frees a staging slot even when all field targets start in deck");
    trash_frees_hand[3].location = 1;
    require(!plan_player_field_restore(field, trash_frees_hand, error),
            "layout without a movable initial hand target keeps the capacity guard");
    require(plan_player_field_restore(field, trash_frees_hand, error, 7).has_value(),
            "the public hand capacity can prove room for a deck-to-field staging move");
    require(!plan_player_field_restore(field, trash_frees_hand, error, 1),
            "a full hand still needs a movable target or a deferred saved card");

    auto full_hand_layout = field;
    full_hand_layout.player_deck = "()";
    full_hand_layout.player_trash = "()";
    full_hand_layout.player_field = card_array({"naturalApple"});
    full_hand_layout.player_hand = card_array({"a", "a", "c", "d", "e", "f", "g"});
    const std::vector<PlayerRestoreCandidate> initial_full_hand{
        {"a@0",0},{"a@0",0},{"c@0",0},{"d@0",0},{"e@0",0},
        {"naturalApple@0",1},{"g@0",1},{"f@0",1}};
    const auto full_hand_staging = plan_player_hand_staging(full_hand_layout, initial_full_hand, 7, error);
    require(full_hand_staging && full_hand_staging->moves.size() == 12
                && full_hand_staging->before_field_move_count == 11,
            "a full hand defers its last card until the native field staging slot is no longer needed");
    std::vector<std::size_t> staged_deck_indices{5,6,7}, staged_hand_indices{0,1,2,3,4};
    for (std::size_t step{}; step < full_hand_staging->moves.size(); ++step)
    {
        if (step == full_hand_staging->before_field_move_count)
        {
            require(staged_hand_indices.size() == 6 && staged_deck_indices == std::vector<std::size_t>{5,6},
                    "the field phase retains the final hand card behind its remaining field target");
            staged_deck_indices.erase(staged_deck_indices.begin()); // FIELD restoration consumes Apple.
        }
        const auto move = full_hand_staging->moves[step];
        auto& source = move.destination == 0 ? staged_deck_indices : staged_hand_indices;
        auto& destination = move.destination == 0 ? staged_hand_indices : staged_deck_indices;
        const auto found = std::find(source.begin(), source.end(), move.candidate);
        require(found != source.end(), "every hand staging action moves exactly one existing native object");
        source.erase(found);
        destination.push_back(move.candidate);
        require(staged_hand_indices.size() <= 7, "hand staging must respect capacity after every native action");
    }
    require(staged_deck_indices.empty()
                && staged_hand_indices == std::vector<std::size_t>{0,1,2,3,4,7,6},
            "full-hand staging restores both equal copies in original order and leaves no deck card");
    require(!plan_player_hand_staging(full_hand_layout, initial_full_hand, 6, error),
            "a target larger than the native public hand capacity is rejected before movement");
    auto reordered_initial = initial_full_hand;
    std::swap(reordered_initial[5], reordered_initial[6]);
    require(!plan_player_hand_staging(full_hand_layout, reordered_initial, 7, error),
            "matching counts cannot hide a changed native startup deck order");
    full_hand_layout.schema_version = 1;
    require(!plan_player_hand_staging(full_hand_layout, initial_full_hand, 7, error),
            "hand reconstruction requires native-order provenance");
    auto shared_deferred_layout = field;
    shared_deferred_layout.player_deck = card_array({"naturalApple"});
    shared_deferred_layout.player_hand = card_array({"b", "c", "d", "e", "f", "g"});
    shared_deferred_layout.player_trash = "()";
    shared_deferred_layout.player_field = card_array({"naturalApple"});
    const std::vector<PlayerRestoreCandidate> shared_deferred_cards{
        {"naturalApple@0",1},{"naturalApple@0",1},
        {"b@0",0},{"c@0",0},{"d@0",0},{"e@0",0},{"f@0",0},{"g@0",0}};
    const auto shared_deferred_plan = plan_player_field_restore(shared_deferred_layout, shared_deferred_cards, error, 7);
    require(shared_deferred_plan && shared_deferred_plan->field_candidates == std::vector<std::size_t>{1},
            "the deferred hand copy must be rebound after the field plan reserves an equivalent deck copy");
    auto unsupported_hand_overlap = field;
    unsupported_hand_overlap.player_deck = unsupported_hand_overlap.player_field = "()";
    unsupported_hand_overlap.player_hand = unsupported_hand_overlap.player_trash = card_array({"naturalCherry"});
    require(!plan_player_hand_staging(unsupported_hand_overlap, {{"naturalCherry@0",0},{"naturalCherry@0",0}}, 7, error)
                && error.find("special-card overlap") != std::string::npos,
            "unsupported overlap is refused before any native hand reconstruction");

    auto deck_only_layout = field;
    deck_only_layout.player_deck = card_array({"a", "b", "c", "d", "e", "f", "g", "h"});
    deck_only_layout.player_hand = deck_only_layout.player_field = deck_only_layout.player_trash = "()";
    const std::vector<PlayerRestoreCandidate> initial_deck_only{
        {"a@0",1},{"b@0",1},{"c@0",1},{"h@0",0},{"g@0",0},{"f@0",0},{"e@0",0},{"d@0",0}};
    const auto deck_only_staging = plan_player_hand_staging(deck_only_layout, initial_deck_only, 7, error);
    require(deck_only_staging && deck_only_staging->moves.size() == 5
                && deck_only_staging->before_field_move_count == 5,
            "an empty saved hand returns every native startup draw without requiring field or trash");
    for (std::size_t index{}; index < 5; ++index)
        require(deck_only_staging->moves[index].candidate == 7 - index
                    && deck_only_staging->moves[index].destination == 1,
                "startup draws return in reverse hand order to preserve the next deck pop");

    auto wrong_candidates = mixed_candidates;
    wrong_candidates[0].identity = "naturalLemon@1";
    require(!plan_player_field_restore(field, wrong_candidates, error),
            "planning rejects a changed upgrade before native writes");
    wrong_candidates = mixed_candidates;
    wrong_candidates[1].location = 0;
    require(!plan_player_field_restore(field, wrong_candidates, error),
            "matching total identities do not permit a saved deck card in the wrong zone");
    wrong_candidates = mixed_candidates;
    wrong_candidates[3].location = 2;
    require(!plan_player_field_restore(field, wrong_candidates, error),
            "unexpected live trash is not a staged movement candidate");
    wrong_candidates = mixed_candidates;
    wrong_candidates.push_back({"extra@0", 0});
    require(!plan_player_field_restore(field, wrong_candidates, error),
            "extra native cards invalidate the entire movement plan");

    auto overlapping_layout = field;
    overlapping_layout.player_trash = "((CardInfo=(Tag=\"b\")))";
    auto overlapping_candidates = mixed_candidates;
    overlapping_candidates[3].identity = "b@0";
    require(!plan_player_field_restore(overlapping_layout, overlapping_candidates, error)
                && error.find("shared hand/field/trash") != std::string::npos,
            "shared hand/trash identity is rejected before any target is used");
    overlapping_layout.player_trash = "((CardInfo=(Tag=\"naturalApple\")))";
    overlapping_candidates[3].identity = "naturalApple@0";
    const auto field_trash_overlap_plan = plan_player_field_restore(
        overlapping_layout, overlapping_candidates, error);
    require(field_trash_overlap_plan
                && field_trash_overlap_plan->trash_candidates == std::vector<std::size_t>{3}
                && field_trash_overlap_plan->field_candidates == std::vector<std::size_t>{4, 0},
            "native-order layout assigns distinct copies to trash and field");
    overlapping_layout.schema_version = 1;
    require(!plan_player_field_restore(overlapping_layout, overlapping_candidates, error)
                && error.find("shared hand/field/trash") != std::string::npos,
            "legacy field/trash identity overlap remains rejected");

    // Runtime regression: field slots are Apple/Lemon/Apple, but native hand
    // sorting exposes Cherry/Cherry/Apple/Lemon/Spring during five-card startup.
    const std::string retained_deck = "((CardInfo=(Tag=\"genericBattery\")),(CardInfo=(Tag=\"spDeckEdit\")))";
    const std::string retained_hand = "((CardInfo=(Tag=\"naturalCherry\")),(CardInfo=(Tag=\"naturalCherry\")),(CardInfo=(Tag=\"naturalSpring\")))";
    const std::string slot_order = "((CardInfo=(Tag=\"naturalApple\")),(CardInfo=(Tag=\"naturalLemon\"),upgradeLevel=1),(CardInfo=(Tag=\"naturalApple\")))";
    const std::string sorted_stage_deck = "((CardInfo=(Tag=\"naturalApple\")),(CardInfo=(Tag=\"genericBattery\")),(CardInfo=(Tag=\"spDeckEdit\")))";
    const std::string sorted_stage_hand = "((CardInfo=(Tag=\"naturalCherry\")),(CardInfo=(Tag=\"naturalCherry\")),(CardInfo=(Tag=\"naturalApple\")),(CardInfo=(Tag=\"naturalLemon\"),upgradeLevel=1),(CardInfo=(Tag=\"naturalSpring\")))";
    error.clear();
    require(exact_player_field_staging_matches(retained_deck, retained_hand, "()", slot_order,
                sorted_stage_deck, sorted_stage_hand, "()", error), error.c_str());
    error.clear();
    require(!exact_player_field_staging_matches(retained_deck, retained_hand, "()", slot_order,
                sorted_stage_deck,
                "((CardInfo=(Tag=\"naturalSpring\")),(CardInfo=(Tag=\"naturalCherry\")),(CardInfo=(Tag=\"naturalCherry\")),(CardInfo=(Tag=\"naturalApple\")),(CardInfo=(Tag=\"naturalLemon\"),upgradeLevel=1))",
                "()", error), "staging must still preserve retained hand order");
    auto wrong_upgrade = sorted_stage_hand;
    wrong_upgrade.replace(wrong_upgrade.find("upgradeLevel=1"), 14, "upgradeLevel=2");
    error.clear();
    require(!exact_player_field_staging_matches(retained_deck, retained_hand, "()", slot_order,
                sorted_stage_deck, wrong_upgrade, "()", error),
            "relaxed temporary field order must not permit changed upgrades");

    ambiguous_field.player_hand = "((CardInfo=(Tag=\"naturalApple\")))";
    error.clear();
    require(parse_exact_player_field_checkpoint(
                serialize_exact_player_field_checkpoint(ambiguous_field), error)
                .has_value(),
            "equivalent hand/field card identities remain valid staging inputs");
    auto hand_field_overlap_candidates = mixed_candidates;
    hand_field_overlap_candidates[2].identity = "naturalApple@0";
    const auto hand_field_overlap_plan = plan_player_field_restore(
        ambiguous_field, hand_field_overlap_candidates, error);
    require(hand_field_overlap_plan
                && hand_field_overlap_plan->field_candidates == std::vector<std::size_t>{4, 0},
            "native hand copy is reserved before assigning the same identity to a field slot");
    auto all_shared = field;
    all_shared.player_deck = card_array({"naturalApple", "z"});
    all_shared.player_hand = card_array({"naturalApple", "b"});
    all_shared.player_trash = card_array({"naturalApple"});
    all_shared.player_field = card_array({"naturalApple", "naturalApple"});
    const std::vector<PlayerRestoreCandidate> all_shared_candidates{
        {"naturalApple@0", 0}, {"b@0", 0}, {"naturalApple@0", 0},
        {"naturalApple@0", 0}, {"naturalApple@0", 0}, {"naturalApple@0", 1}, {"z@0", 1}};
    const auto all_shared_plan = plan_player_field_restore(all_shared, all_shared_candidates, error);
    require(all_shared_plan
                && all_shared_plan->trash_candidates == std::vector<std::size_t>{2}
                && all_shared_plan->field_candidates == std::vector<std::size_t>{3, 4},
            "five identical copies across four zones get unique assignments without consuming retained cards");
    auto missing_shared_copy = all_shared_candidates;
    missing_shared_copy[4].identity = "naturalApple@1";
    require(!plan_player_field_restore(all_shared, missing_shared_copy, error),
            "a differently upgraded copy cannot satisfy a shared-identity field target");
    auto empty_hand_layout = field;
    empty_hand_layout.player_hand = "()";
    empty_hand_layout.player_trash = card_array({"c", "b"});
    const auto empty_hand_plan = plan_player_field_restore(empty_hand_layout, mixed_candidates, error);
    require(empty_hand_plan && empty_hand_plan->trash_candidates == std::vector<std::size_t>{3, 2},
            "an empty saved hand can move every temporary hand card to its real destination");
    require(parse_exact_player_field_checkpoint(
                serialize_exact_player_field_checkpoint(empty_hand_layout), error).has_value(),
            "native field captures preserve an empty hand");
    require(exact_player_field_startup_decklist(field_route.active_decklist,
                empty_hand_layout.player_deck, empty_hand_layout.player_hand,
                empty_hand_layout.player_trash, empty_hand_layout.player_field, error).has_value(),
            "empty-hand field startup still contains the complete active card multiset");
    empty_hand_layout.schema_version = 1;
    require(!parse_exact_player_field_checkpoint(
                serialize_exact_player_field_checkpoint(empty_hand_layout), error),
            "legacy field bounds remain unchanged for an empty hand");

    auto empty_deck_layout = field;
    empty_deck_layout.player_deck = "()";
    empty_deck_layout.player_trash = card_array({"c", "a"});
    require(parse_exact_player_field_checkpoint(
                serialize_exact_player_field_checkpoint(empty_deck_layout), error).has_value(),
            "native field captures preserve an empty deck");
    auto empty_deck_candidates = mixed_candidates;
    const auto empty_deck_plan = plan_player_field_restore(empty_deck_layout, empty_deck_candidates, error);
    require(empty_deck_plan && empty_deck_plan->trash_candidates == std::vector<std::size_t>{3, 1},
            "staged deck extras are removed when no cards should remain in deck");
    require(exact_player_field_startup_decklist(field_route.active_decklist,
                empty_deck_layout.player_deck, empty_deck_layout.player_hand,
                empty_deck_layout.player_trash, empty_deck_layout.player_field, error).has_value(),
            "empty saved deck is distinct from an empty staged startup deck");

    auto all_trash_layout = trash;
    all_trash_layout.player_deck = "()";
    all_trash_layout.player_hand = "()";
    all_trash_layout.player_trash =
        "((CardInfo=(Tag=\"a\")),(CardInfo=(Tag=\"a\")),(CardInfo=(Tag=\"b\"),upgradeLevel=1))";
    require(parse_exact_player_trash_checkpoint(
                serialize_exact_player_trash_checkpoint(all_trash_layout), error).has_value(),
            "native trash capture may have no retained deck or hand");
    require(exact_player_trash_startup_decklist(trash_route.active_decklist, "()", "()",
                all_trash_layout.player_trash, error).has_value(),
            "all-trash startup builds native cards before relocating them");
    const auto all_trash_plan = plan_player_trash_restore(all_trash_layout,
        {{"b@1", 0}, {"a@0", 0}, {"a@0", 1}}, error);
    require(all_trash_plan && all_trash_plan->trash_candidates == std::vector<std::size_t>{1, 2, 0},
            "all-trash plan assigns every card without reserving a nonexistent zone copy");
    require(!exact_player_zones_startup_decklist(trash_route.active_decklist, "()", "()", error),
            "a completely empty active startup is still rejected");
    ambiguous_field.schema_version = 1;
    require(!plan_player_field_restore(ambiguous_field, hand_field_overlap_candidates, error)
                && error.find("shared hand/field/trash") != std::string::npos,
            "legacy identities do not bypass the old hand/field ambiguity guard");

    auto empty_trash_field = field;
    empty_trash_field.player_trash = "()";
    empty_trash_field.player_field =
        "((CardInfo=(Tag=\"naturalApple\")),(CardInfo=(Tag=\"naturalLemon\")),"
        "(CardInfo=(Tag=\"naturalApple\")))";
    auto repeated_field_candidates = mixed_candidates;
    repeated_field_candidates[3].identity = "naturalApple@0";
    const auto empty_trash_plan = plan_player_field_restore(
        empty_trash_field, repeated_field_candidates, error);
    require(empty_trash_plan && empty_trash_plan->trash_candidates.empty()
                && empty_trash_plan->field_candidates == std::vector<std::size_t>{3, 0, 4},
            "existing empty-trash sample keeps two separate apples aligned with their saved slots");

    auto six_visible_cards = field;
    six_visible_cards.player_hand =
        "((CardInfo=(Tag=\"b\")),(CardInfo=(Tag=\"d\")),"
        "(CardInfo=(Tag=\"e\")),(CardInfo=(Tag=\"f\")))";
    const std::string six_visible_decklist =
        "(deckTag=\"field-capacity-test\",cardList=((cardName=\"a\",count=1),"
        "(cardName=\"b\",count=1),(cardName=\"c\",count=1),"
        "(cardName=\"d\",count=1),(cardName=\"e\",count=1),"
        "(cardName=\"f\",count=1),(cardName=\"naturalApple\",count=1),"
        "(cardName=\"naturalLemon\",count=1)))";
    error.clear();
    require(exact_player_field_startup_decklist(
                six_visible_decklist,
                six_visible_cards.player_deck,
                six_visible_cards.player_hand,
                six_visible_cards.player_trash,
                six_visible_cards.player_field,
                error)
                .has_value(),
            "field staging supports more than five cards across saved hand and field");

    auto unsupported_field = field;
    unsupported_field.player_field = "((CardInfo=(Tag=\"spStorageHit\")))";
    unsupported_field.player_field_states = "0,0,1,1";
    error.clear();
    require(!parse_exact_player_field_checkpoint(
                serialize_exact_player_field_checkpoint(unsupported_field), error),
            "special cards are outside the first guarded player-field slice");

    error.clear();
    require(!parse_exact_player_field_states("1,3,2,1;0,0,1,0", error),
            "player-field states reject non-canonical slot order");

    auto charge = sample_exact_character_charge(original);
    const auto charge_json = serialize_exact_character_charge_checkpoint(charge);
    error.clear();
    const auto parsed_charge = parse_exact_character_charge_checkpoint(charge_json, error);
    require(parsed_charge.has_value(), error.c_str());
    require(parsed_charge->charge == 4 && parsed_charge->requirement == 6,
            "exact character charge round trip");
    require(parsed_charge->payload_checksum
                == exact_character_charge_payload_checksum(*parsed_charge),
            "exact character-charge checksum round trip");

    auto full_charge = charge;
    full_charge.charge = full_charge.requirement;
    error.clear();
    require(!parse_exact_character_charge_checkpoint(
                serialize_exact_character_charge_checkpoint(full_charge), error),
            "full character charge is outside the first exact slice");

    auto corrupt_charge = charge_json;
    const auto charge_field = corrupt_charge.find("\"charge\": 4");
    require(charge_field != std::string::npos, "character-charge fixture contains charge");
    corrupt_charge.replace(
        charge_field, std::string{"\"charge\": 4"}.size(), "\"charge\": 3");
    error.clear();
    require(!parse_exact_character_charge_checkpoint(corrupt_charge, error),
            "corrupted exact character charge is rejected");
    require(error.find("checksum") != std::string::npos,
            "character-charge corruption reports checksum error");

    auto turn_progress = sample_exact_turn_progress(original);
    const auto turn_progress_json = serialize_exact_turn_progress_checkpoint(turn_progress);
    error.clear();
    const auto parsed_turn_progress =
        parse_exact_turn_progress_checkpoint(turn_progress_json, error);
    require(parsed_turn_progress.has_value(), error.c_str());
    require(parsed_turn_progress->card_engine_turn_count == 5
                && parsed_turn_progress->player_draw_delay == 4
                && parsed_turn_progress->wave_alert_counter == 5
                && parsed_turn_progress->player_can_click_to_draw == 1,
            "exact turn progress round trip");
    require(parsed_turn_progress->payload_checksum
                == exact_turn_progress_payload_checksum(*parsed_turn_progress),
            "exact turn-progress checksum round trip");

    // Regression: these states display the same zero, but only base=0 can draw.
    auto display_only_zero = turn_progress;
    display_only_zero.player_draw_delay = 0;
    display_only_zero.player_draw_base = 5;
    display_only_zero.player_draw_adjustment = -5;
    auto ready_zero = display_only_zero;
    ready_zero.player_draw_base = 0;
    ready_zero.player_draw_adjustment = 0;
    error.clear();
    const auto parsed_display_only = parse_exact_turn_progress_checkpoint(
        serialize_exact_turn_progress_checkpoint(display_only_zero), error);
    const auto parsed_ready = parse_exact_turn_progress_checkpoint(
        serialize_exact_turn_progress_checkpoint(ready_zero), error);
    require(parsed_display_only && parsed_ready
                && parsed_display_only->player_draw_base == 5
                && parsed_display_only->player_draw_adjustment == -5
                && parsed_ready->player_draw_base == 0
                && parsed_ready->player_draw_adjustment == 0
                && parsed_display_only->payload_checksum != parsed_ready->payload_checksum,
            "equal displayed delays preserve distinct native readiness states");

    auto missing_components = turn_progress_json;
    const auto component_start = missing_components.find("  \"playerDrawBase\":");
    require(component_start != std::string::npos, "schema 2 emits native countdown");
    missing_components.erase(component_start, missing_components.find('\n', component_start) - component_start + 1);
    error.clear();
    require(!parse_exact_turn_progress_checkpoint(missing_components, error),
            "schema 2 must not invent missing native countdown");

    auto inconsistent_components = turn_progress;
    inconsistent_components.player_draw_base = 5;
    error.clear();
    require(!parse_exact_turn_progress_checkpoint(
                serialize_exact_turn_progress_checkpoint(inconsistent_components), error),
            "valid checksum does not permit inconsistent native draw components");

    auto tampered_components = turn_progress_json;
    const auto base_field = tampered_components.find("\"playerDrawBase\": 4");
    require(base_field != std::string::npos, "countdown corruption fixture exists");
    tampered_components.replace(base_field, std::string{"\"playerDrawBase\": 4"}.size(),
                                "\"playerDrawBase\": 3");
    error.clear();
    require(!parse_exact_turn_progress_checkpoint(tampered_components, error),
            "native countdown tampering is rejected");

    auto legacy_turn_progress = turn_progress;
    legacy_turn_progress.schema_version = 1;
    legacy_turn_progress.player_can_click_to_draw = -1;
    const auto legacy_turn_progress_json =
        serialize_exact_turn_progress_checkpoint(legacy_turn_progress);
    require(legacy_turn_progress_json.find("playerCanClickToDraw")
                == std::string::npos,
            "legacy turn progress omits draw-click readiness");
    error.clear();
    const auto parsed_legacy_turn_progress =
        parse_exact_turn_progress_checkpoint(legacy_turn_progress_json, error);
    require(parsed_legacy_turn_progress
                && parsed_legacy_turn_progress->player_can_click_to_draw == -1
                && parsed_legacy_turn_progress->schema_version == 1,
            "legacy schema-1 turn progress remains readable");

    auto corrupt_turn_progress = turn_progress_json;
    const auto draw_delay_field = corrupt_turn_progress.find("\"playerDrawDelay\": 4");
    require(draw_delay_field != std::string::npos,
            "turn-progress fixture contains player draw delay");
    corrupt_turn_progress.replace(
        draw_delay_field,
        std::string{"\"playerDrawDelay\": 4"}.size(),
        "\"playerDrawDelay\": 3");
    error.clear();
    require(!parse_exact_turn_progress_checkpoint(corrupt_turn_progress, error),
            "corrupted exact turn progress is rejected");
    require(error.find("checksum") != std::string::npos,
            "turn-progress corruption reports checksum error");

    auto negative_turn_progress = turn_progress;
    negative_turn_progress.card_engine_turn_count = -1;
    error.clear();
    require(!parse_exact_turn_progress_checkpoint(
                serialize_exact_turn_progress_checkpoint(negative_turn_progress), error),
            "negative CardEngine turn is rejected");

    auto excessive_draw_delay = turn_progress;
    excessive_draw_delay.player_draw_delay = 1'000'001;
    error.clear();
    require(!parse_exact_turn_progress_checkpoint(
                serialize_exact_turn_progress_checkpoint(excessive_draw_delay), error),
            "excessive player draw delay is rejected");

    auto excessive_wave_alert = turn_progress;
    excessive_wave_alert.wave_alert_counter = 1'000'001;
    error.clear();
    require(!parse_exact_turn_progress_checkpoint(
                serialize_exact_turn_progress_checkpoint(excessive_wave_alert), error),
            "excessive wave-alert counter is rejected");

    auto attack_source = sample_checkpoint();
    attack_source.payload_checksum = route_c_payload_checksum(attack_source);
    auto field_with_attacks = sample_exact_player_field(attack_source);
    field_with_attacks.schema_version = 3;
    field_with_attacks.player_field_attack_states = "2,5|springBuff,3,-1,0;1,3|once,2,-1,2";
    const auto field_attack_json = serialize_exact_player_field_checkpoint(field_with_attacks);
    const auto parsed_field_attacks = parse_exact_player_field_checkpoint(field_attack_json, error);
    require(parsed_field_attacks && parsed_field_attacks->schema_version == 3
                && parsed_field_attacks->player_field_attack_states == field_with_attacks.player_field_attack_states,
            "schema-3 field attack modifiers round trip aligned with saved field cards");
    auto missing_attack_record = field_with_attacks;
    missing_attack_record.player_field_attack_states = "2,5|springBuff,3,-1,0";
    require(!parse_exact_player_field_checkpoint(serialize_exact_player_field_checkpoint(missing_attack_record), error),
            "field attack record count must match field cards");
    auto incorrect_attack_value = field_with_attacks;
    incorrect_attack_value.player_field_attack_states = "2,8|springBuff,3,-1,0;1,3|once,2,-1,2";
    require(!parse_exact_player_field_checkpoint(serialize_exact_player_field_checkpoint(incorrect_attack_value), error),
            "a valid checksum cannot authorize an inconsistent attack value");
    auto attack_tampering = field_attack_json;
    attack_tampering.replace(attack_tampering.find("springBuff"), 10, "springBoff");
    require(!parse_exact_player_field_checkpoint(attack_tampering, error), "field modifier tag is covered by checksum");
    auto missing_attack_property = field_attack_json;
    const auto property_begin = missing_attack_property.find("  \"playerFieldAttackStates\"");
    missing_attack_property.erase(property_begin, missing_attack_property.find('\n', property_begin) - property_begin + 1);
    require(!parse_exact_player_field_checkpoint(missing_attack_property, error), "schema-3 field requires attack property");
    const auto field_v2_json = serialize_exact_player_field_checkpoint(sample_exact_player_field(attack_source));
    require(field_v2_json.find("playerFieldAttackStates") == std::string::npos
                && parse_exact_player_field_checkpoint(field_v2_json, error)->schema_version == 2,
            "schema-2 field remains readable without newer attack claims");
    auto disguised_attack_property = field_v2_json;
    disguised_attack_property.insert(disguised_attack_property.rfind('}'), ",\"playerFieldAttackStates\":\"2,2;1,1\"");
    require(!parse_exact_player_field_checkpoint(disguised_attack_property, error), "legacy field cannot carry unchecked attack records");

    auto field_with_health = field_with_attacks;
    field_with_health.schema_version = 4;
    field_with_health.player_field_health_states = "2,5|health,3,-1,4;2,2";
    const auto health_json = serialize_exact_player_field_checkpoint(field_with_health);
    const auto parsed_health = parse_exact_player_field_checkpoint(health_json,error);
    require(parsed_health && parsed_health->schema_version == 4
                && parsed_health->player_field_health_states == field_with_health.player_field_health_states,
            "schema-4 health modifiers round trip beside attack modifiers");
    field_with_health.player_field_states = "0,0,5,0;1,3,2,1";
    field_with_health.player_field_health_states = "2,2;2,2";
    require(parse_exact_player_field_checkpoint(serialize_exact_player_field_checkpoint(field_with_health),error).has_value(),
            "native current card health above effective maximum remains representable");
    field_with_health.player_field_health_states = "2,5|SPRINGBUFF,3,-1,0;2,2";
    require(!parse_exact_player_field_checkpoint(serialize_exact_player_field_checkpoint(field_with_health),error),
            "attack and health modifiers cannot collide in the same FName table");
    field_with_health.player_field_health_states = "2,2";
    require(!parse_exact_player_field_checkpoint(serialize_exact_player_field_checkpoint(field_with_health),error),
            "health records must align with all field slots");
    auto health_tampering = health_json;
    health_tampering.replace(health_tampering.find("health,3"),8,"health,2");
    require(!parse_exact_player_field_checkpoint(health_tampering,error),"health modifier data is integrity checked");
    auto missing_health_property = health_json;
    const auto health_begin = missing_health_property.find("  \"playerFieldHealthStates\"");
    missing_health_property.erase(health_begin,missing_health_property.find('\n',health_begin)-health_begin+1);
    require(!parse_exact_player_field_checkpoint(missing_health_property,error),"schema-4 health record is mandatory");
    auto disguised_health_property = field_attack_json;
    disguised_health_property.insert(disguised_health_property.rfind('}'),",\"playerFieldHealthStates\":\"2,2;2,2\"");
    require(!parse_exact_player_field_checkpoint(disguised_health_property,error),"schema-3 cannot carry unchecked health records");

    auto field_with_counters = *parsed_health;
    field_with_counters.schema_version = 5;
    field_with_counters.player_field_counter_states = "3|springBuff,0;0|counterA,-1|counterB,2";
    const auto counter_json = serialize_exact_player_field_checkpoint(field_with_counters);
    const auto parsed_counters = parse_exact_player_field_checkpoint(counter_json,error);
    require(parsed_counters && parsed_counters->schema_version == 5
        && parsed_counters->player_field_counter_states == field_with_counters.player_field_counter_states,
        "schema-5 counters round trip with zero and negative entries and independent stat tags");
    auto generated_schema = *parsed_counters;
    generated_schema.schema_version = 6;
    const auto parsed_generated_schema = parse_exact_player_field_checkpoint(
        serialize_exact_player_field_checkpoint(generated_schema),error);
    require(parsed_generated_schema && parsed_generated_schema->schema_version == 6
        && parsed_generated_schema->player_field_counter_states == parsed_counters->player_field_counter_states,
        "schema-6 keeps all earlier FIELD supplements and binds its startup policy through the schema checksum");
    field_with_counters.player_field_counter_states = "3";
    require(!parse_exact_player_field_checkpoint(serialize_exact_player_field_checkpoint(field_with_counters),error),
        "counter records must align with every field card");
    field_with_counters.player_field_counter_states = "257;0";
    require(!parse_exact_player_field_checkpoint(serialize_exact_player_field_checkpoint(field_with_counters),error),
        "a valid checksum cannot authorize unbounded counter replay");
    auto counter_tampering = counter_json;
    counter_tampering.replace(counter_tampering.find("counterA,-1"),11,"counterA,-2");
    require(!parse_exact_player_field_checkpoint(counter_tampering,error),"counter values are integrity checked");
    auto missing_counter_property = counter_json;
    const auto counter_begin = missing_counter_property.find("  \"playerFieldCounterStates\"");
    missing_counter_property.erase(counter_begin,missing_counter_property.find('\n',counter_begin)-counter_begin+1);
    require(!parse_exact_player_field_checkpoint(missing_counter_property,error),"schema-5 counter record is mandatory");
    auto disguised_counter_property = health_json;
    disguised_counter_property.insert(disguised_counter_property.rfind('}'),",\"playerFieldCounterStates\":\"0;0\"");
    require(!parse_exact_player_field_checkpoint(disguised_counter_property,error),"schema-4 cannot carry unchecked counter records");

    std::cout << "Route C checkpoint persistence tests passed\n";
    return 0;
}
