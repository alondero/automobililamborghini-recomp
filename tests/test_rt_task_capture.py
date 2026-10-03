"""Synthetic F3DEX fixtures; no game vertices, textures or RAM are stored."""
from __future__ import annotations

import copy
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_rt_task import CaptureError, TaskInspector
from check_rt_sun_capture import compare_stationary


class TaskCaptureTests(unittest.TestCase):
    def setUp(self):
        self.ram = bytearray(0x800000)
        self.root = 0x800BF400
        self.meta = {"schema": 2, "snapshot_point": "producer-before-submit",
                     "ram_layout": "word-swapped", "emitters_complete": True,
                     "task_address": 0x800BF240, "root": self.root, "cameras": [],
                     "emitters": [{"emitter": 0x80009AC0, "begin": self.root,
                                   "end": self.root + 0x100, "camera_slot": 1}]}

    def write(self, address, fmt, value):
        offset = (address & 0x1FFFFFFF) ^ {1: 3, 2: 2}.get(struct.calcsize(fmt), 0)
        struct.pack_into("<" + fmt, self.ram, offset, value)

    def commands(self, address, commands):
        for i, (word0, word1) in enumerate(commands):
            self.write(address + 8 * i, "I", word0)
            self.write(address + 8 * i + 4, "I", word1)

    def triangle_list(self, address=0x80100000):
        for i, (x, y, z) in enumerate(((0, 0, 0), (10, 0, 0), (0, 0, 10))):
            for j, coordinate in enumerate((x, y, z)):
                self.write(0x80101000 + 16 * i + j * 2, "h", coordinate)
        self.commands(address, [(0x04000C00, 0x80101000), (0xBF000000, 0x00000204), (0xB8000000, 0)])

    def inspect(self):
        return TaskInspector(bytes(self.ram), self.meta).inspect()

    def test_group_payload_and_child_owner_do_not_become_fake_commands(self):
        self.triangle_list()
        self.commands(self.root, [(0x6400000C, 0x10010001), (1, 0xFFFFFFFF),
                                 (0x06000000, 0x80100000), (0x6400000D, 1),
                                 (0x06000000, 0x80100000), (0xB8000000, 0)])
        result = self.inspect()
        self.assertEqual(len(result["draws"]), 2)
        first, second = result["draws"]
        self.assertEqual(first["state"]["transform_group"], 0x10010001)
        self.assertEqual(second["state"]["transform_group"], 0)
        self.assertEqual(first["state"]["emitter"], 0x80009AC0)
        self.assertEqual(first["state"]["call_path"], [self.root, 0x80100000])
        self.assertEqual(first["max"], [10, 0, 10])
        self.assertEqual(result["eligibility"], "unproved; native observations only")

    def test_scene_selectors_must_match_owned_ram(self):
        self.commands(self.root, [(0xB8000000, 0)])
        for name, address, value in (("phase", 0x800CE6AC, 8), ("circuit", 0x800CE794, 5),
                                     ("players", 0x800CE6A4, 1), ("race_mode", 0x800CE6B4, 2)):
            self.meta[name] = value
            self.write(address, "h", value)
        self.meta["model_cursors"] = [3, 6, 9, 12]
        for player, model in enumerate(self.meta["model_cursors"]):
            self.write(0x800CE7E8 + player * 2, "h", model)
        self.assertEqual(self.inspect()["task"]["race_mode"], 2)
        for name in ("phase", "circuit", "players", "race_mode", "model_cursors"):
            old = self.meta[name]
            self.meta[name] = [0, 0, 0, 0] if name == "model_cursors" else old + 1
            with self.subTest(name=name), self.assertRaises(CaptureError):
                self.inspect()
            self.meta[name] = old
        self.meta["players"] = True
        with self.assertRaises(CaptureError):
            self.inspect()
        self.meta["players"] = 1
        self.meta.pop("race_mode")
        with self.assertRaises(CaptureError):
            self.inspect()

    def test_light_and_transform_are_captured_when_vertices_load(self):
        self.triangle_list()
        # Row-vector matrix: identity plus a fractional world translation.
        for i in range(4):
            self.write(0x80102000 + (i * 4 + i) * 2, "h", 1)
        self.write(0x80102000 + 24, "h", -2)
        self.write(0x80102000 + 56, "H", 32768)
        for i, value in enumerate((-20, 80, 40)):
            self.write(0x80103008 + i, "b", value)
        self.commands(self.root, [(0x01020040, 0x80102000), (0xB7000000, 0x20000),
                                 (0xBC000002, 64), (0x03860010, 0x80103000),
                                 (0x04000C00, 0x80101000), (0xB6000000, 0x20000),
                                 (0xBF000000, 0x00000204), (0xB8000000, 0)])
        draw = self.inspect()["draws"][0]
        self.assertEqual(draw["min"], [-1.5, 0, 0])
        self.assertTrue(draw["state"]["vertex_lit"])
        self.assertEqual(draw["state"]["light_count"], 1)
        self.assertEqual(draw["state"]["lights"][0]["direction"], [-20, 80, 40])

    def test_visibility_tests_are_disclosed_instead_of_claiming_execution(self):
        self.triangle_list()
        self.commands(self.root, [(0xBE000000, 4), (0x06000000, 0x80100000), (0xB8000000, 0)])
        result = self.inspect()
        self.assertEqual(len(result["draws"]), 1)
        self.assertEqual(len(result["unevaluated_visibility_tests"]), 1)

    def test_incomplete_foreign_overlapping_and_nonfinite_metadata_rejected(self):
        bad = []
        doc = copy.deepcopy(self.meta)
        doc["emitters_complete"] = False
        bad.append(doc)
        doc = copy.deepcopy(self.meta)
        doc["root"] += 8
        bad.append(doc)
        for end in (0x800C6CA0, 0x800C6C08):
            doc = copy.deepcopy(self.meta)
            doc["emitters"][0]["end"] = end
            bad.append(doc)
        doc = copy.deepcopy(self.meta)
        doc["schema"] = 1
        bad.append(doc)
        doc = copy.deepcopy(self.meta)
        doc["snapshot_point"] = "HLE-live-RAM"
        bad.append(doc)
        doc = copy.deepcopy(self.meta)
        doc["emitters"].append(copy.deepcopy(doc["emitters"][0]))
        bad.append(doc)
        doc = copy.deepcopy(self.meta)
        doc["cameras"] = [{"slot": 1, "height_term": float("nan")}]
        bad.append(doc)
        for metadata in bad:
            with self.subTest(metadata=metadata), self.assertRaises(CaptureError):
                TaskInspector(bytes(self.ram), metadata)

    def test_unloaded_vertices_unknown_commands_and_recursive_lists_rejected(self):
        for commands in ([(0xBF000000, 0x00000204)], [(0xB2000000, 0)],
                         [(0x02000000, 0)], [(0x06000000, self.root)], [(0x6400000D, 1)]):
            with self.subTest(commands=commands):
                self.commands(self.root, commands)
                with self.assertRaises(CaptureError):
                    self.inspect()

    def test_panorama_exclusion_requires_the_captured_emitter(self):
        self.commands(self.root, [(0x06000000, 0x810050B0), (0xB8000000, 0)])
        with self.assertRaises(CaptureError):
            self.inspect()
        self.meta["emitters"][0]["emitter"] = 0x8000F6D8
        self.assertEqual(self.inspect()["excluded_lists"][0]["address"], 0x810050B0)

    def test_stationary_comparison_rejects_motion_light_changes_and_missing_views(self):
        reports = []
        for sequence, heading in ((60, 20), (300, 200), (420, 20), (540, 20)):
            reports.append({"task": {"sequence": sequence, "phase": 8, "circuit": 0, "players": 1,
                                     "cameras": [{"slot": 1, "heading": heading}]},
                            "draws": [{"model": TaskInspector.identity(), "view": TaskInspector.identity(),
                                       "native_object": {"flags": 8},
                                       "state": {"transform_group": 0x10010001, "vertex_lit": True,
                                                 "light_count": 1, "lights": [{"direction": [0, 1, 0]}]}}]})
        reports[1]["draws"][0]["view"][0][0] = -1
        reports[1]["draws"][0]["view"][2][2] = -1
        self.assertEqual(compare_stationary(reports)["unit_key_direction"], [0, 1, 0])
        for circuit in range(6):
            other = copy.deepcopy(reports)
            for report in other:
                report["task"]["circuit"] = circuit
                report["draws"][0]["state"]["lights"][0]["direction"] = [circuit + 1, 7, -13]
            result = compare_stationary(other)
            self.assertEqual(result["circuit"], circuit)
            self.assertEqual(result["native_directional_vectors"], [[circuit + 1, 7, -13]])
        mixed = copy.deepcopy(reports)
        mixed[1]["task"]["circuit"] = 1
        with self.assertRaises(CaptureError):
            compare_stationary(mixed)
        for field, value in (("race_mode", 2), ("model_cursors", [1, 0, 0, 0])):
            mixed = copy.deepcopy(reports)
            mixed[1]["task"][field] = value
            with self.subTest(field=field), self.assertRaises(CaptureError):
                compare_stationary(mixed)
        for mutation in ("motion", "light", "view", "turn", "unchanged_view"):
            bad = copy.deepcopy(reports)
            if mutation == "motion":
                bad[1]["draws"][0]["model"][3][0] = 1
            elif mutation == "light":
                bad[1]["draws"][0]["state"]["lights"][0]["direction"] = [1, 0, 0]
            elif mutation == "view":
                bad[1]["task"]["cameras"] = []
            elif mutation == "turn":
                bad[1]["task"]["cameras"][0]["heading"] = 20
            else:
                bad[1]["draws"][0]["view"] = TaskInspector.identity()
            with self.subTest(mutation=mutation), self.assertRaises(CaptureError):
                compare_stationary(bad)

    def test_full_other_mode_replaces_previous_material_bits(self):
        self.triangle_list()
        self.commands(self.root, [(0xBA000020, 0xFFFFFFFF), (0xB9000020, 0xFFFFFFFF),
                                 (0xEF123456, 0x12340000), (0xB9000010, 0x5678),
                                 (0x06000000, 0x80100000), (0xB8000000, 0)])
        state = self.inspect()["draws"][0]["state"]
        self.assertEqual(state["other_hi"], 0x123456)
        self.assertEqual(state["other_lo"], 0x12345678)


if __name__ == "__main__":
    unittest.main()
