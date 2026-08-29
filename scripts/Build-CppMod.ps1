param(
    [string]$UE4SSRoot = (Join-Path $PSScriptRoot '..\vendor\RE-UE4SS-v3.0.1'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build\cpp'),
    [string]$Configuration = 'Game__Shipping__Win64'
)

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$resolvedUE4SSRoot = [System.IO.Path]::GetFullPath($UE4SSRoot, $projectRoot)
$resolvedBuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory, $projectRoot)

& (Join-Path $PSScriptRoot 'Check-CppPrerequisites.ps1') -UE4SSRoot $resolvedUE4SSRoot
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake -S $projectRoot -B $resolvedBuildDirectory -A x64 "-DUE4SS_ROOT=$resolvedUE4SSRoot"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build $resolvedBuildDirectory --config $Configuration --target QuantumCheckpoint
exit $LASTEXITCODE
