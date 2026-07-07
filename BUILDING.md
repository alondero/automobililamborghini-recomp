# Building

The build has two stages: **(1)** recompile the game code from your ROM into C, then
**(2)** compile everything with CMake. All commands run from the repository root.

## Prerequisites

- **Git**, **CMake ≥ 3.20**, and **Python 3**.
- A C/C++ toolchain:
  - **Linux:** `gcc`/`g++` (C17 / C++20), plus `SDL2`, Vulkan headers/loader, and the
    usual desktop build dependencies.
  - **Windows:** **MinGW-w64 GCC** (MSVC is *not* required). RT64 uses its Direct3D 12
    backend. The MinGW `bin` directory must be on `PATH`, or `gcc.exe` fails to load its
    own DLLs.
- A network connection at configure time (CMake fetches `DirectX-Headers` on Windows).
- **Your own ROM:** `Automobili Lamborghini (USA).z64`, placed in the repository root.
  Only the USA release is currently supported.

## 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/alondero/automobililamborghini-recomp.git
cd automobililamborghini-recomp
# If you already cloned without --recurse-submodules:
git submodule update --init --recursive
```

On Windows, enable long paths for the RT64 submodule's deep test files:

```bash
git -c core.longpaths=true submodule update --init --recursive
```

## 2. Apply the dependency patches

The port needs small compatibility patches applied to the submodule working trees.
The submodules are pinned to their public upstream commits; these patches reproduce the
Lamborghini-specific changes (cooperative scheduler dispatch, VI-mode fallback, 30fps
pacing, and — on Windows — the MinGW/D3D12 COM ABI fixes for RT64/plume).

```bash
# ultramodern / librecomp runtime (all platforms):
git -C lib/N64ModernRuntime apply ../../patches/0001-lamborghini-runtime-scheduler-audio-vi.patch

# ultramodern save-state thread-context relink (issue #22, all platforms):
git -C lib/N64ModernRuntime apply ../../patches/0007-ultramodern-savestate-thread-context-relink.patch

# RT64 renderer — all platforms (frame-interpolation transform matching, issue #30):
git -C lib/rt64 apply "$(pwd)/patches/0006-rt64-interp-angular-velocity-matching.patch"

# RT64 renderer — Windows / MinGW only (absolute paths avoid depth confusion):
git -C lib/rt64 apply "$(pwd)/patches/0005-rt64-mingw-gcc-compat.patch"
git -C lib/rt64/src/contrib/plume apply "$(pwd)/patches/0004-plume-d3d12-mingw-com-abi-struct-return.patch"
```

> The `0001` patch is `git diff <upstream>..<lamborghini>` for N64ModernRuntime and
> applies cleanly onto the pinned commit, reproducing the exact runtime tree the port
> was developed against.

## 3. Recompile the game code from your ROM

This reads your ROM and generates `RecompiledFuncs/` (git-ignored). First build the
N64Recomp CLI (bundled in the runtime submodule), then run it against the config:

```bash
# Regenerate the symbol map + config (optional; committed copies are provided):
python3 scripts/gen_syms_toml.py

# Build the recompiler CLIs, then recompile the game code AND the RSP audio ucode:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target N64RecompCLI RSPRecomp
./build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp lamborghini.us.toml
./build/lib/N64ModernRuntime/librecomp/N64Recomp/RSPRecomp aspMain.us.toml
```

Both recompilers read `rom_file_path` from their `.toml` (defaults to
`Automobili Lamborghini (USA).z64` in the repo root). This produces the git-ignored,
ROM-derived translations `RecompiledFuncs/` (game code) and `src/aspMain.cpp` (audio
microcode) — neither is committed to the repository.

## 4. Build the port

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build --target lamborghini_modern -j
```

### Windows (MinGW GCC)

```bash
export PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH"   # adjust to your MinGW path
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc.exe -DCMAKE_CXX_COMPILER=g++.exe
cmake --build build --target lamborghini_modern -j
```

## 5. Run

Run from the repository root so the ROM path resolves:

```bash
./build/lamborghini_modern
```

## Notes

- `lib/N64ModernRuntime`'s root CMake deliberately omits RT64; it is pulled in only by
  this project's `CMakeLists.txt`.
- `RecompiledFuncs/` is regenerated from your ROM and is never committed. Re-run step 3
  after changing the symbol map or config.
