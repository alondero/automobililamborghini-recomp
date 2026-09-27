"""Regeneration must preserve every checked-in gameplay/platform hook."""
import ast
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GenerationHooksTests(unittest.TestCase):
    def test_generator_matches_checked_in_patches(self):
        # Parse without executing the ROM-dependent generator.
        tree = ast.parse((ROOT / "scripts/gen_syms_toml.py").read_text(encoding="utf-8"))
        blocks = next(
            ast.literal_eval(node.value)
            for node in tree.body
            if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "PATCH_BLOCKS"
                    for target in node.targets)
        )
        expected = tomllib.loads((ROOT / "lamborghini.us.toml").read_text(encoding="utf-8"))
        # stubs/ignored are emitted separately, before PATCH_BLOCKS.
        expected_blocks = {key: value for key, value in expected["patches"].items()
                           if key not in {"stubs", "ignored"}}
        self.assertEqual(tomllib.loads(blocks)["patches"], expected_blocks)
