import unittest

from scripts.gen_mod_patch_guards import protected_addresses


class PatchGuardTests(unittest.TestCase):
    def test_preserves_patched_functions_but_allows_stock_hooks(self):
        config = {"patches": {
            "stubs": ["stub"],
            "hook": [{"func": "injected"}, {"func": "injected"}],
            "instruction": [{"func": "changed"}],
        }}
        symbols = {"section": [{"functions": [
            {"name": "stock", "vram": 0x80000400},
            {"name": "injected", "vram": 0x80001000},
            {"name": "changed", "vram": 0x80002000},
            {"name": "stub", "vram": 0x80003000},
        ]}]}
        self.assertEqual(protected_addresses(config, symbols),
                         [0x80001000, 0x80002000, 0x80003000])

    def test_unknown_patch_target_fails_generation(self):
        with self.assertRaisesRegex(ValueError, "missing"):
            protected_addresses({"patches": {"stubs": ["missing"]}},
                                {"section": []})


if __name__ == "__main__":
    unittest.main()
