"""Dump the AC2 parts catalogue (F block JOIN packages) to JSON, with named stats.

Usage: python3 stats.py F_FOLDER out.json [SLUS_200.14]
F_FOLDER = split_bin.py's split/F (files start with JOIN; no stitching needed).

Record (offset = first printable entry of the package's 2nd offset table, +0x34):
name[18] type[18] u32 price, u8 maker, desc[73], then u16 words from +114 (w0..).
Field names checked against published stats (armoredcore.fandom.com) for heads
ZHD-MO/EGRET, EHD-GN-92, EHD-DOME, ZHD-06/RW, ZHD-8008/S; cores ZCH-GR/1;
legs ZLB-3322/OP, ZLR-ZIO/MATRIX; arms ZAW-XP/T9. See docs/research/gen2-archive.md.
"""
import sys, os, struct, json, re

# F id ranges (AC2) -> category
CATS = [((3400, 3411), 'head'), ((3420, 3425), 'core'), ((3430, 3452), 'arms'), ((3460, 3490), 'legs'),
        ((3500, 3506), 'booster'), ((3520, 3529), 'fcs'), ((3540, 3547), 'generator'),
        ((3560, 3567), 'radiator'), ((3580, 3586), 'inside'), ((3600, 3607), 'extension'),
        ((3620, 3651), 'back'), ((3660, 3691), 'back2'), ((3700, 3724), 'weapon_r'),
        ((3730, 3738), 'weapon_l'), ((3750, 3765), 'optional')]

# Every frame part: w0 weight, w1 energy drain, w7 AP, w8 def shell, w9 def energy, w10 cooling.
COMMON = {0: 'weight', 1: 'energy_drain', 7: 'armor_points', 8: 'def_shell', 9: 'def_energy', 10: 'cooling'}
NAMED = {
    'head': {11: 'system_recovery', 12: 'auto_balancer'},
    'core': {11: 'max_weight', 14: 'ob_power', 15: 'ob_energy_drain'},
    'legs': {12: 'max_weight', 13: 'moving_speed', 14: 'braking', 15: 'landing_stability',
             16: 'defensive_stability', 18: 'stationary_drain'},
    'arms': {},
}

def u16(r, k): return struct.unpack_from('<H', r, 114 + 2 * k)[0]
def f32(r, k): return struct.unpack_from('<f', r, 114 + 2 * k)[0]   # two words, low first

def decode(cat, r):
    d = {}
    if cat in NAMED:
        for k, n in COMMON.items(): d[n] = u16(r, k)
        for k, n in NAMED[cat].items(): d[n] = u16(r, k)
    else:
        d['weight'], d['energy_drain'] = u16(r, 0), u16(r, 1)
    if cat == 'head':
        w13, w14 = u16(r, 13), u16(r, 14)
        d['computer_type'] = ['rough', 'standard', 'detailed'][w13 & 0xF] if (w13 & 0xF) < 3 else w13 & 0xF
        d['computer_voice'] = 'male' if (w13 >> 4) & 0xF else 'female'
        d['map_type'] = w13 >> 8          # 0 no memory, 1 area memory, 3 area & place name (2: ?)
        d['hacking_function'] = w14 & 0xFF
        d['radar'] = bool(w14 & 0x100)
        d['noise_canceler'] = bool(w14 & 0x200)
        d['bio_sensor'] = bool(w14 & 0x400)
        if d['radar']: d['radar_range'] = round(f32(r, 15))
    if cat == 'core':
        d['optional_slots'] = u16(r, 13) & 0xFF
    if cat == 'legs':
        w11 = u16(r, 11)
        d['leg_type'] = w11 & 0xFF           # 0 humanoid, 1 reverse joint, 4 hover (others: see doc)
        d['jump'] = w11 >> 8                 # 0 = no jump function
    return d

def info(b):
    for o in struct.unpack_from('<8I', b, 0x34):
        if o != 0xffffffff and o + 178 <= len(b) and all(32 <= c < 127 for c in b[o:o + 6]):
            return o
    return None

def elf_codes(path):
    b = open(path, 'rb').read()
    return [m.group().decode().split('/')[-1] for m in re.finditer(rb'/a2/ac2/model/ac/parts/[\x21-\x7e]+', b)]

def main(fdir, out, elf=None):
    parts = []
    for (lo, hi), cat in CATS:
        for i in range(lo, hi + 1):
            p = os.path.join(fdir, '%05d.bin' % i)
            if not os.path.exists(p): continue
            b = open(p, 'rb').read()
            if b[:4] != b'JOIN': continue
            o = info(b)
            if o is None: continue
            r = b[o:o + 0x140]
            parts.append(dict(f_id=i, category=cat, name=r[:18].split(b'\0')[0].decode().strip(),
                              type=r[18:36].split(b'\0')[0].decode().strip(), price=struct.unpack_from('<I', r, 36)[0],
                              maker=r[40], desc=r[41:114].split(b'\0')[0].decode('latin1').strip(),
                              stats=decode(cat, r), raw_words=list(struct.unpack_from('<32H', r, 114))))
    res = dict(parts=parts)
    if elf: res['elf_codes_in_order'] = elf_codes(elf)
    json.dump(res, open(out, 'w'), indent=1)
    print(len(parts), 'parts')

if __name__ == '__main__':
    main(*sys.argv[1:4])
