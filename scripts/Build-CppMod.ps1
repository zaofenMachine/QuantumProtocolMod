param(
    [string]$UE4SSRoot = (Join-Path $PSScriptRoot '..\vendor\RE-UE4SS-v3.0.1'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build\cpp-vs17-14.38'),
    [string]$Configuration = 'Game__Shipping__Win64'
)

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$resolvedUE4SSRoot = [System.IO.Path]::GetFullPath($UE4SSRoot, $projectRoot)
$resolvedBuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory, $projectRoot)

& (Join-Path $PSScriptRoot 'Check-CppPrerequisites.ps1') -UE4SSRoot $resolvedUE4SSRoot
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$cmakePath = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmakePath -and (Test-Path -LiteralPath $vswhere)) {
    $installations = & $vswhere -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
    foreach ($installationPath in $installations) {
        $candidate = Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $candidate) {
            $cmakePath = $candidate
            break
        }
    }
}
if (-not $cmakePath) {
    throw 'CMake could not be resolved after prerequisite validation.'
}

$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
$previousPath = $env:Path
$previousRustToolchain = $env:RUSTUP_TOOLCHAIN
$previousGitConfigCount = $env:GIT_CONFIG_COUNT
$previousGitConfigKey = $env:GIT_CONFIG_KEY_0
$previousGitConfigValue = $env:GIT_CONFIG_VALUE_0
$env:Path = "$cargoBin;$env:Path"
$env:RUSTUP_TOOLCHAIN = 'nightly-2024-02-14-x86_64-pc-windows-msvc'
# GitHub HTTPS is unreliable on this machine, while its authenticated SSH transport works.
# Apply the rewrite only to child Git processes launched by this script.
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'url.git@github.com:.insteadOf'
$env:GIT_CONFIG_VALUE_0 = 'https://github.com/'

$rustupPath = Join-Path $cargoBin 'rustup.exe'
$rustCompiler = (& $rustupPath which --toolchain $env:RUSTUP_TOOLCHAIN rustc).Trim()
$rustCargo = (& $rustupPath which --toolchain $env:RUSTUP_TOOLCHAIN cargo).Trim()
$ue4ssConfigurations = 'Game__Debug__Win64;Game__Shipping__Win64;Game__Test__Win64;CasePreserving__Debug__Win64;CasePreserving__Shipping__Win64;CasePreserving__Test__Win64'

& $cmakePath -S $projectRoot -B $resolvedBuildDirectory -G 'Visual Studio 17 2022' -A x64 -T 'v143,version=14.38' `
    "-DUE4SS_ROOT=$resolvedUE4SSRoot" `
    "-DCMAKE_CONFIGURATION_TYPES=$ue4ssConfigurations" `
    "-DRust_COMPILER=$rustCompiler" `
    "-DRust_CARGO=$rustCargo" `
    '-DRust_RESOLVE_RUSTUP_TOOLCHAINS=OFF'
if ($LASTEXITCODE -ne 0) {
    $env:Path = $previousPath
    $env:RUSTUP_TOOLCHAIN = $previousRustToolchain
    $env:GIT_CONFIG_COUNT = $previousGitConfigCount
    $env:GIT_CONFIG_KEY_0 = $previousGitConfigKey
    $env:GIT_CONFIG_VALUE_0 = $previousGitConfigValue
    exit $LASTEXITCODE
}

& $cmakePath --build $resolvedBuildDirectory --config $Configuration --target QuantumCheckpointPersistenceTests QuantumCheckpoint
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -eq 0) {
    & $cmakePath --build $resolvedBuildDirectory --config $Configuration --target RUN_TESTS
    $buildExitCode = $LASTEXITCODE
}
$env:Path = $previousPath
$env:RUSTUP_TOOLCHAIN = $previousRustToolchain
$env:GIT_CONFIG_COUNT = $previousGitConfigCount
$env:GIT_CONFIG_KEY_0 = $previousGitConfigKey
$env:GIT_CONFIG_VALUE_0 = $previousGitConfigValue
exit $buildExitCode
