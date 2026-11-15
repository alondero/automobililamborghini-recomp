# Slip rumble

Status: **Experimental**, implemented and default off. Candidate vectors and
controller feel have not been calibrated in gameplay. This follows the
[research proposal in PR 437](https://github.com/alondero/automobililamborghini-recomp/pull/437).

Enable **Slip Rumble (experimental)** in Enhancements. It adds pulsed feedback
when the vehicle's candidate facing and movement vectors diverge. The choice
is saved as `slip_rumble` in `graphics.json`. General's Rumble Strength controls
both original and added feedback. Turning the enhancement off preserves the
original request stream. Wheelspin feedback is not implemented.

## Ownership and evidence

The human-driving branch in [vehicle_update.c](../guest/vehicle_update.c) calls
`func_80028250`, then `func_8002622C`, then the read-only observer (kind 3).
The observer runs synchronously on the game thread, once per execution of that
branch, rather than on interpolated render frames. It neither modifies guest
RAM nor calls the original motor setter. Guest-updater parity compares the
complete updater with an independent ROM reference; the added observer is the
only deliberately ignored call, with existing service ordering preserved.

The USA layout is the relocated pool at `0x80910000`, capacity 64 and stride
`0x10C`. The signed halfword at `0x80098398` selects the current vehicle; invalid
indices clear feedback. The signed halfword at vehicle `+0x0E` must be 1 for
port zero. Other players cannot publish to this sink or cancel its request,
except that losing ownership of the previously published vehicle clears it.

Float32 fields `+0x40/+0x44` and `+0x6C/+0x74` remain named by offset. In the
recovered updater, service positioning assigns sine/cosine results to the
first pair and projects position differences onto it. The steering branch
rotates that pair and takes its dot product with the second pair. These are
candidate facing and movement vectors, not verified tire-slip measurements.
The signed word at `+0x90` is the existing HUD speed. Its thresholds below are
raw HUD units, with no claim that they are km/h. All reads use the existing
word-swapped RDRAM macros and float bit copies, never host struct casts.

The host computes `atan2(abs(cross), abs(dot))`, which folds straight reverse
travel to zero. Degenerate and nonfinite vectors produce no feedback. Initial
experimental tuning ramps angle from 4 to 30 degrees and absolute HUD speed
from 10 to 30 units. Severity selects one to three on updates in a six-update
cycle; zero severity clears it. These values follow the prototype suggested
in PR 437 and are **not Lamborghini-calibrated thresholds**.

## Mixing and cleanup

The original ROM PWM engine, request words and Controller Pak coexistence path
are unchanged. The main thread combines `original_on || slip_on` before the
existing frontend input-capture gate. The frontend smooths boolean requests
and applies global strength; it does not have the short effect expiry once
claimed by the old host comment. Clearing slip never clears original rumble.

A mutex protects pulse phase, vehicle ownership and steady-clock timestamp as
one publication. No RDRAM pointer crosses to the main thread. Pause, non-driving
race phases, attract playback, finished player one, replay playback and guest
input suppression reject samples. The existing pre-frame assistance hook also
clears slip when those state gates close. Developer warp and save-state restore
explicitly clear it. Main-thread disable, capture and disconnected-controller
gates discard samples and require a fresh publication on return. Controller
add/remove/remap events also clear the previous sample.

The watchdog expires at 150 ms, even if guest execution stops. This is a
conservative initial bound, **not measured simulation cadence**. It resets the
pulse on a long gap. The frontend's smoothing envelope may decay after requests
stop, as it does for original effects. In single-player mode the frontend sends
port-zero rumble to all opened controllers; multiplayer retains its existing
port-zero assignment. Added feedback does not extend support to other ports.

## Validation and remaining work

Host tests cover pulse boundaries, reverse, low speed, nonfinite/degenerate
vectors, OR truth tables, disabled original-stream equivalence, timeout,
disconnect/reconnect, capture, ownership loss and guest lifecycle gates. The
runtime fixture verifies byte order and that observation leaves RAM unchanged.
Frontend tests cover default-off, live application, refresh and persistence.

Run `lambo_slip_rumble_tests`, `lambo_slip_rumble_runtime_tests`,
`lambo_frontend_settings_tests` and `lambo_vehicle_parity_tests` through the
normal CMake/CTest build. The latter requires the USA ROM and regenerated guest
patches; see [testing](testing.md).

Before changing the default or describing this as calibrated tire feedback,
record candidate vectors against position deltas on straight, cornering,
sliding and reversing runs across cars and difficulties. Verify the physics
callee's field write order and actual simulation cadence. Check launches,
braking, impacts, off-road and airborne motion; no verified contact gate exists
yet. Playtest controllers at different display rates and global strengths,
including zero. Replace this offset bridge with named recovered physics fields
when their meaning is established. Host tests cannot establish controller feel
or validate these physical interpretations.
