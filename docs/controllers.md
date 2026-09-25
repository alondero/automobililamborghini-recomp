# Controllers and input

This page records durable controller facts and the limits of the current
evidence. It is for input changes and bug reports, not a timeline of research.

## Player-facing behavior

Player one uses the preferred connected controller and falls back to the
keyboard. Players two through four must be assigned in Button bindings.
Bindings are stored in the RecompFrontend controller profile.

Controls offers **Gyro steering** and **Auto-accelerate** for player one. Both
default to off and are saved when you select Apply. Button bindings remain in
the adjacent **Button bindings** tab.

Gyro steering uses the Android phone's gyroscope and accelerometer. Hold the
screen upright in either landscape orientation and tilt it like a steering
wheel. Sensors are opened while gyro steering is enabled and the app window is
focused, including menus and race countdowns; steering is only applied
during active driving. The phone's current position becomes neutral when a race
starts or resumes from a pause, and when you return from the settings overlay.
Adjust the full-steering angle, deadzone, or inversion in Controls. Any manual
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
