# Menu stick sensitivity

Status: **Experimental**. The complete USA menu-input handler is reconstructed
in guest C and has ROM-oracle parity coverage. Physical-controller feel and
interactive menu presentation remain **Unverified**.

The pre-race menus turn the analog stick into menu movement with a fixed
threshold of about 51 on the game's 80-point stick scale. A device with a limited
analog range can therefore feel unresponsive.

**Enhancements > Menu stick sensitivity** is an opt-in multiplier (1.0 = stock,
up to 2.5), saved as `menu_stick_sensitivity` in `graphics.json`. Only stick
values read by the menu handler are scaled. Digital D-pad/keyboard buttons and
race steering retain their original behavior.

## Source replacement

[guest/menu_input.c](../guest/menu_input.c) reconstructs the entire
`func_800427D4`: runtime `0x80041BD4..0x80042C4C`, ROM `0x427D4..0x4384C`
(1,054 instructions). The source replaces four generated-register hooks and
preserves the original callable entry. It compiles to MIPS and then through
N64Recomp using the same [guest-patch pipeline](automatic-pit-stops.md) as pits.

The USA ROM SHA-256 is
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
The body SHA-256 is
`081F33E4BF35E3245F386AE299A4997A4E58050D25D4E612D6066CD6A2FE7264`.
The earlier decomp project's two disassembly fragments, split at ROM `0x42C4C`,
were joined and all instruction words verified against this ROM. A scan of
symbol-covered ROM code found four static incoming calls, all at the entry,
and no interior static incoming edge. This does not establish every possible
indirect caller. The implementation is recovered behavior, not a matching
build of the original source.

The only host interface is `lambo_get_menu_stick_sensitivity()`, a double-valued
setting query with no guest pointer or writes. The UI publishes the setting
atomically; the game-thread handler samples it at each of the four original
read sites. Keeping separate samples preserves live changes between reads.
Scaling is guest-owned: default returns the original signed byte, including
-128; enabled scaling rounds half values away from zero and clamps to -127..127.
No native menu progress is retained, so toggles and savestate loads need no
host-state reset.

## Guest layouts and behavior

[menu_input.h](../guest/menu_input.h) declares big-endian guest layouts and
asserts the recovered offsets. These types are never cast over host RDRAM.
Unknown fields stay unnamed. Bindings in [symbols.ld](../guest/symbols.ld) are
USA-specific; this does not provide a Japanese replacement.

The handler runs synchronously on the game thread in pre-race menus. State at
`0x80098560` includes screen/transition/history halfwords, a menu-item pointer
at +`0x28`, item/next-item halfwords at +`0x2C`/+`0x2E`, the zero-based input
port at +`0x4E`, and typing/cursor/name-entry fields through +`0x5C`.
Menu items are 32 bytes: availability s16 at +2, an activation function pointer
at +4, and four neighbor indices at +`0x10` (up/down/left/right). Negative
availability skips an item; neighbor -1 means no move. Up-neighbor 100 enters
name typing. The linked menu graph must terminate at an available item, as in
the original game.

OSContPad records at `0x800A39E0 + port*6` hold buttons u16 and stick X/Y s8 at
+2/+3. The stock handler consumes pressed bits at `0x800A3A00 + port*2` into
`0x800A39F8 + port*2`, and controls repeat halfwords at `0x800A3A08 + port*2`.
The scaling feature changes local input values, not the shared pad records.

Typing uses work halfwords `0x8009873E` and `0x80098760`: deadzone 7, X/Y
velocity limits +/-48 and +/-32, cursor increments in eighths using arithmetic
right shifts. Bounds, D-pad fallback, cursor settling, name-character storage,
and the discrete X thresholds (+51/-51) remain guest behavior. Name records use
13-byte strides from `0x800A4819`, indexed by the one-based selected player at
`0x800CE6A6`. Menu navigation preserves back-stack handling, button priority,
item skipping, screen transitions and activation callbacks. Sound calls and
menu initialization remain calls to the existing guest functions.

## Verification

`tests/build_menu_reference.py` checks ROM identity and body digest, then
recompiles the complete original handler independently. Its four frozen test
hooks retain the old host `lround` scaling behavior; they are never linked into
the game. `tests/test_menu_input_parity.cpp` compares the MIPS-compiled guest
replacement against that oracle for 6,144 updates (2,048 fixtures, three updates
each). It compares all persistent RAM, ordered sound/initializer/activation
calls and their arguments, call-time menu state, and setting-query counts.
Only compiler-owned stack scratch is excluded.

Cases cover the signed stick domain, stock and 1.5/2.0/2.5 scaling, settings
changing between read sites, four ports, menu-specific port selection, D-pad
fallback, cursor limits, name writes, back history, activation, linked-item
skipping, typing entry, absent neighbors and screen transitions. Sound,
initialization and indirect item callbacks are deterministic recorded stubs;
this tests the handler's calls, not those callees' implementations.

The 6,144-update comparison passed on Windows with MinGW GCC 15.2 and Zig 0.13.0
providing Clang/LLD. The oracle and guest output were freshly generated locally.
N64Recomp emitted LO16 pairing warnings for the compiled patch; the compared
behavior passed, but this is not proof of every possible input or mod behavior.
The obsolete host scaler and its isolated test were removed. The guest parity
suite now checks production scaling, while `tests/test_live_config.cpp` retains
coverage for persistence, clamp and reload.

Run the focused parity test after building:

~~~text
ctest --test-dir build -R "lambo_(menu_input_parity|config_live_updates)" --output-on-failure
~~~

Interactive menu navigation, physical controllers and Android device behavior
remain unverified by this synthetic suite.

The [runtime mod fixture](../tests/mods/guest_source/README.md) also passed on
the rebuilt Windows port: entry/return hooks were regenerated for the menu
replacement, a call through its original symbol exercised sensitivity 1.5 and
the compiled helper, and an ordinary replacement was rejected as a base-patch
conflict. This synthetic init-time invocation does not verify interactive menus.
