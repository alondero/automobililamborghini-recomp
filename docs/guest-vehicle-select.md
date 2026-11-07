# Vehicle selection source replacement

Status: **Implemented** USA guest source; verification results below distinguish
synthetic parity from interactive behavior.

[guest/vehicle_select.c](../guest/vehicle_select.c) reconstructs the complete
left and right model-selection handlers. It replaces the four register hooks
used by Extra Vehicles. The game thread owns selection; the only host interface
is `lambo_get_extra_vehicles()`, a boolean setting query. Each availability check
samples it separately, as the old hooks did. Saved unlocks remain unchanged.

## ROM and layout evidence

The USA ROM SHA-256 is
`cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c`.
Both bodies contain 86 instructions. Existing disassembly words were checked
against every instruction; the right handler includes the old split fragment
at ROM `0x3F5F0`. Neither readable placeholders nor generated C supplied the
reconstructed algorithm.

| Handler | Runtime range (exclusive end) | ROM range | Body SHA-256 |
| --- | --- | --- | --- |
| Left, `func_8003F40C` | `0x8003E80C..0x8003E964` | `0x3F40C..0x3F564` | `a00cf9d791a4a2d7ce7c08431521c9f809a325310b95f1307375aaa8c3bff0a5` |
| Right, `func_8003F56C` | `0x8003E96C..0x8003EAC4` | `0x3F56C..0x3F6C4` | `c069da915d36bc010f003fb23a3d35954d615233152c25e659b9854eeac57ca5` |

A static scan of the current symbol-covered ROM code found no incoming direct
branches/calls to either body, and no outgoing static transfers. Both return
through `jr ra`; the menu invokes item callbacks indirectly. This scan does not
prove absence of arbitrary indirect jumps into a body. Original entry addresses
remain registered for those menu callbacks and runtime mod lookup.

Bindings live in [guest/symbols.ld](../guest/symbols.ld). The guest source is
compiled for big-endian MIPS; it never casts native structs over host RDRAM.

| Field | Address and width | Meaning |
| --- | --- | --- |
| Selected player | `0x800CE6A6`, s16 | One-based player 1..4. |
| Model cursors | `0x800CE7E8`, four s16 | Model indices 0..23. |
| Availability | `0x800985C0`, eight s16 | Any nonzero value permits a category. |
| Descriptors | `0x8013D490`, 24 records of 24 bytes | First s16 is category; remaining fields are not read by these handlers. |

Descriptor data is loaded from ROM `0xAEE80`, not from the code section's
linear address mapping. Reading those ROM halfwords confirms three consecutive
variants per category in order `0,4,1,2,3,5,6,7`. The source decrements/increments,
narrows to s16, wraps at 0/24, and skips locked categories. Valid callers supply
the player/model ranges above. The stock all-locked, cheat-disabled loop does
not terminate; the replacement retains that precondition rather than silently
inventing a selection. Normal setup always enables categories 0 and 4.

## Verification contract

[The oracle builder](../tests/build_vehicle_select_reference.py) pins ROM and
body hashes, checks the return instructions and reads the actual descriptor
categories into a build-local fixture. It separately recompiles both original
ROM bodies with frozen copies of the four old cheat substitutions. Production
generation inputs are not used to recover those substitutions.

[The parity test](../tests/test_vehicle_select_parity.cpp) covers all 256
availability masks, both directions, disabled/enabled cheat and a setting that
changes after the first check. Single-category, stock-default, all-unlocked and
all-locked masks exercise all four players and 24 models; remaining masks rotate
player/model coverage. It excludes the nonterminating stock all-locked case.
Nonzero availability includes negative
halfwords. Persistent RAM and query counts must match; only compiler stack
scratch is excluded. This checks unchanged descriptors, progression, availability
and other players alongside the selected cursor. Both originals have no game
service calls to compare; their setting queries are counted.

The source patch uses the existing Clang/LLD -> N64Recomp build path. Compiled
MIPS bytes, relocations and the original callable entries are registered using
the same machinery as the [pit replacement](automatic-pit-stops.md).

~~~text
cmake --build build --target lambo_selector_parity_tests lambo_cheat_tests
ctest --test-dir build -R "lambo_(vehicle_select_parity|cheats)" --output-on-failure
~~~

Parity does not establish interactive menu rendering or Android behavior.
Those checks must be reported separately from the synthetic fixtures.

On 2026-11-07, the standalone parity executable passed all 7,614 cases on
Windows with MinGW GCC 15.2, Zig 0.13.0 guest compilation and the pinned
N64Recomp. The original disassembly and ROM checks above were performed in
this implementation session.
