# Camera sequence gating

Status: **Confirmed** for the USA ROM boot, attract, race-entry and return paths
measured on Windows on 2026-09-29. Other ROMs are unsupported. Ares and physical
N64 comparison were not run.

## Guest boundary

The camera hooks and companion consumers read unitless signed 16-bit codes at
`0x800CE6AC` (dispatcher state), `0x800CE6B0` (race phase), and `0x800CE6B4`
(game mode). `MEM_H` accesses the runtime's word-swapped RDRAM on the guest
thread. Settings apply only for state 8, phase 3, and mode other than 4.
Missing RDRAM preserves authored values. Each call reads current state, avoiding
cached flags across transitions or save-state loads.

The [existing dispatcher evidence](gyro-steering-research.md#local-guest-gate-evidence)
establishes the addresses. The measurements below establish the previously
unverified intro/title cases. Countdown/results use authored values by explicit
maintainer decision during review of PR #258. Pause retains configured framing:
this is a visual policy, unlike the input-assist gate that must stop writing input.

## Measured sequence values

A temporary observer at `func_800028D0`, before instruction `0x80001CD8`, sampled
the three halfwords after the warp/save-state hooks and before dispatch. No
observer wrote guest state. The checked-in `LAMBO_CAM_TRACE=1` sequence probe now
logs the same boundary without the temporary screenshot synchronization.

| Observed sequence | State | Phase | Mode | Overrides |
| --- | --- | --- | --- | --- |
| Boot licence/intro card | 4 | -1 | 0 | Off |
| Animated title flag | 6 | -1 | 0 | Off |
| Attract setup/countdown | 8 | 2 | 4 | Off |
| Attract driving | 8 | 3 | 4 | Off |
| Title after attract | 6 | -1 | 4 | Off |
| Time-trial pre-start camera/countdown | 8 | 2 | 0 | Off |
| Time-trial driving | 8 | 3 | 0 | On |
| Quitting a time trial | 8 | 1 | 0 | Off |
| Title after quitting a time trial | 6 | -1 | 0 | Off |
| Arcade pre-start camera after menu re-entry | 8 | 2 | 1 | Off |
| Arcade driving after menu re-entry | 8 | 3 | 1 | On |

Boot observation included the licence card, animated title and two attract
passes. Race entry used the existing developer warp through the ROM loader.
A separate windowed run used START, DOWN, DOWN, A to quit a configured race;
it then visited title/attract and navigated the normal menus back into Arcade.
The process and settings were retained throughout. These observations cover
both the boot presentation and the pre-start racing camera, not an assumed
single meaning of "intro".

## FOV companions without a new projection

The initial regression test always called an FOV hook during transitions and
missed a real gap. A new test widens a race projection, changes to a scripted
sequence, then reads companions without calling another FOV hook. Before the
consumer gate, it failed with:

```text
FAIL: state exit must neutralize backdrop without another FOV hook
```

All three consumers now pass RDRAM to the shared gate. Outside active racing,
the backdrop returns float bits `0x3F800000`, the view cone returns the exact
authored double bits `0x3FEC5A1CAC083127`, and the sky returns the most recently
supplied **authored** FOV. Keeping that authored value removes only the configured
contribution; it does not guess a new projection for scenes without FOV hooks.
The renderer receives the correction in its display-list tag, so its worker
thread does not read guest state or live configuration.

In the RT64 run, active modified racing reported sky FOV 60, backdrop bits
`0x3FCB0A77`, and cone bits `0x3FE89F1653F35E3D`. A later title sample in the same
process, after quitting, reported 40, `0x3F800000`, and `0x3FEC5A1CAC083127`.

## Verification record

- ROM: USA, SHA-256
  `cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c`.
- Source baseline: `0f6085a68d356368279b6a6ef6e68ab119d73e5e`; PR #258 camera
  changes plus the companion fix in this revision.
- Windows, MinGW GCC/G++ 15.2.0; RT64 D3D12, 1600x900 client captures.
- Settings compared: distance/height/FOV `(1, 1, 0)` versus `(2, 0.5, 20)`.
- Fresh N64Recomp output was compiled and linked into isolated executables.
  Unchanged host/dependency objects came from the matching baseline checkout;
  this was not a clean dependency build or a run of the complete build script.
- Graphics config, Controller Pak, logs and LOCALAPPDATA were isolated per run.
- Temporary capture observer paused the guest dispatcher at selected ticks,
  allowed RT64 to drain, then used DPI-aware PrintWindow capture. These pauses
  leave VI timing running, so animated overlays need not match at every tick.
  The observer was excluded from the final regenerated production binary.

| Comparison | Result |
| --- | --- |
| Headless boot/title/two attract passes, 8000 VI budget | All 79 paired sampled BMPs byte-identical. |
| RT64 licence/intro card, ticks 80/120/200 | Pixel-identical default versus modified. |
| RT64 animated title, tick 350 | Pixel-identical. |
| RT64 attract, ticks 600/900 | Pixel-identical. |
| RT64 pre-start camera, ticks 40/80 | Pixel-identical; state/phase/mode 8/2/0. |
| RT64 driving, ticks 200/350 | Different framing as intended; 8/3/0, adjusted sky FOV 60. |
| RT64 time-trial -> title/attract -> Arcade | Observed in one process with unchanged settings. |

Not every RT64 capture is pixel-identical: the early boot tick 40 differs by at
most 7 channel levels, and later title/countdown captures include VI-timed
animation differences. The matching samples above and the headless comparison
are the framing evidence; unmatched animation samples are not claimed as passes.
Results phase 4/5 behavior is host-tested, not a completed-race visual check.

## Repeating the checks

Build normally with `build.ps1` or `build.sh`, which regenerates game code. Then:

```text
ctest --test-dir build -R "lambo_camera|lambo_sky_panorama" --output-on-failure
```

For sequence logs, launch with `LAMBO_CAM_TRACE=1` and `--console --verbose`.
Compare normal boot using the two camera-setting triples above. For bounded
headless captures also set `LAMBO_HEADLESS=1`, `LAMBO_MODERN_MAX_VIS=8000`,
`LAMBO_DL_RENDER_STATE=0`, `LAMBO_DL_RENDER_EVERY=50`, and a separate
`LAMBO_DL_RENDER_OUT` path per run. Use separate graphics, Pak and application
state directories; simultaneous runs must not share the runtime's imported ROM
file. Race entry can use `LAMBO_WARP=1:1:0:1` and `LAMBO_WARP_MODE=0`.

Local review artifacts are grouped under the ignored `build-camera` directory:
`final-intro-*` / `final-race-*` contain headless captures and logs;
`full-intro-*` / `full-race-*` contain full-size RT64 captures and sampled tuple
text; `interactive-race-modified` records the menu-driven exit/re-entry;
`comparison.json` records image hashes and pixel comparisons. These are local
verification artifacts, not distributed game assets.
