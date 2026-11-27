# Gen 2 research scripts

Python 3 scripts used to take apart the Armored Core 2 data archive. Notes: `docs/research/gen2-archive.md`.
These are research tools. The real importer is `tools/acr_import`.

Input: AC2DATA.BIN split into one file per ID, named `NNNNN.bin` (5-digit file ID), in group folders under `$AC2_DATA` (default `data/ac2`), e.g. `data/ac2/AC2_B_0301-0828/00351.bin`.
Needs numpy and Pillow, plus matplotlib for `render.py`. `weapons.py` also needs the A block (file 00010.bin) under `$AC2_DATA/AC2_A_0001-0190/`.

| Script | What it does |
|---|---|
| `stitch.py LO HI` | Joins IDs LO..HI into one stream (parts overlap by 32 KB) and lists every model header found |
| `model.py FILE` | Loads a part model (mesh positions, strips, vertex counts) |
| `vif.py FILE OFFSET` | Walks a PS2 DMA/VIF chain and lists UNPACK blocks |
| `render.py OUT.png ID...` | Contact sheet of parts (negative number = Nth model from the stitched B block) |
| `tex.py OUTDIR ID...` | Decodes TEX0 textures to PNG |
| `slots.py ID...` | Shows what each 32 KB slot of a file holds |
| `findhdr.py FILE...` | Finds model headers inside files |
| `weapons.py OUT.json` | Weapon stats (attack, ammo, reload, heat…) from file 10, joined to part names |
| `textured.py OUT.png CAT N` | Loads an in-game part with its per-strip textures and renders it (CAT = core, head, arms, legs, booster, rweapon, lweapon, back) |
| `gltf.py OUT.glb [cat=N ...]` | Exports an assembled, textured AC as binary glTF (default = PR #64 test AC) |
| `cls.py`, `fp.py`, `scan.py`, `hd.py` | First-pass fingerprinting and a hex dump |
