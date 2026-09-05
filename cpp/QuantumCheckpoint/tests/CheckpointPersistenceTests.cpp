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

    std::cout << "Route C checkpoint persistence tests passed\n";
    return 0;
}
