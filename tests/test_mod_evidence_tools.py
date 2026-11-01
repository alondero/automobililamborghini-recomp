"""Evidence tools must detect drift without changing the inspected checkout."""
import json
from pathlib import Path
import subprocess
import tempfile
import tomllib
import unittest

from scripts.gen_mod_pool_trace import ROOT, SITES, summarize_run, trace_config
from tools.verify_dependency_trees import verify_dependency


class DependencyTreeTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="lambo-evidence-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.repo = self.root / "dependency"
        self.git("init", "-q", str(self.repo), cwd=self.root)
        self.git("config", "user.name", "Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "core.autocrlf", "false")
        (self.repo / "base.txt").write_bytes(b"base\n")
        self.git("add", ".")
        self.git("-c", "commit.gpgsign=false", "commit", "-qm", "base")
        self.patch = self.root / "change.patch"
        self.patch.write_text(
            "diff --git a/base.txt b/base.txt\n--- a/base.txt\n+++ b/base.txt\n"
            "@@ -1 +1 @@\n-base\n+patched\n"
            "diff --git a/new.h b/new.h\nnew file mode 100644\n"
            "--- /dev/null\n+++ b/new.h\n@@ -0,0 +1 @@\n+header\n",
            encoding="utf-8", newline="\n")
        self.git("apply", str(self.patch))

    def git(self, *args, cwd=None):
        return subprocess.check_output(["git", *args], cwd=cwd or self.repo, stderr=subprocess.PIPE)

    def test_patch_created_untracked_file_matches_and_real_index_is_untouched(self):
        index = self.repo / ".git/index"
        before = index.read_bytes()
        report = verify_dependency(self.repo, [self.patch])
        self.assertTrue(report["worktree_matches"])
        self.assertEqual(index.read_bytes(), before)
        self.assertEqual((self.repo / "base.txt").read_bytes(), b"patched\n")
        self.assertEqual((self.repo / "new.h").read_bytes(), b"header\n")
        self.assertEqual(self.git("ls-files", "--others", "--exclude-standard").strip(), b"new.h")

    def test_extra_tracked_and_untracked_edits_are_detected_without_resetting_them(self):
        for name in ("base.txt", "extra.h"):
            with self.subTest(name=name):
                path = self.repo / name
                path.write_bytes(b"unpublished\n")
                self.assertFalse(verify_dependency(self.repo, [self.patch])["worktree_matches"])
                self.assertEqual(path.read_bytes(), b"unpublished\n")
                if name == "base.txt":
                    path.write_bytes(b"patched\n")

    def test_missing_checkout_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "not initialized"):
            verify_dependency(self.root / "missing", [self.patch])


class PoolDiagnosticTests(unittest.TestCase):
    def test_config_preserves_production_hooks_and_adds_only_audited_trace_sites(self):
        source = ROOT / "lamborghini.us.toml"
        before = source.read_bytes()
        original = tomllib.loads(before.decode("utf-8"))
        generated = tomllib.loads(trace_config(source))
        old_hooks = original["patches"]["hook"]
        self.assertEqual(generated["patches"]["hook"][:-3], old_hooks)
        self.assertEqual({hook["before_vram"]: hook["func"]
                          for hook in generated["patches"]["hook"][-3:]}, SITES)
        self.assertTrue(all(Path(generated["input"][key]).is_absolute()
                            for key in ("output_func_path", "symbols_file_path", "rom_file_path")))
        self.assertEqual(source.read_bytes(), before)

    def test_successful_gameplay_without_diagnostic_markers_cannot_be_count_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary)
            (artifact / "runner-result.json").write_text(json.dumps({
                "outcome": "passed", "scenario": "mod-pool-callers"}), encoding="utf-8")
            (artifact / "harness-result.json").write_text(json.dumps({
                "exit_code": 0, "outcome": "passed", "replay": {"complete": True}}), encoding="utf-8")
            (artifact / "stdout.log").write_text("ordinary game run\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "diagnostic build"):
                summarize_run(artifact)


if __name__ == "__main__":
    unittest.main()
