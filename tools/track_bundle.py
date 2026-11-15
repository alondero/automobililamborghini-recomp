"""Same-layout Track Lab bundles. Named buffers are resolved by the native loader.

The initial authoring lane uses the USA Circuit 1 table capacities. It keeps
stock event/range records and textures, but generates road/collision/route/grid
data from a new circular route. No guest pointers are supplied by the package.
"""
import copy
import math
import struct

HEADER = struct.Struct('<8sHHIQIBBHIIQQQ')
BLOCK = struct.Struct('<BBHIIIQQ')
ROM = 0x525201D7279F34E3


def fingerprint(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return value


def extract_source(memory, document):
    pointers = document['provenance']['context_pointers']
    context = int(document['provenance']['active_context'], 16)
    segments = int(pointers['segments'], 16)
    collision = int(pointers['unknown_table'], 16)
    vertices = int(pointers['anchors'], 16)
    waypoints = int(pointers['visibility_end_waypoints'], 16)
    row_count = document['visibility']['row_count']
    lists = [(kind, s['index'], int(s['decoded'][field], 16))
             for s in document['segments']
             for kind, field in [(5, 'road_dl'), (6, 'wall_dl'), (7, 'far_scenery_dl')]
             if s['decoded'][field]]
    first_list = min(a for _, _, a in lists)
    spans = [(0, 0, int(pointers['visibility_base'], 16), row_count * 20),
             (1, 0, vertices, (first_list - vertices) // 16 * 16),
             (2, 0, collision, segments - collision),
             (4, 0, segments, row_count * 64), (8, 0, context + 20, 16 * 28)]
    spans.append((9, 0, memory.u32(0x801265B0 + document['target']['circuit'] * 4), 3200))
    # Exact selected Circuit 1 route range chain is 36->37->38->36;
    # pit ranges39/40 extend the same waypoint buffer through index305.
    if document['target']['circuit'] == 0:
        spans.append((3, 0, waypoints, 306 * 16))
    for kind, index, address in lists:
        end = min([collision] + [a for _, _, a in lists if a > address])
        for pc in range(address, end, 8):
            if memory.u32(pc) >> 24 == 0xB8:
                spans.append((kind, index, address, pc + 8 - address))
                break
        else:
            raise ValueError('Unterminated stock display list')
    blocks = []
    for kind, index, address, size in sorted(spans):
        raw = memory.logical_bytes(address, size).hex()
        blocks.append(dict(kind=kind, index=index, address=address,
                           before_hex=raw, after_hex=raw))
    return dict(format='al-track-source', version=1, blocks=blocks)


def compile_bundle(document):
    if document.get('format') != 'al-track-document' or document.get('version') != 2:
        raise ValueError('Expected al-track-document version 2')
    target = document['target']
    if target.get('game_id') != 'lamborghini.us' or target.get('rom_xxh3_64') != f'{ROM:016x}':
        raise ValueError('Unsupported bundle ROM')
    circuit = target['circuit']
    rows = document['visibility']['row_count']
    if type(circuit) is not int or not 0 <= circuit < 6 or type(rows) is not int or not 2 <= rows <= 200:
        raise ValueError('Invalid circuit or row count')
    source = copy.deepcopy(document['track_source'])
    blocks = source['blocks']
    if not isinstance(blocks, list) or not 1 <= len(blocks) <= 608:
        raise ValueError('Invalid block count')
    visibility = document['visibility']['rows']
    if len(visibility) != rows or any(len(row) != 10 for row in visibility):
        raise ValueError('Invalid visibility shape')
    values = [-1 if value is None else value for row in visibility for value in row]
    if any(type(v) is not int or not -1 <= v < rows for v in values):
        raise ValueError('Invalid visibility reference')
    pvs = next(b for b in blocks if b['kind'] == 0)
    pvs['after_hex'] = struct.pack('>' + 'h' * len(values), *values).hex()
    encoded, keys = [], set()
    for block in sorted(blocks, key=lambda b: (b['kind'], b['index'])):
        kind, index = block['kind'], block['index']
        if type(kind) is not int or not 0 <= kind <= 9 or type(index) is not int:
            raise ValueError('Invalid block identity')
        if not (0 <= index < rows if 5 <= kind <= 7 else index == 0) or (kind, index) in keys:
            raise ValueError('Invalid or duplicate block index')
        keys.add((kind, index))
        before, after = bytes.fromhex(block['before_hex']), bytes.fromhex(block['after_hex'])
        if len(before) != len(after) or not before:
            raise ValueError('Track blocks cannot change capacity')
        if len(after) % (8 if 5 <= kind <= 7 else 28 if kind == 8 else 20 if kind == 0 else 16):
            raise ValueError('Unaligned track block')
        # Include unchanged blocks as guards for the coherent source bundle.
        encoded.append(BLOCK.pack(kind, 0, index, len(after), 0, 0,
                                  fingerprint(before), fingerprint(after)) + after)
    payload = b''.join(encoded)
    if len(payload) + HEADER.size > 2 * 1024 * 1024:
        raise ValueError('Bundle exceeds 2 MiB')
    return HEADER.pack(b'ALTRKBD1', 1, 64, 64 + len(payload), ROM, 2,
                       circuit, 10, rows, len(encoded), len(payload), 0, 0, 0) + payload


class BundleMemory:
    """Read the replaced logical blocks to re-extract the displayed mesh."""
    def __init__(self, source):
        self.blocks = [(b['address'], bytes.fromhex(b['after_hex'])) for b in source['blocks']]

    def logical_bytes(self, address, size):
        for base, data in self.blocks:
            if base <= address and address + size <= base + len(data):
                return data[address - base:address - base + size]
        raise ValueError(f'Address outside track bundle: {address:#x}')

    def u32(self, address):
        return int.from_bytes(self.logical_bytes(address, 4), 'big')


def updated_document(document, source, title):
    doc = copy.deepcopy(document)
    doc['version'] = 2
    doc['track_source'] = source
    doc['authoring'] = {'name': title}
    doc['capabilities'] = {'editable': ['visibility', 'track_bundle'],
                           'inspect_only': ['segments', 'anchors', 'waypoints'],
                           'unsupported': ['arbitrary_topology']}
    blocks = {(b['kind'], b['index']): b for b in source['blocks']}
    segments = bytes.fromhex(blocks[4, 0]['after_hex'])
    vertices = bytes.fromhex(blocks[1, 0]['after_hex'])
    for segment in doc['segments']:
        raw = segments[segment['index'] * 64:segment['index'] * 64 + 64]
        segment['raw_be_hex'] = raw.hex()
        dx, dz = struct.unpack_from('>hh', raw, 16)
        anchor = struct.unpack_from('>H', raw, 32)[0]
        x, z = struct.unpack_from('>h2xh', vertices, anchor * 16)
        segment['decoded']['cull_anchor'] = dict(anchor_index=anchor,
            segment_offset_x=dx, segment_offset_z=dz, world_x=x + dx, world_z=z + dz)
    doc['anchors'] = [dict(index=index, raw_be_hex=vertices[index * 16:index * 16 + 16].hex(),
                           decoded=dict(zip(('x', 'z'), struct.unpack_from('>h2xh', vertices, index * 16))))
                      for index in sorted({s['decoded']['cull_anchor']['anchor_index'] for s in doc['segments']})]
    if (3, 0) in blocks:
        raw = bytes.fromhex(blocks[3, 0]['after_hex'])
        for waypoint in doc['waypoints']['records']:
            offset = waypoint['index'] * 16
            waypoint['raw_be_hex'] = raw[offset:offset + 16].hex()
            waypoint['decoded'].update(zip(('plane_a', 'plane_b', 'progress_or_extent', 'unknown_u16', 'unknown_u32'),
                                          struct.unpack_from('>ffhHI', raw, offset)))
    return doc


def elevation_edit(document, segment_index, delta, radius=8000):
    if type(segment_index) is not int or not 0 <= segment_index < len(document['segments']):
        raise ValueError('Invalid elevation segment')
    if not isinstance(delta, (int, float)) or not math.isfinite(delta) or not -3000 <= delta <= 3000:
        raise ValueError('Elevation change must be between -3000 and 3000')
    source = copy.deepcopy(document['track_source'])
    blocks = {(b['kind'], b['index']): b for b in source['blocks']}
    vertices = bytearray.fromhex(blocks[1, 0]['after_hex'])
    preview = document['editor_preview']
    selected = next(g for g in preview['groups'] if g['segment'] == segment_index and g['layer'] == 'road')
    if not selected['vertices']:
        raise ValueError('Selected road segment has no vertices to elevate')
    center = [sum(v[axis] for v in selected['vertices']) / len(selected['vertices']) for axis in (0, 2)]
    updates = {}
    for group in preview['groups']:
        for index, (x, _, z) in zip(group['vertex_indices'], group['vertices']):
            weight = max(0.0, 1.0 - math.hypot(x - center[0], z - center[1]) / radius)
            amount = round(delta * weight * weight * (3 - 2 * weight))
            if index in updates and updates[index] != amount:
                raise ValueError('Instanced vertices require different elevations; choose another segment')
            updates[index] = amount
    for index, amount in updates.items():
        height = struct.unpack_from('>h', vertices, index * 16 + 2)[0] + amount
        if not -32768 <= height <= 32767:
            raise ValueError('Elevation exceeds signed vertex range')
        struct.pack_into('>h', vertices, index * 16 + 2, height)
    blocks[1, 0]['after_hex'] = vertices.hex()
    return updated_document(document, source, f'Elevation edit: segment {segment_index}')


def new_oval(document, radius=20000, half_width=1200):
    if document['target']['circuit'] != 0:
        raise ValueError('The generated oval currently uses Circuit 1 table capacities')
    if type(radius) is not int or not 12000 <= radius <= 28000 or type(half_width) is not int or not 800 <= half_width <= 2000:
        raise ValueError('Oval radius must be 12000..28000; half-width 800..2000')
    source = copy.deepcopy(document['track_source'])
    blocks = {(b['kind'], b['index']): b for b in source['blocks']}
    # Generate from the original buffers, retaining padding and immutable state.
    for block in source['blocks']:
        block['after_hex'] = block['before_hex']
    collision = bytearray.fromhex(blocks[2, 0]['before_hex'])
    slots = [i for i in range(len(collision) // 16) if any(collision[i * 16:i * 16 + 16])]
    if len(slots) != 904:
        raise ValueError('Unexpected Circuit 1 collision capacity')
    stations = len(slots) // 2
    rows = document['visibility']['row_count']
    vertices = bytearray.fromhex(blocks[1, 0]['before_hex'])
    segments = bytearray.fromhex(blocks[4, 0]['before_hex'])

    def point(fraction, r=radius):
        angle = 2 * math.pi * fraction
        return round(r * math.sin(angle)), round(r * math.cos(angle))

    for i in range(stations + 1):
        for side, r in enumerate((radius - half_width, radius + half_width)):
            x, z = point((i % stations) / stations, r)
            # Flat road, no lighting: alternating edge shade makes movement visible.
            color = (85, 90, 98, 255) if i % 2 else (110, 115, 123, 255)
            struct.pack_into('>hhhHhh4B', vertices, (2 * i + side) * 16, x, 0, z, 0, 0, 0, *color)
    bounds = [round(i * stations / rows) for i in range(rows + 1)]
    owners = [next(row for row in range(rows) if bounds[row] <= i < bounds[row + 1]) for i in range(stations)]
    # Up-facing triangles (negative signed area in X/Z): edge neighbors use
    # the same ordered edge pairs consumed by the game's collision query.
    faces = []
    for i in range(stations):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * ((i + 1) % stations), 2 * ((i + 1) % stations) + 1
        faces.extend([(a, b, c), (b, d, c)])
    edges = {}
    for ordinal, face in enumerate(faces):
        for edge in range(3):
            edges.setdefault(tuple(sorted((face[edge], face[(edge + 1) % 3]))), []).append((ordinal, edge))
    neighbors = [[-1] * 3 for _ in faces]
    for linked in edges.values():
        if len(linked) == 2:
            (a, ae), (b, be) = linked
            neighbors[a][ae], neighbors[b][be] = slots[b], slots[a]
    for ordinal, face in enumerate(faces):
        struct.pack_into('>3H3h2h', collision, slots[ordinal] * 16, *face, *neighbors[ordinal], owners[ordinal // 2], 0)
    vertex_base = blocks[1, 0]['address']
    for row in range(rows):
        start, end = bounds[row:row + 2]
        struct.pack_into('>hh', segments, row * 64 + 16, 0, 0)
        anchors = [2 * round(start + (end - start) * i / 15) for i in range(16)]
        struct.pack_into('>16H', segments, row * 64 + 32, *anchors)
        count = (end - start + 1) * 2
        commands = [(0xE7000000, 0), (0xB6000000, 0x000E3000),
                    (0xB7000000, 0x00012205), (0xBB000000, 0),
                    (0xFCFFFFFF, 0xFFFE793C),
                    (0x04000000 | count << 10 | (count * 16 - 1), vertex_base + start * 32)]
        for i in range(end - start):
            a, b, c, d = i * 4, i * 4 + 2, i * 4 + 4, i * 4 + 6
            commands.append((0xB1000000 | a << 16 | b << 8 | c, b << 16 | d << 8 | c))
        # The scene builder relies on material state left by the stock list.
        # Restore its final combine/texture/geometry state for later sky/HUD lists.
        old = bytes.fromhex(blocks[5, row]['before_hex'])
        last_state = {}
        for offset in range(0, len(old), 8):
            w0, w1 = struct.unpack_from('>II', old, offset)
            if w0 >> 24 in (0xBB, 0xFC, 0xB6, 0xB7):
                last_state[w0 >> 24] = (offset, (w0, w1))
        commands.extend(command for _, command in sorted(last_state.values()))
        commands.append((0xB8000000, 0))
        raw = b''.join(struct.pack('>II', *c) for c in commands)
        if len(raw) > len(old):
            raise ValueError(f'Generated segment {row} exceeds stock display-list capacity')
        blocks[5, row]['after_hex'] = (raw + bytes(len(old) - len(raw))).hex()
    for (kind, _), block in blocks.items():
        if kind in (6, 7):
            old = bytes.fromhex(block['before_hex'])
            block['after_hex'] = (struct.pack('>II', 0xB8000000, 0) + bytes(len(old) - 8)).hex()
    waypoints = bytearray.fromhex(blocks[3, 0]['before_hex'])
    for i in range(306):
        position = i if i < 250 else (216 + (i - 250) * 34 / 33 if i <= 283 else 250 + (i - 283) * 18 / 22)
        pit_offset = 0 if i < 250 else 400 * math.sin(math.pi * (i - 250) / 55)
        x, z = point(position / 250, radius - pit_offset)
        struct.pack_into('>ff', waypoints, i * 16, x / 100, z / 100)
    grid = bytearray.fromhex(blocks[8, 0]['before_hex'])
    for i in range(16):
        reverse = i >= 8
        slot = i % 8
        fraction = (1 / 250 + (slot // 2) * 900 / (2 * math.pi * radius)) if reverse else (248 / 250 - (slot // 2) * 900 / (2 * math.pi * radius))
        x, z = point(fraction, radius + (-350 if slot % 2 else 350))
        heading = round((90 + fraction * 360 + (180 if reverse else 0)) % 360)
        station = int((fraction % 1) * stations)
        seed = None
        for ordinal in (station * 2, station * 2 + 1):
            face = faces[ordinal]
            points = [struct.unpack_from('>h2xh', vertices, vertex * 16) for vertex in face]
            signs = [(b[0] - a[0]) * (z - a[1]) - (b[1] - a[1]) * (x - a[0])
                     for a, b in zip(points, points[1:] + points[:1])]
            if all(value >= 0 for value in signs) or all(value <= 0 for value in signs):
                seed = slots[ordinal]
                break
        if seed is None:
            raise ValueError('Generated spawn is outside the road triangles')
        waypoint = round((fraction % 1) * 250) % 250
        region = 36 if waypoint <= 17 else 37 if waypoint <= 215 else 38
        struct.pack_into('>7i', grid, i * 28, x, 0, z, heading, seed, region, waypoint)
    for kind, data in [(1, vertices), (2, collision), (3, waypoints), (4, segments), (8, grid)]:
        blocks[kind, 0]['after_hex'] = data.hex()
    if (9, 0) not in blocks:
        raise ValueError('Re-open a game snapshot to include the minimap source')
    # The live dot renderer scales waypoint coordinates by .061; the outline
    # quad maps texture coordinates0..78 over80 units. Both share camera rotation.
    image = bytearray(3200)
    pixel_radius = radius / 100 * 0.061 * 78 / 80
    for y in range(80):
        for x in range(80):
            distance = abs(math.hypot(x - 39, y - 39) - pixel_radius)
            coverage = min(1, max(0, 1.5 - distance))
            nibble = ((round(7 * coverage) << 1) | 1) if coverage > 0 else 0
            offset = y * 80 + x
            image[offset // 2] |= nibble << (4 if offset % 2 == 0 else 0)
    blocks[9, 0]['after_hex'] = image.hex()
    doc = updated_document(document, source, 'New oval')
    doc['authoring'].update(radius=radius, half_width=half_width)
    doc['visibility']['rows'] = [[(row + step) % rows for step in (1, 2, 3, 4, -1, -2, -3, -4)] + [None, None] for row in range(rows)]
    return doc
