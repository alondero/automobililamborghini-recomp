# Car differences and modeled vehicle parameters

Status: static audit of the USA release, 2026-08-29.

This is a code map, not a gameplay rating chart. It records what the game
actually distinguishes in its race code, what is common to every selector,
and what cannot yet be assigned to a named car or a familiar stat such as
horsepower, grip, or top speed.

## Executive summary

The race code contains six selector values (0 through 5) in selector-indexed
tables and carries the selected value from the car-selection cursor into the
race. The audit can prove these differences:

- Steering input response is selector-dependent. Selector 0 uses a different
  steering response table from selectors 1 through 5. A separate selector
  indexed slew/delta table also changes the maximum steering-command change.
- Throttle and brake pedal handling are common control laws. They are not
  evidence of six different engines or brake systems. The stock throttle
  ramp is +/-10 per update; brake demand is capped at 16 and the braking
  pass applies demand*10 while the brake latch is active.
- The physics pass performs additional selector/category-indexed lookups.
  These are real inputs to vehicle simulation, but the checked-in evidence
  does not isolate them into defensible per-car acceleration, terminal-speed,
  grip, mass, or braking coefficients.
- The renderer has per-car model/LOD data. That changes which visual model is
  drawn at a distance; it is presentation data, not a handling difference.
- The repository does not currently establish the retail names, selector-to-
  name mapping, initial availability, or unlock order. A `VICTORY CODE:`
  string exists in the ROM, but no checked evidence ties a code to a car ID.

Therefore, it would be misleading to publish a table saying, for example,
“Car 3 has 180 km/h top speed and 1.2x grip” based on the currently recovered
data. The reliable current result is a selector-level steering comparison plus
an inventory of the unresolved physics parameters.

## ID vocabulary

Several different notions of “car” appear in the code. They must not be
treated as interchangeable.

| Concept | Evidence | Meaning and limitation |
| --- | --- | --- |
| Selection cursor | `D_800CE7A4` (halfword) | The value parked by the menu/dev warp before a race. The current warp writes it without range validation, so arbitrary values are test inputs, not proof of retail availability. |
| Race selector | `D_800CE79C` (halfword) | The state-7 race-start path copies the selected cursor into this value. Steering and physics use it as a selector. Values 0-5 are referenced by selector-indexed code. |
| Runtime vehicle slot | `D_80098398`; record base `0x800B69A8 + slot*0x10C` | The active participant/body slot used by the race updater. A runtime snapshot shows three body records and three shadow records, so this is not a six-car model list. |
| Vehicle category/index | Record field at `+0x12`, read into `D_800986E8` | Used as the row index for additional parameter tables. Its exact gameplay name is not established; it should not be called mass, class, or engine type without more evidence. |

The addresses above refer to the original race overlay naming. The generated
recompiler symbols in `lamborghini.syms.toml` use the runtime address space,
which is shifted for this overlay; the two address forms should not be mixed
when looking up code.

## Confirmed selector-dependent steering behavior

The player-race updater is the function identified as `func_80019D20` in the
original/splat address space. It reads the selector while converting raw pad
steering into an internal steering command. The exact curve is represented by
packed lookup data, but the selector split is unambiguous:

| Selector | Steering response source | Slew/delta limit source | What can safely be said |
| ---: | --- | ---: | --- |
| 0 | `D_800891F0` | `D_80089170[0] = 4` | Has its own response branch and a limit of 4. |
| 1 | `D_80089174` | `D_80089170[1] = 7` | Uses the shared nonzero-selector response branch; limit 7. |
| 2 | `D_80089174` | `D_80089170[2] = 1` | Shared response branch; limit 1. |
| 3 | `D_80089174` | `D_80089170[3] = 1` | Shared response branch; limit 1. |
| 4 | `D_80089174` | `D_80089170[4] = 1` | Shared response branch; limit 1. |
| 5 | `D_80089174` | `D_80089170[5] = 1` | Shared response branch; limit 1. |

The limit is a command slew/delta limit: it constrains how quickly the
internal steering command catches up with the target. It is not, by itself, a
turning-radius or tire-grip value. A larger limit can make steering input
change more quickly, while the final cornering result also depends on speed,
orientation, velocity, and the rest of the physics state.

The same updater first trims and bounds the raw steering magnitude before
using the response data. The packed ROM region contains the response tables
and neighboring constants, so it is not safe to describe every adjacent
halfword as a clean six-car stat array. What is solid is the branch selection,
the shared-vs-unique curve relationship, and the first six slew limits above.

Historical runtime probes made during the decomp investigation are consistent
with this interpretation: selector 0 showed a command change of 4 per update,
while selector 3 showed a change of 1 per update in the same type of steering
path. Those observations are useful validation, but the static code evidence
is the authoritative source for the table above.

## Controls that are common to all selectors

These values describe input plumbing, not vehicle-specific performance:

### Throttle

The throttle demand is stored in the vehicle record at `+0xAA`. The stock
human-player path ramps it by 10 per update while the accelerator is held and
ramps it down by 10 on release. The analog-throttle hook preserves this stock
behavior after converting the stick position into a target demand. See
[analog-throttle.md](./analog-throttle.md).

### Brake

Brake demand is stored as a float at vehicle record `+0xA0`; the brake latch is
at `+0xAC`. The stock B-button path raises demand by 1 per update up to 16,
zeros it on release, and clears the latch. In the braking pass, the confirmed
deceleration contribution is:

```text
deceleration contribution = brake demand * 10
```

That calculation is enabled while the signed halfword latch is positive. No
separate per-selector brake multiplier has been isolated. See
[analog-brake.md](./analog-brake.md).

### Speedometer value

The signed 32-bit field at vehicle record `+0x90` is passed to the HUD
speedometer. It is a live speed-related value, not a static top-speed rating.
To turn it into a comparable top-speed measurement, each selector must be
tested under identical track, gearing, input, and runtime conditions.

## Selector-dependent physics lookups

The physics routines do contain more than steering and do use the selector.
The following is the strongest safe description from the disassembly:

| Data/use | Confirmed access pattern | Safe interpretation |
| --- | --- | --- |
| `D_8013E290` | Indexed using `(vehicle record +0x12) * 8 + selector * 4`; consumed in `func_8001E01C` and `func_8001EC1C`, with results copied into per-vehicle scratch state. | A category-and-selector-dependent internal parameter. The source audit does not identify it as acceleration, top speed, brake force, grip, or mass. |
| `D_8013E2D0` | Accessed with the same category/selector shape in the vehicle update and physics passes. | A second category-and-selector-dependent internal parameter. Its gameplay role is unresolved. |
| `D_8008930C[selector]` | Six selector-indexed floats are read directly by the physics pass. The first six static values are `0.75`, `0.45`, `0.00001`, `0.000001`, `0.00001`, `0.000001`. | A selector-dependent threshold/scale input to an internal calculation. The code does not give it a stable player-facing stat name. |

The category field and table accesses prove that selector-specific physics
inputs exist. They do not, by themselves, tell us which lookup controls which
observable behavior. Some table labels overlap packed constants in the ROM,
and a runtime dump shows repeated/pair-shaped values rather than a clean list
of six named car specifications. Assigning these values to horsepower or
traction would be speculation.

## What can and cannot be documented as a car stat today

| Requested difference | Current conclusion |
| --- | --- |
| Handling/steering response | **Confirmed difference.** Selector 0 has a unique response LUT; selectors 1-5 share another. Slew limits are 4, 7, 1, 1, 1, 1 for selectors 0-5. |
| Acceleration | **Not isolated.** Selector/category lookups participate in physics, but no clean acceleration coefficient or controlled per-selector result is currently documented. |
| Top speed | **Not isolated.** `+0x90` is a live speedometer input, not a cap. No clean six-value top-speed table was found. |
| Braking | **Common base behavior confirmed.** Demand is capped at 16 and contributes demand*10 while latched. A car-specific brake coefficient was not proven. |
| Grip/traction | **Not isolated.** Steering and velocity/orientation physics are present, with selector/category inputs, but no standalone tire/grip coefficient has been identified. |
| Weight/inertia | **Not proven.** No field in the checked evidence can safely be named vehicle mass or inertia. |
| Collision behavior | **Collision/proximity processing exists.** Per-model geometry and collision thresholds may differ visually/physically, but no car-specific handling coefficient has been separated from the pair-processing code. |
| Gears/transmission | Menu text includes transmission modes such as automatic/custom, but that is an independent setting and is not evidence of a per-car gearing specification. |

## Names, unlocks, and availability

The code audit can establish selector plumbing, not the retail garage list.
The current development warp accepts a numeric car value and writes it to the
selection cursor without validating its range. That is useful for testing
selectors 0-5, but it cannot establish which values are normally selectable.

The original ROM contains a `VICTORY CODE:` string and a selection/menu system,
but the checked static evidence contains no branch that maps a victory code or
progress state to a specific car selector. Car names are likely stored in
bitmap or compressed menu assets; only `DIABLO` was found as a raw model-name
string in the examined data. Consequently, this document intentionally does
not claim a name-to-selector mapping or an initial/unlocked order.

## Visual model differences are separate

The scene builder draws cars using per-car model data and selects among up to
eight model levels based on camera distance. The `no_lod` patch and its audit
show that this path changes model selection only. A lower-detail mesh at a
distance should not be reported as a performance or handling difference.
See [no_lod_audit.md](./no_lod_audit.md).

## How to complete the gameplay stat table

The next useful step is controlled runtime instrumentation for selectors 0-5:

1. Start the same track and starting conditions for every selector, with the
   same transmission mode and identical input timing.
2. Log the selector, category field, vehicle speed field `+0x90`, velocity
   components, steering command/accumulator, throttle demand `+0xAA`, brake
   demand `+0xA0`, and brake latch `+0xAC` every frame.
3. Measure 0-to-target acceleration, terminal speed on a long straight,
   stopping distance from the same speed, and repeatable lateral response.
4. Correlate those measurements with the derived scratch values produced by
   the `D_8013E290`/`D_8013E2D0` lookups. This can reveal which internal value
   controls each observable without guessing from neighboring ROM constants.
5. Decode the car-selection bitmap/assets and exercise the retail progression
   and victory-code paths to document names and unlock order.

Until those measurements are made, the steering table in this document is a
source-level difference, while acceleration/top-speed/braking/grip ratings
remain deliberately unassigned.

## Evidence and references

- [analog-throttle.md](./analog-throttle.md) - throttle field, stock ramp, and
  race-updater hook placement.
- [analog-brake.md](./analog-brake.md) - brake demand/latch fields and the
  confirmed `demand * 10` braking contribution.
- [no_lod_audit.md](./no_lod_audit.md) - per-car visual model/LOD behavior.
- [lamborghini.us.toml](../lamborghini.us.toml) - USA hooks and overlay
  address comments.
- [lamborghini.syms.toml](../lamborghini.syms.toml) - generated runtime
  symbols for the race functions.
- [lambo_warp.c](../src/lambo_warp.c) - development race-start cursor
  plumbing; not a retail unlock table.
- [README.md](../README.md) - repository scope and the fact that original
  ROM code/generated output are not checked in.
- [USA ROM](<../Automobili Lamborghini (USA).z64>) - static source artifact;
  SHA-256 used for this audit:
  `CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.

For the static race overlay data, an original/splat label `0x8xxxxxxx` maps to
raw ROM offset `label - 0x80000000 + 0xC00` because of the overlay's `0xC00`
file offset. The steering tables and selector float values quoted above were
checked using that mapping and the original USA overlay disassembly.

