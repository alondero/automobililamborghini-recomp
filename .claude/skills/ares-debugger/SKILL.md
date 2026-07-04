---
name: ares-debugger
description: Drive the ares emulator headless via its GDB DebugServer — read/write RDRAM, set watchpoints to find writers, set breakpoints at known functions, capture per-frame VI state, and dump memory regions from a running game. ares runs the ROM, so it is the ground-truth reference for what the port should reproduce.
---

# ares DebugServer — Programmatic Emulator Access

ares ships a GDB-compatible TCP server. You can read/write any RDRAM address, set
software/hardware breakpoints, set memory watchpoints, and capture per-frame VI state
from a running game — no human in the loop, no manual dumps.

**Use this skill when:**
- You want to know "who writes address X?" — set a write watchpoint.
- You want to know "is function Y reached?" — set a breakpoint at its entry.
- You want an RDRAM dump from a specific game state without asking the user.
- You want ground-truth ROM behaviour to compare the port against.
- You want to poke a flag (menu open, scene index) and observe what renders.

## Quickstart

The session helper handles launch, drain timing, and cleanup. Prefer it over
hand-rolling the launch + taskkill dance.

```python
import sys
sys.path.insert(0, r'F:\src\automobililamborghini-recomp\tools\emu_instrumentation')
from ares_session import ares_session

with ares_session(port=9150) as c:
    print(f'VI_CURRENT: 0x{c.read32(0xA4400010):08X}')  # live vcounter
```

The context manager taskkills ares on exit even if the block raises. To cross-reference a
KSEG0 register value against a known function, grep the symbol map (`lamborghini.syms.toml`).

## Recipe 1 — Find the writer of an address

The single highest-value workflow — replaces long disassembly hunts with a seconds-long probe.

```python
with ares_session(port=9150, boot_wait=2.0) as c:
    c.set_watchpoint(0x800A39CC, kind='write')
    stop = c.continue_until_halt(timeout=10)
    info = c.parse_stop_reason(stop)          # {'signal':5, 'kind':'watch', 'addr':...}
    regs = c.read_kseg0_registers()           # candidate PC/$ra/args
    c.clear_watchpoint(0x800A39CC, kind='write')
    print(info, sorted(regs.items()))
```

**Catching EARLY-BOOT writes** (one-shot inits that finish before a normal connect):
pass `await_gdb=True` to freeze the CPU at instruction 0, arm the watchpoint, THEN continue.

```python
with ares_session(port=9155, await_gdb=True) as c:   # CPU frozen at instr 0
    c.set_watchpoint(0x800A39D8, kind='write')
    stop = c.continue_until_halt(timeout=90)           # NOW start execution
```

Do NOT single-step through early boot after a hit — it trips spurious signal-16 halts.
Read the pre-store value instead, or just continue. `boot_wait` lets the game advance to a
later state before the watchpoint arms — bump it higher to reach later screens.

## Recipe 2 — "Is function Y called?"

```python
with ares_session(port=9150) as c:
    c.set_breakpoint(0x8003816C)                      # function entry
    stop = c.continue_until_halt(timeout=30)
    ra = c.read_register(31) & 0xFFFFFFFF             # $ra = call site
    c.clear_breakpoint(0x8003816C)
    print(f'reached {c.parse_stop_reason(stop)}, from 0x{ra:08X}')
```

A timeout is a useful negative signal: the function was not reached in that window.
Note the software-breakpoint caveat in "What works" below — watchpoints are more reliable.

## Recipe 3 — Dump a memory region from a running game

```python
with ares_session(port=9150, boot_wait=4.0) as c:
    fb_addr = c.read32(0xA4400004) & 0x00FFFFFF        # VI_DRAM_ADDR
    fb = c.dump_rdram(0x80000000 | fb_addr, 320 * 240 * 2)
    Path('fb.bin').write_bytes(fb)
```

`dump_rdram` chunks at 2KB to stay under `PacketSize=4096`. Throughput ~30 KB/s — fine for
DLs, descriptor tables, framebuffers; a full 8 MB dump takes minutes (ask the user for that).

## Recipe 4 — Poke state and observe

```python
with ares_session(port=9150) as c:
    c.write_mem(0x800A3A56, bytes([0, 0, 0, 1]))      # set a flag, MSB first
```

`write_mem(addr, bytes)` is big-endian-as-supplied — pass the bytes you want at that address.

## Recipe 5 — Per-frame VI capture

```bash
python tools/emu_instrumentation/run_ares_debug.py \
  "Automobili Lamborghini (USA).z64" --frames 3 --output ares_vi.json --port 9151
```

Captures `vi_current`, `vi_dram_addr`, `vi_width` per frame.

## VI / MMIO addresses (ares layout — these differ from real N64 hardware)

| Address | What |
|---------|------|
| `0xA4400000` | VI_CONTROL (`& 3` → 2 = 16bpp RGBA5551, 3 = 32bpp RGBA8888) |
| `0xA4400004` | VI_DRAM_ADDR (current framebuffer; `& 0x00FFFFFF` for RDRAM offset) |
| `0xA4400008` | VI_WIDTH (px) |
| `0xA4400010` | VI_CURRENT — live vcounter, `& 0x1FF` → 0..~262 (NTSC), resets once per frame |
| `0x80000000` | RDRAM base — readable + writable |
| `0xA3Cxxxxx` | DPC/RDP registers — **NOT accessible** via ares GDB (returns 0) |

**Frame-boundary detection: use a large-decrease detector, NOT `cur == 0`.** `VI_CURRENT`
resets once per frame; `cur == 0` only fires if a poll lands in the brief vblank window, and
at ares' instrumentation-slowed rate that misses most frames and undercounts elapsed VI:

```python
if prev is not None and (prev - cur) > 200:   # one VI frame elapsed
    vi += 1
prev = cur
```

## Reading a single halfword/byte from ares RDRAM (endianness trap)

ares RDRAM is **big-endian** (real N64 layout). `read32(addr)` returns 4 bytes as a BE u32.
To pull one halfword, mask keyed on alignment — a blind `& 0xFFFF` returns the *adjacent* field:

```python
def read_h(c, addr):                     # signed BE halfword
    w = c.read32(addr & ~3)
    v = (w & 0xFFFF) if (addr & 2) else ((w >> 16) & 0xFFFF)
    return v - 0x10000 if v >= 0x8000 else v
```

Do NOT apply any host-little-endian byteswap to ares reads — ares memory is already BE.

## What works

| Capability | Packet | Notes |
|------------|--------|-------|
| Memory read | `m` | ~2ms round-trip, `PacketSize=4096` (chunk in 2KB) |
| Memory write | `M` | Big-endian as supplied; verified round-trip |
| Read register | `p N` | Index 31 = `$ra`; PC index is not officially documented (empirically ~37) |
| Read all registers | `g` | ~71 64-bit slots, MIPS64-ish layout |
| Write watchpoint | `Z2`/`z2` | Fires in <10ms, stop format `T05watch:ADDR;` — **the reliable tool** |
| Read watchpoint | `Z3`/`z3` | Works; re-arm (clear then set) after each hit |
| Access watchpoint | `Z4`/`z4` | Accepted |
| Continue / halt | `c` / `vCtrlC` | `continue_until_halt` / `client.halt()` |
| No-ack mode | `QStartNoAckMode` | Auto-enabled by `ares_session` |

## What doesn't work / gotchas

- **Software (`Z0`) and hardware (`Z1`) breakpoints are unreliable** — they return OK but
  may never trap (code DMA'd into RDRAM can overwrite the trap). Prefer watchpoints.
- **DPC/RDP registers at `0xA3Cxxxxx`** are not reachable through ares GDB (return 0).
- **Single-step `s` after a watchpoint hit** re-hits the same watchpoint — `clear_watchpoint()`
  before `step()`.
- **Watchpoint stop registers do NOT reliably give the writer's PC.** At a Z2/Z3 hit, `$ra`
  and several slots hold exception-handler state, and FPU regs alias to plausible-looking
  addresses. Don't trust any "PC candidate" from a watchpoint hit. To find a writer: (1) use
  Z2 to confirm/time the write, then (2) Z3 on a value the writer *reads* just before writing;
  at that hit `$a0`/`$t0` MAY hold the pointer, but only when the read is `lw $aN, off($tN)`.
- **No wall-clock timing under instrumentation.** Heavy watchpoint/polling load drops ares to
  a fraction of realtime and the factor varies per trace. Report event ordering, not seconds.
  For absolute durations, anchor to a per-VI game counter (identify one first).

## Files

| File | Purpose |
|------|---------|
| `tools/emu_instrumentation/ares_session.py` | Context manager — launch, connect, cleanup |
| `tools/emu_instrumentation/ares_debug_client.py` | GDB client + watchpoint/breakpoint/dump helpers |
| `tools/emu_instrumentation/run_ares_debug.py` | Per-frame VI capture CLI |
| `tools/emulators/ares-base/ares-v147/ares.exe` | Bundled ares v147 |

## Caveats

- ares opens a visible window on Windows even in kiosk mode; the helper taskkills it on exit.
- Pick a unique port per concurrent session (9150, 9151, …).
- Each session takes ~2.5s to spin up before the first read; batch probes into one block.
