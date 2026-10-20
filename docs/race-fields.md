# Configurable race fields

Status: implemented for the supported USA ROM; validation recorded below.

## Player behavior

**Enhancements > Cars per race** selects 2-16 total cars, including the player.
The default is six. A new single-player race uses the setting. Arcade sequences
and championships retain their starting field size between rounds. Time trials,
demonstrations and multiplayer retain their native counts.

Six preserves the native opponent picker. Other counts select from all eight
car types, including normally locked cars, and use additional colour variants
when a type repeats. The player's selection and saved unlocks are unchanged.
Championship entrants keep their identities between rounds. Changing the option
while racing applies to the next new race or championship.

The player retains the native rear-start rule. Fields above eight fit into the
four authored rows by interpolating positions along each lane. Reverse grids
use their own authored coordinates; the native constructor adds 180 degrees to
the stored headings. Championship points keep the native first-six awards;
places 7-16 earn zero. Results and standings above eight entrants use two columns
with CPU entrant numbers and a seven-character player-name display. The full
saved name is retained. Race and finish positions support two digits.

## Investigation and plan

A count-only patch is unsafe. The roster picker rejects repeated categories and
cannot terminate above eight. Several adjacent arrays are smaller than sixteen;
IDs are one-based in some tables. Sixteen cars also exceed three allocation
pools and the native AI dispatch channel range.

The implementation followed these steps:

1. Trace participant readers, writers, aliases, allocation pools and fixed bounds.
2. Relocate undersized tables together and update host consumers and snapshots.
3. Latch counts and choose bounded, distinct model/colour descriptors.
4. Fit grids within authored road coordinates and preserve native start ordering.
5. Extend AI dispatch, ranking text and championship presentation.
6. Add persistent configuration and regression checks against generated code.

The USA ROM SHA-256 is
`cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c`.
All addresses below are runtime guest addresses. Hooks run on the game thread.
Halfwords are signed unless stated otherwise. MEM_* handles word-swizzled RAM;
raw copies preserve that layout.

| Structure | Native address / constraint | Relocated storage |
| --- | --- | --- |
| Field size | `0x800CE7AC`, s16 | Same address; set after native defaults |
| Model roster | `0x80098228`, eight halfwords | `0x80901000`, sixteen descriptors |
| Racer-to-body map | `0x800CE818`, twelve halfwords | `0x80901100`, seventeen entries |
| Championship points / ranks | `0x800CE740` / `0x800CE758` | `0x80901200` / `0x80901300`, seventeen entries each |
| AI state | `0x800A5EE8`, sixteen records, stride `0x84` | `0x80902000`, seventeen records |
| Camera arrays | `0x800A2D10` through `0x800A2F10`, mixed capacities | `0x80900000` through `0x80900E00`, separate 0x100-byte slots |
| Shared body pool | `0x800B69A8`, thirty records, stride `0x10C` | `0x80910000`, sixty-four records |
| Draw-node pool | `0x800B8910`, sixty records, stride `0x40` | `0x80918000`, 128 records |
| Per-car asset free list / vertices | `0x800B9818` / `0x800B9878`, twelve entries | `0x8091C000` / `0x80920000`, thirty-two entries |

Physics contexts at `0x800CE830` already hold sixteen zero-based records of
stride `0xE0`. The collision working array has sixteen entries. Camera scalar
scratch between the native arrays is deliberately not relocated. Session metadata
lives at `0x80901400`; the temporary track/grid copy lives at `0x80903000`.
The extension ends at `0x80924000`, below the mod heap at `0x81000000` and above
the runtime PI handles at `0x80800000`.

The generator uses a literal-base instruction scan in
[scripts/race_field_patches.py](../scripts/race_field_patches.py), gated by the
complete ROM hash. It replaces direct references with equivalent relocated
MEM_* operations; accesses through materialized pointers remain native. The
reference-inventory fingerprint and generated TOML parity are tested. This is
ROM-specific address analysis, not a general control-flow-aware relocator.
Changes to the ROM, function boundaries or relocation tables require renewed
alias review and gameplay verification. Generated C is never edited.

The AI and racer-to-body relocation ranges include their native arrays, not
only record zero. The minimap starts its zero-based loop at the second record
(`0x800A5F7A` route point, `0x800CE81A` body). Leaving those aliases behind
overlapped all dots and drew incorrect player labels. The minimap now reads
the relocated records and repeats its eight authored colours for entrants 9-16.

The finish animation and settled finish screen have separate rank-string
builders; both format decimal ranks. The large font originally draws only
digits 1-6. Its measurement and emission now accept 0-9. Missing digits 0/7/8/9
reuse the resident 16x16 HUD font with a gold tint in 24x24 RGBA32 scratch tiles
at `0x80904000..0x80909A00`. The game thread fills each tile before emitting its
texture command. These deterministic tiles remain valid across queued frames
and are included in the existing snapshot region. Authored large digits 1-6
are unchanged; fallback digits have a different texture style.

Each car consumes three body objects and five draw nodes. Tests first exposed
body exhaustion, then a twelve-entry asset pool overwriting its neighbour.
A later snapshot found cars 9-16 stationary: channels 13/14 dispatch special
objects and channels above 14 have no native switch entry. Hooks now identify
physical cars by flag 8 and route those channels through the existing AI path,
preserving special non-car dispatch. Audio/collision channel gates use the same
physical-car test.

## Snapshots and renderer boundary

Relocation is unconditional, including default-six races, so all host readers
use the same layout. Vehicle inputs, finish gating, interpolation identities,
shadow snapshots and diagnostics follow the relocated addresses.

Developer save-states use LMBOSTAT v2: low 8 MiB plus the 0x24000-byte extension.
Old v1 snapshots are rejected before changing live RAM because their embedded
pointers refer to the old pools. Recreate them from fresh races. Normal game
progress and Controller Pak formats do not change. RT producer snapshots use
schema 3 and include contiguous RAM through `0x924000`; the offline inspector
also accepts historical schema 2 captures.

## Verification

See [testing](testing.md#configurable-race-fields) for reproducible commands.
The host fixture covers all counts, all 24 player selections, unique rosters,
mode exclusions, championship latching, compact grid bounds and decimal text.
It executes the generated championship initializer for every count across six
rounds, checking accumulated points and unique ranks after each round.

`tools/check_race_field.py` boots the real game and checks every entrant's body,
speed and rank from a settled snapshot. Local captures contain ROM-derived data
and remain under ignored `artifacts/` directories. Rendered checks use RT64
presentation readback, not the headless renderer's simplified image.

Validation on Windows x64 with MinGW GCC 15.2 has confirmed circuit-1 counts
2, 6, 8, 9 and 16, and 16-car fields on all six circuits in both directions. Time trial retained
one car and two-player racing retained four with the setting at 16. RT64 captures show
clear starting rows, additional cars/colours and readable 16/16 and 14/16 HUD
positions. The native results-to-standings-to-next-race flow retained sixteen entrants,
their roster and accumulated points when the next-session setting was changed
to two. Controlled finishing positions were injected into a settled snapshot;
this is not a natural race-finish test. Direct D3D12 presentation readbacks show
all sixteen rows inside the results and standings panels, including ranks 10-16,
three-digit cumulative points and a long player name. The results display and
championship accumulator both bound the native six-place points-table lookup.

Desktop window screenshots were stale during this validation despite advancing
menu tasks; direct GPU readback resolved that misleading evidence. A temporary
observer captured menu presentations as well as race tasks and was removed
after inspection. No VI, scheduler or renderer production change was required.

The independent effect pools recycle their own entries and are selected by
human player channels, so they do not need expansion for additional AI cars.
Settled snapshots across the exercised fields kept the native display-list
cursor within its 0x7800-byte arena, with at least 11,992 bytes remaining. This
is sampled headroom, not a guarantee for every possible effects-heavy frame.

No Android device performance claim is made. Larger fields increase simulation
and rendering work; field-size selection does not change difficulty tuning.

Follow-up HUD regression checks execute the generated minimap and large-font
emitters. Separated route points yield one distinct dot per entrant for counts
2/6/10/16 and exactly one player label. Every finish rank 1-16 emits the expected
number of digit rectangles. A controlled tenth-place finish snapshot rendered
both decimal digits and the ordinal suffix in RT64 D3D12 presentation readback.
This supplements the earlier results-table checks, which did not exercise the
settled finish overlay.
The 16-car race capture also shows separate coloured map markers and one P1
label. Nearby cars can still overlap at the native map's pixel resolution.
