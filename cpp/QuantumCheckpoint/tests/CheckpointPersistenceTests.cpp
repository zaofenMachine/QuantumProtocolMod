#include "CheckpointPersistence.hpp"
#include "PlayerCounterState.hpp"
#include "PlayerHealthState.hpp"
#include "PlayerRestorePlan.hpp"
#include "EffectQualificationPolicy.hpp"

#include <cstdlib>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
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
            .schema_version = 2,
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
            .schema_version = 3,
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

    auto sample_exact_player_hand_health(const QuantumCheckpoint::RouteCCheckpoint& route_c)
        -> QuantumCheckpoint::ExactPlayerHandHealthCheckpoint
    {
        return {
            .schema_version = 1,
            .captured_at_utc = route_c.captured_at_utc,
            .route_c_payload_checksum = route_c.payload_checksum,
            .game_executable_sha256 = route_c.game_executable_sha256,
            .game_executable_size = route_c.game_executable_size,
            .source_level_name = route_c.source_level_name,
            .wave_index = route_c.wave_index,
            .player_hand =
                "((CardInfo=(Tag=\"naturalApple\")),(CardInfo=(Tag=\"naturalApple\")))",
            .player_hand_health_states = "2,3|mageOctavia_init,1,-1,0;2,2",
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
    auto run_qualification_tests() -> void
    {
        using namespace QuantumCheckpoint;
        std::string error{};
        auto route = sample_checkpoint();
        route.payload_checksum = "0123456789ABCDEF";
        constexpr std::string_view dependency = "FEDCBA9876543210";
        auto historical_zones = sample_exact_player_zones(route);
        historical_zones.player_hand_health_checksum = dependency;
        historical_zones.player_deck_base_health_states = "2,2";
        auto historical_trash = sample_exact_player_trash(route);
        historical_trash.player_hand_health_checksum = dependency;
        historical_trash.player_deck_base_health_states = "2,2";
        historical_trash.player_trash_base_health_states = "2,2";
        auto historical_field = sample_exact_player_field(route);
        historical_field.player_hand_health_checksum = dependency;
        historical_field.player_deck_base_health_states = "2,2";
        historical_field.player_trash_base_health_states = "2,2";
        historical_field.player_field_attack_states = "1,1;0,0";
        historical_field.player_field_health_states = "2,2;2,2";
        historical_field.player_field_counter_states = "0;0";
        // Fixed independent historical FNV vectors, including every old layout
        // version. A new marker must never change their bytes or fixed minima.
        const auto historical_hashes = [&](auto layout, const auto& expected, auto checksum) {
            for (std::size_t index{}; index < expected.size(); ++index)
            {
                layout.schema_version = static_cast<int>(index + 1);
                require(checksum(layout) == expected[index], "all historical layout checksums remain unchanged");
                layout.qualification_policy = ExactPlayerQualificationPolicy;
                require(checksum(layout) == expected[index], "hidden legacy marker never changes historical hash bytes");
                layout.qualification_policy.clear();
            }
        };
        historical_hashes(historical_zones, std::array{
            "A2B48645623F9AAF", "75DB16877DD36A7E", "7A50D14B4A6517F5", "90E7869CAF490B54", "B91310E09E4DC324"},
            exact_player_zones_payload_checksum);
        historical_hashes(historical_trash, std::array{
            "C4C69827A52EA9A4", "6555E4D9F806783B", "F346F1D793DC7D12", "649326BE7777A7B5", "6014171BB4480CBC", "539B47E597360E3D"},
            exact_player_trash_payload_checksum);
        historical_hashes(historical_field, std::array{
            "2F3E9925B59C16C4", "F7436F7E3C506149", "09B0FA55E3578F88", "73178054880B0DCB", "D70BB1CA39643E32",
            "DFACA22DC1587E51", "4F2B99917C153734", "F5B7190AB2AF7597", "0634D5628AB3CDBC"},
            exact_player_field_payload_checksum);

        const auto remove_field = [](std::string json, std::string_view name) {
            const auto begin = json.find("  \"" + std::string{name} + "\"");
            require(begin != std::string::npos, "mutation field exists");
            json.erase(begin, json.find('\n', begin) - begin + 1);
            return json;
        };
        const auto replace_once = [](std::string json, std::string_view from, std::string_view to) {
            const auto begin = json.find(from);
            require(begin != std::string::npos, "mutation value exists");
            json.replace(begin, from.size(), to);
            return json;
        };
        const auto check_marker = [&](auto layout, int required_version, auto serialize, auto parse,
                                      auto checksum, auto validate) {
            layout.schema_version = required_version - 1;
            layout.payload_checksum = checksum(layout);
            const auto legacy_json = serialize(layout);
            require(parse(legacy_json, error).has_value(), "last historical layout remains readable");
            auto hidden = layout;
            hidden.qualification_policy = ExactPlayerQualificationPolicy;
            require(!validate(hidden, error), "historical structs reject nonempty qualification policy");
            for (const std::string value : {"", "flags-baseline-v1"})
            {
                auto injected = legacy_json;
                injected.insert(injected.rfind('}'), ",\"qualificationPolicy\":\"" + value + "\"");
                require(!parse(injected, error), "historical JSON rejects qualification key even when empty");
            }
            for (const std::string extra : {"\"cardCount\":0", "\"handCardCount\":0",
                    "\"layoutPayloadChecksum\":\"\"", "\"card0Effect0UsedOnce\":1",
                    "\"qualificationPayloadChecksum\":\"\"", "\"playerQualificationPolicy\":\"\""})
            {
                auto injected = legacy_json;
                injected.insert(injected.rfind('}'), "," + extra);
                require(!parse(injected, error), "historical layout rejects reserved Q1 fields that its checksum cannot cover");
            }
            layout.schema_version = required_version;
            require(!parse(serialize(layout), error), "new schemas require explicit nonempty policy");
            layout.qualification_policy = ExactPlayerQualificationPolicy;
            const auto json = serialize(layout);
            const auto parsed = parse(json, error);
            require(parsed && parsed->qualification_policy == ExactPlayerQualificationPolicy,
                    "new layout qualification marker round trips");
            require(!parse(remove_field(json, "qualificationPolicy"), error), "missing required marker is rejected");
            for (const std::string value : {"", "flags-baseline-v2", " flags-baseline-v1"})
            {
                auto invalid = layout;
                invalid.qualification_policy = value;
                require(!parse(serialize(invalid), error), "unknown or malformed policy cannot authorize new layout");
                require(checksum(invalid) != checksum(layout), "new policy participates in layout checksum");
            }
            require(!parse(replace_once(json, "flags-baseline-v1", "flags-baseline-v2"), error),
                    "policy tampering fails without upgrading a claim");
            require(!parse(replace_once(json, "\"schemaVersion\": " + std::to_string(required_version),
                                            "\"schemaVersion\": " + std::to_string(required_version - 1)), error),
                    "downgrading layout cannot hide surviving policy key");
        };
        check_marker(historical_zones, ExactPlayerZonesQualificationSchemaVersion,
            serialize_exact_player_zones_checkpoint, parse_exact_player_zones_checkpoint,
            exact_player_zones_payload_checksum, validate_exact_player_zones_checkpoint);
        check_marker(historical_trash, ExactPlayerTrashQualificationSchemaVersion,
            serialize_exact_player_trash_checkpoint, parse_exact_player_trash_checkpoint,
            exact_player_trash_payload_checksum, validate_exact_player_trash_checkpoint);
        check_marker(historical_field, ExactPlayerFieldQualificationSchemaVersion,
            serialize_exact_player_field_checkpoint, parse_exact_player_field_checkpoint,
            exact_player_field_payload_checksum, validate_exact_player_field_checkpoint);

        const std::string apple_instance = "(CardInfo=(Tag=\"naturalApple\",DisplayName=INVTEXT(\"苹果\")),upgradeLevel=0)";
        const std::string tool_instance = "(CardInfo=(Tag=\"spDeckEdit\"),upgradeLevel=0)";
        const std::string lemon_instance = "(CardInfo=(Tag=\"naturalLemon\"),upgradeLevel=0)";
        const auto effect = [&](std::string_view key, int ordinal, std::string factory, std::string tag, int type) {
            const auto profile = qualification::lookup_effect_qualification_profile(key, ordinal, factory, tag, type);
            require(profile.has_value(), "reviewed test profile exists");
            return ExactPlayerQualificationEffect{.ordinal = ordinal, .factory_key = std::move(factory),
                .tag = std::move(tag), .type = type, .flags = {profile->flags.begin(), profile->flags.end()},
                .used_once = 0, .profile_id = std::string{profile->id}};
        };
        const auto apple_effect = effect("naturalApple@0", 0, "player.esper.fruit.apple", "naturalApple", 3);
        const auto lemon_effect = effect("naturalLemon@0", 0, "player.esper.fruit.lemon", "naturalLemon", 3);
        auto qualified_zones = historical_zones;
        qualified_zones.schema_version = ExactPlayerZonesQualificationSchemaVersion;
        qualified_zones.player_hand = "(" + tool_instance + ")";
        qualified_zones.player_deck = "(" + apple_instance + "," + apple_instance + ")";
        qualified_zones.player_deck_base_health_states = "2,2;2,2";
        qualified_zones.qualification_policy = ExactPlayerQualificationPolicy;
        qualified_zones.payload_checksum = exact_player_zones_payload_checksum(qualified_zones);
        ExactPlayerQualificationCheckpoint proof{
            .schema_version = ExactPlayerQualificationBaselineSchemaVersion,
            .captured_at_utc = qualified_zones.captured_at_utc,
            .route_c_payload_checksum = qualified_zones.route_c_payload_checksum,
            .game_executable_sha256 = qualified_zones.game_executable_sha256,
            .game_executable_size = qualified_zones.game_executable_size,
            .source_level_name = qualified_zones.source_level_name,
            .wave_index = qualified_zones.wave_index,
            .layout_kind = qualified_zones.kind,
            .layout_payload_checksum = qualified_zones.payload_checksum,
            .qualification_policy = ExactPlayerQualificationPolicy.data(),
            .cards = {
                {.native_location = 0, .native_index = 0, .card_info_instance = tool_instance,
                 .effects = {effect("spDeckEdit@0", 0, "player.special.deckedit", "spDeckEdit", 3),
                             effect("spDeckEdit@0", 1, "player.special.deckedit_manual", "spDeckEdit_manual", 1)}},
                {.native_location = 1, .native_index = 0, .card_info_instance = apple_instance, .effects = {apple_effect}},
                {.native_location = 1, .native_index = 1, .card_info_instance = apple_instance, .effects = {apple_effect}}
            }
        };
        proof.payload_checksum = exact_player_qualification_payload_checksum(proof);
        require(qualified_zones.payload_checksum == "837379379206DF5E"
                    && proof.payload_checksum == "74E89D4D3F6EA1CB",
                "fixed Phase39 Z6 and Qschema1 FNV vectors remain byte-compatible under B runtime");
        const auto proof_json = serialize_exact_player_qualification_checkpoint(proof);
        const auto parsed = parse_exact_player_qualification_checkpoint(proof_json, error);
        require(parsed && parsed->cards == proof.cards && serialize_exact_player_qualification_checkpoint(*parsed) == proof_json,
                "Q1 typed cards/effects, full UTF-8 CardInfo and bonded flags round trip");
        require(validate_exact_player_qualification_binding(proof, qualified_zones, error), error.c_str());
        require(proof_json.find("\"trashCardCount\": 0") != std::string::npos
                    && proof_json.find("\"fieldCardCount\": 0") != std::string::npos,
                "Q1 explicitly serializes empty covered zones");
        const auto rejected = [&](ExactPlayerQualificationCheckpoint invalid) {
            error.clear();
            return !parse_exact_player_qualification_checkpoint(serialize_exact_player_qualification_checkpoint(invalid), error);
        };
        for (const auto& field : {"qualificationPolicy", "card0Effect0ProfileId", "card0Effect0Flags", "trashCardCount"})
            require(!parse_exact_player_qualification_checkpoint(remove_field(proof_json, field), error),
                    "Q1 requires every marker, effect field and explicit empty-zone count");
        for (const std::string extra : {"\"extra\":0", "\"card00NativeLocation\":0", "\"card3NativeLocation\":1",
                                        "\"card0Effect2Ordinal\":2", "\"card0Effect0Address\":1", "\"card0Guid\":\"old\""})
        {
            auto json = proof_json;
            json.insert(json.rfind('}'), "," + extra);
            require(!parse_exact_player_qualification_checkpoint(json, error), "Q1 rejects all extra, alias and address/GUID fields");
        }
        auto duplicate_json = proof_json;
        duplicate_json.insert(duplicate_json.rfind('}'), ",\"card0Effect0Ordinal\":0");
        require(!parse_exact_player_qualification_checkpoint(duplicate_json, error), "Q1 rejects duplicate JSON keys");
        require(!parse_exact_player_qualification_checkpoint(replace_once(proof_json, "\"cardCount\": 3", "\"cardCount\": 03"), error),
                "Q1 rejects malformed JSON integer spelling");
        require(!parse_exact_player_qualification_checkpoint(replace_once(proof_json, "\"cardCount\": 3", "\"cardCount\": 3.0"), error),
                "Q1 rejects fractional count values");
        require(!parse_exact_player_qualification_checkpoint(replace_once(proof_json, "\"trashCardCount\": 0", "\"trashCardCount\": 1"), error),
                "Q1 rejects inconsistent explicit empty-zone counts");
        require(!parse_exact_player_qualification_checkpoint(replace_once(proof_json, "苹果", "果苹"), error)
                    && error.find("checksum") != std::string::npos,
                "Q1 hashes full CardInfo metadata beyond card identity");
        for (const std::string flags : {"0", "6,6", "7,6", "8", "-1", "06", "6,", ",6", "6 7", "6,7,1"})
            require(!parse_exact_player_qualification_checkpoint(replace_once(proof_json, "\"card0Effect0Flags\": \"6\"",
                "\"card0Effect0Flags\": \"" + flags + "\""), error), "Q1 rejects ONCE, duplicate, noncanonical or out-of-range flags");
        for (const auto flags : {std::vector<std::uint8_t>{}, {1}, {6,7}, {0,6}, {6,6}, {8}})
        {
            auto invalid = proof;
            invalid.cards[0].effects[0].flags = flags;
            require(rejected(invalid), "correct checksum cannot authorize removed or added default flags");
        }
        for (const auto used : {-1,1,2,256})
        {
            auto invalid = proof;
            invalid.cards[0].effects[0].used_once = used;
            require(rejected(invalid), "slice A rejects used1 and malformed used values without any writer");
        }
        for (int mutation{}; mutation < 19; ++mutation)
        {
            auto invalid = proof;
            switch (mutation)
            {
            case 0: invalid.schema_version = 2; break;
            case 1: invalid.qualification_policy = "flags-baseline-v2"; break;
            case 2: invalid.cards[0].effects[0].ordinal = 1; break;
            case 3: std::swap(invalid.cards[0].effects[0], invalid.cards[0].effects[1]); break;
            case 4: invalid.cards[0].effects[0].factory_key = "unknown"; break;
            case 5: invalid.cards[0].effects[0].tag = "unknown"; break;
            case 6: invalid.cards[0].effects[0].type = 256; break;
            case 7: invalid.cards[0].effects[0].profile_id = "unknown"; break;
            case 8: invalid.cards[0].effects.clear(); break;
            case 9: invalid.cards[0].effects.resize(65); break;
            case 10: invalid.cards[2].native_index = 0; break;
            case 11: invalid.cards[2].native_index = 2; break;
            case 12: invalid.cards[0].row = 0; break;
            case 13: invalid.cards[1].native_location = 4; break;
            case 14: std::swap(invalid.cards[0], invalid.cards[1]); break;
            case 15: invalid.cards[1].card_info_instance = "(Tag=\"naturalApple\")"; break;
            case 16: invalid.cards[1].card_info_instance = "(CardInfo=(Tag=\"naturalApple\",Tag=\"naturalLemon\"))"; break;
            case 17: invalid.cards[1].card_info_instance += ",junk"; break;
            case 18: invalid.cards[1].card_info_instance = "(CardInfo=(Tag=\"unknown\"))"; invalid.cards[1].effects.clear(); break;
            }
            require(rejected(invalid), "Q1 rejects correctly checksummed malformed identity, membership, order and profile claims");
        }
        auto maximum = proof;
        maximum.cards.assign(128, proof.cards[1]);
        for (std::size_t index{}; index < maximum.cards.size(); ++index) maximum.cards[index].native_index = static_cast<int>(index);
        require(!rejected(maximum), "128 native DECK copies are supported without imposing FIELD slot limits");
        maximum.cards.push_back(maximum.cards.back());
        require(rejected(maximum), "129 cards exceed the Q1 bound");
        maximum = proof;
        maximum.cards.assign(8, proof.cards[1]);
        for (std::size_t index{}; index < maximum.cards.size(); ++index)
        {
            maximum.cards[index].native_location = 0;
            maximum.cards[index].native_index = static_cast<int>(index);
        }
        require(rejected(maximum), "eight HAND records exceed the existing hand bound");
        require(!parse_exact_player_qualification_checkpoint(std::string(RouteCMaximumFileBytes + 1, ' '), error),
                "Q1 rejects files above 2 MiB before parsing");

        const auto bind_rejected = [&](auto invalid, const auto& layout) {
            invalid.payload_checksum = exact_player_qualification_payload_checksum(invalid);
            return !validate_exact_player_qualification_binding(invalid, layout, error);
        };
        for (int mutation{}; mutation < 10; ++mutation)
        {
            auto invalid = proof;
            switch (mutation)
            {
            case 0: invalid.captured_at_utc += "x"; break;
            case 1: invalid.route_c_payload_checksum = "FEDCBA9876543210"; break;
            case 2: invalid.game_executable_sha256[0] = 'A'; break;
            case 3: ++invalid.game_executable_size; break;
            case 4: invalid.source_level_name += "x"; break;
            case 5: ++invalid.wave_index; break;
            case 6: invalid.layout_payload_checksum = "FEDCBA9876543210"; break;
            case 7: invalid.cards[1].card_info_instance = "(CardInfo=(Tag=\"naturalApple\",DisplayName=INVTEXT(\"changed\")),upgradeLevel=0)"; break;
            case 8: invalid.cards.pop_back(); break;
            case 9: invalid.cards.push_back(invalid.cards.back()); invalid.cards.back().native_index = 2; break;
            }
            require(bind_rejected(invalid, qualified_zones), "Q1 rejects mismatched metadata or incomplete full-layout bijections");
        }
        auto old_layout = qualified_zones;
        old_layout.schema_version = ExactPlayerZonesQualificationSchemaVersion - 1;
        old_layout.qualification_policy.clear();
        old_layout.payload_checksum = exact_player_zones_payload_checksum(old_layout);
        auto old_proof = proof;
        old_proof.layout_payload_checksum = old_layout.payload_checksum;
        require(bind_rejected(old_proof, old_layout), "historical layout cannot acquire a Q1 proof even with matching full cards");

        auto qualified_trash = historical_trash;
        qualified_trash.schema_version = ExactPlayerTrashQualificationSchemaVersion;
        qualified_trash.player_deck = qualified_zones.player_deck;
        qualified_trash.player_hand = qualified_zones.player_hand;
        qualified_trash.player_deck_base_health_states = qualified_zones.player_deck_base_health_states;
        qualified_trash.player_trash = "(" + lemon_instance + ")";
        qualified_trash.qualification_policy = ExactPlayerQualificationPolicy;
        qualified_trash.payload_checksum = exact_player_trash_payload_checksum(qualified_trash);
        require(qualified_trash.payload_checksum == "6599DDACA977629F", "fixed Phase39 T7 FNV remains unchanged");
        auto trash_proof = proof;
        trash_proof.layout_kind = qualified_trash.kind;
        trash_proof.layout_payload_checksum = qualified_trash.payload_checksum;
        trash_proof.cards.push_back({.native_location = 2, .native_index = 0,
            .card_info_instance = lemon_instance, .effects = {lemon_effect}});
        trash_proof.payload_checksum = exact_player_qualification_payload_checksum(trash_proof);
        require(validate_exact_player_qualification_binding(trash_proof, qualified_trash, error), error.c_str());
        auto qualified_field = historical_field;
        qualified_field.schema_version = ExactPlayerFieldQualificationSchemaVersion;
        qualified_field.player_deck = qualified_trash.player_deck;
        qualified_field.player_hand = qualified_trash.player_hand;
        qualified_field.player_trash = qualified_trash.player_trash;
        qualified_field.player_deck_base_health_states = qualified_trash.player_deck_base_health_states;
        qualified_field.player_field = "(" + apple_instance + "," + lemon_instance + ")";
        qualified_field.qualification_policy = ExactPlayerQualificationPolicy;
        qualified_field.payload_checksum = exact_player_field_payload_checksum(qualified_field);
        require(qualified_field.payload_checksum == "65F22A164D117FFC", "fixed Phase39 F10 FNV remains unchanged");
        auto field_proof = trash_proof;
        field_proof.layout_kind = qualified_field.kind;
        field_proof.layout_payload_checksum = qualified_field.payload_checksum;
        field_proof.cards.push_back({.native_location = 3, .row = 0, .index = 0,
            .card_info_instance = apple_instance, .effects = {apple_effect}});
        field_proof.cards.push_back({.native_location = 3, .row = 1, .index = 3,
            .card_info_instance = lemon_instance, .effects = {lemon_effect}});
        field_proof.payload_checksum = exact_player_qualification_payload_checksum(field_proof);
        require(validate_exact_player_qualification_binding(field_proof, qualified_field, error), error.c_str());
        for (int mutation{}; mutation < 6; ++mutation)
        {
            auto invalid = field_proof;
            switch (mutation)
            {
            case 0: invalid.cards.back().row = 0; invalid.cards.back().index = 0; break;
            case 1: invalid.cards.back().row = 2; break;
            case 2: invalid.cards.back().index = 5; break;
            case 3: invalid.cards.back().native_index = 0; break;
            case 4: invalid.cards.back().row = std::numeric_limits<std::int32_t>::max(); break;
            case 5: invalid.cards.back().index = 2; break; // Valid unique slot, wrong saved destination.
            }
            require(bind_rejected(invalid, qualified_field), "Q1 rejects duplicate, invalid and mismatched FIELD destinations");
        }

        // B is an explicit new promise, never a global relaxation of A profiles.
        using namespace qualification;
        require(is_supported_qualification_policy(ExactPlayerQualificationPolicy)
                    && is_supported_qualification_policy(ExactPlayerQualificationBowOncePolicy)
                    && !is_supported_qualification_policy("flags-baseline-bow-once-v3")
                    && qualification_proof_schema_for_policy(ExactPlayerQualificationPolicy) == 1
                    && qualification_proof_schema_for_policy(ExactPlayerQualificationBowOncePolicy) == 2
                    && !qualification_proof_schema_for_policy(""),
                "only exact published A/B policies dispatch to exact proof schemas");
        require(!supports_qualification_card_profile("sakuraBow@0")
                    && !qualification_card_effect_count("sakuraBow@0", ExactPlayerQualificationPolicy)
                    && supports_qualification_card_profile("sakuraBow@0", ExactPlayerQualificationBowOncePolicy)
                    && qualification_card_effect_count("sakuraBow@0", ExactPlayerQualificationBowOncePolicy) == 1
                    && !supports_qualification_card_profile("sakuraBow@1", ExactPlayerQualificationBowOncePolicy)
                    && !supports_qualification_card_profile("unknown@0", ExactPlayerQualificationBowOncePolicy)
                    && !supports_qualification_card_profile("naturalApple@0", "unknown"),
                "B adds exactly base Bow and default profile APIs remain A");
        const auto bow_profile = lookup_effect_qualification_profile("sakuraBow@0", 0,
            "player.aurora.sakura.sakurabow", "sakuraBow", 1, ExactPlayerQualificationBowOncePolicy);
        require(bow_profile && bow_profile->permits_used_once_replay
                    && bow_profile->id == "phase40-bow-once-v2:player.aurora.sakura.sakurabow"
                    && bow_profile->flags.size() == 1 && bow_profile->flags[0] == 0,
                "base Bow B profile matches independently reviewed id and exact ONCE default");
        require(!lookup_effect_qualification_profile("sakuraBow@0", 0,
                    "player.aurora.sakura.sakurabow", "sakuraBow", 1)
                    && !lookup_effect_qualification_profile("sakuraBow@1", 0,
                    "player.aurora.sakura.sakurabow.u1", "sakuraBow", 1, ExactPlayerQualificationBowOncePolicy)
                    && !lookup_effect_qualification_profile("sakuraBow@0", 1,
                    "player.aurora.sakura.sakurabow", "sakuraBow", 1, ExactPlayerQualificationBowOncePolicy),
                "A, upgraded Bow and wrong ordinal never acquire replay permission");
        for (const auto& card : proof.cards)
        {
            const auto key = exact_card_identity_key_from_instance(card.card_info_instance, error);
            require(key.has_value(), "existing A test card identity remains readable");
            require(qualification_card_effect_count(*key, ExactPlayerQualificationPolicy)
                        == qualification_card_effect_count(*key, ExactPlayerQualificationBowOncePolicy),
                    "B retains existing A exact effect counts");
            for (const auto& effect : card.effects)
            {
                const auto a_profile = lookup_effect_qualification_profile(*key, effect.ordinal,
                    effect.factory_key, effect.tag, effect.type, ExactPlayerQualificationPolicy);
                const auto b_profile = lookup_effect_qualification_profile(*key, effect.ordinal,
                    effect.factory_key, effect.tag, effect.type, ExactPlayerQualificationBowOncePolicy);
                require(a_profile && b_profile && !a_profile->permits_used_once_replay
                            && !b_profile->permits_used_once_replay && a_profile->id == b_profile->id
                            && std::equal(a_profile->flags.begin(), a_profile->flags.end(), b_profile->flags.begin(), b_profile->flags.end()),
                        "existing A profile ids/flags remain identical and cannot replay used under B");
            }
        }
        const auto version_pair = [&](auto a_layout, auto a_proof, int b_version, auto serialize, auto parse, auto checksum) {
            const auto a_policy = qualification_policy_for_layout(a_layout.kind, a_layout.schema_version);
            require(a_policy && *a_policy == ExactPlayerQualificationPolicy,
                    "fixed A layout maps only to its original policy");
            auto b_layout = a_layout;
            b_layout.schema_version = b_version;
            require(!parse(serialize(b_layout), error), "new B layout cannot retain A marker");
            b_layout.qualification_policy = ExactPlayerQualificationBowOncePolicy;
            b_layout.payload_checksum = checksum(b_layout);
            const auto b_policy = qualification_policy_for_layout(b_layout.kind, b_layout.schema_version);
            require(b_policy && *b_policy == ExactPlayerQualificationBowOncePolicy
                        && parse(serialize(b_layout), error).has_value(), "B layout marker round trips at its exact version");
            auto wrong_a = a_layout;
            wrong_a.qualification_policy = ExactPlayerQualificationBowOncePolicy;
            require(!parse(serialize(wrong_a), error), "old A layout cannot carry a valid B policy even with recalculated checksum");
            auto a_for_b = a_proof;
            a_for_b.layout_kind = b_layout.kind;
            a_for_b.layout_payload_checksum = b_layout.payload_checksum;
            require(bind_rejected(a_for_b, b_layout), "schema1 A proof cannot bind B layout");
            auto b_for_a = a_proof;
            b_for_a.schema_version = ExactPlayerQualificationBowOnceSchemaVersion;
            b_for_a.qualification_policy = ExactPlayerQualificationBowOncePolicy;
            b_for_a.layout_kind = a_layout.kind;
            b_for_a.layout_payload_checksum = a_layout.payload_checksum;
            require(bind_rejected(b_for_a, a_layout), "schema2 B proof cannot bind A layout");
            auto future = b_layout;
            ++future.schema_version;
            require(!qualification_policy_for_layout(future.kind, future.schema_version)
                        && !parse(serialize(future), error), "future layout cannot inherit B policy by a minimum-version comparison");
            require(!qualification_policy_for_layout(a_layout.kind, a_layout.schema_version - 1),
                    "historical pre-A layout has no inferred qualification policy");
        };
        version_pair(qualified_zones, proof, ExactPlayerZonesBowOnceQualificationSchemaVersion,
            serialize_exact_player_zones_checkpoint, parse_exact_player_zones_checkpoint, exact_player_zones_payload_checksum);
        version_pair(qualified_trash, trash_proof, ExactPlayerTrashBowOnceQualificationSchemaVersion,
            serialize_exact_player_trash_checkpoint, parse_exact_player_trash_checkpoint, exact_player_trash_payload_checksum);
        version_pair(qualified_field, field_proof, ExactPlayerFieldBowOnceQualificationSchemaVersion,
            serialize_exact_player_field_checkpoint, parse_exact_player_field_checkpoint, exact_player_field_payload_checksum);

        const std::string bow_instance = "(CardInfo=(Tag=\"sakuraBow\",Level=1,upgradeLevels=((attack=1,health=1,effects=(\"player.aurora.sakura.sakurabow\")))),upgradeLevel=0)";
        auto b_zones = qualified_zones;
        b_zones.schema_version = ExactPlayerZonesBowOnceQualificationSchemaVersion;
        b_zones.qualification_policy = ExactPlayerQualificationBowOncePolicy;
        b_zones.player_hand = "(" + bow_instance + "," + bow_instance + ")";
        b_zones.payload_checksum = exact_player_zones_payload_checksum(b_zones);
        ExactPlayerQualificationEffect bow_effect{.ordinal = 0, .factory_key = "player.aurora.sakura.sakurabow",
            .tag = "sakuraBow", .type = 1, .flags = {0}, .used_once = 1,
            .profile_id = "phase40-bow-once-v2:player.aurora.sakura.sakurabow"};
        auto b_proof = proof;
        b_proof.schema_version = ExactPlayerQualificationBowOnceSchemaVersion;
        b_proof.qualification_policy = ExactPlayerQualificationBowOncePolicy;
        b_proof.layout_payload_checksum = b_zones.payload_checksum;
        b_proof.cards = {
            {.native_location = 0, .native_index = 0, .card_info_instance = bow_instance, .effects = {bow_effect}},
            {.native_location = 0, .native_index = 1, .card_info_instance = bow_instance, .effects = {bow_effect}},
            proof.cards[1], proof.cards[2]
        };
        b_proof.cards[1].effects[0].used_once = 0;
        b_proof.payload_checksum = exact_player_qualification_payload_checksum(b_proof);
        require(b_zones.payload_checksum == "D647A9E15DFC2255" && b_proof.payload_checksum == "526FF9469A5AF087",
                "schema2 retains checksum field order and hashes the actual positional source used state");
        const auto b_json = serialize_exact_player_qualification_checkpoint(b_proof);
        const auto b_parsed = parse_exact_player_qualification_checkpoint(b_json, error);
        require(b_parsed && b_parsed->cards == b_proof.cards
                    && b_parsed->cards[0].effects[0].used_once == 1
                    && b_parsed->cards[1].effects[0].used_once == 0
                    && validate_exact_player_qualification_binding(*b_parsed, b_zones, error),
                "same-name B Bows preserve distinct source used1/used0 at exact native HAND indexes");
        auto unused_b = b_proof;
        unused_b.cards[0].effects[0].used_once = 0;
        unused_b.payload_checksum = exact_player_qualification_payload_checksum(unused_b);
        require(!rejected(unused_b) && validate_exact_player_qualification_binding(unused_b, b_zones, error)
                    && unused_b.payload_checksum == "95C6B1A3688D6A0E"
                    && unused_b.payload_checksum != b_proof.payload_checksum,
                "B source0 is accepted and hashed separately without normalizing saved source1");
        auto swapped_b = b_proof;
        std::swap(swapped_b.cards[0].effects[0].used_once, swapped_b.cards[1].effects[0].used_once);
        require(exact_player_qualification_payload_checksum(swapped_b) == "B99D09FEDBBC4E39"
                    && exact_player_qualification_payload_checksum(swapped_b) != b_proof.payload_checksum
                    && !validate_exact_player_qualification_checkpoint(swapped_b, error),
                "swapping same-name source used values changes positional proof checksum");
        require(!parse_exact_player_qualification_checkpoint(replace_once(b_json,
                    "\"card0Effect0UsedOnce\": 1", "\"card0Effect0UsedOnce\": 0"), error)
                    && error.find("checksum") != std::string::npos,
                "source used tampering cannot silently become source0");
        for (int mutation{}; mutation < 17; ++mutation)
        {
            auto invalid = b_proof;
            switch (mutation)
            {
            case 0: invalid.schema_version = ExactPlayerQualificationBaselineSchemaVersion; break;
            case 1: invalid.qualification_policy = ExactPlayerQualificationPolicy; break;
            case 2: invalid.schema_version = 3; break;
            case 3: invalid.qualification_policy = "flags-baseline-bow-once-v3"; break;
            case 4: invalid.cards[0].effects[0].used_once = 2; break;
            case 5: invalid.cards[0].effects[0].flags.clear(); break;
            case 6: invalid.cards[0].effects[0].flags = {0,6}; break;
            case 7: invalid.cards[0].effects[0].factory_key += ".u1"; break;
            case 8: invalid.cards[0].card_info_instance = "(CardInfo=(Tag=\"sakuraBow\"),upgradeLevel=1)"; break;
            case 9: invalid.cards[0].effects[0].ordinal = 1; break;
            case 10: invalid.cards[0].effects[0].type = 3; break;
            case 11: invalid.cards[0].effects[0].profile_id = "phase39-default-v1:player.aurora.sakura.sakurabow"; break;
            case 12: invalid.cards[0].effects.push_back(invalid.cards[0].effects[0]); break;
            case 13: invalid.cards[1].native_index = 0; break;
            case 14: invalid.cards[0].card_info_instance = "(CardInfo=(Tag=\"Quarter\"))"; break;
            case 15: invalid.cards[2].effects[0].used_once = 1; break;
            case 16: invalid.cards[2].effects[0].flags = {0}; break;
            }
            require(rejected(invalid), "B rejects cross-policy schemas, unsupported Bow identity, dynamic flags and non-Bow used1");
        }
        auto a_bow = b_proof;
        a_bow.schema_version = ExactPlayerQualificationBaselineSchemaVersion;
        a_bow.qualification_policy = ExactPlayerQualificationPolicy;
        a_bow.cards[0].effects[0].used_once = 0;
        require(rejected(a_bow), "A still refuses even unused base Bow after B profile is added");
        auto no_bow_b = proof;
        no_bow_b.schema_version = ExactPlayerQualificationBowOnceSchemaVersion;
        no_bow_b.qualification_policy = ExactPlayerQualificationBowOncePolicy;
        auto plain_b_layout = qualified_zones;
        plain_b_layout.schema_version = ExactPlayerZonesBowOnceQualificationSchemaVersion;
        plain_b_layout.qualification_policy = ExactPlayerQualificationBowOncePolicy;
        plain_b_layout.payload_checksum = exact_player_zones_payload_checksum(plain_b_layout);
        no_bow_b.layout_payload_checksum = plain_b_layout.payload_checksum;
        no_bow_b.payload_checksum = exact_player_qualification_payload_checksum(no_bow_b);
        require(validate_exact_player_qualification_binding(no_bow_b, plain_b_layout, error)
                    && validate_exact_player_qualification_binding(proof, qualified_zones, error),
                "B fruit-only captures and unchanged explicit A captures remain separately supported");

        // Keep the old FIELD card whitelist: Bow is in HAND, fruits in FIELD.
        auto b_trash = qualified_trash;
        b_trash.schema_version = ExactPlayerTrashBowOnceQualificationSchemaVersion;
        b_trash.qualification_policy = ExactPlayerQualificationBowOncePolicy;
        b_trash.player_hand = b_zones.player_hand;
        b_trash.payload_checksum = exact_player_trash_payload_checksum(b_trash);
        auto b_trash_proof = b_proof;
        b_trash_proof.layout_kind = b_trash.kind;
        b_trash_proof.layout_payload_checksum = b_trash.payload_checksum;
        b_trash_proof.cards.push_back(trash_proof.cards.back());
        b_trash_proof.payload_checksum = exact_player_qualification_payload_checksum(b_trash_proof);
        require(validate_exact_player_qualification_binding(b_trash_proof, b_trash, error), "B T8 binds HAND Bows and unchanged native TRASH");
        auto b_field = qualified_field;
        b_field.schema_version = ExactPlayerFieldBowOnceQualificationSchemaVersion;
        b_field.qualification_policy = ExactPlayerQualificationBowOncePolicy;
        b_field.player_hand = b_zones.player_hand;
        b_field.payload_checksum = exact_player_field_payload_checksum(b_field);
        auto b_field_proof = b_trash_proof;
        b_field_proof.layout_kind = b_field.kind;
        b_field_proof.layout_payload_checksum = b_field.payload_checksum;
        b_field_proof.cards.push_back(field_proof.cards[field_proof.cards.size()-2]);
        b_field_proof.cards.push_back(field_proof.cards.back());
        b_field_proof.payload_checksum = exact_player_qualification_payload_checksum(b_field_proof);
        require(validate_exact_player_qualification_binding(b_field_proof, b_field, error), "B F11 binds HAND Bows and unchanged FIELD destinations");
        auto wrong_b_slot = b_field_proof;
        wrong_b_slot.cards.back().index = 2;
        require(bind_rejected(wrong_b_slot, b_field), "B qualification retains exact FIELD row/index binding");
        auto bowed_field = b_field;
        bowed_field.player_field = "(" + bow_instance + "," + lemon_instance + ")";
        require(!parse_exact_player_field_checkpoint(serialize_exact_player_field_checkpoint(bowed_field), error),
                "new Bow qualification does not broaden the existing plain-fruit FIELD reconstruction range");
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
    require(parsed_trash->schema_version == 3 && parsed_legacy_trash
                && parsed_legacy_trash->schema_version == 1,
            "legacy trash is read without claiming native order");
    auto native_v2_trash = trash;
    native_v2_trash.schema_version = 2;
    const auto parsed_v2_trash = parse_exact_player_trash_checkpoint(
        serialize_exact_player_trash_checkpoint(native_v2_trash),error);
    require(parsed_v2_trash && parsed_v2_trash->schema_version == 2
                && parsed_v2_trash->player_trash == trash.player_trash,
            "schema-2 native trash remains readable with its original collection policy");
    require(exact_player_trash_payload_checksum(native_v2_trash) != exact_player_trash_payload_checksum(trash),
            "schema-3 generated-card collection policy changes the trash checksum");

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
    const auto generated_all_trash = card_array({"naturalApple","naturalApple","naturalSpring"});
    require(!exact_player_trash_startup_decklist(generated_active,"()",generated_hand,
                generated_all_trash,error),"legacy trash startup refuses an additional Apple");
    require(exact_player_trash_startup_decklist(generated_active,"()",generated_hand,
                generated_all_trash,error,true,&additional_card_count).has_value() && additional_card_count == 1,
            "schema-3 trash can restore one generated Apple after FIELD is empty");
    require(exact_player_trash_startup_decklist(generated_active,card_array({"genericBattery"}),"()",
                card_array({"naturalApple","naturalApple","naturalApple","naturalSpring"}),error,true,&additional_card_count).has_value()
                && additional_card_count == 1,"generated trash retains empty-hand support and exact native order");
    require(!exact_player_trash_startup_decklist(generated_active,"()",generated_hand,"()",error,true,&additional_card_count)
                && additional_card_count == 0,"invalid empty trash clears the previous generated-card count");
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
    auto attested_field_staging_layout = full_hand_layout;
    attested_field_staging_layout.schema_version = ExactPlayerFieldSchemaVersion;
    const auto attested_field_staging = plan_player_hand_staging(
        attested_field_staging_layout, initial_full_hand, 7, error);
    require(attested_field_staging
                && attested_field_staging->moves.size() == full_hand_staging->moves.size()
                && attested_field_staging->before_field_move_count == full_hand_staging->before_field_move_count
                && std::equal(attested_field_staging->moves.begin(), attested_field_staging->moves.end(),
                    full_hand_staging->moves.begin(), [](const auto& left, const auto& right) {
                        return left.candidate == right.candidate && left.destination == right.destination;
                    }),
            "the membership-attested FIELD schema preserves the existing full-hand staging plan");
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

    auto hand_health = sample_exact_player_hand_health(attack_source);
    const auto hand_health_json = serialize_exact_player_hand_health_checkpoint(hand_health);
    const auto parsed_hand_health = parse_exact_player_hand_health_checkpoint(hand_health_json, error);
    require(parsed_hand_health && parsed_hand_health->schema_version == 1
                && parsed_hand_health->player_hand == hand_health.player_hand
                && parsed_hand_health->player_hand_health_states == hand_health.player_hand_health_states
                && parsed_hand_health->route_c_payload_checksum == attack_source.payload_checksum,
            "hand health preserves native order and distinct health for duplicate Apple identities");
    require(validate_exact_player_hand_health_checkpoint(*parsed_hand_health, error),
            "parsed hand-health checkpoint passes standalone validation");
    auto swapped_hand_health = hand_health;
    swapped_hand_health.player_hand_health_states = "2,2;2,3|mageOctavia_init,1,-1,0";
    require(swapped_hand_health.player_hand == hand_health.player_hand
                && exact_player_hand_health_payload_checksum(swapped_hand_health)
                    != exact_player_hand_health_payload_checksum(hand_health),
            "swapping duplicate hand-card health records changes the checksum");
    require(parse_exact_player_hand_health_checkpoint(
                serialize_exact_player_hand_health_checkpoint(swapped_hand_health), error).has_value(),
            "health alignment is native hand position and does not merge duplicate identities");

    // Missing sidecar files are handled by the runtime loader. The semantic Route C
    // and previous exact formats do not acquire a hand-health dependency.
    require(parse_route_c_checkpoint(serialize_route_c_checkpoint(attack_source), error).has_value()
                && parse_exact_player_field_checkpoint(field_v2_json, error).has_value(),
            "legacy semantic and field checkpoints remain valid without hand-health metadata");
    require(!parse_exact_player_hand_health_checkpoint("", error),
            "an empty hand-health file is invalid rather than an implicit default");

    auto invalid_hand_health = hand_health;
    for (const auto* invalid_records : {
             "2,3|mageOctavia_init,1,-1,0", "2,2;2,2;2,2", "2,2;0,0",
             "2,4|mageOctavia_init,1,-1,0;2,2", "2,1|negative,-1,-1,0;2,2",
             "2,3|mageOctavia_init,1,-1,8;2,2", "2,3|bad/tag,1,-1,0;2,2",
             "2,3|mageOctavia_init,1,-2,0;2,2", "2,4|same,1,-1,0|SAME,1,-1,0;2,2"})
    {
        invalid_hand_health.player_hand_health_states = invalid_records;
        require(!parse_exact_player_hand_health_checkpoint(
                    serialize_exact_player_hand_health_checkpoint(invalid_hand_health), error),
                "hand-health grammar rejects count, scalar, negative modifier, tag, flag and limit violations");
    }
    for (int flags = 0; flags <= 7; ++flags)
    {
        auto valid_flags = hand_health;
        valid_flags.player_hand_health_states =
            "2,3|mageOctavia_init,1,2," + std::to_string(flags) + ";2,2|zero,0,-1,0";
        const auto parsed_flags = parse_exact_player_hand_health_checkpoint(
            serialize_exact_player_hand_health_checkpoint(valid_flags), error);
        require(parsed_flags && parsed_flags->player_hand_health_states == valid_flags.player_hand_health_states,
                "hand health retains complete native flags, limit and explicit zero modifier");
    }
    for (const int count : {0, 1, 7, 8})
    {
        auto bounded_hand = hand_health;
        bounded_hand.player_hand = "(";
        bounded_hand.player_hand_health_states.clear();
        for (int index{}; index < count; ++index)
        {
            if (index != 0)
            {
                bounded_hand.player_hand += ',';
                bounded_hand.player_hand_health_states += ';';
            }
            bounded_hand.player_hand += "(CardInfo=(Tag=\"naturalApple\"))";
            bounded_hand.player_hand_health_states += "2,2";
        }
        bounded_hand.player_hand += ')';
        require(parse_exact_player_hand_health_checkpoint(
                    serialize_exact_player_hand_health_checkpoint(bounded_hand), error).has_value()
                    == (count >= 1 && count <= 7),
                "hand-health supplement supports exactly one through seven aligned hand cards");
    }
    invalid_hand_health = hand_health;
    invalid_hand_health.player_hand = "((CardInfo=(Tag=\"naturalApple\")),(upgradeLevel=0))";
    require(!parse_exact_player_hand_health_checkpoint(
                serialize_exact_player_hand_health_checkpoint(invalid_hand_health), error),
            "hand-health supplement rejects an invalid card instance");
    for (const int version : {0, ExactPlayerHandHealthSchemaVersion + 1})
    {
        invalid_hand_health = hand_health;
        invalid_hand_health.schema_version = version;
        require(!parse_exact_player_hand_health_checkpoint(
                    serialize_exact_player_hand_health_checkpoint(invalid_hand_health), error),
                "hand-health schema has no implicit legacy or future compatibility");
    }
    for (const auto member : {&ExactPlayerHandHealthCheckpoint::kind,
                             &ExactPlayerHandHealthCheckpoint::captured_at_utc,
                             &ExactPlayerHandHealthCheckpoint::route_c_payload_checksum,
                             &ExactPlayerHandHealthCheckpoint::game_executable_sha256,
                             &ExactPlayerHandHealthCheckpoint::source_level_name})
    {
        invalid_hand_health = hand_health;
        invalid_hand_health.*member = "";
        require(!parse_exact_player_hand_health_checkpoint(
                    serialize_exact_player_hand_health_checkpoint(invalid_hand_health), error),
                "hand-health supplement requires kind, capture, linkage, fingerprint and source level");
    }
    invalid_hand_health = hand_health;
    invalid_hand_health.route_c_payload_checksum = "G123456789ABCDEF";
    require(!parse_exact_player_hand_health_checkpoint(
                serialize_exact_player_hand_health_checkpoint(invalid_hand_health), error),
            "hand-health Route C linkage must be a hexadecimal checksum");
    invalid_hand_health = hand_health;
    invalid_hand_health.game_executable_size = 0;
    require(!parse_exact_player_hand_health_checkpoint(
                serialize_exact_player_hand_health_checkpoint(invalid_hand_health), error),
            "hand-health executable fingerprint requires a nonzero size");
    for (const int wave : {-1, 1001})
    {
        invalid_hand_health = hand_health;
        invalid_hand_health.wave_index = wave;
        require(!parse_exact_player_hand_health_checkpoint(
                    serialize_exact_player_hand_health_checkpoint(invalid_hand_health), error),
                "hand-health wave index retains bounded Route C semantics");
    }
    auto hand_health_tampering = hand_health_json;
    hand_health_tampering.replace(hand_health_tampering.find("mageOctavia_init"), 16, "mageOctavia_edit");
    require(!parse_exact_player_hand_health_checkpoint(hand_health_tampering, error)
                && error.find("checksum") != std::string::npos,
            "valid hand-health modifier grammar cannot conceal payload corruption");
    hand_health_tampering = hand_health_json;
    const auto linkage_offset = hand_health_tampering.find(attack_source.payload_checksum);
    hand_health_tampering[linkage_offset] = hand_health_tampering[linkage_offset] == '0' ? '1' : '0';
    require(!parse_exact_player_hand_health_checkpoint(hand_health_tampering, error)
                && error.find("checksum") != std::string::npos,
            "Route C linkage is included in hand-health payload integrity");
    auto missing_hand_health = hand_health_json;
    const auto hand_health_begin = missing_hand_health.find("  \"playerHandHealthStates\"");
    missing_hand_health.erase(hand_health_begin,
        missing_hand_health.find('\n', hand_health_begin) - hand_health_begin + 1);
    require(!parse_exact_player_hand_health_checkpoint(missing_hand_health, error),
            "hand-health records are mandatory in this supplement");

    // A fixed schema-1 fixture guards the pre-counter wire format byte for byte.
    auto legacy_hand_health = hand_health;
    legacy_hand_health.route_c_payload_checksum = "0123456789ABCDEF";
    const std::string legacy_hand_health_json = R"json({
  "schemaVersion": 1,
  "kind": "route-c-exact-player-hand-health",
  "capturedAtUtc": "2026-08-31T14:00:00Z",
  "routeCPayloadChecksum": "0123456789ABCDEF",
  "gameExecutableSha256": "0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6",
  "gameExecutableSize": 82718720,
  "sourceLevelName": "testDungeon",
  "waveIndex": 3,
  "playerHand": "((CardInfo=(Tag=\"naturalApple\")),(CardInfo=(Tag=\"naturalApple\")))",
  "playerHandHealthStates": "2,3|mageOctavia_init,1,-1,0;2,2",
  "payloadChecksum": "EC46AA3E7329C8FE"
}
)json";
    require(exact_player_hand_health_payload_checksum(legacy_hand_health) == "EC46AA3E7329C8FE"
                && serialize_exact_player_hand_health_checkpoint(legacy_hand_health) == legacy_hand_health_json,
            "schema-1 hand health retains its original checksum and exact serialized bytes");
    const auto parsed_legacy_hand_health = parse_exact_player_hand_health_checkpoint(legacy_hand_health_json, error);
    require(parsed_legacy_hand_health && parsed_legacy_hand_health->schema_version == 1
                && parsed_legacy_hand_health->player_hand_counter_states.empty()
                && serialize_exact_player_hand_health_checkpoint(*parsed_legacy_hand_health) == legacy_hand_health_json,
            "schema-1 parsing does not invent counter records or change its wire representation");
    auto legacy_with_hand_counters = *parsed_legacy_hand_health;
    legacy_with_hand_counters.player_hand_counter_states = "0;0";
    require(!validate_exact_player_hand_health_checkpoint(legacy_with_hand_counters, error)
                && error.find("legacy") != std::string::npos,
            "schema-1 objects cannot claim even default counter coverage");
    for (const std::string records : {"", "0;0"})
    {
        auto injected = legacy_hand_health_json;
        injected.insert(1, "\n  \"playerHandCounterStates\": \"" + records + "\",");
        require(!parse_exact_player_hand_health_checkpoint(injected, error)
                    && error.find("legacy") != std::string::npos,
                "schema-1 JSON rejects a counter property even when it is empty or all zero");
    }

    auto hand_with_counters = legacy_hand_health;
    hand_with_counters.schema_version = 2;
    hand_with_counters.player_hand_counter_states = "1;0";
    const auto hand_counter_json = serialize_exact_player_hand_health_checkpoint(hand_with_counters);
    const auto parsed_hand_counters = parse_exact_player_hand_health_checkpoint(hand_counter_json, error);
    require(parsed_hand_counters && parsed_hand_counters->schema_version == 2
                && parsed_hand_counters->player_hand == hand_with_counters.player_hand
                && parsed_hand_counters->player_hand_health_states == hand_with_counters.player_hand_health_states
                && parsed_hand_counters->player_hand_counter_states == "1;0"
                && validate_exact_player_hand_health_checkpoint(*parsed_hand_counters, error),
            "schema-2 preserves aligned health and generic counters for duplicate hand identities");
    require(exact_player_hand_health_payload_checksum(hand_with_counters) == "85193C32AD2F4164"
                && serialize_exact_player_hand_health_checkpoint(*parsed_hand_counters) == hand_counter_json,
            "schema-2 appends counters after the original hash fields and round trips byte for byte");
    auto swapped_hand_counters = hand_with_counters;
    swapped_hand_counters.player_hand_counter_states = "0;1";
    require(swapped_hand_counters.player_hand == hand_with_counters.player_hand
                && swapped_hand_counters.player_hand_health_states == hand_with_counters.player_hand_health_states
                && exact_player_hand_health_payload_checksum(swapped_hand_counters)
                    != exact_player_hand_health_payload_checksum(hand_with_counters)
                && parse_exact_player_hand_health_checkpoint(
                    serialize_exact_player_hand_health_checkpoint(swapped_hand_counters), error).has_value(),
            "counter assignments to duplicate hand identities remain positional and integrity checked");
    for (const int count : {0, PlayerCounterRestoreMaximum})
    {
        auto bounded = hand_with_counters;
        bounded.player_hand_counter_states = std::to_string(count) + ";" + std::to_string(count);
        require(parse_exact_player_hand_health_checkpoint(
                    serialize_exact_player_hand_health_checkpoint(bounded), error).has_value(),
                "hand generic counters allow zero and the existing native replay maximum");
    }
    for (const auto* records : {"", "0", "0;0;0", "-1;0", "0;-1", "257;0", "0;257",
                                "0|mdvRocket,0;0", "0|counter,1;0", "0|counter,-1;0",
                                "01;0", "0;", "+1;0", "2147483648;0"})
    {
        auto invalid = hand_with_counters;
        invalid.player_hand_counter_states = records;
        invalid.payload_checksum = exact_player_hand_health_payload_checksum(invalid);
        require(!validate_exact_player_hand_health_checkpoint(invalid, error)
                    && !parse_exact_player_hand_health_checkpoint(
                        serialize_exact_player_hand_health_checkpoint(invalid), error),
                "valid checksums cannot authorize misaligned, special, negative, excessive or malformed hand counters");
    }
    auto tampered_hand_counters = hand_counter_json;
    tampered_hand_counters.replace(tampered_hand_counters.find("1;0"), 3, "0;1");
    require(!parse_exact_player_hand_health_checkpoint(tampered_hand_counters, error)
                && error.find("checksum") != std::string::npos,
            "valid counter grammar cannot conceal a swapped native hand index");
    auto missing_hand_counters = hand_counter_json;
    const auto hand_counter_begin = missing_hand_counters.find("  \"playerHandCounterStates\"");
    missing_hand_counters.erase(hand_counter_begin,
        missing_hand_counters.find('\n', hand_counter_begin) - hand_counter_begin + 1);
    require(!parse_exact_player_hand_health_checkpoint(missing_hand_counters, error),
            "schema-2 requires counter records rather than assuming default counts");
    auto numeric_hand_counters = hand_counter_json;
    numeric_hand_counters.replace(numeric_hand_counters.find("\"1;0\""), 5, "0");
    require(!parse_exact_player_hand_health_checkpoint(numeric_hand_counters, error),
            "schema-2 counter records use the existing canonical string grammar");
    auto downgraded_hand_counters = hand_counter_json;
    const auto hand_version_begin = downgraded_hand_counters.find("\"schemaVersion\": ")
        + std::string{"\"schemaVersion\": "}.size();
    downgraded_hand_counters.replace(hand_version_begin, 1, "1");
    require(!parse_exact_player_hand_health_checkpoint(downgraded_hand_counters, error)
                && error.find("legacy") != std::string::npos,
            "downgrading a sidecar cannot hide newer hand counter claims");

    require(ExactPlayerHandHealthCheckpoint{}.schema_version == 3
                && ExactPlayerHandBaseHealthSchemaVersion == 3,
            "new HAND captures explicitly claim the bounded base-health policy");
    auto hand_with_base = hand_with_counters;
    hand_with_base.schema_version = 3;
    hand_with_base.player_hand_health_states = "3,4|mageOctavia_init,1,-1,0;2,2";
    const auto hand_base_json = serialize_exact_player_hand_health_checkpoint(hand_with_base);
    const auto parsed_hand_base = parse_exact_player_hand_health_checkpoint(hand_base_json, error);
    require(parsed_hand_base && parsed_hand_base->schema_version == 3
                && parsed_hand_base->player_hand == hand_with_base.player_hand
                && parsed_hand_base->player_hand_health_states == hand_with_base.player_hand_health_states
                && parsed_hand_base->player_hand_counter_states == hand_with_base.player_hand_counter_states
                && serialize_exact_player_hand_health_checkpoint(*parsed_hand_base) == hand_base_json,
            "HAND3 preserves duplicate positions with different bases plus HEALTH and generic counters");
    auto swapped_hand_base = hand_with_base;
    swapped_hand_base.player_hand_health_states = "2,2;3,4|mageOctavia_init,1,-1,0";
    require(exact_player_hand_health_payload_checksum(swapped_hand_base)
                != exact_player_hand_health_payload_checksum(hand_with_base)
                && parse_exact_player_hand_health_checkpoint(
                    serialize_exact_player_hand_health_checkpoint(swapped_hand_base), error).has_value(),
            "duplicate base-health assignments remain positional and checksum protected");
    auto hand3_unchanged = hand_with_counters;
    hand3_unchanged.schema_version = 3;
    const auto hand3_unchanged_json = serialize_exact_player_hand_health_checkpoint(hand3_unchanged);
    auto normalized_hand3_json = hand3_unchanged_json;
    normalized_hand3_json.replace(normalized_hand3_json.find("\"schemaVersion\": 3"),
        std::string{"\"schemaVersion\": 3"}.size(), "\"schemaVersion\": 2");
    normalized_hand3_json.replace(normalized_hand3_json.find(exact_player_hand_health_payload_checksum(hand3_unchanged)),
        16, exact_player_hand_health_payload_checksum(hand_with_counters));
    require(normalized_hand3_json == hand_counter_json,
            "HAND3 adds no payload fields and changes only the version and checksum for equal state");
    for (const auto& original : {hand_counter_json, hand3_unchanged_json})
    {
        auto tampered = original;
        const auto offset = tampered.find("\"schemaVersion\": ") + std::string{"\"schemaVersion\": "}.size();
        tampered[offset] = tampered[offset] == '2' ? '3' : '2';
        require(!parse_exact_player_hand_health_checkpoint(tampered, error)
                    && error.find("checksum") != std::string::npos,
                "HAND2/3 version-only promotion or demotion cannot forge base-health coverage");
    }
    auto legacy_grown_base = hand_with_base;
    legacy_grown_base.schema_version = 2;
    const auto parsed_legacy_grown = parse_exact_player_hand_health_checkpoint(
        serialize_exact_player_hand_health_checkpoint(legacy_grown_base), error);
    require(parsed_legacy_grown.has_value(),
            "wire parsing remains separate from the runtime immutable-definition policy");
    const auto legacy_grown_states = parse_player_health_states(parsed_legacy_grown->player_hand_health_states, error);
    require(legacy_grown_states && !validate_player_hand_health_for_definition(
                legacy_grown_states->front(), 2, parsed_legacy_grown->schema_version, error)
                && validate_player_hand_health_for_definition(legacy_grown_states->front(), 2, 3, error),
            "a recomputed legacy checksum still cannot authorize nondefault base health");
    auto missing_hand3_counters = hand_base_json;
    const auto hand3_counter_begin = missing_hand3_counters.find("  \"playerHandCounterStates\"");
    missing_hand3_counters.erase(hand3_counter_begin,
        missing_hand3_counters.find('\n', hand3_counter_begin) - hand3_counter_begin + 1);
    require(!parse_exact_player_hand_health_checkpoint(missing_hand3_counters, error),
            "HAND3 still requires aligned counter coverage from schema 2");

    require(ExactPlayerZonesCheckpoint{}.schema_version == 7
                && ExactPlayerTrashCheckpoint{}.schema_version == 8
                && ExactPlayerFieldCheckpoint{}.schema_version == 11
                && ExactPlayerQualificationCheckpoint{}.schema_version == 2
                && ExactPlayerQualificationBaselineSchemaVersion == 1
                && ExactPlayerZonesQualificationSchemaVersion == 6
                && ExactPlayerTrashQualificationSchemaVersion == 7
                && ExactPlayerFieldQualificationSchemaVersion == 10
                && ExactPlayerZonesBowOnceQualificationSchemaVersion == 7
                && ExactPlayerTrashBowOnceQualificationSchemaVersion == 8
                && ExactPlayerFieldBowOnceQualificationSchemaVersion == 11
                && ExactPlayerZonesBaseHealthSchemaVersion == 5
                && ExactPlayerTrashBaseHealthSchemaVersion == 6
                && ExactPlayerFieldBaseHealthSchemaVersion == 9
                && ExactPlayerZonesEffectMembershipSchemaVersion == 4
                && ExactPlayerTrashEffectMembershipSchemaVersion == 5
                && ExactPlayerFieldEffectMembershipSchemaVersion == 8,
            "qualification schemas preserve fixed base-health and effect attestation minima");
    const auto check_hand_health_dependency = [&](auto legacy_layout, int dependency_version,
                                                   auto serialize, auto parse, auto checksum) {
        const auto legacy_json = serialize(legacy_layout);
        const auto legacy_hash = checksum(legacy_layout);
        require(legacy_json.find("playerHandHealthChecksum") == std::string::npos
                    && parse(legacy_json, error).has_value(),
                "legacy layout JSON keeps its original fields and remains readable");
        auto legacy_with_marker = legacy_layout;
        legacy_with_marker.player_hand_health_checksum = parsed_hand_health->payload_checksum;
        require(checksum(legacy_with_marker) == legacy_hash
                    && serialize(legacy_with_marker) == legacy_json,
                "legacy layout hashing and serialization do not acquire a hand-health field");
        auto disguised_legacy = legacy_json;
        disguised_legacy.insert(disguised_legacy.rfind('}'),
            ",\"playerHandHealthChecksum\":\"" + parsed_hand_health->payload_checksum + "\"");
        require(!parse(disguised_legacy, error),
                "legacy layout JSON cannot carry an unchecked newer hand-health dependency");

        auto current = legacy_layout;
        current.schema_version = dependency_version;
        current.player_hand = parsed_hand_health->player_hand;
        current.player_hand_health_checksum = parsed_hand_health->payload_checksum;
        const auto current_json = serialize(current);
        const auto parsed = parse(current_json, error);
        require(parsed && parsed->player_hand_health_checksum == parsed_hand_health->payload_checksum
                    && parsed->player_hand == parsed_hand_health->player_hand,
                "latest layout persists the exact hand-health payload checksum and native hand order");
        require(current_json.find("playerHandHealthChecksum") != std::string::npos
                    && checksum(current) != legacy_hash,
                "the dependency participates in the latest layout payload checksum");
        auto alternate = current;
        alternate.player_hand_health_checksum = exact_player_hand_health_payload_checksum(swapped_hand_health);
        require(alternate.player_hand_health_checksum != current.player_hand_health_checksum
                    && checksum(alternate) != checksum(current),
                "different health assignments for duplicate hand cards change the layout dependency hash");
        auto counter_layout = current;
        counter_layout.player_hand_health_checksum = exact_player_hand_health_payload_checksum(hand_with_counters);
        auto swapped_counter_layout = counter_layout;
        swapped_counter_layout.player_hand_health_checksum = exact_player_hand_health_payload_checksum(swapped_hand_counters);
        require(parse(serialize(counter_layout), error).has_value()
                    && checksum(counter_layout) != checksum(swapped_counter_layout),
                "existing layout dependency versions bind schema-2 hand counter assignments without a layout bump");

        for (const std::string marker : {"", "123456789ABCDEF", "123456789ABCDEF01", "G123456789ABCDEF"})
        {
            auto invalid = current;
            invalid.player_hand_health_checksum = marker;
            require(!parse(serialize(invalid), error),
                    "a nonempty hand requires an exact sixteen-digit hexadecimal dependency");
        }
        auto missing = current_json;
        const auto marker_begin = missing.find("  \"playerHandHealthChecksum\"");
        missing.erase(marker_begin, missing.find('\n', marker_begin) - marker_begin + 1);
        require(!parse(missing, error),
                "missing dependency metadata cannot silently fall back to default hand health");
        auto damaged = current_json;
        const auto digest_begin = damaged.find(parsed_hand_health->payload_checksum);
        damaged[digest_begin] = damaged[digest_begin] == '0' ? '1' : '0';
        require(!parse(damaged, error) && error.find("checksum") != std::string::npos,
                "changing a valid dependency digest without changing the layout checksum is rejected");
        auto downgraded = current_json;
        const auto version_begin = downgraded.find("\"schemaVersion\": ") + std::string{"\"schemaVersion\": "}.size();
        downgraded.replace(version_begin, 1, std::to_string(dependency_version - 1));
        require(!parse(downgraded, error),
                "downgrading a layout cannot hide its required hand-health dependency");

        auto empty = current;
        empty.player_hand = "()";
        empty.player_hand_health_checksum.clear();
        require(parse(serialize(empty), error).has_value(),
                "latest empty-hand layouts require no sidecar and persist an empty dependency");
        empty.player_hand_health_checksum = parsed_hand_health->payload_checksum;
        require(!parse(serialize(empty), error),
                "an empty hand cannot claim an unrelated hand-health dependency");
        auto oversized = current;
        oversized.player_hand = "(";
        for (int index{}; index < 8; ++index)
        {
            if (index != 0) oversized.player_hand += ',';
            oversized.player_hand += "(CardInfo=(Tag=\"naturalApple\"))";
        }
        oversized.player_hand += ')';
        require(!parse(serialize(oversized), error),
                "latest layouts cannot require hand-health records beyond the supported seven cards");
    };
    check_hand_health_dependency(zones, 3,
        serialize_exact_player_zones_checkpoint, parse_exact_player_zones_checkpoint,
        exact_player_zones_payload_checksum);
    check_hand_health_dependency(trash, 4,
        serialize_exact_player_trash_checkpoint, parse_exact_player_trash_checkpoint,
        exact_player_trash_payload_checksum);
    check_hand_health_dependency(generated_schema, 7,
        serialize_exact_player_field_checkpoint, parse_exact_player_field_checkpoint,
        exact_player_field_payload_checksum);

    const auto check_effect_membership_schema = [&](auto layout, int legacy_version,
                                                      int attested_version, int proof_version,
                                                      auto serialize, auto parse, auto checksum) {
        layout.schema_version = legacy_version;
        layout.player_hand = parsed_hand_health->player_hand;
        layout.player_hand_health_checksum = parsed_hand_health->payload_checksum;
        const auto legacy_json = serialize(layout);
        const auto parsed_legacy = parse(legacy_json, error);
        require(parsed_legacy && parsed_legacy->schema_version == legacy_version
                    && parsed_legacy->schema_version < proof_version
                    && parsed_legacy->payload_checksum == checksum(layout),
                "previous latest layouts retain readable payloads without a saved-membership proof");

        auto attested = layout;
        attested.schema_version = attested_version;
        const auto attested_json = serialize(attested);
        const auto parsed_attested = parse(attested_json, error);
        require(parsed_attested && parsed_attested->schema_version >= proof_version
                    && parsed_attested->payload_checksum == checksum(attested)
                    && parsed_attested->player_hand_health_checksum == layout.player_hand_health_checksum
                    && serialize(*parsed_attested) == attested_json,
                "membership-attested layouts round trip with their existing HAND dependency");
        require(checksum(attested) != checksum(layout),
                "the membership attestation version participates in each layout payload checksum");
        auto base_layout = attested;
        base_layout.player_hand = parsed_hand_base->player_hand;
        base_layout.player_hand_health_checksum = parsed_hand_base->payload_checksum;
        const auto parsed_base_layout = parse(serialize(base_layout), error);
        auto swapped_base_layout = base_layout;
        swapped_base_layout.player_hand_health_checksum = exact_player_hand_health_payload_checksum(swapped_hand_base);
        require(parsed_base_layout && parsed_base_layout->schema_version == attested_version
                    && parsed_base_layout->player_hand_health_checksum == parsed_hand_base->payload_checksum
                    && checksum(base_layout) != checksum(swapped_base_layout),
                "existing attested Z4/T5/F8 layouts bind per-index HAND3 base health without another layout bump");

        const auto change_version = [](std::string json, int version) {
            const auto begin = json.find("\"schemaVersion\": ") + std::string{"\"schemaVersion\": "}.size();
            const auto end = json.find(',', begin);
            json.replace(begin, end - begin, std::to_string(version));
            return json;
        };
        auto promoted = change_version(legacy_json, attested_version);
        error.clear();
        require(!parse(promoted, error) && error.find("checksum") != std::string::npos,
                "editing only a legacy schema version cannot add a saved-membership proof");
        error.clear();
        require(!parse(change_version(attested_json, legacy_version), error)
                    && error.find("checksum") != std::string::npos,
                "editing only the attested schema version cannot discard its integrity binding");
        const auto digest_begin = promoted.find("\"payloadChecksum\": \"")
            + std::string{"\"payloadChecksum\": \""}.size();
        promoted.replace(digest_begin, parsed_legacy->payload_checksum.size(), parsed_attested->payload_checksum);
        require(promoted == attested_json,
                "membership attestation changes only schema and checksum, with no extra payload fields");

        auto missing_dependency = attested;
        missing_dependency.player_hand_health_checksum.clear();
        require(!parse(serialize(missing_dependency), error),
                "new proof versions still require a linked sidecar for a nonempty HAND");
        auto empty_hand = attested;
        empty_hand.player_hand = "()";
        empty_hand.player_hand_health_checksum.clear();
        require(parse(serialize(empty_hand), error).has_value(),
                "new proof versions allow an empty HAND without inventing a dependency");
        empty_hand.player_hand_health_checksum = attested.player_hand_health_checksum;
        require(!parse(serialize(empty_hand), error),
                "new proof versions reject a sidecar dependency on an empty HAND");
        require(!parse(change_version(attested_json, decltype(layout){}.schema_version + 1), error)
                    && error.find("schema") != std::string::npos,
                "future layout versions remain unsupported rather than inheriting capture-proof claims");
    };
    check_effect_membership_schema(zones, 3, ExactPlayerZonesEffectMembershipSchemaVersion,
        ExactPlayerZonesEffectMembershipSchemaVersion,
        serialize_exact_player_zones_checkpoint, parse_exact_player_zones_checkpoint,
        exact_player_zones_payload_checksum);
    check_effect_membership_schema(trash, 4, ExactPlayerTrashEffectMembershipSchemaVersion,
        ExactPlayerTrashEffectMembershipSchemaVersion,
        serialize_exact_player_trash_checkpoint, parse_exact_player_trash_checkpoint,
        exact_player_trash_payload_checksum);
    check_effect_membership_schema(generated_schema, 7, ExactPlayerFieldEffectMembershipSchemaVersion,
        ExactPlayerFieldEffectMembershipSchemaVersion,
        serialize_exact_player_field_checkpoint, parse_exact_player_field_checkpoint,
        exact_player_field_payload_checksum);

    const auto check_off_field_base_health = [&](auto layout, int version,
                                                  auto serialize, auto parse, auto checksum, auto validate) {
        layout.schema_version = version - 1;
        layout.player_hand = parsed_hand_base->player_hand;
        layout.player_hand_health_checksum = parsed_hand_base->payload_checksum;
        const auto legacy_json = serialize(layout);
        const auto legacy_hash = checksum(layout);
        require(parse(legacy_json,error).has_value()
                    && legacy_json.find("BaseHealthStates") == std::string::npos,
                "previous layouts retain their exact fields and default-only DECK/TRASH meaning");
        auto hidden_legacy = layout;
        hidden_legacy.player_deck_base_health_states = "2,2";
        hidden_legacy.payload_checksum = checksum(hidden_legacy);
        require(checksum(hidden_legacy) == legacy_hash && serialize(hidden_legacy) == legacy_json
                    && !validate(hidden_legacy,error),
                "legacy hashing stays unchanged but structural validation rejects hidden DECK coverage");
        if constexpr (requires { layout.player_trash_base_health_states; })
        {
            hidden_legacy = layout;
            hidden_legacy.player_trash_base_health_states = "2,2";
            hidden_legacy.payload_checksum = checksum(hidden_legacy);
            require(checksum(hidden_legacy) == legacy_hash && serialize(hidden_legacy) == legacy_json
                        && !validate(hidden_legacy,error),
                    "legacy structural validation rejects hidden TRASH coverage independently");
        }
        for (const std::string key : {"playerDeckBaseHealthStates", "playerTrashBaseHealthStates"})
        {
            if constexpr (!requires { layout.player_trash_base_health_states; })
            {
                if (key == "playerTrashBaseHealthStates") continue;
            }
            for (const std::string value : {"", "2,2"})
            {
                auto injected = legacy_json;
                injected.insert(injected.rfind('}'),",\"" + key + "\":\"" + value + "\"");
                require(!parse(injected,error),
                        "legacy JSON rejects even an empty newer base-health field");
            }
        }

        layout.schema_version = version;
        layout.player_deck = card_array({"mageSorcIgnis", "mageSorcIgnis"});
        layout.player_deck_base_health_states = "1,1;2,2";
        if constexpr (requires { layout.player_trash_base_health_states; })
        {
            layout.player_trash = card_array({"mageSorcIgnis", "mageSorcIgnis"});
            layout.player_trash_base_health_states = "3,3;1,1";
        }
        const auto current_json = serialize(layout);
        const auto current = parse(current_json,error);
        require(current && current->schema_version == version
                    && current->player_deck == layout.player_deck
                    && current->player_deck_base_health_states == "1,1;2,2"
                    && current->player_hand_health_checksum == parsed_hand_base->payload_checksum
                    && serialize(*current) == current_json,
                "new layout binds native DECK indexes to base records and preserves the independent HAND3 link");
        if constexpr (requires { layout.player_trash_base_health_states; })
            require(current->player_trash == layout.player_trash
                        && current->player_trash_base_health_states == "3,3;1,1",
                    "TRASH base records round trip in insertion order with duplicate card definitions");
        auto swapped = layout;
        swapped.player_deck_base_health_states = "2,2;1,1";
        require(checksum(swapped) != checksum(layout) && parse(serialize(swapped),error).has_value(),
                "swapping base values between identical DECK cards changes the layout checksum");
        if constexpr (requires { layout.player_trash_base_health_states; })
        {
            swapped = layout;
            swapped.player_trash_base_health_states = "1,1;3,3";
            require(checksum(swapped) != checksum(layout) && parse(serialize(swapped),error).has_value(),
                    "swapping base values between identical TRASH cards changes the layout checksum");
        }
        auto tampered = current_json;
        tampered.replace(tampered.find("1,1;2,2"),7,"2,2;1,1");
        require(!parse(tampered,error) && error.find("checksum") != std::string::npos,
                "changing aligned base records without recomputing the payload checksum is rejected");

        for (const auto invalid : {"", "1,1", "1,1;2,2;3,3", "1,1;0,0", "1,1;100001,100001",
                                   "1,1;2,3", "1,1;2,3|buff,1,-1,0", "1,1;2,2|expired,0,-1,0",
                                   "1,1;2,2;", "1,1;02,02"})
        {
            auto broken = layout;
            broken.player_deck_base_health_states = invalid;
            require(!parse(serialize(broken),error),
                    "correct checksums cannot authorize invalid or misaligned DECK base records");
            if constexpr (requires { layout.player_trash_base_health_states; })
            {
                broken = layout;
                broken.player_trash_base_health_states = invalid;
                require(!parse(serialize(broken),error),
                        "correct checksums cannot authorize invalid or misaligned TRASH base records");
            }
        }
        for (const std::string key : {"playerDeckBaseHealthStates", "playerTrashBaseHealthStates"})
        {
            if constexpr (!requires { layout.player_trash_base_health_states; })
            {
                if (key == "playerTrashBaseHealthStates") continue;
            }
            auto missing = current_json;
            const auto begin = missing.find("  \"" + key + "\"");
            missing.erase(begin,missing.find('\n',begin) - begin + 1);
            require(!parse(missing,error), "new layouts require explicit base-health fields even for default cards");
        }
        auto empty_deck = layout;
        empty_deck.player_deck = "()";
        empty_deck.player_deck_base_health_states.clear();
        require(parse(serialize(empty_deck),error).has_value(),
                "empty DECK aligns with an explicitly empty base-record string");
        empty_deck.player_deck_base_health_states = "1,1";
        require(!parse(serialize(empty_deck),error), "empty DECK cannot carry a stray numeric record");
        if constexpr (requires { layout.player_trash_base_health_states; layout.player_field; })
        {
            auto empty_trash = layout;
            empty_trash.player_trash = "()";
            empty_trash.player_trash_base_health_states.clear();
            require(parse(serialize(empty_trash),error).has_value(),
                    "FIELD layouts can have empty TRASH and an explicitly empty record string");
            empty_trash.player_trash_base_health_states = "1,1";
            require(!parse(serialize(empty_trash),error), "empty TRASH cannot carry a stray numeric record");
        }
        auto large = layout;
        large.player_deck = "(";
        large.player_deck_base_health_states.clear();
        for (int index{}; index < 11; ++index)
        {
            if (index != 0)
            {
                large.player_deck += ',';
                large.player_deck_base_health_states += ';';
            }
            large.player_deck += "(CardInfo=(Tag=\"mageSorcIgnis\"))";
            large.player_deck_base_health_states += "2,2";
        }
        large.player_deck += ')';
        require(parse(serialize(large),error).has_value(),
                "layout parsing does not impose the FIELD ten-slot limit on DECK numeric records");
        auto future = layout;
        if constexpr (requires { layout.player_field; }) future.schema_version = ExactPlayerFieldSchemaVersion + 1;
        else if constexpr (requires { layout.player_trash; }) future.schema_version = ExactPlayerTrashSchemaVersion + 1;
        else future.schema_version = ExactPlayerZonesSchemaVersion + 1;
        require(!parse(serialize(future),error) && error.find("schema") != std::string::npos,
                "unknown later versions cannot silently expand base-health coverage");
    };
    check_off_field_base_health(zones,ExactPlayerZonesBaseHealthSchemaVersion,
        serialize_exact_player_zones_checkpoint,parse_exact_player_zones_checkpoint,
        exact_player_zones_payload_checksum,validate_exact_player_zones_checkpoint);
    check_off_field_base_health(trash,ExactPlayerTrashBaseHealthSchemaVersion,
        serialize_exact_player_trash_checkpoint,parse_exact_player_trash_checkpoint,
        exact_player_trash_payload_checksum,validate_exact_player_trash_checkpoint);
    check_off_field_base_health(generated_schema,ExactPlayerFieldBaseHealthSchemaVersion,
        serialize_exact_player_field_checkpoint,parse_exact_player_field_checkpoint,
        exact_player_field_payload_checksum,validate_exact_player_field_checkpoint);

    run_qualification_tests();
    std::cout << "Route C checkpoint persistence tests passed\n";
    return 0;
}
