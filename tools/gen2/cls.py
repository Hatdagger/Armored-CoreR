import sys, os, struct, collections
root = sys.argv[1]
def cls(b):
    if len(b) < 32: return 'tiny'
    u = struct.unpack('<8I', b[:32])
    if b[:4] == b'IECS' or b'IECSsreV' in b[:64]: return 'sound'
    if u[2] == 0xffffffff and 0 < u[0] < 256 and u[1] <= len(b): return 'MODEL(cnt,size,-1)'
    # texture: total, pixbytes, n, 0, TEX0 lo/hi
    if u[3] == 0 and u[1] and u[0] > u[1] and u[0] <= len(b) and (u[5] >> 28) in (0, 8, 4, 2):
        if u[1] & (u[1]-1) == 0: return 'TEX(total,pix,n)'
    if all(32 <= x < 127 or x == 0 for x in b[:16]) and b[0] >= 65: return 'TEXT:' + b[:16].split(b'\0')[0].decode('ascii', 'replace')[:10]
    f = struct.unpack('<8f', b[:32])
    if sum(1 for x in f if x in (1.0, -1.0, 0.0) or 1e-3 < abs(x) < 1e4) >= 7 and any(x not in (0.0,) for x in f): return 'floats'
    if 0 < u[0] < 64 and all(u[1+k] <= len(b) for k in range(min(u[0],6))): return f'table(n={u[0]})'
    return 'other'
grp = lambda i: 'A' if i < 300 else 'B' if i < 900 else 'C' if i < 1700 else 'D' if i < 2500 else 'E' if i < 3400 else 'F'
res = collections.defaultdict(collections.Counter); ex = collections.defaultdict(list)
for d, _, fs in os.walk(root):
    for f in fs:
        if not f[:5].isdigit(): continue
        i = int(f[:5]); b = open(os.path.join(d, f), 'rb').read()
        c = cls(b); k = c.split(':')[0]; res[grp(i)][k] += 1; ex[(grp(i), k)].append(i if k != 'TEXT' else c)
for g in sorted(res):
    print(g, dict(res[g]))
    for k in res[g]:
        e = sorted(ex[(g, k)], key=str); print('   ', k, e[:12], '...' if len(e) > 12 else '')
