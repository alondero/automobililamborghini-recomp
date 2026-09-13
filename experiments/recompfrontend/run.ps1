param([switch]$Build)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$previewBuild = Join-Path $repo '.recompfrontend-experiment/build'
$previewExe = Join-Path $previewBuild 'lambo_frontend_preview.exe'
$cachePath = Join-Path $previewBuild 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath)) {
    throw 'Configure the preview first; see experiments/recompfrontend/README.md.'
}
$cache = Get-Content -LiteralPath $cachePath
function Get-PreviewCacheValue([string]$Name) {
    $entry = $cache | Where-Object { $_ -match "^${Name}:[^=]+=" } | Select-Object -First 1
    if (-not $entry) { throw "Missing CMake cache entry: $Name" }
    return $entry.Substring($entry.IndexOf('=') + 1)
}
$compiler = Get-PreviewCacheValue 'CMAKE_CXX_COMPILER'
$env:PATH = (Split-Path -Parent $compiler) + ';' + $env:PATH
if ($Build) {
    $cmake = Get-PreviewCacheValue 'CMAKE_COMMAND'
    & $cmake --build $previewBuild --target lambo_frontend_preview -j 8
    if ($LASTEXITCODE -ne 0) { throw 'Preview build failed' }
}
if (-not (Test-Path -LiteralPath $previewExe)) {
    throw 'Build the preview first; see experiments/recompfrontend/README.md.'
}
& $previewExe
exit $LASTEXITCODE
