"""Master of Arena missions: briefing headers (GG/MS/MIS.T) next to each
mission map's objects (FDAT 5s+4 chunk 13) and its mission code (FDAT 5s+0,
loaded at 0x800A0DD0). Notes: docs/research/gen1-stages.md, "Missions".

Usage: python3 -I moa_missions.py <import>/unpacked     (from your own disc)
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import moa_stage  # noqa: E402

MISSION_CODE_BASE = 0x800A0DD0


def text(path):
    d = open(path, 'rb').read() if os.path.exists(path) else b''
    out = []
    for b in d:
        if b == 0:
            break
        if b == 13:
            continue
        out.append(chr(b) if 32 <= b < 127 or b == 10 else '')
    return ''.join(out)


def briefing(mis_dir, m):
    """Mail header (entry 16m+1) and the 'Theater of operations' page."""
    head = text(os.path.join(mis_dir, '%03d.bin' % (16 * m + 1)))
    theater = ''
    for k in range(16 * m + 1, 16 * m + 16):
        t = text(os.path.join(mis_dir, '%03d.bin' % k))
        if t.startswith('Theater'):
            theater = t
            break
    flat = lambda t: ' / '.join(l.strip() for l in t.split('\n') if l.strip())
    return flat(head), flat(theater)


def objects(fdat_dir, s):
    """Chunk 13 records from 3 on: (index, x, y, z, handler, type)."""
    d = open(os.path.join(fdat_dir, '%03d.bin' % (5 * s + 4)), 'rb').read()
    c = moa_stage.chunks(d)[13]
    out = []
    for i in range(3, len(c) // 40):
        r = c[i * 40:i * 40 + 40]
        x, y, z, h = struct.unpack_from('<4h', r, 0)
        if h == -1 or not any(r):
            continue
        out.append((i, x, y, z, h, struct.unpack_from('<h', r, 14)[0]))
    return out


def handlers(fdat_dir, s):
    code = open(os.path.join(fdat_dir, '%03d.bin' % (5 * s)), 'rb').read()
    return [p for p in struct.unpack_from('<16I', code, 0)[9:] if p]


def main():
    root = sys.argv[1]
    fdat = os.path.join(root, 'GG', 'COM', 'FDAT')
    mis = os.path.join(root, 'GG', 'MS', 'MIS')
    for s in range(20):
        head, theater = briefing(mis, s)
        obj = objects(fdat, s)
        hs = handlers(fdat, s)
        by = collections.Counter(o[4] for o in obj)
        print(f'mission/stage {s:2d}: {head}')
        print(f'    {theater}')
        print(f'    objects {len(obj)}, by handler (code slot 8 + n): {dict(sorted(by.items()))}; '
              f'handlers {", ".join("%08X" % h for h in hs)}')


if __name__ == '__main__':
    main()
