param(
    [string]$UE4SSRoot = (Join-Path $PSScriptRoot '..\vendor\RE-UE4SS-v3.0.1')
)

$resolvedProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$resolvedUE4SSRoot = [System.IO.Path]::GetFullPath($UE4SSRoot, $resolvedProjectRoot)
$problems = [System.Collections.Generic.List[string]]::new()
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

Write-Host "Project: $resolvedProjectRoot"
Write-Host "UE4SS source: $resolvedUE4SSRoot"

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake -and (Test-Path -LiteralPath $vswhere)) {
    $cmakeInstallations = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
    foreach ($installationPath in $cmakeInstallations) {
        $candidate = Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $candidate) {
            $cmake = Get-Item -LiteralPath $candidate
            break
        }
    }
}

if (-not $cmake) {
    $problems.Add('CMake is not installed or not on PATH.')
} else {
    Write-Host "[OK] CMake: $($cmake.Source ?? $cmake.FullName)"
}

if (Test-Path -LiteralPath $vswhere) {
    $installation = & $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($installation) {
        $toolsets = Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like '14.38.*' }
        if ($toolsets) {
            Write-Host "[OK] Visual Studio 2022 C++ tools: $installation (pinned $($toolsets.Name -join ', '))"
        } else {
            $problems.Add('Visual Studio 2022 is present, but the pinned MSVC 14.38 toolset was not found.')
        }
    } else {
        $problems.Add('Visual Studio 2022 x64 C++ build tools were not found.')
    }
} else {
    $problems.Add('Visual Studio 2022 Build Tools / vswhere was not found.')
}

$windowsKitsRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
$windowsSdk = if ($windowsKitsRoot) {
    Get-ChildItem -LiteralPath (Join-Path $windowsKitsRoot 'Include') -Directory -ErrorAction SilentlyContinue |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'um')) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt')) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'shared'))
        } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
}
if ($windowsSdk) {
    Write-Host "[OK] Windows SDK: $($windowsSdk.Name)"
} else {
    $problems.Add('A complete Windows 10/11 SDK was not found.')
}

$rustupPath = (Get-Command rustup -ErrorAction SilentlyContinue).Source
if (-not $rustupPath) {
    $candidate = Join-Path $env:USERPROFILE '.cargo\bin\rustup.exe'
    if (Test-Path -LiteralPath $candidate) {
        $rustupPath = $candidate
    }
}
if ($rustupPath) {
    $rustToolchains = & $rustupPath toolchain list
    if ($rustToolchains -match '^nightly-2024-02-14-x86_64-pc-windows-msvc') {
        Write-Host "[OK] Rust: nightly-2024-02-14-x86_64-pc-windows-msvc"
    } else {
        $problems.Add('Rust nightly-2024-02-14-x86_64-pc-windows-msvc is not installed.')
    }
} else {
    $problems.Add('rustup is not installed.')
}

$requiredFiles = @(
    'CMakeLists.txt',
    'deps\first\Unreal\CMakeLists.txt',
    'deps\first\patternsleuth\Cargo.toml',
    'deps\first\patternsleuth_bind\Cargo.toml'
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
    Write-Host 'UEPseudo access uses the authorized GitHub SSH identity configured for this machine.'
    exit 1
}

Write-Host 'C++ build prerequisites are complete.'
