"""Landing (36) and jump take-off (22) animation lengths per legs part, over
every animation set that uses those legs. Notes: docs/research/gen1-movement.md,
"Landing, from the disc".

<import folder> is acr-import's output (motion/models.toml, parts/legs/...);
<GG folder> holds the disc's .T archives unpacked as P<n>/PA<nn>/000.bin
(entry 0 of each PA archive: the animation set).

Usage: python3 moa_anim_lengths.py <import folder> <GG folder>
"""
import os
import re
import struct
import sys

install, gg = sys.argv[1], sys.argv[2]


def last_tick(n, anim):
    d = open(os.path.join(gg, 'P%d' % (n // 20), 'PA%02d' % n, '000.bin'), 'rb').read()
    lst = struct.unpack_from('<I', d, 16)[0]
    off = struct.unpack_from('<57I', d, lst)[anim]
    return struct.unpack_from('<h', d, lst + off)[0] if off else None


text = open(os.path.join(install, 'motion', 'models.toml')).read()
models = []
for block in text.split('[[model]]')[1:]:
    get = lambda k: re.search(r'^%s\s*=\s*(.+)$' % k, block, re.M).group(1)
    models.append((int(get('id')), int(get('core')),
                   [int(x) for x in re.findall(r'\d+', get('arms'))],
                   [int(x) for x in re.findall(r'\d+', get('legs'))]))

names = {}
for kind in sorted(os.listdir(os.path.join(install, 'parts', 'legs'))):
    for f in os.listdir(os.path.join(install, 'parts', 'legs', kind)):
        m = re.match(r'(\d+)-(.+)\.toml$', f)
        names[int(m.group(1))] = (kind, m.group(2))

for legs in sorted(names):
    rows = sorted({(last_tick(i, 36), last_tick(i, 22)) for i, c, a, l in models if legs in l})
    sets = [i for i, c, a, l in models if legs in l]
    kind, name = names[legs]
    print('%03d %-14s %-13s landing %-10s jump take-off %-8s sets %s' % (
        legs, name, kind, '/'.join(str(r[0]) for r in rows),
        '/'.join(sorted({str(r[1]) for r in rows})), sets))
