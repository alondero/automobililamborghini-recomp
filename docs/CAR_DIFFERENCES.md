# Car differences: verified identities and measurement

The earlier six-selector campaign measured **difficulty overrides, not six
cars**. Its car rankings, cornering groups and suggested top-speed caps are
withdrawn. Do not use the tables in commit `f831078` for a car-selection UI.

The USA ROM has **24 model descriptors mapping to eight physics categories**.
Each category has three consecutive model variants. Car identity, difficulty,
player channel and live vehicle slot are separate dimensions.

## Reproduce the evidence

From the repository root, with Python 3.11 or newer:

```powershell
python tools/emu_instrumentation/audit_car_data.py "Automobili Lamborghini (USA).z64" --output scratch/car-data.json
python tools/emu_instrumentation/probe_car_models.py --out scratch/car-launch --vis 1000 --reps 2
```

Create `scratch` first for the audit output. The audit checks the ROM hash,
menu callback instructions, menu text, model range and descriptor mapping,
then extracts the small parameter tables. The probe uses the corrected
[developer warp](../src/lambo_warp.c), holds difficulty fixed, and rejects
traces without a moving player of the expected physics category. It records
raw-trace hashes alongside measurements. Reusing an output directory rejects
mismatched ROM, executable, source revision, or scenario configuration
metadata, and scenario-prefixed trace/log names keep launch and steering runs
separate. Outputs are local artifacts.

The retained [measurement evidence](evidence/car-models-usa.json) was collected
on 2026-09-05 from clean revision `984a91a`. It records the executable hash,
environment configuration, automatic-gearbox check, and individual trace
hashes. The probe emits the same top-level scenario/configuration/runs shape;
failed attempts are kept separately and make the command fail.

The runtime gate for the trace and warp hooks was `cmake --build build
--target lamborghini_modern -j 4`, followed by a clean boot-smoke probe for
models 3 and 9 at `--vis 1000 --reps 1`; both reached the controlled cap with no
native-crash banner. The full retained campaign used the same executable and
recorded its hashes above.

Source artifact: `Automobili Lamborghini (USA).z64`, USA, big-endian;
SHA-256 `cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c`.
Code addresses below are **runtime addresses**, not the address embedded in
an original/splat function name. Code ROM offset = runtime - `0x80000000`
+ `0xC00`. Asset banks have separate load mappings; do not apply that formula
to every data address.

## The actual car-selection path

| Dimension | Address / shape | Proven use |
| --- | --- | --- |
| Player model cursor | `0x800CE7E8 + player*2`, zero-based player | Selects one of 24 descriptors. |
| Model descriptor | `0x8013D490 + model*24`, ROM `0xAEE80 + model*24` | First halfword is the physics category; pointers select model assets. |
| Vehicle category | Vehicle record `+0x12` | Indexes eight-row engine and physics tables. |
| Difficulty cursor | `0x800CE7A4` | Menu text is DIFFICULTY, NOVICE / EXPERT. |
| Race difficulty | `0x800CE79C` | Copied from difficulty cursor by race finalization. Valid menu values are 0/1. |
| Live vehicle slot | `0x80098398` | Index into records at `0x800B69A8`, stride `0x10C`; not a model ID. |

The menu's right-arrow handler at `0x8003E96C` increments the selected
player's model cursor, wraps at 24 (`slti ..., 0x18` at `0x8003E9A8`),
reads the descriptor's category and skips unavailable categories. The left
handler at `0x8003E80C` wraps to 23. Race setup reads the cursor at
`0x8000722C`, scales it by 24, and passes the descriptor's first halfword to
the body constructor. The constructor stores it at vehicle `+0x12`
(`0x800116AC`). This is also the path the corrected warp now exercises.

The difficulty menu record at ROM `0x14A180` points to `0x800CE7A4` and
callback `0x8003DA98`. That callback executes `xori ..., 1` at `0x8003DAB8`.
The following display record selects text IDs 20/21, NOVICE/EXPERT; its
label is text ID 19, DIFFICULTY. This is stronger evidence than inferring a
car count from neighboring numeric constants.

| Model cursors | Physics category | Availability in the decoded menu gate |
| --- | ---: | --- |
| 0-2 | 0 | Unconditional |
| 3-5 | 4 | Unconditional |
| 6-8 | 1 | Progress bit `0x04` |
| 9-11 | 2 | Progress bit `0x08` |
| 12-14 | 3 | Progress bit `0x02` |
| 15-17 | 5 | Progress bit `0x01` |
| 18-20 | 6 | Progress bit `0x20` |
| 21-23 | 7 | Progress bit `0x10` |

The gate at `0x8003C7A4-0x8003C908` clears eight availability halfwords at
`0x800985C0`, enables categories 0 and 4, then enables the others from
`0x800A4170`. This establishes the bit-to-category mapping, not which
championship or victory code awards each bit. The development warp bypasses
this availability gate. Retail names and the meaning of the three visual
variants remain unverified; identify UI entries by descriptor/category until
names have evidence.

## Real engine and transmission differences

`0x8013E0F0` (ROM `0xAFAE0`) contains eight 52-byte records indexed by
vehicle category. It is separate from the two-column difficulty tables.

| Category | Idle engine value | Shift reference | Upper engine reference | Gears | Ratio multiplier | Gear ratios, 1 through highest |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1100 | 6800 | 7000 | 5 | 2.31 | 2.31, 1.52, 1.12, .88, .68 |
| 1 | 1080 | 6500 | 6700 | 6 | 2.42 | 2.85, 2.08, 1.41, 1.03, .81, .62 |
| 2 | 1300 | 8500 | 8700 | 6 | 1.93 | 3.27, 2.42, 1.88, 1.50, 1.26, 1.01 |
| 3 | 1200 | 7400 | 7600 | 6 | 1.87 | 3.16, 2.30, 1.77, 1.40, 1.12, .89 |
| 4 | 1150 | 7000 | 7200 | 5 | 2.40 | 2.432, 1.695, 1.207, .907, .707 |
| 5 | 1250 | 8000 | 8200 | 6 | 1.92 | 3.057, 2.321, 1.834, 1.424, 1.148, .949 |
| 6 | 980 | 5800 | 6000 | 5 | 2.08 | 2.47, 1.53, 1.12, .83, .67 |
| 7 | 1060 | 6300 | 6500 | 5 | 1.70 | 3.139, 2.014, 1.526, 1.161, .875 |

The halfwords at row offsets `+0/+2/+4` feed engine-speed clamping and
shifting; `+8` limits the selected gear (`0x8001A228-0x8001A250`). The float
at `+0x0C` and gear-indexed float at `+0x10 + gear*4` are multiplied in the
live physics path (`0x800225AC-0x800225DC`) and engine updater
(`0x80025D10-0x80025D78`). Gear zero's ratio is zero. The gear state lives
at vehicle `+0xA8` (`LamboVehicleRecord.gear_state`); the engine-speed state is
at `+0x9E`. The adjacent `+0xB0` halfword is not the gear state. The row's
`+8` value is the inclusive maximum gear state; since neutral is zero, it is
also the number of forward ratios shown in the table.

Each row also has its own engine-output curve pointer at `+0x30`. The engine
updater indexes signed halfword samples by engine speed shifted right by 6
(`0x80025ED8-0x80025EF4`), multiplies by applied throttle and a shared scale,
then stores a driving contribution at vehicle `+0x94` (`0x80026400`). These
curves and gear ratios are concrete per-car inputs to acceleration. Their
raw values are not yet calibrated as horsepower or torque in physical units.
The row's `+0x2C` coefficient participates in fuel depletion when enabled
(`0x80026470-0x800264CC`); it is not a grip rating.

## Difficulty-dependent steering and physics

Novice uses steering slew 4; Expert uses 7. Novice uses response data at
`0x800891F0`; Expert uses `0x80089174`. The latter starts **four bytes after**
the slew base `0x80089170`: the subsequent halfwords are already response
lookup data. Eight readable halfwords `[4,7,1,1,1,1,1,2]` do not prove an
eight-car slew table.

Likewise the eight readable floats beginning at ROM `0x89F0C` are
`0.75, 0.45, 1e-5, 1e-6, 8e-5, 8e-6, 1e-5, 1e-6`. The reviewer correctly
identified the original copy/paste error at indices 4/5. The relevant lookup
uses **difficulty**, so only `0.75/0.45` are legal values at this seam.
At `0x8001FC58-0x8001FCA0` it imposes a lower bound on a working physics
quantity using the category/difficulty parameter. It is not a top-speed
array or a per-car grip table.

Tables `0x8013E290` / `0x8013E2D0` use `category*8 + difficulty*4`.
Their eight rows are identical: `.54/.27` and `.90/.45` respectively.
They cannot explain differences between categories at fixed difficulty.
Difficulty 2 aliases the next row's Novice entry, and 3 aliases its Expert
entry. That explains the previous campaign's misleading parity correlation.
The `0.91` scratch scaling at `0x8001D658-0x8001D868` is conditional;
it must not be asserted for every frame or difficulty.

`func_8001E01C` is listed in [force_stub.txt](../force_stub.txt) and has the
symbol entry at `lamborghini.syms.toml:79`; the force-stub configuration leaves
its standalone generated body empty. However, the enlarged `func_80019D20`
starts at runtime `0x80019120`, has size `0x4EFC`, and includes the entire
`0x8001D41C-0x8001E01C` tail. The scratch stores at `0x8001D6D4` etc. are
present inside that live generated body. The review's inference that these
calculations are all dead because the tail symbol is stubbed is incorrect.
The static audit tool checks this symbol-range containment.

## Shared record schema and controls

[lambo_vehicle.h](../src/lambo_vehicle.h) defines the verified fields with
compile-time size/offset assertions. Trace, analog throttle and analog brake
use this schema. It is an offset schema, **not a struct to cast onto RDRAM**:
librecomp memory is word-swizzled, so guest reads/writes still use MEM_H/MEM_W.
Position is at `+0x14`, not the review's overlapping `+0x08` vector.

The stock throttle ramp is +/-10 per update. Brake demand ramps to 16 and
contributes demand*10 while its latch is active. Those shared control laws
do not prove equal total stopping distances across cars or surfaces. See
[throttle](analog-throttle.md) and [brake](analog-brake.md).

## Measurements with the actual model cursor (2026-09-05)

Headless port, Circuit 1, time trial (no AI opponents), Novice difficulty,
boot-default automatic transmission, held A with zero stick. Two runs per
category, 1000 VIs each; times below count dispatcher updates from first
positive speed, not seconds. The probe captures a varying number of updates
at a VI-limited exit, but all threshold times below repeat exactly.

| Model cursor tested | Category | Updates to 50 | Updates to 100 |
| ---: | ---: | ---: | ---: |
| 0 | 0 | 57 | 107 |
| 3 | 4 | 34 | 78 |
| 6 | 1 | 37 | 89 |
| 9 | 2 | 49 | 96 |
| 12 | 3 | 41 | 83 |
| 15 | 5 | 38 | 81 |
| 18 | 6 | 34 | 81 |
| 21 | 7 | 44 | 97 |

These are controlled **Circuit 1 launch observations in HUD speed-field
units**, not real-world 0-100 km/h times. Through the 100 threshold, speed
never decreases and maximum lateral departure from the start-to-end line is
under 0.11 position units in all tested categories. This is evidence of a
straight launch window, not an instrumented assertion that no contact event
occurred. The tool retains 150-threshold/peak observations for investigation;
they are not published here as clean acceleration or terminal-speed ratings.

One additional full-left launch per category (`--scenario steering --vis 650
--reps 1`) measured maximum launch steering-command slew **4 in all eight
categories**. This confirms the shared Novice input law; it does not establish
equal turning radii or grip. No new yaw-rate rating is claimed.

The earlier `0xFFFFFFFF` reports were probe failures, not model-table faults.
They had no native-crash banner, and the same model 9 case completed without
the trace enabled. The old tracer performed one `fprintf` per guest word and
flushed every frame; that I/O load changed the game-thread timing and could
leave the process short of its controlled VI-cap exit. The tracer now builds
each record in a bounded line buffer, flushes periodically, reports I/O errors,
and closes its stream at teardown. Repeated model 3 and model 9 runs with the
new writer reached the controlled exit and reproduced their threshold values.
The probe records any future early exit with its last trace frame and whether a
native-crash banner or controlled-cap message was observed; a failed case makes
the campaign fail instead of contributing a measurement. The clean retained
campaign contains 16 launch runs and eight steering runs with no excluded
attempts.

## Readiness for car-selection stats

The cursor-to-category mapping and inclusive maximum gear states are ready to
use. Launch comparisons must hold difficulty, transmission, track and input
fixed.
Top speed still needs a collision-free equilibrium measurement; maximum
observed circuit speed is not a terminal-speed rating. Handling comparisons
must use the actual categories at matched speed, not the old illegal
difficulty overrides. Port measurements also need reference-emulator
comparison before being advertised as exact retail performance.

The former claims of a selector-4 ~149 cap, six-car steering groups, and
selector-to-name mappings are not retained. The old raw traces remain local
for investigating that invalid experiment, not as car specifications.
