"""Export an assembled, textured AC2 AC as a binary glTF (.glb).

Usage: AC2_DATA=<data dir> python3 gltf.py OUT.glb [cat=index ...]
Default build = the PR #64 test AC: core 0, head 0, arms 1, legs 9, booster 0, rweapon 0.

Space: Armored-CoreR axes (AC2 x, -y, -z): y up, right hand at -x (as PR #64).
Units are AC2 model units (AC about 9.5 tall); PR #64 scales by 83.5 to Gen 1 height.

Per-mesh 0x50 block (B model): pos[3] 1, rot[3] (radians) 1, scale[3] 1, then
3 x (count, offset) (strip count, unique-vertex count, ?), 0, 0xffffffff. No parent
is stored, so the skeleton is inferred: arms/legs chain each mesh to the previous mesh
on the same side (x sign), centre pieces hang off mesh 0. Parts are stored already
assembled, so every part sits at the origin. Hand weapons go under the right hand.
Rotation order assumed X then Y then Z (unconfirmed; only small core pieces use it).
Untextured single triangles are markers (muzzle, mounts, feet) -> empty nodes.
"""
import os, sys, io, json, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from textured import part, load, textures

S = np.diag([1.0, -1.0, -1.0])  # AC2 -> Armored-CoreR

def euler(r):
    cx, cy, cz = np.cos(r); sx, sy, sz = np.sin(r)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx

def quat(R):
    w = np.sqrt(max(0, 1 + np.trace(R))) / 2
    if w < 1e-6: return [0.0, 0.0, 0.0, 1.0]
    return [float((R[2, 1] - R[1, 2]) / (4 * w)), float((R[0, 2] - R[2, 0]) / (4 * w)),
            float((R[1, 0] - R[0, 1]) / (4 * w)), float(w)]

def mesh_blocks(b):
    cnt = struct.unpack_from('<I', b, 0)[0]
    out = []
    for i in range(cnt):
        f = struct.unpack_from('<12f', b, 0x10 + cnt * 16 + i * 0x50)
        out.append((np.array(f[0:3]), np.array(f[4:7]), np.array(f[8:11])))
    return out

def parents(cat, pos):
    n = len(pos); par = [-1] * n
    for i in range(1, n):
        if cat in ('arms', 'legs'):
            side = np.sign(round(pos[i][0], 3))
            prev = [j for j in range(i) if side != 0 and np.sign(round(pos[j][0], 3)) == side]
            par[i] = prev[-1] if prev else (-1 if cat == 'arms' else 0)
        else:
            par[i] = 0
    return par

class GLB:
    def __init__(self):
        self.bin = bytearray(); self.g = dict(asset=dict(version='2.0', generator='tools/gen2/gltf.py'),
            scene=0, scenes=[dict(nodes=[])], nodes=[], meshes=[], materials=[], textures=[],
            images=[], samplers=[dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)],
            accessors=[], bufferViews=[], buffers=[])
        self.tex_ids = {}
    def view(self, data, target=None):
        while len(self.bin) % 4: self.bin.append(0)
        v = dict(buffer=0, byteOffset=len(self.bin), byteLength=len(data))
        if target: v['target'] = target
        self.bin += data; self.g['bufferViews'].append(v); return len(self.g['bufferViews']) - 1
    def acc(self, arr, typ, ctype=5126, target=34962):
        a = dict(bufferView=self.view(arr.tobytes(), target), componentType=ctype, count=len(arr), type=typ)
        if typ == 'VEC3': a['min'] = arr.min(0).tolist(); a['max'] = arr.max(0).tolist()
        self.g['accessors'].append(a); return len(self.g['accessors']) - 1
    def material(self, key, img):
        if key in self.tex_ids: return self.tex_ids[key]
        m = dict(name='untextured' if img is None else 'tex_%016x' % key,
                 pbrMetallicRoughness=dict(baseColorFactor=[0.6, 0.63, 0.67, 1], metallicFactor=0.3, roughnessFactor=0.6),
                 doubleSided=True)
        if img is not None:
            buf = io.BytesIO(); img.convert('RGB').save(buf, 'PNG')
            self.g['images'].append(dict(bufferView=self.view(buf.getvalue()), mimeType='image/png'))
            self.g['textures'].append(dict(sampler=0, source=len(self.g['images']) - 1))
            m['pbrMetallicRoughness']['baseColorTexture'] = dict(index=len(self.g['textures']) - 1)
        self.g['materials'].append(m); self.tex_ids[key] = len(self.g['materials']) - 1
        return self.tex_ids[key]
    def node(self, **kw):
        self.g['nodes'].append(kw); return len(self.g['nodes']) - 1
    def save(self, path):
        while len(self.bin) % 4: self.bin.append(0)
        self.g['buffers'] = [dict(byteLength=len(self.bin))]
        js = json.dumps(self.g).encode()
        while len(js) % 4: js += b' '
        out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin))
        out += struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(self.bin), 0x004E4942) + bytes(self.bin)
        open(path, 'wb').write(out)

def add_part(glb, cat, k, parent_node=None, offset=None):
    b, fid = part(cat, k)
    meshes = load(b); texs = textures(fid); blocks = mesh_blocks(b)
    pos = [blk[0] for blk in blocks]; par = parents(cat, pos)
    root = glb.node(name='%s_%d_F%d' % (cat, k, fid), children=[], extras=dict(f_id=fid))
    if offset is not None: glb.g['nodes'][root]['translation'] = (S @ offset).tolist()
    if parent_node is None: glb.g['scenes'][0]['nodes'].append(root)
    else: glb.g['nodes'][parent_node].setdefault('children', []).append(root)
    ids = []; world_R = []
    for i, (m, (p, r, s)) in enumerate(zip(meshes, blocks)):
        R = euler(r); world_R.append(R)
        # node transform is relative to its parent mesh (rotation-free parents assumed for translation)
        pp = pos[par[i]] if par[i] >= 0 else np.zeros(3)
        nd = glb.node(name='%s_mesh%d' % (cat, i), translation=(S @ (p - pp)).tolist(),
                      rotation=quat(S @ R @ S), children=[])
        ids.append(nd)
        (glb.g['nodes'][ids[par[i]]] if par[i] >= 0 else glb.g['nodes'][root])['children'].append(nd)
        prims = {}; markers = []
        for st in m['strips']:
            v = (st['v'] - p) @ np.linalg.inv(R).T  # back to mesh-local
            if st['tex0'] == 0 and len(v) == 3:
                markers.append(v.mean(0)); continue
            P, N, U = prims.setdefault(st['tex0'], ([], [], []))
            for t in range(len(v) - 2):
                idx = [t, t + 1, t + 2] if t % 2 == 0 else [t + 1, t, t + 2]
                P.extend(v[idx]); N.extend(st['n'][idx]); U.extend(st['uv'][idx])
        gp = []
        for t0, (P, N, U) in prims.items():
            P = np.array(P, np.float32) @ S.T; P = P[:, :].astype(np.float32)
            N = np.array(N, np.float32) @ S.T
            ln = np.linalg.norm(N, axis=1, keepdims=True); N = (N / np.where(ln == 0, 1, ln)).astype(np.float32)
            # orient each triangle so its face normal agrees with the stored vertex normals
            P3 = P.reshape(-1, 3, 3); N3 = N.reshape(-1, 3, 3); U3 = np.array(U, np.float32).reshape(-1, 3, 2)
            fn = np.cross(P3[:, 1] - P3[:, 0], P3[:, 2] - P3[:, 0])
            flip = (fn * N3.sum(1)).sum(1) < 0
            for A in (P3, N3, U3): A[flip] = A[flip][:, [0, 2, 1]]
            P, N, U = P3.reshape(-1, 3), N3.reshape(-1, 3), U3.reshape(-1, 2)
            gp.append(dict(attributes=dict(POSITION=glb.acc(P, 'VEC3'), NORMAL=glb.acc(N, 'VEC3'),
                           TEXCOORD_0=glb.acc(U, 'VEC2')), material=glb.material(t0, texs.get(t0))))
        if gp:
            glb.g['meshes'].append(dict(name='%s_%d_m%d' % (cat, k, i), primitives=gp))
            glb.g['nodes'][nd]['mesh'] = len(glb.g['meshes']) - 1
        for j, mk in enumerate(markers):
            glb.g['nodes'][nd]['children'].append(glb.node(name='%s_mesh%d_marker%d' % (cat, i, j),
                                                           translation=(S @ mk).tolist()))
    return root, ids, pos

def build(spec, out):
    glb = GLB()
    hands = None
    for cat, k in spec:
        if cat in ('rweapon',):
            continue
        root, ids, pos = add_part(glb, cat, k)
        if cat == 'arms':
            # right hand = the arms' last joint on the -x side (as PR #64; Gen 1's right is -x)
            h = [i for i in range(len(pos)) if pos[i][0] < 0][-1]
            hands = ids[h]
    for cat, k in spec:
        if cat == 'rweapon':
            add_part(glb, cat, k, parent_node=hands)
    glb.save(out)
    return glb

if __name__ == '__main__':
    spec = [('core', 0), ('head', 0), ('arms', 1), ('legs', 9), ('booster', 0), ('rweapon', 0)]
    if len(sys.argv) > 2:
        spec = [(a.split('=')[0], int(a.split('=')[1])) for a in sys.argv[2:]]
    g = build(spec, sys.argv[1])
    print('nodes', len(g.g['nodes']), 'meshes', len(g.g['meshes']), 'textures', len(g.g['images']),
          'bytes', os.path.getsize(sys.argv[1]))
