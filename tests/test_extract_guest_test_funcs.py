"""Focused harnesses must retain compiled helpers without unrelated services."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("guest_extract", Path(__file__).with_name("extract_guest_test_funcs.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GuestExtractionTests(unittest.TestCase):
    def test_transitive_helpers_and_cycle(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "source.c", Path(directory) / "test.c"
            source.write_text('#include "funcs.h"\n'
                              'RECOMP_FUNC void root(void) { helper(); }\n'
                              'RECOMP_FUNC void unused(void) { unrelated_host(); }\n'
                              'RECOMP_FUNC void helper(void) { tail(); }\n'
                              'RECOMP_FUNC void tail(void) { helper(); host(); }\n', encoding="utf-8")
            module.extract(source, output, ["root"])
            selected = output.read_text(encoding="utf-8")
            self.assertIn('void root(', selected)
            self.assertIn('void helper(', selected)
            self.assertIn('void tail(', selected)
            self.assertIn('host();', selected)
            self.assertNotIn('unrelated_host', selected)
            self.assertTrue(selected.startswith('#include "funcs.h"'))
            with self.assertRaisesRegex(ValueError, "Missing guest roots"):
                module.extract(source, output, ["missing"])
