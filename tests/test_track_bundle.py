import copy
import math
import struct
import unittest

from tools import track_bundle as bundle


def source_document():
    rows = 25
    blocks = []

    def block(kind, index, data, address):
        blocks.append(dict(kind=kind, index=index, address=address,
                           before_hex=data.hex(), after_hex=data.hex()))

    vertices = bytes(1000 * 16)
    segments = bytearray(rows * 64)
    collision = b''.join(struct.pack('>3H3h2h', 0, 1, 2, -1, -1, -1, 0, 0) for _ in range(904))
    waypoints = b''.join(struct.pack('>ffhHI', 0, 0, 24, i % 17, 0) for i in range(306))
    block(0, 0, bytes([255]) * rows * 20, 0x80200100)
    block(1, 0, vertices, 0x80210000)
    block(2, 0, collision, 0x80230000)
    block(3, 0, waypoints, 0x80201000)
    block(8, 0, bytes(16 * 28), 0x80200014)
    block(9, 0, bytes(3200), 0x80124020)
    records = []
    for row in range(rows):
        pointer = 0x80220000 + row * 512
        struct.pack_into('>I', segments, row * 64 + 4, pointer)
        raw = struct.pack('>IIII', 0xBB001001, 0xFFFFFFFF, 0xB8000000, 0) + bytes(496)
        block(5, row, raw, pointer)
        records.append(dict(index=row, decoded=dict(road_dl=f'{pointer:#010x}', wall_dl=None, far_scenery_dl=None)))
    block(4, 0, segments, 0x80234000)
    return dict(format='al-track-document', version=1,
                target=dict(game_id='lamborghini.us', rom_xxh3_64=f'{bundle.ROM:016x}', circuit=0),
                visibility=dict(row_count=rows, slots_per_row=10,
                                rows=[[None] * 10 for _ in range(rows)],
                                base_rows=[[None] * 10 for _ in range(rows)],
                                raw_base_rows=[[-1] * 10 for _ in range(rows)],
                                base_fnv1a64=f'{bundle.fingerprint(bytes([255]) * rows * 20):016x}'),
                track_source=dict(format='al-track-source', version=1, blocks=blocks),
                segments=records, waypoints=dict(records=[]))


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.original = source_document()
        self.oval = bundle.new_oval(self.original)
        self.blocks = {(b['kind'], b['index']): b for b in self.oval['track_source']['blocks']}

    def test_generation_preserves_source_and_lap_contract(self):
        self.assertEqual(self.original, source_document())
        before = bytes.fromhex(self.blocks[3, 0]['before_hex'])
        after = bytes.fromhex(self.blocks[3, 0]['after_hex'])
        for i in range(306):
            self.assertEqual(before[i * 16 + 8:i * 16 + 16], after[i * 16 + 8:i * 16 + 16])
        self.assertEqual(struct.unpack_from('>ff', after, 0)[0], struct.unpack_from('>ff', after, 283 * 16)[0])
        self.assertLess(abs(struct.unpack_from('>ff', after, 0)[1] - struct.unpack_from('>ff', after, 283 * 16)[1]), 4.1)
        self.assertNotEqual(self.blocks[9, 0]['after_hex'], self.blocks[9, 0]['before_hex'])

    def test_reciprocal_collision_edges_and_containing_grid_seeds(self):
        vertices = bytes.fromhex(self.blocks[1, 0]['after_hex'])
        triangles = list(struct.iter_unpack('>3H3h2h', bytes.fromhex(self.blocks[2, 0]['after_hex'])))
        for index, triangle in enumerate(triangles):
            for edge, neighbor in enumerate(triangle[3:6]):
                if neighbor < 0:
                    continue
                endpoints = {triangle[edge], triangle[(edge + 1) % 3]}
                other = triangles[neighbor]
                matches = [j for j in range(3) if {other[j], other[(j + 1) % 3]} == endpoints]
                self.assertEqual(len(matches), 1)
                self.assertEqual(other[3 + matches[0]], index)
        for x, y, z, heading, seed, region, waypoint in struct.iter_unpack('>7i', bytes.fromhex(self.blocks[8, 0]['after_hex'])):
            points = [struct.unpack_from('>h2xh', vertices, v * 16) for v in triangles[seed][:3]]
            signs = [(b[0] - a[0]) * (z - a[1]) - (b[1] - a[1]) * (x - a[0]) for a, b in zip(points, points[1:] + points[:1])]
            self.assertTrue(all(s >= 0 for s in signs) or all(s <= 0 for s in signs))
            self.assertIn(region, (36, 37, 38))
            self.assertLess(waypoint, 250)
            self.assertLess(abs(math.hypot(x, z) - 20000), 400)

    def test_binary_counts_and_result_fingerprints(self):
        data = bundle.compile_bundle(self.oval)
        from tools import track_lab
        self.assertEqual(track_lab.compile_document(self.oval), data)
        header = bundle.HEADER.unpack_from(data)
        self.assertEqual(header[0], b'ALTRKBD1')
        self.assertEqual(header[3], len(data))
        offset = 64
        for _ in range(header[9]):
            kind, reserved, index, size, r1, r2, before, after = bundle.BLOCK.unpack_from(data, offset)
            offset += 32
            self.assertEqual(bundle.fingerprint(data[offset:offset + size]), after)
            offset += size
        self.assertEqual(offset, len(data))

    def test_bad_buffers_and_unsupported_circuit_rejected(self):
        bad = copy.deepcopy(self.oval)
        bad['track_source']['blocks'].append(copy.deepcopy(bad['track_source']['blocks'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            bundle.compile_bundle(bad)
        self.original['target']['circuit'] = 1
        with self.assertRaisesRegex(ValueError, 'Circuit 1'):
            bundle.new_oval(self.original)

    def test_elevation_rejects_empty_road(self):
        self.original['editor_preview'] = dict(groups=[
            dict(segment=0, layer='road', vertices=[], vertex_indices=[])])
        with self.assertRaisesRegex(ValueError, 'no vertices'):
            bundle.elevation_edit(self.original, 0, 12)


if __name__ == '__main__':
    unittest.main()
