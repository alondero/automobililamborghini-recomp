"""Compile independently pinned USA selector bodies with frozen old cheat hooks.

No production config is consumed: removal of production originals/hooks cannot
silently change this oracle. ROM bytes and generated output stay build-local.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ROM_SHA256 = "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c"
BODIES = (
    ("func_8003F40C", 0x3F40C, "a00cf9d791a4a2d7ce7c08431521c9f809a325310b95f1307375aaa8c3bff0a5"),
    ("func_8003F56C", 0x3F56C, "c069da915d36bc010f003fb23a3d35954d615233152c25e659b9854eeac57ca5"),
)


def build(recompiler, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    rom_path = ROOT / "Automobili Lamborghini (USA).z64"
    rom = rom_path.read_bytes()
    assert hashlib.sha256(rom).hexdigest() == ROM_SHA256, "Wrong USA ROM"
    for _, offset, digest in BODIES:
        assert hashlib.sha256(rom[offset:offset + 0x158]).hexdigest() == digest
        assert struct.unpack_from(">II", rom, offset + 0x150) == (0x03E00008, 0)
    # Descriptor data is asset-loaded, not mapped by the code's linear rule.
    categories = [struct.unpack_from(">h", rom, 0xAEE80 + model * 24)[0] for model in range(24)]
    assert categories == [category for category in (0, 4, 1, 2, 3, 5, 6, 7) for _ in range(3)]
    (output / "selector_categories.inc").write_text(
        "static constexpr int categories[] = {" + ",".join(map(str, categories)) + "};\n",
        encoding="utf-8")
    syms = output / "symbols.toml"
    syms.write_text('[[section]]\nname=".text"\nrom=4096\nvram=2147484672\nsize=552960\nfunctions=[\n' +
        ",\n".join('{name=' + json.dumps(name) + f',vram={offset + 0x80000000 - 0xC00},size=344' + '}'
                    for name, offset, _ in BODIES) + '\n]\n', encoding="utf-8")
    config = output / "selector_reference.toml"
    text = "[input]\n" + "\n".join(f"{k}={json.dumps(str(v))}" for k, v in {
        "symbols_file_path": syms.as_posix(), "rom_file_path": rom_path.as_posix(),
        "output_func_path": output.as_posix(),
    }.items()) + "\nsingle_file_output=true\n"
    for name, pc, reg in (("func_8003F40C", 0x8003E8AC, 12), ("func_8003F40C", 0x8003E954, 11),
                          ("func_8003F56C", 0x8003EA0C, 11), ("func_8003F56C", 0x8003EAB4, 25)):
        code = '{ extern int selector_reference_enabled(void); if (selector_reference_enabled()) ctx->r' + str(reg) + ' = 1; }'
        text += f'[[patches.hook]]\nfunc="{name}"\nbefore_vram={pc}\ntext={json.dumps(code)}\n'
    config.write_text(text, encoding="utf-8")
    subprocess.run([str(recompiler), str(config)], check=True)
    code = (output / ".c").read_text(encoding="utf-8")
    for name, _, _ in BODIES:
        assert code.count("RECOMP_FUNC void " + name + "(") == 1
        code = code.replace("RECOMP_FUNC void " + name + "(", "RECOMP_FUNC void reference_" + name + "(")
    (output / "selector_reference.c").write_text(code, encoding="utf-8")


if __name__ == "__main__":
    build(Path(sys.argv[1]).resolve(), sys.argv[2])
