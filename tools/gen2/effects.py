"""AC2 / Another Age weapon effects: impact (stagger), heat, recoil, reload, damage rates.

Usage:
  python3 effects.py SLUS_200.14 [SLUS_202.49]          tuning values found in the ELF (AA side by side)
  python3 effects.py SLUS_200.14 --weapons W.json --parts P.json
        + per-weapon stagger / heat on a standard AC (W = weapons.py output, P = stats.py output)

The game keeps its tuning values in a debug-menu table (label, variable, step, min, max); this
reads each value by its label, so it works on both ELFs. Formulas are from the code, see
docs/research/gen2-archive.md "Weapon effects" for the addresses.
"""
import sys, struct, json

LABELS = ['heat2temperature', 'temperature_maxadd', 'std_normalcool_temperature', 'std_quickcool_temperature',
          'std_geneheat_temperature', 'radiparts_std_cooling', 'cooling_radirate', 'cooling_radirate_min',
          'cooling_radirate_max', 'radiparts_std_quickcooling', 'quickcooling_rate_min', 'quickcooling_rate_max',
          'gene_std_heat', 'geneheat_rate', 'geneheat_rate_min', 'geneheat_rate_max', 'otherparts_std_cooling',
          'overheat_damage', 'p1_heatrate', 'vs_heatrate', 'cooling_heatrate', 'p1_damagerate', 'vs_damagerate',
          'dmgreact2frame', 'reactspdstep', 'react_maxxzspd', 'react_maxyspd', 'canoreactspdstep',
          'canoreact_maxxzspd', 'flareheat', 'flaretime', 'flare_damagerate', 'parts_std_totalweight']


def tuning(path):
    d = open(path, 'rb').read()
    va = lambda o: o - 0x1000 + 0x100000
    fo = lambda v: v - 0x100000 + 0x1000
    out = {}
    for lab in LABELS:
        for end in (b':', b';'):
            o = d.find(b'\0' + lab.encode() + end)
            if o >= 0: break
        o += 1
        key = struct.pack('<I', va(o))
        p = d.find(key)
        while p != -1:
            var = struct.unpack_from('<I', d, p + 4)[0]
            if 0x100000 < var < 0x500000:
                out[lab] = round(struct.unpack_from('<f', d, fo(var))[0], 4)
                break
            p = d.find(key, p + 1)
    return out


def clampf(x, k, lo, hi):            # 0x146220: 1 + (x - 1) * k, clamped
    return min(max(1 + (x - 1) * k, lo), hi)


def stability(legs_def_stab, head_balancer, total_weight, t):
    """Stability byte (AC +0xC42), 32..255; a standard AC is 64. 0x146DE8."""
    a = clampf(legs_def_stab / 1791.0, 1.0, 0.5, 4.0)
    b = clampf(head_balancer / 310.0, 0.3, 0.8, 1.0)
    c = clampf(total_weight / t['parts_std_totalweight'], 0.8, 0.5, 2.0)
    return min(max(int(64 * a * b * c), 32), 255)


def stagger_frames(impact, stab, t):
    """Hit-reaction length in frames (0 = no reaction at all). 0x1404F8."""
    return min(max(int(impact * t['dmgreact2frame'] * 64 / stab), 0), 60)


def recoil(recoil_raw, legs_def_stab, head_balancer, total_weight, t):
    """Back-cannon kick on the firing AC (weapon slot +0x28), 1..64. 0x146F10."""
    v = recoil_raw * (1791.0 / legs_def_stab) * (310.0 / head_balancer) * (t['parts_std_totalweight'] / total_weight)
    return min(max(int(v), 1), 64)


def temp_per_hit(heat, t, vs=False):
    """Temperature a hit adds (it arrives over a few frames, at most temperature_maxadd a frame)."""
    return heat * t['heat2temperature'] * (t['vs_heatrate'] if vs else t['p1_heatrate'])


def cooling(radiator_cooling, radiator_forced, gen_calorific, t):
    """(normal, while overheated) temperature lost per frame. 0x146260."""
    rad = clampf(radiator_cooling / 7909.0, 1.1, 0.75, 1.25)
    quick = clampf(radiator_forced / 7771.0, 1.1, 0.5, 1.5)
    gen = clampf(gen_calorific / 3617.0, 1.0, 0.5, 1.5)
    g = t['std_geneheat_temperature'] * gen
    return max(t['std_normalcool_temperature'] * rad - g, 0), max(t['std_quickcool_temperature'] * quick - g, 0)


def main(argv):
    t = tuning(argv[1])
    aa = tuning(argv[2]) if len(argv) > 2 and not argv[2].startswith('--') else None
    print('%-28s %10s %10s' % ('tuning value', 'AC2', 'AA' if aa else ''))
    for k in LABELS:
        print('%-28s %10s %10s' % (k, t.get(k), (aa or {}).get(k, '') if aa else ''))
    if '--weapons' in argv:
        w = json.load(open(argv[argv.index('--weapons') + 1]))
        std = stability(1791, 310, t['parts_std_totalweight'], t)
        print('\nstandard AC stability = %d' % std)
        print('%-16s %6s %6s %8s %8s %8s' % ('weapon', 'impact', 'frames', 'heat', 'temp', 'temp VS'))
        seen = set()
        for x in w:
            if x['name'] in seen: continue
            seen.add(x['name'])
            imp, heat = x['raw'][15] & 0xFF, x['raw'][14]
            print('%-16s %6d %6d %8d %8.1f %8.1f' % (x['name'][:16], imp, stagger_frames(imp, std, t), heat,
                                                     temp_per_hit(heat, t), temp_per_hit(heat, t, True)))
    if '--parts' in argv:
        p = json.load(open(argv[argv.index('--parts') + 1]))['parts']
        print('\nlegs: stability byte on a standard-weight AC (head balancer 310)')
        for x in p:
            if x['category'] == 'legs':
                print('  %-16s def.stab %5d -> %3d' % (x['name'], x['raw_words'][16],
                                                       stability(x['raw_words'][16], 310, t['parts_std_totalweight'], t)))
        print('cores: heat limit (core w12, probably)')
        for x in p:
            if x['category'] == 'core':
                print('  %-16s %d' % (x['name'], x['raw_words'][12]))
        print('radiators: cooling per frame, normal / overheated (generator at standard)')
        for x in p:
            if x['category'] == 'radiator':
                n, q = cooling(x['raw_words'][7], x['raw_words'][8], 3617, t)
                print('  %-16s %.3f / %.3f' % (x['name'], n, q))


if __name__ == '__main__':
    main(sys.argv)
