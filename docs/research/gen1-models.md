# Gen 1 models (Master of Arena): first findings

How the game stores an AC's meshes, textures and colours. Found from the
disc layout, a CD read log and a VRAM read during a VS fight in the recomp.
Research scripts: `tools/research/moa_model.py` (mesh reader),
`moa_model_view.py` (quick picture of a mesh), `moa_tim.py` (TIM lister).
They read your own import folder; nothing from the disc is in the repo.

## What an AC loads

The body model set is one of the 72 archives `GG/P<n/20>/PA<nn>.T`, picked
from the core, arms and legs (table at `0x800D4A64`, already used for
root motion). For the starting AC the fight reads these entries of PA00:

| Entry | What |
| --- | --- |
| 0 | joint animations (rotations) for all 57 animations, see below |
| 1 | root motion (already decoded, `acr_motion_moa.c`) |
| 2 | core (1 piece) |
| 18 | legs (7 pieces) |
| 62 | head (1 piece) |
| 128 | arms (7 pieces) |
| 81, 100, 155, 255 | weapons / back units (1-2 pieces) |

Each player loads their own copy.

### Entry numbers (mapped 2026-11-14)

Every PA archive uses the same numbering; it only holds the cores, arms
and legs that its set (table `0x800D4A64`) lists, plus all heads, boosters,
hand weapons and back units. Checked against all 72 sets.

| Entries | Part (game index i) | Pieces |
| --- | --- | --- |
| 2 + i | core | 1 |
| 15 + i | legs | 7 (caterpillars 1) |
| 57 + i | head | 1 |
| 81 + i | booster | 2 |
| 99 + i | hand weapon (99 = none) | 1 |
| 127 + i | arms | 7 (weapon arms 8) |
| 155 + i | back unit, left | 2 |
| 215 + i | back unit, right | 2 |

No blade model was seen yet (`WEL_T` holds 5 blade textures). The second
piece of every back unit is a single repeated point (likely a muzzle or
launch point).

### Texture entries

`CORE_T`, `HEAD_T`, `ARMS_T`, `LEG_T` hold 16 versions of each part:
entry = pattern x (table size) + i, table sizes 11, 18, 24, 40. The 16
versions differ only in which colour slot each pixel uses, so they are the
16 colour patterns. `BST_T` (14), `WER_T` (26, hand weapons) `WEL_T` (6,
blades), `BWL_T`/`BWR_T` (60, back left/right) have one each and carry
their own palette. `GENE_T`, `COMP_T`, `SPEC_T` (generators, FCS,
optional parts) are 64x128 pictures with palettes, likely the garage and
shop pictures for parts without a model.

## Mesh entries

```
u32 size, u32 0, u32 0x0C, u32 pieces, u32 lods (3), u32 objects (= pieces x 3)
then a PlayStation TMD object table at 0x18, offsets relative to 0x18:
  vert_top, n_vert, normal_top, n_normal, prim_top, n_prim (low 16 bits;
  high 16 bits unknown, falls by piece), extra_top
```

- Objects come in three groups of `pieces`: full detail, simple boxes
  (8-12 vertices; likely hit or shadow volumes), low detail.
- Vertices and normals: `s16 x, y, z, pad` (y down, as on the PS1).
- Primitives are standard TMD packets (`olen, ilen, flag, mode`); seen so
  far: lit flat/gouraud, textured, triangles and quads (modes 0x20-0x2F).
- `extra_top` points at 8 bytes after the normals: `s16 x, y, z`, a point
  near the piece's centre in the bind pose (legs: hip 462 up, thighs 402,
  knees 211, ankles 32; x +-77 for right/left). It is **not** the joint
  position (head: -686 here, joint at -657); the joint position comes from
  the skeleton (a pivot's position is minus its joint's place).
- The same part's mesh differs between sets only by where the set's
  skeleton puts it: moved into its joint's space (vertex + pivot position)
  the pieces match to within 1 unit. Legs 9 is the exception: sets that do
  not list it carry a different, unused mesh, so take each part from a set
  that lists it.

## Animations (entry 0)

Decoded 2026-11-14; `tools/research/moa_anim.py` parses all 3540
animation records in the 72 sets and poses a picture.

Header: `u32 size, u32 ?, u32 ?, u32 joint_table, u32 list, u32 a, u32 b`.
`list` holds 57 offsets (relative to `list`; 0 = not in this set).

**Joint table** (byte codes): `n, 0`, then `08 node` per node, then 4-byte
commands `02 node slot 00` and `03 node parent(u16, ffff = root)`. Nodes
come in two kinds:

- joints (rotation + position = the joint's place in the bind pose),
- pivots (position = minus the joint's place; parent = that joint). A
  joint's parent is the pivot of the joint above it, so world =
  parent pivot x T(position) x R(rotation).

Each pivot draws one slot (`02`); a piece is drawn with its pivot's matrix,
because the mesh vertices are already in bind-pose model space. Node 2 is
the root (pivot without joint, small up/down bob).

**Slots**, from the highest number down, hold: head, core, legs piece 0
(hip, on the root), all arms pieces (+x side first: shoulder, upper arm,
elbow, wrist), the hand weapon (regular arms only, on the -x wrist), the
remaining legs pieces (+x leg, then -x), back right 0, back left 0, back
left 1, booster 0, booster 1, back right 1. Standard sets have 45 nodes and
23 slots; caterpillars 36/17 (one legs piece).

- 47 nodes / 23 slots: weapon arms (8 pieces, arms 12-21). Same order with
  8 arms pieces and **no hand slot**.
- 52 nodes / 25 slots: four legs (9 pieces, legs 21-26). Legs 1-4 (front
  pair) come before the back units, legs 5-8 (rear pair) after them.

Checked by drawing every kind of set (`moa_anim.py`, `moa_export_view.py`).

**Animation record**: `s16 last_tick, s16 frames, 0`, then `frames - 1`
hold times in ticks (1-7), then frames. A frame is `s16 n` and n node
records: `u16` (node << 8 | flags), then data by the flags:

| Bits | Meaning |
| --- | --- |
| 0-1 | 0 in the first frame, 1 in later frames (partial frame) |
| 2-3 | rotation: 1 = 3 x s16, 2 = 3 x s8 (+1 pad byte), 3 = zero, 0 = keep |
| 6-7 | position: same coding |

Values are absolute (not deltas); nodes not listed keep their last value.
Rotations: 4096 = a full turn, the same units for s8 and s16.

**Rotation order: M = Rx · Ry · Rz** (confirmed 2026-11-14). The game's
rotation-matrix routine (main executable `0x80029F94`, PsyQ RotMatrix) builds
exactly this; a Ry · Rx · Rz version also exists (`0x8002A224`) but nothing
calls it. Checked in the recomp's split-screen VS: a replacement idle record
with large core rotations was planted in RAM (each player's entry 0 copy) and
the AC matched the X·Y·Z picture for (1024, 1024, 0) and (1024, 0, 1024),
which rule out every other order.

Open: tables `a` and `b` (they list the arm, waist and back-unit joints:
likely aiming).

## Textures

Not in the PA archive. Each part type has its own archive in `GG/MS`:
`CORE_T`, `HEAD_T`, `ARMS_T`, `LEG_T`, `WEL_T`/`WER_T` (weapons left and
right), `BWL_T`/`BWR_T` (back), `BST_T` (booster), `GENE_T`, `COMP_T`.
Each entry is one 8-bit TIM **without a palette**, e.g. cores at VRAM
(704,256) 128x128 px, arms (768,256), head (768,352), legs (704,416).
How an entry is chosen (CORE_T has 176 entries for 5 cores) is open.

The pixels are shading levels, not colours: index = slot x 64 + shade.
The palette is generated from the AC's colour scheme: 4 colour slots, each
a 64-step ramp from the slot colour down to black. Each part type has its
own palette row in VRAM (player 1): core 486, booster 487 (own CLUT), legs
488, arms 489, head 490, hand weapon 492, back left 493, back right 494;
player 2's AC palette is row 495. With one colour scheme the core, legs,
arms and head rows are identical.

Colour schemes are stored as 4 x RGB in 0-63 steps (above 31 is clipped to
the 5-bit maximum, which brightens slot 0). The menu module holds named
presets; "DEFAULT" (the starting AC) is the 12 bytes after its name.
colour x (64 - (shade & ~1)) / 64, rounded, matches the game's palette
to within about 1 step per channel (not exactly; the exact routine is not
found yet). So our engine can build the palette from any colours the player
picks and keep the original look.

Textured faces pick their texture by palette row, not by the part: hand
weapons draw part of themselves (the hand) with the **arms'** palette and
texture, a few back-right units use the back-left texture, and some arms and
back units use a 4-bit effects page at (512, 256) with palette row 510 (not
found on the disc yet).

## Export (acr-import, `tools/acr_import/acr_models_moa.c`)

`<import>/models/`:

- `<part>/<NN>/pieceK.obj`: one OBJ per piece, full detail only, in its
  joint's space (vertex + pivot position from the part's source set), axes
  converted to x right, y up, z back (`(x, -y, -z)`, keeps the winding).
  Normals included. Materials are named after the part type whose texture
  and palette a face uses (`core`, `arms`, ...), `fx`, or `rgb-RRGGBB` for
  untextured faces; `-semi` = semi-transparent, `-2side` = no culling.
- `<part>/<NN>/texture.png`: 8-bit indexed. Core/head/arms/legs: colour
  pattern 0, palette from the DEFAULT scheme (indices are exact, so the
  engine can swap the palette). Others: their own palette, colour 0x0000
  transparent.
- `<part>/<NN>/part.toml`: source set, pieces, each piece's node and
  bind joint position.
- `sets/set-NN.toml`: nodes (parent, slot, which piece it draws, bind
  position/rotation from animation 0 frame 0) in game space.
- `sets/set-NN-anims.txt`: all animations as text (hold ticks, per-frame
  node changes).

`tools/research/moa_export_view.py` rebuilds an AC from these files only.

## In the engine (2026-11-14)

`src/model/acr_model.c` (skeleton, animations, OBJ/MTL, pose matrices,
colour ramp) and `acr_png.c` (PNG reader with a full inflate, so edited
textures load); `src/platform/sdl3/demo_model.c` loads a build and draws it.

- Model space (OBJ) has the AC facing +z with its right hand at -x; the
  test build's world is mirrored on screen (right = +x), so x is negated
  when an AC is placed.
- Pose at a tick: bind pose, then frames 0..k of the animation; frame i
  stays for its hold ticks, the last one for good. Frame ticks are movement
  steps, the same count the root motion uses (`anim_frame`).
- Textured faces marked semi-transparent are drawn solid: on the PS1 only
  texels with the STP bit blend, and neither the export nor the colour ramp
  keeps that bit. Untextured semi faces blend 50/50.
- Hand weapons and some back units use another part's texture (material
  named after it): the engine uses the fitted part of that kind, as the
  game does through VRAM.
- Lighting: vertex colour 128 = the texel as it is (texel x colour / 128),
  like the PS1, with the default lights until stage lights are read.

## Next

1. Garage/shop on the real models; open items above and below.
2. Find the effects texture (page 512,256, palette row 510) and the
   exact palette ramp routine.

## Which animation the game shows (measured, AC object +108)

Walk start 1 then 2; jump start 22, rising 25, falling 24, landing 36;
blade swing 56 (from ~10 refreshes after Circle, ~77 refreshes long);
ground boost 48 while boosting forward and 49 after letting go, reversed
for a backward boost (49 while boosting back, 48 after): 48 leans into the
direction of travel, 49 brakes against it. Firing changes no body
animation. Kneel not checked yet (needs a cannon build).
