import os
DATA = os.environ.get("AC2_DATA", "data/ac2")  # folder of unzipped AC2_*.zip groups
import glob, os, struct, sys, json
# Build one stream per group from consecutive IDs, record where each ID starts.
def stream(lo, hi):
    ids = []; parts = []; off = 0
    for p in sorted(glob.glob(f'{DATA}/*/*.bin')):
        i = int(os.path.basename(p)[:5])
        if lo <= i <= hi:
            b = open(p, 'rb').read(); ids.append((i, off, len(b))); parts.append(b); off += len(b)
    return b''.join(parts), ids
def scan(b, ids, align=0x800):
    found = []
    for o in range(0, len(b) - 16, align):
        c, s, m, z = struct.unpack_from('<4I', b, o)
        if m == 0xffffffff and z == 0 and 0 < c < 64 and 0x40 < s < 0x80000:
            owner = max((x for x in ids if x[1] <= o), key=lambda x: x[1])
            found.append(dict(off=o, n=c, size=s, file=owner[0], at=o - owner[1]))
    return found
if __name__ == '__main__':
    lo, hi = int(sys.argv[1]), int(sys.argv[2])
    b, ids = stream(lo, hi); f = scan(b, ids)
    for x in f: print(x)
