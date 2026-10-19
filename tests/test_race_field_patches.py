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
        self.assertEqual(fingerprint, 'd1e8177c67d9ee31a820ab3dda51b13f3c677de68f9d3a0b76186367b46e40db')
        actual = tomllib.loads(fields.patch_blocks(rom, functions))
        checked = tomllib.loads(config_text[config_text.index('# Expanded race-field table references'):])
        self.assertEqual(actual, checked)
