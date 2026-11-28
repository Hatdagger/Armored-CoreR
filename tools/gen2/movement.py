"""AC2 / Another Age movement: tuning values, the AC compile (parts -> movement and energy
numbers) and the walk table, as the game computes them.

Usage:
  python3 movement.py SLUS_200.14 [SLUS_202.49]
        the movement tuning values in the ELF (Another Age side by side)
  python3 movement.py SLUS_200.14 --parts P.json --build "head=ZHD-GE/OHR,core=ECM-XR00,..."
        one build's compiled numbers (P = stats.py output). Slots: head core arms legs booster
        fcs generator radiator inside extension back_l back_r arm_r arm_l; options=NAME+NAME
  python3 movement.py --walk B_FOLDER [OUT.json]
        walking speed, jump crouch and landing lengths for the 72 motion sets (B 421-492)
  python3 movement.py SLUS_200.14 --parts P.json --vectors OUT.h
        test vectors for tests/test_gen2_move.c

emu_compile.py runs the game's own compile code in an emulator and checks compile_build()
against it.

Formulas are from the code in SLUS_200.14 (compile 0x145180, helpers 0x146260-0x146b68);
addresses and the per-frame rules are in docs/research/gen2-movement.md. All arithmetic is
single precision, in the game's order, with every result cut off towards zero as the PS2's
FPU does (so 20000 x 0.033 is 659, not 660). A "frame" is one game update (60 a second); a unit is one metre (the HUD shows
speed x 216 as km/h).
"""
import sys, os, struct, json, math

def F(x):
    """To single precision the PS2's way: the fraction that does not fit is cut off (its FPU
    rounds every result towards zero, not to nearest)."""
    f = struct.unpack('<f', struct.pack('<f', x))[0]
    if abs(f) > abs(x):
        f = struct.unpack('<f', struct.pack('<I', struct.unpack('<I', struct.pack('<f', f))[0] - 1))[0]
    return f


def elf(path):
    d = open(path, 'rb').read()
    off, va = struct.unpack_from('<II', d, 52 + 4)
    return d, (lambda v: v - va + off), va


def label_var(d, fo, va, label):
    """Address of the variable a debug-menu label edits (entries: label ptr, variable ptr, ...)."""
    for end in (b':', b';'):
        o = d.find(b'\0' + label.encode() + end)
        if o >= 0: break
    else:
        raise KeyError(label)
    key = struct.pack('<I', o + 1 - fo(va) + va)
    p = d.find(key)
    while p != -1:
        var = struct.unpack_from('<I', d, p + 4)[0]
        if va < var < va + len(d):
            return var
        p = d.find(key, p + 1)
    raise KeyError(label)


# The movement block: floats at fixed offsets from parts_std_totalweight (0x429190 in AC2).
BLOCK = {
    'std_total_weight': 0x00, 'turn_std': 0x04, 'std_upper_weight': 0x08, 'upper_k': 0x0c,
    'turn_k': 0x10, 'turn_lo': 0x14, 'turn_hi': 0x18, 'turn_stdspd': 0x1c,
    'avg_move_speed': 0x20, 'move_k': 0x24, 'move_lo': 0x28, 'move_hi': 0x2c,
    'move_stdacc': 0x30, 'move_stdmaxspd': 0x34, 'boostadd_rate': 0x38, 'boostsub_rate': 0x3c,
    'std_boost_power': 0x44, 'weight_k': 0x48, 'weight_lo': 0x4c, 'weight_hi': 0x50,
    'boost_k': 0x54, 'boost_lo': 0x58, 'boost_hi': 0x5c,
    'std_flymv_acc': 0x60, 'std_flymv_maxspd': 0x64, 'std_flyup_acc': 0x68, 'std_flyup_maxspd': 0x6c,
    'std_dash_acc': 0x70, 'std_dash_maxspd': 0x74, 'brake_std': 0x78, 'std_brake': 0x7c,
    'brake_k': 0x80, 'brake_lo': 0x84, 'brake_hi': 0x88,
    'opt_charge_add': 0xc4, 'opt_turn_rate': 0xd0, 'canoturn_stdspd': 0xe4,
    'ob_std_power': 0x148, 'ob_k': 0x14c, 'ob_speed': 0x150, 'ob_acc_rate': 0x154,
    'boostadd_min': 0x1f0, 'opt_brake_rate': 0x228,
}
# Six floats the compile reads through gp, next to each other (0x43dba4 in AC2).
INLINE = ['jump_rate', 'dashjumpspd', 'land_dmgspd', 'gravity', 'fall_maxspd', 'blade_brk']
# Immediates in the code (same in both games): jump divisor, dash_mintime, blade_spd0,
# boost_add / move_boost_add limits, the OB factor's limits.
FIXED = {'jump_div': 100.0, 'dash_mintime': 13.0, 'blade_spd0': 3.0, 'boostadd_max': 0x180,
         'moveadd_max': 0xc0, 'ob_lo': 0.5, 'ob_hi': 1.0, 'upper_lo': 0.01, 'upper_hi': 1.0}


def constants(path):
    d, fo, va = elf(path)
    base = label_var(d, fo, va, 'parts_std_totalweight')
    c = {k: F(struct.unpack_from('<f', d, fo(base + o))[0]) for k, o in BLOCK.items()}
    i = d.find(struct.pack('<2f', 1.15, 0.55))
    if i < 0: raise ValueError('inline movement constants not found')
    c.update(zip(INLINE, (F(x) for x in struct.unpack_from('<6f', d, i))))
    c.update(FIXED)
    return c


def clampf(x, k, lo, hi):            # 0x146220: 1 + (x - 1) * k, then the two limits
    v = F(F(F(x - 1.0) * k) + 1.0)
    if v < lo: v = lo
    if hi < v: v = hi
    return v


LEG_KIND = {0: 0, 1: 0, 2: 2, 3: 1, 4: 3}   # record leg type -> movement kind
KIND_NAME = ['two legs', 'caterpillar', 'four legs', 'hover']
SLOTS = ['head', 'core', 'arms', 'legs', 'booster', 'fcs', 'generator', 'radiator',
         'inside', 'extension', 'back_l', 'back_r', 'arm_r', 'arm_l']
OPTIONS = ['SP-S/SCR', 'SP-ENE-SCR', 'SP-BCNDR', 'SP-BSI-LE', 'SP-SAB/J', 'SP-CIR-K', 'SP-BE++',
           'SP-BMALAD', 'SP-BF5/LOSP', 'SP-ENE-ACC', 'SP-E/SAVER', 'SP-ECM-JAM', 'SP-M/AUTO',
           'SP-BLS', 'SP-CBRK', 'SP-VIECH']          # bit n of the option mask = n-th optional part
OPT_CHARGE, OPT_TURN, OPT_BRAKE = 1 << 2, 1 << 5, 1 << 14


def compile_build(c, w, option_mask=0, pilot_kind=0):
    """w: slot -> the part's u16 words (stats.py raw_words), absent slots left out.
    Returns the movement / energy numbers the game writes at AC +0x410..+0x490."""
    legs, core, gen = w['legs'], w['core'], w['generator']
    kind = LEG_KIND[legs[11] & 0xff]
    fitted = [w[s] for s in SLOTS if s in w]
    total = sum(p[0] for p in fitted)
    cap = legs[0] + legs[12]
    weight = total if total <= cap else cap                    # 0x146120
    upper = weight - legs[0]
    drain_moving = sum(p[1] for p in fitted)
    drain_standing = drain_moving - legs[1] + legs[18]

    # Booster: caterpillars and hovers (and any AC without a booster) use the legs' own.
    if 'booster' not in w or kind in (1, 3):
        power, charge_drain = legs[19], legs[20]
    else:
        power, charge_drain = w['booster'][7], w['booster'][8]

    o = {'leg_kind': kind, 'weight': weight, 'upper_weight': upper}
    wf = clampf(F(c['std_total_weight'] / F(weight)), c['weight_k'], c['weight_lo'], c['weight_hi'])
    bf = clampf(F(F(power) / c['std_boost_power']), c['boost_k'], c['boost_lo'], c['boost_hi'])
    f = F(wf * bf)
    o['boost_factor'] = bf
    o['flymv_accel'], o['flymv_maxspd'] = F(f * c['std_flymv_acc']), F(f * c['std_flymv_maxspd'])
    o['flyup_accel'], o['flyup_maxspd'] = F(f * c['std_flyup_acc']), F(f * c['std_flyup_maxspd'])
    o['dash_accel'], o['dash_maxspd'] = F(f * c['std_dash_acc']), F(f * c['std_dash_maxspd'])
    brake_rate = c['opt_brake_rate'] if option_mask & OPT_BRAKE else 1.0
    o['dash_brake'] = F(clampf(F(F(F(legs[14]) / c['brake_std']) * brake_rate),
                               c['brake_k'], c['brake_lo'], c['brake_hi']) * c['std_brake'])
    if kind:
        m = clampf(F(F(legs[13]) / c['avg_move_speed']), c['move_k'], c['move_lo'], c['move_hi'])
        if kind == 3:
            o['flymv_accel'], o['flymv_maxspd'] = F(c['move_stdacc'] * m), F(c['move_stdmaxspd'] * m)
        else:
            o['dash_accel'], o['dash_maxspd'] = F(c['move_stdacc'] * m), F(c['move_stdmaxspd'] * m)

    ob = F(c['ob_speed'] * clampf(F(F(core[14]) / c['ob_std_power']), c['ob_k'], c['ob_lo'], c['ob_hi']))
    o['oboost_spd0'] = o['oboost_maxspd'] = ob
    o['oboost_acc'] = F(ob * c['ob_acc_rate'])

    r = 1.0 if F(upper) <= c['std_upper_weight'] else F(c['std_upper_weight'] / F(upper))
    a = clampf(r, c['upper_k'], c['upper_lo'], c['upper_hi'])
    turn_rate = c['opt_turn_rate'] if option_mask & OPT_TURN else 1.0
    t = int(F(F(legs[22]) * turn_rate))
    b = clampf(F(F(t) / c['turn_std']), c['turn_k'], c['turn_lo'], c['turn_hi'])
    f = F(a * b)
    o['turn_maxspd'] = F(f * c['turn_stdspd'])
    o['turn_accel'] = F(F(o['turn_maxspd'] * F(legs[21])) / 100.0)
    o['canoturn_maxspd'] = F(f * c['canoturn_stdspd'])
    o['canoturn_accel'] = F(F(o['canoturn_maxspd'] * F(legs[23])) / 100.0)

    o['jumpspd'] = F(F(F(legs[11] >> 8) * c['jump_rate']) / c['jump_div'])
    for k in ('dashjumpspd', 'fall_maxspd', 'gravity', 'land_dmgspd', 'dash_mintime'):
        o[k] = c[k]

    sub = int(F(c['boostsub_rate'] * F(charge_drain)))
    if sub <= 0: sub = 1
    add = int(F(F(gen[7] - drain_standing) * c['boostadd_rate']))
    if add < int(c['boostadd_min']): add = int(c['boostadd_min'])
    if add > c['boostadd_max']: add = c['boostadd_max']
    madd = int(F(F(gen[7] - drain_moving) * c['boostadd_rate']))
    if madd <= 0: madd = 1
    if madd > c['moveadd_max']: madd = c['moveadd_max']
    if pilot_kind >= 5:
        madd, sub, add = madd << 1, sub >> 1, add << 1
    if sub < add: add = (add + sub) // 2
    if sub < madd: madd = (madd + sub) // 2
    o['boost_sub'], o['boost_add'], o['move_boost_add'] = sub, add, madd
    o['oboost_sub'] = int(F(F(core[16]) * c['boostsub_rate']))
    o['oboost_1stsub'] = int(F(F(core[15]) * c['boostsub_rate']))
    o['boost_max'] = int(F(F(gen[8]) + (c['opt_charge_add'] if option_mask & OPT_CHARGE else 0.0)))
    o['boost_redarea'] = gen[9]
    return o


def kmh(v): return v * 216.0


def walk_table(folder):
    """Per motion set (B 421-492): walking speed by direction and the clip lengths the states wait on."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from motion_bank import bank, stream
    out = {}
    for s in range(421, 493):
        cs = {m['number']: m for m in bank(stream(folder, s))}
        i = s - 421
        def root(m):
            t = cs[m]['tracks'][0]
            return cs[m]['frames'], round(math.dist(t[0][1], t[-1][1]), 3)
        row = {'core_group': i // 24, 'arms_group': (i % 24) // 8, 'legs_group': i % 8,
               'crouch_frames': cs[35]['frames'], 'land_frames': cs[38]['frames'],
               'hard_land_frames': cs[51]['frames'], 'brake_frames': cs[25]['frames'], 'dirs': []}
        for d in range(8):                       # walk start 1 + 2d, walk loop 2 + 2d
            sf, sd = root(1 + 2 * d); lf, ld = root(2 + 2 * d)
            row['dirs'].append({'start_frames': sf, 'start_dist': sd, 'loop_frames': lf, 'loop_dist': ld,
                                'speed': round(ld / lf, 5) if lf > 1 else 0.0})
        out[s] = row
    return out


def by_name(parts):
    return {p['name']: p for p in parts['parts'] if p['category'] != 'back2'}


def build_words(parts, spec):
    names = by_name(parts); w = {}; mask = 0
    for item in spec.split(','):
        slot, name = item.split('=')
        if slot == 'options':
            for n in name.split('+'): mask |= 1 << OPTIONS.index(n)
        else:
            w[slot] = names[name]['raw_words']
    return w, mask


VECTOR_BUILDS = [   # (label, build, pilot kind)
    ('mid two legs', 'head=ZHD-GE/OHR,core=ECM-XR00,arms=ZAN-707/E,legs=ZLN-XA1/FA,booster=EBT-GE2100,'
     'fcs=DOX-105,generator=GR-XR/15000,radiator=RBG-CM6,arm_r=ZWG-RF/37', 0),
    ('heavy two legs, test AC parts', 'head=ZHD-GE/OHR,core=ZCX-F/ROOK,arms=ZAN-414/SLA,legs=ELN-STVM,'
     'booster=EBT-GE,fcs=DOX-105,generator=HOY-B999,radiator=RBG-CM6,arm_r=ZWG-RF/37', 0),
    ('light reverse joint, options', 'head=ZHD-GE/OHR,core=ZCL-XA/2,arms=ZAN-202/TEM,legs=ELB-5005B,'
     'booster=ZBT-Z1/ARTERE,fcs=DOX-105,generator=HOY-B1000,radiator=RBG-CM6,'
     'options=SP-BCNDR+SP-CIR-K+SP-CBRK', 0),
    ('four legs', 'head=ZHD-GE/OHR,core=ZCH-GR/1,arms=ZAN-707/E,legs=ZLF-TR05/INTS,booster=ZBT-H4/T,'
     'fcs=DOX-105,generator=GPS-BVX/10,radiator=RBG-CM6', 0),
    ('caterpillar', 'head=ZHD-GE/OHR,core=ECH-D4,arms=ZAN-707/E,legs=ELC-D1S1,'
     'fcs=DOX-105,generator=HOY-BV2500,radiator=RBG-CM6', 0),
    ('hover', 'head=ZHD-GE/OHR,core=ECL-ONE,arms=ZAN-707/E,legs=ZLR-ZIO/MATRIX,'
     'fcs=DOX-105,generator=GR-XR/SEED,radiator=RBG-CM6', 0),
    ('mid two legs, enemy pilot kind 5', 'head=ZHD-GE/OHR,core=ECM-XR00,arms=ZAN-707/E,legs=ZLN-XA1/FA,'
     'booster=EBT-V55,fcs=DOX-105,generator=GPS-VA,radiator=RBG-CM6', 5),
]


def sweep_builds(parts, per_legs=4):
    """Every legs part, each with a different mix of core, arms, booster, generator and options
    (the fourth of each legs' builds has no booster). Used for the C test vectors and by
    emu_compile.py, which checks compile_build() against the game's own code."""
    cat = lambda c: [p['name'] for p in parts['parts'] if p['category'] == c]
    legs, boosters, cores, gens, arms = cat('legs'), cat('booster'), cat('core'), cat('generator'), cat('arms')
    out = []; i = 0
    for lg in legs:
        for k in range(4):
            i += 1
            if k >= per_legs: continue
            spec = 'head=ZHD-GE/OHR,core=%s,arms=%s,legs=%s,' % (cores[i % len(cores)], arms[(i * 5) % len(arms)], lg)
            if k != 3: spec += 'booster=%s,' % boosters[i % len(boosters)]
            spec += 'fcs=DOX-105,generator=%s,radiator=RBG-CM6' % gens[(i * 3) % len(gens)]
            opts = [o for b, o in ((1, 'SP-BCNDR'), (2, 'SP-CIR-K'), (4, 'SP-CBRK')) if i & b]
            if opts: spec += ',options=' + '+'.join(opts)
            out.append((lg, spec, 5 if i % 9 == 0 else 0))
    return out


FLOATS = ['boost_factor', 'flymv_accel', 'flymv_maxspd', 'flyup_accel', 'flyup_maxspd', 'dash_accel',
          'dash_maxspd', 'dash_brake', 'oboost_spd0', 'oboost_acc', 'turn_maxspd', 'turn_accel',
          'canoturn_maxspd', 'canoturn_accel', 'jumpspd']
INTS = ['leg_kind', 'weight', 'upper_weight', 'boost_sub', 'boost_add', 'move_boost_add', 'oboost_sub',
        'oboost_1stsub', 'boost_max', 'boost_redarea']


def cfloat(v):
    t = '%.9g' % v
    return t + ('f' if '.' in t or 'e' in t else '.0f')


def write_vectors(c, parts, path):
    def part(w): return '{1, %d, %d}' % (w[0], w[1])
    L = ['/* Generated by tools/gen2/movement.py --vectors from SLUS_200.14 and the AC2 parts',
         ' * catalogue: the AC compile as the game computes it (docs/research/gen2-movement.md).',
         ' * The first seven are picked by hand, the rest are one build per legs part. The script',
         ' * agrees with the game\'s own compile code, run in an emulator (emu_compile.py), on all of them. */',
         'typedef struct { const char *name; acr_gen2_build b; acr_gen2_stats s; } gen2_vector;',
         'static const gen2_vector GEN2_VECTORS[] = {']
    for label, spec, pilot in VECTOR_BUILDS + sweep_builds(parts, 1):
        w, mask = build_words(parts, spec)
        o = compile_build(c, w, mask, pilot)
        lg, co, ge = w['legs'], w['core'], w['generator']
        bo = w.get('booster', [0] * 12)
        slots = ', '.join(part(w[s]) if s in w else '{0, 0, 0}' for s in SLOTS)
        L.append('    { "%s",' % label)
        L.append('      { %s,' % slots)
        L.append('        %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d,   /* legs */'
                 % (lg[11] & 0xff, lg[11] >> 8, lg[12], lg[13], lg[14], lg[18], lg[19], lg[20], lg[21], lg[22], lg[23]))
        L.append('        %d, %d, %d,  %d, %d,  %d, %d, %d,  0x%04x, %d },'
                 % (co[14], co[15], co[16], bo[7], bo[8], ge[7], ge[8], ge[9], mask, pilot))
        L.append('      { %s,' % ', '.join(str(o[k]) for k in INTS))
        L.append('        %s } },' % ', '.join(cfloat(o[k]) for k in FLOATS))
    L.append('};')
    open(path, 'w').write('\n'.join(L) + '\n')


def show(o):
    print('  %s, weight %d (upper body %d)' % (KIND_NAME[o['leg_kind']], o['weight'], o['upper_weight']))
    for k, unit in (('dash_maxspd', 'ground boost / ground top speed'), ('flymv_maxspd', 'air top speed'),
                    ('flyup_maxspd', 'rise top speed'), ('oboost_maxspd', 'overboost'), ('fall_maxspd', 'fall top speed')):
        print('  %-16s %.4f a frame = %6.1f km/h  (%s)' % (k, o[k], kmh(o[k]), unit))
    print('  accel a frame: dash %.4f, air %.4f, rise %.4f, brake %.4f; jump %.4f; gravity %.3f'
          % (o['dash_accel'], o['flymv_accel'], o['flyup_accel'], o['dash_brake'], o['jumpspd'], o['gravity']))
    print('  turning %.5f rad a frame (%.1f deg/s), reached in %.0f frames'
          % (o['turn_maxspd'], math.degrees(o['turn_maxspd']) * 60, o['turn_maxspd'] / o['turn_accel'] if o['turn_accel'] else 0))
    print('  energy: max %d, red zone %d, refill %d standing / %d moving, boost %d a frame in the air'
          ' (%d on the ground), overboost %d to start + %d a frame'
          % (o['boost_max'], o['boost_redarea'], o['boost_add'], o['move_boost_add'], o['boost_sub'],
             int(F(F(o['boost_sub']) * 0.5)), o['oboost_1stsub'], o['oboost_sub']))


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    if a[0] == '--walk':
        t = walk_table(a[1])
        if len(a) > 2: json.dump(t, open(a[2], 'w'), indent=1)
        print('set core arms legs | forward loop: frames, km/h | strafe km/h | back km/h | crouch land hard-land')
        for s, r in t.items():
            d = r['dirs']
            print('%3d  %d    %d    %d   | %3d  %6.1f | %6.1f | %6.1f | %3d %3d %3d'
                  % (s, r['core_group'], r['arms_group'], r['legs_group'], d[0]['loop_frames'], kmh(d[0]['speed']),
                     kmh(d[3]['speed']), kmh(d[5]['speed']), r['crouch_frames'], r['land_frames'], r['hard_land_frames']))
        sys.exit(0)
    elfs = [x for x in a if os.path.basename(x).startswith('SLUS')]
    cs = [constants(p) for p in elfs]
    if '--parts' not in a:
        print('%-18s %s' % ('', '  '.join('%12s' % os.path.basename(p) for p in elfs)))
        for k in list(BLOCK) + INLINE:
            vals = [c[k] for c in cs]
            print('%-18s %s%s' % (k, '  '.join('%12.6g' % v for v in vals), '   <- differs' if len(set(vals)) > 1 else ''))
        sys.exit(0)
    parts = json.load(open(a[a.index('--parts') + 1]))
    if '--vectors' in a:
        write_vectors(cs[0], parts, a[a.index('--vectors') + 1])
    elif '--build' in a:
        w, mask = build_words(parts, a[a.index('--build') + 1])
        for p, c in zip(elfs, cs):
            print(os.path.basename(p)); show(compile_build(c, w, mask))
    else:
        for label, spec, pilot in VECTOR_BUILDS:
            w, mask = build_words(parts, spec)
            print(label); show(compile_build(cs[0], w, mask, pilot))
