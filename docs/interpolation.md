# Frame interpolation

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
The group ID includes the current viewport (`s16[0x80098732]`) so culling
one player's view cannot shift another player's ordinal matches. Other
records retain automatic matching within their own record group.

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
  record identity, car/scenery ordering and split-screen IDs.
- `lambo_rt64_scene_matching`: real RT64 frame matching, reordered objects,
  newly visible objects, the stationary-wheel phase trap, changed part counts,
  a 180-degree camera cut, and explicit projection policy. Switching car ordering back to AUTO reproduces
  `Wheels swapped within the same car`.
- `lambo_rt64_angular_velocity_finite`: finite angular velocity and camera
  cut boundaries at 19/21 degrees, yaw/roll cuts, scaled/invalid bases and valid
  midpoint matrices.

The runtime hook entries are mirrored in `scripts/gen_syms_toml.py`.
Changes to patch 0006 use the existing desktop, Android and CI patch paths.
