"""Compile the patched ModMenu entry points against the host test's UI fakes.

The full widget needs a renderer. Keep the real refresh and update bodies so
the host test exercises their control flow without creating an RmlUi context.
"""

import re
import sys
from pathlib import Path


CPP_NON_CODE = re.compile(
    r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)


def extract_method(source, name):
    # Preserve offsets into the original text, but ignore braces and method
    # signatures in comments, character literals, normal strings and raw strings.
    code = CPP_NON_CODE.sub(lambda match: " " * len(match.group()), source)
    start = code.index(f"void ModMenu::{name}(")
    opening = code.index("{", start)
    depth = 0
    for end in range(opening, len(code)):
        depth += (code[end] == "{") - (code[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(f"Unterminated ModMenu::{name} method")


if __name__ == "__main__":
    source = Path(sys.argv[1]).read_text(encoding="utf-8-sig")
    Path(sys.argv[2]).write_text(
        "\n".join(extract_method(source, name)
                  for name in ("refresh_mods", "process_event")),
        encoding="utf-8",
    )
