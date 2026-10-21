"""Branch checks must catch committed mistakes hidden by a clean working tree."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from tools.check_branch import check, github_base


class BranchChecks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "core.autocrlf", "false")
        self.commit("base.txt", "base\n")
        self.base = self.git("rev-parse", "HEAD").strip()

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.root, text=True,
                                       stderr=subprocess.STDOUT)

    def commit(self, name, content):
        (self.root / name).write_bytes(content.encode("utf-8"))
        self.git("add", name)
        self.git("commit", "-qm", name)

    def test_finds_whitespace_in_earlier_commit(self):
        self.commit("bad.txt", "trailing space \n")
        self.commit("later.txt", "clean\n")
        self.git("diff", "--check")
        self.git("diff", "--check", "HEAD~1")
        with self.assertRaises(subprocess.CalledProcessError):
            check(self.base, cwd=self.root)

    def test_uses_merge_base_for_diverged_branches(self):
        self.commit("feature.txt", "clean\n")
        feature = self.git("rev-parse", "HEAD").strip()
        self.git("checkout", "--detach", self.base)
        self.commit("base-only.txt", "unrelated trailing space \n")
        self.assertEqual(check("HEAD", feature, cwd=self.root), self.base)

    def test_worktree_option_checks_index_and_unstaged_changes(self):
        path = self.root / "base.txt"
        path.write_text("bad \n")
        self.git("add", "base.txt")
        path.write_text("clean\n")
        with self.assertRaises(subprocess.CalledProcessError):
            check(self.base, worktree=True, cwd=self.root)
        self.git("reset", "--hard", "HEAD")
        path.write_text("bad \n")
        with self.assertRaises(subprocess.CalledProcessError):
            check(self.base, worktree=True, cwd=self.root)

    def test_new_branch_checks_from_empty_tree(self):
        check("0" * 40, cwd=self.root)
        self.commit("bad.txt", "bad \n")
        with self.assertRaises(subprocess.CalledProcessError):
            check("0" * 40, cwd=self.root)

    def test_first_branch_push_uses_default_branch_baseline(self):
        self.commit("existing.txt", "existing trailing space \n")
        baseline = self.git("rev-parse", "HEAD").strip()
        self.git("update-ref", "refs/remotes/origin/main", baseline)
        self.commit("feature.txt", "clean\n")
        event = {"before": "0" * 40, "repository": {"default_branch": "main"}}
        self.assertEqual(github_base(event), "origin/main")
        check(github_base(event), cwd=self.root)

    def test_event_selects_pr_base_or_entire_push_range(self):
        event = {"before": "old-push", "pull_request": {"base": {"sha": "pr-base"}}}
        self.assertEqual(github_base(event), "pr-base")
        self.assertEqual(github_base({"before": "old-push"}), "old-push")
