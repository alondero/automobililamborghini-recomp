"""Stage original ROM-free mod fixtures for the native live-discovery test."""
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


def manifest(mod_id, **extra):
    return {
        "game_id": "lamborghini", "id": mod_id, "display_name": mod_id,
        "authors": ["Test"], "version": "1.0.0",
        "minimum_recomp_version": "0.0.0", "enabled_by_default": False,
        **extra,
    }


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="lambo-live-mods-") as temporary:
        root = Path(temporary)
        mods = root / "mods"
        incoming = root / "incoming"
        mods.mkdir()
        incoming.mkdir()
        (root / "mod_config").mkdir()
        for name in ("base", "off"):
            folder = mods / name
            folder.mkdir()
            (folder / "mod.json").write_text(json.dumps(manifest(name)))
            (folder / "rt64.json").write_text("{}")
        (root / "mods.json").write_text(json.dumps({
            "enabled_mods": [], "mod_order": ["base", "off"],
        }))
        # Manifest-free RT64 archives default to enabled on a full startup scan.
        # Live discovery must leave them disabled until the player chooses them.
        with zipfile.ZipFile(incoming / "fresh.rtz", "w") as archive:
            archive.writestr("rt64.json", "{}")
        for name, extra in (
            ("future", {"minimum_recomp_version": "999.0.0"}),
            ("dependent", {"dependencies": ["base"]}),
            ("mode", {"custom_gamemode": True}),
            ("native", {"native_libraries": {"fixture": []}}),
            ("code", {}),
        ):
            for destination, prefix in ((mods, "startup_"), (incoming, "")):
                mod_id = prefix + name
                folder = destination / mod_id
                folder.mkdir()
                (folder / "mod.json").write_text(json.dumps(manifest(mod_id, **extra)))
                (folder / "rt64.json").write_text("{}")
                if name == "code":
                    (folder / "mod_syms.bin").write_bytes(b"code fixture")
        subprocess.run([sys.argv[1], str(root)], check=True)
