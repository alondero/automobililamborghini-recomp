# Rain rendering

Status: experimental raster effects for the USA port and JP weather package.
This is original rain artwork and geometry, not a reproduction of JP graphics.

## Player settings

In the JP weather mod, choose Wet and select **Retro** or **Modern** under
Rain style. Retro uses chunky streaks, small camera drops and flat puddle
highlights. Modern adds finer streaks, rounded lens drops, soft puddle edges
and animated ripples. Both replace the sky with storm clouds and hide the
sun flare. Choose Off to keep the weather mod's fog and AI without rain art.
Rain intensity and Camera droplets are independent settings. All three apply
when the next race loads. Texture packs do not select the rain style.

Package 0.3.0 requires the `lambo_register_rain_v1` base export. An older port
rejects the package rather than partially loading its effects. No ray-tracing
hardware is required. The diagnostic software renderer retains its original
output; these effects require RT64.

Dry, Night, US default and races that the weather resolver forces Dry emit no
rain. The existing championship round selections and multiplayer restrictions
still determine whether a race is Wet. Attract playback emits no rain.

## Producer boundary

The package owns an aligned, native guest u32 in its mod allocation. It packs
style in bits 0..1 (0 Off, 1 Retro, 2 Modern), intensity percent in bits 8..14,
and camera droplets in bit 16. Reserved bits, invalid style or intensity above
100 fail closed. Zero unregisters the pointer. Invalid registration preserves
the previous pointer. Last successful registration wins. The game thread owns
registration, updates and reads; renderer workers never follow this pointer.
Runtime initialization clears it. Developer savestates omit the mod heap;
cross-weather restores remain unsupported, as with the existing AI bridge.

All native addresses below are USA, read with word-swapped `MEM_W`/`MEM_H`.
The hooks write task-owned heap geometry and a small wrapper in the completed
display-list root; track assets and the native cursor remain unchanged.

| Field / seam | Contract |
| --- | --- |
| 0x800A2BFC | u32 graphics-task pointer; slots 0x800BF240 / 0x800C6C90 |
| 0x800A39CC | u32 display-list cursor, aligned to eight bytes |
| 0x800CE6AC / 0x800CE6B4 | s16 game state / mode; race 8, attract mode 4 excluded |
| 0x800CE808 | s16 pause flag; stops the weather animation clock |
| 0x800CE6A4 / 0x800CE6A6 | s16 player count / one-based viewport index |
| 0x800BF1D0 / 0x800BF1CC | u32 segment-record base / s16 camera segment |
| Segment record +4/+8/+12 | u32 road / wall / scenery display-list pointers; cache key, never followed after a build |
| 0x800CE794 | s16 circuit index; a change starts a new cache epoch, read once per task |
| 0x8000CF2C | follows road pointer store at CF28; t2 is the segment record |
| 0x8000D7C0 | follows road pointer store at D7BC; t8 is the segment record |
| 0x80005114 / 0x800053F4 | viewport completion / one-player pre-HUD seam |

The existing [sky producer](sky-panorama.md) supplies clouds through immutable
per-style, per-circuit RGBA16 tiles. Their periodic noise wraps at the panorama
boundary, and the circuit's mirror flags are accounted for. Original source
texture hashes and assets are not changed.

Road decoding accepts bounded absolute/physical-address F3DEX lists with
native 16-byte vertices and triangle commands. Matrix changes, segmented
targets and modified vertices are rejected. No partial mesh is rendered after
failure. Flat road faces receive a dark transparent sheen and inset puddle
polygons. Modern ripples share that face's plane. Walls and steep faces do not
receive water. Puddles use depth-tested translucent decals without depth writes.
The expanded RT64 vertex cache uses slots 32..34, preserving native F3DEX's
0..31 bank. Geometry, combiner, primitive color and other-mode state are saved
and restored. Camera rectangles also save and restore scissor state.

Heap sublists are owned by one of the two native graphics-task slots. The
task-reuse fence protects them until RT64 consumes them, as for the panorama.
A road record is emitted at most once into each task's heap, then referenced
by subsequent views. At most 200 road buffers per slot are allocated, with a
fixed per-buffer limit. Buffers are indexed by the order in which records
first appear in a frame. A changed order rewrites the affected buffers in full.
[#398](https://github.com/alondero/automobililamborghini-recomp/issues/398)
tracks keying and sizing them by record.
Hooks record insertion points without advancing the
native cursor. After the complete native root has passed HUD/fog correction,
publication copies it into a bounded heap list and splices in the effects.
Only the first 40 bytes of the original root become a call/end wrapper. Its
size and the native builder cursor remain unchanged. Failed allocation,
invalid insertion order or oversized geometry leaves the original drawing intact.

## Cache lifecycle

Track geometry does not change during a race, so the game thread builds each
segment's rain geometry once. Only Modern ripples change from frame to frame.

A cache entry belongs to one segment record address. Its key is the record's
road, wall and scenery list pointers (+4, +8, +12) and the style and intensity
settings. Camera droplets do not shape geometry. The entry keeps host data
only: the encoded guest vertex block, Modern ripple frames and the segment's
shelter result. Decoded triangles and roof planes live in build scratch storage,
not in the entry. The key pointers are compared, never followed, so a stale
entry cannot read reloaded track memory.

Invalidation rule:

- A changed key field rebuilds that entry on its next use. A reused record
  address with new list pointers therefore never serves the old puddles.
- The whole cache is cleared on any frame outside a race (state other than 8),
  on a circuit change (0x800CE794), after the track loader (hook 0x80006094)
  and after a developer save-state load.
- More than 1024 entries clears the cache and rebuilds from the current frame.

A failed decode is cached like a success. Rain builds only in a race, and the
track loader hook runs after the stock loader has filled the lists.

A list rewritten in place under the same pointers is detected only by those
events. Inferred: no supported feature does that during a race. Track Lab
changes visibility rows only, and modding does not support replacing geometry.

Each road buffer records the build it holds. Static vertices and commands are
copied into it once per build. Later frames reuse them and only add the
insertion. A Modern frame rewrites the ripple vertices and the color word of
their faces when the animation clock has moved. Pause stops that clock, so a
paused frame writes no road geometry. A rebuild, an invalidation, or a buffer index
that now belongs to another record rewrites the whole buffer. All writes still
follow the task-reuse fence of the slot that owns the buffer. Root copies and
camera lists reuse their command storage, so steady race frames do not allocate.

Decode, build, full-buffer and ripple-write counters appear in the `rain`
publish log with `--verbose` or `--lambo-debug`. The first four publications
and every 1800th one are logged.

## Limits and validation

Puddle highlights and lens highlights are stylized raster art. They do not
reflect cars or refract the framebuffer. Roof coverage uses horizontal triangles
decoded from the same segment's wall/scenery lists. Camera rain is suppressed
for the whole covered segment, so transitions at tunnel portals are conservative.
This is not a complete scene-wide shelter or collision system. Unsupported
roof geometry cannot establish coverage.

Focused checks:

```text
ctest --test-dir build -R "lambo_rain|lambo_mod_gameplay|lambo_sky_panorama" --output-on-failure
python -m unittest discover -s tests -p "test_jp_weather*.py"
```

The geometry test covers byte order, invalid DMA, cyclic lists, uninitialized
vertices, floor selection, roof coverage, in-face placement and cloud wrapping.
It can also accept an ignored native-RAM capture as an optional argument.
The local circuit-1 capture decoded all 25 road lists (904 faces). That checks
the decoder, not visual correctness or all-circuit coverage. The producer test
checks unchanged native RAM during construction, invalid record alignment,
bounded publication, independent task storage, fog-enabled decals, balanced
render-state stacks and disabled effects.

`tests/rain_reference.h` freezes the uncached producer from `e053389`. The
geometry test compares puddle bytes, roof coverage, ripple rewrites and vertex
rounding with it. The producer test compares every emitted road sublist with
it, command words and vertex payload. It covers Retro and Modern, one and four
viewports, both task slots, pause, reordered emission and shelter. It also
covers each invalidation rule and the decode, build and allocation counts.
These are synthetic records, not a replay of game frames.

The opt-in `lambo_rain_benchmark` measures the isolated functions and the
producers for 25 synthetic segments; [Testing](testing.md) has the commands and
[Performance](performance.md) the recorded results.
