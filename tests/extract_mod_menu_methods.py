"""Compile the patched ModMenu entry points against the host test's UI fakes.

The full widget needs a renderer. Keep the real refresh and update bodies so
the host test exercises their control flow without creating an RmlUi context.
"""

import sys
from pathlib import Path


def extract_method(source, name):
    start = source.index(f"void ModMenu::{name}(")
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


if __name__ == "__main__":
    source = Path(sys.argv[1]).read_text(encoding="utf-8-sig")
    Path(sys.argv[2]).write_text(
        "\n".join(extract_method(source, name)
                  for name in ("refresh_mods", "process_event")),
        encoding="utf-8",
    )
