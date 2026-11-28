"""Dump AC2 weapon stats (file ID 10) joined to part names (F block).
Usage: AC2_DATA=<data dir> python3 weapons.py out.json
       python3 weapons.py --diff AC2_00010.bin AA_00010.bin   (AC2 vs Another Age changes)

Weapon table: file 00010.bin, offset 0x280b0, 0x80 bytes per row.
Row number = the u16 at +132 of the part's F-block info record (rifles 32..,
back weapons 64..87, extensions 110..115). Rows 0-31 are used by enemies.
Fields (u16 index into the row), checked against published AC2 stats
(ZWG-SRF/8 521/80/90/44/93, EWG-SRF-9 608/60/100/52/132, ZWG-HC-IR/K99 2515/12/92/82):
  w0 hi byte  projectile id
  w2          number of ammo
  w3          ammo price (solid) / energy used per shot (energy weapons)
  w7          back cannons (w11 high byte 0x23): reload time (XP0808 26). Missiles: small value,
              not reload (lock time?). Arm guns: 0 (machine guns 5)
  w8          reload time for everything else (S612 48); back cannons: large value, not decoded
  w5          low byte max locks (S602 2, S608 8, S612 12, M444 4); high byte lock time
              (lower = faster: S612 90, LD2 80, normal 110, S608 125, FIN-B00 255)
  w6          low byte projectiles per lock (ZWM-M24/1MI 4, ZWM-LD2/1DA 2)
  w11         high byte = weapon class (0x23 back cannon)
  back cannons w8: high byte probably recoil (XP0808 0x0E = published 14); low byte unknown
  w12         0x17 (23) = solid, 0x27 (39) = energy
  w13         attack power
  w14         ammo heat
  w15         stun (probably)
Ballistics table: file 10 at 0x180b0 (AA 0x200f0), 0x100 bytes per record (128 records), indexed by
the PROJECTILE ID = high byte of weapon-row w0 (usually equal to the row, but not for ZWC-LQ/2552 <->
ZWC-LNT/250, EWG-MGA2 <-> ZWG-AR/K, the EWX/ZWX specials 82-87 -> 96-101 and extensions 110-122 ->
112-126; ZXR-S/STEALTH has none). +0 u16 projectile kind (1 missile, 2 bullet, 4 spread, 13 multi/
vertical missile), +2 u16 effect/model id?, +4 u16 projectiles per shot (shotguns 6/8, HYDRA 3,
LNT/250 16), +6 u16 hit-effect id?. Records 82-95 and 108-109 (kinds 23/11) are not weapon projectiles:
23 = carrier whose +6 names a kind-11 child record.
+0x24 float velocity/100, +0x28 float range. Checked: SRF/8 1050/1000, SRF-9 1150/910,
K99 870/620, XP0808 -/600 (armoredcore.fandom.com). Missiles: +0x28 is lock range, not velocity.
w8 = recoil<<8 | reload for ALL weapons (checked in the AC2 shop: ZWC-IR/FLUX 32/120, EWC-XP0808 14/26,
ZWG-AR/K 0/3). w7 is not reload (FLUX 30); possibly charge time.
Another Age: raw reloads are unchanged, but AA's published reloads are floor(raw*5/6) for all 8
known pairs (SRF/8 44->36, K99 92->76, XP0808 26->21, S612 48->40, M444 57->47, RRD 45->37,
T9 24->20, S604 41->34), so AA scales reload in code, not data."""
import sys, os, struct, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
DATA = os.environ.get('AC2_DATA', 'data/ac2')
os.environ.setdefault('AC2_F', DATA + '/AC2_F_3400-3801')
from join import pkg, info

BASE, STRIDE = 0x280b0, 0x80
AA_BASE = 0x380f0
BALL, BALL_STRIDE, AA_BALL = 0x180b0, 0x100, 0x200f0  # velocity/range table, same row index  # same rows in Another Age's file 10 (AC25DATA.BIN)
FIELDS = {2: 'ammo', 3: 'ammo_price_or_energy', 8: 'reload', 13: 'attack', 14: 'ammo_heat', 15: 'stun'}

def table():
    return open(DATA + '/AC2_A_0001-0190/00010.bin', 'rb').read()

def row(b, i):
    w = struct.unpack_from('<18H', b, BASE + i * STRIDE)
    return dict(row=i, projectile=w[0] >> 8, ammo=w[2], ammo_price_or_energy=w[3],
                reload=w[8] & 0xFF, recoil=w[8] >> 8, w7_unknown=w[7], reload_w8=w[8], projectile_id=w[0] >> 8, max_locks=w[5] & 0xFF, lock_time=w[5] >> 8, per_lock=w[6] & 0xFF, energy=bool(w[12] & 0x20), attack=w[13], ammo_heat=w[14],
                stun=w[15], raw=list(w), **ballistics(b, w[0] >> 8))

def ballistics(b, i, base=BALL):
    o = base + i * BALL_STRIDE
    if i == 0: return dict(projectile_kind=0, velocity=0, range=0, per_shot=0)
    kind, _, per_shot = struct.unpack_from('<3H', b, o)
    vel, rng = struct.unpack_from('<2f', b, o + 0x24)
    r = dict(projectile_kind=kind, per_shot=per_shot, velocity=round(vel * 100), range=round(rng, 2))
    if kind in (1, 13):   # homing missile: speeds per frame, turn rates in radians/frame
        f = struct.unpack_from('<7f', b, o + 0x24)
        r.update(launch_speed=f[0], lock_range=f[1], accel=f[2], guidance=f[3], max_speed=f[4],
                 turn_rate_deg=round(f[5] * 57.2958, 2), turn_rate2_deg=round(f[6] * 57.2958, 2),
                 lifetime=struct.unpack_from('<H', b, o + 0x20)[0])
    return r

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

def diff(a_path, b_path):
    a = open(a_path, 'rb').read(); b = open(b_path, 'rb').read()
    for i in range(128):
        x = struct.unpack_from('<18H', a, BASE + i * STRIDE); y = struct.unpack_from('<18H', b, AA_BASE + i * STRIDE)
        if x == y: continue
        tag = 'NEW' if not any(x[2:4]) else 'changed'
        print(i, tag, [(FIELDS.get(k, 'w%d' % k), x[k], y[k]) for k in range(18) if x[k] != y[k]])

if __name__ == '__main__':
    if sys.argv[1] == '--diff':
        diff(sys.argv[2], sys.argv[3]); sys.exit()
    b = table()
    res = []
    for f_id, name, typ, idx in weapon_parts():
        if 0 < idx < 128 and any(struct.unpack_from('<18H', b, BASE + idx * STRIDE)[2:4]):
            res.append(dict(f_id=f_id, name=name, type=typ, **row(b, idx)))
    json.dump(res, open(sys.argv[1], 'w'), indent=1)
    for r in res:
        print('%-15s %-17s atk %5d ammo %4d reload %3d heat %4d vel %4d rng %6g %s' % (
            r['name'], r['type'], r['attack'], r['ammo'], r['reload'], r['ammo_heat'], r['velocity'], r['range'], 'EN' if r['energy'] else ''))
