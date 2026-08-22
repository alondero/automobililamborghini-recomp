#!/usr/bin/env python3
"""Watch the ROM copy the selected car into the steering-curve selector."""

from __future__ import annotations

import argparse
from pathlib import Path

from ares_session import ARES_EXE, DEFAULT_ROM, ares_session


STATE = 0x800CE6AC
PLAYERS = 0x800CE6A4
TRACK_FLAG = 0x800CE6A6
MODE = 0x800CE6B4
COUNTER = 0x800CE6BA
TRACK = 0x800CE6C0
LAPS = 0x800CE6E0
CURVE_SELECTOR = 0x800CE79C
CAR_CURSOR = 0x800CE7A4


def read_s16(client, address: int) -> int:
    value = int.from_bytes(client.read_mem(address, 2), "big")
    return value - 0x10000 if value >= 0x8000 else value


def write_s16(client, address: int, value: int) -> None:
    client.write_mem(address, int(value & 0xFFFF).to_bytes(2, "big"))


def request_race(client, car: int) -> None:
    # These are the same menu-side fields consumed by the ROM's state-7 finalizer.
    for address, value in (
        (PLAYERS, 1),
        (TRACK_FLAG, 1),
        (MODE, 2),
        (COUNTER, 0),
        (TRACK, 0),
        (LAPS, 3),
        (CAR_CURSOR, car),
    ):
        write_s16(client, address, value)
    write_s16(client, STATE, 7)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--ares", type=Path, default=ARES_EXE)
    parser.add_argument("--port", type=int, default=9150)
    parser.add_argument("--cars", type=int, nargs="+", default=[0, 3])
    args = parser.parse_args()

    with ares_session(
        rom=args.rom.resolve(),
        port=args.port,
        boot_wait=5.0,
        ares_exe=args.ares.resolve(),
    ) as client:
        for index, car in enumerate(args.cars):
            if not client.set_watchpoint(CURVE_SELECTOR, kind="write", size=2):
                raise RuntimeError("ares rejected the curve-selector write watchpoint")
            request_race(client, car)
            stop = client.continue_until_halt(timeout=30.0)
            info = client.parse_stop_reason(stop)
            cursor = read_s16(client, CAR_CURSOR)
            selector = read_s16(client, CURVE_SELECTOR)
            print(
                f"car={car} stop={info} cursor={cursor} selector={selector} "
                f"curve={'B' if selector == 0 else 'A'}"
            )
            client.clear_watchpoint(CURVE_SELECTOR, kind="write", size=2)
            if cursor != car or selector != car:
                raise RuntimeError(
                    f"selector copy mismatch: car={car}, cursor={cursor}, selector={selector}"
                )
            if index + 1 < len(args.cars):
                if not client.set_watchpoint(STATE, kind="write", size=2):
                    raise RuntimeError("ares rejected the state write watchpoint")
                state_stop = client.continue_until_halt(timeout=30.0)
                state = read_s16(client, STATE)
                print(f"transition stop={client.parse_stop_reason(state_stop)} state={state}")
                client.clear_watchpoint(STATE, kind="write", size=2)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
