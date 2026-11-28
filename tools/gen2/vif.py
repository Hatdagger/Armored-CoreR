import struct, sys
def unpack_size(cmd, num):
    vn = (cmd >> 2) & 3; vl = cmd & 3
    bits = (vn + 1) * (32 >> vl) * num
    return ((bits + 31) // 32) * 4
def parse_vif(b, o, end, out):
    end = min(end, len(b))
    while o + 4 <= end:
        w = struct.unpack_from('<I', b, o)[0]; o += 4
        imm = w & 0xffff; num = (w >> 16) & 0xff; cmd = (w >> 24) & 0x7f
        if cmd >= 0x60:
            n = num or 256; sz = unpack_size(cmd, n)
            out.append((cmd & 0x6f, imm & 0x3ff, n, b[o:o+sz], o)) if o+sz<=len(b) else None; o += sz
        elif cmd in (0x30, 0x31): o += 16
        elif cmd == 0x20: o += 4
        elif cmd in (0x50, 0x51): o += (imm or 65536) * 16
        elif cmd == 0x4a: o += (num or 256) * 8
    return o
def parse_dma(b, o, limit=None):
    out = []; limit = limit or len(b); n = 0
    while o + 16 <= limit and n < 10000:
        n += 1
        tag = struct.unpack_from('<Q', b, o)[0]
        qwc = tag & 0xffff; tid = (tag >> 28) & 7
        parse_vif(b, o + 8, o + 16, out)
        if tid in (0, 3, 4):
            o += 16; 
            if tid == 0: break
            continue
        parse_vif(b, o + 16, min(o + 16 + qwc * 16, limit), out)
        o += 16 + qwc * 16
        if tid in (6, 7): break
    return out, o
if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read(); o = int(sys.argv[2], 0)
    out, e = parse_dma(b, o)
    print('end', hex(e))
    for c, a, n, d, off in out[:40]:
        print(hex(off), f'cmd={c:02x} addr={a} n={n}', d[:32].hex())
