# Track index — circuit 0-based ↔ 1-based ↔ F-key

The ROM has **no track-name strings**; the menu shows "CIRCUIT 1" through "CIRCUIT 6"
with a numbered map preview (`src/lambo_warp.c:51-55`). So the only labels on disk
are the integer index. This file maps each index to its navigation hotkey and the
only organic category the user can read off the menu: **basic** (1-3) vs **pro**
(4-6). Captured 2026-07-23 from the in-game track-select menu.

## The table

| Index (0-based) | Circuit (1-based) | F-key | Tier | Authored radius (1P) | `no_lod_circuit` default | Status |
|---|---|---|---|---|---|---|
| 0 | 1 | F1 | basic | 55000 | true (PVS synth on) | stable |
| 1 | 2 | F2 | basic | 50000 | true (PVS synth on) | stable |
| 2 | 3 | F3 | basic | 40000 | true (PVS synth on) | stable |
| 3 | 4 | F4 | pro   | 45000 | **false (N64-style)** | experimental |
| 4 | 5 | F5 | pro   | 35000 | **false (N64-style)** | experimental — city track |
| 5 | 6 | F6 | pro   | 35000 | **false (N64-style)** | experimental |

So `no_lod_circuit[3..5]` is `false` by default — the PVS synth is off for the pro
tracks, restoring the authored 10-slot PVS rows. The radius cull still runs for them
(gated by the global `no_lod`), so distance culling works; the user just doesn't
get the modern cross-track pop-in fix on those tracks. **Opt-in:** flip the
relevant entry to `true` in `graphics.json` to test the modern look on a pro
track. The basic tracks (1-3) ship with the modern look on.

`draw_distance_circuit[4]` (the **5th element** of the array) is the second pro
track = city track. The currently-applied multiplier is `0.6667` × global `1.5` =
`1.0×` the N64 authored radius — the city track runs at native N64 distances even
with the radius cull on. (Math note: `0.6667 = 1.0 / 1.5` — the precise inversion
of the global multiplier so the city exactly hits N64.)

## Hotkeys / env var

- **F1–F6** (dev-warp): jump straight to that circuit as a 1-player single race, 3 laps,
  car 0. Active in any warpable state (3–8 except 7) — see `src/lambo_warp.c:68-72`.
- **`LAMBO_WARP=N`** (env, 1-based): same launch-once warp. Use `LAMBO_WARP=5` to land
  on the city track from a cold boot.

## How this map is used

The `draw_distance_circuit` and `no_lod_circuit` arrays in `graphics.json` are keyed
by the **0-based** index above. The file is read at startup by
`lambo::config::load_and_apply_graphics()`; the per-circuit value multiplies the
global `draw_distance` (for the radius) or gates the PVS synth (for `no_lod`).

If a future edit needs to tune a different circuit, change the element at the index
above. **Common mistake:** counting the array 1-based in the JSON — the 5th element
of the array is index 4, not index 5. The `draw_distance(c)` and `no_lod_circuit(c)`
calls in `lambo_no_lod.cpp` are passed the 0-based index the scene builder already
uses internally, so the table here is the single source of truth.
