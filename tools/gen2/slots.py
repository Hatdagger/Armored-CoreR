import os
DATA = os.environ.get("AC2_DATA", "data/ac2")  # folder of unzipped AC2_*.zip groups
import sys, struct, glob
def kind(c):
    if not any(c[:64]): return 'empty'
    u = struct.unpack('<4I', c[:16])
    if u[2] == 0xffffffff: return f'MODEL n={u[0]} sz={u[1]:#x}'
    if u[3] == 0 and u[1] and u[1] & (u[1]-1) == 0 and u[1] < u[0] <= u[1]*2: return f'TEX tot={u[0]:#x} pix={u[1]:#x} n={u[2]}'
    return 'raw ' + c[:16].hex()
for i in sys.argv[1:]:
    p = glob.glob(f'{DATA}/*/{int(i):05d}.bin')[0]; b = open(p, 'rb').read()
    print(i, [kind(b[o:o+0x8000]) for o in range(0, len(b), 0x8000)])
