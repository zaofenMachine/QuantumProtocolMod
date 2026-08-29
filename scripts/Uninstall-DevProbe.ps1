[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $projectRoot 'runtime\deployment-manifest.json'

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Deployment manifest not found: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$binaryRoot = [System.IO.Path]::GetFullPath([string]$manifest.binaryRoot)

foreach ($entry in $manifest.files) {
    $target = [System.IO.Path]::GetFullPath((Join-Path $binaryRoot ([string]$entry.relativePath)))
    if (-not $target.StartsWith($binaryRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside the recorded binary directory: $target"
    }

    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        continue
    }

    $currentHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash
    if ($currentHash -ne [string]$entry.sha256) {
        Write-Warning "Preserving modified deployed file: $target"
        continue
    }

    if ($PSCmdlet.ShouldProcess($target, 'Remove deployed probe file')) {
        Remove-Item -LiteralPath $target -Force
    }
}

if ($manifest.backupRoot -and (Test-Path -LiteralPath ([string]$manifest.backupRoot))) {
    Write-Warning "A pre-install backup exists and was not restored automatically: $($manifest.backupRoot)"
}

Write-Host 'Uninstall pass complete. Empty directories were intentionally preserved.'
