#!/usr/bin/env python3
"""Writes a small sample map the way Tinkercad exports one (a zip with
tinker.obj + obj.mtl, millimetres, z up): a 260 x 260 mm floor, walls
round it, four pillars, a ramp up to a ledge, and the two start markers
(magenta, cyan). Used by the tests; also a starting point to look at.

    python make_sample.py sample.zip
"""
import sys
import zipfile

boxes = []   # (x0, y0, z0, x1, y1, z1, material)


def box(x0, y0, z0, x1, y1, z1, m):
    boxes.append((x0, y0, z0, x1, y1, z1, m))


box(-130, -130, 0, 130, 130, 4, "grey")                 # floor plate, top at 4 mm
for s in (-1, 1):
    box(-130, s * 130 - 4, 4, 130, s * 130 + 4, 40, "tan")   # north / south walls
    box(s * 130 - 4, -126, 4, s * 130 + 4, 126, 40, "tan")   # east / west walls
for x in (-50, 50):
    for y in (-50, 50):
        box(x - 8, y - 8, 4, x + 8, y + 8, 60, "orange")     # pillars
box(70, -20, 4, 126, 20, 20, "grey")                    # a ledge on the east side
box(-6, -96, 4, 6, -84, 8, "magenta")                   # start 1 (south, faces north)
box(-6, 84, 4, 6, 96, 8, "cyan")                        # start 2 (north)

mats = {"grey": (0.55, 0.55, 0.58), "tan": (0.76, 0.66, 0.48), "orange": (0.9, 0.45, 0.1),
        "magenta": (1, 0, 1), "cyan": (0, 1, 1)}

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
with zipfile.ZipFile(sys.argv[1] if len(sys.argv) > 1 else "sample.zip", "w") as z:
    z.writestr("tinker.obj", "\n".join(obj + faces) + "\n")
    z.writestr("obj.mtl", mtl)
