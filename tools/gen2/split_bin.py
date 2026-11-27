"""Gen 2: split AC2DATA.BIN / AC25DATA.BIN into per-ID files, fixing the 32 KB offset problem.

Usage (Windows: just double-click, or):
    python split_bin.py                 # uses the default rip folders below
    python split_bin.py PATH [PATH...]  # a .BIN, a .iso, or a folder holding either

For each archive it:
  1. reads the file table (uint32 LE sector pairs: offset, size; pair index = file ID),
  2. finds the real start of every file it can recognise (JOIN packages, part models)
     by testing sector shifts around the table offset, and picks the common rule,
  3. writes each file from its corrected start to the next file's corrected start
     into <rip folder>/split/<group>/NNNNN.bin (same A-G groups as the project zips),
  4. checks that every F-block shop package has its name record intact.
Nothing is written into the BIN or ISO; it is only read.
"""
import os, struct, sys
from collections import Counter

DEFAULT_DIRS = [
    r"C:\Users\Aileron\Documents\Games\Emulators\PS2\Armored Core 2 (USA)",
    r"C:\Users\Aileron\Documents\Games\Emulators\PS2\Armored Core 2 - Another Age (USA)",
]
NAMES = ("AC2DATA.BIN", "AC25DATA.BIN")
SECTOR = 2048
SHIFTS = [0, 16, 32, -16] + [s for s in range(-32, 65) if s not in (0, 16, 32, -16)]
GROUPS = [(1, 299, "A"), (300, 899, "B"), (900, 1699, "C"), (1700, 2499, "D"),
          (2500, 3399, "E_sound"), (3400, 3999, "F"), (4000, 99999, "G")]


class View:
    """Read-only window onto a file (a whole .BIN, or the BIN inside an .iso)."""
    def __init__(self, path, base=0, size=None):
        self.f = open(path, "rb"); self.base = base
        self.size = size if size is not None else os.path.getsize(path) - base
    def read(self, off, n):
        if off < 0 or off >= self.size: return b""
        n = min(n, self.size - off); self.f.seek(self.base + off); return self.f.read(n)


def find_in_iso(iso):
    """Locate AC2DATA.BIN / AC25DATA.BIN in an ISO9660 image's root directory."""
    with open(iso, "rb") as f:
        f.seek(16 * SECTOR); pvd = f.read(SECTOR)
        if pvd[1:6] != b"CD001": return None
        root = pvd[156:190]
        lba, ln = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
        f.seek(lba * SECTOR); d = f.read(ln); i = 0
        while i < len(d):
            rl = d[i]
            if rl == 0: i = (i // SECTOR + 1) * SECTOR; continue
            nl = d[i + 32]; name = d[i + 33:i + 33 + nl].decode("ascii", "replace").split(";")[0].upper()
            if name in NAMES:
                return name, struct.unpack_from("<I", d, i + 2)[0] * SECTOR, struct.unpack_from("<I", d, i + 10)[0]
            i += rl
    return None


def locate(path):
    """Return (label, View, out_dir) for a BIN, an ISO, or a folder holding one."""
    if os.path.isdir(path):
        for root, _, files in os.walk(path):
            for fn in files:
                if fn.upper() in NAMES: return locate(os.path.join(root, fn))
        for root, _, files in os.walk(path):
            for fn in files:
                if fn.lower().endswith(".iso"): return locate(os.path.join(root, fn))
        return None
    out = os.path.join(os.path.dirname(os.path.abspath(path)), "split")
    if path.lower().endswith(".iso"):
        hit = find_in_iso(path)
        if not hit: return None
        name, base, size = hit
        return name, View(path, base, size), out
    return os.path.basename(path).upper(), View(path), out


def read_table(v):
    t0off, t0size = struct.unpack("<2I", v.read(0, 8))
    tbytes = max(t0size * SECTOR, 0x8000)
    raw = v.read(0, min(tbytes, 0x100000))
    total = v.size // SECTOR; tend = t0off + t0size
    ents = {}; last = -1
    for i in range(1, len(raw) // 8):
        o, s = struct.unpack_from("<2I", raw, i * 8)
        if s == 0 or o < tend or o + s > total + 64 or o < last: continue
        ents[i] = (o, s); last = o
    return ents


def kind_at(v, byte_off):
    h = v.read(byte_off, 16)
    if len(h) < 16: return None
    if h[:4] == b"JOIN": return "JOIN"
    c, s, m, z = struct.unpack("<4I", h)
    if m == 0xFFFFFFFF and z == 0 and 0 < c < 64 and 0x40 < s < 0x80000: return "MODEL"
    return None


F_GROUPS = [(3400, 3411), (3420, 3425), (3430, 3452), (3460, 3490), (3500, 3506), (3520, 3529),
            (3540, 3547), (3560, 3567), (3580, 3586), (3600, 3607), (3620, 3651), (3660, 3691),
            (3700, 3724), (3730, 3738), (3750, 3765)]


def join_index(i):
    for lo, hi in F_GROUPS:
        if lo <= i <= hi: return i - lo
    return None


def matches(v, i, o, k):
    off = (o + k) * SECTOR
    if 3400 <= i:
        h = v.read(off, 0x14)
        if h[:4] != b"JOIN": return False
        want = join_index(i)
        # the package stores its index in the category: rejects a neighbour's package
        return want is None or struct.unpack_from("<I", h, 0x10)[0] == want
    if 300 <= i <= 1699: return kind_at(v, off) == "MODEL"
    return False


def detect(v, ents):
    """Shift (sectors) of each recognisable file. Pass 1 finds every candidate; the
    block's most common shift is then preferred, so a file never grabs its neighbour."""
    cand = {i: [k for k in SHIFTS if matches(v, i, o, k)] for i, (o, s) in ents.items() if i >= 300}
    cand = {i: c for i, c in cand.items() if c}
    block = {}
    for i, c in cand.items(): block.setdefault(group(i), Counter()).update(c)
    found = {}
    for i, c in cand.items():
        pref = block[group(i)].most_common(1)[0][0]
        found[i] = pref if pref in c else c[0]
    return found, {g: cnt.most_common(1)[0][0] for g, cnt in block.items()}


def group(i):
    for lo, hi, g in GROUPS:
        if lo <= i <= hi: return g
    return "other"


def name_ok(b):
    """Name/stats record = first printable entry in the 2nd offset table (+0x34)."""
    if b[:4] != b"JOIN" or len(b) < 0x54: return False
    for o in struct.unpack_from("<8I", b, 0x34):
        if o != 0xFFFFFFFF and o + 0x90 <= len(b) and all(32 <= c < 127 for c in b[o:o + 18]):
            return True
    return False


def split(path):
    hit = locate(path)
    if not hit: print(f"!! No AC2DATA.BIN / AC25DATA.BIN (or .iso holding one) found in {path}"); return False
    label, v, out = hit
    print(f"\n== {label}  ({v.size:,} bytes)  from {path}")
    ents = read_table(v); print(f"   table: {len(ents)} files, IDs {min(ents)}..{max(ents)}")
    found, block_shift = detect(v, ents)
    print(f"   shift per block: {block_shift}")
    if not found: print("!! Could not recognise any file headers; nothing written."); return False
    hist = Counter(found.values()); best, n = hist.most_common(1)[0]
    print(f"   recognised {len(found)} files; shift (sectors) -> count: {dict(hist.most_common(6))}")
    print(f"   rule: real start = table offset {'+' if best >= 0 else ''}{best} sectors "
          f"({best * SECTOR:#x} bytes); {n}/{len(found)} agree ({100 * n / len(found):.0f}%)")
    if n < len(found):
        print("   files that disagree use their own detected start (listed in split_log.txt)")
    starts = {i: (o + found.get(i, block_shift.get(group(i), best))) for i, (o, s) in ents.items()}
    order = sorted(ents, key=lambda i: (starts[i], i))
    os.makedirs(out, exist_ok=True)
    log = open(os.path.join(out, f"split_log_{label.split('.')[0]}.txt"), "w")
    log.write(f"{label}: global shift {best} sectors\nID\ttable_off\tshift\tstart\tbytes\n")
    total_sec = v.size // SECTOR; written = 0; fok = fbad = 0; bad = []
    for n_, i in enumerate(order):
        st = starts[i]
        end = starts[order[n_ + 1]] if n_ + 1 < len(order) else total_sec
        if end <= st: end = st + ents[i][1]
        b = v.read(st * SECTOR, (end - st) * SECTOR)
        d = os.path.join(out, group(i)); os.makedirs(d, exist_ok=True)
        open(os.path.join(d, f"{i:05d}.bin"), "wb").write(b); written += len(b)
        log.write(f"{i}\t{ents[i][0]}\t{starts[i] - ents[i][0]}{'' if i in found else '?'}\t{st}\t{len(b)}\n")
        if group(i) in ("F", "G") and b[:4] == b"JOIN":
            ji = join_index(i)
            if name_ok(b) and (ji is None or struct.unpack_from("<I", b, 0x10)[0] == ji): fok += 1
            else: fbad += 1; bad.append(i)
        if n_ % 200 == 0: print(f"   writing... {n_}/{len(order)}", end="\r")
    log.close()
    print(f"   wrote {len(order)} files, {written / 1e6:.0f} MB -> {out}          ")
    print(f"   CHECK shop packages with name record intact: {fok} ok, {fbad} bad"
          + (f"  (bad IDs: {bad[:20]})" if bad else ""))
    print(f"   expected: every shop package ok (AC2: 234 + the unknown 3780/3800/3801 may fail)")
    return True


if __name__ == "__main__":
    paths = sys.argv[1:] or [p for p in DEFAULT_DIRS if os.path.exists(p)]
    if not paths: print("Give the path to AC2DATA.BIN, AC25DATA.BIN, an .iso or its folder.")
    for p in paths:
        try: split(p)
        except Exception as e: print(f"!! {p}: {e}")
    if len(sys.argv) == 1: input("\nDone. Press Enter to close.")
