[CmdletBinding()]
param(
    [string]$GameRoot = 'F:\SteamLibrary\steamapps\common\Quantum Protocol',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $projectRoot 'vendor\ue4ss-3.0.1-dev'
$binaryRoot = Join-Path $GameRoot 'Quantum\Binaries\Win64'
$gameExecutable = Join-Path $binaryRoot 'Quantum-Win64-Shipping.exe'
$manifestPath = Join-Path $projectRoot 'runtime\deployment-manifest.json'
$backupRoot = Join-Path $projectRoot ('backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))

if (-not (Test-Path -LiteralPath $gameExecutable -PathType Leaf)) {
    throw "Quantum Protocol executable was not found: $gameExecutable"
}

if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'UE4SS.dll') -PathType Leaf)) {
    throw "UE4SS development package is missing under: $sourceRoot"
}

$files = @(
    @{ Source = (Join-Path $sourceRoot 'dwmapi.dll'); Relative = 'dwmapi.dll' },
    @{ Source = (Join-Path $sourceRoot 'UE4SS.dll'); Relative = 'UE4SS.dll' },
    @{ Source = (Join-Path $projectRoot 'deployment\UE4SS-settings.ini'); Relative = 'UE4SS-settings.ini' },
    @{ Source = (Join-Path $projectRoot 'deployment\mods.txt'); Relative = 'Mods\mods.txt' },
    @{ Source = (Join-Path $projectRoot 'src\QuantumCheckpointProbe\Scripts\main.lua'); Relative = 'Mods\QuantumCheckpointProbe\Scripts\main.lua' }
)

$existing = foreach ($entry in $files) {
    $destination = Join-Path $binaryRoot $entry.Relative
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        $destination
    }
}

if ($existing.Count -gt 0 -and -not $Force) {
    throw "Existing UE4SS/deployment files were found. Re-run with -Force to back them up first:`n$($existing -join "`n")"
}

if ($existing.Count -gt 0) {
    foreach ($path in $existing) {
        $relative = [System.IO.Path]::GetRelativePath($binaryRoot, $path)
        $backupPath = Join-Path $backupRoot $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupPath) | Out-Null
        Copy-Item -LiteralPath $path -Destination $backupPath -Force
    }
}

$deployed = foreach ($entry in $files) {
    $destination = Join-Path $binaryRoot $entry.Relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $entry.Source -Destination $destination -Force
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $destination
    [PSCustomObject]@{
        relativePath = $entry.Relative
        sha256 = $hash.Hash
    }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $manifestPath) | Out-Null
[PSCustomObject]@{
    installedAt = (Get-Date).ToString('o')
    gameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
    binaryRoot = (Resolve-Path -LiteralPath $binaryRoot).Path
    backupRoot = if ($existing.Count -gt 0) { $backupRoot } else { $null }
    files = @($deployed)
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host "Installed the checkpoint prototype to: $binaryRoot"
Write-Host "Deployment manifest: $manifestPath"
if ($existing.Count -gt 0) {
    Write-Host "Previous files backed up to: $backupRoot"
}
