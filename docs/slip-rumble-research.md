# Additive slip rumble

Status: source review and implementation proposal; no slip effect implemented or
controller playtest performed. Lamborghini baseline: `a917ca59956693fa2d313e12b68f85b6d1f4b45b`.

## Reference implementation

GT64 was inspected at commit
[`c777744c6efa5e63eb7537b591dfe80e75469448`](https://github.com/tijayfly/gt64_recomp/tree/c777744c6efa5e63eb7537b591dfe80e75469448).
Its [slip calculation](https://github.com/tijayfly/gt64_recomp/blob/c777744c6efa5e63eb7537b591dfe80e75469448/src/game/enhancements.cpp#L1590-L1607)
combines two independent signals:

- Wheelspin: absolute wheel/road RPM mismatch, divided by road RPM with a 3000
  floor. A 5% dead band ramps to full strength at 40% mismatch. Neutral disables
  this component.
- Sliding: wrapped heading versus travel-direction angle, folded for reverse.
  Strength ramps between 4 and 30 degrees and is multiplied by a speed ramp
  between 10 and 30 km/h.

The larger signal wins. These are GT64 units and tuning, not Lamborghini values.
The [pulse generator](https://github.com/tijayfly/gt64_recomp/blob/c777744c6efa5e63eb7537b591dfe80e75469448/src/game/enhancements.cpp#L1611-L1641)
uses six physics-update phases and zero to three on phases. This encodes severity
as duty cycle; it does not directly command analog motor strength.
Its [hook](https://github.com/tijayfly/gt64_recomp/blob/c777744c6efa5e63eb7537b591dfe80e75469448/gt64.us.toml#L132-L136)
runs at entry to the function the author identifies as wheel-contact processing.

The important design is the [per-controller mixer](https://github.com/tijayfly/gt64_recomp/blob/c777744c6efa5e63eb7537b591dfe80e75469448/src/game/enhancements.cpp#L1558-L1583):
it stores original and slip motor requests separately and outputs their boolean
OR. Neither source's stop can cancel the other source. Original request timing
is preserved, although overlapping pulses change the combined feel.
The [VI watchdog](https://github.com/tijayfly/gt64_recomp/blob/c777744c6efa5e63eb7537b591dfe80e75469448/src/game/enhancements.cpp#L1644-L1664)
clears only slip after more than six VIs without a refresh. The
[setting](https://github.com/tijayfly/gt64_recomp/blob/c777744c6efa5e63eb7537b591dfe80e75469448/src/game/config.cpp#L79-L90)
defaults on; disable and missed updates rely on the watchdog for cleanup.
Car/controller ownership, accessory state and pause state gate sampling.

GT64's frontend also smooths binary pulses before applying global Rumble Strength;
see its pinned [frontend implementation](https://github.com/N64Recomp/RecompFrontend/blob/d0d90ba49f46f4896aaeda362056c21b1e342561/recompinput/src/input_state.cpp#L71-L134).
This is a good fit for this port's boolean rumble API. The reuse opportunity is
the architecture, not GT64's memory offsets or physical calibration.

## Lamborghini evidence

[The original rumble audit](rumble-triggers.md) and
[current native engine](../src/libultra_stubs.c) establish that ROM requests
already produce PWM. Keep the request words, accumulator, Controller Pak
coexistence overrides and original start/stop path intact.
[The host sink](../src/main.cpp) publishes `g_rumble_on` atomically from the game
thread. `rumble_apply()` calls frontend rumble on the main thread and suppresses
it while the frontend captures input. Physical output currently uses port zero.

The pinned Lamborghini
[frontend](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/input_state.cpp#L71-L134)
accepts a boolean. Each update raises its envelope by 0.17 while on; off multiplies
by 0.92 and subtracts 0.01, with clamping. Smoothstep and global strength scale
the high-frequency SDL motor. Its dummy effect duration is 1,000,000 ms.
The nearby `main.cpp` comments claiming full-strength output, 150 ms expiry and
edge-only refresh are stale. Do not rely on those comments for stale-slip cleanup.
Envelope timing depends on frontend update cadence, so identical pulse duty does
not establish identical feel across ports or frame-pump rates.

[The recovered updater](../guest/vehicle_update.c) and
[guest layout](../guest/vehicle_update.h) provide useful candidates:

| Data | Evidence and confidence |
| --- | --- |
| Position `+0x14/+0x18/+0x1C` | Named float32 X/Y/Z fields. |
| `+0x40/+0x44` | Float32 candidates for planar facing. The service-position branch assigns trigonometric functions of an authored angle. The steering branch rotates this pair before a dot product. Orientation/sign convention still needs confirmation. |
| `+0x6C/+0x74` | Float32 candidates for planar movement. Service positioning compares their norm to remaining distance projected onto the facing candidates, then zeros both on arrival. Exact units and update timing remain unverified. |
| Speed `+0x90` | Signed 32-bit HUD speed, already documented in [analog throttle](analog-throttle.md). It is not proof of wheel RPM. |

Vehicles occupy `0x80910000`, stride `0x10C`, capacity 64; human channel is the
signed halfword at `+0x0E` (1..4). See [host schema](../src/lambo_vehicle.h) and
[updater ownership](vehicle-source.md). Guest layouts are big-endian MIPS;
host observers must use the existing word-swapped memory access conventions.
Read only on the game thread, validate indices and human ownership, and publish
values rather than a guest-memory pointer to the main thread.

No verified independent wheel/road RPM pair was found in this review. High engine
speed plus low road speed is insufficient evidence of wheelspin. The candidate
fields must retain neutral names until their meaning is measured.

## Proposed implementation

1. Add a read-only game-thread observer after the controlled vehicle's physics
   update. The human branch in `guest/vehicle_update.c` calls `func_80028250()`
   then `func_8002622C()`; confirm the latter's write ordering before placing the
   observer. Observe once per simulation update, not per interpolated render.
2. Validate facing and movement candidates against position deltas on straight,
   turning, sliding and reversing runs. If confirmed, normalize facing `h` and
   compute `forward = dot(v,h)` and `side = v.x*h.z - v.z*h.x`.
   `atan2(abs(side), abs(forward))` gives a reverse-safe slide angle. Reject
   nonfinite values and degenerate facing/movement. Add a low-speed fade and
   angle dead band tuned from Lamborghini traces. If movement fields fail,
   position deltas are a fallback only after validating facing and rejecting
   warps/resets. Wheelspin remains a separate follow-up signal investigation.
3. Convert severity into zero to three on ticks in a six-tick cycle as an initial
   prototype. Measure simulation cadence and controller feel before retaining
   that period. Keep pulse state on the game thread and publish an atomic slip
   request plus coherent freshness/epoch information.
4. In `rumble_apply()`, combine `original_on || slip_on`, then apply the existing
   frontend input-capture gate and call the existing frontend API. Clear slip
   independently: never call `lambo_pak_set_rumble(0)` to end a slip effect.
   This preserves every original on request and leaves original PWM unmodified.
5. Add a persistent Slip Rumble toggle through the existing
   [settings integration](../src/ui/lambo_frontend_settings.cpp). Off selects the
   original path exactly; both sources use the existing global Rumble Strength.
   Limit the first implementation to the currently supported port-zero sink.
6. Reuse the state predicates from
   [driving-assist gating](../src/lambo_driving_assists_runtime.cpp): live race,
   active driving phase, unpaused, no attract or replay playback, unfinished human
   player, and guest input not suppressed. Reset slip phase and history on
   disable, ownership change, race exit, warp and save-state restore. Clear on
   disconnect and reject stale samples on reconnect. Add a main-thread freshness
   timeout so missing game-thread updates cannot sustain slip. Set its duration
   from measured simulation cadence; do not rely solely on a VI counter that
   might also stop during a pause.

The OR mix fills some original off intervals during slipping. That is intentional
additive feedback, not bit-identical combined output. If original impacts become
harder to distinguish, reduce slip duty rather than rewriting the ROM requests.

## Validation required before enabling

Trace candidate vectors, position deltas, speed, channel, phase, slip severity,
original bit and mixed bit. Compare straight driving, gripped corners, induced
slides, reverse, launch, braking, impacts, off-road travel and airborne motion.
Airborne effects need a verified contact gate if the measured signal produces
false slip; this review has not established that field. Include each car category
and difficulty in calibration rather than assuming GT64's thresholds transfer.

Test mixer truth tables, original-only trace equivalence when disabled, original
on preservation, pulse boundaries, invalid samples, pause/replay/exit/reset and
stale publication. Run guest-updater parity if adding an observer there, allowing
only the intended observer call difference. Finally playtest supported controllers
at different display rates and global strengths, including zero. A source review
cannot establish vibration feel or prove these candidate fields are tire slip.
