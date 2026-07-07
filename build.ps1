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
      3. Defensive submodule reset before patching (CI's "Reset submodules
         before patching" step — a half-applied patch from a prior run would
         otherwise break the next apply).
      4. Apply Lamborghini patches (Windows: 0001, 0006, 0005, 0004) with
         --ignore-whitespace (CRLF mismatches on Windows git).
      5. First CMake configure (without RecompiledFuncs/ yet — that's the
         whole point: build the recompiler tools first).
      6. Build N64RecompCLI + RSPRecomp.
      7. Run them against the ROM to (re)generate RecompiledFuncs/ and
         src/aspMain.cpp (git-ignored; deterministic given the ROM).
      8. SECOND CMake configure — picks up the newly generated sources via
         CONFIGURE_DEPENDS / EXISTS checks. Without this, src/aspMain.cpp is
         silently excluded.
      9. Build lamborghini_modern.exe.

    The output binary lands at build\lamborghini_modern.exe (run from the repo
    root so the ROM path resolves).

.PARAMETER Clean
    Wipe build/ before configuring. Slow (~5-10 min cold link) but useful when
    chasing stale-link issues with libRecompiledFuncs.a or chasing the
    PATCH_BLOCKS-stale-again class of bug.

.EXAMPLE
    .\build.ps1            # incremental build
    .\build.ps1 -Clean     # full clean rebuild
#>
[CmdletBinding()]
param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --- Locate worktree root from this script's own path -----------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $ScriptDir
try {
    $RepoRoot = (git rev-parse --show-toplevel).Trim()
    if (-not $RepoRoot) { throw "Not inside a git repository. Run from the worktree root." }
    Set-Location $RepoRoot
    Write-Host "[repo] $RepoRoot" -ForegroundColor DarkGray

    # --- 1. Toolchain PATH ----------------------------------------------------
    # Order matters: Python's CMake must beat MSYS2's; MinGW bin must beat any
    # cc shim that may sit ahead of gcc on this box. Anything already on PATH
    # is preserved as the suffix.
    $PythonScripts = 'C:\Users\alond\AppData\Local\Programs\Python\Python313\Scripts'
    $MinGW         = 'C:\ProgramData\mingw64\mingw64\bin'
    $NewPrefix     = @($PythonScripts, $MinGW) | Where-Object { Test-Path $_ }
    if ($NewPrefix.Count -gt 0) {
        $env:PATH = ($NewPrefix -join ';') + ';' + $env:PATH
        Write-Host ("[path] + {0}" -f ($NewPrefix -join '; ')) -ForegroundColor DarkGray
    }

    # --- 2. Verify the toolchain is actually present --------------------------
    $cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
    $gcc   = (Get-Command gcc.exe   -ErrorAction SilentlyContinue).Source
    $gxx   = (Get-Command g++.exe   -ErrorAction SilentlyContinue).Source
    $ninja = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
    if (-not $cmake) { throw 'cmake.exe not on PATH. Install CMake or extend $PythonScripts.' }
    if (-not $gcc)   { throw 'gcc.exe not on PATH. Add MinGW to $env:PATH (see comment in script).' }
    if (-not $gxx)   { throw 'g++.exe not on PATH. Add MinGW to $env:PATH.' }
    if (-not $ninja) { throw 'ninja not on PATH. Install it (pip install ninja) and put on PATH.' }
    Write-Host "[tools] cmake=$cmake" -ForegroundColor DarkGray
    Write-Host "[tools] gcc=$gcc"     -ForegroundColor DarkGray
    Write-Host "[tools] g++=$gxx"     -ForegroundColor DarkGray
    Write-Host "[tools] ninja=$ninja" -ForegroundColor DarkGray

    # Sanity check: reject MSYS2's 3.25 cmake if it slipped back onto PATH first.
    $cmakeVersion = (& $cmake --version | Select-Object -First 1) -replace 'cmake version ', ''
    if ($cmakeVersion -like '3.25*') {
        Write-Warning "cmake $cmakeVersion looks like MSYS2's buggy 3.25.1; move $PythonScripts ahead of MSYS2 on PATH."
    }

    # --- 3. ROM check ---------------------------------------------------------
    $RomPath = 'Automobili Lamborghini (USA).z64'
    if (-not (Test-Path $RomPath)) {
        throw "Missing ROM: $RomPath. Place your legally-dumped USA ROM at the repo root."
    }

    # --- 4. Submodules --------------------------------------------------------
    # Long paths for the deep RT64 nesting (Windows default path limit = 260).
    # CI sets core.longpaths globally before any git call so subsequent
    # `git apply` invocations for 0004/0005 also honour it (RT64's deep
    # nesting can produce paths >260 chars). Match that here.
    git config --global core.longpaths true | Out-Null
    # The Windows runner's git may inject CRLF on checkout; --ignore-whitespace
    # in the apply step below neutralises that, but autocrlf=false is also
    # defensive (CI runs this too).
    git config --global core.autocrlf false | Out-Null
    Write-Host "`n[1/5] Initialising submodules..." -ForegroundColor Cyan
    git submodule update --init --recursive | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'submodule update failed.' }

    # --- 5. Defensive submodule reset -----------------------------------------
    # Mirrors CI's "Reset submodules before patching" — a previous partial
    # apply would otherwise leave patches failing with "patch failed: ... file:N".
    Write-Host "[2/5] Resetting submodules to clean state before patching..." -ForegroundColor Cyan
    git -C lib/N64ModernRuntime checkout -- . | Out-Null
    git -C lib/rt64 checkout -- . | Out-Null
    git -C lib/rt64/src/contrib/plume checkout -- . | Out-Null

    # --- 6. Apply Lamborghini patches (Windows: 0001, 0006, 0005, 0004) -------
    # Mirrors CI's Windows job exactly. 0007 is intentionally omitted — the
    # current CI doesn't apply it either, suggesting its content has landed
    # upstream or is unnecessary for a Release build.
    Write-Host "[2/5] Applying Lamborghini submodule patches..." -ForegroundColor Cyan
    $patches = @(
        @{ Sub = 'lib/N64ModernRuntime';       Patch = 'patches/0001-lamborghini-runtime-scheduler-audio-vi.patch' },
        @{ Sub = 'lib/rt64';                   Patch = 'patches/0006-rt64-interp-angular-velocity-matching.patch' },
        @{ Sub = 'lib/rt64';                   Patch = 'patches/0005-rt64-mingw-gcc-compat.patch' },
        @{ Sub = 'lib/rt64/src/contrib/plume'; Patch = 'patches/0004-plume-d3d12-mingw-com-abi-struct-return.patch' }
    )
    foreach ($p in $patches) {
        # Idempotency check: distinguish three states.
        #   (a) --check 0      -> not yet applied, will apply cleanly
        #   (b) --check !=0 && --reverse --check 0 -> already applied
        #   (c) --check !=0 && --reverse --check !=0 -> context drift (submodule changed)
        # Without distinguishing (b) from (c), drift silently masquerades as
        # 'already applied' and produces a confusing compile error much later.
        git -C $p.Sub apply --ignore-whitespace --check $p.Patch 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            git -C $p.Sub apply --ignore-whitespace $p.Patch | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "patch failed: $($p.Sub) <- $($p.Patch)" }
            Write-Host "  applied  $($p.Sub) <- $($p.Patch)" -ForegroundColor Green
        } else {
            git -C $p.Sub apply --ignore-whitespace --reverse --check $p.Patch 2>&1 | Out-Null
            if ($LASTEXITCODE -eq 0) {
                Write-Host "  skipped  $($p.Sub) <- $($p.Patch) (already applied)" -ForegroundColor DarkGray
            } else {
                throw "patch $($p.Patch) does not apply cleanly to $($p.Sub) and is not already applied - likely submodule drift (upstream context changed). Re-pull the submodule or refresh the patch."
            }
        }
    }

    # --- 7. Optional clean ----------------------------------------------------
    if ($Clean -and (Test-Path build)) {
        Write-Host "`n[clean] Removing build/..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force build
    }

    # --- 8. CMake configure (FIRST) -------------------------------------------
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

    # --- 9. Build the recompiler tools ----------------------------------------
    Write-Host "[4/5] Building N64RecompCLI + RSPRecomp..." -ForegroundColor Cyan
    & $cmake --build build --target N64RecompCLI RSPRecomp -j
    if ($LASTEXITCODE -ne 0) { throw 'recompiler tool build failed.' }

    # --- 10. Regenerate the ROM-derived translations --------------------------
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

    # --- 11. CMake configure (SECOND) -----------------------------------------
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

    # --- 12. Build lamborghini_modern -----------------------------------------
    Write-Host "`n[5/5] Building lamborghini_modern..." -ForegroundColor Cyan
    & $cmake --build build --target lamborghini_modern -j
    if ($LASTEXITCODE -ne 0) { throw 'lamborghini_modern build failed.' }

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