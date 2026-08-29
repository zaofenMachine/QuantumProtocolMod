param(
    [Parameter(Mandatory = $true)]
    [string]$ObjectDumpPath,

    [string]$OutputPath = (Join-Path $PSScriptRoot '..\runtime\quantum-schema-inventory.md'),

    [string[]]$Owners = @(
        'CardPlacement',
        'GameDeckRun',
        'RecordableCard',
        'CardInfoInstance',
        'CardInfo',
        'CardUpgradeLevelInfo',
        'DecklistCard',
        'Decklist',
        'ControllerCardGroup',
        'ControllerBoard',
        'ControllerFieldSlot',
        'CardPlacementComponent',
        'InGameCard',
        'CardEngine',
        'SpawnController'
    )
)

$resolvedDump = (Resolve-Path -LiteralPath $ObjectDumpPath).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath, (Get-Location).Path)
$wanted = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$Owners | ForEach-Object { [void]$wanted.Add($_) }

$definitions = @{}
foreach ($owner in $Owners) {
    $definitions[$owner] = [ordered]@{
        Properties = [System.Collections.Generic.List[object]]::new()
        Functions = [System.Collections.Generic.List[string]]::new()
    }
}

$propertyPattern = '^\[[^\]]+\]\s+(?<kind>[A-Za-z0-9_]+Property)\s+/Script/Quantum\.(?<owner>[^:\s]+):(?<member>[^\s\[]+)'
$functionPattern = '^\[[^\]]+\]\s+(?:Function|DelegateFunction)\s+/Script/Quantum\.(?<owner>[^:\s]+):(?<member>[^\s\[]+)'

foreach ($line in [System.IO.File]::ReadLines($resolvedDump)) {
    if ($line -match $propertyPattern -and $wanted.Contains($Matches.owner)) {
        $definitions[$Matches.owner].Properties.Add([pscustomobject]@{
            Type = $Matches.kind
            Name = $Matches.member
        })
        continue
    }

    if ($line -match $functionPattern -and $wanted.Contains($Matches.owner)) {
        $definitions[$Matches.owner].Functions.Add($Matches.member)
    }
}

$outputDirectory = Split-Path -Parent $resolvedOutput
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('# Quantum reflected schema inventory')
$lines.Add('')
$lines.Add("Source: ``$resolvedDump``")
$lines.Add('')
$lines.Add('Generated from an UE4SS object dump. This report lists reflected names only; it does not prove that every runtime state field is reflected or safe to restore.')

foreach ($owner in $Owners) {
    $entry = $definitions[$owner]
    $lines.Add('')
    $lines.Add("## $owner")
    $lines.Add('')
    $lines.Add("Properties: $($entry.Properties.Count); functions: $($entry.Functions.Count)")

    if ($entry.Properties.Count -gt 0) {
        $lines.Add('')
        $lines.Add('| Property | Type |')
        $lines.Add('| --- | --- |')
        foreach ($property in $entry.Properties) {
            $lines.Add("| ``$($property.Name)`` | ``$($property.Type)`` |")
        }
    }

    if ($entry.Functions.Count -gt 0) {
        $lines.Add('')
        $lines.Add('Functions:')
        $lines.Add('')
        foreach ($function in $entry.Functions) {
            $lines.Add("- ``$function``")
        }
    }
}

[System.IO.File]::WriteAllLines($resolvedOutput, $lines, [System.Text.UTF8Encoding]::new($false))
Write-Host "Wrote reflected schema inventory: $resolvedOutput"

foreach ($owner in $Owners) {
    $entry = $definitions[$owner]
    Write-Host ("{0}: properties={1}, functions={2}" -f $owner, $entry.Properties.Count, $entry.Functions.Count)
}
