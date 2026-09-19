# Sky panorama motion

Status: angular yaw projection and display-list behavior confirmed by focused
host tests. Windows RT64 validation is recorded below. Vertical artwork still
uses the game's authored pitch/roll convention; this is not a spherical sky.

The USA sky emitter (`func_800102D8`, runtime entry `0x8000F6D8`) draws a
scrolling panorama: eight tiles in each of two rows, 128 units per tile, at
Z=-220. Heading selects textures and a translation modulo 128. Circuit-specific
masks select a horizontally mirrored vertex bank for each tile.

## Angular projection

The earlier renderer correction used the world's horizontal aspect/FOV policy,
but its motion test compared two linear translations through the same matrix.
A tile edge 128 units from the centre at depth 220 subtended only
`atan(128/220) = 30.2 degrees`, rather than one eighth of a turn (45 degrees).

The port now maps panorama coordinates to bearings. For local panorama
coordinate `u` and guest modulo-128 phase `p`, it emits
`x = 220 * tan((u-p) * pi/512) + p`, at `z = -220`. The guest translation
subtracts `p`; the world's horizontal projection then gives the correct ray
bearing. Each tile spans 45 degrees, and a 180-degree heading change traverses
four tiles. This is a horizontal cylindrical mapping with the existing vertical
artwork convention, not a scroll-speed multiplier.

USA instructions 0x8000F6F4..F76C derive yaw from per-viewport camera direction
components at 0x800A2EC0/0x800A2EE8, then multiply degrees by the ROM double
2.8444 at 0x8008D910. That approximates 1024/360 units per degree with less
than 0.006 degrees of full-turn error. The modulo phase and tile index remain
guest-owned.

Sixteen strips per tile approximate the nonlinear mapping using affine UVs.
Shared boundaries use identical rounded vertices and UVs. The visible interval
is clipped in panorama space before evaluating tan, avoiding its singularities.
The first and last strips still reach the old coverage envelope. A wide window
therefore shows more compass directions, rather than more repetitions of a
flat strip. Current and previous projections keep the same renderer policy.

Patch 0011 restores vertical coverage independently of world FOV. Top and bottom
caps continue each tile's edge UVs to Y=+/-32512 without repeating its interior.
The coverage radius includes the viewport diagonal and absolute guest vertical
offset, covering banked views within the tested bounds. Coverage is independent
of the angular mapping. At least an 8:1 aspect is covered for paused resizes;
larger live aspects extend the envelope up to signed-16-bit vertex limits.

## Guest bridge and lifetime

One hook records the list cursor at `0x8000F9AC`, after matrices and material
setup. Another runs at `0x8001025C`, after the six original quads and before
texture perspective is restored. After validating and building the replacement,
it rewinds to the recorded cursor and replaces those six draws with its sublist.
Original vertices/textures remain unchanged. Invalid layout, phase, texture
ranges or allocation failure keep all stock draws; their widescreen coverage
is not guaranteed. The diagnostic renderer also retains stock draws.

All guest fields use `MEM_W`/`MEM_H` for word-swapped memory. These bridges run
on the guest game thread during per-viewport sky emission. Addresses were
checked against regenerated USA instructions; source vertices were also
inspected in a previous local RDRAM snapshot.

| Address | Meaning |
| --- | --- |
| 0x800A2BFC | u32 current graphics-task buffer pointer |
| 0x800A39CC | u32 display-list cursor |
| 0x800A2F90 | float vertical translation, consumed by guTranslate |
| 0x80098E58 | float horizontal phase, panorama units in [0,128), negated by guTranslate |
| 0x800CE6A4 / 0x800CE6A6 | neighboring s16 player count / viewport index |
| 0x800CE794 | s16 circuit index, 0 through 5 |
| 0x800987E0 | s16 centre tile index, 0 through 7 |
| 0x80098238 | u32 track-data pointer; two rows of eight texture pointers at +0x254 |
| 0x80089054 | u16 mirror masks at circuit * 4 + row * 2 |
| 0x8011F450 / 0x8011F5D0 | normal/mirrored vertex banks, two rows of twelve 16-byte vertices |

Heap storage is keyed by task pointer and viewport. Each buffer reserves 320
quads (64 vertex bytes and nine commands each), plus a terminator. Less than
180 degrees is visible, intersecting at most five tiles. Three replacement
commands occupy the game's roughly 31 KiB task arena.

The call uses F3D `G_DL` (0x06), bracketed by RT64 extended-address enable/disable.
This preserves the pointer's bit-31 extended-address marker. The rejected
`gEXDisplayList` experiment stored only 28 address bits; RT64's hook decoder
then resolved that pointer as segmented memory. That command cannot carry an
absolute extended-heap pointer with the current resolver.

The frame builder at 0x80000FDC..0x80001028 selects task pointers 0x800BF240 and
0x800C6C90 (slot + 0x68, stride 0x7A50). Lists start at task + 0x1C0 and end
before scheduler fields at task + 0x79C0. The replacement validates task identity,
cursor bounds/alignment and its recorded start before replacing any command.

Storage lives for the game's RDRAM/heap lifetime. The runtime graphics action
in `ultramodern/src/events.cpp` calls `send_dl` before arming DP completion.
The scheduler waits for completion before reusing graphics tasks, and
`RT64Context::send_dl` interprets guest lists before returning. The native
sublist therefore has the same consumption lifetime as original task vertices.
A future source-level sky emitter should own these lists instead of relying
on fixed addresses and cursor-replacement hooks.

### Race-start asset lifetime

The actual menu-to-race transition exposed a separate lifetime bug. A final
menu task still referenced `0x801F8B40` when the state-7 loader overwrote that
asset with race textures. A hardware watchpoint caught the write in
`osPiStartDma_recomp`, called by `boot_pad_apply_calibration`. RT64 subsequently
interpreted `0xFFFEFFFE` texture pixels as color-image commands. The resulting
invalid framebuffer readback zeroed the extended sky heap, so the eventual
display-list crash misleadingly appeared to originate in the sky sublist.

Delaying SP completion until after parsing did not fix this: the main game loop
can dispatch while one previous graphics task remains outstanding, including a
task still queued in the guest scheduler. That experiment was reverted.
The dispatcher-entry hook at `0x80001CD0` now returns before its stack prologue
when state (`s16` at `0x800CE6AC`) is 7 and the `s16` outstanding-task counter at `0x80098270` is
positive. The main loop continues receiving completion messages and retries
loading on a later dispatch. The check runs after the existing warp hook.

Counter evidence: submission increments at `0x8000574C`; the main loop consumes
task-done messages and decrements at `0x800014B4`. The current unsubmitted frame
is not counted. Waiting natively on this counter would deadlock its owner;
deferring dispatch preserves message processing and drains both guest-queued
and renderer-active tasks before menu assets are reused.

## Verification

Current work is based on 0830dd9, incorporating the panorama candidate from
bcfc437 and correcting its task guard and banked-camera coverage.
Windows, MinGW GCC 15.2.0, Direct3D 12 (api=1).
USA ROM SHA-256:
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.

Focused host tests:

- `lambo_sky_projection`: fixed compass landmarks track yaw, including half-turns
  and heading wraps, using the ROM's 2.8444 conversion. Projection matches the world across source
  FOVs 20/32/40/52 degrees, adjustments -19/0/20/60, output aspects 4:3
  through 8:1, and both full-height and half-height layouts. All four corners
  are tested through a full Z rotation in five-degree steps, vertical offsets
  -512/0/512, and heading phases including tile wrap boundaries.
- `lambo_sky_panorama`: actual native display-list emission into synthetic
  guest memory, six circuit mask offsets, eight tile indices, both task buffers,
  all four viewport indices, sub-tile phases, angular geometry, UV mirroring, edge caps, list termination,
  storage isolation and cursor rejection. The real task-address regression
  fails against the earlier guard and passes after correction.
- The 25-test Python host suite and documentation checker pass.

Reproduction after the supported build:

~~~text
ctest --test-dir build -R "lambo_(sky|camera_projection)" --output-on-failure
python tools/run_game_scenario.py scenarios/sky-turning-smoke.json
python tools/run_game_scenario.py scenarios/menu-to-race-smoke.json
~~~

### Angular implementation validation (2026-09-19)

Regenerated the USA sources with N64Recomp and rebuilt the normal
`build/lamborghini_modern.exe` CMake target. The three focused CTests, 25 Python
tests, documentation checker and `git diff --check` pass. The emitted-vertex
angular assertion failed on the old flat-strip implementation before the fix.

The standard RT64 sky replay completed 600 frames and 601 swaps. An isolated
fullscreen run with +40 FOV, distance 0.4, height 0.6, Full resolution,
Expand aspect, MSAA4X and no-LOD also completed 600 frames / 601 swaps.
A 1600x450 window at +60 FOV completed the same replay. Inspected race captures
show continuous sky coverage. Artifacts are in ignored `artifacts/sky-angular*`;
the local capture driver is `scratch/verify-angular.py` (turn/wide/menu modes).
These sampled captures do not establish every camera pose or eliminate an
intermittent crash. The earlier all-circuit/split-screen matrix below predates
the angular change. Android remains unverified.

After the race-start guard, two actual menu-to-race runs with the isolated
aggressive fullscreen settings above completed 3000 VIs, 1473/1475 swaps and
final state 8. Both traversed the race menus to screen -7; inspected captures
show the player race, not the attract demo. The second run's effective input
is preserved in `recordings/menu-to-race-smoke.jsonl` and replayed by
`scenarios/menu-to-race-smoke.json`. The original pulse-driven command
`python scratch/verify-angular.py menu` crashed before the guard and passed
after it; debugger traces are retained in ignored `artifacts/sky-angular-menu-*`.
The checked-in menu replay also passes on the final normal executable:
2996 VIs, 1474 swaps, 1272/1272 verified input frames and max state 8.
The final warp-based RT64 turning replay and headless harness smoke each pass
600/600 input frames and 601 swaps. After building all project test targets,
`ctest --test-dir build -R '^lambo_' --output-on-failure` passes 32/32 tests.
The initial unfiltered CTest run encountered unbuilt executables; vendored zstd
tests remain unbuilt and are excluded from this project-suite result.

### Historical flat-strip validation

For the earlier investigation an isolated diagnostic executable was linked from the
changed source files, freshly regenerated `funcs_0.c`, the patched RT64
projection/framebuffer sources and existing dependency libraries/unchanged
objects. This was not a clean release build. Local build and capture scripts
are retained under ignored `scratch/`; images and logs are under
`artifacts/sky-final-*`.

Runtime capture matrix: all six circuits at +40 FOV, two/three/four-player
layouts at +60, and a 32:9 window at +60. The steering trace alternates left
and right turns. All ten runs exited normally with 600 replay frames and 601
swaps. Inspected captures showed continuous sky without exposed edges or
missing tiles. A separate 692-frame trace paused the game, resized the window
from 1280x720 to 1600x450, then resumed; captures confirmed the pause menu and
unchanged lap time before/after resize, with complete sky coverage. That run
completed with 693 swaps. These are sampled visual checks, not an exhaustive
pixel-level oracle; the host tests establish the projection/coverage invariants.

The mathematical guarantee is bounded by signed 16-bit vertices, the tested
FOV range and vertical offsets. Runtime samples do not cover every road
position or every possible camera setting. Android and a clean release rebuild
remain unverified. Invalid guest layout/texture pointers or allocation failure
cannot promise complete coverage and are rejected rather than dereferenced.
