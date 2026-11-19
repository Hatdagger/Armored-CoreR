r"""List every place Master of Arena's VS engine starts an AC animation, with the
animation number and the cross-fade step it passes (AC +0x7A; 4.12 a tick,
0 = cut). Notes: docs/research/gen1-models.md, "Blending and which pose shows".

Make the disassembly from your disc's FDAT.T entry 209 (the VS engine, loaded
at 0x80050DD8), with LLVM:

  llvm-objcopy -I binary -O elf32-tradlittlemips \
      --rename-section .data=.text,alloc,load,code,contents 209.bin vs209.elf
  llvm-objdump -d --triple=mipsel-unknown-linux --mcpu=mips1 --no-show-raw-insn \
      --adjust-vma=0x80050DD8 vs209.elf | tail -n +7 \
      | sed -E 's/ <_binary[^>]*>//; s/^([0-9a-f]+):\s+/\1  /' > vs209.s

Usage: python3 moa_anim_fades.py vs209.s
"""
import re
import sys
from collections import defaultdict

with open(sys.argv[1]) as f:
    lines = f.read().splitlines()
ins = {}
order = []
for l in lines:
    m = re.match(r'([0-9a-f]{8})  (\S+)\s*(.*)$', l)
    if m:
        a = int(m.group(1), 16)
        ins[a] = (m.group(2), m.group(3))
        order.append(a)

# who jumps or branches to an address (with the jumper's address)
incoming = defaultdict(list)
for a in order:
    op, args = ins[a]
    m = re.search(r'0x([0-9a-f]+)$', args)
    if op == 'j' and m:
        incoming[0x80000000 | int(m.group(1), 16)].append(a)
    elif op.startswith('b') and m:
        incoming[int(m.group(1), 16)].append(a)

STOP = ('jr', 'jal', 'jalr', 'j')


def defines(a, reg):
    """Value given to $reg by the instruction at a: ('const', v), ('reg', r),
    ('anim', None) for the current animation byte, ('?', text), or None."""
    op, args = ins.get(a, ('', ''))
    p = [x.strip() for x in args.split(',')]
    if not p or p[0] != '$%d' % reg:
        return None
    if op == 'addiu' and p[1] == '$zero':
        return ('const', int(p[2], 16) if p[2].startswith('0x') or p[2].startswith('-0x') else int(p[2]))
    if op == 'move':
        return ('const', 0) if p[1] == '$zero' else ('reg', int(p[1][1:]))
    if op == 'lbu' and p[1].startswith('0x6c('):
        return ('anim', None)
    if op in ('sb', 'sh', 'sw'):
        return None
    return ('?', '%s %s' % (op, args))


def resolve(a, reg, depth=0, seen=None):
    """Values $reg can hold just before the instruction at a."""
    seen = seen or set()
    out = set()
    if depth > 40 or (a, reg) in seen:
        return {('?', 'deep')}
    seen.add((a, reg))
    b = a - 4
    steps = 0
    while b in ins and steps < 24:
        # someone may jump to b+4 (the instruction we came from)
        for src in incoming.get(b + 4, []):
            d = defines(src + 4, reg)              # the jump's delay slot
            if d:
                out |= follow(d, src, depth, seen)
            else:
                out |= resolve(src, reg, depth + 1, seen)
        op, _ = ins[b]
        if op in STOP:
            # the instruction after a jr/j is its delay slot: fall-through ends here
            if op in ('jr', 'j'):
                return out
        if op in ('jr', 'j') or (op.startswith('b') and op not in ('bal',)):
            pass
        d = defines(b, reg)
        if d:
            # a definition in a branch delay slot still runs on the fall-through
            out |= follow(d, b, depth, seen)
            return out
        if op in ('jal', 'jalr') and reg in (2, 3, 4, 5, 6, 7):
            out.add(('?', 'call at %08x' % b))
            return out
        b -= 4
        steps += 1
    out.add(('?', 'unknown'))
    return out


def follow(d, at, depth, seen):
    if d[0] == 'reg':
        return resolve(at, d[1], depth + 1, seen)
    return {d}


# target -> (anim register or constant, fade register or constant)
WRAP = {
    0x80054A2C: (5, 7),
    0x8007A884: (5, ('const', 0)), 0x8007A8F0: (5, ('const', 0)),
    0x8007A95C: (5, ('const', 0x200)),
    0x8007B60C: (5, 7), 0x8007B8B0: (5, 7), 0x8007BC6C: (5, 7), 0x800859F4: (5, 7),
    0x8007FF44: (5, ('const', 0x200)), 0x8007FFF0: (5, ('const', 0x200)),
}
INSIDE = {0x8007A8C8, 0x8007A934, 0x8007A9A4, 0x8007B65C, 0x8007B900, 0x8007BD74, 0x80085A48,
          0x8007FFC8, 0x80080058}

rows = []
for a in order:
    op, args = ins[a]
    if op != 'jal' or a in INSIDE:
        continue
    t = 0x80000000 | int(args, 16)
    if t not in WRAP:
        continue
    ar, fr = WRAP[t]

    def vals(spec):
        if isinstance(spec, tuple):
            return {spec}
        d = defines(a + 4, spec)
        return follow(d, a, 0, set()) if d else resolve(a, spec)
    rows.append((a, t, vals(ar), vals(fr)))


def show(v):
    return ', '.join(sorted(('%d' % x[1] if x[0] == 'const' else 'current' if x[0] == 'anim' else '?')
                            for x in v))


for a, t, an, fa in rows:
    print('%08x -> %08x  anim %-14s fade %s' % (a, t, show(an), show(fa)))
