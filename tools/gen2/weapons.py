"""Dump AC2 weapon stats (file ID 10) joined to part names (F block).
Usage: AC2_DATA=<data dir> python3 weapons.py out.json

Weapon table: file 00010.bin, offset 0x280b0, 0x80 bytes per row.
Row number = the u16 at +132 of the part's F-block info record (rifles 32..,
back weapons 64..87, extensions 110..115). Rows 0-31 are used by enemies.
Fields (u16 index into the row), checked against published AC2 stats
(ZWG-SRF/8 521/80/90/44/93, EWG-SRF-9 608/60/100/52/132, ZWG-HC-IR/K99 2515/12/92/82):
  w0 hi byte  projectile id
  w2          number of ammo
  w3          ammo price (solid) / energy used per shot (energy weapons)
  w8          reload time (back cannons: a larger value, meaning not decoded)
  w12         0x17 (23) = solid, 0x27 (39) = energy
  w13         attack power
  w14         ammo heat
  w15         stun (probably)
Range is in a separate 0x100-stride table (e.g. 0x24fca holds 1000 for ZWG-SRF/8);
how a weapon picks its row there is not decoded yet."""
import sys, os, struct, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
DATA = os.environ.get('AC2_DATA', 'data/ac2')
os.environ.setdefault('AC2_F', DATA + '/AC2_F_3400-3801')
from join import pkg, info

BASE, STRIDE = 0x280b0, 0x80

def table():
    return open(DATA + '/AC2_A_0001-0190/00010.bin', 'rb').read()

def row(b, i):
    w = struct.unpack_from('<18H', b, BASE + i * STRIDE)
    return dict(row=i, projectile=w[0] >> 8, ammo=w[2], ammo_price_or_energy=w[3],
                reload=w[8], energy=bool(w[12] & 0x20), attack=w[13], ammo_heat=w[14],
                stun=w[15], raw=list(w))

def weapon_parts():
    out = []
    for i in list(range(3600, 3608)) + list(range(3620, 3652)) + list(range(3700, 3725)):  # blades/shields (3730+) keep stats in their own record
        p = pkg(i)
        o = info(p) if p else None
        if o is None or o + 136 > len(p): continue
        r = p[o:o + 136]
        idx = struct.unpack_from('<H', r, 132)[0]
        out.append((i, r[:18].decode().strip(), r[18:36].decode().strip(), idx))
    return out

if __name__ == '__main__':
    b = table()
    res = []
    for f_id, name, typ, idx in weapon_parts():
        if 0 < idx < 128 and any(struct.unpack_from('<18H', b, BASE + idx * STRIDE)[2:4]):
            res.append(dict(f_id=f_id, name=name, type=typ, **row(b, idx)))
    json.dump(res, open(sys.argv[1], 'w'), indent=1)
    for r in res:
        print('%-15s %-17s atk %5d ammo %4d reload %3d heat %4d %s' % (
            r['name'], r['type'], r['attack'], r['ammo'], r['reload'], r['ammo_heat'], 'EN' if r['energy'] else ''))
