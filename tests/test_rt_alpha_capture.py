from __future__ import annotations

import copy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_rt_alpha_capture import validate_alpha_results, validate_native_clip
from inspect_rt_task import CaptureError


class NativeAlphaCaptureTests(unittest.TestCase):
    def setUp(self):
        self.render = {
            "calls": [{"call": 7, "color_address": 77}],
            "raster": [{"call": 7, "draw_index": 3, "first": 9, "count": 6,
                        "indexed": True, "test_z": False, "omitted": False}],
            "sun_shadow_rejected_ranges": [{"draw": 7, "reason": 64}],
        }
        self.evidence = dict(measured=True, error="", stride=112, faces=2,
                             color_address=77, target_extent=[8, 4],
                             tested_pixels=20, native_covered=16, evaluated_covered=16,
                             disagreements=0, unsupported_faces=0, no_interior_faces=0,
                             ray_query=False, edge_pixels=False, alpha_admitted=False)
        self.records = [[3, face, 10, 8, 8, 0, 0, 0xFFFFFFFF] + [0.0] * 20 for face in (9, 12)]

    def raw(self, records=None):
        return b"".join(struct.pack("<8I20f", *record) for record in (records or self.records))

    def test_complete_interior_evidence_never_admits_alpha_or_edges(self):
        result = validate_alpha_results(self.render, self.evidence, self.raw())
        self.assertTrue(result["interior_parity"])
        self.assertTrue(result["complete_face_evidence"])
        for key in ("alpha_admitted", "ray_query", "edge_pixels"):
            self.assertFalse(result[key])
            evidence = dict(self.evidence, **{key: True})
            with self.assertRaises(CaptureError):
                validate_alpha_results(self.render, evidence, self.raw())

    def test_wrong_face_draw_or_framebuffer_cannot_authenticate(self):
        for change in ("draw", "face", "duplicate", "framebuffer", "missing"):
            with self.subTest(change=change):
                records, evidence = copy.deepcopy(self.records), dict(self.evidence)
                if change == "draw":
                    records[0][0] = 7
                elif change == "face":
                    records[0][1] = 10
                elif change == "duplicate":
                    records[1][1] = 9
                elif change == "framebuffer":
                    evidence["color_address"] = 78
                else:
                    records.pop()
                with self.assertRaises(CaptureError):
                    validate_alpha_results(self.render, evidence, self.raw(records))

    def test_native_pixel_domain_cannot_be_misrepresented_as_ray_evidence(self):
        for domain in ("raster_pixel_center", "raster_pixel_center_double_uv"):
            self.evidence["input_domain"] = domain
            result = validate_alpha_results(self.render, self.evidence, self.raw())
            self.assertEqual(result["input_domain"], domain)
            self.assertFalse(result["ray_query"])
            self.assertFalse(result["alpha_admitted"])
        self.evidence["input_domain"] = "authenticated_ray_hit"
        with self.assertRaises(CaptureError):
            validate_alpha_results(self.render, self.evidence, self.raw())

    def test_gpu_disagreement_requires_a_bounded_pixel_and_matching_counts(self):
        self.records[0][4:8] = [7, 1, 0, 19]
        self.evidence.update(evaluated_covered=15, disagreements=1)
        result = validate_alpha_results(self.render, self.evidence, self.raw())
        self.assertFalse(result["interior_parity"])
        self.assertEqual(result["mismatches"][0]["first_pixel"], [3, 2])
        for change in ("pixel", "summary", "count", "nan", "extent"):
            with self.subTest(change=change):
                records, evidence = copy.deepcopy(self.records), dict(self.evidence)
                if change == "pixel":
                    records[0][7] = 32
                elif change == "summary":
                    evidence["disagreements"] = 0
                elif change == "count":
                    records[0][3] = 11
                elif change == "nan":
                    records[0][8] = float("nan")
                else:
                    evidence["target_extent"] = [True, 4]
                with self.assertRaises(CaptureError):
                    validate_alpha_results(self.render, evidence, self.raw(records))

    def test_unsupported_and_untested_faces_are_explicit_partial_evidence(self):
        self.records[0][2:7] = [0, 0, 0, 0, 1]
        self.evidence.update(tested_pixels=10, native_covered=8, evaluated_covered=8,
                             unsupported_faces=1, no_interior_faces=1)
        result = validate_alpha_results(self.render, self.evidence, self.raw())
        self.assertTrue(result["interior_parity"])
        self.assertFalse(result["complete_face_evidence"])
        self.assertFalse(result["alpha_admitted"])
        for record in self.records:
            record[2:7] = [0, 0, 0, 0, 1]
        self.evidence.update(tested_pixels=0, native_covered=0, evaluated_covered=0,
                             unsupported_faces=2, no_interior_faces=2)
        self.assertFalse(validate_alpha_results(self.render, self.evidence, self.raw())["interior_parity"])

    def test_actual_vertex_outputs_require_the_original_presented_face(self):
        self.render["indices"] = [0] * 9 + [0, 1, 2] * 2
        screen = struct.pack("<12f", 1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6)
        clip = struct.pack("<12f", 10, 11, 12, 4, 20, 21, 22, 5, 30, 31, 32, 6) * 2
        self.assertEqual(validate_native_clip(self.render, self.raw(), clip, screen), 2)
        for corrupt in (clip[:-1], bytes(len(clip)), struct.pack("<f", float("nan")) + clip[4:],
                        clip[:12] + struct.pack("<f", 5) + clip[16:]):
            with self.assertRaises(CaptureError):
                validate_native_clip(self.render, self.raw(), corrupt, screen)


if __name__ == "__main__":
    unittest.main()
