#include "CheckpointPersistence.hpp"

#include <cstdlib>
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

    std::cout << "Route C checkpoint persistence tests passed\n";
    return 0;
}
