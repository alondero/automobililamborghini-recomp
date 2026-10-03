from __future__ import annotations

import copy
import math
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_rt_overlay_capture import filter_taps, inside
from check_rt_render_capture import (area, belongs_to_physical_car, opaque_coverage, receiver_material, validate_overlay_count,
                                     screen_face, topology, uncovered_area, validate_pair)
from inspect_rt_task import CaptureError


def material() -> dict:
    return {"alpha_compare": 0, "coverage_times_alpha": False, "alpha_blend": False,
            "force_blend": False, "z_compare": True, "z_update": True, "z_mode": 0, "z_source": 0,
            "other_lo": 0xC8112230, "other_hi": 0x18ACFF, "combine_w0": 0xFC26A004,
            "combine_w1": 0x1FFC93F8, "standard_fog": True, "fog_rgba": [.2, .3, .4, 1]}


class MaterialTests(unittest.TestCase):
    def test_physical_car_components_follow_authenticated_parent_chain(self):
        objects = [{"flags": 0x9, "parent": -1}, {"flags": 0x26, "parent": 0}]
        self.assertTrue(belongs_to_physical_car({"index": 0}, objects))
        self.assertTrue(belongs_to_physical_car({"index": 1}, objects))
        self.assertFalse(belongs_to_physical_car({"index": 2}, objects))
        objects.extend([{"flags": 0, "parent": 3}, {"flags": 0, "parent": 2}])
        self.assertFalse(belongs_to_physical_car({"index": 2}, objects))

    def test_generic_material_capture_does_not_claim_a_c1_overlay(self):
        validate_overlay_count(0, require_overlay=False)
        with self.assertRaises(CaptureError):
            validate_overlay_count(0)
        validate_overlay_count(16)
        validate_overlay_count(32)
        validate_overlay_count(96)
        with self.assertRaises(CaptureError):
            validate_overlay_count(17)

    def test_cutout_blend_and_depth_paths_are_excluded(self):
        self.assertTrue(opaque_coverage(material()))
        for field, value in (("alpha_compare", 1), ("coverage_times_alpha", True),
                             ("alpha_blend", True), ("force_blend", True), ("z_compare", False),
                             ("z_update", False), ("z_mode", 2), ("z_source", 1)):
            with self.subTest(field=field):
                self.assertFalse(opaque_coverage(dict(material(), **{field: value})))

    def test_receiver_requires_measured_shader_and_finite_fog(self):
        call = {"material": material(), "shader_other_lo": 0xC8112230,
                "shader_other_hi": 0x18ACFF, "shader_flags": 0}
        self.assertTrue(receiver_material(call))
        for field, value in (("combine_w0", 0), ("standard_fog", False),
                             ("fog_rgba", [0, 0, math.nan, 1]), ("fog_rgba", [0, 0, 2, 1])):
            with self.subTest(field=field):
                changed = copy.deepcopy(call)
                changed["material"][field] = value
                self.assertFalse(receiver_material(changed))
        for field, value in (("shader_other_lo", 0), ("shader_flags", 1 << 29), ("shader_flags", 1 << 30)):
            self.assertFalse(receiver_material(dict(call, **{field: value})))

    def test_edge_incidence_is_diagnostic_not_caster_policy(self):
        report = topology([[[0, 0, 0], [1, 0, 0], [0, 1, 0]]])
        self.assertEqual(report["boundary_edges"], 3)
        self.assertNotIn("closed_solid", report)


class CoverageTests(unittest.TestCase):
    def test_union_overlaps_holes_and_empty_clips(self):
        square = [[0, 0], [2, 0], [2, 2], [0, 2]]
        left = [[0, 0], [1.5, 0], [1.5, 2], [0, 2]]
        right = [[.5, 0], [2, 0], [2, 2], [.5, 2]]
        self.assertAlmostEqual(uncovered_area(square, [left, right]), 0)
        self.assertAlmostEqual(uncovered_area(square, [left, left]), 1)
        self.assertAlmostEqual(uncovered_area(square, [[], [[0, 0], [1, 0]]]), 4)
        self.assertAlmostEqual(uncovered_area(square, [list(reversed(left))]), 1)
        self.assertFalse(inside(.5, .5, []))
        self.assertFalse(inside(.5, .5, [[0, 0], [1, 1]]))

    def test_crossing_triangle_keeps_its_visible_receiver_footprint(self):
        draw = {"resolution": [100, 100], "viewport": [0, 0, 100, 100],
                "screen_scale": [1, 1], "screen_offset": [0, 0]}
        face = [[20, 20, -.5, 1], [80, 20, .5, 1], [50, 80, .5, 1]]
        clipped = screen_face(draw, [0, 1, 2], face)
        self.assertAlmostEqual(area(clipped), 1350)
        self.assertAlmostEqual(area(screen_face(draw, [0, 1, 2], face, flags=2)), 1800)
        self.assertEqual(screen_face(draw, [0, 1, 2], [[20, 20, 1, 1]] * 3), [])
        self.assertEqual(screen_face(draw, [0, 1, 2], [[20, 20, .5, -1]] * 3), [])
        far = screen_face(draw, [0, 1, 2], [[20, 20, 1.5, 1], [80, 20, .5, 1], [50, 80, .5, 1]])
        self.assertGreater(area(far), 0)
        self.assertLess(area(far), 1800)

    def test_vi_filter_warp_clamp_and_nonzero_taps(self):
        info = {"vi_viewport": [0, 0, 8, 8], "video_resolution": [4, 4], "filtering": 0}
        self.assertEqual(filter_taps(3, 3, info), [(1.5, 1.5)])
        info["filtering"] = 1
        self.assertEqual(set(filter_taps(3, 3, info)), {(1.5, 1.5), (1.5, 2.5), (2.5, 1.5), (2.5, 2.5)})
        info["filtering"] = 2
        self.assertEqual(filter_taps(3, 3, info), [(1.5, 1.5)])
        self.assertEqual(filter_taps(0, 0, info), [(.5, .5)])
        info["filtering"] = 3
        with self.assertRaises(CaptureError):
            filter_taps(3, 3, info)


class PresentationTests(unittest.TestCase):
    def setUp(self):
        self.render = {"schema": 1, "workload": 10, "native": {"sequence": 60}, "weight": 1,
                       "gpu_indices_equal": True, "world_bytes": 48, "world_indices": [0, 0, 0],
                       "local_positions": [0, 0, 0, 1, 0, 0, 0, 1, 0], "indices": [0, 1, 2],
                       "world_groups": [0x10010001], "calls": [{"call": 1}], "raster": []}
        self.present = {"schema": 1, "workload": 10, "sequence": 60, "successful_present": True,
                        "native_color_image": True, "frames": 1, "frame": 0, "format": "BGRA8",
                        "width": 4, "height": 2, "row_bytes": 256, "video_resolution": [4, 2],
                        "vi_viewport": [0, 0, 4, 2]}
        self.world = struct.pack("<12f", 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1)
        self.swap = bytes(512)

    def test_valid_native_tuple_and_exact_extents(self):
        self.assertEqual(len(validate_pair(self.render, self.present, self.world, self.swap)), 3)

    def test_wrong_task_interpolation_or_failed_present_rejected(self):
        for field, value in (("workload", 11), ("frames", 2), ("frame", 1),
                             ("successful_present", False), ("native_color_image", False), ("row_bytes", 16)):
            with self.subTest(field=field), self.assertRaises(CaptureError):
                validate_pair(self.render, dict(self.present, **{field: value}), self.world, self.swap)
        with self.assertRaises(CaptureError):
            validate_pair(dict(self.render, weight=.5), self.present, self.world, self.swap)

    def test_corrupt_gpu_or_range_data_rejected(self):
        for field, value in (("indices", [0, 1, 3]), ("indices", [0, 1]),
                             ("world_indices", [1, 0, 0]), ("local_positions", [math.inf]*9),
                             ("calls", [{"call": 1}, {"call": 1}])):
            with self.subTest(field=field), self.assertRaises(CaptureError):
                validate_pair(dict(self.render, **{field: value}), self.present, self.world, self.swap)
        for world in (self.world[:-4], struct.pack("<12f", *([math.nan]*12))):
            with self.assertRaises(CaptureError):
                validate_pair(self.render, self.present, world, self.swap)
        with self.assertRaises(CaptureError):
            validate_pair(self.render, self.present, self.world, self.swap[:-1])

    def test_duplicate_raster_submission_rejects_cross_thread_capture(self):
        r = {"call": 1, "first": 0, "count": 3, "indexed": True, "test_z": False}
        with self.assertRaises(CaptureError):
            validate_pair(dict(self.render, raster=[r, r]), self.present, self.world, self.swap)


if __name__ == "__main__":
    unittest.main()
