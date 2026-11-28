import sys, struct
for p in sys.argv[1:]:
    b = open(p, 'rb').read(); hits = []
    for o in range(0, len(b) - 16, 4):
        c, s, m, z = struct.unpack_from('<4I', b, o)
        if m == 0xffffffff and z == 0 and 0 < c < 64 and 0x40 < s <= len(b) - o:
            hits.append((hex(o), c, hex(s)))
    print(p.split('/')[-1], len(b), hits[:8])
