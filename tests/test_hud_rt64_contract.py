"""Require a deliberate HUD hash-boundary audit when the RT64 gitlink changes."""
import subprocess
import unittest
from pathlib import Path


class HudRT64ContractTests(unittest.TestCase):
    def test_reviewed_rt64_pin(self):
        root = Path(__file__).resolve().parents[1]
        if not (root / ".git").exists():
            self.skipTest("RT64 gitlink audit requires a Git checkout; source archive has no .git")
        entry = subprocess.check_output(
            ["git", "ls-files", "--stage", "lib/rt64"], cwd=root, text=True)
        self.assertEqual(
            entry.split()[1], "f0728a2520d5aa735886240de3fee75cc805f6d6",
            "RT64 changed: audit State::flush sampleWidth/sampleHeight -> "
            "TMEMHasher::hash (including clampS/clampT) against docs/HUD.md "
            "before updating this reviewed pin.")


if __name__ == "__main__":
    unittest.main()
