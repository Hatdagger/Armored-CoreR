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

Each player loads their own copy. Which entry belongs to which part is
still to be mapped (part model ids, weapon detail +1).

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

`u32 size?, u32 ?, u32 ?, u32 0x1C, u32 0x188, u32 a, u32 b`: a joint
table at 0x1C, a list of 57 offsets (relative to 0x188, ending 0x10000)
to per-animation records of signed 16-bit values (rotations), and two
small tables at `a` and `b`. Layout still to decode; the root motion in
entry 1 lines up with it (57 animations in both).

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

1. Map PA entries and `*_T` entries to part ids (trace the loader that
   reads the part list before VS start).
2. Decode entry 0 (joint rotations) and the piece hierarchy.
3. Exporter: OBJ per piece + a PNG per texture (palette from the default
   colours) + a rig file (joints, parents, bind positions).
