"""Describe functions whose port-side patches cannot be regenerated from ROM."""

import sys
import tomllib
from pathlib import Path


def protected_addresses(config, symbols):
    patches = config.get("patches", {})
    names = set(patches.get("stubs", []))
    for kind in ("hook", "instruction"):
        names.update(patch["func"] for patch in patches.get(kind, []))
    functions = {
        function["name"]: function["vram"]
        for section in symbols["section"]
        for function in section.get("functions", [])
    }
    missing = names - functions.keys()
    if missing:
        raise ValueError(f"Patched functions missing from symbols: {sorted(missing)}")
    return sorted({functions[name] for name in names})


def main():
    config_path, symbols_path, output_path = map(Path, sys.argv[1:])
    addresses = protected_addresses(
        tomllib.loads(config_path.read_text(encoding="utf-8")),
        tomllib.loads(symbols_path.read_text(encoding="utf-8")),
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        "// Generated from port patches; do not edit.\n"
        + "static constexpr uint32_t protected_mod_functions[] = {\n"
        + "".join(f"    0x{address:08X}u,\n" for address in addresses)
        + "};\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
