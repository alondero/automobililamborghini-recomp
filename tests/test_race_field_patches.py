"""Pin the ROM relocation inventory and parity of generated field patches."""
import hashlib
from pathlib import Path
import sys
import tomllib
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import race_field_patches as fields

class RaceFieldPatchTests(unittest.TestCase):
    def test_rejects_unverified_rom(self):
        with self.assertRaises(ValueError):
            fields.references(bytes(4096), [])

    @unittest.skipUnless((ROOT / 'Automobili Lamborghini (USA).z64').exists(), 'USA ROM required')
    def test_generation_and_reference_inventory(self):
        config_text = (ROOT / 'lamborghini.us.toml').read_text(encoding='utf-8')
        config = tomllib.loads(config_text)
        excluded = set(config['patches']['stubs'] + config['patches']['ignored'])
        symbols = tomllib.loads((ROOT / 'lamborghini.syms.toml').read_text())
        functions = [(f['name'], f['vram'], f['size']) for section in symbols['section']
                     for f in section['functions'] if f['name'] not in excluded]
        rom = (ROOT / 'Automobili Lamborghini (USA).z64').read_bytes()
        refs = fields.references(rom, functions)
        fingerprint = hashlib.sha256('\n'.join(f'{pc:08x}:{word:08x}' for _,pc,word,_ in refs).encode()).hexdigest()
        self.assertEqual(fingerprint, '4f828a2c8dd7ea11dcb7929a7052888dc823050880af4de8548fab57e5a61b7f')
        # Minimap aliases start at entrant one while its loop is zero-based.
        # Both the AI route point and the player-label body lookup must move.
        pcs = {pc for _, pc, _, _ in refs}
        self.assertTrue({0x8005519C, 0x8005532C, 0x80055500, 0x80055544} <= pcs)
        actual = tomllib.loads(fields.patch_blocks(rom, functions))
        checked = tomllib.loads(config_text[config_text.index('# Expanded race-field table references'):])
        self.assertEqual(actual, checked)
        hooks = {h['before_vram']: h['text'] for h in actual['patches']['hook']}
        # Entering and settled finish screens have separate string builders.
        for pc in (0x800508C8, 0x80050A14):
            self.assertIn('lambo_field_rank_text', hooks[pc])
        self.assertEqual(hooks[0x800553AC], '{ ctx->r24 &= 7; }')
