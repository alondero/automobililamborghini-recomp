<#
.SYNOPSIS
    Create a standalone companion texture-pack checkout from this seed.

.DESCRIPTION
    Copies the tracked pack template and tooling to an external directory,
    imports the locally generated hash-named PNGs, and writes a root rt64.json
    whose paths point into textures/. The destination must not be inside the
    port checkout because the source images originate from runtime captures.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DestinationDirectory,
    [string]$InputDirectory = '',
    [switch]$InitializeGit
)

$ErrorActionPreference = 'Stop'
$SeedRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$PortRoot = [System.IO.Path]::GetFullPath((Join-Path $SeedRoot '..'))
$Destination = [System.IO.Path]::GetFullPath($DestinationDirectory)
$ResolvedInput = if ($InputDirectory) {
    [System.IO.Path]::GetFullPath($InputDirectory)
} else {
    Join-Path $PortRoot 'out\texture-packs\automobili-enhanced'
}
if (-not (Test-Path -LiteralPath $ResolvedInput -PathType Container)) {
    throw "Input texture directory does not exist: $ResolvedInput"
}

$portPrefix = $PortRoot.TrimEnd('\') + '\'
if ($Destination.StartsWith($portPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "DestinationDirectory must be outside the port checkout: $Destination"
}
if (Test-Path -LiteralPath $Destination) {
    $existing = @(Get-ChildItem -LiteralPath $Destination -Force)
    if ($existing.Count -gt 0) {
        throw "DestinationDirectory must be empty: $Destination"
    }
} else {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
}

# Copy the seed without importing the ignored textures/ directory or any local
# build output. The importer below is the only step that copies source images.
foreach ($file in Get-ChildItem -LiteralPath $SeedRoot -File -Recurse) {
    $relative = $file.FullName.Substring($SeedRoot.Length).TrimStart([char[]]@('\', '/'))
    if ($relative -eq 'rt64.json' -or
        ($relative -match '^textures[\\/]' -and $relative -notmatch '^textures[\\/]README\.md$') -or
        $relative -match '^(generated[\\/]|dist[\\/]|raw-dumps[\\/]|decoded[\\/])' -or
        $relative -match '(^|[\\/])__pycache__([\\/]|$)' -or
        $file.Extension -in @('.pyc', '.pyo')) {
        continue
    }
    $target = Join-Path $Destination $relative
    $targetParent = Split-Path -Parent $target
    New-Item -ItemType Directory -Force -Path $targetParent | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
}

$importer = Join-Path $SeedRoot 'tools\import-current-pack.ps1'
$destinationTextures = Join-Path $Destination 'textures'
& $importer -InputDirectory $ResolvedInput -DestinationDirectory $destinationTextures

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    throw 'Python 3.9+ is required to generate rt64.json'
}
$manifest = Join-Path $Destination 'rt64.json'
$policy = Join-Path $Destination 'texture-policy.json'
& $python.Source (Join-Path $Destination 'tools\make_pack.py') $destinationTextures `
    '--manifest' $manifest '--policy' $policy
if ($LASTEXITCODE -ne 0) {
    throw "rt64.json generation failed with exit code $LASTEXITCODE"
}

if ($InitializeGit) {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) {
        throw 'git is required when -InitializeGit is supplied'
    }
    & $git.Source -C $Destination init
    if ($LASTEXITCODE -ne 0) {
        throw "git init failed with exit code $LASTEXITCODE"
    }
}

Write-Host "Created companion pack at $Destination"
Write-Host "Review pack.json and CREDITS.md, then set rights_confirmed=true only after licensing is approved."
