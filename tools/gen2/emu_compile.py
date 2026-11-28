"""Check movement.py's compile against the game's own code, run in a MIPS emulator.

Usage: python3 emu_compile.py SLUS_200.14 P.json      (P = stats.py output; needs `pip install unicorn`)

Loads the AC2 executable, fills in real part records, and runs three stretches of the AC setup
routine as they are: 0x1452c8-0x145490 (optional parts -> multipliers), 0x145630-0x145780
(weight, speeds, energy, turning, overboost) and 0x145a48-0x145ad4 (jump, gravity, fall and
landing speeds). Then compares all 32 values of the movement block (AC +0x410..+0x490, weight
+0xd8 / +0xd4) with movement.compile_build() bit for bit, over every legs part with four mixes
of other parts and options each.

The emulated FPU is set to cut results off towards zero, as the PS2's does. The leg kind and
pilot kind (AC +0x70, +0x71) are filled in by hand: the game takes them from the motion set
number and the pilot record, which this does not run.
"""
import sys, os, struct, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import movement as M
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS64, UC_MODE_LITTLE_ENDIAN
from unicorn.mips_const import (UC_CPU_MIPS64_MIPS64R2_GENERIC, UC_MIPS_REG_GP, UC_MIPS_REG_SP,
                                UC_MIPS_REG_S1, UC_MIPS_REG_S2, UC_MIPS_REG_T0)

GP = 0x445470
AC, BLOB, BUILD, STACK, STUB = 0x1000000, 0x1010000, 0x1020000, 0x1f00000, 0x1030000
# Where the setup routine keeps each part's record (0x145dc0), and the pointer it reads it through.
SLOT_OFF = {'head': 0x80, 'core': 0x120, 'arms': 0x1d0, 'legs': 0x290, 'booster': 0x360, 'fcs': 0x3f0,
            'generator': 0x4c0, 'radiator': 0x550, 'inside': 0x5e0, 'extension': 0x670,
            'back_l': 0x700, 'back_r': 0x7a0, 'arm_r': 0x840, 'arm_l': 0x8d0}
SLOT_GP = {'head': 0x44084c, 'core': 0x440850, 'legs': 0x440854, 'arms': 0x440858, 'back_r': 0x44085c,
           'back_l': 0x440860, 'arm_l': 0x440864, 'arm_r': 0x440868, 'booster': 0x44086c, 'fcs': 0x440870,
           'generator': 0x440874, 'radiator': 0x440878, 'inside': 0x44087c, 'extension': 0x440880}
FIELDS_F = {'boost_factor': 0x424, 'turn_accel': 0x43c, 'turn_maxspd': 0x440, 'canoturn_accel': 0x444,
            'canoturn_maxspd': 0x448, 'jumpspd': 0x44c, 'dashjumpspd': 0x450, 'fall_maxspd': 0x454,
            'gravity': 0x458, 'land_dmgspd': 0x45c, 'dash_mintime': 0x460, 'dash_accel': 0x464,
            'dash_brake': 0x468, 'dash_maxspd': 0x46c, 'flyup_accel': 0x470, 'flyup_maxspd': 0x474,
            'flymv_accel': 0x478, 'flymv_maxspd': 0x47c, 'blade_spd0': 0x480, 'blade_brk': 0x484,
            'oboost_spd0': 0x488, 'oboost_acc': 0x48c, 'oboost_maxspd': 0x490}
FIELDS_I = {'weight': 0xd8, 'upper_weight': 0xd4, 'boost_max': 0x410, 'boost_redarea': 0x414,
            'boost_sub': 0x418, 'boost_add': 0x41c, 'move_boost_add': 0x420, 'oboost_1stsub': 0x428,
            'oboost_sub': 0x42c}


def run(image, words, mask, pilot):
    mu = Uc(UC_ARCH_MIPS, UC_MODE_MIPS64 | UC_MODE_LITTLE_ENDIAN)
    mu.ctl_set_cpu_model(UC_CPU_MIPS64_MIPS64R2_GENERIC)
    mu.mem_map(0, 0x2000000)
    mu.mem_write(0x100000, image)
    for slot, off in SLOT_OFF.items():
        if slot in words:
            w = words[slot]
            mu.mem_write(BLOB + off + 0x72, struct.pack('<%dH' % len(w), *w))
        mu.mem_write(SLOT_GP[slot], struct.pack('<I', BLOB + off if slot in words else 0))
    mu.mem_write(AC + 0x70, bytes([M.LEG_KIND[words['legs'][11] & 0xff], pilot]))
    mu.mem_write(BUILD + 0x1c, struct.pack('<I', mask))

    def call(begin, end, regs):
        mu.reg_write(UC_MIPS_REG_GP, GP)
        mu.reg_write(UC_MIPS_REG_SP, STACK)
        for r, v in regs.items(): mu.reg_write(r, v)
        mu.emu_start(begin, end)
    mu.mem_write(STUB, struct.pack('<2I', 0x44C8F800, 0))       # ctc1 t0, $31: round towards zero
    call(STUB, STUB + 4, {UC_MIPS_REG_T0: 1})
    call(0x1452c8, 0x145490, {UC_MIPS_REG_S2: BUILD, UC_MIPS_REG_S1: AC})
    mu.mem_write(0x145690, b'\0\0\0\0')   # an R5900-only multiply (anti-missile range, not movement)
    call(0x145630, 0x145780, {UC_MIPS_REG_S1: AC})
    call(0x145a48, 0x145ad4, {UC_MIPS_REG_S1: AC})
    out = {k: struct.unpack('<f', mu.mem_read(AC + o, 4))[0] for k, o in FIELDS_F.items()}
    out.update({k: struct.unpack('<i', mu.mem_read(AC + o, 4))[0] for k, o in FIELDS_I.items()})
    return out


if __name__ == '__main__':
    if len(sys.argv) < 3: sys.exit(__doc__)
    d = open(sys.argv[1], 'rb').read()
    off, va, size = struct.unpack_from('<III', d, 52 + 4)[0], 0x100000, struct.unpack_from('<I', d, 52 + 16)[0]
    image = d[off:off + size]
    parts = json.load(open(sys.argv[2]))
    c = M.constants(sys.argv[1])
    bad = n = 0
    for label, spec, pilot in M.VECTOR_BUILDS + M.sweep_builds(parts):
        w, mask = M.build_words(parts, spec)
        game = run(image, w, mask, pilot)
        ours = M.compile_build(c, w, mask, pilot)
        ours.update(blade_spd0=c['blade_spd0'], blade_brk=c['blade_brk'])
        n += 1
        for k, v in game.items():
            same = struct.pack('<f', ours[k]) == struct.pack('<f', v) if isinstance(v, float) else ours[k] == v
            if not same:
                bad += 1
                if bad <= 20: print('DIFF %s | %s: game %r, script %r' % (label, k, v, ours[k]))
    print('%d builds, %d values each: %d differences' % (n, len(game), bad))
    sys.exit(1 if bad else 0)
