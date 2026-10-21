"""A green unittest run must not hide a skipped required regression."""
import contextlib
import io
import unittest
from tools.run_host_tests import run


class RequiredChecksTests(unittest.TestCase):
    def run_case(self, required=(), require_all=False):
        class OptionalFixture(unittest.TestCase):
            @unittest.skip("missing optional fixture")
            def runTest(self):
                pass
        case = OptionalFixture()
        with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            return run(unittest.TestSuite([case]), required=required, require_all=require_all)

    def test_optional_skip_is_allowed(self):
        self.assertEqual(self.run_case(), 0)

    def test_missing_required_module_fails(self):
        self.assertEqual(self.run_case(required=("other.module",)), 1)

    def test_empty_suite_fails(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(run(unittest.TestSuite()), 1)

    def test_required_module_skip_fails(self):
        self.assertEqual(self.run_case(required=(__name__,)), 1)

    def test_strict_suite_skip_fails(self):
        self.assertEqual(self.run_case(require_all=True), 1)
