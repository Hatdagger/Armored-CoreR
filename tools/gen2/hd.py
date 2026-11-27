import sys, struct
p = sys.argv[1]; n = int(sys.argv[2],0) if len(sys.argv) > 2 else 128; off = int(sys.argv[3],0) if len(sys.argv) > 3 else 0
b = open(p, 'rb').read()
print(p, len(b))
for o in range(off, min(off + n, len(b)), 16):
    c = b[o:o+16]
    w = struct.unpack('<%dI' % (len(c)//4), c[:len(c)//4*4])
    print(f"{o:06x} {c.hex(' ')}  {' '.join(f'{x:08x}' for x in w)}  {''.join(chr(x) if 32<=x<127 else '.' for x in c)}")
