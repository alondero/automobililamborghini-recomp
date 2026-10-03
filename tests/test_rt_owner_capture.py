import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_rt_overlay_capture import owner_at_tap, verify_owner_tap
from inspect_rt_task import CaptureError


class OwnerCaptureTests(unittest.TestCase):
    def setUp(self):
        material = {
            "alpha_compare": 0,
            "coverage_times_alpha": False,
            "alpha_blend": False,
            "force_blend": False,
            "z_compare": True,
            "z_update": True,
            "z_mode": 0,
            "z_source": 0,
            "standard_fog": True,
            "other_lo": 0xC8112078,
            "other_hi": 1 << 20,
            "combine_w0": 0xFC327FFF,
            "combine_w1": 0xFFFFF838,
            "fog_rgba": [0.1, 0.2, 0.3, 1.0],
        }
        self.owner = {"width": 2, "height": 1, "color_address": 0x1234}
        self.raw = struct.pack("<IIII", 6, 0, 6, 1)
        self.render = {"indices": [0, 1, 2, 2, 1, 3]}
        self.faces = {(7, 0), (7, 3)}
        self.calls = {7: {"color_address": 0x1234, "material": material,
                          "shader_other_lo": 0xC8112078, "shader_other_hi": 1 << 20,
                          "shader_flags": 0}}
        self.draws = {5: {"call": 7, "first": 0, "count": 6, "indexed": True,
                          "test_z": False, "overlay": False}}

    def test_owner_id_and_primitive_resolve_to_admitted_receiver(self):
        self.assertEqual(owner_at_tap(0.5, 0.5, self.owner, self.raw), (5, 0))
        self.assertEqual(verify_owner_tap(0.5, 0.5, self.owner, self.raw,
                                          self.render, self.faces, self.calls, self.draws), (7, 0))
        self.assertEqual(verify_owner_tap(1.5, 0.5, self.owner, self.raw,
                                          self.render, self.faces, self.calls, self.draws), (7, 3))

    def test_allocated_texture_extent_does_not_stretch_video_space_taps(self):
        owner = {"width": 4, "height": 1, "color_address": 0x1234}
        raw = struct.pack("<IIIIIIII", 1, 0, 2, 0, 3, 0, 4, 0)
        # With a 2-texel VI resolution and 4-texel allocation, source tap 0.5
        # still addresses source texel 0, not texel 1.
        self.assertEqual(owner_at_tap(0.5, 0.5, owner, raw), (0, 0))
        self.assertEqual(owner_at_tap(1.5, 0.5, owner, raw), (1, 0))

    def test_unowned_or_unadmitted_taps_fail_closed(self):
        empty = struct.pack("<II", 0, 0)
        with self.assertRaises(CaptureError):
            verify_owner_tap(0.5, 0.5, self.owner, empty,
                             self.render, self.faces, self.calls, self.draws)
        with self.assertRaises(CaptureError):
            owner_at_tap(0.5, 0.5, self.owner, empty)
        with self.assertRaises(CaptureError):
            verify_owner_tap(0.5, 0.5, self.owner, self.raw,
                             self.render, set(), self.calls, self.draws)

    def test_unsupported_owner_material_fails_closed(self):
        self.calls[7]["material"]["standard_fog"] = False
        with self.assertRaises(CaptureError):
            verify_owner_tap(0.5, 0.5, self.owner, self.raw,
                             self.render, self.faces, self.calls, self.draws)

    def test_unknown_raw_test_z_and_overlay_owners_fail_closed(self):
        with self.assertRaises(CaptureError):
            verify_owner_tap(0.5, 0.5, self.owner, self.raw,
                             self.render, self.faces, self.calls, {})
        for field, value in (("indexed", False), ("test_z", True), ("overlay", True)):
            with self.subTest(field=field):
                draw = dict(self.draws[5], **{field: value})
                with self.assertRaises(CaptureError):
                    verify_owner_tap(0.5, 0.5, self.owner, self.raw,
                                     self.render, self.faces, self.calls, {5: draw})


if __name__ == "__main__":
    unittest.main()
