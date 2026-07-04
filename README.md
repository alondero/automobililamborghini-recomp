# Automobili Lamborghini: Recompiled

A native PC port of the Nintendo 64 game **Automobili Lamborghini** (USA), produced by *static recompilation* of the original game code. The N64 machine instructions are translated ahead-of-time into C with [N64Recomp](https://github.com/N64Recomp/N64Recomp), run on the [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) libraries (`ultramodern` + `librecomp`), and rendered with [RT64](https://github.com/rt64/rt64).

This is the same approach used by many recent N64 recompilation projects such as [Snowboard Kids 2 Recompiled](https://github.com/cdlewis/snowboardkids2-recomp), [Smash Bros Recompiled](https://github.com/zestydevy/smash64r), [Bomberman 64 Recompiled](https://github.com/RevoSucks/BM64Recomp). 

## Legal

**This repository contains no game assets or code from the original ROM.** It ships only the recompilation configuration, the runtime/renderer glue code, and a symbol map (function names and addresses recovered by reverse engineering — no ROM bytes).
To build or run the port you must supply your own copy of the original cartridge dump.
Only the **North American (USA) release** is currently supported. The build reads code directly from your ROM to regenerate the recompiled C; that output (`RecompiledFuncs/`) is deliberately git-ignored and never committed.

## Status

This is an in-progress port. It boots, presents the attract/title sequence and menus, and goes in-race. Input, audio, and rendering are wired through RT64. Expect rough edges — see the issue tracker.

## Building

See **[BUILDING.md](./BUILDING.md)**. In brief: clone with submodules, supply your ROM, run the recompile step, then configure and build with CMake.

## Acknowledgements

- **Wiseguy** and contributors — [N64Recomp](https://github.com/N64Recomp/N64Recomp) and [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime).
- The **RT64** team — [RT64](https://github.com/rt64/rt64) renderer.
- The **Zelda64Recomp** project, whose CMake wiring served as the template for this one.

## License

The code original to this repository is released under the [GNU General Public License v3.0](./LICENSE), matching the wider N64Recomp port ecosystem (e.g. Zelda 64: Recompiled and Snowboard Kids 2: Recompiled). The vendored submodules (`lib/N64ModernRuntime`, `lib/rt64`) and any patched dependencies retain their own respective licenses.
