"""Audit the scene builder's per-circuit draw-distance cull from a race RDRAM snapshot.

The frame builder (func_8000A6C0) draws the camera segment's precomputed visibility
list (up to 10 entries, table ptr 0x800CE678) but culls each entry against a radius
from the float[6][5] table at 0x80088FD0, indexed [circuit (0x800CE794)][players
(0x800CE6A4)] -- see docs/no_lod_audit.md section 8. This script replays that test
offline for every segment of the loaded track: for each segment k (camera stood at
k's own test point) and each entry e of k's row, it computes the builder's distance
(0x8000D094-0x8000D374: rec16[rec64[e]+0x20] + rec64[e]+{0x10,0x12} vs the camera
point, XZ plane) and reports which entries the circuit's radius would hide. Culled
entries are exactly the "world pops in at a distance" events the no_lod
draw-distance lift (src/lambo_no_lod.cpp) removes.

Usage:
    LAMBO_HEADLESS=1 LAMBO_WARP=<circuit> LAMBO_RACE_DL_DUMP=<base> \
        LAMBO_RACE_DL_DUMP_AT=300 LAMBO_MODERN_MAX_VIS=1500 build/lamborghini_modern
    python tools/pvs_distance_audit.py <base>-300.bin

Example result (CIRCUIT 5, the city track, radius 35000): segments 28-30 hide
segment 31 at ~51k units -- a whole block popping in down a long street.
"""
import struct
import sys


def main(path):
    with open(path, 'rb') as f:
        d = f.read()

    def gh(a):
        """Guest (big-endian) signed halfword; recomp RDRAM stores native LE words."""
        off = (a - 0x80000000) & ~3
        w = struct.unpack_from('<I', d, off)[0]
        v = (w >> 16) if (a & 3) == 0 else (w & 0xFFFF)
        return v - 0x10000 if v >= 0x8000 else v

    def gw(a):
        return struct.unpack_from('<I', d, a - 0x80000000)[0]

    pvs = gw(0x800CE678)      # 20-byte rows of 10 s16 entries, -1 terminated
    rec64 = gw(0x800BF1D0)    # viewport-0 segment record list (64-byte stride)
    rec16 = gw(0x800BF1C0)    # 16-byte test-point records
    circuit = gh(0x800CE794)
    players = gh(0x800CE6A4)
    radius = struct.unpack_from('<f', d, 0x88FD0 + (circuit * 5 + players) * 4)[0]
    print(f'pvs=0x{pvs:08X} rec64=0x{rec64:08X} rec16=0x{rec16:08X} '
          f'circuit={circuit} players={players} radius={radius:.0f}')
    if not (0x80000000 <= rec64 < 0x80800000):
        sys.exit('segment record base is null -- snapshot not taken in a race?')

    def point(seg):
        idx = gh(rec64 + seg * 64 + 0x20)
        x = gh(rec16 + idx * 16 + 0x0) + gh(rec64 + seg * 64 + 0x10)
        z = gh(rec16 + idx * 16 + 0x2) + gh(rec64 + seg * 64 + 0x12)
        return x, z

    # Segment count: PVS rows are only sane for real segments; stop at the first
    # row whose entries leave plausible range.
    rows = []
    k = 0
    while k < 200:
        raw = [gh(pvs + k * 20 + i * 2) for i in range(10)]
        ents = []
        for v in raw:
            if v < 0:
                break
            ents.append(v)
        if (not ents and k > 0) or any(v > 4096 for v in ents):
            break
        rows.append(ents)
        k += 1

    n = len(rows)
    total = 0
    worst = []
    for k, ents in enumerate(rows):
        cx, cz = point(k)
        for e in ents:
            if e >= n:
                continue
            x, z = point(e)
            dist = ((x - cx) ** 2 + (z - cz) ** 2) ** 0.5
            total += 1
            if dist >= radius:
                worst.append((dist, k, e))
    worst.sort(reverse=True)
    print(f'segments={n} entries={total} culled at radius {radius:.0f}: {len(worst)}')
    for dist, k, e in worst:
        print(f'  camera in seg {k:3d} hides entry {e:3d} at dist {dist:9.0f}')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
