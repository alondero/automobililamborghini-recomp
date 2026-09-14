# Frame interpolation

Status: implemented subsystem notes with historical runtime measurements. The
replay and native checks were not rerun in this audit.

Cars are rendered by `func_8000A6C0`'s scene-record walk. The linked-list
iteration begins at runtime `0x80009AF8`, reads the record slot from
`sp+0x1B2`, and converges at `0x8000E400` before following record offset 6.
Records start at `0x800B69A8`, stride `0x10C`. These hooks bracket each
iteration, including the culled paths, without changing the original meshes.

The record flag `0x8` identifies the car draws in the measured race path:
slots 1, 4, 7, 10, 13 and 16 submit a body followed by four wheels. Their
matrix pool addresses alternate between buffers and their traversal order
changes with visibility. RT64 must therefore match by record and part order,
not by pool address, screen position or estimated angular velocity.
The group ID includes the current viewport (`s16[0x800CE6A6]`, 1 through 4) so culling
one player's view cannot shift another player's ordinal matches. Other
records retain automatic matching within their own record group.

The frame builder initializes that viewport counter at runtime `0x80004AA8`
and advances it at `0x80005124`. Do not use `0x80098732`: it is the race lap
high-water mark, updated at `0x80029794` from the player lap field at
`0x800A5EF0 + player * 0x84`. Using that field changed all car IDs on start-line
crossings, including the initial crossing in Arcade. The cars lost their previous
transforms for one game frame, then translation interpolation stayed disabled
for another while velocity history recovered. The camera continued interpolating,
making the car's two-frame jump look like a camera reset. Close chase settings
make this especially visible.

If an explicitly ordered group's transform count changes, patch 0006 leaves
that group unmatched for one frame. It must not pair a shortened sequence
with the beginning of an old sequence. Rigid car geometry keeps vertex and
texture-coordinate interpolation disabled.

Patch 0006 also detects anonymous camera rotations greater than 20 degrees
per game frame using the normalized three-axis basis. It resets all camera
interpolation components together, including translation. Small pans retain
interpolation; explicit projection tags retain control of their own policy.
This prevents the singular midpoint of a front-to-rear camera reversal.
It is an angular cut detector, not a detector of every possible camera teleport.

## Regression evidence

The September 2026 Viewer reproduction runs at 30 Hz game updates and
120 Hz presentation. Enter Viewer during the starting countdown and sweep
the camera around the stationary grid, then return to the chase camera.
Per-record AUTO tagging alone still swaps wheel pairs inside a car:
one captured frame pairs `t14 <- p15`, `t15 <- p14`, `t16 <- p17`, and
`t17 <- p16`, creating false displacements of 1.58 and 1.66 units.

Across comparable grid/Viewer captures, that intermediate implementation
recorded 536 car-part matches with displacement greater than 0.1 units out
of 9,035. Ordered car groups recorded zero out of 9,105. These measurements
come from temporary instrumentation of the real RT64 matcher; the logs are
local artifacts and the instrumentation is removed from the shipped patch.

Automated coverage:

- `lambo_interpolation_tags`: production GBI emission, balanced groups,
  record identity, car/scenery ordering, four distinct viewport IDs and stable
  identities across lap high-water marks 0 through 30.
- `lambo_rt64_scene_matching`: real RT64 frame matching, reordered objects,
  newly visible objects, the stationary-wheel phase trap, changed part counts,
  a 180-degree camera cut, and explicit projection policy. Switching car ordering back to AUTO reproduces
  `Wheels swapped within the same car`.
- `lambo_rt64_angular_velocity_finite`: finite angular velocity and camera
  cut boundaries at 19/21 degrees, yaw/roll cuts, scaled/invalid bases and valid
  midpoint matrices.

The scene-matching test feeds production-emitted car IDs across a start-line
transition into the real RT64 matcher. Both it and the tag test fail with the old
lap-based ID. A 600-update Circuit 1 Arcade replay using `harness-smoke.jsonl`
reproduced the reported jump at workload 241: the player's five IDs changed from
`0x10000001` to `0x10010001`, all five matches were lost, and translation resumed
at workload 243. This was measured with buffered native renderer traces; a time
trial comparison did not trigger the same lap-field transition.
With the viewport fix, that same Arcade replay retains all five mappings and
translation interpolation through workloads 240 through 243; no player-part
mapping is lost after the initial scene appears during the 600-update run.

The runtime hook entries are mirrored in `scripts/gen_syms_toml.py`.
Changes to patch 0006 use the existing desktop, Android and CI patch paths.
