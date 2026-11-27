import os
DATA = os.environ.get("AC2_DATA", "data/ac2")  # folder of unzipped AC2_*.zip groups
import sys, glob, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from model import load, to_tris
import numpy as np

def find(i):
    r = glob.glob(f'{DATA}/*/{i:05d}.bin'); return r[0] if r else None

def draw(ax, path, title):
    try:
        tris = to_tris(load(path))
    except Exception as e:
        ax.set_title(f'{title}\n(fail)', fontsize=8); ax.axis('off'); return 0
    if not tris:
        ax.set_title(f'{title}\n(no tris)', fontsize=8); ax.axis('off'); return 0
    # PS2: y down -> flip, show x, z(depth), -y(up)
    T = np.array([[(v[0], v[2], -v[1]) for v in t] for t in tris])
    T = T[np.isfinite(T).all(axis=(1, 2))]
    T = T[(np.abs(T) < 100).all(axis=(1, 2))]
    pc = Poly3DCollection(T, facecolor='#8aa', edgecolor='#234', linewidths=0.1)
    ax.add_collection3d(pc)
    lo, hi = T.reshape(-1, 3).min(0), T.reshape(-1, 3).max(0); c = (lo + hi) / 2; r = (hi - lo).max() / 2 + 1e-3
    ax.set_xlim(c[0]-r, c[0]+r); ax.set_ylim(c[1]-r, c[1]+r); ax.set_zlim(c[2]-r, c[2]+r)
    ax.view_init(15, -60); ax.axis('off')
    ax.set_title(f'{title}\n{len(T)} tris, {hi[2]-lo[2]:.1f} tall', fontsize=8)
    return len(T)

from stitch import stream, scan
B, IDS = stream(301, 828); MODELS = scan(B, IDS)
ids = [int(x) for x in sys.argv[2:]]
cols = 4; rows = (len(ids) + cols - 1) // cols
fig = plt.figure(figsize=(cols * 3, rows * 3))
for k, i in enumerate(ids):
    ax = fig.add_subplot(rows, cols, k + 1, projection='3d')
    if i < 0:
        m = MODELS[-i]; draw(ax, B[m['off']:m['off']+m['size']+0x8000], f"#{-i} (file {m['file']})")
    else:
        p = find(i)
        if p: draw(ax, p, str(i))
plt.tight_layout(); plt.savefig(sys.argv[1], dpi=90)
