"""Textured AC2 part loader + tiny software renderer.

How texturing works (decoded 2026-11-27, session 3):
- Inside each in-game (B block) mesh, before its triangle strips, the DMA chain sends a
  small UNPACK to VU address 256 holding a GIF A+D packet. Register 0x06 = TEX0_1:
  the texture used by the strips that follow. TEX0 = 0 means untextured.
- Every texture in the part's F-block JOIN package carries the same TEX0 value in its
  header (+0x10). So: strip texture = the F texture with an identical TEX0.
- UVs are 0..1 floats (qword 3 of each vertex), vertex colour qword 4 = 128.0 (neutral).

Pairing: the k-th B model in a category = the k-th F entry (see CATS).
Usage: AC2_DATA=<data dir> python3 textured.py OUT.png CATEGORY INDEX
       e.g. python3 textured.py head.png head 1
"""
import os, sys, struct
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
DATA = os.environ.get('AC2_DATA', 'data/ac2')
os.environ.setdefault('AC2_F', DATA + '/AC2_F_3400-3801')
from vif import parse_dma
from join import pkg, tables
from tex import find_textures, decode
from stitch import stream, scan

# category: (first stitched B model index, step, first F id, count)
CATS = {
    'core': (0, 1, 3420, 6), 'booster': (6, 2, 3500, 7), 'head': (22, 1, 3400, 12),
    'arms': (34, 1, 3430, 23), 'legs': (57, 1, 3460, 31), 'rweapon': (88, 1, 3700, 25),
    'lweapon': (113, 1, 3730, 9), 'back': (122, 1, 3620, 32),
}
_B = None
def b_models():
    global _B
    if _B is None:
        b, ids = stream(301, 828); _B = (b, scan(b, ids))
    return _B

def part(cat, k):
    """(model bytes, F id) for the k-th part (0-based) of a category."""
    first, step, fid, n = CATS[cat]
    b, ms = b_models(); m = ms[first + k * step]
    return b[m['off']:m['off'] + m['size'] + 0x8000], fid + k

def load(b):
    """Meshes: list of dict(pos, strips=[dict(v, n, uv, tex0)])."""
    cnt = struct.unpack_from('<I', b, 0)[0]
    hdr = 0x10 + cnt * 16
    starts = [struct.unpack_from('<IIHH', b, 0x10 + i * 16)[1] for i in range(cnt)]
    meshes = []
    for i in range(cnt):
        start = starts[i]
        lim = min([s for s in starts if s > start] + [len(b)])
        pos = struct.unpack_from('<3f', b, hdr + i * 0x50)
        out, _ = parse_dma(b, start, lim)
        tex0 = 0; strips = []
        for cmd, addr, n, d, off in out:
            if cmd != 0x6c: continue
            if addr == 256:  # GIF A+D register writes
                for k in range(1, n):
                    data, reg = struct.unpack_from('<QQ', d, k * 16)
                    if reg == 0x06: tex0 = data
                continue
            if n < 5: continue
            nloop = struct.unpack_from('<I', d, 0)[0] & 0x7fff
            if not nloop: continue
            st = (n - 1) // nloop
            if st < 3: continue
            q = np.frombuffer(d[16:16 + nloop * st * 16], '<f4').reshape(nloop, st, 4)
            strips.append(dict(v=q[:, 0, :3] + pos, n=q[:, 1, :3], uv=q[:, 2, :2].copy(), tex0=tex0))
        meshes.append(dict(pos=pos, strips=strips))
    return meshes

def textures(fid):
    """{tex0: PIL image} for one F-block package."""
    p = pkg(fid); res = {}
    if not p: return res
    t1, t2 = tables(p)
    for k in range(8):
        if t2[k] == 0xffffffff: continue
        sec = p[t2[k]:t2[k] + t1[k]]
        for t in find_textures(sec):
            res[struct.unpack_from('<Q', sec, t['off'] + 0x10)[0]] = decode(sec, t)
    return res

def triangles(meshes):
    """Flat arrays: positions (N,3,3), uvs (N,3,2), tex0 per tri."""
    P, U, T = [], [], []
    for m in meshes:
        for s in m['strips']:
            for k in range(len(s['v']) - 2):
                idx = (k, k + 1, k + 2) if k % 2 == 0 else (k + 1, k, k + 2)
                P.append(s['v'][list(idx)]); U.append(s['uv'][list(idx)]); T.append(s['tex0'])
    return np.array(P), np.array(U), T

def render(P, U, T, texs, size=512, yaw=35, pitch=15, tint=(150, 160, 170)):
    """Orthographic z-buffer rasterizer. PS2 y is down; shown as up."""
    a, e = np.radians(yaw), np.radians(pitch)
    Ry = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    Rx = np.array([[1, 0, 0], [0, np.cos(e), -np.sin(e)], [0, np.sin(e), np.cos(e)]])
    V = P.reshape(-1, 3) * [1, -1, 1] @ (Rx @ Ry).T
    lo, hi = V.min(0), V.max(0); sc = (size * 0.9) / (hi - lo)[:2].max()
    V = (V - (lo + hi) / 2) * sc + [size / 2, size / 2, 0]
    V = V.reshape(-1, 3, 3)
    img = np.full((size, size, 3), 32, np.float32); zb = np.full((size, size), -1e9)
    tx = {k: np.asarray(v.convert('RGB'), np.float32) for k, v in texs.items()}
    light = np.array([0.4, 0.6, -0.7]); light /= np.linalg.norm(light)
    for tri, uv, t in zip(V, U, T):
        x, y = tri[:, 0], size - tri[:, 1]
        n = np.cross(tri[1] - tri[0], tri[2] - tri[0]); nn = np.linalg.norm(n)
        if nn == 0: continue
        shade = 0.35 + 0.65 * abs(n @ light) / nn
        x0, x1 = int(max(0, x.min())), int(min(size - 1, x.max())) + 1
        y0, y1 = int(max(0, y.min())), int(min(size - 1, y.max())) + 1
        if x0 >= x1 or y0 >= y1: continue
        gx, gy = np.meshgrid(np.arange(x0, x1) + .5, np.arange(y0, y1) + .5)
        d = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(d) < 1e-9: continue
        w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / d
        w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / d
        w2 = 1 - w0 - w1
        m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not m.any(): continue
        z = w0 * tri[0, 2] + w1 * tri[1, 2] + w2 * tri[2, 2]
        sub = zb[y0:y1, x0:x1]; m &= z > sub
        if t in tx:
            im = tx[t]; h, w = im.shape[:2]
            u = (w0 * uv[0, 0] + w1 * uv[1, 0] + w2 * uv[2, 0]) % 1
            v = (w0 * uv[0, 1] + w1 * uv[1, 1] + w2 * uv[2, 1]) % 1
            col = im[(v * h).astype(int) % h, (u * w).astype(int) % w] / 255 * tint
        else:
            col = np.broadcast_to(np.array([90, 90, 100], np.float32), gx.shape + (3,))
        img[y0:y1, x0:x1][m] = (col * shade)[m]; sub[m] = z[m]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))

if __name__ == '__main__':
    b, fid = part(sys.argv[2], int(sys.argv[3]))
    meshes = load(b); texs = textures(fid)
    P, U, T = triangles(meshes)
    used = sorted(set(T))
    print('F', fid, 'tris', len(P), 'tex0 used', [hex(t) for t in used],
          'matched', sum(t in texs for t in used if t), '/', sum(1 for t in used if t))
    render(P, U, T, texs).save(sys.argv[1])
