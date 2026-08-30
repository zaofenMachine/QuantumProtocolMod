[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$activeManifest = Join-Path $projectRoot 'runtime\cpp-active-deployment.json'

if (-not (Test-Path -LiteralPath $activeManifest -PathType Leaf)) {
    throw "Active C++ deployment manifest was not found: $activeManifest"
}

$manifest = Get-Content -LiteralPath $activeManifest -Raw | ConvertFrom-Json
$modsRoot = [System.IO.Path]::GetFullPath([string]$manifest.modsRoot)
$modsFile = [System.IO.Path]::GetFullPath([string]$manifest.modsFile)
$targetDll = [System.IO.Path]::GetFullPath([string]$manifest.targetDll)
$backupRoot = [System.IO.Path]::GetFullPath([string]$manifest.backupRoot)

foreach ($target in @($modsFile, $targetDll)) {
    if (-not $target.StartsWith($modsRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the recorded Mods directory: $target"
    }
}

$runningProcesses = @(Get-Process -Name 'Quantum-Win64-Shipping', 'Quantum' -ErrorAction SilentlyContinue)
if ($runningProcesses.Count -gt 0) {
    throw 'Quantum Protocol is running. Exit the game completely before uninstalling the C++ mod.'
}

if (Test-Path -LiteralPath $targetDll -PathType Leaf) {
    $currentDllHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $targetDll).Hash
    if ($currentDllHash -ne [string]$manifest.installedDllSha256) {
        Write-Warning "Preserving a DLL that changed after deployment: $targetDll"
    } elseif ($PSCmdlet.ShouldProcess($targetDll, 'Roll back deployed C++ mod DLL')) {
        $removedDll = Join-Path $backupRoot 'main.dll.removed-on-uninstall'
        Move-Item -LiteralPath $targetDll -Destination $removedDll -Force
        if ([bool]$manifest.previousDllExisted) {
            Copy-Item -LiteralPath ([string]$manifest.dllBackup) -Destination $targetDll
        }
    }
}

if (Test-Path -LiteralPath $modsFile -PathType Leaf) {
    $currentModsHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $modsFile).Hash
    if ($currentModsHash -eq [string]$manifest.installedModsSha256) {
        if ($PSCmdlet.ShouldProcess($modsFile, 'Restore pre-install mods.txt')) {
            Copy-Item -LiteralPath ([string]$manifest.modsBackup) -Destination $modsFile -Force
        }
    } else {
        Write-Warning 'mods.txt changed after deployment; preserving other edits and disabling only QuantumCheckpoint.'
        $modsText = [System.IO.File]::ReadAllText($modsFile)
        $updatedModsText = [regex]::Replace(
            $modsText,
            '(?m)^\s*QuantumCheckpoint\s*:\s*[01]\s*(?:;.*)?$',
            'QuantumCheckpoint : 0'
        )
        if ($PSCmdlet.ShouldProcess($modsFile, 'Disable QuantumCheckpoint in modified mods.txt')) {
            [System.IO.File]::WriteAllText($modsFile, $updatedModsText, [System.Text.UTF8Encoding]::new($false))
        }
    }
}

if ($PSCmdlet.ShouldProcess($activeManifest, 'Archive active deployment marker')) {
    $archivedMarker = Join-Path $backupRoot 'active-manifest.after-uninstall.json'
    Move-Item -LiteralPath $activeManifest -Destination $archivedMarker -Force
}

if ($WhatIfPreference) {
    Write-Host 'Rollback dry run completed. No files were changed.'
} else {
    Write-Host 'QuantumCheckpoint C++ deployment rollback completed.'
}
Write-Host "Rollback files were preserved in: $backupRoot"
