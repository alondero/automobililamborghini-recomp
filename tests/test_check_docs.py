from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_docs import check_source_doc_anchors  # noqa: E402

# Split so this file's own repository scan does not read fixtures as references.
DOCS = "docs" + "/"


class SourceDocAnchorTests(unittest.TestCase):
    def test_source_reference_to_missing_heading_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "docs").mkdir()
            (root / "docs" / "guide.md").write_text("# Guide\n\n## Native provenance\n", encoding="utf-8")
            source = root / "tool.py"
            source.write_text(f"# See {DOCS}guide.md#native-provenance.\n"
                              f"# Stale: {DOCS}guide.md#guest-bridge-contract.\n"
                              f"# Missing: {DOCS}absent.md#anything\n", encoding="utf-8")
            errors: list[str] = []
            check_source_doc_anchors(errors, [source], root)
            self.assertEqual(len(errors), 2, errors)
            self.assertIn(f"tool.py:2: broken documentation anchor: {DOCS}guide.md#guest-bridge-contract", errors[0])
            self.assertIn(f"tool.py:3: broken documentation link: {DOCS}absent.md", errors[1])

    def test_repository_sources_have_no_broken_documentation_anchors(self) -> None:
        errors: list[str] = []
        check_source_doc_anchors(errors)
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
