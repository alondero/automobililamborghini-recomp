<#
.SYNOPSIS
    Copy the current locally generated replacement PNGs into pack staging.

.DESCRIPTION
    The port repository keeps texture output under out/ (ignored). This helper
    seeds a companion-pack source directory without ever copying ROMs or raw
    RT64 capture files. The destination is required and must be outside this
    port checkout so ROM-derived artwork cannot be staged accidentally in the
    main repository.
#>
[CmdletBinding()]
param(
    [string]$InputDirectory = '',
    [string]$DestinationDirectory = ''
)

$ErrorActionPreference = 'Stop'
$PackRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PackRoot '..'))

if (-not $InputDirectory) {
    $InputDirectory = Join-Path $RepoRoot 'out\texture-packs\automobili-enhanced'
}
if (-not $DestinationDirectory) {
    throw "DestinationDirectory is required and must point to an external companion repository (for example C:\src\automobili-lamborghini-textures\textures)."
}

$InputDirectory = [System.IO.Path]::GetFullPath($InputDirectory)
$DestinationDirectory = [System.IO.Path]::GetFullPath($DestinationDirectory)
$portCheckout = $null
if (Test-Path -LiteralPath (Join-Path $RepoRoot '.git')) {
    $portCheckout = $RepoRoot
}
if ($portCheckout) {
    $repoPrefix = $portCheckout.TrimEnd('\') + '\'
    if ($DestinationDirectory.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "DestinationDirectory must be outside the port checkout: $DestinationDirectory"
    }
}
if (-not (Test-Path -LiteralPath $InputDirectory -PathType Container)) {
    throw "Input texture directory does not exist: $InputDirectory"
}

$files = @(Get-ChildItem -LiteralPath $InputDirectory -File -Filter '*.png' | Sort-Object Name)
if ($files.Count -eq 0) {
    throw "No PNG sources found in $InputDirectory"
}

New-Item -ItemType Directory -Force -Path $DestinationDirectory | Out-Null
foreach ($file in $files) {
    # Only hash-named files are valid source inputs. The build validator repeats
    # this check, but failing here makes an accidental dump copy obvious.
    if ($file.BaseName -notmatch '^[0-9a-fA-F]{16}$') {
        throw "Unexpected source filename (expected a 16-hex RT64 hash): $($file.Name)"
    }
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $DestinationDirectory $file.Name) -Force
}

Write-Host "Imported $($files.Count) PNG source(s) into $DestinationDirectory"
Write-Host "Keep this source directory outside the port checkout; commit it only in the companion texture repository."
