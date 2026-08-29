# Track modding research

**Status:** ROM/decomp audit, 2026-08-29

This is a working map of the data and runtime contracts that have to move
together to add or edit a circuit. It combines direct inspection of
`Automobili Lamborghini (USA).z64`, the companion disassembly, live-RDRAM
captures, and the recomp port's no-LOD instrumentation. It is intentionally
explicit about uncertainty: a plausible binary pattern is not treated as a
confirmed game-system fact until a consumer or runtime behavior supports it.

## Executive summary

A circuit is not one mesh. At minimum, a playable track is a coordinated
bundle of:

1. visual segment records and their road/wall/scenery display lists;
2. visibility rows (the authored PVS-like segment walk);
3. auxiliary segment test points used by distance and view-cone culling;
4. waypoint/track-point records used by AI, race position, and progress logic;
5. wrap/boundary metadata used to move between segments and laps;
6. per-track context/descriptor pointers established during race setup;
7. surface/collision data, whose exact consumer is still unresolved; and
8. texture, palette, model, and scenery descriptors referenced by the display
   lists or setup commands.

The important separation is that rendering visibility and AI navigation are
different systems. Widening the renderer's segment walk does not give AI a new
road, and editing waypoint coordinates does not create road geometry or
collision. A first modding prototype should therefore reuse the existing
closed-loop segment renderer and replace a complete set of parallel data
tables, rather than attempt to replace only the visible mesh.

The port's current no-LOD features are useful as an investigative tool. They
remove or widen several author-time visibility assumptions, making omitted
segments, floating geometry, missing scenery, and car LOD substitutions
observable. They do not synthesize missing geometry and do not bypass every
remaining cull or physics contract.

## Confidence and address conventions

* **Confirmed** means directly read from code/data, or reproduced in a live
  capture.
* **Strong** means the format or relationship is repeatedly supported, but a
  final consumer/cross-reference is missing.
* **Candidate** means useful for investigation, not safe to edit as fact.
* **Unknown** means the bytes or field have not yet been assigned a reliable
  meaning.

Addresses in this document need a domain label:

* ROM offsets are offsets into the 4 MiB big-endian `.z64` image.
* Runtime/VRAM addresses are the addresses used by the MIPS code and live
  RDRAM captures.
* The race overlay's disassembly uses a `-0xC00` runtime shift for overlay
  code relative to some splat symbols. Fixed-RAM globals and loaded asset data
  are not automatically shifted. In particular, do not subtract `0xC00`
  from every `0x800A...`, `0x801...`, or asset address.

When adding an extractor, keep ROM offsets and runtime pointers as separate
fields and record the loader mapping that produced each pointer.

## 1. Runtime data flow

The useful high-level model is:

```text
circuit selection
    -> track context / descriptor setup (D_80098238)
        -> segment records + PVS rows + test points + waypoints
            -> scene builder: distance + cone + authored visibility
                -> road / wall / scenery display lists
            -> AI tick: waypoint coordinates + links + progress scalar
                -> steering, segment advance, lap/race position
            -> physics/collision: separate surface/contact data (not decoded)
```

The scene builder and AI use different primary arrays. This is why a visual
track export is not yet a track mod: the car needs a coherent path and the
physics code needs a compatible surface representation.

## 2. Circuit selection and context setup

### 2.1 Track context

**Confirmed:** the global at runtime `0x80098238` points at the active track
context during a race. The setup code initializes it from a race context around
`0x80251800`, then copies context fields into per-viewport globals. The known
context fields are:

| Context offset | Observed use | Confidence |
|---|---|---|
| `+0x00` | scene-builder segment-record source / related track data | strong |
| `+0x04` | PVS row block; copied to `0x800CE678` | confirmed |
| `+0x08` | PVS end address; the same pointer is copied to `0x800BF1C8`, where later code reads 16-byte coordinate/progress records | confirmed dual-use; table extent unknown |
| `+0x0C` | another per-segment table; copied to `0x800BF1C4` | confirmed, field meaning unknown |
| `+0x10` | auxiliary 16-byte test-point records; copied to `0x800BF1C0` | confirmed |

The scene builder also has a per-viewport pointer at `0x800BF1D0` for its
64-byte segment records. The `+0x08` dual use is important: it makes the
waypoint-like array begin immediately at the address that the scene builder
uses as the end of the PVS block, but the array's own extent is not encoded by
this observation. The context-to-viewport copy happens in the race
initialization path; the complete initialization of every pointer should be
traced before designing a replacement loader.

Setup calls `func_80067790` with circuit-indexed values from fixed tables near
`D_80088DC0` and `D_80088DF0`, writing data into the `0x80251800` and
`0x80305800` context areas. Those fixed tables contain code-like command or
descriptor data, not simple ROM pointers. Their exact format remains
unresolved, but they are an important reason a mod loader cannot assume that
changing one global pointer is sufficient.

### 2.2 Circuit index

The game has six integer-indexed circuits. The menu does not contain track
names; it presents `CIRCUIT 1` through `CIRCUIT 6`. The port uses zero-based
indices internally and in `graphics.json`; see `docs/TRACK_INDEX.md`.

The authored 1-player scene radii, read from the `float[6][5]` table at runtime
`0x80088FD0`, are:

| Internal circuit | Menu circuit | 1P | 2P | 3P | 4P |
|---:|---:|---:|---:|---:|---:|
| 0 | 1 | 55000 | 55000 | 50000 | 25000 |
| 1 | 2 | 50000 | 50000 | 40000 | 20000 |
| 2 | 3 | 40000 | 40000 | 30000 | 20000 |
| 3 | 4 | 45000 | 45000 | 30000 | 25000 |
| 4 | 5 | 35000 | 35000 | 27500 | 25000 |
| 5 | 6 | 35000 | 35000 | 27500 | 25000 |

These are rendering budgets, not track dimensions. A new circuit needs an
authored radius or a port-side default, but increasing it cannot repair a
missing segment, PVS entry, display list, or collision surface.

## 3. Visual geometry and scenery

### 3.1 Segment record contract

**Confirmed:** the scene builder treats each segment as a 64-byte record. The
most useful known fields are:

```c
struct SegmentRecord {
    /* 0x00 */ uint32_t unknown_or_geometry_data;
    /* 0x04 */ Gfx *road_dl;
    /* 0x08 */ Gfx *wall_dl;
    /* 0x0C */ Gfx *far_scenery_dl;
    /* 0x10..0x3F */ uint8_t unknown[0x30];
};
```

The exact field type is a runtime pointer after loading, not necessarily a
literal 32-bit ROM address. The builder can emit up to three sub-display lists
from this record. A null road record is treated as a non-renderable/sentinel
segment by the port's synthesized full-track walk. The remaining fields need
to be decoded before authoring new segment test points or segment-local state.

There is one additional geometry/cull relationship that is useful enough to
record separately. The offline `tools/pvs_distance_audit.py` reproduces the
builder's point calculation as:

```text
point(segment):
    point_index = segment_record[+0x20]
    x = point16[point_index][+0x00] + segment_record[+0x10]
    z = point16[point_index][+0x02] + segment_record[+0x12]
```

The record-relative `+0x20` index, the `+0x10/+0x12` offsets, and the 16-byte
point stride at `0x800BF1C0` are confirmed by the cull replay and matching
assembly. This is an auxiliary segment anchor/cull point, not a vertex buffer
and not proof that it is the AI waypoint. It should be exported in a track
format because a valid display list can still be distance-culled when this
anchor is wrong.

The `+0x0C` list is not merely cosmetic: it contains distant relief, trees, and
trackside structures that were omitted in 2P-4P by an integer player-count
gate. The port now routes that gate through
`lambo_no_lod_scenery_guard()` in `src/lambo_no_lod.cpp`.

### 3.2 Authored visibility rows

**Confirmed:** `0x800CE678` points at the active visibility block. Each row is
20 bytes: ten signed big-endian halfword segment indices. Negative values are
holes, not row terminators. The native loop has a ten-entry cap and skips a
negative slot before advancing.

The active header at `0x80098238` supplies:

```text
header + 0x00  segment-record list
header + 0x04  PVS base
header + 0x08  PVS end
header + 0x10  16-byte auxiliary test-point records
```

The observed row count is `(header+0x08 - header+0x04) / 20`. On circuit 5,
the block is 1100 bytes, giving 55 rows; a following all-null record acts as a
natural sentinel. This is a critical extraction rule: parsing rows until the
first `-1` silently loses entries after holes and undercounts the circuit.

The original N64 scene walk uses the camera segment's row. The port's optional
full-track walk synthesizes a row containing authored entries first, then the
other valid segment indices, excluding the camera segment and null records.
This makes the renderer useful for auditing geometry, but it also exposes
parallel structures and increases draw-list pressure.

### 3.3 Distance and cone culling

The builder applies more than one visibility test:

1. A coarse horizontal distance test uses the per-circuit/player-count radius
   table at `0x80088FD0` and a forward-cone test.
2. A fine path checks up to 16 auxiliary sub-points of the segment against the
   same radius and a front-half-plane condition.
3. The authored cone threshold is a double `0.886` at `0x8008D8C0` and
   `0x8008D8C8`, approximately a 27.6-degree half-angle.

The port's camera-FOV hook rewrites the cone constants per frame so the cone
tracks an expanded projection. The draw-distance hook scales the authored
radii; `0` means effectively unlimited. These fixes are useful for capture,
but the cone tests remain meaningful and do not implement occlusion.

### 3.4 Draw-list capacity

The widened walk appends segment entries to a shared per-frame list at
`0x800B6758`. The observed list has 21 usable slots, with the next global at
`0x800B6782`. The port clamps writes at the last slot in split-screen because
the list accumulates across viewports. A new circuit with more visible
segments, or a full-track audit on a large circuit, may require increasing this
capacity rather than merely widening the PVS loop.

### 3.5 What “missing geometry” means

With no-LOD and full-track visibility enabled, a missing object can be sorted
into separate causes:

* no segment index in the active PVS/segment table;
* a valid segment record whose road/wall/scenery pointer is null;
* a display list that references missing vertices, textures, or model data;
* a valid display list rejected by the distance or FOV cone;
* geometry intentionally placed far away with no connecting geometry; or
* geometry present visually but unsupported by collision/physics data.

The combined port enhancements address parts of the fourth case, but through
separate hooks: no-LOD scales distance reach and the full-track walk supplies
candidates; the camera hook widens the FOV cone. They do not create
intermediate road pieces. Unlimited reach can make author-intended hidden gaps
look like floating sections across the map; that is evidence about authored
culling assumptions, not proof that the segment is bad.

Two measured city-track observations make this concrete. At one capture the
camera was in segment 21 and segment 54 was only about 1,027 world units away,
but was absent from the authored row. The stock drawn list was
`[21,22,23,24]`; the widened walk reached `[21,22,23,24,25,48,49,50]`, adding
645 triangles. A separate radius audit found segment 31 around 51,000 units
away against the authored 35,000-unit circuit-5 radius. These are culling
examples, not evidence that those segments are missing from the ROM. They are
good regression fixtures for a future track extractor.

## 4. Waypoints, AI, and race progression

### 4.1 Waypoint record

**Confirmed:** the active waypoint/track-point array at `0x800BF1C8` has a
16-byte stride. Assembly consumers establish this layout:

```c
struct WaypointRecord {
    /* 0x00 */ float plane_a;
    /* 0x04 */ float plane_b;
    /* 0x08 */ int16_t progress_or_extent;
    /* 0x0A */ uint16_t unknown;
    /* 0x0C */ uint32_t unknown;
};
```

The two floats are used as horizontal-plane coordinates. Naming them `x` and
`z` is safer than assuming the engine's vertical-axis convention: the current
assembly often performs 2D differences and normalization without exposing the
semantic axis names.

The signed halfword at `+0x08` is definitely not a float z coordinate. It is
converted to float/double by `func_80029628`, `func_8002AE94`, and
`func_80030D98`; code scales it by 0.5, adds thresholds such as 10 or 30, and
compares it to distances/progress. The safest current name is
`progress_or_extent`. It may represent segment length, a transition extent, or
an event threshold, but that distinction is not proven. `+0x0C` has not yet
produced a reliable read in the main waypoint consumers.

Do not export or edit only the two coordinate floats. Preserve the `+0x08`
scalar and unknown fields until the progress subsystem is understood.

### 4.2 Steering path

The clearest AI/path consumer is `func_8002D37C`:

1. It selects an active AI slot from `0x800CE6AA` and a 0x84-byte state record
   in the 16-slot array at `0x800A5EE8`.
2. It updates the slot's progress accumulator at `+0x2E` using a signed table
   value from `D_80089404`.
3. It copies the current lane/track id at `+0x16` and segment at `+0x18` into
   shared scratch globals.
4. It derives next and previous segment indices, including wrap, through the
   16-byte records at `D_80121750`.
5. It reads the next/previous waypoint `+0x00/+0x04` values, computes a 2D
   direction, normalizes it, and stores per-AI direction scratch values.
6. It combines that direction with active-car rotation/shadow state and writes
   steering-related scratch values used by the later vehicle/physics path.

This gives a practical model: the stock AI follows a linear ordered waypoint
loop, not the renderer's PVS graph. A branch, shortcut, or alternate lane will
need new segment-selection logic; changing the display lists or adding a PVS
edge will not make stock AI choose it.

### 4.3 Segment wrap and link metadata

`D_80121750` is a 16-byte metadata array used by the segment progression
helpers. The core reads four halfword fields at `+0/+2/+4/+6` to establish
forward/backward boundaries and wrap to the other end of the loop. Exact field
names remain partly unresolved, but this table is part of the closed-loop
contract.

`func_80015044` initializes a separate table at `D_801675D0`. It loops over
track/lane ids and segments with a `track_index << 14` (0x4000) stride and
16-byte records. In the generated records it writes:

```text
+0x08  previous segment (segment - 1)
+0x0A  next segment (segment + 1)
+0x0C  zero/initial marker
+0x0D/+0x0E copied boundary/track values
```

This generated table is distinct from the waypoint coordinates. It is likely
used by lap/minimap/race-position code as well as progression, so a mod format
should represent it explicitly or regenerate it from a documented loop model.

### 4.4 AI state contract

The state base `0x800A5EE8` uses 0x84-byte records and a 16-slot loop. Fields
observed in the race code include:

| Offset | Observed role |
|---:|---|
| `+0x00/+0x02` | flags/state bits |
| `+0x16` | lane/track selector |
| `+0x18` | current segment |
| `+0x2E` | progress accumulator |
| `+0x30` | countdown/timer-like value |
| `+0x38/+0x3C` | floating state used by race logic |
| `+0x44` | mode/state selector |
| `+0x48` | initialized to 0 in setup |
| `+0x4C` | initialized to 1 in setup |
| `+0x78` | initialized to -1 |
| `+0x80` | initialized to 200 |
| `+0x82` | initialized to 0 |

Some slots are structurally reserved for the player/camera and some for AI;
the exact active/ghost convention needs a runtime trace. The important modding
point is that the array is fixed-size and the code currently assumes a finite
set of competitors, independent of segment count.

## 5. Collision and surface geometry

### 5.1 Strong binary candidate: 16-byte plane-like records

The ROM scan in `tools/extract_track_data.py` found six large, exact contiguous
runs of big-endian 16-byte records. Every one of 3228 records matches four
finite floats: a non-negative scalar followed by a unit-length 3-vector.

| Table | ROM start | ROM end | Records |
|---:|---:|---:|---:|
| 1 | `0x001684D4` | `0x0016A874` | 570 |
| 2 | `0x0016B0E4` | `0x0016CA64` | 408 |
| 3 | `0x0016D250` | `0x0016FAA0` | 645 |
| 4 | `0x00172DC8` | `0x00175438` | 615 |
| 5 | `0x00179338` | `0x0017AF58` | 450 |
| 6 | `0x0017B684` | `0x0017D844` | 540 |

The record shape is:

```c
struct CandidatePlaneRecord { // big-endian, 16 bytes
    float scalar;              // observed >= 0, approximately 0..145
    float nx;
    float ny;
    float nz;                  // (nx, ny, nz) has magnitude ~1
};
```

The leading hypothesis is a Hessian-normal plane/contact representation,
`n . p = scalar`, suitable for track walls or collision surfaces. This is a
strong format observation but only a candidate semantic assignment: no direct
consumer has yet been tied conclusively to these six absolute ranges. The six
tables matching six playable circuits is also strong but not proven by a
selection pointer.

There are smaller 7-36-record runs with the same format between the large
tables, including ROM `0x170BD4` (15), `0x1714B8` (17), and `0x176CB0` (13).
They could be finish-line planes, AI/camera anchors, or another collision
subsystem. They must not be discarded by a future extractor.

### 5.2 Collision worklist

Several code paths remain candidates rather than decoded contracts:

* `func_80060464` is car-pair collision/proximity processing. It is not car
  render LOD; the historical NOP at `0x8005FA3C` was neutral to the visible
  model-swap problem.
* `func_800170C4` and `func_8005AC84` are large physics/collision candidates
  that still need a complete field-level trace.
* `func_800626C8` is a better lead for track-position interpolation than for
  proven collision response; it uses an `0xE0`-stride context and secondary
  index tables, but no confirmed plane-table reader has been established.
* `func_80037BC0` normalizes a vehicle vector; it is not itself proof of a
  track-surface collision routine.

The next collision breakthrough should be a data watchpoint on one of the
candidate plane-table ranges while driving into a wall or leaving the road.
Until that happens, treat the six tables as “preserve and investigate,” not as
an established public mod format.

## 6. ROM asset map and descriptor clues

The race overlay code ends around ROM `0x00088CE4`; the main loaded asset
region continues to approximately `0x003CCBB0`. A static scan found no Yaz0,
MIO0, or Yay0 magic in the relevant region, so the track material appears to
be raw display-list/texture/structured data rather than one obvious compressed
track archive.

Known pointer-table candidates include:

| ROM offset | Entries | Current interpretation |
|---:|---:|---|
| `0x149610` | 34 | Controller Pak error strings |
| `0x149710` | 109 | menu vocabulary |
| `0x14AA84` | 31 | recursive/menu subtable candidate |
| `0x1683A8` | 15 | texture/model descriptor candidate |
| `0x16AFF4` | 12 | subtable candidate |
| `0x16D124` | 15 | texture/model descriptor candidate |
| `0x172C9C` | 15 | texture/model descriptor candidate |
| `0x17920C` | 15 | texture/model descriptor candidate |
| `0x17B558` | 15 | texture/model descriptor candidate |
| `0x17EEEC` | 32 | RGBA5551 palette pointer candidate |

The five known 15-entry tables are spaced through the same region as the
track-related data. An older note describes “six 15-entry track tables,” but
the currently enumerated scan has five 15-entry tables plus a 12-entry
subtable. This discrepancy must be resolved before using them as a circuit
index. Their pointer targets look texture/model-like and have not been proven
to be geometry descriptors.

The practical extraction rule is to follow pointer targets after the active
track setup, then classify each target by display-list commands, vertex data,
texture image loads, and palette loads. Do not infer a track solely from
proximity in the ROM: the six plane-like tables and descriptor tables are
interleaved with other race assets.

## 7. Car models and other visibility that affects track auditing

The same scene builder also selects car model detail. The vehicle-state region
starts around `0x800B69A8`; the scene-builder render pointer is loaded from
`0x800B69B0 + i*0x10C` and the associated position pointer is at a nearby
field. It computes a scaled camera distance, truncates the result to a
halfword at `0x80098720`, and selects from an eight-entry model ladder. The
primary ladder is around `0x8000C17C..0x8000C1DC`; a second overlay
pass repeats the decision around `0x8000E210..0x8000E2C4`. A far path is gated
near `0x8000C2C4`.

The port's `no_lod` car-detail hook zeroes the distance halfword at
`0x8000C108`, forcing the closest model branch. This matters when validating
a new track: otherwise a distant car or scenery object can be mistaken for a
track asset failure. It also illustrates that “no LOD” is several independent
mechanisms, not one global switch.

## 8. Proposed modding data contract

A first external format should describe a stock-compatible closed-loop track
in logical units, then have a loader/compiler produce the original runtime
arrays. A useful minimum schema is:

```text
track
  id, display_name, authored_radius[5]
  segments[]
    road_dl / wall_dl / scenery_dl or native asset references
    auxiliary cull points
  visibility_rows[segment][10]
  waypoints[]
    plane_a, plane_b, progress_or_extent, reserved fields
  segment_links[]
    forward/backward boundaries and wrap fields
  ai_lanes[] / generated D_801675D0-style links
  surface_data
    required eventually; opaque preserve-and-investigate data until a consumer
    is proven (not safe to copy between tracks as a final solution)
  materials, textures, palettes, models
  setup_descriptors
    context/pointer-table data required by the loader
```

For the first prototype, constrain the schema to:

* one closed loop;
* a fixed maximum segment count compatible with the existing arrays;
* one waypoint per segment (or a documented ratio);
* no branch/shortcut selection;
* existing display-list/material conventions; and
* a render-only replacement can retain the original circuit's collision as a
  temporary scaffold, but must be labeled non-playable for physics purposes;
  do not present copied collision data as a valid new-track surface.

This can be implemented as a PC-side importer that packs/patches the runtime
arrays, without immediately changing the MIPS scene builder. A later format can
replace native display lists with host-renderer meshes, but it should preserve
the same logical segment and waypoint identifiers for debugging.

Important binary requirements are big-endian word/float encoding, 16-byte
alignment for waypoint and plane-like records, valid runtime pointers after
loading, and explicit bounds for every table. Adding more data than the
original ROM loaded requires a new loader/swizzle path; the current graphics
configuration only changes behavior and does not stream arbitrary external
track assets.

## 9. Geometry-completion workflow

When a scene has a hole that was not visible in the original game:

1. Capture the circuit with stock settings, then with no-LOD radius scaling,
   full-track walk, FOV-cone widening, and car-detail forcing enabled.
2. Compare the segment IDs drawn, not just screenshots. If the ID is absent,
   inspect PVS rows, the header range, and the null-record sentinel.
3. If the ID is present, inspect the 64-byte record's three display-list
   pointers and walk each display list for vertices, textures, and nested DLs.
4. Check the auxiliary test-point record and the authored radius/cone. A valid
   segment can still fail a per-point cull.
5. Follow the active context's `+0x04/+0x08/+0x0C/+0x10` pointers and verify
   that the waypoint/progress data covers the same logical loop.
6. Trace `D_80121750` and the generated `D_801675D0` links for wrap, finish,
   and lane consistency.
7. Only then investigate the plane-like collision tables and physics readers.

This ordering separates a rendering omission from a missing asset, a data
pointer problem, and a physics problem. It also avoids “fixing” an apparent
hole by making every segment visible when the real problem is that the segment
has no road display list.

## 10. Validation checklist for a new track

Before calling a track playable, an extractor/loader should validate:

* context pointers resolve into the intended loaded RDRAM ranges;
* segment records are 64-byte aligned and every non-sentinel record has the
  expected road/wall/scenery pointer policy;
* PVS base/end are aligned, row count is `(end-base)/20`, and every row has
  ten signed slots with holes represented explicitly;
* every referenced segment index is in range and the null-record sentinel is
  not treated as a playable segment;
* auxiliary test-point records exist for every segment the fine cull can visit;
* waypoint records have matching indices and valid plane coordinates;
* the `+0x08` progress/extent scalar is populated with a known-good convention;
* forward/backward links wrap exactly once around the loop;
* generated lane/track links contain valid previous/next indices;
* AI state assumptions remain within the fixed 16-slot/0x84-byte contract;
* authored draw radii and per-circuit port settings are present;
* texture/model/palette pointers land in loaded data and nested display lists
  terminate safely; and
* collision/surface data is present, even if initially copied from a known
  track, and does not overrun the runtime arena.

For visual regression, record camera segment, drawn segment IDs, display-list
triangle counts, and car model IDs. Screenshots alone cannot distinguish a PVS
failure from a FOV-cone failure.

## 11. Highest-value unresolved investigations

1. Put a read watchpoint on each of the six plane-like ROM ranges after track
   setup and reproduce a wall/road departure. Identify the first reader and
   its record-index source.
2. Trace all writes/reads of the active context's `+0x0C` table and the
   `0x800BF1C0` test-point array to recover the fine-cull record layout.
3. Cross-reference the six descriptor-table candidates from circuit selection;
   resolve the five-versus-six 15-entry-table discrepancy.
4. Decode the remaining 64-byte segment-record fields and determine whether
   any are segment-local transforms, bounding points, or lighting/scenery
   selectors.
5. Identify the exact semantic of waypoint `+0x08` by correlating its value
   with segment length, race progress, and finish-line transitions.
6. Trace `D_80121750` and `D_801675D0` from initialization into minimap,
   ranking, lap, and AI code, then generate both from one logical loop model.
7. Measure the maximum safe segment count and replace the 21-slot drawn-list
   limitation with a dynamically sized host-side structure in the port.
8. Build a small known-good “track dump” containing all pointers, table sizes,
   segment IDs, waypoint records, links, and display-list references before
   attempting novel geometry.

## Sources and reproducibility

Primary local sources used for this audit:

* `Automobili Lamborghini (USA).z64` in the recomp worktree.
* `docs/no_lod_audit.md`, which records the no-LOD scene-builder, PVS, scenery,
  cone, draw-distance, and car-detail experiments.
* `docs/TRACK_INDEX.md` and `src/lambo_no_lod.cpp` in this worktree.
* `tools/pvs_distance_audit.py` in this worktree, which replays the observed
  segment-anchor distance calculation from a captured RDRAM image.
* `lamborghini.us.toml`, including the live hook addresses and comments.
* The sibling companion decomp checkout
  `automobililamborghini-decomp/asm/race_full_functions/`, especially
  `func_8000A6C0.s`, `func_80015044.s`, `func_800291CC.s`,
  `func_80029628.s`, `func_8002AE94.s`, `func_8002D37C.s`, and
  `func_80030D98.s`. This checkout is external to the recomp repository; the
  filenames are given so the audit can be reproduced in that companion tree.
* Companion notes `automobililamborghini-decomp/docs/notes/race_track_data.md`
  and `automobililamborghini-decomp/docs/notes/asset_format_findings_2026-05-22.md`.
* Companion extractor `automobililamborghini-decomp/tools/extract_track_data.py`;
  its `--verify` scan reproduces the six large 16-byte-record boundaries listed
  above.

The next document revision should replace candidate labels with confirmed
field names as runtime watchpoints identify consumers. Until then, preserve
unknown fields and treat the tables above as a dependency map, not as a final
reverse-engineered file format.
