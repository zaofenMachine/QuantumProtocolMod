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
    $parts.Add(('turn={0}' -f $Card.turn))
    $parts.Add(('generic={0}' -f $Card.genericCounters))
    $parts.Add(('special={0}' -f $Card.specialCounters))
    $parts.Add(('modifiers={0}' -f $Card.modifiers))
    $parts.Add(('effects={0}' -f (($Card.effects | ForEach-Object {
        '{0}/{1}/{2}/{3}' -f $_.type, $_.blockers, $_.actionState, $_.highlighted
    }) -join ',')))
    return ($parts -join '|')
}

function ConvertTo-NormalizedInventory {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $inventory = Get-Content -LiteralPath $resolvedPath -Raw | ConvertFrom-Json
    if ($inventory.kind -ne 'read-only-battle-inventory') {
        throw "Not a read-only battle inventory: $resolvedPath"
    }
    if ($null -eq $inventory.objects -or @($inventory.objects).Count -eq 0) {
        throw "Battle inventory has no objects: $resolvedPath"
    }

    $objectsByName = @{}
    foreach ($snapshot in $inventory.objects) {
        if ($snapshot.role -eq 'CardPlacementComponent') {
            continue
        }
        $objectName = Get-UnrealObjectName $snapshot.fullName
        if ($objectName) {
            $objectsByName[$objectName] = $snapshot
        }
    }

    $placementsByOwner = @{}
    foreach ($placement in @($inventory.objects | Where-Object role -eq 'CardPlacementComponent')) {
        $ownerMatch = [regex]::Match($placement.fullName, 'BP_InGameCard_C_\d+')
        if ($ownerMatch.Success) {
            $placementsByOwner[$ownerMatch.Value] = $placement
        }
    }

    $cards = [System.Collections.Generic.List[object]]::new()
    foreach ($card in @($inventory.objects | Where-Object role -eq 'BP_InGameCard_C')) {
        $cardName = Get-UnrealObjectName $card.fullName
        $placement = $placementsByOwner[$cardName]
        $field = '-'
        if ($null -ne $placement) {
            $slotName = Get-UnrealObjectName (Get-SnapshotProperty $placement 'getter:getPlacedFieldSlot')
            $slot = if ($slotName) { $objectsByName[$slotName] } else { $null }
            if ($null -ne $slot) {
                $field = '{0}:{1}:{2}' -f `
                    (Get-SnapshotProperty $slot 'boardSide'), `
                    (Get-SnapshotProperty $slot 'rowType'), `
                    (Get-SnapshotProperty $slot 'SlotIndex')
            }
        }

        $genericCounterName = Get-UnrealObjectName (Get-SnapshotProperty $card 'cardOverlayGenericCounters')
        $specialCounterName = Get-UnrealObjectName (Get-SnapshotProperty $card 'cardOverlaySpecialCounters')
        $genericCounter = if ($genericCounterName) { $objectsByName[$genericCounterName] } else { $null }
        $specialCounter = if ($specialCounterName) { $objectsByName[$specialCounterName] } else { $null }

        $effects = [System.Collections.Generic.List[object]]::new()
        foreach ($effectName in (Get-EffectReferences (Get-SnapshotProperty $card 'cardOverlayEffects'))) {
            $effect = $objectsByName[$effectName]
            if ($null -eq $effect) {
                continue
            }
            $widgetName = Get-UnrealObjectName (Get-SnapshotProperty $effect 'mCardEffectWidget')
            $widget = if ($widgetName) { $objectsByName[$widgetName] } else { $null }
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
            tag = Get-SnapshotProperty $card 'getter:getTag'
            id = Get-SnapshotProperty $card 'getter:getId'
            location = Get-SnapshotProperty $card 'getter:getCardLocation'
            field = $field
            health = Get-SnapshotProperty $card 'getter:getCurrentHealth'
            turn = Get-SnapshotProperty $card 'getter:getCurrentTurnCounter'
            genericCounters = if ($null -ne $genericCounter) {
                Get-SnapshotProperty $genericCounter 'getter:getCurrentCounters'
            } else { $null }
            specialCounters = if ($null -ne $specialCounter) {
                Get-SnapshotProperty $specialCounter 'getter:getCurrentCounters'
            } else { $null }
            modifiers = Get-SnapshotProperty $card 'cardModifiers'
            effects = @($effects)
        })
    }

    $zones = [ordered]@{}
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
            $side = Get-SnapshotProperty $controller 'boardSide'
            $zoneName = "$role/$side"
            $zones[$zoneName] = @(
                ConvertTo-CardArray (Get-SnapshotProperty $controller 'getter:getCardInstanceListSorted')
            )
        }
    }

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
    $deckRun = Get-SnapshotProperty $gameInstance 'getter:getCurrentDeckRun'
    $deckRunWithoutTimestamp = $deckRun -replace '(?i)Timestamp="[^"]*",?', ''

    $playerCards = @($cards | Where-Object { -not $_.location.StartsWith('ENEMY_') })
    $enemyCards = @($cards | Where-Object { $_.location.StartsWith('ENEMY_') })

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
            activeDeck = @(ConvertTo-CardArray (Get-SnapshotProperty $gameInstance 'getter:getActiveDecklistInstances'))
            activeStorageHash = Get-TextSha256 (Get-SnapshotProperty $gameInstance 'getter:getActiveStorage')
            lootDropsHash = Get-TextSha256 (Get-SnapshotProperty $gameInstance 'getter:getLootDrops')
            lootDropInstancesHash = Get-TextSha256 (Get-SnapshotProperty $gameInstance 'getter:getLootDropInstances')
            deckRunIgnoringTimestampHash = Get-TextSha256 $deckRunWithoutTimestamp
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
        playerCardState = ConvertTo-CountedValues @(
            $playerCards | ForEach-Object { Get-CardStateSignature $_ }
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

Add-Difference 'player-card-state' 'playerCardState' `
    $beforeState.playerCardState $afterState.playerCardState
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
        playerCardStateIncludingLocationEqual = Test-Equivalent `
            $beforeState.playerCardState $afterState.playerCardState
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
        note = 'Runtime card GUIDs are diagnostic identity only and are excluded from semantic equality.'
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
