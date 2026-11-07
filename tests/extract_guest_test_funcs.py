"""Isolate compiled guest roots and their local helpers for a parity harness.

The full generated translation unit intentionally contains all patches. Focused
tests must not supply fake services for unrelated patches. This selects complete
generated definitions without rewriting instructions or editing generated files
in place; direct calls to ROM/host services remain supplied by each harness.
"""
from pathlib import Path
import re
import sys


def extract(source, output, roots):
    text = Path(source).read_text(encoding="utf-8")
    matches = list(re.finditer(r"^RECOMP_FUNC void (\w+)\(.*?(?=^RECOMP_FUNC void |\Z)",
                              text, re.MULTILINE | re.DOTALL))
    functions = {match[1]: match[0] for match in matches}
    if len(functions) != len(matches) or not matches:
        raise ValueError("Missing or duplicate guest definitions")
    missing = set(roots) - functions.keys()
    if missing:
        raise ValueError(f"Missing guest roots: {sorted(missing)}")
    selected = set(roots)
    pending = list(roots)
    while pending:
        function = pending.pop()
        calls = set(re.findall(r"\b(\w+)\s*\(", functions[function])) & functions.keys()
        for callee in calls - selected:
            selected.add(callee)
            pending.append(callee)
    header = text[:matches[0].start()]
    Path(output).write_text(header + "\n".join(body for name, body in functions.items() if name in selected),
                            encoding="utf-8")


if __name__ == "__main__":
    extract(sys.argv[1], sys.argv[2], sys.argv[3:])
