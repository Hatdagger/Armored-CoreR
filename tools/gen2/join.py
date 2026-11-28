"""Read AC2 F-block JOIN packages (parts catalogue, IDs 3400-3801).
Usage: set AC2_F to the folder of extracted F files (NNNNN.bin)."""
import struct, os
D = os.environ.get('AC2_F', 'data/ac2/F') + '/'
def rd(i):
    p = D + '%05d.bin' % i
    return open(p, 'rb').read() if os.path.exists(p) else b''
def pkg(i):
    # Data starts 32 KB in and spills into the next IDs, so stitch 3 files.
    b = rd(i) + rd(i + 1) + rd(i + 2)
    j = b.find(b'JOIN', 0x8000)
    if j != 0x8000: j = b.find(b'JOIN')
    return b[j:] if j >= 0 else None
def tables(p):
    return list(struct.unpack_from('<8I', p, 0x14)), list(struct.unpack_from('<8I', p, 0x34))
def info(p):
    """Offset of the name/stats record (first printable entry in table 2)."""
    for o in tables(p)[1]:
        if o != 0xffffffff and o + 40 < len(p) and all(32 <= c < 127 for c in p[o:o + 36]):
            return o
    return None
