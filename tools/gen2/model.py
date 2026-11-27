import os
import struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vif import parse_dma

def load(path, base=0):
    b = open(path, 'rb').read() if isinstance(path, str) else path
    b = b[base:]
    cnt, size, m1, _ = struct.unpack_from('<4I', b, 0)
    assert m1 == 0xffffffff
    meshes = []
    hdr = 0x10 + cnt * 16
    starts = [struct.unpack_from('<IIHH', b, 0x10 + i * 16)[1] for i in range(cnt)]
    for i in range(cnt):
        end, start, a, c = struct.unpack_from('<IIHH', b, 0x10 + i * 16)
        lim = min([s for s in starts if s > start] + [len(b)])
        pos = struct.unpack_from('<3f', b, hdr + i * 0x50)
        out, _ = parse_dma(b, start, lim)
        strips = []
        for cmd, addr, n, d, off in out:
            if cmd != 0x6c or n < 5: continue
            q = [struct.unpack_from('<4f', d, k * 16) for k in range(n)]
            nloop = struct.unpack_from('<I', d, 0)[0] & 0x7fff
            if nloop == 0: continue
            stride = (n - 1) // nloop
            if stride < 1: continue
            verts = [q[1 + k * stride][:3] for k in range(nloop)]
            uvs = [q[1 + k * stride + 2][:2] for k in range(nloop)] if stride >= 3 else None
            strips.append((verts, uvs, stride))
        meshes.append((pos, strips))
    return meshes

def to_tris(meshes):
    tris = []
    for pos, strips in meshes:
        for verts, uvs, stride in strips:
            v = [(x + pos[0], y + pos[1], z + pos[2]) for x, y, z in verts]
            for k in range(len(v) - 2):
                tris.append((v[k], v[k + 1], v[k + 2]))
    return tris

if __name__ == '__main__':
    m = load(sys.argv[1])
    for pos, s in m:
        print('mesh at', [round(p, 2) for p in pos], 'strips', len(s), 'verts', sum(len(x[0]) for x in s), 'strides', set(x[2] for x in s))
