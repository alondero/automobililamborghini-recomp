#!/usr/bin/env python3
"""Holds src/lambo_config.cpp to the graphics snapshot's thread contract (issue #265).

The settings Apply writes the GraphicsConfig snapshot on the render thread while
the main-thread pump reads it, so every access must hold g_graphics_snapshot_mutex.
tests/test_live_config.cpp exercises that across threads, but a torn read depends
on timing and that test can pass without the lock. This check fails for certain
when an access is added outside the lock.
"""

import sys
from pathlib import Path

try:  # unittest discovery imports this as tests.test_graphics_snapshot_locking
    from tests.test_quit_path import require
except ImportError:  # ctest runs it as a script from tests/
    from test_quit_path import require

SNAPSHOT = "g_current_graphics"
DEFINITION = "ultramodern::renderer::GraphicsConfig g_current_graphics{};"
LOCK = "std::lock_guard<std::mutex> lock(g_graphics_snapshot_mutex);"


def unlocked_accesses(source: str) -> list[int]:
    """Return the 1-based lines that touch the snapshot without the lock.

    An access counts as locked when the lock guard is the statement directly
    above it, or directly above a run of consecutive snapshot statements.
    """
    lines = [line.strip() for line in source.splitlines()]
    unlocked = []
    for index, line in enumerate(lines):
        code = line.split("//", 1)[0]
        if SNAPSHOT not in code or code == DEFINITION:
            continue
        above = index - 1
        while above >= 0 and SNAPSHOT in lines[above].split("//", 1)[0]:
            above -= 1
        if above < 0 or lines[above] != LOCK:
            unlocked.append(index + 1)
    return unlocked


def main() -> None:
    root = Path(sys.argv[1]).resolve()
    source = (root / "src" / "lambo_config.cpp").read_text(encoding="utf-8")

    require(source.count(DEFINITION) == 1,
            "lambo_config.cpp must define the graphics snapshot exactly once")
    require(source.count(LOCK) >= 1, "the graphics snapshot lock is never taken")
    unlocked = unlocked_accesses(source)
    require(not unlocked,
            f"{SNAPSHOT} is used without g_graphics_snapshot_mutex at "
            f"src/lambo_config.cpp line(s) {unlocked}")

    # The checker itself must reject the unlocked form it exists to catch.
    require(unlocked_accesses(f"int f() {{\nreturn {SNAPSHOT}.ds_option;\n}}\n") == [2],
            "the checker no longer detects an unlocked snapshot read")


if __name__ == "__main__":
    main()
