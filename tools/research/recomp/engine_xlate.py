#!/usr/bin/env python3
"""Find where an address used by one fight engine lives in another.

The six disc engines (FDAT 206 menu, 207 missions, 208 link, 209 VS, 210 arena, 211 replay) share
most of their fight code but put their data at different addresses. This pairs routines whose code
is identical once addresses are masked out (unique matches only), then, for every access to an
address in a paired routine, looks at the same instruction in the other engine. Each pair of
accesses is a vote for "address X here = address Y there".

  python3 tools/engine_xlate.py MODULES_DIR 209 210 0x801A2E0C 0x801D4BC0 ...

MODULES_DIR holds NNN.bin from your own disc (see extract_fdat.py). "exact" = votes on that very
address; "near" = borrowed from accesses within 0x40 of it (check those by reading the code).
"""
import bisect, collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ram_map as rm

MEM = (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x32, 0x3A)


def norm(w):
    op = w >> 26
    if op in (2, 3):
        return op << 26                       # j / jal target
    if op in (0x0F, 0x09, 0x0D) or op in MEM:
        return w & 0xFFFF0000                 # lui / addiu / ori immediates, load / store offsets
    return w


def routines(path):
    w, f = rm.module_words(path)
    end = max(w) + 4
    out = {}
    for i, a in enumerate(f):
        b = f[i + 1] if i + 1 < len(f) else end
        seq = tuple(norm(w[x]) for x in range(a, b, 4))
        if len(seq) >= 6:
            out[a] = seq
    return w, f, out


def votes(mod_dir, src, dst):
    ws, fs, s = routines(os.path.join(mod_dir, "%s.bin" % src))
    wd, fd, d = routines(os.path.join(mod_dir, "%s.bin" % dst))
    hs, hd = collections.defaultdict(list), collections.defaultdict(list)
    for a, q in s.items():
        hs[q].append(a)
    for a, q in d.items():
        hd[q].append(a)
    pairs = {hs[q][0]: hd[q][0] for q in hs if len(hs[q]) == 1 and len(hd.get(q, [])) == 1}
    bypc = collections.defaultdict(list)
    for r in rm.scan(wd, fd):
        bypc[r[2]].append(r[0])
    v = collections.defaultdict(collections.Counter)
    for a, fn, pc, _k, _s in rm.scan(ws, fs):
        if fn in pairs:
            for b in bypc.get(pairs[fn] + (pc - fn), []):
                v[a][b - a] += 1
    return v, len(pairs), len(fs), len(fd)


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    mod_dir, src, dst = sys.argv[1:4]
    v, n, ns, nd = votes(mod_dir, src, dst)
    print("paired routines: %d (of %d / %d)" % (n, ns, nd))
    for t in (int(x, 16) for x in sys.argv[4:]):
        cands = [t] if t in v else [a for a in v if abs(a - t) <= 0x40]
        if not cands:
            print("%08X: no paired accesses nearby" % t)
            continue
        c = collections.Counter()
        for a in cands:
            c.update(v[a])
        best = c.most_common(2)
        (delta, k) = best[0]
        more = "; next %+X ×%d" % best[1] if len(best) > 1 else ""
        print("%08X -> %08X  (%s, delta %+X, %d votes%s)" % (
            t, t + delta, "exact" if cands == [t] else "near", delta, k, more))


if __name__ == "__main__":
    main()
