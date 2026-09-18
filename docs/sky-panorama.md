# Sky panorama motion

Status: projection and display-list behavior confirmed by focused host tests;
Windows RT64 one-player and four-player steering smoke runs passed.

The USA sky emitter (`func_800102D8`, runtime entry `0x8000F6D8`) draws a
scrolling panorama, not a surrounding cube. It selects three adjacent tiles
from an eight-tile ring, in each of two rows. Each column is 128 units wide
at Z=-220. Heading determines both the texture index and a translation modulo
128 units. The alternate vertex bank mirrors selected textures horizontally.

The old renderer extension kept the original horizontal projection and
stretched it across the output viewport. It also restored the authored FOV
on both axes. This magnified the panorama's translation: 1.333 times at 16:9
with stock FOV, or 2.115 times at 16:9 when vertical FOV changes from 40 to
60 degrees. These are projection calculations, not measured image velocities.

Patch 0011 now uses the world's horizontal aspect/FOV policy for explicitly
tagged sky draws. It restores only vertical coverage. It also removes 0008's
zero-translation heuristic. The port supplies adjacent panorama columns at
the original spacing, retaining tile wrapping and mirroring. Current and
previous projections receive the same policy during interpolation. The
original heading calculation, modulo phase and sky matrices are unchanged.

## Guest bridge and lifetime

The hook runs at `0x8001025C`, after the original six quads and before texture
perspective is restored. It does not modify the original vertices or textures.
All guest fields use `MEM_W`/`MEM_H` to preserve the runtime's word-swapped
memory representation. The values below were checked against regenerated USA
instructions; vertex coordinates were also inspected in a local RDRAM snapshot.

| Address | Meaning |
| --- | --- |
| 0x800A2BFC | u32 current graphics-task buffer pointer |
| 0x800A39CC | u32 display-list cursor |
| 0x800CE6A4 / 0x800CE6A6 | Neighboring s16 player count / viewport index |
| 0x800CE794 | s16 circuit index, 0 through 5 |
| 0x800987E0 | s16 center panorama tile, 0 through 7 |
| 0x80098238 | u32 track-data pointer; texture pointers at +0x254, two rows of eight u32s |
| 0x80089054 | u16 mirror masks indexed by circuit * 4 + row * 2 |
| 0x8011F450 / 0x8011F5D0 | Normal/mirrored vertex banks, two rows of twelve 16-byte N64 vertices |

The extension allocates a sublist in the recomp heap, keyed by the original
task-buffer pointer and viewport. Only three extra commands occupy the game's
roughly 31 KiB task arena. Each heap buffer holds at most 1,012 quads, including
vertices, nine commands per quad and a terminator. X coordinates stay within
signed 16-bit range. Extended RDRAM addressing is enabled only around its call.

Storage lives for the single RDRAM/heap lifetime of the running game. It is
reused when the corresponding original task buffer is reused. The runtime's
graphics action in `ultramodern/src/events.cpp` calls `send_dl` before arming DP
completion; the game scheduler waits for that completion before recycling
graphics tasks. `RT64Context::send_dl` consumes guest lists through RT64's
interpreter before returning. Thus these copies have the same guest-memory
lifetime as the original task's vertices, not the lifetime of GPU presentation.
A future source-level sky emitter should own these lists directly instead of
depending on fixed addresses and a post-emission hook.

Invalid layout fields or allocation failure leave the original three columns
intact; edge coverage is then not guaranteed. The diagnostic renderer retains
the original sky because it does not support these extended-address lists.
Columns cover at least an 8:1 output aspect so ordinary window resizes while
paused remain covered. Wider live aspects extend further, up to signed-vertex
limits. Resizing beyond that overscan while paused requires another guest frame.

## Verification

Baseline: f24bce54bc4b20f4546dd394dba4dae9ac66d04f, Windows, MinGW GCC.
USA ROM SHA-256:
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
Generated sources were freshly regenerated with the existing N64Recomp tool
and the modified `lamborghini.us.toml`; generated files are not checked in.

- `lambo_sky_projection`: motion against world projection, vertical coverage,
  4:3 through 8:1, single/split-screen FOVs, every texture-index wrap. The
  previous stretch policy fails this test; the corrected policy passes.
- `lambo_sky_panorama`: actual native display-list emission into synthetic guest
  RAM; adjacent textures, mirror masks, vertex spacing, triangle indices,
  termination, neighboring s16 fields, distinct task/viewport allocations.
- All five changed C++ translation units passed compilation with the existing
  dependency headers. Existing third-party compiler warnings remain.
- The 25-test Python host suite and documentation checker passed.

An isolated diagnostic executable was linked with MinGW GCC 15.2.0 from the
five changed C++ objects, freshly generated `funcs_0.c`, and the existing
same-commit dependency libraries and unchanged objects. Frontend assets and
DLLs were copied beside that executable. This was not a full release rebuild.
The first isolated RT64 launch failed in frontend styling because those assets
were missing; supplying the normal build assets resolved it.

The following commands passed with that executable:

```text
python tools/run_game_scenario.py scenarios/harness-smoke.json --exe scratch/lamborghini_modern.exe --artifacts-dir artifacts/sky-headless --timeout 60
python tools/run_game_scenario.py scenarios/frontend-rt64-smoke.json --exe scratch/lamborghini_modern.exe --artifacts-dir artifacts/sky-rt64 --timeout 60
python tools/run_game_scenario.py scenarios/sky-turning-smoke.json --exe scratch/lamborghini_modern.exe --artifacts-dir artifacts/sky-turn --timeout 60
```

Each completed 600 replay frames and 601 framebuffer swaps. RT64 used Windows
Direct3D 12 (`api=1`), a 1600x900 window and 120 Hz presentation. A local copy of
the steering scenario with `warp: "1:1:0:4"` and `warp_mode: 2` also passed;
the native result confirmed four players. Its artifacts are under
`artifacts/sky-four-player`. Runtime smoke tests assert progress and completion,
not image velocities; the host projection test supplies the motion oracle.

These tests do not establish visual quality during pitch/roll or runtime GPU
performance. A full release rebuild and interactive turning checks across
circuits and split-screen layouts remain separate validation.
