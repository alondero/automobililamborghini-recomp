# Japanese cartridge

Status: experimental and not playable. The Japanese cartridge has a checked-in
symbol table and recompile configuration. Its recompile completes and the
output compiles. The generated code is not linked into the port, the runtime
still accepts only the USA ROM, and no port feature has been checked on it.
The plan is
[issue 338](https://github.com/alondero/automobililamborghini-recomp/issues/338).

The Japanese release is *Super Speed Race 64*. Most of its functions are the
same instruction sequences as *Automobili Lamborghini (USA)* at different
addresses (see [Symbol table](#symbol-table)). Its link order differs, libultra
(Nintendo's N64 system library) sits elsewhere, and it has one extra code
overlay.

## Cartridge identity

Both dumps are big-endian `.z64` files. A byte-swapped or word-swapped copy of
the same cartridge hashes differently.

| Field | USA | Japan |
| --- | --- | --- |
| Expected file name | `Automobili Lamborghini (USA).z64` | `Super Speed Race 64 (Japan).z64` |
| Size | 4 MiB | 8 MiB |
| Header title | `LAMBORGHINI` | `SUPER SPEED RACE 64` |
| Cartridge code | `NLCE` | `NLCJ` |
| Header version | 0 | 0 |
| Header CRC | `41B25DC4 1B726786` | `9CE02E22 206EF1B0` |
| SHA-1 | `78b36b48040426478011caef5e11884af2e80375` | `98009e4948faf885864e52e1268700a0144c1b2e` |
| SHA-256 | `CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C` | `06C9C992014DA5982E747117C690C1590A011C0C6756B0522DF126366BF8B9A1` |
| MD5 | `ec39579f066a9714ff030d07dec3c9d3` | `27a7a2a19fc67346bb25e3c5bb3a91ce` |
| XXH3-64 (runtime check) | `525201d7279f34e3` | `a784eb51a19b6cc0` |

The file name uses the header spelling, *Race*, not *Racer*. The upper 4 MiB of
the Japanese dump is a byte-for-byte copy of the lower 4 MiB. All code and data
found so far is in the lower half.

XXH3-64 is the 64-bit hash the runtime compares in `src/main.cpp`
(`game.rom_hash`). The USA value there matches this table. The Japanese value
was computed the same way, once, and nothing in the repository checks it yet.
The SHA-1 values are the ones `scripts/gen_jp_syms_toml.py` checks.

The release workflow fetches both dumps from the private assets repository
named by `ROM_ASSETS_REPO`. Neither dump is committed here or packaged.

## Code layout

Measured from the Japanese ROM bytes:

| Section | ROM | Runtime address | Code size | Evidence |
| --- | --- | --- | --- | --- |
| `.text` | `0x1000` | `0x80000400` | `0x7AB80` | Boot entry is `0x80000400`, as in the USA ROM. Code ends at ROM `0x7BB80`, where the RSP (Reality Signal Processor) boot microcode starts. Its first word is `0x09000419`, the same as at USA ROM `0x87690`. |
| `.ovl_801F9800` | `0xFA1F0` | `0x801F9800` | `0x2E5B0` | Boot copies ROM `0xFA1F0`-`0x151840` to `0x801F9800` with `func_80009B60` (Japanese runtime `0x80000734`-`0x8000075C`). The last return (`jr $ra`) is at ROM `0x12878C`; the rest is the overlay's data. |

- The main image's data runs to `0x80084110`, where boot starts clearing the
  zero-filled data (BSS). The USA image is larger: its clear starts at
  `0x8008FCD0`.
- Nine call sites pass a fixed ROM range to `0x801F9800`. Eight copy the
  overlay range above: boot and seven game-state transitions (for example at
  Japanese runtime `0x80001FD0` and `0x80002038`). The ninth, inside
  `boot_pad_apply_calibration` at Japanese runtime `0x8000773C`, copies ROM
  `0x151840`-`0x177640`. That block has no returns or stack-frame prologues,
  so it is inferred to be data. The search looked for literal ROM and runtime
  addresses; a copy with a computed address would not have been found.
- There are 151 distinct call instructions (`jal`) in ROM `0x1000`-`0x130000`
  that target `0x801F0000`-`0x80230000`. When runtime minus ROM is
  `0x800FF610`, 129 of their targets land on a stack-frame prologue. No other
  offset reaches more than 7.
- Boot also copies ROM `0x84D10`-`0xD55E0` to `0x80125000`. No code was found
  there.

The USA build has one `.text` section and no code overlay. The extra Japanese
section recompiles without the per-overlay sections that
[issue 337](https://github.com/alondero/automobililamborghini-recomp/issues/337)
adds under the
[enhancement roadmap](https://github.com/alondero/automobililamborghini-recomp/issues/26).
Whether the runtime registers its functions when the game copies the overlay,
and what happens when the data block replaces it, are runtime questions for a
later stage.

## Audio microcode

The audio microcode text is byte-identical. The USA RSPRecomp input reads ROM
`0x88B90`, size `0xE1C`; the same bytes are at Japanese ROM `0x7D080`. The 16
audio command (ACMD) dispatch entries in `aspMain.us.toml` are at Japanese ROM
`0x84A60`, and the RSP boot microcode is identical too. `src/aspMain.cpp` is
therefore valid for both cartridges. The microcode data block differs after the
dispatch table; RSPRecomp does not read it.

## Symbol table

The USA symbol table comes from the decompilation project's splat output (see
the header of `lamborghini.syms.toml`). This project has no Japanese
decompilation output, so the issue's preferred source of Japanese function
boundaries does not exist here. Instead, `scripts/gen_jp_syms_toml.py` derives
the Japanese table from the USA table and both ROMs. It writes
`lamborghini.jp.syms.toml` and `lamborghini.jp.toml`, which are checked in like
their USA counterparts.

1. **Match.** A USA function is found in the Japanese code when the words agree
   after every immediate is masked, so the opcodes and registers are identical.
   Repeated or short bodies are accepted only where a neighbouring match
   predicts the same displacement, or where the raw bytes occur once.
2. **Discover.** The remaining code is split into functions by call targets,
   returns, jumps and resolved switch tables. A body that falls through into a
   matched function makes that function an alternate entry sharing its
   epilogue, the same shape as the USA `BootLoadInitialAssets` pieces.
3. **Align.** A discovered function takes the USA name of a changed function
   when their instruction sequences clearly correspond, or when a weaker
   sequence match is confirmed by the named functions both bodies call.
4. **Slot.** Unnamed functions between two named neighbours take the USA names
   from the same position. This assumes link order inside a module did not
   change; it has not been checked independently.

A named Japanese function keeps the USA name, so `stubs`, `ignored` and later
hooks can use one name in both regions. Names that start with `jp_func_` carry
the Japanese runtime address. A matched name has the same opcodes and
registers as the USA function. An aligned or slotted name is an inference from
similarity, not proof that the routine is the same.

The generator prints these counts:

| Measure | Count |
| --- | ---: |
| USA functions | 1047 |
| Matched (same opcodes and registers) | 791 |
| ... of which every non-address immediate also agrees | 762 |
| Discovered | 396 |
| ... named by alignment (inferred) | 53 |
| ... named by link-order slot (inferred) | 25 |
| Japanese functions | 1187 |
| USA stubs carried | 167 of 244 |
| USA ignored functions carried | 34 of 48 |
| USA hooked or patched functions with no Japanese name | 13 of 74 |

It also names the functions behind each gap:

- Two carried stubs, `func_8007DAB4` and `func_80086724`, rely on an inferred
  name. A wrong inference would stub the wrong Japanese function.
- 72 of the 77 USA stubs that were not carried are alternate entries inside a
  larger USA function. The other five have no Japanese name yet. Their
  Japanese code is recompiled as ordinary code, so whatever the USA stub
  avoids is live there.
- 12 of the 14 ignored USA names that were not carried are alternate entries.
  `func_8007E4E0` has two equally good Japanese candidates, `0x800694E0` and
  `0x80074FD0`. `func_8005F39C` has no close candidate.
- Hooks and instruction patches are not carried to the Japanese configuration.

The "every non-address immediate also agrees" count tracks `lui` (load upper
immediate) registers through straight-line code only. An address whose `lui`
sits in another block therefore counts as a difference. The other 29 matches
have not been reviewed one instruction at a time; some may differ in a real
constant.

The Japanese recompile reports no function branching outside its boundaries,
creates no unnamed static functions, and leaves only zero padding outside the
listed functions. Its output, `RecompiledFuncsJP/`, compiles with the C
diagnostics `CMakeLists.txt` relaxes for `RecompiledFuncs/`. The tests check
all of this.

The output has 419 indirect calls through register `$t9`, against 55 in the
USA output. 348 of them are in the overlay from `0x8020EA6C` upward, a block
with no USA counterpart. The overlay's data holds libjpeg error messages (for
example "Bogus marker length" at ROM `0x1501A0`), so this block is inferred to
be a JPEG image decoder, which calls through function tables. Whether every
indirect target is a registered function can only be measured at runtime.

## Regenerate and check

With both dumps at the repository root:

~~~text
python scripts/gen_jp_syms_toml.py
./build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp lamborghini.jp.toml
python -m unittest -v tests.test_jp_recompile
~~~

`python scripts/gen_jp_syms_toml.py --check` compares instead of writing, and
prints the ROM hashes it verified. `lamborghini.jp.toml` records a digest of
the USA symbol table and configuration it was derived from. A test without
ROMs fails when either has changed, so a USA change that was not carried
across fails on every pull request. The other tests in
`tests/test_jp_recompile.py` skip without both dumps, a built N64Recomp, or
gcc. The release workflow runs them and fails if any skip.

On Windows, run the recompile tests from a shell with the MinGW `bin`
directory first on `PATH`. Otherwise N64Recomp fails to start with exit code
`0xC0000139`.

## Not done yet

- The runtime, picker and frontend accept only the USA identity.
- `RecompiledFuncsJP/` is not linked. Both images use the same generated
  function names, so linking both needs a separate symbol namespace.
- Port code addresses guest memory at USA addresses.
- No hook, instruction patch, cheat, HUD, widescreen, audio, save or race check
  has been run on the Japanese cartridge.
- Japanese text rendering has not been checked.
