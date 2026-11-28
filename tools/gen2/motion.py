"""Gen 2 motion clips (AC2). Format decoded 2026-11-27 (session 6) from the ELF:
motion player 0x127c10 / add-motion 0x126680; see docs/research/gen2-archive.md "Motion".

Clip: u32 size, u32 ?, u16 0x100, u16 ?, u16 nodes, u16 ?, u16 frames, u16 ?
Then per node 0x30 bytes: u8 type (2 joint, 3 ?), u8 0, u8 parent (0xff root), u8 index, u32 key-data offset (from clip),
u32 child?, u32 sibling?, then 0x20 bytes of constants.
Key data: 0x80-byte keys: u16 frame, u16 next key's frame (0xffff on the last key), vec4 position, 2 x vec4,
vec4 rotation (radians, xyz), 2 x vec4, vec4 scale. Keys run until the flag is 0xffff.
Usage: python3 motion.py FILE [FILE...]   (B 421-492; stage files also carry enemy clips)
"""
import struct, sys

def clips(b):
    n = len(b); o = 0
    while o < n - 0x200:
        nodes, fr = struct.unpack_from('<H', b, o + 8)[0], struct.unpack_from('<H', b, o + 0xc)[0]
        size = struct.unpack_from('<I', b, o)[0]
        ok = 5 <= nodes <= 64 and 1 <= fr <= 900 and o + 0x10 + nodes * 0x30 <= n and 0 < size < 0x100000
        if ok:
            recs = [b[o + 0x10 + k * 0x30: o + 0x40 + k * 0x30] for k in range(nodes)]
            offs = [struct.unpack_from('<I', r, 4)[0] for r in recs]
            nz = [x for x in offs if x]
            ok = (all(r[0] in (2, 3) for r in recs) and len(nz) >= nodes // 2 and nz == sorted(nz)
                  and all(0x10 + nodes * 0x30 <= x < size for x in nz))
        if ok:
            k = keys(b, o, recs[[i for i, x in enumerate(offs) if x][0]])
            ok = bool(k) and k[0][0] == 0 and k[-1][0] <= fr
        if ok:
            yield o, size, nodes, fr, recs
            o += 16
        else:
            o += 16

def keys(b, o, rec):
    off = struct.unpack_from('<I', rec, 4)[0]
    out = []
    if not off: return out
    p = o + off
    while p + 0x80 <= len(b):
        frame, nxt = struct.unpack_from('<HH', b, p)
        pos = struct.unpack_from('<3f', b, p + 0x10); rot = struct.unpack_from('<3f', b, p + 0x40)
        out.append((frame, pos, rot))
        if nxt == 0xffff or nxt <= frame or len(out) > 1000: break
        p += 0x80
    return out

if __name__ == '__main__':
    for path in sys.argv[1:]:
        b = open(path, 'rb').read()
        cl = list(clips(b))
        print(path, len(cl), 'clips:', ' '.join('%dn/%df' % (n, f) for _, _, n, f, _ in cl))
