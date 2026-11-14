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
- `extra_top` points at 8 bytes after the normals: `s16 x, y, z` = the
  piece's joint position in the bind pose (legs: hip 462 up, thighs 402,
  knees 211, ankles 32; x +-77 for right/left). Without animation the
  pieces sit apart (the poses come from entry 0).

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
23 slots; caterpillars 36/17; others 47 and 52 (not looked at yet).

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

Open: rotation order (X·Y·Z works for the pictures; check against the
recomp), tables `a` and `b` (they list the arm, waist and back-unit joints:
likely aiming), sets with 47 and 52 nodes.

## Textures

Not in the PA archive. Each part type has its own archive in `GG/MS`:
`CORE_T`, `HEAD_T`, `ARMS_T`, `LEG_T`, `WEL_T`/`WER_T` (weapons left and
right), `BWL_T`/`BWR_T` (back), `BST_T` (booster), `GENE_T`, `COMP_T`.
Each entry is one 8-bit TIM **without a palette**, e.g. cores at VRAM
(704,256) 128x128 px, arms (768,256), head (768,352), legs (704,416).
How an entry is chosen (CORE_T has 176 entries for 5 cores) is open.

The pixels are shading levels, not colours: index = slot x 64 + shade.
The palette (CLUT row 486 for player 1, 495 for player 2) is generated
from the AC's colour scheme: 4 colour slots, each a 64-step ramp from the
slot colour down to black (about colour x (64 - shade) / 64, in pairs).
So our engine can build the palette from any colours the player picks,
the same way, and keep the original look.

## Next

1. Check the rotation order against the recomp picture.
2. Exporter in acr-import: OBJ per piece (bind-pose space, y flipped up) +
   PNG per texture (palette from the default colours, pattern 0) + a rig
   file (nodes, parents, slots, piece per slot) + animations as text.
3. Engine: load a build's pieces, play the animations (hold times in ticks).
