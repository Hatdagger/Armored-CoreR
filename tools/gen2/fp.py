import sys, os, struct, collections, math
root = sys.argv[1]
rows = []
for d, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith('.bin') or not f[:5].isdigit(): continue
        p = os.path.join(d, f); b = open(p, 'rb').read()
        i = int(f[:5]); h = b[:16]
        c = collections.Counter(b[:65536]); n = min(len(b), 65536) or 1
        ent = -sum(v/n*math.log2(v/n) for v in c.values())
        u = struct.unpack('<4I', h.ljust(16, b'\0'))
        asc = ''.join(chr(x) if 32 <= x < 127 else '.' for x in h)
        rows.append((i, len(b), h.hex(), asc, round(ent, 2), u))
rows.sort()
for r in rows:
    print(f"{r[0]:5d} {r[1]:>10d} {r[2]} {r[3]} e={r[4]} u0={r[5][0]} u1={r[5][1]}")
