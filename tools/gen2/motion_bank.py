"""Read a Gen 2 motion bank (AC2 B 421-492) in game order: motion number = index.
Bank (as the game loads it, loader 0x126d28): u32 count (77 for AC sets), 12 bytes, then count x 16-byte
entries (u32 index, u32 clip offset from the bank start, 8 bytes 0); entry 0 is empty, so motion number
m is the clip at entry m's offset. Clips follow the table (see motion.py for the clip format). The bank starts 0x8000
into its split file and runs on into the following files, so pass the B folder.
Usage: python3 motion_bank.py B_FOLDER SET_FILE_NUMBER
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from motion import keys

def stream(folder, first, count=4):
    return b''.join(open(os.path.join(folder, '%05d.bin' % i), 'rb').read()
                    for i in range(first, first + count) if os.path.exists(os.path.join(folder, '%05d.bin' % i)))

def bank(b, start=0x8000):
    n = struct.unpack_from('<I', b, start)[0]
    out = []
    for m in range(n):
        rel = struct.unpack_from('<I', b, start + 0x10 + m * 16 + 4)[0]
        if not rel:
            out.append(dict(number=m, offset=None, nodes=0, frames=0, parents=[], types=[], tracks=[])); continue
        o = start + rel
        nodes, fr = struct.unpack_from('<H', b, o + 8)[0], struct.unpack_from('<H', b, o + 0xc)[0]
        recs = [b[o + 0x10 + k * 0x30: o + 0x40 + k * 0x30] for k in range(nodes)]
        end = 0x10 + nodes * 0x30
        tracks = []
        for r in recs:
            off = struct.unpack_from('<I', r, 4)[0]
            k = keys(b, o, r)
            tracks.append(k)
            if off: end = max(end, off + len(k) * 0x80)
        out.append(dict(number=m, offset=o, nodes=nodes, frames=fr, parents=[r[2] for r in recs],
                        types=[r[0] for r in recs], tracks=tracks))
    return out

if __name__ == '__main__':
    folder, s = sys.argv[1], int(sys.argv[2])
    for c in bank(stream(folder, s)):
        moving = sum(1 for t in c['tracks'] if len(t) > 1 and len({k[2] for k in t}) > 1)
        print('%2d  nodes %2d  frames %3d  moving joints %2d' % (c['number'], c['nodes'], c['frames'], moving))
