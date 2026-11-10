# Rain rendering

Status: experimental raster effects for the USA port and JP weather package.
This is original rain artwork and geometry, not a reproduction of JP graphics.

## Player settings

In the JP weather mod, choose Wet and select **Retro** or **Modern** under
Rain style. Retro uses chunky pixel streaks, small square camera drops and flat
puddle highlights. Modern draws thin, slanted streaks that fade from head to
tail in three depth layers. They follow your car: as speed rises they stream
out of the middle of the view and stretch, and steering sweeps them sideways.
Modern lens drops are round, with a dark rim, a bright
glint, and trails when they run. Its puddles have soft edges and animated
ripples. Modern is drawn at the output resolution, not the N64's. Both styles
replace the sky with storm clouds and hide the sun flare. Choose Off to keep
the weather mod's fog and AI without rain art.
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

Rain checks the registered visual style and intensity before querying RT64.
Dry, Off and zero-intensity hooks make no renderer configuration query.
`lambo_rain_frame`, called at both USA graphics-builder entry points, resets
one guest-thread aspect snapshot. The first sky, rain or HUD query fills both
sky and HUD aspects under one RT64 configuration lock; later queries reuse it
until the next task build. Camera rain reuses its settings query's aspect.
Swap-chain size, aspect mode/target and HUD ratio changes are observed on the
next build, including builds while paused. The headless fallback remains a
zero sky aspect and a 4:3 HUD aspect.

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
| Vehicle pool 0x80910000, stride 0x10C | read only; +0 s16 flag 8 marks a populated car, +0x0E s16 channel, +0x90 s32 speed in km/h, +0xB2 s16 steering demand |

The existing [sky producer](sky-panorama.md) supplies clouds through immutable
per-style, per-circuit RGBA16 tiles. Their periodic noise wraps at the panorama
boundary, and the circuit's mirror flags are accounted for. Original source
texture hashes and assets are not changed. RGBA16 holds about nine steps of
the cloud grey range. Every channel takes the same step, rounded to the
nearest, so the clouds keep one blue-grey tint. Truncating each channel at its
own offset once painted green and blue bands across the sky. Soft grey steps
remain. An ordered dither removed them in game but read as a hatch and showed
each tile's clamped edge.

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
by subsequent views. Each record owns one road buffer per slot, sized to its
mesh ([Road buffers](#road-buffers)). Hooks record insertion points without advancing the
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
settings. Camera droplets do not shape geometry. The entry keeps a host copy of
the encoded vertex block, the Modern ripple frames and the segment's shelter
result. It also records where the segment's guest road buffer is in each task
slot ([Road buffers](#road-buffers)). Decoded triangles and roof planes live in
build scratch storage, not in the entry. The key pointers are compared, never
followed, so a stale entry cannot read reloaded track memory.

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
paused frame writes no road geometry. A rebuild or an invalidation rewrites
the whole buffer. All writes still follow the task-reuse fence of the slot
that owns the buffer. Root copies and camera lists reuse their command storage,
so steady race frames do not allocate.

### Road buffers

A buffer belongs to a segment record and a task slot. It lives in the record's
cache entry, so the order in which records appear in a frame does not matter.
A changed order moves and rewrites nothing.

The size is exact. The vertex block comes first (16 bytes a vertex). The
sublist follows it directly: the render-state header, four commands for each
face, and the footer (8 bytes a command). Nothing is reserved after the footer.
A mesh at the 12,000-vertex limit needs 320,128 bytes. A typical mesh needs a
small fraction of that. The limit itself is unchanged, and so is the output.

Lifecycle:

- The first emission into a slot allocates the buffer. A slot holds at most
  200 road buffers. A record that would exceed that draws no rain in that slot.
- A rebuild that produces a larger mesh allocates a new block and then frees
  the old one. A smaller mesh reuses the block it has. Both happen after
  `lambo_rain_frame` for that slot, so the slot's last task has finished.
- A failed allocation leaves that record without rain geometry for the frame.
  The native drawing, the other records and the camera rain are untouched.
- Clearing the cache (see the invalidation rule) hands each buffer to its slot.
  The slot frees them at its next `lambo_rain_frame`. RT64 may still be drawing
  the slot's previous task until then, so freeing earlier would be unsafe.
  Leaving the race therefore returns all road buffers to the heap.
- A record that scrolls out of view keeps its buffer until the next clear, so
  a lap does not reallocate. The 200-buffer cap bounds this. Inferred: a track
  has at most 200 records, because the camera segment check rejects indexes of
  200 or more.

The camera lists, root copy and sky tiles are fixed-size and allocated once.
One camera buffer per view and task slot (48 KiB) serves either style, so a
style change between races never meets a smaller block.

Decode, build, full-buffer and ripple-write counters appear in the `rain`
publish log with `--verbose` or `--lambo-debug`, with the live and peak guest
heap held by road buffers (`road_bytes`, `peak_road_bytes`), by all rain blocks
(`heap_bytes`, `peak_heap_bytes`), and the live road buffers in each slot
(`road_buffers=slot0/slot1`). Counts are requested bytes. The heap itself may
round a block up. `view1_motion` is the eased speed (m/s) and turn (rad/s)
that view one's streaks follow ([Streaks follow the car](#streaks-follow-the-car)).
The first four publications and every 1800th one are logged.

## Camera rain

Camera rain is drawn over each view after its scene, from the camera
insertion seam in the table above. A covered camera segment hides it.

RT64 draws rectangles on the native 320x240 grid. `convertViewportRect` rounds
each rectangle edge up to a whole native pixel before it scales the rectangle
to the output. A rectangle narrower than a native pixel either disappears or
becomes a full native pixel, so it looks the same blocky size at every output
resolution. Modern camera rain was once rectangles 0.6 native pixels wide, so
in game it looked like Retro. Retro still uses rectangles: the native grid is
its intended look.

Modern camera rain is triangles, which RT64 rasterizes at output resolution.
Each view's list does the following:

- Saves the other-mode, combiner, primitive-colour, scissor, geometry-mode,
  viewport and projection stacks, and pushes the modelview matrix. It restores
  all of them before it ends.
- Sets its own viewport for the view: full screen, a 2P half, or a 3P/4P
  quadrant tagged `G_EX_ORIGIN_WIDE` like the game's quadrant scenes.
- Sets an orthographic projection of exactly 1/2048 (vertex units are 2048
  per half view), an identity modelview, and no depth, fog or lighting. The
  combiner is shade colour and shade alpha, with translucent blending.
- Loads vertices in batches of up to 63 into RT64's expanded slots 32..94.
  This preserves the native F3DEX bank 0..31. A shape has at most 32
  vertices, so it never spans two loads.
- Leaves the projection aspect mode alone. RT64 widens an orthographic view
  only in the default automatic mode, which the game uses.

RT64 widens an orthographic full-width or quadrant view to the output aspect,
so the art is laid out across the widened view without stretching.
`lambo::rain::screen_rain` builds the shapes, and the runtime encodes them.
Streak count follows intensity and view area up to 16:9. Wider outputs keep
the 16:9 count, so the worst case fits the buffer and the rain is sparser.
Each view draws a fixed subset of its drops to get that count, and at most
15% more than expected, so variance cannot overflow the buffer.
Lens drops follow the Camera droplets setting. No texture is sampled.

### Streaks follow the car

Modern streaks are drops in a volume in front of the camera, in metres: far,
middle and near slabs at 10-16, 5.5-9 and 3.2-5.2. They are projected with a
stylized vertical field of view of about 62 degrees, not the game's camera.
Each slab is a box that repeats sideways, so any view width is covered. At
rest, drops fall at 9 m/s with a light wind and reproduce the old flat layers:
about 150, 250 and 400 native px/s, and streaks of 8-27 px at 240 lines.

A streak runs from the drop to where it was 0.06 s earlier, relative to the
moving camera. Driving forward brings drops closer, so they stream out of the
view centre and stretch. Turning sweeps them against the turn. A drop that
passes the near edge of its slab reappears at the far edge. Both edges fade
while the car moves, which hides that jump. The smear is capped at 35% of the
view height.

Each view follows the populated car on controller channel view + 1 (table
above). Speed is converted from km/h, and steering demand of about +-35 maps
to a turn of up to 0.6 rad/s, scaled down below 15 m/s. The view eases toward
that motion over about a quarter second, so a warp or crash does not snap the
rain. Without a matching car the rain stays at rest. Pause stops the streaks,
and leaving the race resets every view.

Verified in game (RT64/D3D12, circuit 1, Normal intensity): at 104 mph the
streaks fan out from the view centre, and holding left or right logged a
negative or positive turn. The logged view-one speed matched the speedometer.
A four-player championship race (warp mode 3) had cars on channels 1-4 and
drew rain in every quadrant, moving with each quadrant's own car.

### Clock precision

Streak positions are offsets of each slab's box, not time times speed. Every
offset wraps at its box size, which leaves the drawing unchanged, so the state
stays small for a race of any length. The time-only animation (Modern ripples
and lens drops, Retro drops) uses the race time wrapped at 10 s. Ripples
complete seven cycles in that period and lens drops one. Retro streaks share
no period with it and use the unwrapped race time in double precision. The
old float clock reached about 0.1 native px of error after an hour, which
sub-pixel streaks could show as jitter.

## Sky tiles and aspect sampling

Each style/circuit keeps its existing 64 KiB guest allocation. A 16-bit mask
tracks the two rows of eight tiles. The producer fills only the requested
4 KiB tile, with the same mirroring, vertical order, noise and quantization as
full generation. It sets the bit after the tile is complete. Published tiles
are immutable for the process lifetime, including across track changes and
savestate loads; texture DMA can outlive a CPU frame. Allocation failure keeps
the native tile. No worker touches guest RAM.

Dry and visual-Off rain hooks return before querying the renderer. The native
task-start hook resets the producer's aspect sample. Sky, rain and HUD helpers
then share one coherent snapshot on the game thread, taking RT64's configuration
mutex at most once per task. The sample reads the current swap-chain dimensions,
aspect mode/target and HUD ratio policy. Resizes and changes to those settings
appear on the next task; a task never mixes old and new aspect values. There
is no separate list of configuration setters that can miss an invalidation.

The producer test compares every generated pixel with full generation for both
styles, all six circuits and all tiles. It checks untouched tiles, repeated
requests and the Dry/Wet aspect query bounds. This is host-side output evidence,
not a recorded Wet game replay or visual verification.

## Limits and validation

Puddle highlights and lens highlights are stylized raster art. They do not
reflect cars or refract the framebuffer. Rain does not splash on the road or
the cars. Roof coverage uses horizontal triangles
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
It replays each camera list the way RT64 draws it: Modern must draw no
rectangles. Its triangles must include sub-pixel streaks with alpha gradients.
They must use only vertices from their own load in slots 32..94, keep every
stack balanced, and stay in their view for 1P, 2P, 4P and Heavy rain at 32:9.
Quadrant views must use the wide origin and a scissor on their half of the
output. Every generated shape must fit one vertex load.
Retro must still draw rectangles. The producer test also checks that road
buffers match the mesh size exactly, that a full 12,000-vertex
mesh fits with guard bytes intact, and that growth, failed allocation, the
200-buffer cap and leaving the race keep the guest heap counters balanced.
These are synthetic records, not a replay of game frames.

The geometry test also checks the moving streaks: they slant with the wind at
rest, point out of the view centre and at least double in length at speed,
and tilt against a turn. An hour of frames moves the volume as far per frame
as the first seconds do. It checks the streak count for 1P, 2P, 32:9 and
intensity, and that every sky texel keeps one tint and rounds to the nearest
step. The producer test checks that each view follows its own channel and
skips free car slots, and that pause and leaving the race hold or reset it.

For three- and four-player Wet captures, use warp mode 3 (championship).
Single race (mode 2) forces those player counts Dry, as on the JP cartridge.
Mode 0 is a time trial: it builds only player one's car, so the other
quadrants have nothing to follow and show only sky. That was the cause of the
sky-only quadrants seen while fixing #409; real races are unaffected.

The opt-in `lambo_rain_benchmark` measures the isolated functions and the
producers for 25 synthetic segments; [Testing](testing.md) has the commands and
[Performance](performance.md) the recorded results.
