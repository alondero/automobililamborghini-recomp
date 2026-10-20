"""Restart the real save/load fixture repeatedly; no RAM/cache survives a child."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def main():
    helper = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="lambo-fog-state-") as directory:
        root = Path(directory)
        for players in (1, 2):
            previous = root / f"state-{players}p-0.lstate"
            subprocess.run([str(helper), "write", str(previous), str(players)], check=True, timeout=20)
            for cycle in range(1, 5):
                current = root / f"state-{players}p-{cycle}.lstate"
                subprocess.run([str(helper), "cycle", str(previous), str(current)], check=True, timeout=20)
                previous = current
            subprocess.run([str(helper), "default", str(previous)], check=True, timeout=20)
        # Save payloads retain authored coefficients, never a progressively
        # scaled value. The 32-byte v2 header precedes 8 MiB RAM and the field extension.
        blob = previous.read_bytes()
        assert len(blob) == 32 + 0x800000 + 0x24000
        assert struct.unpack_from("<I", blob, 20)[0] == 0x31474F46  # FOG1 marker
        assert struct.unpack_from("<I", blob, 32 + 0xBF404)[0] == 0x64009D00
        # Legacy files cannot say whether a fog value is already corrected.
        # Non-default density must fail before mutating RAM, for both 1P/2P.
        legacy_blob = bytearray(blob)
        struct.pack_into("<I", legacy_blob, 20, 0)
        struct.pack_into("<I", legacy_blob, 32 + 0xBF404, 0x3200CE80)
        legacy = root / "legacy.lstate"
        for players in (1, 2):
            struct.pack_into("<I", legacy_blob, 32 + 0xCE6A4, players << 16)
            legacy.write_bytes(legacy_blob)
            subprocess.run([str(helper), "reject", str(legacy)], check=True, timeout=20)
        subprocess.run([str(helper), "legacy-default", str(legacy)], check=True, timeout=20)
        old_version = root / "v1.lstate"
        old_blob = bytearray(blob[:32 + 0x800000])
        struct.pack_into("<I", old_blob, 8, 1)
        old_version.write_bytes(old_blob)
        subprocess.run([str(helper), "reject", str(old_version)], check=True, timeout=20)
        truncated = root / "truncated.lstate"
        truncated.write_bytes(blob[:-1])
        subprocess.run([str(helper), "reject", str(truncated)], check=True, timeout=20)
        print("Four fresh-process save/load cycles per 1P/2P layout preserve fog 0x3200ce80.")


if __name__ == "__main__":
    main()
