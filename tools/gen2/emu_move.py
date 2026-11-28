"""Check the C port's movement routines against the game's own code, run in a MIPS emulator.

Usage: cc -O2 tools/gen2/emu_move_harness.c -lm -o g2prim
       python3 emu_move.py SLUS_200.14 ./g2prim            (needs `pip install unicorn`)

4000 random cases through seven routines of SLUS_200.14 and the same ones in
src/gen2/acr_gen2_move.c, compared bit for bit: turn (0x140af8), brake (0x140c58), the speed
limit with its over-speed bleed (0x140d18), fall (0x140e58), rise (0x143858), the direction
number (0x13fa68) and the whole energy routine (0x139c50: every state, leg kind, charging and
the red zone). The emulated FPU cuts results off towards zero, as the PS2's does.

Not covered: the state handlers themselves (they need a live motion player) and the yaw
rotation (VU0 code).
"""
import struct, random, subprocess, sys
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS64, UC_MODE_LITTLE_ENDIAN
from unicorn.mips_const import *
if len(sys.argv) < 3: sys.exit(__doc__)
d = open(sys.argv[1],'rb').read()
GP=0x445470; U=0x1100000; STACK=0x1f00000; STUB=0x1030000; RET=0x1040000; VEC=0x1050000
mu = Uc(UC_ARCH_MIPS, UC_MODE_MIPS64 | UC_MODE_LITTLE_ENDIAN)
mu.ctl_set_cpu_model(UC_CPU_MIPS64_MIPS64R2_GENERIC)
mu.mem_map(0, 0x2000000)
mu.mem_write(0x100000, d[0x1000:0x1000+0x3412fc])
# R5900 sqrt.s takes its operand from the ft field; a standard MIPS core reads fs. Re-encode.
for a_ in range(0x140c58, 0x140e58, 4):
    w_ = struct.unpack('<I', mu.mem_read(a_, 4))[0]
    if (w_ & 0xffe0003f) == 0x46000004 and (w_ >> 16) & 31 and not (w_ >> 11) & 31:
        mu.mem_write(a_, struct.pack('<I', 0x46000004 | (((w_ >> 16) & 31) << 11) | (w_ & 0x7c0)))
mu.mem_write(STUB, struct.pack('<2I', 0x44C8F800, 0))
mu.reg_write(UC_MIPS_REG_T0, 1); mu.emu_start(STUB, STUB+4)
mu.mem_write(0x44072c, struct.pack('<I', U))
fb = lambda f: struct.unpack('<I', struct.pack('<f', f))[0]
bf = lambda u: struct.unpack('<f', struct.pack('<I', u))[0]
def wf(o, f): mu.mem_write(U+o, struct.pack('<f', f))
def rfb(o): return struct.unpack('<I', mu.mem_read(U+o, 4))[0]
def call(addr, a0=U, a1=0, f12=0.0, f13=0.0):
    mu.reg_write(UC_MIPS_REG_GP, GP); mu.reg_write(UC_MIPS_REG_SP, STACK); mu.reg_write(UC_MIPS_REG_RA, RET)
    mu.reg_write(UC_MIPS_REG_A0, a0); mu.reg_write(UC_MIPS_REG_A1, a1)
    mu.reg_write(UC_MIPS_REG_F12, fb(f12)); mu.reg_write(UC_MIPS_REG_F13, fb(f13))
    mu.emu_start(addr, RET)
R = random.Random(2)
f32 = lambda x: bf(fb(x))
cases = []; want = []
# masks
mu.mem_write(U+0x730, struct.pack('<10H', 1, 2, 4, 8, 0, 0x40, 0, 0, 0x10, 0x20))   # fwd back turnL(734) turnR(736) . boost(73a) . . left(740) right(742)
for i in range(4000):
    k = i % 7
    if k == 0:   # turn
        t = R.choice([-1, 0, 1]); ys = f32(R.uniform(-0.03, 0.03)); yaw = f32(R.uniform(-1, 1)); acc = f32(R.uniform(0.0005, 0.004)); mx = f32(R.uniform(0.01, 0.03))
        mu.mem_write(U+0xb4, struct.pack('<H', 8 if t > 0 else 4 if t < 0 else 0)); wf(0xa8, ys); wf(0x64, yaw)
        call(0x140af8, f12=acc, f13=mx)
        cases.append('turn %d %08x %08x %08x %08x' % (t, fb(ys), fb(yaw), fb(acc), fb(mx))); want.append('%08x %08x' % (rfb(0xa8), rfb(0x64)))
    elif k == 1: # brake
        vx = f32(R.uniform(-2, 2)); vz = f32(R.uniform(-2, 2)); dec = f32(R.choice([R.uniform(0.005, 0.2), R.uniform(1, 4)]))
        wf(0x90, vx); wf(0x98, vz); call(0x140c58, f12=dec)
        ret = mu.reg_read(UC_MIPS_REG_F0) & 0xffffffff
        cases.append('brake %08x %08x %08x' % (fb(vx), fb(vz), fb(dec))); want.append('%08x %08x %08x' % (rfb(0x90), rfb(0x98), ret))
    elif k == 2: # push (0x140d18)
        vx = f32(R.uniform(-4, 4)); vz = f32(R.uniform(-4, 4)); ax = f32(R.uniform(-0.1, 0.1)); az = f32(R.uniform(-0.1, 0.1)); mx = f32(R.uniform(0.3, 4)); fa = f32(R.uniform(0.02, 0.07))
        wf(0x90, vx); wf(0x98, vz); wf(0xba8, fa)
        mu.mem_write(VEC, struct.pack('<4f', ax, 0, az, 0))
        call(0x140d18, a1=VEC, f12=mx)
        cases.append('push %08x %08x %08x %08x %08x %08x' % (fb(vx), fb(vz), fb(ax), fb(az), fb(mx), fb(fa))); want.append('%08x %08x' % (rfb(0x90), rfb(0x98)))
    elif k == 3: # fall
        vy = f32(R.uniform(-2, 3)); g = f32(0.042); mx = f32(1.74)
        wf(0x94, vy); call(0x140e58, f12=g, f13=mx)
        cases.append('fall %08x %08x %08x' % (fb(vy), fb(g), fb(mx))); want.append('%08x' % rfb(0x94))
    elif k == 4: # rise (0x143858 on &vy with -acc, -max)
        vy = f32(R.uniform(-2, 2)); acc = f32(R.uniform(0.005, 0.03)); mx = f32(R.uniform(0.4, 1.2))
        wf(0x94, vy); call(0x143858, a0=U+0x94, f12=-acc, f13=-mx)
        cases.append('rise %08x %08x %08x' % (fb(vy), fb(acc), fb(mx))); want.append('%08x' % rfb(0x94))
    elif k == 5: # direction
        b = [R.randint(0, 1) for _ in range(4)]
        mu.mem_write(U+0xb4, struct.pack('<H', b[0]*1 + b[1]*2 + b[2]*0x10 + b[3]*0x20)); call(0x13fa68)
        cases.append('dir %d %d %d %d' % tuple(b)); want.append('%d' % (mu.reg_read(UC_MIPS_REG_V0) & 0xffffffff))
    else:        # energy
        st = R.choice([0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 16]); kind = R.randint(0, 3); ch = R.randint(0, 3) == 0; boost = R.randint(0, 1)
        mx = R.choice([28000, 38000, 53000]); red = R.choice([2500, 4000, 5800]); sub = R.randint(1, 300); add = R.randint(50, 384); madd = R.randint(1, 192); osub = R.randint(100, 230)
        e = R.choice([R.randint(0, mx), R.randint(0, 400), mx, mx - R.randint(0, 300), red + R.randint(-300, 300)])
        vx = f32(R.choice([0.0, 0.0005, R.uniform(-1, 1)])); vz = f32(R.choice([0.0, 0.0005, R.uniform(-1, 1)]))
        mu.mem_write(U+0xc, struct.pack('<H', st)); mu.mem_write(U+0x7a0, bytes([kind])); mu.mem_write(U+8, struct.pack('<i', e))
        mu.mem_write(U+0x10, struct.pack('<H', 4 if ch else 0)); mu.mem_write(U+0xe8, struct.pack('<hh', 0, 0)); mu.mem_write(U+0x19, b'\0')
        mu.mem_write(U+0xb4, struct.pack('<H', 0x40 if boost else 0)); wf(0x90, vx); wf(0x98, vz)
        mu.mem_write(U+0xb40, struct.pack('<5i', mx, red, sub, add, madd)); mu.mem_write(U+0xb5c, struct.pack('<i', osub)); mu.mem_write(U+0xb64, struct.pack('<i', 0))
        call(0x139c50)
        e2 = struct.unpack('<i', mu.mem_read(U+8, 4))[0]; fl = struct.unpack('<H', mu.mem_read(U+0x10, 2))[0]
        cases.append('energy %d %d %d %d %d %d %d %d %d %d %d %08x %08x' % (st, kind, ch, boost, e, mx, red, sub, add, madd, osub, fb(vx), fb(vz)))
        want.append('%d %d' % (e2, 1 if fl & 4 else 0))
out = subprocess.run([sys.argv[2]], input='\n'.join(cases) + '\n', capture_output=True, text=True).stdout.strip().split('\n')
bad = {}
for c, w, g in zip(cases, want, out):
    if w != g:
        op = c.split()[0]; bad[op] = bad.get(op, 0) + 1
        if bad[op] <= 4: print('DIFF', c, '| game', w, '| port', g)
tot = {}
for c in cases: tot[c.split()[0]] = tot.get(c.split()[0], 0) + 1
print('cases', tot, 'differences', bad or 0)
sys.exit(1 if bad or len(out) != len(cases) else 0)
