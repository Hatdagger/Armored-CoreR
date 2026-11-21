#!/usr/bin/env python3
"""Writes a small sample map the way Tinkercad exports one (a zip with
tinker.obj + obj.mtl, millimetres, z up): a 260 x 260 mm floor, walls
round it, four pillars, a ramp up to a ledge, and two players (green) and two
enemies (red) as teammate pairs. Used by the tests; also a starting point to look at.

    python make_sample.py sample.zip
    python make_sample.py --tiles <folder>

--tiles writes a map in three 200 mm tiles instead: "A1.zip" (west end,
a magenta marker: the older colours), "B1.zip" (east end, cyan) and "A2 rot90.zip" (a
pillar off its middle, turned a quarter), to check joining.
"""
import os
import sys
import zipfile

boxes = []   # (x0, y0, z0, x1, y1, z1, material)


def box(x0, y0, z0, x1, y1, z1, m):
    boxes.append((x0, y0, z0, x1, y1, z1, m))


def write(path):
    mats = {"grey": (0.55, 0.55, 0.58), "tan": (0.76, 0.66, 0.48), "orange": (0.9, 0.45, 0.1),
            "magenta": (1, 0, 1), "cyan": (0, 1, 1),
        "green": (0.2745, 0.7176, 0.2863), "red": (0.9137, 0.1137, 0.1765)}   # Tinkercad's own

    obj = ["# sample", "mtllib obj.mtl"]
    faces = []
    n = 0
    for x0, y0, z0, x1, y1, z1, m in boxes:
        for z in (z0, z1):
            for y in (y0, y1):
                for x in (x0, x1):
                    obj.append(f"v {x} {y} {z}")
        q = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
        faces.append(f"usemtl {m}")
        for a, b, c, d in q:   # as triangles, like Tinkercad
            faces.append(f"f {n + a + 1} {n + b + 1} {n + c + 1}")
            faces.append(f"f {n + a + 1} {n + c + 1} {n + d + 1}")
        n += 8
    mtl = "".join(f"newmtl {k}\nKd {r} {g} {b}\n\n" for k, (r, g, b) in mats.items())
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("tinker.obj", "\n".join(obj + faces) + "\n")
        z.writestr("obj.mtl", mtl)


def single(path):
    box(-130, -130, 0, 130, 130, 4, "grey")                 # floor plate, top at 4 mm
    for s in (-1, 1):
        box(-130, s * 130 - 4, 4, 130, s * 130 + 4, 40, "tan")   # north / south walls
        box(s * 130 - 4, -126, 4, s * 130 + 4, 126, 40, "tan")   # east / west walls
    for x in (-50, 50):
        for y in (-50, 50):
            box(x - 8, y - 8, 4, x + 8, y + 8, 60, "orange")     # pillars
    box(70, -20, 4, 126, 20, 20, "grey")                    # a ledge on the east side
    for x in (-10, 10):                                     # 10 mm either side: teammates
        box(x - 2, -92, 4, x + 2, -88, 8, "green")          # players (south, face north)
        box(x - 2, 88, 4, x + 2, 92, 8, "red")              # enemies (north)
    write(path)


def tiles(folder):
    """Tinkercad's own spots (somewhere on the workplane, not centred) on purpose."""
    os.makedirs(folder, exist_ok=True)
    # A1: floor 200 x 200 at x 0..200, y 0..200; walls west and south; P1 marker.
    box(0, 0, 0, 200, 200, 4, "grey")
    box(0, 0, 4, 4, 200, 40, "tan")
    box(0, 0, 4, 200, 4, 40, "tan")
    box(94, 94, 4, 106, 106, 8, "magenta")
    write(os.path.join(folder, "A1.zip"))
    boxes.clear()
    # B1: floor at x 300..500; walls east and south; P2 marker.
    box(300, 0, 0, 500, 200, 4, "grey")
    box(496, 0, 4, 500, 200, 40, "tan")
    box(300, 0, 4, 500, 4, 40, "tan")
    box(394, 94, 4, 406, 106, 8, "cyan")
    write(os.path.join(folder, "B1.zip"))
    boxes.clear()
    # "A2 rot90": a pillar 50 mm north of the middle and a wall on the north edge
    # (turned a quarter: 50 mm east, the east edge). Made round its middle.
    box(-100, -100, 0, 100, 100, 4, "grey")
    box(-8, 42, 4, 8, 58, 60, "orange")
    box(-100, 96, 4, 100, 100, 40, "tan")
    write(os.path.join(folder, "A2 rot90.zip"))


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--tiles":
        tiles(sys.argv[2])
    else:
        single(sys.argv[1] if len(sys.argv) > 1 else "sample.zip")
