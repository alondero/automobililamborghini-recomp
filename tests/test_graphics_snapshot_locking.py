#!/usr/bin/env python3
"""Holds src/lambo_config.cpp to the graphics snapshot's thread contract (issue #265).

The settings Apply writes the GraphicsConfig snapshot on the render thread while
the main-thread pump reads it, so every access must hold g_graphics_snapshot_mutex.
tests/test_live_config.cpp exercises that across threads, but a torn read depends
on timing and that test can pass without the lock. This check enforces a
braced scope containing the guard followed by the snapshot accesses.
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
    """Return accesses outside the snapshot's braced locking pattern.

    The guard must be the first statement in the block. The snapshot accesses
    must follow it directly, before any further scope boundary.
    """
    unlocked = []
    previous = ""
    locked = False
    for line_number, line in enumerate(source.splitlines(), start=1):
        code = line.split("//", 1)[0].strip()
        if not code:
            continue
        if code == LOCK:
            locked = previous.endswith("{")
        elif SNAPSHOT in code and code != DEFINITION:
            if not locked or "{" in code or "}" in code:
                unlocked.append(line_number)
                locked = False
        else:
            locked = False
        previous = code
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

    read = f"return {SNAPSHOT}.ds_option;"
    require(unlocked_accesses(f"int f() {{\n{LOCK}\n{read}\n}}") == [],
            "the checker rejected a read in the guard's scope")
    require(unlocked_accesses(f"int f() {{\n{read}\n}}") == [2],
            "the checker accepted a read without a guard")
    require(unlocked_accesses(f"int f(bool take_lock) {{\nif (take_lock)\n{LOCK}\n{read}\n}}") == [4],
            "the checker accepted a read after a guard scoped to an unbraced if")
    require(unlocked_accesses(f"int f() {{\n{{\n{LOCK}\n}}\n{read}\n}}") == [5],
            "the checker accepted a read after the guard's block ended")


if __name__ == "__main__":
    main()
