"""Read a Gen 2 motion bank (AC2 B 421-492) in game order: motion number = index.
Bank (as the game loads it, loader 0x126d28): u32 count (77 for AC sets), 12 bytes, then count x 16-byte
entries (u32 motion number, u32 clip offset from the bank start, 8 bytes 0). Row 0 is empty and row r
holds motion r - 1, so an AC set has motions 0-75. Clips follow the table (see motion.py for the clip format). The bank starts 0x8000
into its split file and runs on into the following files, so pass the B folder.
Usage: python3 motion_bank.py B_FOLDER SET_FILE_NUMBER
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from motion import keys

def stream(folder, first, count=4):
    return b''.join(open(os.path.join(folder, '%05d.bin' % i), 'rb').read()
                    for i in range(first, first + count) if os.path.exists(os.path.join(folder, '%05d.bin' % i)))

def bank(b, start=0x8000):
    n = struct.unpack_from('<I', b, start)[0]
    out = []
    for row in range(1, n):
        m, rel = struct.unpack_from('<II', b, start + 0x10 + row * 16)
        if not rel: continue
        o = start + rel
        nodes, fr = struct.unpack_from('<H', b, o + 8)[0], struct.unpack_from('<H', b, o + 0xc)[0]
        recs = [b[o + 0x10 + k * 0x30: o + 0x40 + k * 0x30] for k in range(nodes)]
        end = 0x10 + nodes * 0x30
        tracks = []
        for r in recs:
            off = struct.unpack_from('<I', r, 4)[0]
            k = keys(b, o, r)
            tracks.append(k)
            if off: end = max(end, off + len(k) * 0x80)
        out.append(dict(number=m, offset=o, nodes=nodes, frames=fr, parents=[r[2] for r in recs],
                        types=[r[0] for r in recs], tracks=tracks))
    return out

# Names from the AC state code in SLUS_200.14 (see docs/research/gen2-archive.md, Motion).
DIRS = 8   # direction index 0-7 from table 0x3c7d58 (current motion -> direction); 8 = neutral
NAMES = {0: 'rest', 38: 'land', 51: 'land_damaged', 37: 'jump'}
for d in range(DIRS):
    NAMES[1 + 2 * d] = 'walk_start_dir%d' % d
    NAMES[2 + 2 * d] = 'walk_loop_dir%d' % d
for d, m in enumerate([17, 19, 20, 21, 22, 18, 23, 24]):   # table 0x3c7d30 (dash start, by direction)
    NAMES[m] = 'boost_start_dir%d' % d
for d, m in enumerate([39, 40, 41, 42, 43, 44, 45, 46]):   # table 0x3c7e90 (used by the jump code)
    NAMES[m] = 'jump_dir%d' % d
for k in range(8):                                          # dash-brake chooser 0x13be60 (jump table 0x433b60)
    NAMES[25 + k] = 'boost_stop_%d' % k
NAMES.update({   # session 8: from the state tables at 0x3c8020 / 0x3c8368 / 0x3c86d8 (see gen2-archive.md)
    33: 'turn_a?', 34: 'turn_b?', 35: 'walk_stop?', 36: 'jump_takeoff', 47: 'idle_loop',
    48: 'destroyed', 49: 'ob_ground_start?', 66: 'ob_ground_loop?', 50: 'ob_air_start?', 67: 'ob_air_loop?',
    52: 'hit_ground_q0', 55: 'hit_ground_q1', 53: 'hit_ground_q2', 54: 'hit_ground_q3',
    56: 'kneel_a', 57: 'kneel_a_rise', 58: 'kneel_b', 59: 'kneel_b_rise', 75: 'kneel_c',
    63: 'blade_pose_a?', 64: 'blade_pose_b?', 65: 'blade_pose_c?'})

if __name__ == '__main__':
    folder, s = sys.argv[1], int(sys.argv[2])
    for c in bank(stream(folder, s)):
        moving = sum(1 for t in c['tracks'] if len(t) > 1 and len({k[2] for k in t}) > 1)
        print('%2d  %-18s nodes %2d  frames %4d  moving joints %2d' % (c['number'], NAMES.get(c['number'], ''), c['nodes'], c['frames'], moving))
