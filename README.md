# Automobili Lamborghini: Recompiled

A native PC port of the Nintendo 64 game **Automobili Lamborghini** (USA), produced by *static recompilation* of the original game code. The N64 machine instructions are translated ahead-of-time into C with [N64Recomp](https://github.com/N64Recomp/N64Recomp), run on the [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) libraries (`ultramodern` + `librecomp`), and rendered with [RT64](https://github.com/rt64/rt64).

This is the same approach used by many recent N64 recompilation projects such as [Snowboard Kids 2 Recompiled](https://github.com/cdlewis/snowboardkids2-recomp), [Smash Bros Recompiled](https://github.com/zestydevy/smash64r), [Bomberman 64 Recompiled](https://github.com/RevoSucks/BM64Recomp). 

## Legal

**This repository contains no game assets or code from the original ROM.** It ships only the recompilation configuration, the runtime/renderer glue code, and a symbol map (function names and addresses recovered by reverse engineering — no ROM bytes).
To build or run the port you must supply your own copy of the original cartridge dump.
Only the **North American (USA) release** is currently supported. The build reads code directly from your ROM to regenerate the recompiled C; that output (`RecompiledFuncs/`) is deliberately git-ignored and never committed.

## Status

This is an in-progress port. It boots, presents the attract/title sequence and menus, and goes in-race. Input, audio, and rendering are wired through RT64. Expect rough edges — see the issue tracker.

## Graphics options

Graphics settings persist in `graphics.json` (in `%LOCALAPPDATA%\LamborghiniRecomp` on
Windows, `~/.config/LamborghiniRecomp` elsewhere; create a `portable.txt` in the
directory you launch from to keep everything there instead). Game saves live in the
same directory. The file is created with
defaults on first run — edit it and relaunch. The schema and vocabulary match the other
N64Recomp ports (Zelda 64: Recompiled et al.):

| Key | Values | Default | Meaning |
| --- | --- | --- | --- |
| `res_option` | `Original`, `Original2x`, `Auto` | `Auto` | Internal render resolution. `Auto` scales with the window size; `Original`/`Original2x` render at 1×/2× of 240p, each supersampled by the `ds_option` factor. |
| `ar_option` | `Original`, `Expand`, `Manual` | `Expand` | Aspect ratio. `Expand` widens the 3D view to the window's aspect (true widescreen, not stretch). |
| `hr_option` | `Original`, `Clamp16x9`, `Full` | `Clamp16x9` | Where edge-pinned HUD elements sit in widescreen (takes effect as HUD elements gain extended-GBI alignment). |
| `rr_option` | `Original`, `Display`, `Manual` | `Display` | Presented framerate. `Display` renders RT64-interpolated frames at the monitor refresh rate — game logic stays at its native 30Hz. `Manual` uses `rr_manual_value`. |
| `msaa_option` | `None`, `MSAA2X`, `MSAA4X`, `MSAA8X` | `MSAA2X` | Anti-aliasing. |
| `hpfb_option` | `Auto`, `On`, `Off` | `Auto` | High-precision framebuffer. |
| `wm_option` | `Windowed`, `Fullscreen` | `Windowed` | Window mode. **F11** or **Alt+Enter** toggles at runtime (and is remembered). |
| `window_width` / `window_height` | pixels | `1600`/`900` | Windowed-mode size. |
| `api_option` | `Auto`, `D3D12`, `Vulkan`, `Metal` | `Auto` | Graphics API. |
| `widescreen_fog_match` | `true`, `false` | `true` | Widens the dense 3P/4P split-screen fog to the open 1P fog window/colour so the extra draw distance shows. Only affects 3+ player races. |
| `widescreen_sky_match` | `true`, `false` | `true` | Draws the sky panorama in 3P/4P split screen (the original skips it, leaving a flat dark backdrop above the horizon). Only affects 3+ player races. |

## Troubleshooting

**Black screen / `vkQueueSubmit failed` spam on Intel graphics.** Intel drivers
older than 31.0.101.2115 trip an RT64 workaround that avoids Direct3D 12; on the
6th-gen HD Graphics parts it was written for, D3D12 removes the device, but on newer
Intel GPUs (Iris Xe, Arc) stuck on an old driver it is the Vulkan fallback that loses
the device. The game now keeps those modern Intel GPUs on Direct3D 12 automatically,
so this should no longer happen out of the box — but the real fix is still to update
the Intel graphics driver (Windows Update or
[intel.com/download-center](https://www.intel.com/content/www/us/en/download-center/home.html)).

**Still a black screen, or a different GPU?** Force a specific backend by setting
`"api_option"` in `graphics.json` to `"D3D12"` or `"Vulkan"`. On Windows the game also
retries the other backend automatically if your chosen one fails to initialise, so a
bad `api_option` no longer strands you on a black screen.

## Developer warp (race-track warp)

For development it is useful to jump straight into a race without driving the menus
(the same pattern as the debug warp menus in other N64Recomp ports):

- **F1–F6** at any time after boot warps to that circuit as a 1-player single race
  (3 laps, first car).
- **`LAMBO_WARP=circuit[:laps[:car[:players]]]`** (environment variable, circuit `1`–`6`)
  performs the warp automatically at boot — handy for headless/scripted runs.

The warp performs the same stores the game's own menu makes when you confirm RACE
(selection cursors + audio quiesce + game-state 7), so the race it starts is a normal
single race. Each warp is logged to stderr as `[warp] CIRCUIT N: ...`.

## Building

See **[BUILDING.md](./BUILDING.md)**. In brief: clone with submodules, supply your ROM, run the recompile step, then configure and build with CMake.

## Acknowledgements

- **Wiseguy** and contributors — [N64Recomp](https://github.com/N64Recomp/N64Recomp) and [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime).
- The **RT64** team — [RT64](https://github.com/rt64/rt64) renderer.
- The **Zelda64Recomp** project, whose CMake wiring served as the template for this one.

## License

The code original to this repository is released under the [GNU General Public License v3.0](./LICENSE), matching the wider N64Recomp port ecosystem (e.g. Zelda 64: Recompiled and Snowboard Kids 2: Recompiled). The vendored submodules (`lib/N64ModernRuntime`, `lib/rt64`) and any patched dependencies retain their own respective licenses.
