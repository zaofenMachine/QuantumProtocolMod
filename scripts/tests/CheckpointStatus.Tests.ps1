#Requires -Version 7.2
<#
.SYNOPSIS
Standalone, dependency-free tests for the read-only checkpoint status viewer.
.DESCRIPTION
All writes and injected readers operate beneath a new temporary directory. The
default fixtures are deliberately synthetic header documents, not restorable
checkpoints or native capture evidence. No live installation is opened.

Optionally pass -FixtureRoot to the CLOSED Phase45 evidence root to copy and
exercise its actual endpoint bytes. The original files are never changed, and
receipt paths are never followed. This optional check is not a game test.
.EXAMPLE
pwsh -NoProfile -File scripts/tests/CheckpointStatus.Tests.ps1
.EXAMPLE
pwsh -NoProfile -File scripts/tests/CheckpointStatus.Tests.ps1 -FixtureRoot F:/copied-phase45-evidence
#>
[CmdletBinding()]
param([string]$FixtureRoot, [switch]$KeepTemporaryFiles)

Set-StrictMode -Version 3
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path $PSScriptRoot -Parent
$modulePath = Join-Path $scriptRoot 'CheckpointStatus.psm1'
$entryPath = Join-Path $scriptRoot 'Get-CheckpointStatus.ps1'
Import-Module $modulePath -Force -ErrorAction Stop
$script:StatusModule = Get-Module CheckpointStatus

$script:Utf8 = [Text.UTF8Encoding]::new($false, $true)
$script:TemporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$script:TemporaryRoot = Join-Path $script:TemporaryParent ('QuantumCheckpointStatusTests-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($script:TemporaryRoot)
$script:Tests = [Collections.Generic.List[object]]::new()
$script:Sequence = 0
$script:Names = [ordered]@{
    main = 'route-c.json'; spawn = 'route-c-exact-spawn-plan.json'
    hand = 'route-c-exact-player-hand-health.json'; qualification = 'route-c-exact-player-qualification.json'
    layout = 'route-c-exact-player-field.json'; charge = 'route-c-exact-character-charge.json'
    turn = 'route-c-exact-turn-progress.json'
}
$script:Time = [DateTimeOffset]::UtcNow.AddMinutes(-1).ToString('yyyy-MM-ddTHH:mm:ssZ')

function Assert-True([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw "Assertion failed: $Message" }
}
function Assert-Equal($Actual, $Expected, [string]$Message) {
    if ($Actual -cne $Expected) { throw "Assertion failed: $Message; expected=[$Expected], actual=[$Actual]" }
}
function Assert-NoRestoreClaim($Result) {
    Assert-Equal $Result.kind 'checkpoint-status-view' 'result kind'
    foreach ($key in @('restoreAdmissionProven', 'fullPlayerExactRestorationProven')) {
        Assert-True ($Result.$key -is [bool] -and $Result.$key -eq $false) "$key must be Boolean false"
    }
}
function Test-Case([string]$Name, [scriptblock]$Body) {
    try {
        & $Body
        $script:Tests.Add([pscustomobject]@{ name = $Name; status = 'passed' })
    } catch {
        $script:Tests.Add([pscustomobject]@{ name = $Name; status = 'failed'; reason = $_.Exception.Message })
    }
}
function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 50 -Compress), $script:Utf8)
}
function Read-FixtureJson([string]$Path) {
    # Keep timestamps as strings on both PS7.2 and newer versions that auto-parse dates.
    $text = [IO.File]::ReadAllText($Path, $script:Utf8)
    $value = ConvertFrom-Json -InputObject $text -AsHashtable -Depth 50
    $doc = [Text.Json.JsonDocument]::Parse($text)
    try {
        foreach ($key in @('attemptedAtUtc', 'finishedAtUtc', 'capturedAtUtc')) {
            if ($value.Contains($key)) { $value[$key] = $doc.RootElement.GetProperty($key).GetString() }
        }
    } finally { $doc.Dispose() }
    return $value
}
function Get-Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Get-PartPath($Fixture, [string]$Name) { return Join-Path $Fixture.Checkpoint $script:Names[$Name] }
function Save-Receipt($Fixture) { Write-Json $Fixture.ReceiptPath $Fixture.Receipt }
function Set-PartDocument($Fixture, [string]$Name, $Document, [switch]$RebindObservedSha) {
    $path = Get-PartPath $Fixture $Name
    Write-Json $path $Document
    if ($RebindObservedSha) {
        ($Fixture.Receipt.components | Where-Object name -CEQ $Name).sha256 = Get-Hash $path
        Save-Receipt $Fixture
    }
}
function New-Fixture([ValidateSet('qualified', 'main-only', 'refused', 'missing')][string]$Mode = 'qualified') {
    $script:Sequence++
    $root = Join-Path $script:TemporaryRoot ('case-' + $script:Sequence.ToString('D3'))
    $cp = Join-Path $root 'Checkpoint'; $reports = Join-Path $root 'Reports'
    [void][IO.Directory]::CreateDirectory($cp); [void][IO.Directory]::CreateDirectory($reports)
    $checksums = @{ main = '1111111111111111'; spawn = '2222222222222222'; hand = '3333333333333333'
        qualification = '4444444444444444'; layout = '5555555555555555'; charge = '6666666666666666'; turn = '7777777777777777' }
    $schemas = @{ main = 2; spawn = 1; hand = 3; qualification = 2; layout = 13; charge = 3; turn = 2 }
    $kinds = @{ main = 'route-c-substage-restart'; spawn = 'route-c-exact-spawn-plan'; hand = 'route-c-exact-player-hand-health'
        qualification = 'route-c-exact-player-qualification'; layout = 'route-c-exact-player-field'
        charge = 'route-c-exact-character-charge'; turn = 'route-c-exact-turn-progress' }
    foreach ($name in $script:Names.Keys) {
        $d = [ordered]@{ schemaVersion = $schemas[$name]; kind = $kinds[$name]; capturedAtUtc = $script:Time
            gameExecutableSha256 = ('A' * 64); gameExecutableSize = 82718720; sourceLevelName = 'synthetic-test-only'; waveIndex = 2 }
        if ($name -cne 'main') { $d.routeCPayloadChecksum = $checksums.main }
        if ($name -cin @('layout', 'qualification')) { $d.qualificationPolicy = 'flags-baseline-bow-once-v2' }
        if ($name -ceq 'layout') { $d.playerHandHealthChecksum = $checksums.hand }
        if ($name -ceq 'qualification') { $d.layoutKind = $kinds.layout; $d.layoutPayloadChecksum = $checksums.layout }
        $d.payloadChecksum = $checksums[$name]
        Write-Json (Join-Path $cp $script:Names[$name]) $d
    }
    $failure = [ordered]@{ stage = 'layout'; category = 'observation-refused'; reason = '合成拒绝原因 / unsupported FIELD state' }
    $r = [ordered]@{ schemaVersion = 1; kind = 'route-c-capture-outcome'; attemptId = '1234:manual:1'; origin = 'manual'
        attemptedAtUtc = $script:Time; finishedAtUtc = $script:Time; capturedAtUtc = $script:Time
        mainPayloadChecksum = $checksums.main; gameExecutableSha256 = ('A' * 64); sourceLevelName = 'synthetic-test-only'
        waveIndex = 2; automaticDeadlineTicks = 0; layoutKind = $kinds.layout; layoutSchema = 13
        qualificationPolicy = 'flags-baseline-bow-once-v2'; qualificationLayoutChecksum = $checksums.layout
        qualificationLayoutKind = $kinds.layout; qualificationMainChecksum = $checksums.main; handRequiredChecksum = $checksums.hand
        outcome = 'player-bundle-committed'; playerBundle = 'committed-layout-and-qualification'
        allSelectedComponentsCommitted = $true; diagnosticComplete = $true; fullPlayerExactRestorationProven = $false
        invalidationStarted = $true; invalidationCompleted = $true; firstFailure = $null; firstPlayerRefusal = $null
        components = @(); events = @() }
    foreach ($name in $script:Names.Keys) {
        $r.components += [ordered]@{ name = $name; selected = $true; requiredForPlayer = ($name -cin @('hand', 'qualification', 'layout'))
            status = 'committed'; path = (Join-Path $cp $script:Names[$name]); payloadChecksum = $checksums[$name]
            sha256 = (Get-Hash (Join-Path $cp $script:Names[$name])); hashStatus = 'observed-after-write'; error = '' }
        $r.events += [ordered]@{ stage = $name; category = 'selected'; reason = $checksums[$name] }
    }
    foreach ($name in $script:Names.Keys) { $r.events += [ordered]@{ stage = $name; category = 'committed'; reason = 'write_file_atomically returned after MoveFileEx success' } }
    if ($Mode -cin @('main-only', 'refused', 'missing')) {
        foreach ($key in @('layoutKind', 'qualificationPolicy', 'qualificationLayoutChecksum', 'qualificationLayoutKind', 'qualificationMainChecksum', 'handRequiredChecksum')) { $r[$key] = '' }
        $r.layoutSchema = 0; $r.playerBundle = 'unavailable'; $r.events = @($failure)
        $r.firstFailure = $failure; $r.firstPlayerRefusal = $failure; $r.outcome = 'main-only'
        foreach ($c in $r.components) {
            if ($c.name -cnotin @('main', 'spawn')) {
                $c.selected = $false; $c.requiredForPlayer = $false; $c.status = 'not-selected'
                $c.path = ''; $c.payloadChecksum = ''; $c.sha256 = ''; $c.hashStatus = 'not-attempted'
                [IO.File]::Delete((Join-Path $cp $script:Names[$c.name]))
            }
        }
        if ($Mode -ceq 'refused') {
            $r.outcome = 'rejected-before-main'; $r.allSelectedComponentsCommitted = $false
            $r.invalidationStarted = $false; $r.invalidationCompleted = $false; $r.firstPlayerRefusal = $null
            $r.firstFailure = [ordered]@{ stage = 'capture'; category = 'terminal-exception'; reason = 'The battle is not in the stable OPEN state' }
            $r.events = @($r.firstFailure); $r.capturedAtUtc = ''; $r.mainPayloadChecksum = ''; $r.gameExecutableSha256 = ''
            $r.sourceLevelName = ''; $r.waveIndex = -1
            foreach ($c in $r.components) {
                $c.selected = $false; $c.requiredForPlayer = $false; $c.status = 'not-selected'
                $c.path = ''; $c.payloadChecksum = ''; $c.sha256 = ''; $c.hashStatus = 'not-attempted'
            }
            # Unrelated old bytes predate this attempt; the receipt must not claim them.
            Write-Json (Join-Path $cp $script:Names.main) @{ schemaVersion = 1; kind = 'route-c-substage-restart'; payloadChecksum = '8888888888888888' }
            Write-Json (Join-Path $cp 'route-c-exact-player-zones.json') @{ schemaVersion = 5; payloadChecksum = '9999999999999999' }
        }
    }
    $fixture = [pscustomobject]@{ Root = $root; Checkpoint = $cp; ReceiptPath = (Join-Path $reports 'route-c-capture-latest.json'); Receipt = $r }
    if ($Mode -cne 'missing') { Save-Receipt $fixture }
    return $fixture
}
function Assert-State($Result, [string]$Receipt, [string]$Relation, [string]$Consistency = 'two-pass-sampled') {
    Assert-NoRestoreClaim $Result
    Assert-Equal $Result.receiptStatus $Receipt 'receipt status'
    Assert-Equal $Result.relation $Relation 'current file relation'
    Assert-Equal $Result.readConsistency $Consistency 'read consistency'
}
function Test-InvalidReceipt([string]$Name, [scriptblock]$Mutation, [string]$Expected = 'invalid') {
    Test-Case $Name {
        $f = New-Fixture; & $Mutation $f; Save-Receipt $f
        $s = Get-CheckpointStatus -ModRoot $f.Root
        Assert-State $s $Expected 'unknown'; Assert-Equal $s.exitCode 2 'invalid receipt exit'
        Assert-True ($null -eq $s.lastAttempt) 'invalid receipt must not promote history'
    }
}
function Invoke-WithReader([string]$Root, [scriptblock]$Reader) {
    # The production export/CLI cannot accept a callback. Invoke the unexported
    # core in module scope solely for deterministic temporary-directory races.
    return & $script:StatusModule { param($root, $reader) Get-CheckpointStatusCore -ModRoot $root -Reader $reader } $Root $Reader
}
function New-TestReader([Collections.Generic.List[object]]$Calls, [scriptblock]$BeforeRead) {
    return {
        param([string]$Path, [int]$Pass)
        $Calls.Add([pscustomobject]@{ path = $Path; pass = $Pass })
        if ($BeforeRead) { & $BeforeRead $Path $Pass }
        if (![IO.File]::Exists($Path)) { return $null }
        return ,([IO.File]::ReadAllBytes($Path))
    }.GetNewClosure()
}
function Invoke-Entry([string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Get-Process -Id $PID).Path
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $script:Utf8; $start.StandardErrorEncoding = $script:Utf8
    # A redirected Windows child can inherit an OEM output codepage. Set the
    # child encoding explicitly without changing the viewer or its parameters.
    $parameters = @{}
    for ($i = 0; $i -lt $Arguments.Count; $i++) {
        switch -CaseSensitive ($Arguments[$i]) {
            '-Json' { $parameters.Json = $true }
            '-ModRoot' { $i++; $parameters.ModRoot = $Arguments[$i] }
            '-GameRoot' { $i++; $parameters.GameRoot = $Arguments[$i] }
            default { throw 'Unsupported test entry argument.' }
        }
    }
    $payload = [Convert]::ToBase64String($script:Utf8.GetBytes((ConvertTo-Json -InputObject @{ path = $entryPath; parameters = $parameters } -Compress)))
    $command = '$ErrorActionPreference="Stop"; [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); $inv=ConvertFrom-Json -AsHashtable ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("' + $payload + '"))); $bound=$inv.parameters; & $inv.path @bound; exit $LASTEXITCODE'
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    foreach ($arg in @('-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded)) { $start.ArgumentList.Add($arg) }
    $p = [Diagnostics.Process]::Start($start)
    try {
        $stdoutTask = $p.StandardOutput.ReadToEndAsync(); $stderrTask = $p.StandardError.ReadToEndAsync()
        if (!$p.WaitForExit(20000)) { $p.Kill(); throw 'Test child viewer timed out.' }
        return [pscustomobject]@{ code = $p.ExitCode; stdout = $stdoutTask.GetAwaiter().GetResult(); stderr = $stderrTask.GetAwaiter().GetResult() }
    } finally { $p.Dispose() }
}

Test-Case 'qualified bytes match; capture history is not restore admission' {
    $f = New-Fixture; $s = Get-CheckpointStatus -ModRoot $f.Root
    Assert-State $s 'valid' 'matching-files'; Assert-Equal $s.exitCode 0 'observed exit'
    Assert-Equal $s.lastAttempt.playerBundle 'committed-layout-and-qualification' 'historical narrow bundle'
    Assert-Equal @($s.components | Where-Object currentStatus -CEQ 'matching').Count 7 'seven matching recorded parts'
}
Test-Case 'main-only history and matching files remain separate from player bundle' {
    $f = New-Fixture 'main-only'; $s = Get-CheckpointStatus -ModRoot $f.Root
    Assert-State $s 'valid' 'matching-files'; Assert-Equal $s.lastAttempt.outcome 'main-only' 'actual history'
    Assert-Equal $s.lastAttempt.playerBundle 'unavailable' 'no invented player bundle'
    Assert-Equal $s.lastAttempt.firstPlayerRefusal.reason $f.Receipt.firstPlayerRefusal.reason 'raw reason retained'
}
Test-Case 'refused attempt plus legacy files does not qualify current files' {
    $f = New-Fixture 'refused'; $s = Get-CheckpointStatus -ModRoot $f.Root
    Assert-State $s 'valid' 'no-publication'; Assert-Equal $s.lastAttempt.outcome 'rejected-before-main' 'actual refusal'
    Assert-Equal ($s.components | Where-Object name -CEQ 'main').currentStatus 'present-unbound' 'old main unbound'
}
Test-Case 'missing receipt does not infer qualification from existing files' {
    $f = New-Fixture 'missing'; $s = Get-CheckpointStatus -ModRoot $f.Root
    Assert-State $s 'missing' 'unknown'; Assert-Equal $s.exitCode 2 'missing receipt exit'
}
foreach ($item in @(@('broken JSON', '{'), @('truncated JSON', '{"schemaVersion":1,'), @('trailing comma JSON', '{"schemaVersion":1,}'))) {
    $label = $item[0]; $text = $item[1]
    Test-Case $label { $f = New-Fixture; [IO.File]::WriteAllText($f.ReceiptPath, $text, $script:Utf8); Assert-State (Get-CheckpointStatus -ModRoot $f.Root) 'invalid' 'unknown' }
}
Test-Case 'invalid UTF8 is rejected' {
    $f = New-Fixture; [IO.File]::WriteAllBytes($f.ReceiptPath, [byte[]]@(0xff, 0xfe, 0xc0, 0x80))
    Assert-State (Get-CheckpointStatus -ModRoot $f.Root) 'invalid' 'unknown'
}
foreach ($scope in @('top', 'component', 'failure', 'event', 'case-alias')) {
    Test-Case "duplicate JSON key: $scope" {
        $f = New-Fixture 'main-only'; $raw = [IO.File]::ReadAllText($f.ReceiptPath)
        $raw = switch ($scope) {
            'top' { $raw.Replace('"schemaVersion":1', '"schemaVersion":1,"schemaVersion":1') }
            'component' { $raw.Replace('"name":"main"', '"name":"main","name":"main"') }
            'failure' { $raw.Replace('"firstFailure":{"stage":"layout"', '"firstFailure":{"stage":"layout","stage":"layout"') }
            'event' { $raw.Replace('"events":[{"stage":"layout"', '"events":[{"stage":"layout","stage":"layout"') }
            'case-alias' { $raw.Replace('"schemaVersion":1', '"schemaVersion":1,"SchemaVersion":1') }
        }
        [IO.File]::WriteAllText($f.ReceiptPath, $raw, $script:Utf8)
        Assert-State (Get-CheckpointStatus -ModRoot $f.Root) 'invalid' 'unknown'
    }
}
Test-InvalidReceipt 'unknown schema is unsupported' { param($f) $f.Receipt.schemaVersion = 99 } 'unsupported'
Test-InvalidReceipt 'string schema is not an integer' { param($f) $f.Receipt.schemaVersion = '1' }
Test-InvalidReceipt 'Boolean schema is not an integer' { param($f) $f.Receipt.schemaVersion = $true }
Test-InvalidReceipt 'fractional wave is not an integer' { param($f) $f.Receipt.waveIndex = 2.5 }
Test-InvalidReceipt 'string Boolean is not Boolean' { param($f) $f.Receipt.diagnosticComplete = 'true' }
Test-InvalidReceipt 'missing required field' { param($f) $f.Receipt.Remove('layoutSchema') }
Test-InvalidReceipt 'extra top field' { param($f) $f.Receipt.extraField = 1 }
Test-InvalidReceipt 'duplicate component identity' { param($f) $f.Receipt.components[1].name = 'main' }
Test-InvalidReceipt 'unknown component status' { param($f) $f.Receipt.components[0].status = 'verified' }
Test-InvalidReceipt 'false producer full-state claim rejected' { param($f) $f.Receipt.fullPlayerExactRestorationProven = $true }
Test-InvalidReceipt 'outcome contradicts components' { param($f) $f.Receipt.outcome = 'main-only' }
Test-InvalidReceipt 'bundle contradicts dependencies' { param($f) $f.Receipt.qualificationLayoutChecksum = '0000000000000000' }
Test-InvalidReceipt 'attempt origin mismatch' { param($f) $f.Receipt.origin = 'automatic' }
Test-InvalidReceipt 'invalid UTC timestamp' { param($f) $f.Receipt.finishedAtUtc = 'today' }
Test-InvalidReceipt 'too many events' { param($f) $f.Receipt.events = @(1..129 | ForEach-Object { @{ stage = 'main'; category = 'selected'; reason = '' } }) }
Test-InvalidReceipt 'main receipt payload differs from component' { param($f) $f.Receipt.mainPayloadChecksum = '0000000000000000'; $f.Receipt.qualificationMainChecksum = '0000000000000000' }
Test-InvalidReceipt 'selected layout missing required flag' { param($f) $f.Receipt.components[4].requiredForPlayer = $false }
Test-InvalidReceipt 'selected qualification missing required flag' { param($f) $f.Receipt.components[3].requiredForPlayer = $false }
Test-InvalidReceipt 'bound HAND missing required flag' { param($f) $f.Receipt.components[2].requiredForPlayer = $false }
Test-InvalidReceipt 'manual request with automatic deadline' { param($f) $f.Receipt.automaticDeadlineTicks = 10 }
Test-InvalidReceipt 'automatic request with missing deadline' { param($f) $f.Receipt.attemptId = '1234:automatic:1'; $f.Receipt.origin = 'automatic' }
foreach ($key in @('path', 'payloadChecksum', 'error')) {
    Test-Case "unselected component cannot carry stale data: $key" {
        $f = New-Fixture 'main-only'; $f.Receipt.components[3][$key] = 'unexpected'; Save-Receipt $f
        Assert-State (Get-CheckpointStatus -ModRoot $f.Root) 'invalid' 'unknown'
    }
}
Test-Case 'incomplete diagnostics cannot prove a refused attempt published nothing' {
    $f = New-Fixture 'refused'; $f.Receipt.diagnosticComplete = $false; Save-Receipt $f
    $s = Get-CheckpointStatus -ModRoot $f.Root; Assert-State $s 'valid' 'unknown'; Assert-Equal $s.exitCode 2 'diagnostic gap remains unknown'
}

foreach ($part in @('main', 'layout', 'qualification', 'hand', 'charge', 'turn')) {
    Test-Case "missing recorded part: $part" {
        $f = New-Fixture; [IO.File]::Delete((Get-PartPath $f $part))
        $expected = if ($part -ceq 'main') { 'stale' } else { 'incomplete' }
        $s = Get-CheckpointStatus -ModRoot $f.Root; Assert-State $s 'valid' $expected
        Assert-Equal ($s.components | Where-Object name -CEQ $part).currentStatus 'missing' 'specific missing part'
        Assert-Equal $s.lastAttempt.playerBundle 'committed-layout-and-qualification' 'historical claim preserved'
    }
}
foreach ($part in @('charge', 'turn')) {
    Test-Case "optional SHA drift does not erase history: $part" {
        $f = New-Fixture; $path = Get-PartPath $f $part; [IO.File]::AppendAllText($path, ' ', $script:Utf8)
        $s = Get-CheckpointStatus -ModRoot $f.Root; Assert-State $s 'valid' 'incomplete'
        Assert-Equal ($s.components | Where-Object name -CEQ $part).currentStatus 'changed' 'optional SHA drift'
        Assert-Equal $s.lastAttempt.playerBundle 'committed-layout-and-qualification' 'historical bundle retained'
    }
}
Test-Case 'same size and mtime changed main is stale by bytes' {
    $f = New-Fixture; $path = Get-PartPath $f 'main'; $time = [IO.File]::GetLastWriteTimeUtc($path)
    $bytes = [IO.File]::ReadAllBytes($path); $length = $bytes.Length
    $offset = [IO.File]::ReadAllText($path).IndexOf('synthetic-test-only', [StringComparison]::Ordinal)
    Assert-True ($offset -gt 0) 'fixture marker present'; $bytes[$offset] = [byte][char]'S'
    [IO.File]::WriteAllBytes($path, $bytes); [IO.File]::SetLastWriteTimeUtc($path, $time)
    Assert-Equal ([IO.FileInfo]::new($path)).Length $length 'same length'
    Assert-Equal ([IO.File]::GetLastWriteTimeUtc($path)) $time 'same mtime'
    Assert-State (Get-CheckpointStatus -ModRoot $f.Root) 'valid' 'stale'
}
foreach ($part in @('main', 'qualification')) {
    Test-Case "payload FNV alone is insufficient without observed SHA: $part" {
        $f = New-Fixture; $c = $f.Receipt.components | Where-Object name -CEQ $part
        $c.sha256 = ''; $c.hashStatus = 'unavailable'; Save-Receipt $f
        $s = Get-CheckpointStatus -ModRoot $f.Root
        Assert-State $s 'valid' $(if ($part -ceq 'main') { 'unknown' } else { 'incomplete' })
        Assert-Equal ($s.components | Where-Object name -CEQ $part).currentStatus 'unbound' 'missing observed SHA'
    }
}
foreach ($mutation in @(
    @{ name = 'Q layout binding'; part = 'qualification'; field = 'layoutPayloadChecksum'; value = '0000000000000000' },
    @{ name = 'Q main binding'; part = 'qualification'; field = 'routeCPayloadChecksum'; value = '0000000000000000' },
    @{ name = 'Q policy binding'; part = 'qualification'; field = 'qualificationPolicy'; value = 'flags-baseline-v1' },
    @{ name = 'layout HAND binding'; part = 'layout'; field = 'playerHandHealthChecksum'; value = '0000000000000000' },
    @{ name = 'HAND main binding'; part = 'hand'; field = 'routeCPayloadChecksum'; value = '0000000000000000' },
    @{ name = 'sidecar capture time'; part = 'turn'; field = 'capturedAtUtc'; value = '2000-01-01T00:00:00Z' },
    @{ name = 'sidecar EXE'; part = 'charge'; field = 'gameExecutableSha256'; value = ('B' * 64) },
    @{ name = 'sidecar wave'; part = 'spawn'; field = 'waveIndex'; value = 3 },
    @{ name = 'sidecar string wave'; part = 'spawn'; field = 'waveIndex'; value = '2' },
    @{ name = 'unknown main schema'; part = 'main'; field = 'schemaVersion'; value = 99 },
    @{ name = 'unknown Q schema'; part = 'qualification'; field = 'schemaVersion'; value = 99 }
)) {
    Test-Case "matching SHA does not excuse header mismatch: $($mutation.name)" {
        $f = New-Fixture; $d = Read-FixtureJson (Get-PartPath $f $mutation.part); $d[$mutation.field] = $mutation.value
        Set-PartDocument $f $mutation.part $d -RebindObservedSha
        Assert-State (Get-CheckpointStatus -ModRoot $f.Root) 'valid' 'incomplete'
    }
}
Test-Case 'orphan Q and old extra layout cannot create a player bundle' {
    $f = New-Fixture 'main-only'
    Write-Json (Get-PartPath $f 'qualification') @{ schemaVersion = 2; kind = 'route-c-exact-player-qualification'; payloadChecksum = '4444444444444444' }
    Write-Json (Join-Path $f.Checkpoint 'route-c-exact-player-zones.json') @{ schemaVersion = 5; payloadChecksum = '9999999999999999' }
    $s = Get-CheckpointStatus -ModRoot $f.Root; Assert-NoRestoreClaim $s
    Assert-Equal $s.lastAttempt.playerBundle 'unavailable' 'orphan cannot become bundle'
    Assert-Equal ($s.components | Where-Object name -CEQ 'qualification').currentStatus 'present-unbound' 'orphan Q listed'
    Assert-True (@($s.components | Where-Object { $_.path -like '*route-c-exact-player-zones.json' -and $_.currentStatus -ceq 'present-unbound' }).Count -eq 1) 'extra known layout listed'
}
Test-Case 'relocated receipt original paths are discrepancies rather than IO instructions' {
    $f = New-Fixture
    foreach ($c in $f.Receipt.components) { $c.path = Join-Path $script:TemporaryRoot ('original-install/Checkpoint/' + $script:Names[$c.name]) }; Save-Receipt $f
    $s = Get-CheckpointStatus -ModRoot $f.Root; Assert-State $s 'valid' 'incomplete'
    Assert-True (@($s.issues).Count -gt 0) 'path discrepancy explained'
}
foreach ($hostile in @('sentinel', 'relative', 'unc', 'device')) {
    Test-Case "receipt hostile path never read: $hostile" {
        $f = New-Fixture; $sentinel = Join-Path $script:TemporaryRoot ('sentinel-' + [guid]::NewGuid().ToString('N'))
        [IO.File]::WriteAllText($sentinel, 'must not be read', $script:Utf8)
        $f.Receipt.components[0].path = switch ($hostile) { 'sentinel' { $sentinel } 'relative' { '../sentinel' } 'unc' { '\\invalid-host\no-share\sentinel' } 'device' { '\\?\C:\sentinel' } }; Save-Receipt $f
        $calls = [Collections.Generic.List[object]]::new(); $reader = New-TestReader $calls $null
        $s = Invoke-WithReader $f.Root $reader
        Assert-State $s 'valid' 'incomplete'
        $allowed = @((Join-Path $f.Root 'Reports/route-c-capture-latest.json')) + @($script:Names.Values | ForEach-Object { Join-Path $f.Checkpoint $_ }) + @('route-c-exact-player-zones.json', 'route-c-exact-player-trash.json' | ForEach-Object { Join-Path $f.Checkpoint $_ })
        foreach ($call in $calls) { Assert-True ($allowed -icontains $call.path) ('only fixed path read: ' + $call.path) }
        Assert-Equal @($calls | Where-Object path -CEQ $sentinel).Count 0 'outside sentinel never opened'
        Assert-Equal ([IO.File]::ReadAllText($sentinel)) 'must not be read' 'sentinel unchanged'
    }
}
Test-Case 'unknown checkpoint file is not opened' {
    $f = New-Fixture; $unknown = Join-Path $f.Checkpoint 'not-a-component.json'; [IO.File]::WriteAllText($unknown, 'not JSON')
    $calls = [Collections.Generic.List[object]]::new(); $s = Invoke-WithReader $f.Root (New-TestReader $calls $null)
    Assert-State $s 'valid' 'matching-files'; Assert-Equal @($calls | Where-Object path -CEQ $unknown).Count 0 'unknown ordinary file ignored'
}
foreach ($race in @('main', 'receipt', 'population', 'missing-file')) {
    Test-Case "second-pass change prevents combined inference: $race" {
        $f = New-Fixture; $target = if ($race -ceq 'receipt') { $f.ReceiptPath } else { Get-PartPath $f 'main' }
        $changed = @{ done = $false }
        $action = {
            param($path, $pass)
            if ($pass -eq 2 -and !$changed.done) {
                $changed.done = $true
                if ($race -ceq 'population') { [IO.File]::WriteAllText((Join-Path $f.Checkpoint 'new-file.json'), 'x') }
                elseif ($race -ceq 'missing-file') { [IO.File]::Delete((Get-PartPath $f 'hand')) }
                else { [IO.File]::AppendAllText($target, ' ', $script:Utf8) }
            }
        }
        # Population mutation must happen before pass2 population enumeration, so
        # mutate at the last fixed-file read of pass1 for this one scenario.
        if ($race -ceq 'population') {
            $action = { param($path, $pass) if ($pass -eq 1 -and $path -ceq (Get-PartPath $f 'turn')) { [IO.File]::WriteAllText((Join-Path $f.Checkpoint 'new-file.json'), 'x') } }
        }
        $calls = [Collections.Generic.List[object]]::new()
        $s = Invoke-WithReader $f.Root (New-TestReader $calls $action)
        Assert-NoRestoreClaim $s; Assert-Equal $s.readConsistency 'unstable' 'sample drift detected'
        Assert-Equal $s.relation 'unknown' 'discard combined inference'; Assert-True ($null -eq $s.lastAttempt) 'no promoted unstable history'
    }
}
Test-Case 'read failure stays unreadable rather than cached success' {
    $f = New-Fixture; $action = { param($path, $pass) if ($pass -eq 2) { throw [IO.IOException]::new('synthetic sharing denial') } }
    $calls = [Collections.Generic.List[object]]::new()
    $s = Invoke-WithReader $f.Root (New-TestReader $calls $action)
    Assert-NoRestoreClaim $s; Assert-Equal $s.status 'unreadable' 'read error'; Assert-Equal $s.exitCode 3 'read error exit'; Assert-Equal $s.readConsistency 'error' 'read error consistency'
}
Test-Case 'oversize fixed file is bounded' {
    $f = New-Fixture; [IO.File]::WriteAllBytes((Get-PartPath $f 'main'), [byte[]]::new(2097153))
    $s = Get-CheckpointStatus -ModRoot $f.Root; Assert-NoRestoreClaim $s; Assert-Equal $s.status 'unreadable' '2MiB bound'
}
Test-Case 'fixed file directory is not treated as absent' {
    $f = New-Fixture; $path = Get-PartPath $f 'main'; [IO.File]::Delete($path); [void][IO.Directory]::CreateDirectory($path)
    $s = Get-CheckpointStatus -ModRoot $f.Root; Assert-NoRestoreClaim $s; Assert-Equal $s.status 'unreadable' 'nonordinary fixed file rejected'
}
Test-Case 'Checkpoint junction is rejected before a target reader is called' {
    $f = New-Fixture; foreach ($name in $script:Names.Keys) { [IO.File]::Delete((Get-PartPath $f $name)) }
    [IO.Directory]::Delete($f.Checkpoint)
    $target = Join-Path $script:TemporaryRoot ('junction-target-' + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($target); [IO.File]::WriteAllText((Join-Path $target 'sentinel'), 'unread target')
    [void](New-Item -ItemType Junction -Path $f.Checkpoint -Target $target -ErrorAction Stop)
    try {
        $calls = [Collections.Generic.List[object]]::new()
        $s = Invoke-WithReader $f.Root (New-TestReader $calls $null)
        Assert-NoRestoreClaim $s; Assert-Equal $s.status 'unreadable' 'reparse directory rejected'
        Assert-Equal $calls.Count 0 'no files read through junction'
    } finally { Remove-Item -LiteralPath $f.Checkpoint -Force }
}
Test-Case 'known checkpoint hardlink is rejected before reading its target bytes' {
    $f = New-Fixture; $path = Get-PartPath $f 'main'; [IO.File]::Delete($path)
    $target = Join-Path $script:TemporaryRoot ('hardlink-target-' + [guid]::NewGuid().ToString('N'))
    [IO.File]::WriteAllText($target, 'unread hardlink target')
    [void](New-Item -ItemType HardLink -Path $path -Target $target -ErrorAction Stop)
    try {
        $calls = [Collections.Generic.List[object]]::new()
        $s = Invoke-WithReader $f.Root (New-TestReader $calls $null)
        Assert-NoRestoreClaim $s; Assert-Equal $s.status 'unreadable' 'hardlink rejected'
        Assert-Equal $calls.Count 0 'no reader calls before link refusal'
    } finally { Remove-Item -LiteralPath $path -Force }
    Assert-Equal ([IO.File]::ReadAllText($target)) 'unread hardlink target' 'target remains ordinary and unchanged'
}
Test-Case 'entry does not expose Reader test hook' {
    Assert-True (!(Get-Command $entryPath).Parameters.ContainsKey('Reader')) 'CLI has no arbitrary reader callback'
    Assert-True (!(Get-Command Get-CheckpointStatus).Parameters.ContainsKey('Reader')) 'export has no arbitrary reader callback'
}
Test-Case 'terminal text escapes controls while JSON preserves raw refusal' {
    $f = New-Fixture 'main-only'; $rawReason = '中文 "quoted" $(not-an-expression)' + [char]0x1b + '[31m' + "`r`nline2" + [char]0x85
    $f.Receipt.firstFailure.reason = $rawReason; $f.Receipt.firstPlayerRefusal.reason = $rawReason; $f.Receipt.events[0].reason = $rawReason; Save-Receipt $f
    $s = Get-CheckpointStatus -ModRoot $f.Root; Assert-Equal $s.lastAttempt.firstFailure.reason $rawReason 'object preserves data'
    $text = Invoke-Entry @('-ModRoot', $f.Root)
    Assert-Equal $text.code 0 'text CLI exit'; Assert-True (!$text.stdout.Contains([string][char]0x1b)) 'no raw ESC'
    Assert-True (!$text.stdout.Contains([string][char]0x85)) 'no raw C1 control'; Assert-True ($text.stdout.Contains('\u001b')) 'ESC escaped'
    Assert-True ($text.stdout.Contains('\u000d\u000aline2')) 'embedded newline escaped'
    $json = Invoke-Entry @('-ModRoot', $f.Root, '-Json')
    Assert-Equal $json.code 0 'JSON CLI exit'; $parsed = ConvertFrom-Json -InputObject $json.stdout
    Assert-NoRestoreClaim $parsed; Assert-Equal $parsed.lastAttempt.firstFailure.reason $rawReason 'JSON roundtrip raw text'
}
Test-Case 'CLI missing receipt has structured output and ordinary unknown exit' {
    $f = New-Fixture 'missing'; $r = Invoke-Entry @('-ModRoot', $f.Root, '-Json')
    Assert-Equal $r.code 2 'unknown CLI exit'; Assert-State (ConvertFrom-Json $r.stdout) 'missing' 'unknown'
}
Test-Case 'CLI diagnostic gap does not turn raw refusal into a no-publication claim' {
    $f = New-Fixture 'refused'; $f.Receipt.diagnosticComplete = $false; Save-Receipt $f
    $s = Get-CheckpointStatus -ModRoot $f.Root
    Assert-State $s 'valid' 'unknown'; Assert-Equal $s.lastAttempt.diagnosticComplete $false 'diagnostic scope retained'
    $text = Invoke-Entry @('-ModRoot', $f.Root)
    Assert-Equal $text.code 2 'diagnostic gap CLI exit'
    Assert-True ($text.stdout.Contains('诊断不完整，无法确认保存结果')) 'top-line result remains unknown'
    Assert-True (!$text.stdout.Contains('上次保存尝试：已拒绝，未发布新主档')) 'no unqualified no-publication summary'
}

if ($FixtureRoot) {
    # Optional real endpoint cases. The checksums identify immutable receipt
    # source bytes; copied component bytes are independently matched to their SHA.
    $source = (Resolve-Path -LiteralPath $FixtureRoot).Path
    $sourceReceipts = @{
        'qualified-source' = '816FF797AB64A1EB71A001E8B18F082E8B351C7B34542E73CE4D64A194C2CE93'
        'unsupported-source' = '8E54E8C7D9DEB6E169C0848DEBC453BB6793A63ECD4D81058C356188916CBA6D'
        'prompt-refused' = 'C39952415CA20B27B9BD9958224B8B611B0CE8F528E8FDC1241D6021B5E0F73D'
        'prompt-active-refused' = '1FF6C75EBA059EA222BD7F01604503CF84A338DB3E25C82A1E2C723FC7407362'
    }
    foreach ($case in $sourceReceipts.Keys) {
        Test-Case "copied frozen45 actual endpoint: $case" {
            $rp = Join-Path $source "$case/after-receipt.json"
            Assert-Equal (Get-Hash $rp) $sourceReceipts[$case] 'immutable source receipt SHA'
            $f = New-Fixture; foreach ($name in $script:Names.Keys) { [IO.File]::Delete((Get-PartPath $f $name)) }
            $f.Receipt = Read-FixtureJson $rp
            foreach ($name in $script:Names.Keys) {
                $original = Join-Path $source "$case/after-checkpoint/$($script:Names[$name])"
                if ([IO.File]::Exists($original)) { [IO.File]::Copy($original, (Get-PartPath $f $name), $false) }
            }
            foreach ($c in $f.Receipt.components) {
                if ($c.selected) {
                    Assert-Equal (Get-Hash (Get-PartPath $f $c.name)) $c.sha256 'actual copied component SHA'
                    $c.path = Get-PartPath $f $c.name # explicitly derived relocation fixture, not source mutation
                }
            }
            Save-Receipt $f
            $s = Get-CheckpointStatus -ModRoot $f.Root
            Assert-State $s 'valid' $(if ($case -ceq 'prompt-active-refused') { 'no-publication' } else { 'matching-files' })
            Assert-Equal $s.lastAttempt.outcome $f.Receipt.outcome 'directory label does not override actual outcome'
            Assert-Equal (Get-Hash $rp) $sourceReceipts[$case] 'source receipt remains unchanged'
        }
    }
}

$failures = @($script:Tests | Where-Object status -CEQ 'failed')
$kept = $KeepTemporaryFiles.IsPresent -or $failures.Count -gt 0
if (!$kept) {
    # The single generated root is checked before recursive cleanup. No source
    # fixture, installation, receipt-supplied path or computed child is deleted.
    $target = [IO.Path]::GetFullPath($script:TemporaryRoot)
    Assert-True ([IO.Path]::GetDirectoryName($target) -ceq $script:TemporaryParent) 'temporary cleanup parent'
    Assert-True ([IO.Path]::GetFileName($target).StartsWith('QuantumCheckpointStatusTests-', [StringComparison]::Ordinal)) 'temporary cleanup prefix'
    Assert-True (((Get-Item -LiteralPath $target).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'temporary cleanup root is ordinary'
    Remove-Item -LiteralPath $target -Recurse -Force
}
[ordered]@{
    kind = 'checkpoint-status-pure-tests'; schemaVersion = 1
    status = $(if ($failures.Count -eq 0) { 'passed' } else { 'failed' })
    passed = $script:Tests.Count - $failures.Count; failed = $failures.Count
    powerShellVersion = $PSVersionTable.PSVersion.ToString(); minimumDeclaredPowerShellVersion = '7.2'
    tests = $script:Tests.ToArray(); temporaryRoot = $script:TemporaryRoot; temporaryFilesRetained = $kept
    actualCopiedFixturesRequested = [bool]$FixtureRoot
    syntheticHeadersAreRestoreProof = $false; gameplayTested = $false; fullPlayerExactRestorationProven = $false
} | ConvertTo-Json -Depth 30
if ($failures.Count -gt 0) { exit 1 }
exit 0
