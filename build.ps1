<#
.SYNOPSIS
    Build Automobili Lamborghini: Recompiled on Windows (MinGW GCC + Ninja).

.DESCRIPTION
    End-to-end build for the N64Recomp static-recompilation port. Mirrors the
    build-windows job in .github/workflows/build-release.yml so a local build
    is byte-for-byte the same flow as a CI release build (modulo the toolchain
    install step). Idempotent: each step detects what's already done and skips.

    Steps:
      1. Toolchain PATH: Python's CMake (v4.x) ahead of MSYS2's buggy 3.25.1,
         and MinGW's bin ahead of any cc shim.
      2. Initialise submodules (recursive; long paths for RT64's deep nesting).
      3. ROM check (skippable via -RomPath; CI forwards its $env:ROM_FILENAME).
      4. Reset the patched dependencies and apply patches/series.txt with
         tools/apply_patches.py (a run that died mid-apply would otherwise
         leave patch-created files that block the next apply).
      5. First CMake configure (without RecompiledFuncs/ yet — that's the
         whole point: build the recompiler tools first).
      6. Build N64RecompCLI + RSPRecomp.
      7. Run them against the ROM to (re)generate RecompiledFuncs/ and
         src/aspMain.cpp (git-ignored; deterministic given the ROM).
      8. SECOND CMake configure — picks up the newly generated sources via
         CONFIGURE_DEPENDS / EXISTS checks. Without this, src/aspMain.cpp is
         silently excluded.
      9. Build lamborghini_modern.exe and run the Windows RDRAM allocation
         regression test.

    The output binary lands at build\lamborghini_modern.exe (run from the repo
    root so the ROM path resolves).

.PARAMETER Clean
    Wipe build/ before configuring. Slow (~5-10 min cold link) but useful when
    chasing stale-link issues with libRecompiledFuncs.a or chasing the
    PATCH_BLOCKS-stale-again class of bug.

.PARAMETER RomPath
    Path to the USA ROM. Defaults to the standard filename at the repo root.
    CI passes its own value via -RomPath $env:ROM_FILENAME so a future rename
    of the ROM doesn't fork the script's hardcoded default from the workflow's
    env-var convention.

.PARAMETER PreserveSubmodules
    Build using already initialized, pinned dependencies without checking out or
    cleaning their files. Patch application remains idempotent and rejects
    conflicts. Use this when a checkout contains local dependency changes.

.EXAMPLE
    .\build.ps1                                   # incremental build
    .\build.ps1 -Clean                            # full clean rebuild
    .\build.ps1 -RomPath 'other-name.z64'         # non-default ROM filename
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$RomPath = 'Automobili Lamborghini (USA).z64',
    [switch]$PreserveSubmodules
)

$ErrorActionPreference = 'Continue'
# NOTE: Set-StrictMode would be nice for catching undefined-variable bugs, but
# it also amplifies native-command stderr (cmake/git warnings, etc.) into
# terminating errors when combined with $ErrorActionPreference, breaking
# builds whose only stderr output is benign (e.g. trailing-whitespace warnings
# from patch 0001, or CMake's "Ignoring extra path from command line: .exe").
# Real failures are caught via explicit `if ($LASTEXITCODE -ne 0)` checks.

# --- Locate worktree root from this script's own path -----------------------
# Resolve the script's own directory through .NET so we get a clean Windows
# path regardless of how the caller invoked us. We then derive the repo root
# from there (this script always lives at the repo root) — this avoids the
# PowerShell-5.1 + Git-for-Windows gotcha where `git rev-parse --show-toplevel`
# returns a forward-slash path like '/f/src/foo' that Set-Location mangles
# to 'F:\f\src\foo' (treating /f as relative 'f' under the current drive).
$ScriptDir = [System.IO.Path]::GetFullPath((Split-Path -Parent $MyInvocation.MyCommand.Path))
Push-Location $ScriptDir
try {
    $RepoRoot = $ScriptDir
    Set-Location $RepoRoot
    Write-Host "[repo] $RepoRoot" -ForegroundColor DarkGray

    # --- 1. Toolchain PATH ----------------------------------------------------
    # Order matters: Python's CMake must beat MSYS2's; MinGW bin must beat any
    # cc shim that may sit ahead of gcc on this box. Anything already on PATH
    # is preserved as the suffix.
    #
    # The Python-Scripts prefix is needed because MSYS2's cmake 3.25.1 is
    # buggy on this box (warns "Ignoring extra path from command line: .exe"
    # and emits Unix paths that Ninja on Windows can't resolve). The fix is
    # to put Python's bundled cmake ahead of MSYS2's. Either:
    #   - set $env:LAMBO_PYTHON_SCRIPTS=<path-to-Python's-Scripts-dir>, OR
    #   - put Python's Scripts dir on PATH yourself, OR
    #   - rely on the auto-discovery below, which finds a Python-for-Windows
    #     install under $env:LOCALAPPDATA\Programs\Python\Python*\Scripts.
    $PythonScripts = $env:LAMBO_PYTHON_SCRIPTS
    if (-not $PythonScripts -and $env:LOCALAPPDATA) {
        $pyRoot = Join-Path $env:LOCALAPPDATA 'Programs\Python'
        if (Test-Path $pyRoot) {
            # Highest Python version's Scripts dir that contains cmake.exe.
            $candidate = Get-ChildItem -Path $pyRoot -Directory -Filter 'Python*' -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName 'Scripts' } |
                Where-Object { Test-Path (Join-Path $_ 'cmake.exe') } |
                Select-Object -First 1
            if ($candidate) { $PythonScripts = $candidate }
        }
    }
    # Native Git for Windows must beat any MSYS/Cygwin git (e.g. devkitPro's
    # C:\devkitPro\msys2\usr\bin\git.exe). CMake's FetchContent/ExternalProject
    # resolve `git` from PATH via find_package(Git); an MSYS-family git invoked
    # outside its own shell can die with a cygheap "add_item ... failed" fatal
    # error, which CMake reports as Git_VERSION='' and the directx-headers
    # fetch aborts. Override the auto-discovery with $env:LAMBO_GIT_CMD if Git
    # for Windows lives somewhere unusual.
    $GitCmd = $env:LAMBO_GIT_CMD
    if (-not $GitCmd) {
        $GitCmd = @(
            (Join-Path $env:ProgramFiles 'Git\cmd'),
            (Join-Path ${env:ProgramFiles(x86)} 'Git\cmd'),
            (Join-Path $env:LOCALAPPDATA 'Programs\Git\cmd')
        ) | Where-Object { $_ -and (Test-Path (Join-Path $_ 'git.exe')) } | Select-Object -First 1
    }
    $MinGW         = 'C:\ProgramData\mingw64\mingw64\bin'
    $NewPrefix     = @($PythonScripts, $MinGW, $GitCmd) | Where-Object { $_ -and (Test-Path $_) }
    if ($NewPrefix.Count -gt 0) {
        $env:PATH = ($NewPrefix -join ';') + ';' + $env:PATH
        Write-Host ("[path] + {0}" -f ($NewPrefix -join '; ')) -ForegroundColor DarkGray
    }

    # --- 2. Verify the toolchain is actually present --------------------------
    $cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
    $gcc   = (Get-Command gcc.exe   -ErrorAction SilentlyContinue).Source
    $gxx   = (Get-Command g++.exe   -ErrorAction SilentlyContinue).Source
    $ninja = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
    $git   = (Get-Command git.exe   -ErrorAction SilentlyContinue).Source
    if (-not $cmake) { throw 'cmake.exe not on PATH. Set $env:LAMBO_PYTHON_SCRIPTS to Python''s Scripts dir, add it to PATH manually, or install CMake.' }
    if (-not $gcc)   { throw 'gcc.exe not on PATH. Add MinGW to $env:PATH (see comment in script).' }
    if (-not $gxx)   { throw 'g++.exe not on PATH. Add MinGW to $env:PATH.' }
    if (-not $ninja) { throw 'ninja not on PATH. Install it (pip install ninja) and put on PATH.' }
    if (-not $git)   { throw 'git.exe not on PATH. Install Git for Windows (or set $env:LAMBO_GIT_CMD to its cmd\ dir).' }
    Write-Host "[tools] cmake=$cmake" -ForegroundColor DarkGray
    Write-Host "[tools] gcc=$gcc"     -ForegroundColor DarkGray
    Write-Host "[tools] g++=$gxx"     -ForegroundColor DarkGray
    Write-Host "[tools] ninja=$ninja" -ForegroundColor DarkGray
    Write-Host "[tools] git=$git"     -ForegroundColor DarkGray
    if ($git -match '\\(msys2?|cygwin)\\') {
        Write-Warning "git resolved to an MSYS/Cygwin build ($git); CMake's dependency fetch may crash with a cygheap 'add_item' fatal error. Install Git for Windows or set `$env:LAMBO_GIT_CMD."
    }

    # Sanity check: reject MSYS2's 3.25 cmake if it slipped back onto PATH first.
    $cmakeVersion = (& $cmake --version | Select-Object -First 1) -replace 'cmake version ', ''
    if ($cmakeVersion -like '3.25*') {
        Write-Warning "cmake $cmakeVersion looks like MSYS2's buggy 3.25.1; move $PythonScripts ahead of MSYS2 on PATH."
    }

    # --- 3. ROM check ---------------------------------------------------------
    if (-not (Test-Path $RomPath)) {
        throw "Missing ROM: $RomPath. Place your legally-dumped USA ROM at the repo root (or pass -RomPath)."
    }

    # --- 4. Submodules --------------------------------------------------------
    # Long paths for the deep RT64 nesting (Windows default path limit = 260).
    # CI sets core.longpaths globally before any git call so subsequent
    # `git apply` invocations for 0004/0005 also honour it (RT64's deep
    # nesting can produce paths >260 chars). Match that here.
    git config --global core.longpaths true | Out-Null
    # The Windows runner's git may inject CRLF on checkout; the patch tool
    # tolerates whitespace-only context differences, but autocrlf=false is
    # also defensive (CI runs this too).
    git config --global core.autocrlf false | Out-Null
    if ($PreserveSubmodules) {
        Write-Host "`n[1/5] Verifying existing submodule pins..." -ForegroundColor Cyan
        $SubmodulePins = @(git submodule status --recursive)
        if ($LASTEXITCODE -ne 0 -or $SubmodulePins.Count -eq 0 -or
            ($SubmodulePins | Where-Object { $_ -match '^[-+U]' })) {
            throw 'PreserveSubmodules requires initialized dependencies at their recorded pins; no files were reset.'
        }
    }
    else {
        Write-Host "`n[1/5] Initialising submodules..." -ForegroundColor Cyan
        git submodule update --init --recursive | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'submodule update failed.' }
    }

    # --- 5. Reset and patch dependencies -------------------------------------
    # The ordinary build reconstructs dependency files: a run that died
    # mid-apply leaves patch-created files behind that would block the next
    # apply. -PreserveSubmodules keeps local dependency edits; the tool then
    # applies only what is missing and stops on conflicts. patches/series.txt
    # names the directories and patches; CMake configure re-checks the series.
    Write-Host "[2/5] Applying patches/series.txt..." -ForegroundColor Cyan
    $PatchArgs = @('tools/apply_patches.py', '--platform', 'windows')
    if (-not $PreserveSubmodules) { $PatchArgs += '--reset' }
    & python @PatchArgs
    if ($LASTEXITCODE -ne 0) { throw 'dependency patches were not applied.' }

    # --- 6. Optional clean ----------------------------------------------------
    if ($Clean -and (Test-Path build)) {
        Write-Host "`n[clean] Removing build/..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force build
    }

    # --- 7. CMake configure (FIRST) -------------------------------------------
    # Pin compilers explicitly: on this box a `cc` shim may sit ahead of gcc in
    # PATH and CMake's auto-detection would otherwise pick it up — "C compiler
    # is broken" at project() time.
    Write-Host "`n[3/5] Configuring CMake (first pass)..." -ForegroundColor Cyan
    if (-not (Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
    & $cmake -S . -B build -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=gcc.exe `
        -DCMAKE_CXX_COMPILER=g++.exe
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed.' }

    # --- 8. Build the recompiler tools ----------------------------------------
    Write-Host "[4/5] Building N64RecompCLI + RSPRecomp..." -ForegroundColor Cyan
    & $cmake --build build --target N64RecompCLI RSPRecomp -j
    if ($LASTEXITCODE -ne 0) { throw 'recompiler tool build failed.' }

    # --- 9. Regenerate the ROM-derived translations --------------------------
    Write-Host "[4/5] Regenerating RecompiledFuncs/ and src/aspMain.cpp..." -ForegroundColor Cyan
    # Normalise the path to backslashes. PowerShell's `&` call operator hands
    # the literal string to CreateProcessW, which on some Windows builds
    # refuses mixed-separator paths (CI uses pure backslashes for the same reason).
    $n64recomp = (Resolve-Path 'build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp.exe').Path
    $rsprecomp = (Resolve-Path 'build/lib/N64ModernRuntime/librecomp/N64Recomp/RSPRecomp.exe').Path
    if (-not (Test-Path $n64recomp)) { throw "missing tool: $n64recomp" }
    if (-not (Test-Path $rsprecomp)) { throw "missing tool: $rsprecomp" }
    & $n64recomp lamborghini.us.toml
    if ($LASTEXITCODE -ne 0) { throw 'N64Recomp failed.' }
    & $rsprecomp aspMain.us.toml
    if ($LASTEXITCODE -ne 0) { throw 'RSPRecomp failed.' }

    # --- 10. CMake configure (SECOND) -----------------------------------------
    # Required: CMakeLists uses if(EXISTS src/aspMain.cpp) and a CONFIGURE_DEPENDS
    # glob for RecompiledFuncs/. The EXISTS check is configure-time only; the
    # second configure wires in the freshly generated files. Without it, the
    # build silently drops src/aspMain.cpp.
    Write-Host "[4/5] Configuring CMake (second pass - wires in generated sources)..." -ForegroundColor Cyan
    & $cmake -S . -B build -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=gcc.exe `
        -DCMAKE_CXX_COMPILER=g++.exe
    if ($LASTEXITCODE -ne 0) { throw 'cmake second configure failed.' }

    # --- 11. Build lamborghini_modern -----------------------------------------
    Write-Host "`n[5/5] Building lamborghini_modern..." -ForegroundColor Cyan
    & $cmake --build build --target lamborghini_modern -j
    if ($LASTEXITCODE -ne 0) { throw 'lamborghini_modern build failed.' }

    # --- 12. Run the startup-allocation regression ---------------------------
    # This reproduces the old eager 4 GiB commit under a 1 GiB process limit,
    # then proves the production allocator can reserve 4 GiB while committing
    # only its accessible 512 MiB prefix.
    Write-Host "[5/5] Testing Windows RDRAM allocation..." -ForegroundColor Cyan
    & $cmake --build build --target lambo_rdram_allocation_tests -j
    if ($LASTEXITCODE -ne 0) { throw 'RDRAM allocation test build failed.' }
    & ctest.exe --test-dir build -R '^lambo_windows_rdram_reservation$' --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'RDRAM allocation regression test failed.' }

    # --- 13. Done -------------------------------------------------------------
    $exe = Join-Path $RepoRoot 'build\lamborghini_modern.exe'
    if (Test-Path $exe) {
        $stamp = (Get-Item $exe).LastWriteTime
        Write-Host "`n[done] $exe  ($stamp)" -ForegroundColor Green
        Write-Host "Run from repo root:  .\build\lamborghini_modern.exe" -ForegroundColor Green
    } else {
        Write-Warning "lamborghini_modern.exe not found at expected path."
    }
}
finally {
    Pop-Location
}
