[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Before,

    [Parameter(Mandatory = $true)]
    [string]$After,

    [string]$Output
)

$ErrorActionPreference = 'Stop'

function Get-SnapshotProperty {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Snapshot,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $property = $Snapshot.properties.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }

    return [string]$property.Value
}

function Get-UnrealObjectName {
    param([AllowNull()][string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -eq 'None') {
        return $null
    }

    $clean = $Value.Trim().Trim("'").Trim('"')
    $lastDot = $clean.LastIndexOf('.')
    if ($lastDot -lt 0 -or $lastDot + 1 -ge $clean.Length) {
        return $null
    }

    return $clean.Substring($lastDot + 1)
}

function Get-TextSha256 {
    param([AllowNull()][string]$Value)

    if ($null -eq $Value) {
        return $null
    }

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-', '')
    } finally {
        $algorithm.Dispose()
    }
}

function Split-UnrealArray {
    param([AllowNull()][string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }

    $text = $Value.Trim()
    if ($text -eq '()') {
        return @()
    }
    if (-not ($text.StartsWith('(') -and $text.EndsWith(')'))) {
        throw "Expected an Unreal array enclosed by parentheses, got: $text"
    }

    $inner = $text.Substring(1, $text.Length - 2)
    $items = [System.Collections.Generic.List[string]]::new()
    $start = 0
    $depth = 0
    $inQuote = $false
    $escaped = $false

    for ($index = 0; $index -lt $inner.Length; $index++) {
        $character = $inner[$index]
        if ($inQuote) {
            if ($escaped) {
                $escaped = $false
            } elseif ($character -eq '\') {
                $escaped = $true
            } elseif ($character -eq '"') {
                $inQuote = $false
            }
            continue
        }

        if ($character -eq '"') {
            $inQuote = $true
        } elseif ($character -eq '(') {
            $depth++
        } elseif ($character -eq ')') {
            $depth--
            if ($depth -lt 0) {
                throw "Unbalanced Unreal array value: $Value"
            }
        } elseif ($character -eq ',' -and $depth -eq 0) {
            $items.Add($inner.Substring($start, $index - $start).Trim())
            $start = $index + 1
        }
    }

    if ($inQuote -or $depth -ne 0) {
        throw "Unbalanced Unreal array value: $Value"
    }

    $tail = $inner.Substring($start).Trim()
    if ($tail.Length -gt 0) {
        $items.Add($tail)
    }

    return @($items)
}

function ConvertTo-CardDescriptor {
    param([Parameter(Mandatory = $true)][string]$Value)

    $tagMatch = [regex]::Match($Value, '(?<![A-Za-z])Tag="([^"]+)"')
    if (-not $tagMatch.Success) {
        throw "CardInfoInstance did not contain a Tag field: $Value"
    }

    $upgradeMatch = [regex]::Match($Value, '(?<![A-Za-z])upgradeLevel=(-?\d+)')
    $upgradeLevel = 0
    if ($upgradeMatch.Success) {
        $upgradeLevel = [int]$upgradeMatch.Groups[1].Value
    }

    return ('{0}@{1}' -f $tagMatch.Groups[1].Value, $upgradeLevel)
}

function ConvertTo-CardArray {
    param([AllowNull()][string]$Value)

    return @(Split-UnrealArray $Value | ForEach-Object { ConvertTo-CardDescriptor $_ })
}

function ConvertTo-CountedValues {
    param([object[]]$Values)

    return @(
        $Values |
            Group-Object |
            Sort-Object Name |
            ForEach-Object {
                [ordered]@{
                    value = $_.Name
                    count = $_.Count
                }
            }
    )
}

function ConvertTo-ComparableJson {
    param([AllowNull()][object]$Value)

    return ($Value | ConvertTo-Json -Depth 30 -Compress)
}

function Test-Equivalent {
    param(
        [AllowNull()][object]$Left,
        [AllowNull()][object]$Right
    )

    return (ConvertTo-ComparableJson $Left) -ceq (ConvertTo-ComparableJson $Right)
}

function Get-EffectReferences {
    param([AllowNull()][string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }

    return @(
        [regex]::Matches($Value, 'BP_CardEffectDisplay_C_(?:CAT_)?\d+') |
            ForEach-Object { $_.Value }
    )
}

function Get-CardStateSignature {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Card,

        [switch]$IgnoreLocation
    )

    $parts = [System.Collections.Generic.List[string]]::new()
    $parts.Add(('card={0}' -f $Card.descriptor))
    if (-not $IgnoreLocation) {
        $parts.Add(('location={0}' -f $Card.location))
        $parts.Add(('field={0}' -f $Card.field))
    }
    $parts.Add(('health={0}' -f $Card.health))
    $parts.Add(('baseHealth={0}' -f $Card.baseHealth))
    $parts.Add(('turn={0}' -f $Card.turn))
    $parts.Add(('turnBase={0}' -f $Card.turnBase))
    $parts.Add(('turnAdjustment={0}' -f $Card.turnAdjustment))
    $parts.Add(('turnActive={0}' -f $Card.turnActive))
    $parts.Add(('generic={0}' -f $Card.genericCounters))
    $parts.Add(('special={0}' -f $Card.specialCounters))
    $parts.Add(('modifiers={0}' -f $Card.modifiers))
    $parts.Add(('effects={0}' -f (($Card.effects | ForEach-Object {
        '{0}/{1}/{2}/{3}' -f $_.type, $_.blockers, $_.actionState, $_.highlighted
    }) -join ',')))
    return ($parts -join '|')
}

function Get-NativeAttackSignature {
    param([Parameter(Mandatory = $true)][object]$Card)
    return ('card={0}|location={1}|field={2}|baseAttack={3}|currentAttack={4}|modifierCount={5}|modifiers={6}' -f `
        $Card.descriptor, $Card.location, $Card.field, $Card.nativeBaseAttack, $Card.nativeCurrentAttack, `
        $Card.nativeModifierCount, $Card.nativeModifiers)
}

function Get-NativeLevelSignature {
    param([Parameter(Mandatory = $true)][object]$Card)
    return ('card={0}|location={1}|field={2}|level={3}|modifierSum={4}|modifierCount={5}' -f `
        $Card.descriptor, $Card.location, $Card.field, $Card.nativeLevel, $Card.nativeLevelModifierSum, $Card.nativeLevelModifierCount)
}

function Get-NativeCounterSignature {
    param([Parameter(Mandatory = $true)][object]$Card)
    return ('card={0}|location={1}|field={2}|generic={3}|specialTotal={4}|specialEntries={5}|special={6}' -f `
        $Card.descriptor, $Card.location, $Card.field, $Card.nativeGenericCounters, $Card.nativeSpecialCounterTotal, `
        $Card.nativeSpecialCounterEntries, $Card.nativeSpecialCounters)
}

function Get-NativeHealthSignature {
    param([Parameter(Mandatory = $true)][object]$Card)
    return ('card={0}|location={1}|field={2}|base={3}|current={4}|adjustment={5}|modifierSum={6}|max={7}' -f `
        $Card.descriptor, $Card.location, $Card.field, $Card.nativeBaseHealth, $Card.nativeCurrentHealth, `
        $Card.nativeMaxHealthAdjustment, $Card.nativeModifierHealthSum, $Card.nativeMaxHealth)
}

function ConvertTo-NativeOrderedPlayerCards {
    param(
        [Parameter(Mandatory = $true)][object]$Inventory,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Cards
    )

    # GUIDs anchor observations inside one inventory only. They are excluded from
    # comparison because native startup creates new GUIDs. Never infer this order
    # from actor enumeration, metadata sorting, or matching duplicate card names.
    try {
        $cardsById = [System.Collections.Generic.Dictionary[string,object]]::new([System.StringComparer]::Ordinal)
        foreach ($card in $Cards) {
            if ([string]::IsNullOrWhiteSpace($card.id) -or $cardsById.ContainsKey($card.id)) {
                throw 'Card observations contain a missing or duplicate runtime ID'
            }
            $cardsById.Add($card.id, $card)
        }
        $roles = [ordered]@{
            BP_ControllerDeck_C = 'DECK'
            BP_ControllerHand_C = 'HAND'
            BP_ControllerTrash_C = 'TRASH'
        }
        $seenIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
        $orderedState = [ordered]@{}
        foreach ($role in $roles.Keys) {
            $location = $roles[$role]
            $controllers = @($Inventory.objects | Where-Object {
                $_.role -ceq $role -and -not $_.fullName.Contains('Default__') -and
                (Get-SnapshotProperty $_ 'boardSide') -ceq 'PLAYER'
            })
            if ($controllers.Count -ne 1) { throw "Expected one native player $location controller" }
            $idText = Get-SnapshotProperty $controllers[0] 'native:cardIdOrder'
            $instanceText = Get-SnapshotProperty $controllers[0] 'native:cardOrder'
            if ([string]::IsNullOrWhiteSpace($idText) -or [string]::IsNullOrWhiteSpace($instanceText)) {
                throw "Native player $location ID or instance order is unavailable"
            }
            # Wrapping preserves [] and single-element arrays on Windows PowerShell
            # as well as PowerShell 7, without ConvertFrom-Json enumeration ambiguity.
            $parsed = ('{"ids":' + $idText + '}') | ConvertFrom-Json
            if ($parsed.ids -isnot [System.Array] -or @($parsed.PSObject.Properties).Count -ne 1) {
                throw "Native player $location ID order is not a JSON array"
            }
            $ids = @($parsed.ids)
            $instances = @(Split-UnrealArray $instanceText)
            $zoneCards = @($Cards | Where-Object { $_.location -ceq $location })
            if ($ids.Count -ne $instances.Count -or $ids.Count -ne $zoneCards.Count) {
                throw "Native player $location order does not cover its complete card membership"
            }
            $states = [System.Collections.Generic.List[object]]::new()
            for ($index = 0; $index -lt $ids.Count; $index++) {
                $id = $ids[$index]
                if ($id -isnot [string] -or [string]::IsNullOrWhiteSpace($id) -or
                    -not $seenIds.Add($id) -or -not $cardsById.ContainsKey($id)) {
                    throw "Native player $location order contains a missing, duplicate, or unresolved ID"
                }
                $card = $cardsById[$id]
                if ($card.location -cne $location -or $card.instance -cne $instances[$index]) {
                    throw "Native player $location ID disagrees with its zone or ordered card instance"
                }
                $states.Add($card)
            }
            $orderedState[$location] = @($states)
        }
        return [ordered]@{ available = $true; reason = 'Complete native DECK/HAND/TRASH ID and instance order'; state = $orderedState }
    } catch {
        return [ordered]@{ available = $false; reason = $_.Exception.Message; state = $null }
    }
}

function ConvertTo-NativeOrderedPlayerState {
    param([Parameter(Mandatory = $true)][object]$OrderedCards)

    try {
        if (-not $OrderedCards.available) { throw $OrderedCards.reason }
        $orderedState = [ordered]@{}
        foreach ($location in $OrderedCards.state.Keys) {
            $states = [System.Collections.Generic.List[object]]::new()
            foreach ($card in $OrderedCards.state[$location]) {
                if ($card.nativeStatisticsStatus -cne 'verified-native-attack' -or
                    $card.nativeHealthStatus -cne 'verified-native-health' -or
                    $card.nativeCountersStatus -cne 'verified-native-counters' -or
                    $card.nativeLevelStatus -cne 'verified-native-level' -or -not $card.effectsAvailable) {
                    throw "Native player $location card has incomplete verified runtime observations"
                }
                foreach ($field in @('health','baseHealth','turn','turnBase','turnAdjustment','turnActive',
                    'nativeBaseAttack','nativeCurrentAttack','nativeModifierCount','nativeModifiers',
                    'nativeBaseHealth','nativeCurrentHealth','nativeMaxHealthAdjustment','nativeModifierHealthSum','nativeMaxHealth',
                    'nativeGenericCounters','nativeSpecialCounterTotal','nativeSpecialCounterEntries','nativeSpecialCounters',
                    'nativeLevel','nativeLevelModifierSum','nativeLevelModifierCount')) {
                    if ($null -eq $card[$field] -or ($field -ne 'nativeModifiers' -and [string]::IsNullOrWhiteSpace($card[$field]))) {
                        throw "Native player $location card lacks runtime field $field"
                    }
                }
                $states.Add([ordered]@{
                    cardState = Get-CardStateSignature $card
                    nativeAttack = Get-NativeAttackSignature $card
                    nativeHealth = Get-NativeHealthSignature $card
                    nativeCounters = Get-NativeCounterSignature $card
                    nativeLevel = Get-NativeLevelSignature $card
                })
            }
            $orderedState[$location] = @($states)
        }
        return [ordered]@{ available = $true; reason = 'Complete native DECK/HAND/TRASH ID order and runtime observations'; state = $orderedState }
    } catch {
        return [ordered]@{ available = $false; reason = $_.Exception.Message; state = $null }
    }
}

function ConvertTo-NativeEffectMembership {
    param(
        [Parameter(Mandatory = $true)][object]$OrderedCards,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$PlayerCards
    )

    try {
        if (-not $OrderedCards.available) { throw $OrderedCards.reason }
        $state = [ordered]@{}
        foreach ($location in @('DECK', 'HAND', 'TRASH', 'FIELD')) {
            $zoneCards = if ($location -ceq 'FIELD') {
                @($PlayerCards | Where-Object location -CEQ 'FIELD' | Sort-Object field)
            } else { @($OrderedCards.state[$location]) }
            $seenSlots = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
            $states = [System.Collections.Generic.List[object]]::new()
            foreach ($card in $zoneCards) {
                if ($location -ceq 'FIELD' -and (-not $card.fieldBindingAvailable -or
                    $card.field -cnotmatch '^PLAYER:(FRONT|BACK):[0-4]$' -or -not $seenSlots.Add($card.field))) {
                    throw 'Native player FIELD effects lack unique, resolved player slot bindings'
                }
                if ($card.nativeEffectsStatus -cne 'verified-native-effects' -or
                    -not [string]::IsNullOrEmpty($card.nativeEffectsReadError) -or
                    $card.nativeEffectsCount -cnotmatch '^(0|[1-9][0-9]?)$' -or [int]$card.nativeEffectsCount -gt 64 -or
                    [string]::IsNullOrWhiteSpace($card.nativeEffectsOrdered)) {
                    throw "Native player $location effect membership is unavailable or invalid"
                }
                $parsed = ('{"effects":' + $card.nativeEffectsOrdered + '}') | ConvertFrom-Json
                if ($parsed.effects -isnot [System.Array] -or @($parsed.PSObject.Properties).Count -ne 1 -or
                    @($parsed.effects).Count -ne [int]$card.nativeEffectsCount) {
                    throw "Native player $location effect membership count or array is invalid"
                }
                $effects = [System.Collections.Generic.List[object]]::new()
                foreach ($effect in $parsed.effects) {
                    if ($null -eq $effect -or @($effect.PSObject.Properties).Count -ne 3 -or
                        @($effect.PSObject.Properties.Name | Where-Object { $_ -cnotin @('tag', 'type', 'factoryKey') }).Count -ne 0 -or
                        $effect.tag -isnot [string] -or [string]::IsNullOrEmpty($effect.tag) -or
                        [System.Text.Encoding]::UTF8.GetByteCount($effect.tag) -gt 256 -or
                        $effect.factoryKey -isnot [string] -or $effect.factoryKey.Length -gt 1023 -or
                        $effect.factoryKey.IndexOf([char]0) -ge 0 -or
                        ($effect.type -isnot [int] -and $effect.type -isnot [long]) -or $effect.type -lt 0 -or $effect.type -gt 14) {
                        throw "Native player $location effect descriptor is malformed"
                    }
                    $effects.Add([ordered]@{ tag = $effect.tag; type = $effect.type; factoryKey = $effect.factoryKey })
                }
                $entry = [ordered]@{ instance = $card.instance; effects = @($effects) }
                if ($location -ceq 'FIELD') { $entry['field'] = $card.field }
                $states.Add($entry)
            }
            $state[$location] = @($states)
        }
        # Storage and the character ability slot are outside the Route C D/H/T/F
        # layout contract. Do not silently ignore transient zones such as PENDING.
        if (@($PlayerCards | Where-Object { $_.location -cnotin @('DECK', 'HAND', 'TRASH', 'FIELD', 'STORAGE', 'CHARACTER') }).Count -ne 0) {
            throw 'Native player effect membership contains an unsupported player zone'
        }
        return [ordered]@{ available = $true; reason = 'Complete ordered native effect membership bound to DECK/HAND/TRASH positions, FIELD slots and full card instances; STORAGE and CHARACTER ability cards excluded'; state = $state }
    } catch {
        return [ordered]@{ available = $false; reason = $_.Exception.Message; state = $null }
    }
}

function ConvertTo-NormalizedInventory {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $inventory = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($inventory.kind -ne 'read-only-battle-inventory') {
        throw "Not a read-only battle inventory: $resolvedPath"
    }
    if ($null -eq $inventory.objects -or @($inventory.objects).Count -eq 0) {
        throw "Battle inventory has no objects: $resolvedPath"
    }

    $objectsByName = @{}
    $objectNameCounts = @{}
    foreach ($snapshot in $inventory.objects) {
        if ($snapshot.role -eq 'CardPlacementComponent') {
            continue
        }
        $objectName = Get-UnrealObjectName $snapshot.fullName
        if ($objectName) {
            $objectsByName[$objectName] = $snapshot
            $objectNameCounts[$objectName]++
        }
    }

    $placementsByOwner = @{}
    $placementCountsByOwner = @{}
    foreach ($placement in @($inventory.objects | Where-Object role -eq 'CardPlacementComponent')) {
        $ownerMatch = [regex]::Match($placement.fullName, 'BP_InGameCard_C_\d+')
        if ($ownerMatch.Success) {
            $placementsByOwner[$ownerMatch.Value] = $placement
            $placementCountsByOwner[$ownerMatch.Value]++
        }
    }

    $cards = [System.Collections.Generic.List[object]]::new()
    foreach ($card in @($inventory.objects | Where-Object role -eq 'BP_InGameCard_C')) {
        $cardName = Get-UnrealObjectName $card.fullName
        $placement = $placementsByOwner[$cardName]
        $field = '-'
        $fieldBindingAvailable = $false
        if ($null -ne $placement) {
            $slotName = Get-UnrealObjectName (Get-SnapshotProperty $placement 'getter:getPlacedFieldSlot')
            $slot = if ($slotName) { $objectsByName[$slotName] } else { $null }
            if ($null -ne $slot) {
                $field = '{0}:{1}:{2}' -f `
                    (Get-SnapshotProperty $slot 'boardSide'), `
                    (Get-SnapshotProperty $slot 'rowType'), `
                    (Get-SnapshotProperty $slot 'SlotIndex')
                $fieldBindingAvailable = $placementCountsByOwner[$cardName] -eq 1 -and
                    $objectNameCounts[$cardName] -eq 1 -and $objectNameCounts[$slotName] -eq 1 -and
                    $slot.role -ceq 'BP_FieldSlot_C'
            }
        }

        $genericCounterName = Get-UnrealObjectName (Get-SnapshotProperty $card 'cardOverlayGenericCounters')
        $specialCounterName = Get-UnrealObjectName (Get-SnapshotProperty $card 'cardOverlaySpecialCounters')
        $genericCounter = if ($genericCounterName) { $objectsByName[$genericCounterName] } else { $null }
        $specialCounter = if ($specialCounterName) { $objectsByName[$specialCounterName] } else { $null }

        $effects = [System.Collections.Generic.List[object]]::new()
        $effectText = Get-SnapshotProperty $card 'cardOverlayEffects'
        $effectsAvailable = $null -ne $effectText
        $effectNames = @(Get-EffectReferences $effectText)
        try {
            if ([string]::IsNullOrWhiteSpace($effectText) -or
                @(Split-UnrealArray $effectText).Count -ne $effectNames.Count) { $effectsAvailable = $false }
        } catch { $effectsAvailable = $false }
        foreach ($effectName in $effectNames) {
            $effect = $objectsByName[$effectName]
            if ($null -eq $effect) {
                $effectsAvailable = $false
                continue
            }
            $widgetName = Get-UnrealObjectName (Get-SnapshotProperty $effect 'mCardEffectWidget')
            $widget = if ($widgetName) { $objectsByName[$widgetName] } else { $null }
            if ($null -eq $widget -or $null -eq (Get-SnapshotProperty $widget 'EffectType') -or
                $null -eq (Get-SnapshotProperty $widget 'activationBlockers') -or
                $null -eq (Get-SnapshotProperty $effect 'getter:getEffectActionState') -or
                $null -eq (Get-SnapshotProperty $effect 'isAutomationHighlighted')) {
                $effectsAvailable = $false
            }
            $effects.Add([ordered]@{
                type = if ($null -ne $widget) { Get-SnapshotProperty $widget 'EffectType' } else { $null }
                blockers = if ($null -ne $widget) { Get-SnapshotProperty $widget 'activationBlockers' } else { $null }
                actionState = Get-SnapshotProperty $effect 'getter:getEffectActionState'
                highlighted = Get-SnapshotProperty $effect 'isAutomationHighlighted'
            })
        }

        $cardInfoInstance = Get-SnapshotProperty $card 'getter:getCardInfoInstance'
        $cards.Add([ordered]@{
            descriptor = ConvertTo-CardDescriptor $cardInfoInstance
            instance = $cardInfoInstance
            tag = Get-SnapshotProperty $card 'getter:getTag'
            id = Get-SnapshotProperty $card 'getter:getId'
            location = Get-SnapshotProperty $card 'getter:getCardLocation'
            field = $field
            fieldBindingAvailable = $fieldBindingAvailable
            health = Get-SnapshotProperty $card 'getter:getCurrentHealth'
            baseHealth = Get-SnapshotProperty $card 'nativeDiagnostic:baseHealth'
            turn = Get-SnapshotProperty $card 'getter:getCurrentTurnCounter'
            turnBase = Get-SnapshotProperty $card 'nativeDiagnostic:turnBase'
            turnAdjustment = Get-SnapshotProperty $card 'nativeDiagnostic:turnAdjustment'
            turnActive = Get-SnapshotProperty $card 'getter:isTurnActive'
            genericCounters = if ($null -ne $genericCounter) {
                Get-SnapshotProperty $genericCounter 'getter:getCurrentCounters'
            } else { $null }
            specialCounters = if ($null -ne $specialCounter) {
                Get-SnapshotProperty $specialCounter 'getter:getCurrentCounters'
            } else { $null }
            modifiers = Get-SnapshotProperty $card 'cardModifiers'
            nativeStatisticsStatus = Get-SnapshotProperty $card 'nativeStats:status'
            nativeBaseAttack = Get-SnapshotProperty $card 'nativeStats:baseAttack'
            nativeCurrentAttack = Get-SnapshotProperty $card 'nativeStats:currentAttack'
            nativeModifierCount = Get-SnapshotProperty $card 'nativeStats:modifierCount'
            nativeModifiers = Get-SnapshotProperty $card 'nativeStats:modifiers'
            nativeLevelStatus = Get-SnapshotProperty $card 'nativeLevel:status'
            nativeLevel = Get-SnapshotProperty $card 'nativeLevel:level'
            nativeLevelModifierSum = Get-SnapshotProperty $card 'nativeLevel:modifierSum'
            nativeLevelModifierCount = Get-SnapshotProperty $card 'nativeLevel:modifierCount'
            nativeCountersStatus = Get-SnapshotProperty $card 'nativeCounters:status'
            nativeGenericCounters = Get-SnapshotProperty $card 'nativeCounters:generic'
            nativeSpecialCounterTotal = Get-SnapshotProperty $card 'nativeCounters:specialTotal'
            nativeSpecialCounterEntries = Get-SnapshotProperty $card 'nativeCounters:specialEntryCount'
            nativeSpecialCounters = Get-SnapshotProperty $card 'nativeCounters:specialCounters'
            nativeHealthStatus = Get-SnapshotProperty $card 'nativeHealth:status'
            nativeBaseHealth = Get-SnapshotProperty $card 'nativeHealth:baseHealth'
            nativeCurrentHealth = Get-SnapshotProperty $card 'nativeHealth:currentHealth'
            nativeMaxHealthAdjustment = Get-SnapshotProperty $card 'nativeHealth:maxHealthAdjustment'
            nativeModifierHealthSum = Get-SnapshotProperty $card 'nativeHealth:modifierHealthSum'
            nativeMaxHealth = Get-SnapshotProperty $card 'nativeHealth:maxHealth'
            nativeEffectsStatus = Get-SnapshotProperty $card 'nativeEffects:status'
            nativeEffectsReadError = Get-SnapshotProperty $card 'nativeEffects:readError'
            nativeEffectsCount = Get-SnapshotProperty $card 'nativeEffects:count'
            nativeEffectsOrdered = Get-SnapshotProperty $card 'nativeEffects:ordered'
            effects = @($effects)
            effectsAvailable = $effectsAvailable
        })
    }

    $zones = [ordered]@{}
    $nativePlayerZones = [ordered]@{}
    $nativePlayerZoneInstances = [ordered]@{}
    $zoneRoles = @(
        'BP_ControllerDeck_C',
        'BP_ControllerHand_C',
        'BP_ControllerStorage_C',
        'BP_ControllerPendingCards_C',
        'BP_ControllerEnemyPending_C',
        'BP_ControllerTrash_C',
        'BP_ControllerCharacterCardSlot_C'
    )
    foreach ($role in $zoneRoles) {
        foreach ($controller in @($inventory.objects | Where-Object role -eq $role)) {
            if ($controller.fullName.Contains('Default__')) { continue }
            $side = Get-SnapshotProperty $controller 'boardSide'
            $zoneName = "$role/$side"
            $zones[$zoneName] = @(
                ConvertTo-CardArray (Get-SnapshotProperty $controller 'getter:getCardInstanceListSorted')
            )
            if ($side -eq 'PLAYER' -and $role -in @(
                'BP_ControllerDeck_C', 'BP_ControllerHand_C', 'BP_ControllerTrash_C')) {
                $nativeOrder = Get-SnapshotProperty $controller 'native:cardOrder'
                if (-not [string]::IsNullOrWhiteSpace($nativeOrder)) {
                    $nativePlayerZones[$zoneName] = @(ConvertTo-CardArray $nativeOrder)
                    $nativePlayerZoneInstances[$zoneName] = $nativeOrder
                }
            }
        }
    }

    # Actor enumeration can swap PLAYER/ENEMY insertion order across a reload.
    # Canonicalize map keys without sorting the card sequences inside each zone.
    $canonicalZones = [ordered]@{}
    foreach ($key in @($zones.Keys | Sort-Object)) { $canonicalZones[$key] = $zones[$key] }
    $zones = $canonicalZones

    $engine = $inventory.objects | Where-Object role -eq 'BP_CardEngine_C' | Select-Object -First 1
    $bottomBar = $inventory.objects | Where-Object role -eq 'BP_BottomBar_C' | Select-Object -First 1
    $spawner = $inventory.objects | Where-Object role -eq 'Spawner_C' | Select-Object -First 1
    $characterSlot = $inventory.objects |
        Where-Object role -eq 'BP_ControllerCharacterCardSlot_C' |
        Select-Object -First 1
    $gameInstance = $inventory.objects |
        Where-Object { $_.role -eq 'GI_Quantum_C' -and $_.fullName.Contains('/Engine/Transient.') } |
        Select-Object -First 1

    foreach ($required in @($engine, $bottomBar, $spawner, $characterSlot, $gameInstance)) {
        if ($null -eq $required) {
            throw "Battle inventory is incomplete: $resolvedPath"
        }
    }

    $spawnList = Get-SnapshotProperty $spawner 'spawnList'
    $spawnWaves = @(Split-UnrealArray $spawnList)
    $spawnWaveHashes = @($spawnWaves | ForEach-Object { Get-TextSha256 $_ })

    $playerCards = @($cards | Where-Object { -not $_.location.StartsWith('ENEMY_') })
    $enemyCards = @($cards | Where-Object { $_.location.StartsWith('ENEMY_') })
    $orderedPlayerCards = ConvertTo-NativeOrderedPlayerCards $inventory @($cards)
    $orderedPlayerState = ConvertTo-NativeOrderedPlayerState $orderedPlayerCards
    $nativeEffectMembership = ConvertTo-NativeEffectMembership $orderedPlayerCards $playerCards

    return [ordered]@{
        path = $resolvedPath
        fileSha256 = (Get-FileHash -LiteralPath $resolvedPath -Algorithm SHA256).Hash
        schemaVersion = $inventory.schemaVersion
        capturedAtUtc = $inventory.capturedAtUtc
        context = [ordered]@{
            sourceLevelName = Get-SnapshotProperty $gameInstance 'sourceLevelName'
            currentLevel = Get-SnapshotProperty $gameInstance 'CurrentLevel'
            levelToLoad = Get-SnapshotProperty $gameInstance 'levelToLoad'
            levelChangeType = Get-SnapshotProperty $gameInstance 'lastLevelChangeType'
            characterHash = Get-TextSha256 (Get-SnapshotProperty $gameInstance 'activeCharacterInfo')
            stageHash = Get-TextSha256 (Get-SnapshotProperty $gameInstance 'activeStageInfo')
            activeStorageHash = Get-TextSha256 (Get-SnapshotProperty $gameInstance 'getter:getActiveStorage')
            lootDropsHash = Get-TextSha256 (Get-SnapshotProperty $gameInstance 'getter:getLootDrops')
            lootDropInstancesHash = Get-TextSha256 (Get-SnapshotProperty $gameInstance 'getter:getLootDropInstances')
        }
        engine = [ordered]@{
            gameState = Get-SnapshotProperty $engine 'currentGameState'
            enemyBoardPenalty = Get-SnapshotProperty $engine 'isEnemyBoardPenaltyOn'
            startingHandSize = Get-SnapshotProperty $engine 'startingHandSize'
            turn = Get-SnapshotProperty $engine 'getter:getTurnCount'
            maxTurnCountdown = Get-SnapshotProperty $engine 'getter:getCurrentMaxTurnCountdown'
            health = Get-SnapshotProperty $engine 'getter:getCurrentHealth'
        }
        bottomBar = [ordered]@{
            health = Get-SnapshotProperty $bottomBar 'currentHealth'
            maxHealth = Get-SnapshotProperty $bottomBar 'maxHealth'
        }
        characterResource = [ordered]@{
            charge = Get-SnapshotProperty $characterSlot 'getter:getCurrentCharacterCardCharge'
            requirement = Get-SnapshotProperty $characterSlot 'getter:getAmountPerCharacterCard'
            abilityOk = Get-SnapshotProperty $characterSlot 'getter:isCharacterAbilityOk'
        }
        spawner = [ordered]@{
            currentWaveIndex = Get-SnapshotProperty $spawner 'currentWaveIndex'
            lastWaveIndex = Get-SnapshotProperty $spawner 'lastWaveIndex'
            currentTurnCountdown = Get-SnapshotProperty $spawner 'currentTurnCountdown'
            waveCountdownPenalty = Get-SnapshotProperty $spawner 'waveCountdownPenalty'
            currentWaveAlertCounter = Get-SnapshotProperty $spawner 'currentWaveAlertCounter'
            amountPerWaveAlertLevel = Get-SnapshotProperty $spawner 'amountPerWaveAlertLevel'
            maxWaveAlertStacks = Get-SnapshotProperty $spawner 'maxWaveAlertStacks'
            autoSpawn = Get-SnapshotProperty $spawner 'autoSpawn'
            spawnListHash = Get-TextSha256 $spawnList
            spawnWaveHashes = $spawnWaveHashes
        }
        zones = $zones
        nativePlayerZones = $nativePlayerZones
        nativePlayerZoneInstances = $nativePlayerZoneInstances
        nativePlayerZonesAvailable = $nativePlayerZones.Count -eq 3
        playerNativeOrderedStateAvailable = $orderedPlayerState.available
        playerNativeOrderedStateReason = $orderedPlayerState.reason
        playerNativeOrderedState = $orderedPlayerState.state
        playerNativeEffectMembershipAvailable = $nativeEffectMembership.available
        playerNativeEffectMembershipReason = $nativeEffectMembership.reason
        playerNativeEffectMembership = $nativeEffectMembership.state
        playerCardState = ConvertTo-CountedValues @(
            $playerCards | ForEach-Object { Get-CardStateSignature $_ }
        )
        playerNativeStatisticsAvailable = $playerCards.Count -gt 0 -and @(
            $playerCards | Where-Object { $_.nativeStatisticsStatus -cne 'verified-native-attack' }
        ).Count -eq 0
        playerNativeStatistics = ConvertTo-CountedValues @(
            $playerCards | ForEach-Object { Get-NativeAttackSignature $_ }
        )
        playerNativeLevelAvailable = $playerCards.Count -gt 0 -and @(
            $playerCards | Where-Object { $_.nativeLevelStatus -cne 'verified-native-level' }
        ).Count -eq 0
        playerNativeLevel = ConvertTo-CountedValues @(
            $playerCards | ForEach-Object { Get-NativeLevelSignature $_ }
        )
        playerNativeCountersAvailable = $playerCards.Count -gt 0 -and @(
            $playerCards | Where-Object { $_.nativeCountersStatus -cne 'verified-native-counters' }
        ).Count -eq 0
        playerNativeCounters = ConvertTo-CountedValues @(
            $playerCards | ForEach-Object { Get-NativeCounterSignature $_ }
        )
        playerNativeHealthAvailable = $playerCards.Count -gt 0 -and @(
            $playerCards | Where-Object { $_.nativeHealthStatus -cne 'verified-native-health' }
        ).Count -eq 0
        playerNativeHealth = ConvertTo-CountedValues @(
            $playerCards | ForEach-Object { Get-NativeHealthSignature $_ }
        )
        playerCardRuntimeStateIgnoringLocation = ConvertTo-CountedValues @(
            $playerCards | ForEach-Object { Get-CardStateSignature $_ -IgnoreLocation }
        )
        playerCardIdentity = ConvertTo-CountedValues @(
            $playerCards | ForEach-Object { $_.descriptor }
        )
        enemyCardState = ConvertTo-CountedValues @(
            $enemyCards | ForEach-Object { Get-CardStateSignature $_ }
        )
        runtimeCardIds = @($cards | ForEach-Object { $_.id } | Sort-Object)
    }
}

$beforeState = ConvertTo-NormalizedInventory $Before
$afterState = ConvertTo-NormalizedInventory $After
$differences = [System.Collections.Generic.List[object]]::new()

function Add-Difference {
    param(
        [Parameter(Mandatory = $true)][string]$Category,
        [Parameter(Mandatory = $true)][string]$Path,
        [AllowNull()][object]$BeforeValue,
        [AllowNull()][object]$AfterValue
    )

    if (-not (Test-Equivalent $BeforeValue $AfterValue)) {
        $differences.Add([ordered]@{
            category = $Category
            path = $Path
            before = $BeforeValue
            after = $AfterValue
        })
    }
}

foreach ($key in $beforeState.context.Keys) {
    Add-Difference 'context' "context.$key" $beforeState.context[$key] $afterState.context[$key]
}
foreach ($key in $beforeState.engine.Keys) {
    Add-Difference 'engine' "engine.$key" $beforeState.engine[$key] $afterState.engine[$key]
}
foreach ($key in $beforeState.bottomBar.Keys) {
    Add-Difference 'health' "bottomBar.$key" $beforeState.bottomBar[$key] $afterState.bottomBar[$key]
}
foreach ($key in $beforeState.characterResource.Keys) {
    Add-Difference 'character-resource' "characterResource.$key" `
        $beforeState.characterResource[$key] $afterState.characterResource[$key]
}
foreach ($key in $beforeState.spawner.Keys) {
    $category = if ($key.StartsWith('spawn')) { 'future-spawn-plan' } else { 'spawner' }
    Add-Difference $category "spawner.$key" $beforeState.spawner[$key] $afterState.spawner[$key]
}

$allZoneNames = @($beforeState.zones.Keys + $afterState.zones.Keys | Sort-Object -Unique)
foreach ($zoneName in $allZoneNames) {
    Add-Difference 'player-and-enemy-zones' "zones.$zoneName" `
        $beforeState.zones[$zoneName] $afterState.zones[$zoneName]
}

if ($beforeState.nativePlayerZonesAvailable -and $afterState.nativePlayerZonesAvailable) {
    Add-Difference 'native-player-zone-order' 'nativePlayerZones' `
        $beforeState.nativePlayerZones $afterState.nativePlayerZones
    Add-Difference 'native-player-zone-instances' 'nativePlayerZoneInstances' `
        $beforeState.nativePlayerZoneInstances $afterState.nativePlayerZoneInstances
}
if ($beforeState.playerNativeOrderedStateAvailable -and $afterState.playerNativeOrderedStateAvailable) {
    foreach ($zone in $beforeState.playerNativeOrderedState.Keys) {
        Add-Difference 'player-native-ordered-state' "playerNativeOrderedState.$zone" `
            $beforeState.playerNativeOrderedState[$zone] $afterState.playerNativeOrderedState[$zone]
    }
}
if ($beforeState.playerNativeEffectMembershipAvailable -and $afterState.playerNativeEffectMembershipAvailable) {
    foreach ($zone in $beforeState.playerNativeEffectMembership.Keys) {
        Add-Difference 'player-native-effect-membership' "playerNativeEffectMembership.$zone" `
            $beforeState.playerNativeEffectMembership[$zone] $afterState.playerNativeEffectMembership[$zone]
    }
}

Add-Difference 'player-card-state' 'playerCardState' `
    $beforeState.playerCardState $afterState.playerCardState
if ($beforeState.playerNativeStatisticsAvailable -and $afterState.playerNativeStatisticsAvailable) {
    Add-Difference 'player-native-statistics' 'playerNativeStatistics' `
        $beforeState.playerNativeStatistics $afterState.playerNativeStatistics
}
if ($beforeState.playerNativeLevelAvailable -and $afterState.playerNativeLevelAvailable) {
    Add-Difference 'player-native-level' 'playerNativeLevel' `
        $beforeState.playerNativeLevel $afterState.playerNativeLevel
}
if ($beforeState.playerNativeCountersAvailable -and $afterState.playerNativeCountersAvailable) {
    Add-Difference 'player-native-counters' 'playerNativeCounters' `
        $beforeState.playerNativeCounters $afterState.playerNativeCounters
}
if ($beforeState.playerNativeHealthAvailable -and $afterState.playerNativeHealthAvailable) {
    Add-Difference 'player-native-health' 'playerNativeHealth' `
        $beforeState.playerNativeHealth $afterState.playerNativeHealth
}
Add-Difference 'player-card-state' 'playerCardRuntimeStateIgnoringLocation' `
    $beforeState.playerCardRuntimeStateIgnoringLocation `
    $afterState.playerCardRuntimeStateIgnoringLocation
Add-Difference 'player-card-identity' 'playerCardIdentity' `
    $beforeState.playerCardIdentity $afterState.playerCardIdentity
Add-Difference 'enemy-card-state' 'enemyCardState' `
    $beforeState.enemyCardState $afterState.enemyCardState

$beforeIdSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$beforeState.runtimeCardIds)
$afterIdSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$afterState.runtimeCardIds)
$sharedIds = [System.Collections.Generic.HashSet[string]]::new($beforeIdSet)
$null = $sharedIds.IntersectWith($afterIdSet)

$spawnerScalarBefore = [ordered]@{}
$spawnerScalarAfter = [ordered]@{}
foreach ($key in $beforeState.spawner.Keys | Where-Object { -not $_.StartsWith('spawn') }) {
    $spawnerScalarBefore[$key] = $beforeState.spawner[$key]
    $spawnerScalarAfter[$key] = $afterState.spawner[$key]
}

$differentSpawnWaveIndices = [System.Collections.Generic.List[int]]::new()
$waveCount = [Math]::Max(
    @($beforeState.spawner.spawnWaveHashes).Count,
    @($afterState.spawner.spawnWaveHashes).Count)
for ($index = 0; $index -lt $waveCount; $index++) {
    $beforeHash = if ($index -lt @($beforeState.spawner.spawnWaveHashes).Count) {
        $beforeState.spawner.spawnWaveHashes[$index]
    } else { $null }
    $afterHash = if ($index -lt @($afterState.spawner.spawnWaveHashes).Count) {
        $afterState.spawner.spawnWaveHashes[$index]
    } else { $null }
    if ($beforeHash -cne $afterHash) {
        $differentSpawnWaveIndices.Add($index)
    }
}

$report = [ordered]@{
    schemaVersion = 1
    kind = 'battle-inventory-semantic-diff'
    generatedAtUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
    before = [ordered]@{
        path = $beforeState.path
        fileSha256 = $beforeState.fileSha256
        capturedAtUtc = $beforeState.capturedAtUtc
    }
    after = [ordered]@{
        path = $afterState.path
        fileSha256 = $afterState.fileSha256
        capturedAtUtc = $afterState.capturedAtUtc
    }
    checks = [ordered]@{
        semanticEqual = ($differences.Count -eq 0)
        contextEqual = Test-Equivalent $beforeState.context $afterState.context
        engineEqual = Test-Equivalent $beforeState.engine $afterState.engine
        healthEqual = Test-Equivalent $beforeState.bottomBar $afterState.bottomBar
        characterResourceEqual = Test-Equivalent `
            $beforeState.characterResource $afterState.characterResource
        spawnerRuntimeScalarsEqual = Test-Equivalent $spawnerScalarBefore $spawnerScalarAfter
        futureSpawnPlanEqual = Test-Equivalent `
            $beforeState.spawner.spawnWaveHashes $afterState.spawner.spawnWaveHashes
        playerZoneSequencesEqual = Test-Equivalent $beforeState.zones $afterState.zones
        playerNativeZoneOrderAvailable = $beforeState.nativePlayerZonesAvailable -and $afterState.nativePlayerZonesAvailable
        playerNativeZoneSequencesEqual = if ($beforeState.nativePlayerZonesAvailable -and $afterState.nativePlayerZonesAvailable) {
            Test-Equivalent $beforeState.nativePlayerZones $afterState.nativePlayerZones
        } else { $null }
        playerNativeZoneInstancesEqual = if ($beforeState.nativePlayerZonesAvailable -and $afterState.nativePlayerZonesAvailable) {
            Test-Equivalent $beforeState.nativePlayerZoneInstances $afterState.nativePlayerZoneInstances
        } else { $null }
        playerNativeOrderedStateAvailable = $beforeState.playerNativeOrderedStateAvailable -and $afterState.playerNativeOrderedStateAvailable
        playerNativeOrderedStateEqual = if ($beforeState.playerNativeOrderedStateAvailable -and $afterState.playerNativeOrderedStateAvailable) {
            Test-Equivalent $beforeState.playerNativeOrderedState $afterState.playerNativeOrderedState
        } else { $null }
        playerNativeEffectMembershipAvailable = $beforeState.playerNativeEffectMembershipAvailable -and $afterState.playerNativeEffectMembershipAvailable
        playerNativeEffectMembershipEqual = if ($beforeState.playerNativeEffectMembershipAvailable -and $afterState.playerNativeEffectMembershipAvailable) {
            Test-Equivalent $beforeState.playerNativeEffectMembership $afterState.playerNativeEffectMembership
        } else { $null }
        playerCardStateIncludingLocationEqual = Test-Equivalent `
            $beforeState.playerCardState $afterState.playerCardState
        playerNativeStatisticsAvailable = $beforeState.playerNativeStatisticsAvailable -and $afterState.playerNativeStatisticsAvailable
        playerNativeStatisticsEqual = if ($beforeState.playerNativeStatisticsAvailable -and $afterState.playerNativeStatisticsAvailable) {
            Test-Equivalent $beforeState.playerNativeStatistics $afterState.playerNativeStatistics
        } else { $null }
        playerNativeLevelAvailable = $beforeState.playerNativeLevelAvailable -and $afterState.playerNativeLevelAvailable
        playerNativeLevelEqual = if ($beforeState.playerNativeLevelAvailable -and $afterState.playerNativeLevelAvailable) {
            Test-Equivalent $beforeState.playerNativeLevel $afterState.playerNativeLevel
        } else { $null }
        playerNativeCountersAvailable = $beforeState.playerNativeCountersAvailable -and $afterState.playerNativeCountersAvailable
        playerNativeCountersEqual = if ($beforeState.playerNativeCountersAvailable -and $afterState.playerNativeCountersAvailable) {
            Test-Equivalent $beforeState.playerNativeCounters $afterState.playerNativeCounters
        } else { $null }
        playerNativeHealthAvailable = $beforeState.playerNativeHealthAvailable -and $afterState.playerNativeHealthAvailable
        playerNativeHealthEqual = if ($beforeState.playerNativeHealthAvailable -and $afterState.playerNativeHealthAvailable) {
            Test-Equivalent $beforeState.playerNativeHealth $afterState.playerNativeHealth
        } else { $null }
        playerCardRuntimeStateIgnoringLocationEqual = Test-Equivalent `
            $beforeState.playerCardRuntimeStateIgnoringLocation `
            $afterState.playerCardRuntimeStateIgnoringLocation
        playerCardIdentityMultisetEqual = Test-Equivalent `
            $beforeState.playerCardIdentity $afterState.playerCardIdentity
        enemyCardStateEqual = Test-Equivalent $beforeState.enemyCardState $afterState.enemyCardState
        runtimeCardIdsEqual = Test-Equivalent $beforeState.runtimeCardIds $afterState.runtimeCardIds
    }
    identity = [ordered]@{
        beforeRuntimeCardIdCount = @($beforeState.runtimeCardIds).Count
        afterRuntimeCardIdCount = @($afterState.runtimeCardIds).Count
        sharedRuntimeCardIdCount = $sharedIds.Count
        beforeNativeOrderedStateCoverage = $beforeState.playerNativeOrderedStateReason
        afterNativeOrderedStateCoverage = $afterState.playerNativeOrderedStateReason
        beforeNativeEffectMembershipCoverage = $beforeState.playerNativeEffectMembershipReason
        afterNativeEffectMembershipCoverage = $afterState.playerNativeEffectMembershipReason
        nativeEffectMembershipNote = 'Native effect membership compares ordered tag/type/factoryKey arrays without UI or action-history inference. DECK/HAND/TRASH use verified native ID-to-instance bindings; FIELD uses unique resolved player slots. Full CardInfoInstance text is compared in every position. STORAGE and CHARACTER ability cards are outside this D/H/T/F scope; other player zones, including PENDING, make coverage unknown. Empty arrays are valid; missing or invalid native coverage yields null and does not alter older checks.'
        note = 'Runtime GUIDs are excluded from equality. playerZoneSequencesEqual compares metadata-sorted controller views, not draw/hand order. Native order is unknown (null) unless both inventories include all three player-zone arrays. Ordered runtime state requires complete native DECK/HAND/TRASH ID arrays resolving unique cards in the correct zone and instance position, with verified attack/health/counters/level and complete turn/effect observations. GUIDs only link each inventory internally; missing or invalid coverage yields null, never inferred order. Native attack/modifier equality is unknown (null) unless every player card in both inventories has verified native statistics; the older card-state check does not cover those fields.'
    }
    futureSpawnPlan = [ordered]@{
        beforeWaveCount = @($beforeState.spawner.spawnWaveHashes).Count
        afterWaveCount = @($afterState.spawner.spawnWaveHashes).Count
        differentWaveIndices = @($differentSpawnWaveIndices)
    }
    differenceCount = $differences.Count
    differences = @($differences)
}

$json = $report | ConvertTo-Json -Depth 30
if (-not [string]::IsNullOrWhiteSpace($Output)) {
    $outputDirectory = Split-Path -Parent $Output
    if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    }
    [System.IO.File]::WriteAllText(
        [System.IO.Path]::GetFullPath($Output),
        $json + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))
    Write-Host "Wrote semantic diff: $([System.IO.Path]::GetFullPath($Output))"
}

$report.checks | Format-List
Write-Host "Difference count: $($report.differenceCount)"
if ($differentSpawnWaveIndices.Count -gt 0) {
    Write-Host "Different future spawn-wave indices: $($differentSpawnWaveIndices -join ', ')"
}
foreach ($difference in $differences) {
    Write-Host "[$($difference.category)] $($difference.path)"
}
