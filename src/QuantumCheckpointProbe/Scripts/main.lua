local MOD = "[QuantumCheckpointProbe]"

local scan_in_progress = false
local seen_names = {}
local checkpoint = nil
local restore_in_progress = false
local board_reset_test_in_progress = false
local board_reset_test_completed = false
local player_cards_load_test_in_progress = false

local object_terms = {
    "spawncontroller",
    "cardengine",
    "bottombar",
    "bp_spawncontroller",
    "bp_gameoverender",
    "bp_dungeoncompleteender",
    "umg_gameoveroptions",
    "bp_controllerboard",
    "bp_controllerdeck",
    "bp_controllerhand",
    "bp_controllerstorage",
    "bp_controllertrash",
    "bp_controllerpendingcards",
    "bp_controllerenemypending",
}

local function_terms = {
    "onwavestart",
    "onfinalwavestart",
    "spawnnextwave",
    "spawnwaveindex",
    "onhealthchanged",
    "onlastwavetriggered",
    "onlevelended",
    "onshowgameover_event_0",
    "resetgamestate",
    "resetplayerboard",
}

local property_names = {
    "currentWaveIndex",
    "lastWaveIndex",
    "currentTurnCountdown",
    "waveIndex",
    "spawnWaveIndex",
    "waveNumber",
    "waveCountdownPenalty",
    "health",
    "currentHealth",
    "maxHealth",
    "currentGameState",
    "deckRun",
    "deckrunId",
    "deck",
    "hand",
    "storage",
    "trash",
    "pending",
    "levelName",
    "levelTag",
    "levelToLoad",
}

local function log(message)
    print(string.format("%s %s\n", MOD, message))
end

local function contains_any(value, terms)
    local lowered = string.lower(value)
    for _, term in ipairs(terms) do
        if string.find(lowered, term, 1, true) then
            return true
        end
    end
    return false
end

local function safe_full_name(object)
    local ok, value = pcall(function()
        return object:GetFullName()
    end)
    if ok and value then
        return value
    end
    return "<unavailable>"
end

local function unwrap_context(context)
    local ok, value = pcall(function()
        return context:get()
    end)
    if ok and value then
        return value
    end
    return context
end

local function unwrap_value(value)
    local ok_type, wrapper_type = pcall(function()
        return value:type()
    end)
    if ok_type and (wrapper_type == "RemoteUnrealParam" or wrapper_type == "LocalUnrealParam") then
        local ok_get, unwrapped = pcall(function()
            return value:get()
        end)
        if ok_get then
            return unwrapped
        end
    end
    return value
end

local function printable_value(value)
    value = unwrap_value(value)
    local value_type = type(value)
    if value_type == "nil" or value_type == "number" or value_type == "boolean" or value_type == "string" then
        return tostring(value)
    end

    local ok, full_name = pcall(function()
        return value:GetFullName()
    end)
    if ok and full_name then
        return full_name
    end

    local ok_length, length = pcall(function()
        return #value
    end)
    if ok_length then
        return string.format("%s(length=%s)", value_type, tostring(length))
    end

    return string.format("%s(%s)", value_type, tostring(value))
end

local function fname_to_string(value)
    value = unwrap_value(value)
    local ok, result = pcall(function()
        return value:ToString()
    end)
    if ok and result then
        return result
    end
    return printable_value(value)
end

local function has_property(object, property_name)
    local ok, property = pcall(function()
        return object:Reflection():GetProperty(property_name)
    end)
    if not ok or not property then
        return false
    end

    local ok_valid, is_valid = pcall(function()
        return property:IsValid()
    end)
    return ok_valid and is_valid
end

local function log_known_properties(object, full_name)
    for _, property_name in ipairs(property_names) do
        if has_property(object, property_name) then
            local ok, value = pcall(function()
                return object:GetPropertyValue(property_name)
            end)
            if ok then
                log(string.format("PROPERTY %s.%s = %s", full_name, property_name, printable_value(value)))
            else
                log(string.format("PROPERTY_READ_FAILED %s.%s error=%s", full_name, property_name, tostring(value)))
            end
        end
    end
end

local function read_property(object, property_name)
    if not object or not object:IsValid() or not has_property(object, property_name) then
        return nil
    end

    local ok, value = pcall(function()
        return object:GetPropertyValue(property_name)
    end)
    if not ok then
        return nil
    end
    return unwrap_value(value)
end

local function is_live_level_instance(full_name)
    return string.find(full_name, ":PersistentLevel%.") ~= nil
        and string.find(full_name, "Default__", 1, true) == nil
        and string.find(full_name, "_GEN_VARIABLE", 1, true) == nil
end

local function find_battle_objects()
    local found = {
        trash = {},
    }

    ForEachUObject(function(object)
        if not object or not object:IsValid() then
            return
        end

        local full_name = safe_full_name(object)
        if not is_live_level_instance(full_name) then
            return
        end

        if string.find(full_name, "^BP_CardEngine_C%s") then
            found.card_engine = object
        elseif string.find(full_name, "^BP_BottomBar_C%s") then
            found.bottom_bar = object
        elseif string.find(full_name, "^BP_ControllerDeck_C%s") then
            found.deck = object
        elseif string.find(full_name, "^BP_ControllerHand_C%s") then
            found.hand = object
        elseif string.find(full_name, "^BP_ControllerStorage_C%s") then
            found.storage = object
        elseif string.find(full_name, "^BP_ControllerPendingCards_C%s") then
            found.pending = object
        elseif string.find(full_name, "^BP_ControllerEnemyPending_C%s") then
            found.enemy_pending = object
        elseif string.find(full_name, "^BP_ControllerBoard_C%s") then
            found.board = object
        elseif string.find(full_name, "^BP_ControllerTrash_C%s") then
            table.insert(found.trash, object)
        elseif string.find(string.lower(full_name), "spawner_c ", 1, true)
                and has_property(object, "currentWaveIndex") then
            found.spawner = object
        end
    end)

    return found
end

local function capture_card_group(label, object)
    local result = {
        label = label,
        object_name = object and safe_full_name(object) or "<missing>",
        cards = {},
    }

    if not object or not object:IsValid() then
        log("CHECKPOINT_ZONE_MISSING " .. label)
        return result
    end

    local ok, card_array = pcall(function()
        return object:getCardInstanceListSorted()
    end)
    if not ok or not card_array then
        log(string.format("CHECKPOINT_ZONE_FAILED %s error=%s", label, tostring(card_array)))
        return result
    end

    local function append_instance(index, value)
        if type(value) == "function" then
            value = value()
        end
        local instance = unwrap_value(value)
        local card = {
            index = index,
            upgrade_level = tonumber(instance.upgradeLevel) or 0,
            tag = "<unknown>",
        }

        local card_info = instance.CardInfo
        if card_info then
            card.tag = fname_to_string(card_info.Tag)
        end
        table.insert(result.cards, card)
    end

    local ok_iterate, iterate_error = pcall(function()
        if type(card_array) == "table" then
            log(string.format("CHECKPOINT_ZONE_RETURN %s type=table length=%d", label, #card_array))
            for index, instance in ipairs(card_array) do
                append_instance(index, instance)
            end
            return
        end

        local ok_type, array_type = pcall(function() return card_array:type() end)
        log(string.format(
            "CHECKPOINT_ZONE_RETURN %s type=%s",
            label,
            ok_type and tostring(array_type) or type(card_array)
        ))
        card_array:ForEach(function(index, element)
            append_instance(index, element)
        end)
    end)

    if not ok_iterate then
        log(string.format("CHECKPOINT_ZONE_ITERATE_FAILED %s error=%s", label, tostring(iterate_error)))
        return result
    end

    local card_labels = {}
    for _, card in ipairs(result.cards) do
        table.insert(card_labels, string.format("%s@%d", card.tag, card.upgrade_level))
    end
    log(string.format("CHECKPOINT_ZONE %s count=%d cards=[%s]", label, #result.cards, table.concat(card_labels, ",")))
    return result
end

local function capture_ingame_cards()
    local captured = {}
    local objects = FindAllOf("BP_InGameCard_C") or {}

    for _, card in ipairs(objects) do
        if card and card:IsValid() and is_live_level_instance(safe_full_name(card)) then
            local entry = {
                object_name = safe_full_name(card),
            }

            local ok_tag, tag = pcall(function() return card:getTag() end)
            local ok_location, location = pcall(function() return card:getCardLocation() end)
            local ok_health, health = pcall(function() return card:getCurrentHealth() end)
            local ok_turn, turn = pcall(function() return card:getCurrentTurnCounter() end)
            local placement = {
                index = nil,
                row = nil,
                side = nil,
            }
            local placement_component = read_property(card, "CardPlacementComponent")
            if placement_component and placement_component:IsValid() then
                local ok_slot, slot = pcall(function()
                    return placement_component:getPlacedFieldSlot()
                end)
                slot = ok_slot and unwrap_value(slot) or nil
                if slot and slot:IsValid() then
                    placement.index = read_property(slot, "SlotIndex")
                    placement.row = read_property(slot, "rowType")
                    placement.side = read_property(slot, "boardSide")
                end
            end

            entry.tag = ok_tag and fname_to_string(tag) or "<unknown>"
            entry.location = ok_location and tonumber(location) or nil
            entry.health = ok_health and tonumber(health) or nil
            entry.turn_counter = ok_turn and tonumber(turn) or nil
            entry.placement = placement
            table.insert(captured, entry)
            log(string.format(
                "CHECKPOINT_CARD tag=%s location=%s health=%s turn=%s field_slot=%s/%s/%s object=%s",
                entry.tag,
                tostring(entry.location),
                tostring(entry.health),
                tostring(entry.turn_counter),
                tostring(entry.placement.index),
                tostring(entry.placement.row),
                tostring(entry.placement.side),
                entry.object_name
            ))
        end
    end

    log(string.format("CHECKPOINT_INGAME_CARDS count=%d", #captured))
    return captured
end

local function zone_signature(zone)
    local values = {}
    for _, card in ipairs(zone and zone.cards or {}) do
        table.insert(values, string.format("%s@%d", card.tag, card.upgrade_level))
    end
    return table.concat(values, ",")
end

local function inspect_checkpoint_delta()
    if not checkpoint then
        log("CHECKPOINT_COMPARE_FAILED no checkpoint saved")
        return
    end

    local objects = find_battle_objects()
    if not objects.spawner or safe_full_name(objects.spawner) ~= checkpoint.spawner_name then
        log("CHECKPOINT_COMPARE_FAILED checkpoint belongs to a different battle instance")
        return
    end

    local current_zones = {
        deck = capture_card_group("compare_deck", objects.deck),
        hand = capture_card_group("compare_hand", objects.hand),
        storage = capture_card_group("compare_storage", objects.storage),
        pending = capture_card_group("compare_pending", objects.pending),
        enemy_pending = capture_card_group("compare_enemy_pending", objects.enemy_pending),
    }
    for index, trash_object in ipairs(objects.trash) do
        current_zones["trash_" .. index] = capture_card_group("compare_trash_" .. index, trash_object)
    end

    local labels = { "deck", "hand", "storage", "pending", "enemy_pending", "trash_1", "trash_2" }
    local changed = 0
    for _, label in ipairs(labels) do
        local before = zone_signature(checkpoint.zones[label])
        local after = zone_signature(current_zones[label])
        local is_changed = before ~= after
        if is_changed then
            changed = changed + 1
        end
        log(string.format(
            "CHECKPOINT_COMPARE_ZONE zone=%s changed=%s saved=[%s] current=[%s]",
            label,
            tostring(is_changed),
            before,
            after
        ))
    end

    local cards = capture_ingame_cards()
    log(string.format(
        "CHECKPOINT_COMPARE_RESULT changed_zones=%d live_cards=%d health=%s saved_health=%s",
        changed,
        #cards,
        tostring(read_property(objects.bottom_bar, "currentHealth")),
        tostring(checkpoint.current_health)
    ))
end

local function capture_checkpoint(reason)
    local objects = find_battle_objects()
    if not objects.card_engine or not objects.spawner then
        log("CHECKPOINT_SAVE_FAILED no active battle objects")
        return
    end

    local snapshot = {
        reason = reason,
        spawner_name = safe_full_name(objects.spawner),
        wave_index = read_property(objects.spawner, "currentWaveIndex"),
        last_wave_index = read_property(objects.spawner, "lastWaveIndex"),
        turn_countdown = read_property(objects.spawner, "currentTurnCountdown"),
        current_health = read_property(objects.bottom_bar, "currentHealth"),
        max_health = read_property(objects.bottom_bar, "maxHealth"),
        game_state = read_property(objects.card_engine, "currentGameState"),
        zones = {},
    }

    snapshot.zones.deck = capture_card_group("deck", objects.deck)
    snapshot.zones.hand = capture_card_group("hand", objects.hand)
    snapshot.zones.storage = capture_card_group("storage", objects.storage)
    snapshot.zones.pending = capture_card_group("pending", objects.pending)
    snapshot.zones.enemy_pending = capture_card_group("enemy_pending", objects.enemy_pending)
    for index, trash_object in ipairs(objects.trash) do
        snapshot.zones["trash_" .. index] = capture_card_group("trash_" .. index, trash_object)
    end
    snapshot.ingame_cards = capture_ingame_cards()

    checkpoint = snapshot
    board_reset_test_completed = false
    log(string.format(
        "CHECKPOINT_SAVED reason=%s wave=%s last_wave=%s countdown=%s health=%s/%s game_state=%s spawner=%s",
        tostring(reason),
        tostring(snapshot.wave_index),
        tostring(snapshot.last_wave_index),
        tostring(snapshot.turn_countdown),
        tostring(snapshot.current_health),
        tostring(snapshot.max_health),
        tostring(snapshot.game_state),
        snapshot.spawner_name
    ))
end

local function restore_checkpoint_health()
    if restore_in_progress then
        log("CHECKPOINT_RESTORE_SKIPPED already running")
        return
    end
    if not checkpoint then
        log("CHECKPOINT_RESTORE_FAILED no checkpoint saved")
        return
    end

    local objects = find_battle_objects()
    if not objects.card_engine or not objects.bottom_bar or not objects.spawner then
        log("CHECKPOINT_RESTORE_FAILED no active battle objects")
        return
    end
    if safe_full_name(objects.spawner) ~= checkpoint.spawner_name then
        log("CHECKPOINT_RESTORE_FAILED checkpoint belongs to a different battle instance")
        return
    end

    local current_health = read_property(objects.bottom_bar, "currentHealth")
    local target_health = checkpoint.current_health
    if type(current_health) ~= "number" or type(target_health) ~= "number" then
        log("CHECKPOINT_RESTORE_FAILED health value unavailable")
        return
    end

    local delta = target_health - current_health
    if delta < 0 then
        log(string.format(
            "CHECKPOINT_RESTORE_FAILED current health %d is above checkpoint health %d; damage restoration is not enabled",
            current_health,
            target_health
        ))
        return
    end

    restore_in_progress = true
    local ok, restore_error = pcall(function()
        if delta > 0 then
            objects.card_engine:healHealth(delta)
        end
    end)
    restore_in_progress = false

    if not ok then
        log("CHECKPOINT_RESTORE_FAILED health error=" .. tostring(restore_error))
        return
    end

    log(string.format(
        "CHECKPOINT_RESTORE_HEALTH_REQUESTED before=%d target=%d delta=%d",
        current_health,
        target_health,
        delta
    ))
    ExecuteWithDelay(750, function()
        ExecuteInGameThread(function()
            local refreshed = find_battle_objects()
            local after_health = read_property(refreshed.bottom_bar, "currentHealth")
            log(string.format(
                "CHECKPOINT_RESTORE_HEALTH_RESULT target=%d actual=%s",
                target_health,
                tostring(after_health)
            ))
        end)
    end)
end

local function test_reset_player_board()
    if board_reset_test_in_progress then
        log("PLAYER_BOARD_RESET_TEST_SKIPPED already running")
        return
    end
    if not checkpoint then
        log("PLAYER_BOARD_RESET_TEST_FAILED save a checkpoint with Ctrl+F5 first")
        return
    end

    local objects = find_battle_objects()
    if not objects.card_engine or not objects.spawner then
        log("PLAYER_BOARD_RESET_TEST_FAILED no active battle objects")
        return
    end
    if safe_full_name(objects.spawner) ~= checkpoint.spawner_name then
        log("PLAYER_BOARD_RESET_TEST_FAILED checkpoint belongs to a different battle instance")
        return
    end

    board_reset_test_in_progress = true
    board_reset_test_completed = false
    log(string.format(
        "PLAYER_BOARD_RESET_TEST_BEGIN saved_deck=%d saved_hand=%d saved_health=%s",
        #(checkpoint.zones.deck.cards or {}),
        #(checkpoint.zones.hand.cards or {}),
        tostring(checkpoint.current_health)
    ))

    local ok, reset_error = pcall(function()
        objects.card_engine:resetPlayerBoard()
    end)
    if not ok then
        board_reset_test_in_progress = false
        log("PLAYER_BOARD_RESET_TEST_CALL_FAILED error=" .. tostring(reset_error))
        return
    end

    log("PLAYER_BOARD_RESET_TEST_CALL_RETURNED")
    ExecuteWithDelay(1500, function()
        ExecuteInGameThread(function()
            local refreshed = find_battle_objects()
            local deck = capture_card_group("reset_result_deck", refreshed.deck)
            local hand = capture_card_group("reset_result_hand", refreshed.hand)
            local storage = capture_card_group("reset_result_storage", refreshed.storage)
            local cards = capture_ingame_cards()
            log(string.format(
                "PLAYER_BOARD_RESET_TEST_RESULT deck=%d hand=%d storage=%d live_cards=%d health=%s saved_deck=[%s] current_deck=[%s] saved_hand=[%s] current_hand=[%s]",
                #deck.cards,
                #hand.cards,
                #storage.cards,
                #cards,
                tostring(read_property(refreshed.bottom_bar, "currentHealth")),
                zone_signature(checkpoint.zones.deck),
                zone_signature(deck),
                zone_signature(checkpoint.zones.hand),
                zone_signature(hand)
            ))
            board_reset_test_in_progress = false
            board_reset_test_completed = true
        end)
    end)
end

local function test_load_player_cards_start()
    if player_cards_load_test_in_progress then
        log("PLAYER_CARDS_LOAD_TEST_SKIPPED already running")
        return
    end
    if not checkpoint then
        log("PLAYER_CARDS_LOAD_TEST_FAILED save a checkpoint with Ctrl+F5 first")
        return
    end
    if not board_reset_test_completed then
        log("PLAYER_CARDS_LOAD_TEST_FAILED run Ctrl+F3 and wait for its result first")
        return
    end

    local objects = find_battle_objects()
    if not objects.card_engine or not objects.spawner then
        log("PLAYER_CARDS_LOAD_TEST_FAILED no active battle objects")
        return
    end
    if safe_full_name(objects.spawner) ~= checkpoint.spawner_name then
        log("PLAYER_CARDS_LOAD_TEST_FAILED checkpoint belongs to a different battle instance")
        return
    end

    player_cards_load_test_in_progress = true
    log("PLAYER_CARDS_LOAD_TEST_BEGIN calling BP_CardEngine.LoadPlayerCardsStart with no arguments")
    local ok, load_error = pcall(function()
        objects.card_engine:LoadPlayerCardsStart()
    end)
    if not ok then
        player_cards_load_test_in_progress = false
        log("PLAYER_CARDS_LOAD_TEST_CALL_FAILED error=" .. tostring(load_error))
        return
    end

    log("PLAYER_CARDS_LOAD_TEST_CALL_RETURNED")
    ExecuteWithDelay(2500, function()
        ExecuteInGameThread(function()
            local refreshed = find_battle_objects()
            local deck = capture_card_group("load_result_deck", refreshed.deck)
            local hand = capture_card_group("load_result_hand", refreshed.hand)
            local storage = capture_card_group("load_result_storage", refreshed.storage)
            local cards = capture_ingame_cards()
            log(string.format(
                "PLAYER_CARDS_LOAD_TEST_RESULT deck=%d hand=%d storage=%d live_cards=%d health=%s saved_deck=[%s] current_deck=[%s] saved_hand=[%s] current_hand=[%s]",
                #deck.cards,
                #hand.cards,
                #storage.cards,
                #cards,
                tostring(read_property(refreshed.bottom_bar, "currentHealth")),
                zone_signature(checkpoint.zones.deck),
                zone_signature(deck),
                zone_signature(checkpoint.zones.hand),
                zone_signature(hand)
            ))
            player_cards_load_test_in_progress = false
        end)
    end)
end

local function is_definition(full_name)
    return string.find(full_name, "^Function%s") ~= nil
        or string.find(full_name, "^Class%s") ~= nil
        or string.find(full_name, "^BlueprintGeneratedClass%s") ~= nil
end

local function is_function(full_name)
    return string.find(full_name, "^Function%s") ~= nil
end

local function scan_loaded_objects(reason)
    if scan_in_progress then
        log("SCAN_SKIPPED already running")
        return
    end

    scan_in_progress = true
    local match_count = 0
    local function_count = 0
    log("SCAN_BEGIN reason=" .. tostring(reason))

    local ok, scan_error = pcall(function()
        ForEachUObject(function(object)
            if not object or not object:IsValid() then
                return
            end

            local full_name = safe_full_name(object)
            if is_function(full_name) and contains_any(full_name, function_terms) then
                function_count = function_count + 1
                if not seen_names[full_name] then
                    seen_names[full_name] = true
                    log("FUNCTION " .. full_name)
                end
                -- Hook registration is intentionally disabled in the phase-two
                -- manual prototype. UE4SS 3.0.1 can access-violate while hot
                -- reload unregisters native hooks. F8 remains a read-only
                -- reflected-object inventory.
            elseif contains_any(full_name, object_terms) and (is_definition(full_name) or is_live_level_instance(full_name)) then
                match_count = match_count + 1
                if not seen_names[full_name] then
                    seen_names[full_name] = true
                    log("OBJECT " .. full_name)
                end
                if is_live_level_instance(full_name) then
                    log_known_properties(object, full_name)
                end
            end
        end)
    end)

    scan_in_progress = false
    if ok then
        log(string.format("SCAN_END objects=%d functions=%d", match_count, function_count))
    else
        log("SCAN_FAILED error=" .. tostring(scan_error))
    end
end

RegisterKeyBind(Key.F8, { ModifierKey.CONTROL }, function()
    ExecuteInGameThread(function()
        scan_loaded_objects("manual-hotkey")
    end)
end)

RegisterKeyBind(Key.F3, { ModifierKey.CONTROL }, function()
    ExecuteInGameThread(function()
        test_reset_player_board()
    end)
end)

RegisterKeyBind(Key.F2, { ModifierKey.CONTROL }, function()
    ExecuteInGameThread(function()
        test_load_player_cards_start()
    end)
end)

RegisterKeyBind(Key.F5, { ModifierKey.CONTROL }, function()
    ExecuteInGameThread(function()
        capture_checkpoint("manual-hotkey")
    end)
end)

RegisterKeyBind(Key.F6, { ModifierKey.CONTROL }, function()
    ExecuteInGameThread(function()
        restore_checkpoint_health()
    end)
end)

RegisterKeyBind(Key.F7, { ModifierKey.CONTROL }, function()
    ExecuteInGameThread(function()
        inspect_checkpoint_delta()
    end)
end)

RegisterKeyBind(Key.F9, { ModifierKey.CONTROL }, function()
    log("FULL_OBJECT_DUMP_REQUESTED")
    DumpAllObjects()
end)

log("LOADED phase-two safe prototype; Ctrl+F2 tests internal player-card loading after Ctrl+F3, Ctrl+F3 tests native player-board reset after a saved checkpoint, Ctrl+F5 saves a reprogram anchor, Ctrl+F6 restores health, Ctrl+F7 compares card zones, Ctrl+F8 inventories objects, Ctrl+F9 dumps all objects; hot reload is disabled")
