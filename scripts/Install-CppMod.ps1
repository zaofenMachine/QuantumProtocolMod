[CmdletBinding()]
param(
    [string]$GameRoot = 'F:\SteamLibrary\steamapps\common\Quantum Protocol',
    [string]$DllPath = (Join-Path $PSScriptRoot '..\build\cpp-vs17-14.38\Output\Game__Shipping__Win64\bin\QuantumCheckpoint.dll'),
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedGameRoot = [System.IO.Path]::GetFullPath($GameRoot)
$resolvedDllPath = [System.IO.Path]::GetFullPath($DllPath, $projectRoot)
$binaryRoot = Join-Path $resolvedGameRoot 'Quantum\Binaries\Win64'
$gameExecutable = Join-Path $binaryRoot 'Quantum-Win64-Shipping.exe'
$modsRoot = Join-Path $binaryRoot 'Mods'
$modsFile = Join-Path $modsRoot 'mods.txt'
$targetDirectory = Join-Path $modsRoot 'QuantumCheckpoint\dlls'
$targetDll = Join-Path $targetDirectory 'main.dll'
$activeManifest = Join-Path $projectRoot 'runtime\cpp-active-deployment.json'

if (-not (Test-Path -LiteralPath $gameExecutable -PathType Leaf)) {
    throw "Quantum Protocol executable was not found: $gameExecutable"
}
if (-not (Test-Path -LiteralPath $resolvedDllPath -PathType Leaf)) {
    throw "Built C++ mod DLL was not found: $resolvedDllPath`nRun .\scripts\Build-CppMod.ps1 first."
}
if (-not (Test-Path -LiteralPath $modsFile -PathType Leaf)) {
    throw "UE4SS mods.txt was not found: $modsFile"
}
if (Test-Path -LiteralPath $activeManifest -PathType Leaf) {
    throw "An active C++ deployment manifest already exists: $activeManifest`nRun .\scripts\Uninstall-CppMod.ps1 before installing another build."
}

$runningProcesses = @(Get-Process -Name 'Quantum-Win64-Shipping', 'Quantum' -ErrorAction SilentlyContinue)
if ($runningProcesses.Count -gt 0) {
    throw 'Quantum Protocol is running. Exit the game completely before deploying the C++ mod.'
}

$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedDllPath).Hash
$modsText = [System.IO.File]::ReadAllText($modsFile)
$lineEnding = if ($modsText.Contains("`r`n")) { "`r`n" } else { "`n" }
$lines = [System.Text.RegularExpressions.Regex]::Split($modsText, '\r?\n')
$modLinePattern = '^\s*QuantumCheckpoint\s*:\s*[01]\s*(?:;.*)?$'
$luaProbeLinePattern = '^\s*QuantumCheckpointProbe\s*:\s*[01]\s*(?:;.*)?$'
$matchingIndexes = @()
for ($index = 0; $index -lt $lines.Count; $index++) {
    if ($lines[$index] -match $modLinePattern) {
        $matchingIndexes += $index
    }
}
if ($matchingIndexes.Count -gt 1) {
    throw "mods.txt contains more than one QuantumCheckpoint entry. Resolve the duplicate entries before installing: $modsFile"
}
if ($matchingIndexes.Count -eq 1) {
    $lines[$matchingIndexes[0]] = 'QuantumCheckpoint : 1'
} else {
    $keybindIndex = -1
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match '^\s*Keybinds\s*:') {
            $keybindIndex = $index
            break
        }
    }

    $updatedLines = [System.Collections.Generic.List[string]]::new()
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($index -eq $keybindIndex) {
            $updatedLines.Add('QuantumCheckpoint : 1')
        }
        $updatedLines.Add($lines[$index])
    }
    if ($keybindIndex -lt 0) {
        if ($updatedLines.Count -gt 0 -and $updatedLines[$updatedLines.Count - 1] -ne '') {
            $updatedLines.Add('')
        }
        $updatedLines.Add('QuantumCheckpoint : 1')
    }
    $lines = $updatedLines.ToArray()
}

$luaProbeIndexes = @()
for ($index = 0; $index -lt $lines.Count; $index++) {
    if ($lines[$index] -match $luaProbeLinePattern) {
        $luaProbeIndexes += $index
    }
}
if ($luaProbeIndexes.Count -gt 1) {
    throw "mods.txt contains more than one QuantumCheckpointProbe entry. Resolve the duplicate entries before installing: $modsFile"
}
if ($luaProbeIndexes.Count -eq 1) {
    $lines[$luaProbeIndexes[0]] = 'QuantumCheckpointProbe : 0'
}
$updatedModsText = [string]::Join($lineEnding, $lines)

Write-Host "Source DLL: $resolvedDllPath"
Write-Host "Source SHA-256: $sourceHash"
Write-Host "Target DLL: $targetDll"
Write-Host 'UE4SS load entry: QuantumCheckpoint : 1'
Write-Host 'Legacy Lua probe: disabled when present to prevent destructive hotkey conflicts'
if ($DryRun) {
    Write-Host 'Dry run completed. No files were changed.'
    exit 0
}

$deploymentId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$backupRoot = Join-Path $projectRoot "backups\cpp\$deploymentId"
$modsBackup = Join-Path $backupRoot 'mods.txt.before-install'
$dllBackup = Join-Path $backupRoot 'main.dll.before-install'
$manifestArchive = Join-Path $backupRoot 'deployment-manifest.json'
$previousDllExists = Test-Path -LiteralPath $targetDll -PathType Leaf

New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
Copy-Item -LiteralPath $modsFile -Destination $modsBackup
if ($previousDllExists) {
    Copy-Item -LiteralPath $targetDll -Destination $dllBackup
}

New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
$temporaryDll = Join-Path $targetDirectory ('.main.dll.' + [guid]::NewGuid().ToString('N') + '.tmp')
Copy-Item -LiteralPath $resolvedDllPath -Destination $temporaryDll
Move-Item -LiteralPath $temporaryDll -Destination $targetDll -Force

$temporaryModsFile = Join-Path $modsRoot ('.mods.txt.' + [guid]::NewGuid().ToString('N') + '.tmp')
[System.IO.File]::WriteAllText($temporaryModsFile, $updatedModsText, [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryModsFile -Destination $modsFile -Force

$installedDllHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $targetDll).Hash
$installedModsHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $modsFile).Hash
if ($installedDllHash -ne $sourceHash) {
    throw "Deployed DLL hash verification failed: $targetDll"
}

$manifest = [PSCustomObject]@{
    schemaVersion = 1
    deploymentId = $deploymentId
    installedAt = (Get-Date).ToString('o')
    gameRoot = $resolvedGameRoot
    modsRoot = $modsRoot
    modsFile = $modsFile
    targetDll = $targetDll
    sourceDll = $resolvedDllPath
    installedDllSha256 = $installedDllHash
    installedModsSha256 = $installedModsHash
    backupRoot = $backupRoot
    modsBackup = $modsBackup
    previousDllExisted = $previousDllExists
    dllBackup = if ($previousDllExists) { $dllBackup } else { $null }
}

$manifestJson = $manifest | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($manifestArchive, $manifestJson, [System.Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $activeManifest) | Out-Null
$temporaryManifest = $activeManifest + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
[System.IO.File]::WriteAllText($temporaryManifest, $manifestJson, [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryManifest -Destination $activeManifest

Write-Host "Installed QuantumCheckpoint C++ mod: $targetDll"
Write-Host "Deployment manifest: $activeManifest"
Write-Host "Rollback backup: $backupRoot"
Write-Host 'The legacy QuantumCheckpointProbe Lua mod is disabled for this C++ deployment.'
