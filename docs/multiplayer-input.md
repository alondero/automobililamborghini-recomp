# Multi-controller input — empirical measurement (preflight for #95 / #96)

**Status:** preflight report. Zero source changes. This is the byte-exact ground truth
that Slice A (multi-pad input + per-channel PIF bridge) and Slice B (per-port Controller
Pak) are written against.

**Method.** All ares figures were captured by running the unmodified **Automobili
Lamborghini (USA)** ROM under bundled ares v147 through its GDB DebugServer
(`tools/emu_instrumentation/ares_session.py`), reading RDRAM and setting write
watchpoints. All port figures come from an 8 MiB RDRAM save-state
(`LAMBO_STATE_SAVE`, state 8) parsed back to big-endian order, plus static reads of the
recompiled sources. ares RDRAM is big-endian; the port stores RDRAM as little-endian
words with `^3`/`^2` byte/halfword swaps (`recomp.h`), so port bytes were un-swapped
before comparison. Scripts live in the session scratchpad (`probe1_pif.py` …
`probe4_scan.py`, `parse_lstate.py`).

> **Two premises in the parent tickets are corrected by measurement, not assumed away:**
> 1. The per-channel controller record is **8 bytes**, not 24 (a controller-read joybus
>    frame = 4 command/format bytes + 4 response bytes). PIF RAM is 64 bytes total; 4×24
>    could not fit.
> 2. The game's `OSContPad` is the standard libultra **6-byte** struct at `0x800A39E0`,
>    not a 24-byte record.

---

## Addresses at a glance

| Symbol | Addr (KSEG0) | Role |
|---|---|---|
| SI read buffer (menu/boot) | `0x8011C640` | 4× controller-READ joybus frames (cmd `0x01`) |
| SI status/pak buffer | `0x8011C6D0` | STATUS (`0x00`) + pak READ/WRITE (`0x02`/`0x03`) frames |
| controller mode | `0x8011C680` | `= 0x01` |
| controller count | `0x8011C681` | `= 0x04` (game-detected; ares answers 4 devices) |
| `OSContPad[]` array | `0x800A39E0` | decoded pads, **6-byte** stride, `pad[i] = base + i*6` |
| `players` | `0x800CE6A4` | halfword; render + (future) input side key on it |
| state machine | `0x800CE6AC` | 2→4→6→7→8 = attract → … → demo race |

SI/PIF MMIO (ares layout): `SI_DRAM_ADDR 0xA4800000`, `SI_PIF_ADDR_RD64B 0xA4800004`,
`SI_PIF_ADDR_WR64B 0xA4800010`, `SI_STATUS 0xA4800018`. The PIF RAM 64-byte window is
also directly readable at `0xBFC007C0` under ares and mirrors `0x8011C640`.

---

## 1. PIF RAM layout

The game hand-rolls its SI kernel and DMAs a 64-byte joybus command block to/from PIF RAM.
Two distinct blocks are used, staged in RDRAM at `0x8011C640` and `0x8011C6D0`.

### 1a. Controller-read block `0x8011C640` (cmd `0x01`)

Captured identically from ares (`FF 01 04 01 00 00 00 00` ×4 + `FE`) and the port state-8
save-state. Per-channel record = **8 bytes**, four of them back-to-back, then a `0xFE`
end marker:

```
0x8011C640  byte layout (64-byte PIF block)

 offset  +0   +1   +2   +3   +4   +5   +6   +7
 ch0    [FF] [01] [04] [01] [BH] [BL] [SX] [SY]   channel 0
 ch1    [FF] [01] [04] [01] [BH] [BL] [SX] [SY]   channel 1
 ch2    [FF] [01] [04] [01] [BH] [BL] [SX] [SY]   channel 2
 ch3    [FF] [01] [04] [01] [BH] [BL] [SX] [SY]   channel 3
 +0x20  [FE] ...end...

   FF = format/pad byte (frame present)
   01 = tx count  (1 command byte follows)
   04 = rx count  (4 response bytes expected)
   01 = command   0x01 = CONTROLLER READ
   BH = button HIGH byte   (response +0)   \  standard joybus order:
   BL = button LOW  byte   (response +1)   /  byte4 = HIGH  (verified: func_80074DF4
   SX = stick X    (signed, response +2)      reads lhu of bytes 4,5; port
   SY = stick Y    (signed, response +3)      LAMBO_INPUT_PROBE reports bytes45)
```

Empty sockets: a real N64 PIF sets bit7 (`0x80`) of the **rx-count** byte (offset +2 → `0x84`)
when a channel has no device. **ares answers device-present on all four channels**
(see §2), so every channel shows a clean `04` rx and the game latches `count = 4`.

### 1b. Status/pak block `0x8011C6D0` (cmd `0x00` / `0x02` / `0x03`)

A single frame per operation, on channel 0. Captured (ares and port, byte-identical) as a
**pak block-READ** during the boot/menu pak scan:

```
0x8011C6D0

 +0   +1   +2   +3   +4   +5   +6 .......... +38  +39  +40
[FF] [03] [21] [02] [AH] [AL] [ 32 data bytes ...] [CRC][21] [FE]

   FF = format byte
   03 = tx count  (cmd + 2 address bytes)
   21 = rx count  (0x21 = 33 = 32 data + 1 data-CRC)
   02 = command   0x02 = PAK BLOCK READ
   AH,AL = block address; low 5 bits are the address-CRC, so addr = (AH<<8|AL) & 0xFFE0
           observed 0x04E6 -> 0x04E0 (ID/inode area — osPfsInitPak)
```

Pak **WRITE** frames are `FF 23 01 03 AH AL <32 data> ` (tx=`0x23`=35 = cmd + 2 addr + 32
data; rx=`0x01` = 1 CRC ack; cmd `0x03`). Channel is encoded by **position**: each
`0x00` byte before a frame skips one channel, so a pak frame on channel N is preceded by N
`0x00` skip bytes. (This is exactly how the port's `lambo_joybus_answer` already counts
`channel`.)

---

## 2. `func_8007F780` bridge / entry-state dump

`func_8007F780` (runtime `0x8007EB80`) is the port's controller-read bridge — the recomp's
stand-in for the ROM's raw SI read. Ground-truth entry state, captured on ares across the
boot→race progression, and the port's state-8 save-state:

| Field | ares (measured) | port state-8 save |
|---|---|---|
| `state` `0x800CE6AC` | 2→4→6→7→8 over ~24 s | 8 |
| `players` `0x800CE6A4` | 0 in attract; **1** from state 7 | 1 |
| mode `0x8011C680` | `01` | `01` |
| count `0x8011C681` | `04` | `04` |
| `0x8011C640` (4 records) | `FF 01 04 01 00 00 00 00` ×4 | `FF 01 04 01 00 00 00 00` ×4 |
| `0x8011C6D0` (pak frame) | `FF 03 21 02 04 E6 …` | `FF 03 21 02 04 E6 …` |
| `OSContPad[0..3]` `0x800A39E0` | all-zero at rest (no input) | all-zero at rest |

State dwell (ares, uninstrumented rate): state 2 ≈ 0–6 s, state 4 ≈ 7–12 s, state 6 ≈
13–21 s, state 7 at ~22 s (transient), state 8 (demo race) from ~24 s. `players` is written
to `1` exactly at the state 6→7 transition.

`OSContPad[]` at `0x800A39E0` is **live** — a write watchpoint there fired on ares,
confirming the game populates it (via `func_80074DF4`). It reads all-zero in the attract
demo because the demo presses no buttons and centres the sticks; that is expected, not a
failure to fill.

---

## 3. Port-to-car mapping rule

> **car / player N reads `OSContPad[N]` = raw SI channel N. Identity. No remap.**

Proven from the recompiled decoder `func_80074DF4` (`osContGetReadData`-equivalent, VRAM
`0x800741F4`, `RecompiledFuncs/funcs_10.c:2269`): a **single loop counter `i`** drives both
sides —

```
source:  0x8011C640 + i*8     (8-byte SI serial entry per channel)
dest:    0x800A39E0 + i*6     (6-byte OSContPad per channel)   ; i in [0, count)
```

Per-field writes (funcs_10.c:2315–2338) give the `OSContPad` layout: `sh` button → `+0`,
`sb` stick_x → `+2`, `sb` stick_y → `+3`, `sb` errno → `+4` (6 bytes with pad). Button is
read from bytes 4–5 of the SI entry (`lhu`, byte4 = HIGH). All three call sites
(`funcs_0.c:960`, `:3259`, `:23079`) pass the same `$a0 = 0x800A39E0`.

There is **no per-player remap** anywhere on this path — the same index is used to read the
SI slot and to write the pad slot. A "press start to join" menu remap (P2=port3, etc.)
was searched for and **not found**; player assignment is positional by controller port.

**Consequence for Slice A:** to drive car N, fill SI channel N's 4 response bytes
(`0x8011C640 + N*8 + 4 .. +7`) from physical pad N. The port today
(`lambo_joybus_answer`, cmd `0x01`) fills only `channel == 0`; channels 1–3 get zeroes.
That single `channel == 0` guard is the whole input-side gap.

---

## 4. `players` setter map (`0x800CE6A4`)

Every write is a halfword `sh` via `lui $at,0x800D` + offset `-0x195C`
(`0x800D0000 − 0x195C = 0x800CE6A4`). Nine stores exist across six functions:

| Function (recomp block / real entry VRAM) | Value written | Reachability |
|---|---|---|
| `func_800030F8` (`0x80003D64`) | **0** (reset/init) | boot-reachable; this is the warp/dispatch entry |
| `func_80038D6C` (`0x8003858C`, entry `0x8003816C`) | **1** | attract / race-load path — fires at state 7 (measured) |
| `func_8003CD84` (entry `0x8003C184`) | **1** + clamp write-backs (4 stores) | menu +/- adjust logic |
| `func_8003E6D0` (`0x8003DAE0`) | **1** | menu selector |
| `func_8003E738` (`0x8003DB48`) | **3** (+ writes `2` to `0x800CE6B4`) | menu selector |
| `func_8003E778` (`0x8003DB88`) | **4** (+ writes `2` to `0x800CE6B4`) | menu selector |

**Finding:** the menu *does* own a `players` writer path — the `func_8003E6xx/E7xx`
selector cluster writes 1 / 3 / 4 and `func_8003CD84` is the +/- adjust/clamp (the "2" case
is reached through the adjust path; no dedicated hard-coded `→2` selector was found in the
cluster). So `lambo_warp_tick` is **not** the only possible writer. **However**, from a
clean *headless* boot the attract path only ever reaches `func_80038D6C` (→ 1); the menu
selectors require menu navigation (input). Until menu-reachability is diagnosed
(separate ticket, per map #95), the dev warp (`lambo_warp_tick`, hook at
`func_800028D0@0x800044DC`) remains the practical writer for driving `players` to 2/3/4
outside the menu. Slice A must **honour** whatever `players` holds; it must not invent a
value.

---

## 5. Pak per-port command timing (Slice B)

All pak traffic is issued on `0x8011C6D0`, one frame at a time, channel encoded by position.

Observed sequence (ares):

- **Boot / menu (`osPfsInitPak`, `func_8007A8A0`):** cmd `0x02` block-reads of the ID / inode
  area, addresses `< 0x8000` (e.g. `0x04E0`) — validating the pak's formatted checksums.
- **In-race (`osMotorInit` rumble-detect, `func_8007AF60`):** cmd `0x03` write of bank `0x80`
  to address `0x8000`, then cmd `0x02` read-back of `0x8000` expecting byte31 == `0x80`.
  Repeats per frame while the race runs.

All observed pak frames target **channel 0**. The attract demo issues **no real block
save** (nothing to persist), so a 2P/3P *save* interleaving could not be captured headless.
What is proven is the **frame format and the channel-by-position encoding**, which is all
Slice B needs to route: a pak READ/WRITE frame on channel N (N `0x00` skip bytes ahead of
it) should be served from / persisted to that port's own image. Recommended file scheme
(from map #95): `lambo_controller_pak.pN.mpk`, N = channel + 1, legacy single-file path
stays channel-0-compatible.

> **Unverified (headless gap):** whether the game ever stages pak commands on channels > 0
> during a real multi-player save, and the ordering/interleave across channels. This needs a
> driven 2P race that reaches a records/best-times save. Marked open rather than guessed.

---

## 6. Convergence gate (bar for Slice A)

Same game state (state 8 demo race), same input (none), current single-port-stubbed port
code vs unmodified ROM on ares:

| Region | ares | port | Diff |
|---|---|---|---|
| `0x8011C640` records ch0–ch3 (32 B) | `FF 01 04 01 00 00 00 00` ×4 | identical | **byte-equal** |
| `0x8011C6D0` pak frame (first 40 B) | `FF 03 21 02 04 E6 … 21 FE` | identical | **byte-equal** |
| mode/count `0x8011C680` | `01 04` | `01 04` | **byte-equal** |
| `OSContPad[0..3]` `0x800A39E0` | all-zero | all-zero | **byte-equal** |

**The at-rest baseline is already byte-identical.** The divergence Slice A must close is
**latent**: it appears only when buttons/sticks are pressed on channels 1–3. On ares those
channels carry live response bytes; the port zeroes them (the `channel == 0` guard in
`lambo_joybus_answer`). So the Slice A gate is:

> With physical pads on ports 1–N and `players = N`, `0x8011C640 + k*8 + 4 .. +7`
> (k = 0…N−1) on the port must equal ares's bytes for the same pressed inputs — i.e. each
> channel's button/stick response must be non-zero and correct, not just channel 0. Verify
> by comparing `OSContPad[k]` at `0x800A39E0 + k*6` between port and ares under an identical
> pressed-input sequence.

Capturing the *pressed-input* side of this gate needs input injection on ≥2 ares channels,
which the headless GDB path does not provide today — it is the natural first regression
harness for Slice A to build (drive N pads, diff `0x8011C640` + `0x800A39E0` port-vs-ares).

---

## Appendix — reproduce

```
# ares ground truth (state/players/buffers, pak frames, OSContPad watchpoint):
python <scratchpad>/probe1_pif.py        # PIF buffers + PIF window + C640 write hit
python <scratchpad>/probe2_state.py      # state dwell + C6D0 pak-command sequence
python <scratchpad>/probe3_oscontpad.py  # OSContPad@0x800A39E0 write watchpoint + poke

# port side (state-8 RDRAM save-state, then parse in BE order):
LAMBO_HEADLESS=1 LAMBO_STATE_SAVE=port_state8.lstate LAMBO_STATE_SAVE_STATE=8 \
  LAMBO_STATE_SAVE_DELAY=120 LAMBO_MODERN_MAX_VIS=2000 ./build/lamborghini_modern
python <scratchpad>/parse_lstate.py port_state8.lstate
```

ares figures: bundled ares v147 via `tools/emu_instrumentation/ares_session.py`
(GDB DebugServer). Port figures: `build/lamborghini_modern` state-8 save-state, un-swapped
to big-endian for comparison.
