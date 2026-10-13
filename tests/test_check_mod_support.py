import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("check_mod_support", ROOT / "tools/check_mod_support.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class ModSupportTests(unittest.TestCase):
    def test_texture_staging_preserves_archive_and_isolates_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "original-name.rtz"
            # Opaque data proves the checker does not unzip or rewrite RT64 packs.
            package.write_bytes(b"opaque RT64 archive")
            profiles = []

            def run(command, *, cwd, env, **kwargs):
                profiles.append(cwd)
                config = cwd / "LamborghiniRecomp"
                self.assertEqual((config / "mods/texture_smoke.rtz").read_bytes(), package.read_bytes())
                self.assertEqual(json.loads((config / "mods.json").read_text()), {
                    "enabled_mods": ["texture_smoke"], "mod_order": ["texture_smoke"],
                })
                self.assertEqual(env["LAMBO_HEADLESS"], "1")
                self.assertEqual(env["LAMBO_LAUNCHER"], "0")
                self.assertNotIn("LAMBO_PORTABLE", env)
                self.assertNotIn("LAMBO_TEXTURE_PACK", env)
                for key in ("LOCALAPPDATA", "XDG_CONFIG_HOME", "XDG_STATE_HOME"):
                    self.assertEqual(env[key], str(cwd))
                self.assertEqual(command, [str(root / "game.exe"), str(root / "game.z64"), "--console", "--verbose"])
                self.assertEqual(kwargs["timeout"], 40)
                return subprocess.CompletedProcess(command, 0, b"boot", b"")

            with patch.dict(os.environ, {"LAMBO_PORTABLE": "1", "LAMBO_TEXTURE_PACK": "user-pack"}), patch.object(checker.subprocess, "run", side_effect=run):
                self.assertEqual(checker.run_case(root / "game.exe", package, root / "game.z64", True, texture=True), (0, "boot"))
            self.assertFalse(profiles[0].exists())

    def test_nrm_manifest_mutation_and_enabled_state_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "smoke.nrm"
            with zipfile.ZipFile(package, "w") as archive:
                archive.writestr("mod.json", json.dumps({"id": "code_smoke", "minimum_recomp_version": "0.5.0"}))
                archive.writestr("code.bin", b"fixture")

            def run(command, *, cwd, **kwargs):
                config = cwd / "LamborghiniRecomp"
                with zipfile.ZipFile(config / "mods/smoke.nrm") as archive:
                    self.assertEqual(json.loads(archive.read("mod.json"))["minimum_recomp_version"], "999.0.0")
                    self.assertEqual(archive.read("code.bin"), b"fixture")
                self.assertEqual(json.loads((config / "mods.json").read_text())["enabled_mods"], [])
                return subprocess.CompletedProcess(command, 2, b"", b"rejected")

            with patch.object(checker.subprocess, "run", side_effect=run):
                self.assertEqual(checker.run_case(root / "game", package, root / "rom", False, "999.0.0"), (2, "rejected"))
            with zipfile.ZipFile(package) as archive:
                self.assertEqual(json.loads(archive.read("mod.json"))["minimum_recomp_version"], "0.5.0")

    def run_cli(self, root, texture_result=None):
        results = [(0, checker.MARKER + "\ngame thread #1 started"),
                   (0, "game thread #1 started"), (2, "Error loading mods:")]
        args = ["checker", "--exe", str(root / "game"), "--package", str(root / "smoke.nrm"),
                "--rom", str(root / "rom"), "--output", str(root / "logs")]
        if texture_result is not None:
            args += ["--texture-package", str(root / "pack.rtz")]
            results.append(texture_result)
        with patch.object(sys, "argv", args), patch.object(checker, "run_case", side_effect=results) as run, contextlib.redirect_stdout(io.StringIO()):
            result = checker.main()
        return result, run

    def test_texture_requires_loaded_identity_and_successful_boot(self):
        good = "Loading mod texture_smoke\ngame thread #1 started\n"
        for code, output, expected in [
            (0, good, 0),
            (0, "Skipping non-mod texture_smoke.rtz\ngame thread #1 started", 1),
            (0, "game thread #1 started", 1),
            (0, "Loading mod texture_smoke", 1),
            (0, good.replace("texture_smoke", "texture_smoke_other"), 1),
            (2, good, 1),
            (0, good + "Skipping non-mod broken.rtz", 1),
            (0, good + "Error loading mods:", 1),
        ]:
            with self.subTest(code=code, output=output), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                result, run = self.run_cli(root, (code, output))
                self.assertEqual(result, expected)
                self.assertEqual(run.call_args.kwargs, {"texture": True})
                self.assertEqual((root / "logs/texture-pack.log").read_text(), output)

    def test_texture_is_optional(self):
        with tempfile.TemporaryDirectory() as directory:
            result, run = self.run_cli(Path(directory))
            self.assertEqual(result, 0)
            self.assertEqual(run.call_count, 3)

    def test_portable_executable_is_rejected_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "portable.txt").touch()
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                self.run_cli(root, (0, "unused"))
            self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
