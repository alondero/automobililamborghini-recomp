#!/usr/bin/env python3
"""Measure the ROM's title/attract state-machine dwell (in VI frames) on ares.

This is the REPRODUCIBLE reference measurement for comparing the PC port's per-state
frame counts against real hardware (ares). It watches the game's state variable at
0x800CE6AC and counts VI retraces via VI_CURRENT wrap detection, printing the VI frame
number at each state transition. Run it whenever you need the ares baseline for a
state-machine timing question instead of trusting a stale note.

WHY THE ROBUST WRAP DETECTION MATTERS (do not "simplify" this):
  VI_CURRENT (0xA4400010, raw & 0x1FF) is the live scanline counter, 0..~262, resetting
  once per frame. Detect a frame boundary as a LARGE DECREASE (prev - cur > 200), NOT as
  `cur == 0`. The `cur == 0` idiom only fires if you happen to sample during the brief
  vblank window; at ares' polling-slowed rate that misses most frames and UNDERCOUNTS the
  dwell by several-fold. (This is the exact bug that produced the bogus "ares state-8
  dwell = 540 VI" figure in W101 — the true value is ~3094 VI. See the graveyard:
  w102-ares-state8-dwell-3094-vi-no-perf-gap-2026-07-01.)

CROSS-VALIDATION: the printed boot-transition frame numbers should line up (within a few
percent) with the PC port's own `[state] vi=N state=M` log. If they don't, your VI
counting is wrong, not the game.

Usage:
  "C:/Users/alond/AppData/Local/Programs/Python/Python313/python.exe" \
    tools/emu_instrumentation/measure_ares_state_dwell.py [--port 9160] [--target-state 8] [--max-vi 8000]
"""
import sys, time, argparse

sys.path.insert(0, r'F:\src\automobililamborghini-recomp\tools\emu_instrumentation')
from ares_session import ares_session

STATE = 0x800CE6AC       # title/attract state-machine variable (halfword, big-endian)
VI_CURRENT = 0xA4400010  # ares live vcounter


def read_h(c, addr):
    """Signed big-endian halfword from ares RDRAM (alignment-keyed extraction)."""
    w = c.read32(addr & ~3)
    v = (w & 0xFFFF) if (addr & 2) else ((w >> 16) & 0xFFFF)
    return v - 0x10000 if v >= 0x8000 else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=9160)
    ap.add_argument('--target-state', type=int, default=8,
                    help='report the dwell of this state (default 8 = attract demo race)')
    ap.add_argument('--max-vi', type=int, default=8000)
    ap.add_argument('--boot-wait', type=float, default=1.0)
    args = ap.parse_args()

    with ares_session(port=args.port, boot_wait=args.boot_wait) as c:
        vi = 0
        prev = None
        last_state = None
        poll = 0
        state_first_vi = {}
        t0 = time.time()
        while vi < args.max_vi:
            cur = c.read32(VI_CURRENT) & 0x1FF
            if prev is not None and (prev - cur) > 200:   # robust wrap detection
                vi += 1
            prev = cur
            poll += 1
            if poll % 4 == 0:
                st = read_h(c, STATE)
                if st != last_state:
                    print(f"[vi={vi}] state {last_state} -> {st}  t={time.time()-t0:.1f}s", flush=True)
                    state_first_vi.setdefault(st, vi)
                    last_state = st
                    ts = args.target_state
                    if st != ts and ts in state_first_vi and vi > state_first_vi[ts] + 5:
                        print(f">>> STATE {ts} dwell = {vi - state_first_vi[ts]} VI frames", flush=True)
                        break
        print("STATE_FIRST_VI:", state_first_vi, flush=True)
        print(f"total_vi={vi} wall={time.time()-t0:.1f}s rate={vi/max(1e-9,time.time()-t0):.1f}vi/s", flush=True)


if __name__ == '__main__':
    main()
