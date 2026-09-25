# Gyro steering research

Status: **Confirmed** primary-source comparison, checked 2026-09-25. Peer
runtime behaviour and physical Android handling are **Unverified** here.
Implementation recommendations below are **Inferred**, not device measurements.

## Racing recomp comparison

Diddy Kong Racing's **DKR-R** implements angle-based controller gyro input.
At commit `8a8e927e9ea14c5c07ca7ad74b44fdbe077f5ff6`, its input module defaults
gyro off, reads SDL controller gyro data, subtracts a calibrated bias, integrates
angular velocity, and maps the resulting angle to the stick. It supports roll
or yaw, sensitivity, inversion, a rate deadzone, calibration, and recentering.
The controller snapshot path ignores repeated sensor timestamps. Missing or
disabled sensors reset its accumulator. These are useful patterns for held-angle
steering; passing raw angular velocity directly to the stick would instead
require continuous movement to keep turning.
[DKR-R input source](https://github.com/ThatGuyMcd/DKR-R/blob/8a8e927e9ea14c5c07ca7ad74b44fdbe077f5ff6/runtime-recomp/src/game/runtime_input.cpp).

The inspected DKR-R gyro routine gates on its Modern preset and sensor
availability. This research did not establish a race-only gate in its callers,
or support for Android handset sensors. Its controller implementation is not
evidence that those requirements are already solved.
[DKR-R architecture](https://github.com/ThatGuyMcd/DKR-R/blob/8a8e927e9ea14c5c07ca7ad74b44fdbe077f5ff6/docs/ARCHITECTURE.md).

Searches also covered Mario Kart and F-Zero recomp projects. No further
verified gyro-steering implementation was found in this review. SpaghettiKart's
public README documents ordinary keyboard/gamepad bindings but does not
establish gyro steering; absence from that README is not proof of absence from
the project.
[SpaghettiKart README](https://github.com/HarbourMasters/SpaghettiKart/blob/main/README.md).

## SDL and Android facts

- Phone sensors use `SDL_SensorOpen` / `SDL_SensorGetData`; controller sensors
  use the separate `SDL_GameController*Sensor*` API used by DKR-R. Opening a
  device sensor can fail and polling returns an error code.
  [SDL_SensorOpen](https://wiki.libsdl.org/SDL2/SDL_SensorOpen),
  [SDL_SensorGetData](https://wiki.libsdl.org/SDL2/SDL_SensorGetData).
- Gyroscope readings are radians per second; accelerometer readings include
  gravity and use metres per second squared. Axes remain in the device's
  natural orientation when the display rotates. Positive Z points toward the
  user; positive rotation is counterclockwise when viewed from the positive
  axis. Landscape phone steering therefore needs explicit coordinate handling.
  [SDL sensor coordinate definitions](https://wiki.libsdl.org/SDL2/SDL_SensorType).
- SDL's Android sensor backend requests approximately 60 Hz, subject to the
  sensor's minimum interval. Its inspected SDL2 branch passes **zero** as the
  sensor timestamp to `SDL_PrivateSensorUpdate`; an implementation must not
  require a nonzero hardware timestamp. Closing the sensor disables it and
  destroys its event queue.
  [SDL Android sensor backend](https://github.com/libsdl-org/SDL/blob/SDL2/src/sensor/android/SDL_androidsensor.c).
- SDL normally lists Android's accelerometer as a three-axis joystick unless
  `SDL_HINT_ACCELEROMETER_AS_JOYSTICK` is disabled. Explicit sensor handling
  avoids depending on generic joystick bindings for this feature.
  [SDL accelerometer joystick hint](https://wiki.libsdl.org/SDL2/SDL_HINT_ACCELEROMETER_AS_JOYSTICK).
- Android documents integrating gyro rates into orientation and using gravity
  to determine orientation. Sensor availability varies by device, so it must
  be detected at runtime.
  [Android motion sensors](https://developer.android.com/develop/sensors-and-location/sensors/sensors_motion).

## Recommended Lamborghini boundary

Keep both assists default-off and persist them in Controls. Sample phone
motion on the SDL/event thread and publish a host-owned sample; do not call
platform sensor APIs from guest execution. Apply the sample only at the final
race input boundary, after confirming local human driving, and suppress it in
the frontend, retail pause menu, attract/demo playback, and results screens.
Use the same gate for auto-accelerate. Brake should override automatic throttle.

Prefer held-angle steering with gravity correction or an explicit neutral
reference over raw angular velocity. Reset accumulated motion when disabled,
on focus loss, and when entering or leaving the driving state; otherwise menu
movement and background time can become an unintended steering jump. Keep
normal steering available when sensors are absent or stale. A sensitivity
control and small centre deadzone allow different devices and holding styles.

Validation needs synthetic tests for default-off behaviour, race/pause/menu
gates, stale or invalid samples, angle clamping, and brake priority. Physical
Android validation must check both landscape orientations, stationary drift,
suspend/resume, and whether a held turn remains stable. Those physical checks
cannot be replaced by a desktop build or synthetic sensor samples.

## Local guest gate evidence

**Confirmed** from the generated sources in the main checkout, read without
editing. Function names retain their historical addresses; instruction comments
below give the actual instruction addresses shown by the generator.

| Guest s16 | Gate | Source evidence |
| --- | --- | --- |
| `0x800CE6AC` | `8` | `func_800028D0`, instruction `0x80001E80`, dispatches to the race handler `func_800030F8`. |
| `0x800CE6B0` | `3` | `func_80009158` exits unless this is 3 (`0x80008560`); countdown code sets 3 at `0x8005355C` when `0x800CE7C4` reaches 250. Race setup initializes 2; completion paths write 4 or 5 (`0x80019C24`, `0x80019AC4`, `0x8002A644`). |
| `0x800CE808` | `0` | Race handler dispatches this pause/menu substate at `0x80002D9C`. START changes 0 to 1 at `0x80002FF4`; pause state consumes A/START and resume clears it at `0x80003218`. `func_80009158` also exits unless it is zero. |
| `0x800CE6B4` | not `4` | Attract setup copies demo track `0x800CE774` to circuit `0x800CE794`, sets player count 1, then writes mode 4 at `0x800385B8`. A positive player count alone does not exclude attract races. |

Files: `RecompiledFuncs/funcs_0.c` (dispatcher, race handler, setup and active
race checks), `funcs_1.c` (completion phase writes), `funcs_2.c` (attract setup),
and `funcs_5.c` (countdown). Combine all four checks with the human-vehicle
mapping and frontend/focus gate. Multiplayer may need an additional per-player
finished check; the global phase alone need not change when one racer finishes.

A **source-supported completion flag candidate** is s16
`0x800A5EEA + player_index * 0x84`, using the game's one-based player index.
Instruction `0x8002A548` writes 1; `0x8002A660` and `0x8002A670` test players
1 and 2 before changing global phase to 5. Thus player one's flag is
`0x800A5F6E`. Reset is **Confirmed** in `boot_pad_apply_calibration`:
instruction `0x80006B5C` clears this halfword for each record, with loop indices
0 through 15 (`0x80006B1C`, `0x80006FAC`). The dispatcher calls this initializer
in state 7 before setting state 8. Consequently race starts/restarts through
that normal initialization path clear the flag before driving resumes. Other
uses of the record were not exhaustively traced; do not treat this as a general
multiplayer API.

## Gravity and gyro sign

**Inferred mathematically** from SDL's documented axes: with an initially
upright device and positive counterclockwise Z rotation `theta`, gravity's
stationary accelerometer reading in device coordinates becomes
`(g * sin(theta), g * cos(theta))`. Thus `atan2(accelX, accelY)` increases with
positive `gyroZ`; those signs agree for complementary filtering. A landscape
neutral introduces a constant angle offset, which should be removed by
calibration and wrapped across the angle boundary. For steering where positive
stick X means right, negate the resulting counterclockwise-positive angle.
Physical-device validation remains necessary, especially near a flat pose
where the XY gravity projection becomes small.

## Implementation and verification

The port uses phone sensors, a complementary gravity/gyro filter, and the
active-race checks above. These settings affect player one. Controller gyro
support remains outside this implementation. The phone pose at the first valid
driving sample establishes neutral; missing/stale sensors fall back to manual
steering. Synthetic input is applied before recording and excluded from replay
playback and the physical-input release barrier.

Checked on Windows with MinGW-w64 GCC 15.2.0, 2026-09-25:

- Three focused native suites passed: driving-assist policy/filter, guest
  replay/input integration, and frontend settings persistence/Apply/Discard.
- All 29 Python host tests and the documentation check passed.
- Changed production translation units compiled against existing dependency
  headers. An isolated incremental executable reused cached dependencies and
  generated game output; this was not a clean supported-script build.
- The headless `scenarios/harness-smoke.json` run passed: 600/600 replay frames
  verified, 601 swaps, maximum player speed 175 in the harness's guest units.
  This checks regression/playback, not physical gyro or assisted driving.
- USA ROM SHA-256:
  `cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c`.
  No generated output was edited or regenerated for this work.
- No Android device was connected. Android compilation, phone handling, sensor
  lifecycle, and interactive race/pause/results transitions remain unverified
  on hardware. Follow the Android checks in [Testing](testing.md).
