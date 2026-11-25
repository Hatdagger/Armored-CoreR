"""Master of Arena mission units: the enemy / vehicle / turret models of a
mission map (FDAT 5s+4 chunk 2, the model set at slot 0x80) and which
objects use them. Notes: docs/research/gen1-stages.md, "Mission units".

  python3 -I moa_units.py <import>/unpacked                 roster of maps 0-19
  python3 -I moa_units.py <import>/unpacked <stage> <slot> <out.png>
                                                            picture of one unit

A unit entry (after its u32 size): an animation block in PA entry 0's layout
(minus that entry's size word: byte 0 = animation count, byte 1 = nodes,
then the block length, 0x1C, joint table, animation list, end), then
`u32 3 (lods), u32 objects` and a TMD object table in the AC part mesh
layout (objects = pieces x 3). Object handler n of the mission code draws
slot 0x80 + n.
"""
import collections
import hashlib
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import moa_anim  # noqa: E402
import moa_stage  # noqa: E402
from moa_model import parse  # noqa: E402


def stage_file(root, s):
    return open(os.path.join(root, 'GG', 'COM', 'FDAT', '%03d.bin' % (5 * s + 4)), 'rb').read()


def units(root, s):
    """Chunk 2 entries by slot (b'' = empty slot)."""
    c = moa_stage.chunks(stage_file(root, s))[2]
    n = struct.unpack_from('<H', c, 0)[0]
    o, out = 4, []
    for _ in range(n):
        sz = struct.unpack_from('<I', c, o)[0]
        out.append(c[o + 4:o + sz] if sz >= 5 else b'')
        o += sz
    return out


def objects_by_handler(root, s):
    c = moa_stage.chunks(stage_file(root, s))[13]
    cnt = collections.Counter()
    for i in range(3, len(c) // 40):
        r = c[i * 40:i * 40 + 40]
        h = struct.unpack_from('<h', r, 6)[0]
        if h != -1 and any(r):
            cnt[h] += 1
    return cnt


def split(data):
    """(animation count, joints, anim block with a dummy size word in front, mesh).
    Objects = pieces x LODs, LOD 0 first; pieces = the joint table's slots
    (the u32 before the object count is not the LOD count: the jets have 2)."""
    code, alen = struct.unpack_from('<2I', data, 0)
    _, nobj = struct.unpack_from('<2I', data, alen)
    d = b'\0\0\0\0' + data
    _, slot, _ = moa_anim.joint_table(d, struct.unpack_from('<7I', d, 0))
    pieces = max(slot.values()) + 1
    mesh = struct.pack('<6I', len(data), 0, 0xC, pieces, nobj // pieces, nobj) + data[alen + 8:]
    return code & 0xFF, (code >> 8) & 0xFF, d, parse(mesh)


def anim_records(d, nanim):
    h = struct.unpack_from('<7I', d, 0)
    base, end = h[4], h[5]
    offs = struct.unpack_from('<%dI' % nanim, d, base)
    starts = sorted(set(x for x in offs if x))
    out = {}
    for k, x in enumerate(offs):
        if not x and k:
            continue
        nxt = [v for v in starts if v > x]
        out[k] = (base + x, base + nxt[0] if nxt else end)
    return h, out


def draw(data, out, title=''):
    import numpy as np
    from PIL import Image, ImageDraw
    nanim, joints, d, mesh = split(data)
    h, recs = anim_records(d, nanim)
    _, slot, parent = moa_anim.joint_table(d, h)
    st = moa_anim.state(moa_anim.parse_rec(d, *recs[0]), 0) if 0 in recs else {}
    W = moa_anim.world(parent, st)
    tris = []
    for node, sl in slot.items():
        if sl >= mesh['parts']:
            continue
        o = mesh['objs'][sl]
        V = [(W[node] @ np.array([x, y, z, 1.0]))[:3] * (1, -1, 1) for x, y, z in o['verts']]
        for p in o['prims']:
            vs = [V[q] for q in p[2] if q < len(V)]
            if len(vs) == 4:
                tris += [(vs[0], vs[1], vs[2]), (vs[1], vs[3], vs[2])]
            elif len(vs) == 3:
                tris.append(tuple(vs))
    allv = [v for t in tris for v in t] or [np.zeros(3)]
    lo, hi = np.min(allv, axis=0), np.max(allv, axis=0)
    c0, r = (lo + hi) / 2, (max(hi - lo) / 2) or 1
    S = 360
    img = Image.new('RGB', (S * 2, S + 18), (30, 34, 40))
    dr = ImageDraw.Draw(img)
    dr.text((4, 2), f'{title} {nanim} animations, {len(slot)} jointed pieces of {mesh["parts"]}, '
                    f'{int(2 * r)} model units across', fill=(255, 255, 255))
    for ya, ox in ((0.6, 0), (2.17, S)):
        c, sn = math.cos(ya), math.sin(ya)
        T = []
        for t in tris:
            q = [((v[0] - c0[0]) * c + (v[2] - c0[2]) * sn, v[1] - c0[1],
                  -(v[0] - c0[0]) * sn + (v[2] - c0[2]) * c) for v in t]
            nrm = np.cross(np.subtract(q[1], q[0]), np.subtract(q[2], q[0]))
            g = int(200 * (0.35 + 0.65 * abs(nrm[2]) / (np.linalg.norm(nrm) + 1e-9)))
            T.append((sum(x[2] for x in q),
                      [(ox + S / 2 + x[0] / r * S * 0.45, 18 + S / 2 - x[1] / r * S * 0.45) for x in q], g))
        for _, pts, g in sorted(T, key=lambda t: -t[0]):
            dr.polygon(pts, fill=(g, g, g), outline=(20, 20, 20))
    img.save(out)


def roster(root):
    ids = {}
    for s in range(20):
        u = units(root, s)
        cnt = objects_by_handler(root, s)
        parts = []
        for k in sorted(set(cnt) | set(range(1, len(u)))):
            d = u[k] if k < len(u) else b''
            if d:
                tag = ids.setdefault(hashlib.sha1(d).hexdigest(), 'U%02d' % len(ids))
                tag += '(%d anims)' % d[0]
            else:
                tag = 'no model'
            parts.append(f'handler {k}: {cnt.get(k, 0)} x {tag}')
        print(f'map {s:2d}: ' + '; '.join(parts))


if __name__ == '__main__':
    if len(sys.argv) == 2:
        roster(sys.argv[1])
    else:
        root, s, k, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
        draw(units(root, s)[k], out, f'map {s} slot {k}:')
