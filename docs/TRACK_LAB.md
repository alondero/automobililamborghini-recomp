# Track Lab

Track Lab is the first safe track-modding workflow for this port. It extracts a
loaded stock circuit into readable JSON, visualises the decoded segment anchors
and assumed AI waypoints, edits the circuit's authored visibility rows, and
compiles those edits into a guarded `.altrk` package that the port can apply.

This is intentionally a **stock-circuit correction editor**, not yet an
arbitrary playable-track editor. Geometry, collision, waypoint data, segment
links, minimap data, and setup descriptors are inspect-only or unsupported.
The reverse-engineering basis and remaining work are recorded in
[Track modding research](./TRACK_MODDING_RESEARCH.md).

## What v0 can do

| Data | Support | Reason |
| --- | --- | --- |
| Ten-slot visibility/PVS rows | Edit and inject | The size, indexing, and consumer are statically confirmed; all-six live bounds and hook application are port-confirmed. |
| Segment display-list pointers | Inspect | Useful for diagnosing a missing road/wall/scenery layer; external assets are not loaded. |
| Segment cull anchors | Inspect and map | The anchor calculation is confirmed, but the surrounding record is not fully decoded. |
| Waypoint-like records | Inspect and map | Coordinates and 16-byte stride are confirmed; table extent and other fields remain uncertain. |
| Geometry and materials | Unsupported | No safe allocation, relocation, and display-list import contract exists yet. |
| Collision/surfaces | Unsupported | Candidate plane-like data exists, but its live consumer and mapping are not proven. |
| Branches, shortcuts, new circuits | Unsupported | Stock AI and progression follow an ordered closed loop through separate metadata. |

A PVS row controls which existing segment records are candidates for rendering
from a camera segment. It does not add geometry, make a surface driveable, or
change the path followed by computer-controlled cars.

## End-to-end workflow

Track Lab needs a settled in-race RDRAM snapshot from this port and the supported
USA ROM. Do not load another `.altrk` while making the baseline capture. Wait a
few seconds after the race appears so the asset-loading thread has finished.

An interactive capture uses <kbd>F7</kbd>. The default output is
`lambo_savestate.lstate`; set `LAMBO_STATE_FILE` to choose another path. A
repeatable PowerShell capture can warp directly to circuit 1 and save after 300
in-race ticks:

```powershell
$env:LAMBO_WARP = '1'
$env:LAMBO_STATE_SAVE = "$env:TEMP\circuit-1.lstate"
$env:LAMBO_STATE_SAVE_DELAY = '300'
.\build\lamborghini_modern.exe
```

Extract the snapshot. Circuit numbers in the JSON are the game's zero-based
internal indices (`0` through `5`), while the menu and `LAMBO_WARP` use `1`
through `6`.

```powershell
New-Item -ItemType Directory -Force track-lab-work | Out-Null
python tools\track_lab.py extract "$env:TEMP\circuit-1.lstate" track-lab-work\circuit-1.json
python tools\track_lab.py validate track-lab-work\circuit-1.json
```

Open `tools/track_lab_web/index.html` in a browser and choose the JSON file. The
editor provides:

- a fitted spatial view using cull anchors or assumed AI waypoint coordinates;
- selection of a segment and its ten visibility slots;
- Shift-click insertion into the first hole;
- read-only decoded and raw records;
- row reset, undo/redo, document validation, and JSON download.

The browser never patches a ROM or running process. Download the edited JSON,
then use the Python validator as the authoritative format check:

```powershell
python tools\track_lab.py validate track-lab-work\circuit-1-edited.json
python tools\track_lab.py diff track-lab-work\circuit-1-edited.json
python tools\track_lab.py compile track-lab-work\circuit-1-edited.json track-lab-work\circuit-1.altrk
```

Launch the port with the compiled correction:

```powershell
$env:LAMBO_NO_LOD = '0'
.\build\lamborghini_modern.exe --track-patch track-lab-work\circuit-1.altrk
```

`LAMBO_TRACK_PATCH` is the equivalent environment setting. An explicit
`--track-patch` argument wins when both are present.

Set `LAMBO_NO_LOD=0` while judging PVS changes. The no-LOD enhancement can use
a synthesized all-segments walk, which intentionally makes most authored PVS
differences visually irrelevant. It does not overwrite the authored table, so
it is safe to turn back on after testing.

## Verified stock coverage

Fresh settled port captures on 2026-09-03 completed extraction, validation,
zero-edit compilation, and guarded post-load hook checks for every stock
circuit:

| Internal | Menu | PVS rows | Base FNV-1a64 | Hook result |
| ---: | ---: | ---: | --- | --- |
| 0 | 1 | 25 | `46c9dcf0e2ecd291` | already applied |
| 1 | 2 | 29 | `37cb277ebc479b88` | already applied |
| 2 | 3 | 30 | `004aa02c02db17de` | already applied |
| 3 | 4 | 75 | `e03944e6281e71df` | already applied |
| 4 | 5 | 55 | `6dbc155bc809e069` | already applied |
| 5 | 6 | 67 | `09c24da181ce6745` | already applied |

Menu Circuit 1 (internal circuit 0) also passed a nontrivial live correction:
row 0 slot 8 changed from the raw `-1` hole to segment 5, producing result hash
`9b10faba9efe8d94` in the saved RDRAM. A matching-package savestate restored
idempotently, while the same savestate was rejected with no package active.

These are recomp-port results, not an ares-reference comparison. The generated
snapshots and packages live under the git-ignored `track-lab-work/` directory
and contain no committed ROM data.

## Safety model

An `.altrk` package contains only sparse `(row, slot)` replacements. The native
loader validates the complete package before game startup, and the post-track-
load hook applies it transactionally only when all of these conditions hold:

1. the package targets the supported USA ROM hash and a circuit in `0..5`;
2. the active circuit matches;
3. all context pointers and the PVS span are valid inside the 8 MiB RDRAM;
4. the live row count matches and every row still has exactly ten slots;
5. the complete live PVS fingerprint matches the extracted baseline;
6. every edited cell contains its recorded old signed-halfword value; and
7. the prospective complete result matches the package's result fingerprint.

Any rejection is read-only. The transactional table inspection and write path
performs no file I/O or allocation, and it never changes table sizes or
pointers. The hook reports its result through the port's normal logger after
that operation. Packages are reapplied after a matching developer savestate is
restored. Savestates record the active package
identity and are rejected when loaded with a different package (or with no
package), preventing a snapshot from silently mixing track configurations.

Packages are corrections, not ROM patches: the original ROM and its assets are
unchanged. The loader also refuses a package made for another ROM revision.

## JSON contract

The editable source format is `al-track-document` version 1. Its important
fields are:

```json
{
  "format": "al-track-document",
  "version": 1,
  "target": {
    "game_id": "lamborghini.us",
    "rom_xxh3_64": "525201d7279f34e3",
    "circuit": 0
  },
  "capabilities": {
    "editable": ["visibility"],
    "inspect_only": ["segments", "anchors", "waypoints"],
    "unsupported": ["geometry", "collision", "new_track"]
  },
  "visibility": {
    "row_count": 25,
    "slots_per_row": 10,
    "base_fnv1a64": "...",
    "base_rows": [],
    "raw_base_rows": [],
    "rows": []
  }
}
```

Only `visibility.rows` is editable. Each entry is either a segment index in
`0..row_count-1` or `null` for a hole. `base_rows`, `raw_base_rows`, decoded
inspection fields, raw records, provenance, and capability declarations are
validated as immutable evidence. `raw_base_rows` retains unusual negative hole
values even though the editor displays every negative value as `null`.

The extractor accepts either exactly 8 MiB of raw N64Recomp word-swapped RDRAM
or the port's 32-byte `LMBOSTAT` v1 header followed by that payload. A savestate
captured while a Track Lab package is active is not accepted as a new baseline.

## Portable package layout

`.altrk` v1 is little-endian and independent of host pointer values. Its
64-byte header contains the magic `ALTRKPV1`, format/header/file sizes, target
ROM hash, PVS-only capability flag, circuit, ten-slot row shape, edit count,
payload size, complete base/result FNV-1a fingerprints, and zeroed reserved
bytes. Each sorted eight-byte edit stores a row, slot, expected old `s16`, and
replacement `s16` (`-1` means a hole).

The fingerprints are computed over the logical big-endian PVS halfword byte
stream. They protect the whole table; the per-edit old values provide a second,
local guard and make a package diff reviewable.

## Interpreting the map

The cull-anchor view is derived from the confirmed scene-builder calculation:

```text
anchor x = anchor[segment.anchor_index].x + segment.offset_x
anchor z = anchor[segment.anchor_index].z + segment.offset_z
```

The waypoint view reads one 16-byte record per PVS row beginning at the dual-use
PVS-end pointer. This is a useful inspection hypothesis, not a proven table
extent. The two floats are horizontal-plane coordinates used by AI/path code;
the signed `+0x08` value is only safely named `progress_or_extent`, and the
remaining fields are unknown. The UI fits each coordinate source independently,
so the two views must not be compared as though they share one map transform.

Stock opponent AI does not traverse PVS links. It advances through ordered
waypoints plus separate previous/next and wrap metadata, then derives a steering
direction from neighboring waypoint coordinates. A visibility correction can
fix pop-in or expose an existing nearby section; it cannot teach AI to follow a
new route.

## Next implementation milestones

The route from this correction editor to genuinely new playable tracks is:

1. identify the live collision/surface consumer with watchpoints and prove the
   six candidate plane-table mappings;
2. decode the remaining segment and waypoint fields plus exact waypoint extent;
3. recover and generate loop/wrap, lap/progress, race-position, and minimap data
   from one logical route model;
4. add a bounded host allocation/swizzle layer for relocated segment records,
   display lists, vertices, textures, and palettes;
5. raise or replace the stock 20-entry drawn-segment-list capacity safely; and
6. only then add geometry/collision authoring and call the result a new track.

Until collision is proven, copied stock collision under replacement visuals is
a render experiment, not a playable custom track.

Extraction, compilation, package loading, and hook guards are verified for all
six stock circuits in the recomp port. A correction write and matching/mismatched
savestate interaction are verified on menu Circuit 1 (internal circuit 0).
Require ares/port comparison fixtures before Track Lab leaves its experimental
status.
