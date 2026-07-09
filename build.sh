#!/usr/bin/env bash
#
# Build Automobili Lamborghini: Recompiled on Linux.
#
# Mirrors the build-linux job in .github/workflows/build-release.yml so a local
# build is the same flow as a CI release build (modulo the apt-get install
# step). Idempotent: each step detects what's already done and skips.
#
# Steps:
#   1. Toolchain detection (cmake, gcc, g++, ninja, git, nproc).
#   2. ROM check — the USA ROM is required to translate game code (it's also
#      git-ignored; CI fetches it from a private assets repo, locally you
#      place it next to this script).
#   3. Submodule init (recursive; core.longpaths isn't needed on Linux).
#   4. Defensive submodule reset before patching (half-applied patches from a
#      prior run would otherwise break the next apply with "patch failed: ...").
#   5. Apply Lamborghini patches (Linux: 0001, 0007, 0006 — no MinGW/D3D12
#      fixes needed; no plume patch). 0007 adds the save-state thread-context
#      registry; without it, src/lambo_savestate.c fails to link with
#      "undefined reference to ultramodern_relink_thread_contexts".
#      --ignore-whitespace is a belt-and-suspenders guard for any CRLF drift.
#   6. First CMake configure — the initial one runs before N64Recomp/
#      RSPRecomp generate sources. The CMakeLists uses if(EXISTS) on the
#      ROM-derived files so the first configure can succeed without them.
#   7. Build N64RecompCLI + RSPRecomp tools.
#   8. Run them against the ROM to (re)generate RecompiledFuncs/ and
#      src/aspMain.cpp (git-ignored; deterministic given the ROM).
#   9. SECOND CMake configure — picks up the newly generated sources. The
#      CMakeLists uses CONFIGURE_DEPENDS on RecompiledFuncs/ globs AND
#      if(EXISTS src/aspMain.cpp). The EXISTS check is configure-time only;
#      the second configure wires it in. Without this, src/aspMain.cpp is
#      silently excluded.
#  10. Build lamborghini_modern.
#
# Usage:
#   ./build.sh           # incremental build
#   ./build.sh --clean   # wipe build/ first (slow: ~5-10 min cold link)

set -euo pipefail

# --- 0. Locate repo root from script location --------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- 1. Argument parsing ----------------------------------------------------
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        -h|--help)
            sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "Unknown arg: $arg (try --help)" >&2; exit 1 ;;
    esac
done

# --- 2. Toolchain detection -------------------------------------------------
log()  { printf '\033[1;36m%s\033[0m\n' "$*"; }
warn() { printf '\033[1;33mWARN\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERR\033[0m  %s\n' "$*" >&2; exit 1; }

log "[repo] $(pwd)"

for tool in cmake gcc g++ ninja git; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        die "missing tool: $tool. Install with:
  sudo apt-get install -y --no-install-recommends \\
    build-essential cmake ninja-build python3 pkg-config \\
    libsdl2-dev libvulkan-dev \\
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \\
    libdbus-1-dev"
    fi
done

NPROC="$(nproc)"
log "[tools] cmake=$(command -v cmake)  gcc=$(command -v gcc)  g++=$(command -v g++)  ninja=$(command -v ninja)  nproc=$NPROC"

# --- 3. ROM check -----------------------------------------------------------
# Default to the standard filename; CI overrides via the same ROM_FILENAME
# env var the workflow already defines (workflow env block, .github/workflows/
# build-release.yml). Reading the env var directly (rather than a CLI flag)
# matches the workflow's convention so a future rename stays single-sourced.
ROM="${ROM_FILENAME:-Automobili Lamborghini (USA).z64}"
if [ ! -f "$ROM" ]; then
    die "missing ROM: $ROM
Place your legally-dumped USA ROM at the repo root and re-run."
fi

# --- 4. Submodules ----------------------------------------------------------
log ""
log "[1/5] Initialising submodules..."
git submodule update --init --recursive

# --- 5. Defensive submodule reset (mirrors CI) ------------------------------
log "[2/5] Resetting submodules to clean state before patching..."
git -C lib/N64ModernRuntime checkout -- .
git -C lib/rt64 checkout -- .
git -C lib/rt64/src/contrib/plume checkout -- . 2>/dev/null || true

# --- 6. Apply Lamborghini patches (Linux: 0001, 0007, 0006) ----------------
# Mirrors CI's Linux job exactly (workflow lines 93-95). 0001 then 0007 both
# patch N64ModernRuntime with disjoint hunks (verified to apply sequentially
# on the pinned commit). 0007 adds the save-state thread-context registry +
# `ultramodern_relink_thread_contexts` (issue #22, all platforms). Without it,
# src/lambo_savestate.c fails to link with "undefined reference to
# `ultramodern_relink_thread_contexts`".
log "[2/5] Applying Lamborghini submodule patches..."
PATCHES=(
    "lib/N64ModernRuntime:0001-lamborghini-runtime-scheduler-audio-vi.patch"
    "lib/N64ModernRuntime:0007-ultramodern-savestate-thread-context-relink.patch"
    "lib/rt64:0006-rt64-interp-angular-velocity-matching.patch"
    "lib/rt64:0008-rt64-widescreen-split-subviewport.patch"
)
for entry in "${PATCHES[@]}"; do
    sub="${entry%%:*}"
    patch="${entry#*:}"
    # Patch paths MUST be absolute — `git -C "$sub"` changes CWD into the
    # submodule, so a relative path would resolve to `$sub/patches/$patch`
    # (not the repo root's patches/). CI's original inline step used
    # `$(pwd)/patches/...` for the same reason. Without this, apply --check
    # fails with "can't open patch" and the script dies with a misleading
    # "does not apply cleanly" error.
    patch_abs="$SCRIPT_DIR/patches/$patch"
    # Idempotency check: distinguish three states.
    #   (a) --check 0                                         -> not yet applied
    #   (b) --check !=0 && --reverse --check 0               -> already applied
    #   (c) --check !=0 && --reverse --check !=0             -> context drift
    if git -C "$sub" apply --ignore-whitespace --check "$patch_abs" >/dev/null 2>&1; then
        git -C "$sub" apply --ignore-whitespace "$patch_abs"
        echo "  applied  $sub <- $patch"
    elif git -C "$sub" apply --ignore-whitespace --reverse --check "$patch_abs" >/dev/null 2>&1; then
        echo "  skipped  $sub <- $patch (already applied)"
    else
        die "patch $patch does not apply cleanly to $sub and is not already applied — likely submodule drift (upstream context changed). Re-pull the submodule or refresh the patch."
    fi
done

# --- 7. Optional clean ------------------------------------------------------
if [ "$CLEAN" -eq 1 ] && [ -d build ]; then
    log ""
    log "[clean] Removing build/..."
    rm -rf build
fi

# --- 8. CMake configure (FIRST) ---------------------------------------------
log ""
log "[3/5] Configuring CMake (first pass)..."
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++

# --- 9. Build recompiler tools ----------------------------------------------
log "[4/5] Building N64RecompCLI + RSPRecomp..."
cmake --build build --target N64RecompCLI RSPRecomp -j"$NPROC"

# --- 10. Regenerate ROM-derived translations --------------------------------
log "[4/5] Regenerating RecompiledFuncs/ and src/aspMain.cpp..."
N64RECOMP=build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp
RSPRECOMP=build/lib/N64ModernRuntime/librecomp/N64Recomp/RSPRecomp
[ -x "$N64RECOMP" ] || die "missing tool: $N64RECOMP"
[ -x "$RSPRECOMP" ] || die "missing tool: $RSPRECOMP"
"$N64RECOMP" lamborghini.us.toml
"$RSPRECOMP" aspMain.us.toml

# --- 11. CMake configure (SECOND) -------------------------------------------
log "[4/5] Configuring CMake (second pass — wires in generated sources)..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++

# --- 12. Build lamborghini_modern -------------------------------------------
log ""
log "[5/5] Building lamborghini_modern..."
cmake --build build --target lamborghini_modern -j"$NPROC"

# --- 13. Done ---------------------------------------------------------------
EXE=build/lamborghini_modern
if [ -x "$EXE" ]; then
    log ""
    log "[done] $EXE  ($(stat -c '%y' "$EXE" 2>/dev/null || stat -f '%Sm' "$EXE"))"
    log "Run from repo root:  ./build/lamborghini_modern"
else
    warn "lamborghini_modern not found at expected path: $EXE"
fi