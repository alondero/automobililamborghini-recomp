# Japanese weather investigation

Status: discovery for [issue 340](https://github.com/alondero/automobililamborghini-recomp/issues/340).
An [experimental package](../mods/jp_weather/README.md) now implements the
confirmed race-slot rules, fog and two AI table changes. The exact wet throttle
branch and full parity remain unfinished. The measurements below distinguish the JP
cartridge's behavior from the issue's proposed per-circuit and AI-vision model.
The supported port remains USA only.

## Inputs and method

Both cartridge identities are recorded in [Japanese cartridge](japanese-rom.md).
The Japanese SHA-256 is
`06c9c992014da5982e747117c690c1590a011c0c6756b0522df126366bf8b9a1`.
The main-image mapping is `ROM = VRAM - 0x7FFFF400`; the overlay mapping is
`ROM = VRAM - 0x800FF610`. These mappings are not interchangeable.

Static findings came from the verified ROMs, their checked-in symbol tables,
instruction inspection and freshly generated Japanese recompile output.
REA rejected the raw `.z64` format; these are repository-tool observations,
not REA decompilation results. Recompilation succeeded on Windows with the
existing N64Recomp executable. Generated output and cartridges remain ignored.

The live probe cold-boots each case in ares, attaches after boot, pauses the
guest and writes single-race menu selections. It requests game state 7, watches
the active weather write, then watches the transition to state 8 and captures
fog settings. This is an intrusive loader experiment: it does not navigate
menus or call the menu's audio-quiesce pair. It proves what the stock loader
does with those selections, not an ordinary playthrough, AI trajectories or
port-versus-reference parity. Each launch owns and terminates only its own
emulator process. Invalid debugger replies and unexpected stops fail the case.

## Recorded live results

On 2026-11-01, all 18 single-player combinations (six circuits times three
weather selections) reached state 8 with the requested active circuit and
weather. The [retained matrix](evidence/jp-weather-live.json) contains the
emulator hash, individual watchpoint stops, field measurements and capture
limitations. The [static report](evidence/jp-weather-static.json) separately
records cartridge identities, menu bindings and extracted AI tables.

| Circuit | Dry fog near/far | Dry fog RGB |
| --- | --- | --- |
| 1 | 995 / 1000 | 57, 48, 55 |
| 2 | 985 / 1000 | 5, 6, 12 |
| 3 | 995 / 1000 | 57, 48, 55 |
| 4 | 995 / 1000 | 57, 48, 55 |
| 5 | 975 / 1000 | 64, 64, 72 |
| 6 | 995 / 1000 | 57, 48, 55 |

Wet and night matched the constant overrides below on every circuit. These
are observations after explicitly selecting weather, not measurements of
automatic weather assignment. The matrix used the initial probe before review
added strict packet checksum/EOF checks and positive pause acknowledgement.
Its weather/state watchpoint stops and memory lengths were checked; the final
matrix was independently checked for all 18 unique cases and zero failures.
The retained metadata records that distinction rather than claiming the
hardened protocol was used for these captures.

Two subsequent [three-player captures](evidence/jp-weather-multiplayer.json)
used the hardened probe on circuit 1. Both wet and night selections resolved
to dry, with fog `980/1000`, RGB `(4,4,16)`. This corroborates the inspected
player-count branch for that circuit; other multiplayer circuits and four
players were not measured. No visual, audio or AI trajectory check was run.

## Weather belongs to race slots

The menu string table at `0x8023821C` labels values as `DRY`, `WET`, `NIGHT`.
Callbacks at `0x80201220` and `0x802011A4` increment the selected weather and
wrap at three. The following fields are big-endian guest halfwords, owned by
the guest menu/race thread:

| Field | JP address | Meaning |
| --- | --- | --- |
| Weather selections | `0x800C4F30` | Array indexed by race slot; 0 dry, 1 wet, 2 night |
| Circuit selections | `0x800C4EB0` | Separate array indexed by race slot |
| Championship slot | `0x800C4EAE` | Index into both arrays |
| Race mode | `0x800C4EA8` | Single race is 2 |
| Player count | `0x800C4E98` | Selects the single-race weather restriction |
| Active circuit | `0x800C4FFA` | Circuit chosen by the loader |
| Active weather | `0x800C503C` | Weather chosen by the loader |
| Game state | `0x800C4EA0` | 7 loads a race; 8 enters the race |

The loader `boot_pad_apply_calibration` starts at JP `0x800070F8`.
Single-race code copies weather slot zero at `0x80007360` when there are fewer
than three players; otherwise `0x80007368` forces dry. Championship code reads
the current race slot at `0x8000745C`, indexes the weather array, and writes
active weather at `0x80007474`. It independently selects the circuit.

The championship reset at `0x80201724` initializes six slots. The circuit order
is `[0, 4, 2, 3, 1, 5]` (zero-based), read from ROM `0x869D2` (after the
slot-zero sentinel at `0x869D0`). Its store at
`0x802017AC` clears each weather entry to dry. Adding a championship round
also clears that slot's weather. This inspected reset path gives every
circuit dry weather. It does not supply a fixed wet/night circuit assignment.

Consequently, a native array keyed solely by circuit would describe a new
preset, not the measured JP race-slot model. Further mode-specific paths must
be checked before claiming that no automatic weather selection exists anywhere
in the cartridge.

## Fog and rendering distances

The inspected loader branches assign these constants:

| Selected weather | Fog near | Fog far | Fog RGB |
| --- | --- | --- | --- |
| Wet | 930 | 999 | 64, 64, 70 |
| Night | 850 | 999 | 7, 7, 11 |

Dry uses the selected track/view record rather than those overrides. The fog
near/far values are projection fog parameters, not physical visibility meters.
Near and far are halfwords at `0x80099C18` and `0x80099C1A`. Red is a byte at
`0x80099C1C`; green and blue are bytes at `0x800A9C20` and `0x800A9C24`.
The split addresses are intentional: the loader uses different upper halves,
and the renderer at `0x80005BCC` onward reads the same locations.

Wet stores begin at `0x80008188`; night stores begin at `0x800081CC`.
Night is nearly black, not literal RGB zero. A separate perspective setup at
`0x80005744` selects dry/wet/night distances of `550/250/350`, writes float
`0x8009687C`, and passes it to the perspective builder at `0x800057EC`.
That call establishes a rendering use, not an AI sensing radius.

## AI changes found

The weather-dependent table at JP `0x8007DAF0` (ROM `0x7E6F0`) contains
big-endian floats indexed as `weather * 16 + difficulty * 4`:

| Weather | Difficulty 0 | 1 | 2 | 3 |
| --- | --- | --- | --- | --- |
| Dry | 290 | 290 | 240 | 240 |
| Wet | 220 | 220 | 180 | 180 |
| Night | 290 | 290 | 240 | 240 |

`func_8002D37C` reads it at JP `0x800365B8`, subtracts three times a working
float at `0x800C4F94`, and compares the result with vehicle field `+0x90`.
The working float's meaning and physical units are unresolved.
The vehicle field is speed: the JP code at `0x8002A0D4` onward calculates a
vector magnitude, scales it by 1.08, truncates it, and writes `+0x90` at
`0x8002A148`. This also agrees with the USA
[vehicle layout](../src/lambo_vehicle.h). The comparison feeds driving control;
calling this table AI vision distance would be unsupported.

The corresponding USA read is `0x80030738`, from table `0x80089444`
(ROM `0x8A044`), with values `[290, 240]`. USA has two native difficulty values;
JP's four indices cannot be copied directly into that table.

Wet also halves a difficulty-dependent countdown. JP table `0x8007DA78`
(ROM `0x7E678`) contains signed halfwords `[50, 35, 25, 15]`. Wet arithmetic
right-shifts these to `[25, 17, 12, 7]` at the stores ending at `0x800358BC`
and `0x80036718`. The destination is AI record `+0x30`, with base `0x800989D0`
and stride `0x84`. The function decrements that field by one when nonzero at
`0x80032434` through `0x80032468`. These are update counts; neither real-time
durations nor an AI sight distance has been measured. A wet branch at
`0x80035B6C` also changes a throttle-demand cap from 100 to 60.

The initial findings above were static. A later controlled JP wet/difficulty-0
run [observed a read watchpoint](evidence/jp-weather-ai-read.json) on speed-table cell `0x8007DB00`, whose live value
was `220`. Countdown initialization was not observed within the bounded run.
The reusable `tools/emu_instrumentation/probe_jp_weather_ai.py` retains partial
observations and marks incomplete cases as failures. It does not infer a guest
PC from the legacy debugger helper's unverified register-index convention.
No port-versus-JP AI behavior delta has been demonstrated.

## Mod integration boundary

The [mod loader](modding.md#code-mod-contract) supplies persistent enum
configuration. A shared package can own one `weather.mode` option for issues
340, 72 and 73. The experimental package now registers it without changing the
base, alongside per-round selections for championship races.

The identified USA AI routine is protected: `lamborghini.us.toml` supplies
hooks and instruction replacements for `func_8002D37C`, including relocated
vehicle/AI pool accesses. The [guard generator](../scripts/gen_mod_patch_guards.py)
marks it unhookable. Forcing a replacement with the original ROM routine would
discard those fixes. A data-table update through another verified lifecycle
hook is a possible design, but its consumers, restoration and ownership have
not been validated for the full set of JP effects. The loader-tail hook below
supports the current data-table changes, not the instruction-encoded AI cap.

The user selected reproduction of JP race-slot behavior. The package uses the
unprotected `func_80066F84` loader-tail call at USA `0x800081E0` for confirmed
data changes. The protected routine's wet throttle cap remains instruction-
encoded; no exact mod-only integration has been established for that branch.
A minimal inert base-port bridge has been proposed, with approval pending
because the original issue prohibits core changes. AI behavior parity is still
unverified; a per-frame global throttle clamp would not be equivalent.

## Reproduce

With both verified cartridges in the repository root:

~~~text
python scripts/audit_jp_weather.py --check
python -m unittest discover -s tests -p test_jp_weather.py
python -m unittest discover -s tests -p test_probe_jp_weather.py
python tools/emu_instrumentation/probe_jp_weather.py --ares PATH_TO_ARES --output artifacts/jp-weather-live.json
~~~

The probe defaults to all six circuit indices and all three weather selections,
one player, with a fresh boot for each case. `--players 3 --circuits 0 1 2`
checks the multiplayer restriction separately. Do not run probes concurrently
on the same debugger port. The output retains failures separately and exits
nonzero if any case fails; successful rows alone do not certify a complete run.
The final probe records the requested cases and sets `complete` only after
attempting the whole matrix. A complete campaign can still contain failures.
