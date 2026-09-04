"""Lightweight contract tests for the dependency-free Track Lab browser UI."""

from __future__ import annotations

from html.parser import HTMLParser
import json
from pathlib import Path
import re
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "tools" / "track_lab_web"


class _PageParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.ids: list[str] = []
        self.scripts: list[str] = []
        self.stylesheets: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        if values.get("id"):
            self.ids.append(values["id"] or "")
        if tag == "script" and values.get("src"):
            self.scripts.append(values["src"] or "")
        if tag == "link" and values.get("rel") == "stylesheet":
            self.stylesheets.append(values.get("href") or "")


class TrackLabWebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.html = (WEB / "index.html").read_text(encoding="utf-8")
        cls.js = (WEB / "app.js").read_text(encoding="utf-8")
        cls.css = (WEB / "style.css").read_text(encoding="utf-8")

    def test_page_is_self_contained_and_has_unique_ids(self) -> None:
        parser = _PageParser()
        parser.feed(self.html)
        self.assertEqual(parser.scripts, ["app.js"])
        self.assertEqual(parser.stylesheets, ["style.css"])
        self.assertEqual(len(parser.ids), len(set(parser.ids)))
        self.assertNotRegex(self.html + self.css, r"https?://")

    def test_scope_limit_is_prominent_and_specific(self) -> None:
        self.assertIn("stock visibility rows only", self.html)
        for unsupported in ("Arbitrary playable tracks", "geometry", "collision"):
            self.assertIn(unsupported, self.html)

    def test_v1_visibility_contract_and_fallbacks_are_explicit(self) -> None:
        self.assertIn('DOCUMENT_FORMAT = "al-track-document"', self.js)
        self.assertIn("DOCUMENT_VERSION = 1", self.js)
        self.assertIn("visibility.rows", self.js)
        self.assertIn("visibility.base_rows", self.js)
        self.assertIn("visibility_rows", self.js)
        self.assertIn('fetch("/track.json"', self.js)

    def test_editor_is_fixed_to_ten_slots_and_supports_holes(self) -> None:
        self.assertRegex(self.js, r"const SLOT_COUNT = 10;")
        self.assertIn('trimmed === "" || trimmed === "-1"', self.js)
        self.assertIn("row.map((value) => value === null || value === -1 ? null : value)", self.js)
        self.assertIn("findIndex((value) => value === null || value === -1)", self.js)

    def test_exporter_schema_coordinates_and_raw_fields_are_supported(self) -> None:
        # These are the exact decoded keys emitted by tools/track_lab.py.
        for key in ("world_x", "world_z", "plane_a", "plane_b", "raw_be_hex"):
            self.assertIn(key, self.js)
        self.assertRegex(self.js, r"Array\.isArray\(value\?\.records\)")

    def test_waypoint_hypothesis_is_not_presented_as_confirmed_topology(self) -> None:
        self.assertIn("Waypoint hypothesis", self.html)
        self.assertIn('id="coordinateWarning"', self.html)
        self.assertIn("assumption_warning", self.js)
        self.assertIn("drawRoute(fitted, !showingWaypointHypothesis)", self.js)
        self.assertIn("if (closeLoop && ordered.length > 2)", self.js)

    def test_required_interactions_and_responsive_accessibility_exist(self) -> None:
        for behavior in (
            "addReferenceToFirstHole",
            "resetSelectedRow",
            "function undo()",
            "function redo()",
            "validateDocument",
            "downloadDocument",
            "handleGlobalShortcut",
        ):
            self.assertIn(behavior, self.js)
        self.assertIn('role="img"', self.html)
        self.assertIn('aria-live="polite"', self.html)
        self.assertGreaterEqual(len(re.findall(r"@media \(max-width:", self.css)), 3)
        self.assertIn("prefers-reduced-motion", self.css)

    @unittest.skipUnless(shutil.which("node"), "Node.js is required for browser validator tests")
    def test_browser_validator_rejects_tampered_immutable_evidence(self) -> None:
        app_path = json.dumps(str(WEB / "app.js"))
        script = f"""
const {{ validateTrackDocumentEvidence, fnv1a64Pvs }} = require({app_path});
const rawRows = [
  [0, 1, -1, -2, -3, -4, -5, -6, -7, -8],
  [1, 0, -1, -1, -1, -1, -1, -1, -1, -1],
];
function makeDocument() {{
  const documentRawRows = rawRows.map(row => row.slice());
  const documentBaseRows = documentRawRows.map(row => row.map(value => value < 0 ? null : value));
  return {{
    format: "al-track-document",
    version: 1,
    target: {{
      game_id: "lamborghini.us",
      rom_xxh3_64: "525201d7279f34e3",
      circuit: 0,
    }},
    capabilities: {{
      editable: ["visibility"],
      inspect_only: ["segments", "anchors", "waypoints"],
      unsupported: ["geometry", "collision", "new_track"],
    }},
    visibility: {{
      row_count: 2,
      slots_per_row: 10,
      base_fnv1a64: "f37c114a970dcf9d",
      base_rows: documentBaseRows,
      raw_base_rows: documentRawRows,
      rows: documentBaseRows.map(row => row.slice()),
    }},
  }};
}}
function check(mutator) {{
  const document = makeDocument();
  mutator(document);
  return validateTrackDocumentEvidence(document);
}}
const untouched = makeDocument();
const before = JSON.stringify(untouched);
const valid = validateTrackDocumentEvidence(untouched);
const result = {{
  hash: fnv1a64Pvs(rawRows),
  unchanged: before === JSON.stringify(untouched),
  valid,
  missingTarget: check(doc => delete doc.target),
  wrongRom: check(doc => doc.target.rom_xxh3_64 = "0000000000000000"),
  extraTargetField: check(doc => doc.target.name = "not part of v1"),
  badCapabilities: check(doc => doc.capabilities.editable = ["geometry"]),
  extraCapabilityField: check(doc => doc.capabilities.geometry = ["editable"]),
  shortBaseRow: check(doc => doc.visibility.base_rows[0].pop()),
  invalidBaseValue: check(doc => doc.visibility.base_rows[0][0] = -1),
  shortRawRow: check(doc => doc.visibility.raw_base_rows[0].pop()),
  invalidRawValue: check(doc => doc.visibility.raw_base_rows[0][0] = 32768),
  inconsistentRaw: check(doc => doc.visibility.raw_base_rows[0][2] = 0),
  wrongHash: check(doc => doc.visibility.base_fnv1a64 = "0000000000000000"),
  extraVisibilityField: check(doc => doc.visibility.note = "not part of v1"),
  malformedWorkingRow: check(doc => doc.visibility.rows[0][0] = "0"),
}};
process.stdout.write(JSON.stringify(result));
"""
        completed = subprocess.run(
            [shutil.which("node") or "node", "-e", script],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        result = json.loads(completed.stdout)

        self.assertEqual(result["hash"], "f37c114a970dcf9d")
        self.assertTrue(result["unchanged"])
        self.assertEqual(result["valid"], [])
        for case in (
            "missingTarget",
            "wrongRom",
            "extraTargetField",
            "badCapabilities",
            "extraCapabilityField",
            "shortBaseRow",
            "invalidBaseValue",
            "shortRawRow",
            "invalidRawValue",
            "inconsistentRaw",
            "wrongHash",
            "extraVisibilityField",
            "malformedWorkingRow",
        ):
            with self.subTest(case=case):
                self.assertTrue(result[case], f"{case} was accepted")


if __name__ == "__main__":
    unittest.main()
