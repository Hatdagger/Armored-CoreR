import sys, os, collections
root = sys.argv[1]
mag = {b'TIM2': 'TIM2', b'SShd': 'SShd', b'SSbd': 'SSbd', b'IECS': 'IECS(sound)', b'VAGp': 'VAGp',
       b'\x10\x00\x00\x00': None}
res = collections.defaultdict(lambda: collections.Counter())
per = {}
for d, _, fs in os.walk(root):
    for f in fs:
        if not f[:5].isdigit(): continue
        b = open(os.path.join(d, f), 'rb').read(); i = int(f[:5])
        hits = []
        for m, n in mag.items():
            if n and m in b: hits.append(f"{n}x{b.count(m)}")
        per[i] = hits
g = lambda i: 'A' if i < 300 else 'B' if i < 900 else 'C' if i < 1700 else 'D' if i < 2500 else 'E' if i < 3400 else 'F'
for i in sorted(per):
    res[g(i)][' '.join(h.split('x')[0] for h in per[i]) or '-'] += 1
for k, v in res.items(): print(k, dict(v))
