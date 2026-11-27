import os
DATA = os.environ.get("AC2_DATA", "data/ac2")  # folder of unzipped AC2_*.zip groups
import struct, sys, glob, os
import numpy as np
from PIL import Image
SW = np.arange(256); SW = (SW & 0xe7) | ((SW & 0x08) << 1) | ((SW & 0x10) >> 1)
def find_textures(b):
    out = []
    for o in range(0, len(b) - 32, 16):
        tot, pix, n, z, lo, hi = struct.unpack_from('<4I2I', b, o)
        if z or not pix or pix & (pix - 1) or n not in (1, 2, 3, 4) or not (pix < tot <= pix + 0x1000): continue
        psm = (lo >> 20) & 0x3f; tw = 1 << ((lo >> 26) & 0xf); th = 1 << (((lo >> 30) | (hi << 2)) & 0xf)
        if psm not in (0x13, 0x14): continue
        bpp = 8 if psm == 0x13 else 4
        if tw * th * bpp // 8 != pix: continue
        out.append(dict(off=o, w=tw, h=th, bpp=bpp, pix=pix, tot=tot))
    return out
def decode(b, t):
    o = t['off'] + 0x20; w, h = t['w'], t['h']
    raw = np.frombuffer(b[o:o + t['pix']], np.uint8)
    if t['bpp'] == 4:
        idx = np.empty(raw.size * 2, np.uint8); idx[0::2] = raw & 0xf; idx[1::2] = raw >> 4
        pal = np.frombuffer(b[o + t['pix']:o + t['pix'] + 64], np.uint8).reshape(16, 4)
    else:
        idx = raw; pal = np.frombuffer(b[o + t['pix']:o + t['pix'] + 1024], np.uint8).reshape(256, 4)[SW]
    pal = pal.astype(np.uint16); pal[:, 3] = np.minimum(pal[:, 3] * 2, 255)
    return Image.fromarray(pal[idx.reshape(h, w)].astype(np.uint8), 'RGBA')
if __name__ == '__main__':
    os.makedirs(sys.argv[1], exist_ok=True)
    for i in sys.argv[2:]:
        p = glob.glob(f'{DATA}/*/{int(i):05d}.bin')[0]; b = open(p, 'rb').read()
        for k, t in enumerate(find_textures(b)):
            decode(b, t).save(f"{sys.argv[1]}/{int(i):05d}_{k}_{t['w']}x{t['h']}.png"); print(i, t)
