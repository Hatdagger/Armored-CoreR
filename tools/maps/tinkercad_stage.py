#!/usr/bin/env python3
"""Turn a Tinkercad export into an Armored-CoreR stage.

In Tinkercad: Export -> .OBJ. That downloads a zip (tinker.obj + obj.mtl).
Then:

    python tinkercad_stage.py my-map.zip <import dir> --id 50 --name "My Map"

and start the game with ACR_STAGE=50. See docs/MAPS.md for the rules
(scale, colours for the two starts, what makes a good map).

Writes <import dir>/stages/<id>/:
    stage.toml     name, starts
    visual.obj/mtl what is drawn (Tinkercad's colours)
    collision.obj  what ACs stand on, run into and shoot

No packages needed: plain Python 3.
"""
import argparse
import math
import os
import sys
import zipfile

# Marker colours (Tinkercad: shape colour -> custom -> hex).
P1_HEX = (0xFF, 0x00, 0xFF)   # magenta: player 1's start
P2_HEX = (0x00, 0xFF, 0xFF)   # cyan: player 2's start
MARKER_TOL = 24               # per channel, 0-255
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


def is_colour(c, hexc):
    return all(abs(c[i] - hexc[i]) <= MARKER_TOL for i in range(3))


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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="Tinkercad .zip (Export -> .OBJ) or an .obj")
    ap.add_argument("import_dir", help="the folder acr-import wrote (the one holding stages/)")
    ap.add_argument("--id", type=int, default=50, help="stage number (default 50; 0-40 are the disc's)")
    ap.add_argument("--name", default="Custom Map")
    ap.add_argument("--scale", type=float, default=50.0, help="world units per mm (default 50)")
    ap.add_argument("--up", choices=["auto", "y", "z"], default="auto", help="the file's up axis")
    args = ap.parse_args()
    if 0 <= args.id <= 40:
        print(f"warning: stage {args.id} is one of the disc's; it will be overwritten until the next import")

    obj, mtl = read_input(args.input)
    cols = parse_mtl(mtl)
    raw, tris = parse_obj(obj, cols)
    if not tris:
        sys.exit("The file has no faces.")
    up = guess_up(raw, tris) if args.up == "auto" else args.up
    pts = [to_engine(p, up, args.scale) for p in raw]

    # Pull out the two start markers.
    marks = {1: [], 2: []}
    world = []
    for t in tris:
        if is_colour(t[3], P1_HEX):
            marks[1].extend(t[:3])
        elif is_colour(t[3], P2_HEX):
            marks[2].extend(t[:3])
        else:
            world.append(t)
    if not world:
        sys.exit("Only start markers in the file: nothing to stand on.")

    # Centre the map on x / z (heights stay: Tinkercad's workplane is 0).
    xs = [pts[i][0] for t in world for i in t[:3]]
    zs = [pts[i][2] for t in world for i in t[:3]]
    cx, cz = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2
    pts = [(p[0] - cx, p[1], p[2] - cz) for p in pts]
    width, depth = (max(xs) - min(xs)), (max(zs) - min(zs))

    starts = {}
    for k in (1, 2):
        if marks[k]:
            mp = [pts[i] for i in marks[k]]
            x = (min(p[0] for p in mp) + max(p[0] for p in mp)) / 2
            z = (min(p[2] for p in mp) + max(p[2] for p in mp)) / 2
            starts[k] = [x, min(p[1] for p in mp), z]
        else:
            z = (-0.35 if k == 1 else 0.35) * depth
            y = top_y(pts, world, 0.0, z)
            if y is None:
                sys.exit(f"No start {k} marker and no floor at the map's middle line to drop it on.")
            starts[k] = [0.0, y, z]
            print(f"note: no {'magenta' if k == 1 else 'cyan'} marker; start {k} put at x 0, z {int(z)}")
    for k in (1, 2):
        s = starts[k]
        floor = top_y(pts, world, s[0], s[2], below=s[1])
        if floor is None:
            print(f"warning: nothing under start {k}: that AC will fall")
        elif s[1] - floor > 600:
            print(f"warning: start {k} is {int(s[1] - floor)} units above the floor below it (it will drop)")
        else:
            s[1] = floor
    face = {1: facing_to(starts[1], starts[2]), 2: facing_to(starts[2], starts[1])}

    out = os.path.join(args.import_dir, "stages", f"{args.id:02d}")
    os.makedirs(out, exist_ok=True)

    remap, merged = merge_colours([t[3] for t in world])
    if merged:
        print(f"note: {merged} rarely used colour(s) merged into the nearest of {MAX_COLOURS}")
    palette = sorted(set(remap.values()))
    name_of = {c: f"c{c[0]:02x}{c[1]:02x}{c[2]:02x}" for c in palette}

    with open(os.path.join(out, "visual.mtl"), "w") as f:
        for c in palette:
            f.write(f"newmtl {name_of[c]}\nKd {c[0] / 255:.4f} {c[1] / 255:.4f} {c[2] / 255:.4f}\n\n")
    used = sorted({i for t in world for i in t[:3]})
    new = {old: n + 1 for n, old in enumerate(used)}
    with open(os.path.join(out, "visual.obj"), "w") as f:
        f.write(f"# {args.name}: made from a Tinkercad export by tools/maps/tinkercad_stage.py\nmtllib visual.mtl\n")
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
        f.write(f"visual_faces = {len(world)}\ncollision_polygons = {polys}\n")
        f.write(f"start_1 = {st(1)}\nstart_2 = {st(2)}\n")

    print(f"{args.name}: {len(world)} faces, {len(palette)} colours, up axis {up}, "
          f"{width / args.scale:.0f} x {depth / args.scale:.0f} mm = {int(width)} x {int(depth)} units")
    print(f"start 1 {st(1)}  start 2 {st(2)}")
    print(f"written to {out}  ->  run the game with ACR_STAGE={args.id}")


if __name__ == "__main__":
    main()
