# Track Lab

Status: experimental visibility/elevation editor and fixed-capacity track bundles.
The generated Circuit 1 oval has passed a forward four-car AI lap test. Arbitrary
topology, extra menu slots and other regional ROMs are not supported.

Track Lab is the first safe track-modding workflow for this port. It extracts a
loaded stock circuit into readable JSON, records decoded segment anchors and
assumed AI waypoints for inspection, edits the circuit's authored visibility
rows, and compiles those edits into a guarded `.altrk` package that the port can
apply.

The original visibility format remains supported. A second bundle format now
replaces named buffers while retaining stock capacities and segment pointers.
The companion editor can change road elevation and generate a new oval with
matching collision, routes, grid and minimap. A bundle replaces its target
stock slot for that launch; it does not add a circuit to the menu.
The reverse-engineering basis and remaining work are recorded in
[Track modding research](./TRACK_MODDING_RESEARCH.md).

## Supported data

| Data | Support | Reason |
| --- | --- | --- |
| Ten-slot visibility/PVS rows | Edit and inject | The size, indexing, and consumer are statically confirmed; all-six live bounds and hook application are port-confirmed. |
| Segment display-list pointers | Inspect | Useful for diagnosing a missing road/wall/scenery layer; external assets are not loaded. |
| Segment cull anchors | Inspect and map | The anchor calculation is confirmed, but the surrounding record is not fully decoded. |
| Waypoint-like records | Inspect and map | Coordinates and 16-byte stride are confirmed; table extent and other fields remain uncertain. |
| Geometry and materials | Elevation and generated oval | Same-capacity vertex/display-list replacement; general relocation is unsupported. |
| Collision/surfaces | Shared vertices and generated oval triangles | Confirmed triangle/neighbor/owner records reference the rendered road vertices. |
| Branches, shortcuts, new circuits | Circuit 1 oval replacement | Preserves the stock main/pit range and event topology; arbitrary branches are unsupported. |

## Fixed-capacity bundles

The companion editor's snapshot workflow extracts road, wall and scenery
display lists into a geometric preview. Its local Python bridge imports this
repository's `track_lab.py` and `track_bundle.py`. Version 2 documents carry
`track_source` buffers and optional `editor_preview`; the preview is never
compiled. Use the same CLI to validate, diff or compile either document version.

`ALTRKBD1` packages use the existing 64-byte little-endian header with flag 2,
block count in the edit-count field, and zero global hash/reserved fields.
Each block has a 32-byte `<BBHIIIQQ` descriptor: kind, zero byte, index, size,
two zero words, baseline/result FNV-1a64 hashes; logical big-endian replacement
bytes follow. Kinds are visibility (0), shared vertices (1), collision (2),
waypoints (3), segments (4), road/wall/scenery lists (5/6/7), inline grid (8),
and 80x80 IA4 minimap (9). List indices select stock segment records; other
indices are zero. There are no package-supplied destination pointers.

The native loader resolves and bounds every buffer from the active context,
checks overlap, complete fingerprints and replacement references, then writes
transactionally. Segment pointers, route event/scalar fields and spawn range
IDs stay unchanged. Changed display lists use a restricted command set and
may load vertices only from the named vertex buffer. Mixed partial application
rejects; a complete matching result is idempotent. Invalid references, corrupt
payloads and late baseline mismatches have host tests proving no writes.

The oval generator builds 904 road/collision triangles, 25 render segments,
250 main plus 56 pit waypoints, a containing collision seed for each grid slot,
and a matching minimap. It preserves Circuit 1's event/range topology and
uses its existing assets as storage, without copying its road layout. Run
without `--track-patch` to load the original circuit.

Local November 2026 verification covered all six stock geometry previews;
every nonzero collision triangle matched a rendered road triangle. The actual
generated game height consumer passed 12,927 nondegenerate triangle elevation
checks. Browser-authored elevation and oval packages matched every imported
buffer after a fresh native load. Three AI opponents completed a full forward
oval lap in a four-car race and remained moving; reverse, multiplayer and
Championship custom-track behavior are not claimed verified. Actual RT64
presented frames were captured, including the generated minimap.

The companion repository's `verify_track_cycle.py` creates fresh run directories,
checks compiler bytes and post-load buffers, and optionally captures a native
frame with `--presented`. Snapshots and generated packages contain local game
assets and must remain uncommitted.

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

The core port keeps Track Lab headless: edit only `visibility.rows` with a
JSON-aware tool, preserving `base_rows` and `raw_base_rows` from the capture.
The Python validator accepts a full evidence-bearing document for inspection, or
a reduced patch document containing only `format`, `version`, `target`, and
`visibility`; read-only segment, anchor, and waypoint records are never needed
by the compiler. A companion visual editor can consume the same JSON contract
without becoming part of the native port.

After editing the JSON, use the Python validator as the authoritative format
check:

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
`9b10faba9efe8d94` in the saved RDRAM. A savestate restored with the active
correction idempotently, and the same snapshot remained loadable when no package
was active.

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
7. the package's sorted sparse edits are the only writes performed.

Any rejection is read-only. The transactional table inspection and write path
performs no file I/O or allocation, and it never changes table sizes or
pointers. The hook reports its result through the port's normal logger after
that operation. An active package is reapplied after any developer savestate is
restored. Savestates retain their general-purpose reserved header fields and do
not become coupled to a particular optional track package.

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
`0..row_count-1` or `null` for a hole. A full extracted document validates
`base_rows`, `raw_base_rows`, decoded inspection fields, raw records, provenance,
and capability declarations as immutable evidence. The patch compiler needs the
target and visibility fields only, so clients may omit inspection-only records.
`raw_base_rows` retains unusual negative hole values even though the editor
displays every negative value as `null`.

Companion editors may include an optional `editor_preview` rendering cache.
This is opaque presentation metadata, not extraction evidence. Validation and
compilation never treat it as writable game data or include it in `.altrk`.

The extractor accepts either exactly 8 MiB of raw N64Recomp word-swapped RDRAM
or the port's 32-byte `LMBOSTAT` v1/v2 header followed by that payload. Version 2
also carries the port's 0x24000-byte race-field extension. Header
reserved words remain opaque to Track Lab; the capture's table fingerprint is
the baseline guard.

## Portable package layout

`.altrk` v1 is little-endian and independent of host pointer values. Its
64-byte header contains the magic `ALTRKPV1`, format/header/file sizes, target
ROM hash, PVS-only capability flag, circuit, ten-slot row shape, edit count,
payload size, and complete base/result FNV-1a fingerprints. Each sorted
eight-byte edit stores a row, slot, expected old `s16`, and replacement `s16`
(`-1` means a hole).

The fingerprints are computed over the logical big-endian PVS halfword byte
stream. They protect the whole table; the per-edit old values provide a second,
local guard and make a package diff reviewable.

## Interpreting the map

The cull-anchor view is derived from the confirmed scene-builder calculation:

```text
anchor x = anchor[segment.anchor_index].x + segment.offset_x
anchor z = anchor[segment.anchor_index].z + segment.offset_z
```

The anchor is an N64 vertex: X is the signed halfword at `+0`, height at `+2`,
and Z at `+4`. The scene builder reads `+0` at runtime `0x8000D0A8` and `+4` at
`0x8000D0EC`. Older Track Lab exports incorrectly decoded height as Z; re-extract
those snapshots or migrate the derived coordinates using the preserved raw
records. Segment translation remains X/Z at segment `+0x10/+0x12`.
Cull anchors are test points, not road centerline points or segment links.

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

## Remaining implementation milestones

Collision, Circuit 1 route extent, grid seeds and minimap generation are now
confirmed for the bounded oval workflow above. General authoring still needs:

1. editable route topology, branches and event placement beyond the preserved
   stock range graph;
2. a bounded host allocation/swizzle layer for relocated segment records,
   display lists, vertices, textures and palettes;
3. a safely raised or replaced stock 20-entry drawn-segment-list capacity;
4. extra menu entries and a stable mod distribution format; and
5. reverse, multiplayer, Championship and reference-emulator validation.

Extraction, compilation, package loading, and hook guards are verified for all
six stock circuits in the recomp port. A correction write and matching/mismatched
savestate interaction are verified on menu Circuit 1 (internal circuit 0).
Require ares/port comparison fixtures before Track Lab leaves its experimental
status.
