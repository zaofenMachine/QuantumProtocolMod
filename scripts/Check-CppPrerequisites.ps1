param(
    [string]$UE4SSRoot = (Join-Path $PSScriptRoot '..\vendor\RE-UE4SS-v3.0.1')
)

$resolvedProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$resolvedUE4SSRoot = [System.IO.Path]::GetFullPath($UE4SSRoot, $resolvedProjectRoot)
$problems = [System.Collections.Generic.List[string]]::new()

Write-Host "Project: $resolvedProjectRoot"
Write-Host "UE4SS source: $resolvedUE4SSRoot"

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmake) {
    Write-Host "[OK] CMake: $($cmake.Source)"
} else {
    $problems.Add('CMake is not installed or not on PATH.')
}

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($installation) {
        Write-Host "[OK] Visual Studio C++ tools: $installation"
    } else {
        $problems.Add('Visual Studio is present, but the x64 C++ build tools workload was not found.')
    }
} else {
    $problems.Add('Visual Studio 2022 Build Tools / vswhere was not found.')
}

$requiredFiles = @(
    'CMakeLists.txt',
    'deps\first\Unreal\CMakeLists.txt',
    'deps\first\patternsleuth\CMakeLists.txt'
)

foreach ($relativePath in $requiredFiles) {
    $candidate = Join-Path $resolvedUE4SSRoot $relativePath
    if (Test-Path -LiteralPath $candidate) {
        Write-Host "[OK] $relativePath"
    } else {
        $problems.Add("Missing UE4SS source component: $relativePath")
    }
}

if ($problems.Count -gt 0) {
    Write-Warning 'C++ build prerequisites are incomplete:'
    $problems | ForEach-Object { Write-Warning "  $_" }
    Write-Host 'UEPseudo access requires a GitHub account linked to Epic Games and an accepted EpicGames organization invitation.'
    exit 1
}

Write-Host 'C++ build prerequisites are complete.'
