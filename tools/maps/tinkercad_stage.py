#!/usr/bin/env python3
"""Turn a Tinkercad export into an Armored-CoreR stage.

In Tinkercad: Export -> .OBJ. That downloads a zip (tinker.obj + obj.mtl).
Then:

    python tinkercad_stage.py my-map.zip <import dir> --id 50 --name "My Map"

and start the game with ACR_STAGE=50.

A big map can be built in tiles, one Tinkercad design each, named after
their place on a grid (letters west to east, numbers south to north):

    python tinkercad_stage.py A1.zip B1.zip "A2 B2 rot90.zip" <import dir>

Each tile is --tile mm square (default 200, Tinkercad's workplane), its
middle set on its grid square's middle. One export can fill several squares
("A2 B2"); rot90 / rot180 / rot270 turn it clockwise, seen from above. See docs/MAPS.md for the rules
(scale, colours for the two starts, what makes a good map).

Writes <import dir>/stages/<id>/:
    stage.toml     name, starts
    visual.obj/mtl what is drawn (Tinkercad's colours, each with a texture
                   made here: tex-RRGGBB.png; --plain for none)
    collision.obj  what ACs stand on, run into and shoot

No packages needed: plain Python 3.
"""
import argparse
import math
import os
import re
import sys
import zipfile

# Marker colours: any clear green = the players' side (team 1), any clear
# red = the enemies' side (team 2). Magenta / cyan, the first scheme, still
# work the same way. One or two markers a side: two are teammates.
P1_HEX = (0xFF, 0x00, 0xFF)   # magenta: team 1 (older maps)
P2_HEX = (0x00, 0xFF, 0xFF)   # cyan: team 2 (older maps)
MARKER_TOL = 24               # per channel, 0-255
TEAMMATE_SIDE = 500           # the game puts teammates 500 either side of the team's start
MAX_COLOURS = 15              # the engine's material list holds 16


def read_input(path):
    """Returns (obj text, mtl text or '')."""
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            names = z.namelist()
            objs = [n for n in names if n.lower().endswith(".obj")]
            mtls = [n for n in names if n.lower().endswith(".mtl")]
            if not objs:
                sys.exit("No .obj in the zip. In Tinkercad use Export -> .OBJ.")
            obj = z.read(objs[0]).decode("utf-8", "replace")
            mtl = z.read(mtls[0]).decode("utf-8", "replace") if mtls else ""
            return obj, mtl
    with open(path, encoding="utf-8", errors="replace") as f:
        obj = f.read()
    mtl = ""
    for line in obj.splitlines():
        if line.startswith("mtllib"):
            p = os.path.join(os.path.dirname(path), line.split(None, 1)[1].strip())
            if os.path.exists(p):
                with open(p, encoding="utf-8", errors="replace") as f:
                    mtl = f.read()
            break
    return obj, mtl


def parse_mtl(text):
    cols = {}
    cur = None
    for line in text.splitlines():
        s = line.split()
        if not s:
            continue
        if s[0] == "newmtl" and len(s) > 1:
            cur = s[1]
            cols[cur] = (200, 200, 200)
        elif s[0] == "Kd" and cur and len(s) >= 4:
            cols[cur] = tuple(max(0, min(255, int(round(float(v) * 255)))) for v in s[1:4])
    return cols


def parse_obj(text, cols):
    """Returns (points, triangles as (a, b, c, colour))."""
    pts, tris = [], []
    colour = (200, 200, 200)
    for line in text.splitlines():
        s = line.split()
        if not s:
            continue
        if s[0] == "v" and len(s) >= 4:
            pts.append(tuple(float(v) for v in s[1:4]))
        elif s[0] == "usemtl" and len(s) > 1:
            colour = cols.get(s[1], (200, 200, 200))
        elif s[0] == "f" and len(s) >= 4:
            idx = []
            for c in s[1:]:
                i = int(c.split("/")[0])
                idx.append(i - 1 if i > 0 else len(pts) + i)
            for k in range(1, len(idx) - 1):
                tris.append((idx[0], idx[k], idx[k + 1], colour))
    return pts, tris


def tri_area_normal(p, t):
    a, b, c = p[t[0]], p[t[1]], p[t[2]]
    u = [b[i] - a[i] for i in range(3)]
    w = [c[i] - a[i] for i in range(3)]
    n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
    return n


def guess_up(pts, tris):
    """The up axis: the one most face area lies flat against (floors, the
    floor plate's underside, roofs). Tinkercad's workplane is z up."""
    area = [0.0, 0.0, 0.0]
    for t in tris:
        n = tri_area_normal(pts, t)
        for i in range(3):
            area[i] += abs(n[i])
    return "y" if area[1] > area[2] else "z"


def to_engine(p, up, scale):
    """Tinkercad mm -> engine world units, y up, z = north (Tinkercad's
    back), x = east. The engine's world is left-handed, so a map seen from
    above looks the same as in Tinkercad's top view."""
    x, y, z = p
    if up == "z":
        return (x * scale, z * scale, y * scale)
    return (x * scale, y * scale, -z * scale)


GRID_RE = re.compile(r"(?<![A-Za-z])([A-Z])([1-9][0-9]?)(?![0-9])")
ROT_RE = re.compile(r"rot(90|180|270)", re.I)


def tile_places(path):
    """Grid squares (column, row from 0) and quarter turns from a file name."""
    name = os.path.splitext(os.path.basename(path))[0]
    cells = [(ord(m.group(1)) - ord("A"), int(m.group(2)) - 1) for m in GRID_RE.finditer(name)]
    rot = ROT_RE.search(name)
    return cells, (int(rot.group(1)) // 90 if rot else 0)


def is_colour(c, hexc):
    return all(abs(c[i] - hexc[i]) <= MARKER_TOL for i in range(3))


def marker_team(c):
    """1 for a players' marker (green), 2 for an enemies' (red), else 0."""
    r, g, b = c
    # Clear greens / reds only (Tinkercad's 46B749 / E91D2D and the like):
    # orange, yellow, lime, pink and browns stay scenery.
    if is_colour(c, P1_HEX) or (g >= 140 and r <= 120 and b <= 120):
        return 1
    if is_colour(c, P2_HEX) or (r >= 170 and g <= 90 and b <= 90):
        return 2
    return 0


def parts_of(tris):
    """Separate shapes in a set of faces (Tinkercad puts same-coloured shapes in one object)."""
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for t in tris:
        for i in t[1:3]:
            parent[find(i)] = find(t[0])
    groups = {}
    for i in {i for t in tris for i in t[:3]}:
        groups.setdefault(find(i), []).append(i)
    return list(groups.values())


def merge_colours(colours):
    """At most MAX_COLOURS: rare colours join the nearest common one."""
    count = {}
    for c in colours:
        count[c] = count.get(c, 0) + 1
    keep = sorted(count, key=lambda c: -count[c])[:MAX_COLOURS]
    remap = {}
    for c in count:
        remap[c] = c if c in keep else min(keep, key=lambda k: sum((k[i] - c[i]) ** 2 for i in range(3)))
    return remap, len(count) - len(keep)


def top_y(pts, tris, x, z, below=None):
    """Highest upward face at (x, z) (engine axes, y up), optionally below a height."""
    best = None
    for a, b, c, _ in tris:
        A, B, C = pts[a], pts[b], pts[c]
        d = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2])
        if abs(d) < 1e-9:
            continue
        l1 = ((B[2] - C[2]) * (x - C[0]) + (C[0] - B[0]) * (z - C[2])) / d
        l2 = ((C[2] - A[2]) * (x - C[0]) + (A[0] - C[0]) * (z - C[2])) / d
        l3 = 1 - l1 - l2
        if min(l1, l2, l3) < -1e-6:
            continue
        y = l1 * A[1] + l2 * B[1] + l3 * C[1]
        if below is not None and y > below + 1:
            continue
        if best is None or y > best:
            best = y
    return best


def facing_to(src, dst):
    """Engine facing, 4096 a turn, 0 = +z, 1024 = +x."""
    a = math.atan2(dst[0] - src[0], dst[2] - src[2])
    return int(round(a / (2 * math.pi) * 4096)) & 4095


# ---------------------------------------------------------------------------
# Textures. Tinkercad has none, so each colour gets one made here: the
# colour's kind picks the pattern (SURFACES below) and the colour itself
# tints it. Small, few shades, hard pixels, like the PS1 games' own. Nothing
# is taken from a disc.
#
# A texture is 64 x 128: the upper half is for walls (sides), the lower half
# for floors and roofs (tops). The game's stage textures don't repeat, so
# every face is cut into squares of about --texture-size mm, each showing
# the pattern once; the squares are fitted to each shape, so a pattern
# always ends at the shape's edge.
TEX = 64
MAX_DRAWN = 12000                                          # drawn faces before the squares are widened
SHADES = (0.34, 0.48, 0.62, 0.76, 0.88, 1.0, 1.1, 1.24)   # 5 = the colour itself
LIT, BLACK = 8, 9                                          # a lit window; hazard black


class Rand:
    def __init__(self, seed):
        self.s = (seed * 2654435761 + 12345) & 0xFFFFFFFF

    def next(self, n):
        self.s = (self.s * 1103515245 + 12345) & 0xFFFFFFFF
        return (self.s >> 8) % n


def surface_kind(c):
    """The pattern for a Tinkercad colour (docs/MAPS.md, "Textures")."""
    r, g, b = (v / 255 for v in c)
    hi, lo = max(r, g, b), min(r, g, b)
    sat = 0 if hi == 0 else (hi - lo) / hi
    if sat < 0.2:
        return "panel" if hi >= 0.5 else "plate"
    if hi == r:
        hue = 60 * (((g - b) / (hi - lo)) % 6)
    elif hi == g:
        hue = 60 * ((b - r) / (hi - lo) + 2)
    else:
        hue = 60 * ((r - g) / (hi - lo) + 4)
    if 42 <= hue < 70:
        return "hazard"
    if 170 <= hue < 265:
        return "facade"
    if hue < 42 or hue >= 330:
        return "crate"
    return "panel"


def _blank(shade):
    return [[shade] * TEX for _ in range(TEX)]


def _mottle(px, rnd, dark=9, light=5):
    for y in range(TEX):
        for x in range(TEX):
            k = rnd.next(100)
            if k < dark and px[y][x] > 0:
                px[y][x] -= 1
            elif k < dark + light and px[y][x] < 7:
                px[y][x] += 1


def _seams(px, lines_x=(0,), lines_y=(0,)):
    """A dark line with a light one beside it: the edge of a slab."""
    for x0 in lines_x:
        for y in range(TEX):
            px[y][x0] = 2
            px[y][(x0 + 1) % TEX] = min(7, px[y][(x0 + 1) % TEX] + 1)
    for y0 in lines_y:
        for x in range(TEX):
            px[y0][x] = 2
            px[(y0 + 1) % TEX][x] = min(7, px[(y0 + 1) % TEX][x] + 1)


def _bolt(px, x, y):
    px[y][x] = px[y][x + 1] = px[y + 1][x] = 2
    px[y + 1][x + 1] = 1
    px[y - 1][x] = px[y - 1][x + 1] = 6


def tex_panel(rnd):
    side = _blank(5)
    for x in range(TEX):                       # streaks running down from a seam
        if rnd.next(100) < 22:
            top, length = (0, 34)[rnd.next(2)], 6 + rnd.next(22)
            for y in range(top + 2, min(TEX, top + 2 + length)):
                side[y][x] -= 1
    _mottle(side, rnd)
    for y in range(57, TEX):                   # grime at the foot
        for x in range(TEX):
            side[y][x] = max(0, side[y][x] - (1 if y < 61 else 2))
    _seams(side, (0,), (0, 32))
    for bx in (5, 57):
        for by in (6, 26, 39, 52):
            _bolt(side, bx, by)
    top = _blank(5)
    _mottle(top, rnd, 12, 6)
    for _ in range(5):                         # scuffs
        x, y, n = 4 + rnd.next(50), 4 + rnd.next(50), 3 + rnd.next(6)
        for k in range(n):
            if x + k < TEX and y + k // 2 < TEX:
                top[y + k // 2][x + k] = 3
    _seams(top, (0, 32), (0, 32))
    return side, top


def tex_plate(rnd):
    def plate(step):
        px = _blank(5)
        _mottle(px, rnd, 8, 3)
        for y in range(TEX):
            for x in range(TEX):
                a, b = x % 8, y % 8
                if (a == 2 and 1 <= b <= 3) or (a == 6 and 5 <= b <= 7):
                    px[y][x] = 7
                elif (a == 3 and 1 <= b <= 3) or (a == 7 and 5 <= b <= 7):
                    px[y][x] = 3
        _seams(px, (0,) if step else (0, 32), (0,) if step else (0, 32))
        for k in range(4, TEX, 16):
            _bolt(px, k, 4)
            _bolt(px, 3, k + 6)
        return px
    return plate(1), plate(0)


def tex_hazard(rnd):
    def stripes(width):
        px = _blank(5)
        _mottle(px, rnd, 10, 0)
        for y in range(TEX):
            for x in range(TEX):
                if ((x + y) // width) % 2:
                    px[y][x] = BLACK if rnd.next(100) >= 6 else 1
        _seams(px)
        return px
    return stripes(8), stripes(16)


def tex_facade(rnd):
    side = _blank(4)
    _mottle(side, rnd, 8, 4)
    for wy in range(2):                        # two storeys a square, four windows each
        for wx in range(4):
            x0, y0 = wx * 16 + 3, wy * 32 + 9
            lit = rnd.next(100) < 30
            for y in range(y0, y0 + 16):
                for x in range(x0, x0 + 10):
                    side[y][x] = 1 if not lit else LIT
                    if lit and (y == y0 + 8 or x == x0 + 5):
                        side[y][x] = 4         # the window's bars against the light
            for x in range(x0 - 1, x0 + 11):
                side[y0 - 1][x] = 2
                side[y0 + 16][x] = 6           # the sill catches the light
            for y in range(y0, y0 + 16):
                side[y][x0 - 1] = 2
        for x in range(TEX):                   # the floor line between storeys
            side[wy * 32][x] = 2
            side[wy * 32 + 1][x] = 6
            side[wy * 32 + 2][x] = 5
    for y in range(TEX):
        side[y][0] = 2
    top = _blank(3)
    _mottle(top, rnd, 10, 4)
    for k in range(TEX):                       # the parapet round the roof
        for d in range(3):
            top[d][k] = top[TEX - 1 - d][k] = top[k][d] = top[k][TEX - 1 - d] = 5
    for k in range(3, TEX - 3):
        top[3][k] = top[k][3] = 1
    for x0, y0 in ((12, 12), (38, 36)):        # vents
        for y in range(y0, y0 + 12):
            for x in range(x0, x0 + 14):
                top[y][x] = 4 if (y - y0) % 3 else 1
        for x in range(x0, x0 + 14):
            top[y0][x] = 7
            top[y0 + 12][x] = 1
    return side, top


def tex_crate(rnd):
    def ribs(across):
        px = _blank(5)
        for y in range(TEX):
            for x in range(TEX):
                k = (y if across else x) % 8
                px[y][x] = (6, 5, 5, 4, 3, 4, 4, 5)[k]
        for _ in range(7):                     # rust and dents
            cx, cy, r = rnd.next(TEX), rnd.next(TEX), 2 + rnd.next(4)
            for y in range(max(0, cy - r), min(TEX, cy + r)):
                for x in range(max(0, cx - r), min(TEX, cx + r)):
                    if (x - cx) ** 2 + (y - cy) ** 2 <= r * r and rnd.next(100) < 70:
                        px[y][x] = max(0, px[y][x] - 1)
        for d in range(4):                     # the frame
            for k in range(TEX):
                px[d][k] = px[TEX - 1 - d][k] = 3 if d < 3 else 6
        for y in range(TEX):
            px[y][0] = 2
            px[y][1] = 6
        return px
    return ribs(0), ribs(1)


SURFACES = {"panel": tex_panel, "plate": tex_plate, "hazard": tex_hazard, "facade": tex_facade, "crate": tex_crate}


def make_texture(c):
    """(kind, rows of RGB) for a colour: 64 wide, walls above floors."""
    kind = surface_kind(c)
    side, top = SURFACES[kind](Rand(c[0] << 16 | c[1] << 8 | c[2]))
    tone = 215 / max(215, max(c))              # room above the colour for the light shades
    base = [v * tone for v in c]
    pal = [tuple(min(255, int(v * s + 0.5)) for v in base) for s in SHADES]
    pal += [(250, 232, 150), (24, 24, 28)]     # LIT, BLACK
    return kind, [[pal[s] for s in row] for row in side + top]


def write_png(path, rows):
    """A plain 8-bit RGB PNG (no packages needed)."""
    import struct
    import zlib
    w, h = len(rows[0]), len(rows)
    raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def faces_down(pts, t):
    """A face whose front looks straight down. (y is up here, but the world
    is left-handed: a front that looks down has a cross product that points up.)"""
    A, B, C = (pts[i] for i in t[:3])
    u = [B[a] - A[a] for a in range(3)]
    w = [C[a] - A[a] for a in range(3)]
    n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
    return n[1] > 0 and n[1] > 0.99 * math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)


def _clip(poly, idx, bound, keep_above):
    """The part of a polygon on one side of a grid line (vertices: s, t, x, y, z)."""
    out = []
    for i, a in enumerate(poly):
        b = poly[(i + 1) % len(poly)]
        ina = a[idx] >= bound - 1e-7 if keep_above else a[idx] <= bound + 1e-7
        inb = b[idx] >= bound - 1e-7 if keep_above else b[idx] <= bound + 1e-7
        if ina:
            out.append(a)
        if ina != inb:
            f = (bound - a[idx]) / (b[idx] - a[idx])
            out.append(tuple(a[j] + (b[j] - a[j]) * f for j in range(5)))
    return out


def textured_faces(pts, world, remap, cell):
    """Cuts every face into squares of one pattern each.

    Returns [(colour, ((x, y, z), (u, v)) x 3)]. Shapes are told apart by
    colour and touching corners; each gets its own grid from its lowest
    corner, with the square stretched a little so a whole number fits."""
    key = {}

    def kof(i):
        p = pts[i]
        return key.setdefault((round(p[0], 1), round(p[1], 1), round(p[2], 1)), len(key))
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for t in world:                            # same colour and a shared corner: one shape
        c = remap[t[3]]
        ks = [(c, kof(i)) for i in t[:3]]
        for k in ks[1:]:
            parent[find(k)] = find(ks[0])
    box = {}
    for t in world:
        root = find((remap[t[3]], kof(t[0])))
        b = box.setdefault(root, [[1e18] * 3, [-1e18] * 3])
        for i in t[:3]:
            for a in range(3):
                b[0][a] = min(b[0][a], pts[i][a])
                b[1][a] = max(b[1][a], pts[i][a])
    half = 0.5 / (2 * TEX)                     # half a texel of the 128 rows: keeps the two halves apart
    out = []
    for t in world:
        c = remap[t[3]]
        lo, hi = box[find((c, kof(t[0])))]
        size = [hi[a] - lo[a] for a in range(3)]
        fit = [size[a] / max(1, round(size[a] / cell)) if size[a] > 1e-6 else cell for a in range(3)]
        A, B, C = (pts[i] for i in t[:3])
        u = [B[a] - A[a] for a in range(3)]
        w = [C[a] - A[a] for a in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        an = [abs(v) for v in n]
        if max(an) < 1e-9:
            continue
        if an[1] >= an[0] and an[1] >= an[2]:
            sa, ta, top = 0, 2, True           # a floor or a roof: across x and z
        elif an[0] >= an[2]:
            sa, ta, top = 2, 1, False          # a wall facing east or west: along z, up y
        else:
            sa, ta, top = 0, 1, False          # a wall facing north or south: along x, up y
        tri = [((p[sa] - lo[sa]) / fit[sa], (p[ta] - lo[ta]) / fit[ta], p[0], p[1], p[2]) for p in (A, B, C)]
        # Walls: every other shape, and every other side of it, shows the picture
        # mirrored, so a row of the same buildings doesn't light the same windows.
        na = 0 if sa == 2 else 2               # the axis the wall faces along
        mirror = not top and (int(round(lo[0] / 50)) * 7 + int(round(lo[2] / 50)) * 13 + sa + (n[na] > 0)) % 2 == 1
        s0, s1 = math.floor(min(v[0] for v in tri) + 1e-6), math.ceil(max(v[0] for v in tri) - 1e-6)
        for i in range(s0, max(s1, s0 + 1)):
            strip = _clip(_clip(tri, 0, i, True), 0, i + 1, False)
            if len(strip) < 3:
                continue
            t0, t1 = math.floor(min(v[1] for v in strip) + 1e-6), math.ceil(max(v[1] for v in strip) - 1e-6)
            for j in range(t0, max(t1, t0 + 1)):
                poly = _clip(_clip(strip, 1, j, True), 1, j + 1, False)
                if len(poly) < 3:
                    continue
                vs = []
                for s, tt, x, y, z in poly:
                    lu, lv = min(1.0, max(0.0, s - i)), min(1.0, max(0.0, tt - j))
                    if mirror:
                        lu = 1.0 - lu
                    row = (0.5 if top else 0.0) + half + (1.0 - lv) * (0.5 - 2 * half)   # down the image
                    vs.append(((x, y, z), (lu, 1.0 - row)))
                for k in range(1, len(vs) - 1):
                    a, b, d = vs[0], vs[k], vs[k + 1]
                    e1 = [b[0][m] - a[0][m] for m in range(3)]
                    e2 = [d[0][m] - a[0][m] for m in range(3)]
                    cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
                    if cr[0] ** 2 + cr[1] ** 2 + cr[2] ** 2 > 1e-4:
                        out.append((c, (a, b, d)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="Tinkercad .zip export(s) (Export -> .OBJ) or .obj files, "
                    "then the folder acr-import wrote (the one holding stages/)")
    ap.add_argument("--import-dir", help="the import folder, if not given last")
    ap.add_argument("--id", type=int, default=50, help="stage number (default 50; 0-40 are the disc's)")
    ap.add_argument("--name", default="Custom Map")
    ap.add_argument("--scale", type=float, default=50.0, help="world units per mm (default 50)")
    ap.add_argument("--tile", type=float, default=200.0, help="tile size in mm for grid-named files (default 200)")
    ap.add_argument("--up", choices=["auto", "y", "z"], default="auto", help="the files' up axis")
    ap.add_argument("--texture-size", type=float, default=10.0,
                    help="mm one square of a texture covers (default 10: two AC widths and a half)")
    ap.add_argument("--plain", action="store_true", help="no textures: Tinkercad's plain colours, as before")
    args = ap.parse_args()
    files = list(args.inputs)
    import_dir = args.import_dir
    if not import_dir:
        if len(files) < 2 or not os.path.isdir(files[-1]) and os.path.splitext(files[-1])[1]:
            sys.exit("Give the import folder last (or --import-dir).")
        import_dir = files.pop()
    if 0 <= args.id <= 40:
        print(f"warning: stage {args.id} is one of the disc's; it will be overwritten until the next import")

    # Every file, in engine units, each set on its grid square(s).
    tile = args.tile * args.scale
    pts, tris = [], []
    tiled = len(files) > 1 or tile_places(files[0])[0]
    for path in files:
        obj, mtl = read_input(path)
        raw, ftris = parse_obj(obj, parse_mtl(mtl))
        if not ftris:
            print(f"warning: {os.path.basename(path)} has no faces; skipped")
            continue
        up = guess_up(raw, ftris) if args.up == "auto" else args.up
        fp = [to_engine(p, up, args.scale) for p in raw]
        cells, turns = tile_places(path)
        if tiled and not cells:
            sys.exit(f"{os.path.basename(path)}: name it after its grid square (A1, B1, A2...) to join it to the others.")
        if not cells:
            cells = [(0, 0)]
        # The tile's own middle (its footprint, markers left out), turned.
        world_i = {i for t in ftris if not marker_team(t[3]) for i in t[:3]}
        xs = [fp[i][0] for i in world_i] or [0.0]
        zs = [fp[i][2] for i in world_i] or [0.0]
        mx, mz = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2
        w, d = max(xs) - min(xs), max(zs) - min(zs)
        if tiled and (abs(w - tile) > 2 * args.scale or abs(d - tile) > 2 * args.scale):
            print(f"warning: {os.path.basename(path)} is {w / args.scale:.0f} x {d / args.scale:.0f} mm, "
                  f"not {args.tile:.0f} x {args.tile:.0f}: it may not meet its neighbours")
        for col, row in cells:
            base = len(pts)
            for p in fp:
                x, z = p[0] - mx, p[2] - mz
                for _ in range(turns):        # clockwise from above: north -> east
                    x, z = z, -x
                pts.append((x + col * tile, p[1], z + row * tile))
            tris.extend((a + base, b + base, c + base, col_) for a, b, c, col_ in ftris)
        print(f"{os.path.basename(path)}: {len(ftris)} faces, up {up}"
              + (f", at {' '.join(chr(65 + c) + str(r + 1) for c, r in cells)}" if tiled else "")
              + (f", turned {turns * 90}" if turns else ""))
    if not tris:
        sys.exit("The files have no faces.")
    args.import_dir = import_dir

    # Pull out the markers: green = players (team 1), red = enemies (team 2).
    marks = {1: [], 2: []}
    world = []
    for t in tris:
        k = marker_team(t[3])
        if k:
            marks[k].append(t)
        else:
            world.append(t)
    if not world:
        sys.exit("Only markers in the file: nothing to stand on.")

    # Centre the map on x / z (heights stay: Tinkercad's workplane is 0).
    xs = [pts[i][0] for t in world for i in t[:3]]
    zs = [pts[i][2] for t in world for i in t[:3]]
    cx, cz = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2
    pts = [(p[0] - cx, p[1], p[2] - cz) for p in pts]
    width, depth = (max(xs) - min(xs)), (max(zs) - min(zs))

    side_name = {1: "green (players)", 2: "red (enemies)"}
    spots = {1: [], 2: []}          # each marker: x, y (feet), z
    for k in (1, 2):
        for part in parts_of(marks[k]):
            mp = [pts[i] for i in part]
            x = (min(p[0] for p in mp) + max(p[0] for p in mp)) / 2
            z = (min(p[2] for p in mp) + max(p[2] for p in mp)) / 2
            y = min(p[1] for p in mp)
            floor = top_y(pts, world, x, z, below=y)
            if floor is None:
                print(f"warning: nothing under a {side_name[k]} marker at x {int(x)}, z {int(z)}: that AC will fall")
            elif y - floor > 600:
                print(f"warning: a {side_name[k]} marker is {int(y - floor)} units above the floor (it will drop)")
            else:
                y = floor
            spots[k].append([x, y, z])
        spots[k].sort(key=lambda p: (p[0], p[2]))
        if len(spots[k]) > 2:
            print(f"note: {len(spots[k])} {side_name[k]} markers; the game takes up to 2 a side for now")
    starts = {}
    for k in (1, 2):
        if spots[k]:
            n = len(spots[k])
            starts[k] = [sum(p[i] for p in spots[k]) / n for i in range(3)]
            starts[k][1] = max(p[1] for p in spots[k])
        else:
            z = (-0.35 if k == 1 else 0.35) * depth
            y = top_y(pts, world, 0.0, z)
            if y is None:
                sys.exit(f"No {side_name[k]} marker and no floor at the map's middle line to drop it on.")
            starts[k] = [0.0, y, z]
            print(f"note: no {side_name[k]} marker; that side starts at x 0, z {int(z)}")
    face = {1: facing_to(starts[1], starts[2]), 2: facing_to(starts[2], starts[1])}
    for k in (1, 2):
        if len(spots[k]) == 2:
            a_, b_ = spots[k]
            half = math.hypot(a_[0] - b_[0], a_[2] - b_[2]) / 2
            if abs(half - TEAMMATE_SIDE) > 100:
                print(f"note: the {side_name[k]} pair are {int(half)} units either side of their middle; "
                      f"the game places teammates {TEAMMATE_SIDE} either side for now")

    out = os.path.join(args.import_dir, "stages", f"{args.id:02d}")
    os.makedirs(out, exist_ok=True)

    remap, merged = merge_colours([t[3] for t in world])
    if merged:
        print(f"note: {merged} rarely used colour(s) merged into the nearest of {MAX_COLOURS}")
    palette = sorted(set(remap.values()))
    name_of = {c: f"c{c[0]:02x}{c[1]:02x}{c[2]:02x}" for c in palette}

    kinds = {}
    with open(os.path.join(out, "visual.mtl"), "w") as f:
        for c in palette:
            f.write(f"newmtl {name_of[c]}\nKd {c[0] / 255:.4f} {c[1] / 255:.4f} {c[2] / 255:.4f}\n")
            if not args.plain:
                kinds[c], rows = make_texture(c)
                write_png(os.path.join(out, f"tex-{name_of[c][1:]}.png"), rows)
                f.write(f"map_Kd tex-{name_of[c][1:]}.png\n")
            f.write("\n")
    used = sorted({i for t in world for i in t[:3]})
    new = {old: n + 1 for n, old in enumerate(used)}
    visual_faces = len(world)
    with open(os.path.join(out, "visual.obj"), "w") as f:
        f.write(f"# {args.name}: made from a Tinkercad export by tools/maps/tinkercad_stage.py\nmtllib visual.mtl\n")
        if args.plain:
            for i in used:
                p = pts[i]
                f.write(f"v {p[0]:.1f} {p[1]:.1f} {p[2]:.1f}\n")
            last = None
            for t in sorted(world, key=lambda t: palette.index(remap[t[3]])):
                c = remap[t[3]]
                if c != last:
                    f.write(f"usemtl {name_of[c]}\n")
                    last = c
                f.write(f"f {new[t[0]]} {new[t[1]]} {new[t[2]]}\n")
        else:
            # Every face cut into squares that each show their colour's texture once.
            # The underside of the lowest floor is never seen: left out. A very big
            # map gets bigger squares, so the game still loads it quickly.
            low = min(pts[i][1] for i in used)
            seen = [t for t in world if not (all(pts[i][1] <= low + 0.5 for i in t[:3]) and faces_down(pts, t))]
            cell = args.texture_size * args.scale
            faces = textured_faces(pts, seen, remap, cell)
            while len(faces) > MAX_DRAWN and cell < 64 * args.texture_size * args.scale:
                cell *= 2
                faces = textured_faces(pts, seen, remap, cell)
            if cell != args.texture_size * args.scale:
                print(f"note: a big map: texture squares widened to {cell / args.scale:g} mm "
                      f"to keep it under {MAX_DRAWN} drawn faces")
            texture_mm = cell / args.scale
            faces.sort(key=lambda fc: palette.index(fc[0]))
            vi, ti, lines = {}, {}, []
            last = None
            for c, tri in faces:
                if c != last:
                    lines.append(f"usemtl {name_of[c]}")
                    last = c
                ref = []
                for p, uv in tri:
                    a = vi.setdefault((round(p[0], 1), round(p[1], 1), round(p[2], 1)), len(vi) + 1)
                    b = ti.setdefault((round(uv[0], 4), round(uv[1], 4)), len(ti) + 1)
                    ref.append(f"{a}/{b}")
                lines.append("f " + " ".join(ref))
            for p in vi:                         # (dicts keep the order things were added in)
                f.write(f"v {p[0]:.1f} {p[1]:.1f} {p[2]:.1f}\n")
            for uv in ti:
                f.write(f"vt {uv[0]:.4f} {uv[1]:.4f}\n")
            f.write("\n".join(lines) + "\n")
            visual_faces = len(faces)

    # Collision: whole units; faces that rounding flattens to nothing dropped.
    ipts = {i: tuple(int(round(v)) for v in pts[i]) for i in used}
    polys = 0
    with open(os.path.join(out, "collision.obj"), "w") as f:
        f.write(f"# {args.name}: collision (every coloured shape except the start markers)\n")
        for i in used:
            p = ipts[i]
            f.write(f"v {p[0]} {p[1]} {p[2]}\n")
        for t in world:
            a, b, c = (ipts[t[0]], ipts[t[1]], ipts[t[2]])
            u = [b[i] - a[i] for i in range(3)]
            w = [c[i] - a[i] for i in range(3)]
            n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
            if n == (0, 0, 0):
                continue
            f.write(f"f {new[t[0]]} {new[t[1]]} {new[t[2]]}\n")
            polys += 1

    def st(k):  # x, y down, z, facing
        s = starts[k]
        return f"[{int(round(s[0]))}, {-int(round(s[1]))}, {int(round(s[2]))}, {face[k]}]"

    with open(os.path.join(out, "stage.toml"), "w") as f:
        f.write("# A custom map made in Tinkercad (tools/maps/tinkercad_stage.py, docs/MAPS.md).\n"
                "# Engine world units; y is down as in the game.\n")
        f.write(f'id = {args.id}\nname = "{args.name}"\nkind = "custom"\n')
        f.write(f"visual_faces = {visual_faces}\ncollision_polygons = {polys}\n")
        f.write("# 1v1: one AC at each side's start. Teams: teammates side by side there.\n")
        f.write(f"start_1 = {st(1)}\nstart_2 = {st(2)}\n")
        f.write("# Every marker as placed (team 1 green / players, team 2 red / enemies).\n")
        for k in (1, 2):
            items = ", ".join(f"[{int(round(p[0]))}, {-int(round(p[1]))}, {int(round(p[2]))}, {face[k]}]" for p in spots[k])
            f.write(f"team_{k} = [{items}]\n")

    print(f"{args.name}: {len(world)} faces, {len(palette)} colours, "
          f"{width / args.scale:.0f} x {depth / args.scale:.0f} mm = {int(width)} x {int(depth)} units")
    if not args.plain:
        print("textures: " + ", ".join(f"#{name_of[c][1:]} {kinds[c]}" for c in palette)
              + f"; {visual_faces} drawn faces ({texture_mm:g} mm squares; --plain for none)")
    print(f"start 1 {st(1)}  start 2 {st(2)}")
    print(f"written to {out}  ->  run the game with ACR_STAGE={args.id}")


if __name__ == "__main__":
    main()
