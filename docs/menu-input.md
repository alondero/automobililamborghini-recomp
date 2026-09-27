# Menu stick sensitivity

Status: **Experimental**. Implemented and unit-/config-tested; the generated menu
handler carries the hooks. Physical-hardware behaviour remains **Unverified**.

The pre-race menus turn the analog stick into menu movement with a fixed
threshold of about 51 on the game's 80-point stick scale (roughly 64% of full
deflection). A device with a limited or imprecise analog range, including virtual
touch controls, can therefore feel unresponsive.

**Enhancements > Menu stick sensitivity** is an opt-in multiplier (1.0 = stock,
up to 2.5) that scales the stick inside the menu routine only. It is off by
default and is saved as `menu_stick_sensitivity` in `graphics.json`. Gameplay
steering, the D-pad, and the keyboard never pass through this routine, so a race
is unaffected.

## Guest boundary

The USA ROM SHA-256 used for this investigation is
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.

`func_800427D4` (runtime `0x80041BD4`, ROM offset `0x427D4`) reads the parsed
controller pad at `0x800A39E0 + channel*6`. The record follows the OSContPad
shape: buttons at `+0`, `stick_x` at `+2`, `stick_y` at `+3`. It derives menu
movement two ways:

- a cursor-velocity path: `stick_x` (`0x800424FC`) into `0x8009873E`, and
  `stick_y` (`0x800426E0`) into `0x80098760`; each is deadzoned by 7, clamped to
  ±48 on X and ±32 on Y, and advances the cursor by `velocity>>3` per frame; and
- a discrete ±51 direction threshold on `stick_x` (`slti 0x33` at `0x800428D4`
  and `slti -0x32` at `0x8004294C`) that synthesizes the D-right / D-left bits.

All four hooks run synchronously on the game thread inside this handler. Each
rewrites the register that already holds the loaded stick byte, immediately
before its consumer runs. They never write the shared controller buffer, buttons,
or any race input, and they retain no host state, so toggling the option or
loading a save state needs no reset.

| Runtime hook | Reads | Native action |
| --- | --- | --- |
| `0x80042500` | `stick_x` (velocity) | Scale the loaded byte. |
| `0x800426E4` | `stick_y` (velocity) | Scale the loaded byte. |
| `0x800428D4` | `stick_x` (±51 right) | Scale it before the comparison. |
| `0x8004294C` | `stick_x` (±51 left) | Scale it before the comparison. |

`lambo_menu_stick_scale` returns its input unchanged unless
`menu_stick_sensitivity()` is greater than 1.0, so a default build is a
byte-for-byte no-op. Scaled values are rounded to nearest and clamped to the
signed pad byte domain (±127). The enabled value is atomic because the settings
UI thread writes it and the game thread reads it. These fixed seams should
eventually be replaced by a named source-level menu-input interface.

## Verification

`lambo_menu_stick_scale` is a pure function. `tests/test_menu_stick.cpp` covers
the no-op default, positive and negative scaling, rounding, and clamping.
`tests/test_live_config.cpp` covers the stock default, persistence, reload, the
range clamp, and the environment override. Regenerating from
`scripts/gen_syms_toml.py` and building verifies hook placement.

Run after building:

~~~text
ctest --test-dir build -R "lambo_(menu_stick|config_live_updates)" --output-on-failure
~~~

The option has not been exercised on physical hardware in this checkout; the
interactive menu check remains **Unverified**.
