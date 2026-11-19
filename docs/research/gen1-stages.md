# Gen 1 stages (Master of Arena arenas): first findings

Read 2026-11-19 from the arena engine's stage loader (FDAT 210,
`0x80056330`-`0x800564A8`) and the VS engine's collision code (FDAT 209,
`0x80073EB0`-`0x80074530`), and checked by assembling Green Silo (stage
27) from its file. Research reader: `tools/research/moa_stage.py`.
acr-import writes every arena stage as readable files
(`tools/acr_import/acr_stages_moa.c`, see "Import" below).

## Files

Stage `s` (20-40 are the arena / VS stages) uses FDAT entries `5s+0..5s+4`:
VS loads `5s+1` (code) and `5s+4`; the arena loads `5s+2` (code, to
`0x8009EFE8`) and `5s+4`. Green Silo (27) has no VS code (FDAT 136 is
empty): it is an arena-only stage.

| Stage | Name | | Stage | Name |
| --- | --- | --- | --- | --- |
| 20 | Lunar Base | | 29 | Seabed |
| 22 | Pillar Hall | | 33 | Forest |
| 24 | Scaffold Yard | | 36 | Night City |
| 25 | Garden Plaza | | 39 | Ocean Platform |
| 27 | Green Silo | | | |

(The arena's stage list, from the fork's hub; the other numbers' names
aren't known yet.)

## FDAT 5s+4: chunks

A run of `[u32 size][data]` chunks, read in this order:

| # | Green Silo size | Loader call | What |
| --- | --- | --- | --- |
| 0 | 0x50 | `0x8005A9A0(0x48)` | model set: u16 count, then entries `[u32 size][model]` (size < 5 = empty), registered at model slot 0x48 + i (`0x801A686C`); same mesh format as AC parts |
| 1 | 0xC778 | `0x8005A9A0(0x104)` | **the stage's textured models**: model set, entry k = the textured version of model k (TMD at entry +0xC; some have two objects, both drawn) |
| 2 | 0x8 | `0x8005A9A0(0x80)` | model set |
| 3 | 0xCCC | `0x8005A9A0(0xA0)` | model set |
| 4 | 0x3A6C | `0x8005E9F0(0, ...)` | **the stage's area models**: one PlayStation TMD (id 0x41), one object per model number |
| 5 | 0 | stored at `0x801F0E30` | ? |
| 6 | 0xC3C0 | `0x80052DF8` | ? (starts `1, 0xE20, "pBAV"`: sound bank?) |
| 7 | 0x3688 | copied to `0x801B2018`, then `0x80074154` | **collision**, per model number |
| 8 | 0x863C | `0x800783B0` | **area records** (512 x 52 bytes, 64 in RAM at `0x801C2018`), then neighbour lists |
| 9 | 0x34 | `0x80079908` | ? |
| 10 | 0x4000 | `0x80079798` | ? |
| 11 | 0x3800 | `0x8005DB5C` | ? |
| 12 | 0x4 | | ? |
| 13 | 0x2800 | `0x80079E08` | **points / objects**, 40-byte records |

Chunk 4's models are a flat, see-through green copy of chunk 1's (same
numbers, same shapes; TMD modes 0x22 / 0x2A) in every stage 20-40: a
simple version (radar, map or far view?). The look is chunk 1's textured
models (corrected 2026-11-19 after the user's description of Green Silo:
dark grey arches and floors, green windows). Other stages have more data
after chunk 13 (not read yet).

TMD packets in stage models (as in AC models) always have the lit layout
(normal, then corners), whatever the flag byte's light bit: Green Silo's
arches carry flag 0x11. Read as unlit, a padding zero became a corner at
vertex 0 (the dome's top) and stretched faces into the roof (user's
report, fixed 2026-11-19).

See-through textured faces (mode bit 1) blend only where the texel's
colour has bit 15 set; Green Silo's palette 481 (glass, windows) has it on
every colour, 480 on none. acr-import marks a page + palette material
`-semi` when its palette has any such colour; the engine draws those 50/50.

## Textures: RTIM <stage>

Before the stage file the arena loader reads **RTIM entry <stage>**
(`0x80056230`, the same byte as the FDAT number): TIMs straight into VRAM.
Green Silo: three 8-bit pages, (320, 0) and (384, 0) 64 x 256 words with
palettes on rows 480 / 481, and (640, 0) 64 x 128 (palette row 485). The
faces find them through their TMD tsb (page) and cba (palette). Faces on
page 448 (a few on models 0, 9, 10) have no texture in RTIM 27: loaded
elsewhere (shared effects page?); drawn plain grey for now.

## Area records (chunk 8)

52 bytes each on disc (the RAM copy is 64: 12 bytes the loader fills).

| Offset | Type | What |
| --- | --- | --- |
| +0x00 | s16 x3 | box min x, y, z (world units, y down) |
| +0x08 | s16 x3 | box max |
| +0x10 | s16 x3 | centre: where the area's model sits |
| +0x16 | u16 | turn: bits 10-11 = quarter turns (the loader keeps `(v >> 10) & 3`) |
| +0x18 / +0x20 | s16 x3 | the box again (view-cone test) |
| +0x1E | s16 | **model number** (chunk 4 object, chunk 7 collision entry) |
| +0x26 | s16 | neighbour count |
| +0x28 | u32 | neighbour list (offset into the tail, made a pointer) |
| +0x2C | | 700 in Green Silo |
| +0x30 | u32 | flags (renderer.md: bits 8, 11-13) |

A model point `p` (model units, 4 to a world unit, as AC models) is placed
at `centre + turn(p / 4)`, turning by the quarter count the other way to
the collision code's world-to-model turn (`0x80074400`):
q = 1: (x, z) -> (-z, x); 2: (-x, -z); 3: (z, -x).

Green Silo: 104 areas. Four quarter domes (model 0, radius 6,718, floor
at -662, roof at -6,750), four quarter discs of floor (model 12), the
tunnels underneath (models 1-7, below the floor, no collision) and 60 tall
empty boxes (model 8, no polygons): area volumes, perhaps for the camera
or out-of-area.

## Collision (chunk 7)

1,024 pairs of u32 offsets (from the chunk's start; made pointers by
`0x80074154`), one per model number: **vertices** and **polygons**.

- Vertices: `s16 x, y, z, pad` (model units).
- Polygons: `u16 count`, three 32-byte bit tables (x, y and z slices of the
  area box, 8 words each; bit k = polygons 32k-32k+31 reach that slice;
  `0x80074178` ANDs them to skip groups), then from +0x64 one 20-byte
  record per polygon:

| Offset | Type | What |
| --- | --- | --- |
| +0 | u16 | flags: bit 0 = four corners; bits 2-13 pick which corners are the box's min / max on each axis (`0x8007386C`) |
| +2 | s16 x3 | unit normal (4096 = 1) |
| +8 | s32 | plane distance: normal . p + d = 0 (model units x 4096) |
| +0xC | u16 x4 | corners: byte offset into the vertices in bits 3-14; four corners in strip order (0 1 3 2 around), like TMD quads |

The search (`0x80073EB0`): find the area the point is in (`0x80074530`),
turn the point into the model's space (`0x80074210`), AND the slice masks,
test the polygons of the groups left (`0x80073A6C` -> `0x8007386C`).

Green Silo: 396 polygons (91 in each quarter dome, 8 in each floor
quarter). What the engine then does with a hit (wall slide, floor height
AC +0x2C, ceilings, step heights) is not read yet.

## Points (chunk 13)

256 records of 40 bytes: `s16 x, y, z`, `s16` (-1 from record 3 on),
`u16` facing at +10, a type at +14, more per type. Records 1 and 2 are
the two **starts** (Ocean Platform: z -9,345 facing 0 and z 9,919 facing
2048; Green Silo: x 5,000 and -5,000 facing each other at -662). Record 0
is another point (intro camera?). Later records are stage objects (types
seen 4-62 with ranges such as 4,000 / 1,000 / 20,000).

## The game's world is mirrored

The game's facing is (-sin, cos) (the AC's right hand is at -x); the
engine's is (sin, cos). acr-import negates x when it writes stages; the
facing numbers stay the same.

## Import

`acr-import` writes `stages/<NN>/` for every stage 20-40:

- `stage.toml`: name, `start_1` / `start_2` (x, y down, z, facing),
  the area boxes;
- `visual.obj` + `visual.mtl` + `texture-NN.png`: chunk 1's textured
  models placed by the areas, a PNG per page and palette used (256 x 256,
  palette = the CLUT, 0x0000 see-through);
- `simple.obj` + `simple.mtl`: chunk 4's flat green models;
- `collision.obj`: chunk 7's polygons placed the same way.

Both OBJ files are in the engine's world with y up (x = -game x,
y = -game y). Open them in Blender to look around.

## In the engine (2026-11-19)

- `src/gen1/acr_gen1_stage.{h,c}`: the collision (integer), floor and
  ceiling queries, wall push for an AC's cylinder, shot segments.
- Movement (`acr_gen1_sim.c`, `def->stage`): walls push the AC out
  sideways; on the ground it steps up to 120, follows a floor dropping by
  up to 120 and falls past that; it lands on the stage's floor and stops
  rising at a ceiling. **Stand-ins** until the game's rules are read.
- Shots and missiles stop at the stage (`battle.stage`).
- AC TEST loads Green Silo when the import has it (F7 / Back+Left: the
  open test floor), drawn from the textured models (an import from before
  the textures falls back to the flat green ones). Lit by a fixed light
  until the stage lights are read.
- The arch feet on the four diagonals are solid (user: in the game they
  can't be passed through): chunk 7's model 0 has them as leaning walls
  from the floor to about 2,500 up, inner face about 6,350 from the
  middle; a walk out along a diagonal stops at 6,228. Higher up, the
  arches have no polygons of their own (the user isn't sure about the
  game); the dome shell is the next thing a flying AC meets.
- Tests: `tests/test_gen1_stage.c` (a made-up stage; with
  `ACR_STAGE_DIR=<import>/stages` also Green Silo: the starts stand at
  -662, a walk out stops at the dome, a rise stops at the roof, and two
  computer pilots fight two minutes without leaving the dome).

## Next

1. The game's own wall / floor response (`0x80074FA4` sweep, AC +0x2C /
   +0x30, the step at `0x80054124`), and out-of-area (the 60 model-8 boxes,
   player block +0x08).
2. The other stages: their textured models already import (chunk 1 +
   RTIM); check them in the engine (Lunar Base first) and read the data
   after chunk 13. Page 448's texture.
3. Stage lights (`0x801A8D80` light sets by area +0x30 byte 0) and fog.
4. Chunks 6, 9-12 and the stage objects in chunk 13.
