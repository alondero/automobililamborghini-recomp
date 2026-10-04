from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from run_rt_shadow_matrix import BASES, only_inconclusive, variant  # noqa: E402


class MatrixVariantTests(unittest.TestCase):
    def test_soft_variant_sets_configured_shadow_settings(self) -> None:
        scenario = variant("soft", 3, 2, 1.5, 16, car=4, sweep=True)
        self.assertEqual(scenario["warp"], "3:1:4:1")
        self.assertEqual(scenario["warp_mode"], 2)
        self.assertEqual(scenario["graphics"],
                         {"rt_shadows": True, "rt_shadow_rays": 16, "rt_shadow_softness": 1.5})
        self.assertEqual(scenario["developer_env"], {"LAMBO_RT_SHADOW_SWEEP": "1"})

    def test_colour_precision_is_pinned_for_every_control_and_trace(self) -> None:
        # Native and traced captures must share one colour format to compare.
        for kind in BASES:
            with self.subTest(kind=kind):
                scenario = variant(kind, 1, 0, 0.5, 8, hpfb="On")
                self.assertEqual(scenario["graphics"]["hpfb_option"], "On")
        hard = variant("hard", 1, 0, 0.5, 8, hpfb="On")["graphics"]
        self.assertTrue(hard["rt_shadows"])
        self.assertEqual(hard["rt_shadow_softness"], 0.0)
        self.assertNotIn("hpfb_option", variant("native", 1, 0, 0.5, 8).get("graphics", {}))

    def test_pinned_colour_control_omits_owner_buffer(self) -> None:
        # The owner buffer forces standard colour, which would void the control.
        omitted = variant("omitted", 1, 0, 0.5, 8, hpfb="On")["diagnostics"]
        self.assertTrue(omitted["rt_drop_overlay"])
        self.assertNotIn("rt_owner_buffer", omitted)
        self.assertTrue(variant("omitted", 1, 0, 0.5, 8)["diagnostics"]["rt_owner_buffer"])


    def test_only_shader_warmup_failures_are_retried(self) -> None:
        self.assertTrue(only_inconclusive({"failures": ["task 300: inconclusive, traced drew 4 draws with ubershaders"]}))
        self.assertFalse(only_inconclusive({"failures": []}))
        self.assertFalse(only_inconclusive({"failures": ["task 300: inconclusive, traced drew 4 draws with ubershaders",
                                                         "task 420: replacement not ready"]}))


if __name__ == "__main__":
    unittest.main()
