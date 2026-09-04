#!/usr/bin/env python3
"""Exercise a Python-produced Track Lab package through the native runtime."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from tests.test_track_lab import make_rdram
from tools import track_lab as tl


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(
            "usage: python -m tests.test_track_patch_interop "
            "<lambo_track_patch_tests>",
            file=sys.stderr,
        )
        return 2

    native_test = Path(argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary_directory:
        directory = Path(temporary_directory)
        snapshot_path = directory / "synthetic-rdram.bin"
        package_path = directory / "python-produced.altrk"

        snapshot_path.write_bytes(make_rdram())
        document = tl.extract_document(snapshot_path)
        rows = document["visibility"]["rows"]
        rows[0][1] = 2       # ordinary negative hole -> segment
        rows[0][3] = 1       # unusual -2 hole -> segment
        rows[0][4] = 0       # INT16_MIN hole -> segment
        rows[1][5] = None    # segment -> canonical -1 hole
        package_path.write_bytes(tl.compile_document(document))

        completed = subprocess.run(
            [str(native_test), "--python-interop", str(package_path)],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            if completed.stdout:
                print(completed.stdout, end="", file=sys.stderr)
            if completed.stderr:
                print(completed.stderr, end="", file=sys.stderr)
            return completed.returncode

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
