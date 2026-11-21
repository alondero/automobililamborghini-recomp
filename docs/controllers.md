# Controllers and input

This page records durable controller facts and the limits of the current
evidence. It is for input changes and bug reports, not a timeline of research.

## Player-facing behavior

Player one uses the preferred connected controller and falls back to the
keyboard. Players two through four must be assigned in Controls.
Bindings are stored in the RecompFrontend controller profile.

**Driving** holds the throttle and brake configuration and offers **Gyro
steering** and **Auto-accelerate** for player one. The assists default to off and
everything is saved when you select Apply. Button bindings remain in the
adjacent **Controls** tab.

Driving also holds two steering settings for player one: direct steering
response and the game's own steering deadzone. Both defaults are the original
game. See [Steering response](steering.md).

Gyro steering uses the Android phone's gyroscope and accelerometer. Hold the
screen upright in either landscape orientation and tilt it like a steering
wheel. Sensors are opened while gyro steering is enabled and the app window is
focused, including menus and race countdowns; steering is only applied
during active driving. The phone's current position becomes neutral when a race
starts or resumes from a pause, and when you return from the settings overlay.
Adjust the full-steering angle, deadzone, or inversion in Driving. Any manual
stick input or digital left/right steering takes priority over gyro steering.
Missing, stale, or invalid sensor samples temporarily fall back to normal
steering without changing the calibrated neutral. Controller motion
sensors are not used by this option.

Auto-accelerate holds the race accelerator until you brake. Both digital and
analog braking suspend automatic acceleration; releasing the brake resumes it.
Manual accelerator input still works. Assists are inactive during countdowns,
menus, pause, attract demos, player-one completion, and replay playback.

Status: **Experimental**. Host tests cover steering, calibration retention,
manual steering priority, race gating, the dedicated guest assistance hook,
and replay capture of its final pad state.
Physical phone handling remains **Unverified**. The
[research notes](gyro-steering-research.md) record the comparison with DKR-R,
SDL sensor details, and guest-state evidence.

The port has separate host input and game input paths. Host events update the
active profile and publish a controller snapshot. The recompiled game consumes
that snapshot on the game thread. A menu binding working does not prove that a
race receives the same input.

The settings overlay has its own bindings, separate from the N64 button
bindings: A accepts the highlighted item and Back backs out. The same Back
binding answers a confirmation prompt, including the quit confirmation, by
running that prompt's own cancel action. The affirmative Quit stays on its own
confirmation. One case is worth knowing: on the graphics "unapplied changes"
confirmation, Back discards those changes, exactly like its Discard button. The
bindings are changed in Controls, and the overlay uses the profile of the
pad that opened it.

Status: **Confirmed** in a played build for the quit confirmation, which was
opened in the running game and dismissed with the Back action both before and
after the patch. **Unverified** with a physical gamepad: the button-to-key hop
that turns a pad's B into the Back action was covered by
`lambo_frontend_settings_tests`, not played. See
[frontend notes](recompfrontend.md#the-quit-confirmation) for the route, the
F15 substitution, and what the playthrough showed.

## Custom controller mappings

If SDL does not recognize your controller, add its SDL mapping to a file named
`gamecontrollerdb.txt` and restart the game. On Windows and Linux the game checks
beside the executable first, then in the active [configuration directory](configuration.md),
including a directory selected with `--data-dir`. Portable mode normally uses
the same directory for both. For equivalent mappings, the configuration file
takes priority. SDL's `SDL_GAMECONTROLLERCONFIG` environment variable can still
override an equivalent mapping for one launch.

Include a matching platform field followed by a comma on each mapping line,
such as `platform:Windows,` or `platform:Linux,`. On Android the game checks
the app-private `files/LamborghiniRecomp` directory. The Android launcher has
no mapping import option.

For an N64-style pad, the default bindings use SDL `a`, `b`, `start`,
`leftshoulder` and `rightshoulder` for A, B, Start, L and R, and `lefttrigger`
for Z. Use `leftx` and `lefty` for the stick, the four `dp` directions for the
D-pad, and the signed halves of `rightx` and `righty` for the C buttons. If
the pad has no Select button, leave `back` unmapped and use F1 to open Settings.
Saved profiles can still change the game's bindings in **Controls**.

The [optional Windows example](controller-profiles/generic-n64-windows.txt)
comes from MatyMeatBoy's measurements for one transparent-red, three-prong
N64 USB pad. Copy its lines into `gamecontrollerdb.txt` only if your pad
matches the measured layout. A, B and
Start are buttons 5, 4 and 9. Z, L and R are buttons 8, 6 and 7. C-up, C-right,
C-down and C-left are buttons 0, 1, 2 and 3. The stick uses axes 0 and 1, and the
D-pad uses hat 0. Incompatible pads share USB ID `0079:0006`, and SDL 2.26.3
can fall back across CRC variants for that base GUID. This example can therefore
affect a second pad with the same USB ID. Physical play in this port remains
**Unverified**. [AeroGauge PR #129](https://github.com/alondero/aerogauge-recomp/pull/129/files)

## The Android touch overlay

The on-screen controls send keyboard keys, and the frontend adds the keyboard
and the gamepad profile together, so a connected gamepad and the touch pad can
drive the same car twice. The Android app therefore hides the on-screen stick
and buttons while a controller is connected, and shows them again when it
disconnects. The Menu button remains, so Settings stays reachable, and the
launcher offers **Touch controls: Auto / Always / Never**. See
[Android](ANDROID.md) for the player-facing description.

Presence decides this, not the Controls assignment. A gamepad connected but
assigned to player two or three still hides player one's touch controls on a
tablet, because the Android layer cannot read the controller profile. The
launcher's **Always** choice is the way out of that case. No button press is
needed to trigger it: Android's input-device listeners report connects and
drops, and a device counts when SDL's own Android joystick handler accepts it.

Status: **Experimental**. Host tests cover the visibility rule, the release of
held keys, the stored choice, and which touches the overlay keeps while the pad
is shown or hidden. A phone with a real Bluetooth gamepad is **Unverified**.

## Pre-race menu navigation

The pre-race menus turn the analog stick into menu movement with a fixed
threshold of about 51 of the game's 80-point stick range (roughly 64% of full
deflection), so a device with a limited analog range can feel unresponsive. The
Enhancements settings tab offers an opt-in **Menu stick sensitivity** multiplier
(1.0 = stock, up to 2.5) that scales the stick inside the menu routine only. It
is off by default, is saved as `menu_stick_sensitivity`, and never affects race
steering, the D-pad, or the keyboard. See [menu input](menu-input.md) for the
ROM seams.

## Guest layout evidence

The following layout is reported by an external measurement, but this checkout
has no checked-in capture or test that proves it. Treat it as a hypothesis when
changing multiplayer input:

- controller reads use eight-byte records near guest address `0x8011C640`;
- the `OSContPad` mapping was reported as an identity by SI channel with a
  six-byte stride;
- a setter path writes player records, so the menu writer is not the only
  possible writer;
- the no-input state appeared to match across all eight ports;
- one channel-zero guard left channels one through three uncovered in one
  input path;
- Controller Pak traffic may interleave with controller reads, but the exact
  ownership and ordering are not established here.

These statements are useful leads, not a supported ABI. Do not rename unknown
fields or move a guest-memory bridge based on them alone.

## Evidence required for an input change

An input change that depends on guest layout should include:

1. the ROM identity and emulator or hardware version;
2. controller reads and writes for channels one through four;
3. the input buffer address, width, byte order, and units;
4. any Controller Pak transaction captured at the same time;
5. a focused fixture or host test when the behavior can be isolated without a
   copyrighted ROM.

If the evidence cannot be reproduced, keep the claim marked unverified and
describe the failure mode. The long-term replacement for fixed input bridges
is a named source-level function or runtime interface with a stable contract.
