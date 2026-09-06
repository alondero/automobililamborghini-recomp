"""Extract complete generated ROM functions for the player-name integration test.

No ROM-derived C is checked in. Run N64Recomp first; hooks in its output are
retained verbatim so this test also verifies the production hook placement.
"""
import pathlib
import re
import sys

source, output = map(pathlib.Path, sys.argv[1:])
wanted = {"func_8002A228", "func_800401F0"}
found = {}
for path in source.glob("funcs_*.c"):
    for match in re.finditer(
        r"^RECOMP_FUNC void (\w+)\(.*?(?=^RECOMP_FUNC void |\Z)",
        path.read_text(), re.MULTILINE | re.DOTALL,
    ):
        if match[1] in wanted:
            if match[1] in found:
                raise RuntimeError(f"Duplicate function: {match[1]}")
            found[match[1]] = match[0]
if found.keys() != wanted:
    raise RuntimeError(f"Missing generated functions: {wanted - found.keys()}")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text('#include "recomp.h"\n#include "funcs.h"\n' +
                  "\n".join(found[name] for name in sorted(wanted)))
