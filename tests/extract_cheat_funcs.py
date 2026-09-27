"""Extract USA-ROM selector functions and timer block, retaining injected hooks.

The timer block is inside the non-returning boot dispatcher. Extract its
complete pause gate, countdown and both four-player clock loops; terminate
at the next dispatcher branch. Never check the generated output into git.
"""
import pathlib
import re
import sys

source, output = map(pathlib.Path, sys.argv[1:])
wanted = {"func_8003F40C", "func_8003F56C", "BootLoadInitialAssets"}
found = {}
for path in source.glob("funcs_*.c"):
    for match in re.finditer(
        r"^RECOMP_FUNC void (\w+)\(.*?(?=^RECOMP_FUNC void |\Z)",
        path.read_text(encoding="utf-8"), re.MULTILINE | re.DOTALL,
    ):
        if match[1] in wanted:
            if match[1] in found:
                raise RuntimeError(f"Duplicate function: {match[1]}")
            found[match[1]] = match[0]
if found.keys() != wanted:
    raise RuntimeError(f"Missing functions: {wanted - found.keys()}")
boot = found.pop("BootLoadInitialAssets")
timer_start = 0x800014B8
timer_end = 0x80001894
timer_hooks = (
    (0x80001558, "lambo_cheat_countdown"),
    (0x800015DC, "lambo_cheat_lap_timer"),
    (0x80001754, "lambo_cheat_current_lap_timer"),
)
assert all(timer_start <= address < timer_end for address, _ in timer_hooks), \
    "timer hook falls outside the extracted dispatcher region"
start_marker = f"    // 0x{timer_start:08X}:"
end_marker = f"    // 0x{timer_end:08X}:"
start = boot.index(start_marker)
end = boot.index(end_marker, start)
timer_block = boot[start:end]
for address, function in timer_hooks:
    marker = f"0x{address:08X}:"
    assert marker in timer_block, \
        f"timer hook instruction 0x{address:08X} is absent from the extracted region"
    assert function in timer_block and timer_block.index(function) < timer_block.index(marker), \
        f"{function} is not injected before 0x{address:08X} inside the extracted region"
found["timer"] = (
    "void cheat_test_timer(uint8_t* rdram, recomp_context* ctx) {\n"
    + timer_block + "    return;\n}\n"
)
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text('#include "recomp.h"\n#include "funcs.h"\n' +
                  "\n".join(found.values()), encoding="utf-8")
