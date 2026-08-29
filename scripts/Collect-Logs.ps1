[CmdletBinding()]
param(
    [string]$GameRoot = 'F:\SteamLibrary\steamapps\common\Quantum Protocol'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$binaryRoot = Join-Path $GameRoot 'Quantum\Binaries\Win64'
$destinationRoot = Join-Path $projectRoot ('logs\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$patterns = @('UE4SS.log', 'UE4SS_ObjectDump.txt', 'UE4SS-crash*', 'crash_*')

New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
$copied = @()
foreach ($pattern in $patterns) {
    foreach ($file in Get-ChildItem -LiteralPath $binaryRoot -Filter $pattern -File -ErrorAction SilentlyContinue) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $destinationRoot $file.Name) -Force
        $copied += $file.Name
    }
}
if ($copied.Count -eq 0) {
    Write-Warning "No UE4SS logs were found under: $binaryRoot"
} else {
    Write-Host "Collected logs to: $destinationRoot"
    $copied | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
}
