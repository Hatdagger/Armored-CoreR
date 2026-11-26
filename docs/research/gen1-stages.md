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
aren't known yet. Stages 0-19 are the mission maps: see "Mission maps".)

## FDAT 5s+4: chunks

A run of `[u32 size][data]` chunks, read in this order:

| # | Green Silo size | Loader call | What |
| --- | --- | --- | --- |
| 0 | 0x50 | `0x8005A9A0(0x48)` | model set: u16 count, then entries `[u32 size][model]` (size < 5 = empty), registered at model slot 0x48 + i (`0x801A686C`); same mesh format as AC parts |
| 1 | 0xC778 | `0x8005A9A0(0x104)` | **the stage's textured models**: model set, entry k = the textured version of model k (TMD at entry +0xC; some have two objects, both drawn) |
| 2 | 0x8 | `0x8005A9A0(0x80)` | model set |
| 3 | 0xCCC | `0x8005A9A0(0xA0)` | model set |
| 4 | 0x3A6C | `0x8005E9F0(0, ...)` | **the stage's area models**: one PlayStation TMD (id 0x41), one object per model number |
| 5 | 0 | stored at `0x801F0E30` | ? (missions: **the cutscenes**, pointer at `0x801F8E90`; see "Mission script") |
| 6 | 0xC3C0 | `0x80052DF8` | ? (starts `1, 0xE20, "pBAV"`: sound bank?) |
| 7 | 0x3688 | copied to `0x801B2018`, then `0x80074154` | **collision**, per model number |
| 8 | 0x863C | `0x800783B0` | **area records** (512 x 52 bytes, 64 in RAM at `0x801C2018`), then neighbour lists |
| 9 | 0x34 | `0x80079908` | ? |
| 10 | 0x4000 | `0x80079798` | ? |
| 11 | 0x3800 | `0x8005DB5C` | ? |
| 12 | 0x4 | | ? (missions: per object handler a 36-byte header and **its motion set**, the AC format; see "Mission script") |
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
quarter).

The low 3 bits of the corner words are flags (the vertex offset is bits
3-14): **corner 0 = 1** asks for the inside-the-outline test (without it
only the plane and the polygon's box count); **corner 1 = the class**,
skipped when it shares a bit with the body's mask (an AC's AC +0x43 is 5,
so class 1 and 4 polygons let ACs through: Ocean Platform has 85 class 1
polygons; Green Silo none); corner 3 = a priority (x 64 off the score
below; triangles' corner 3 is the filler 0x7FFC, priority 4 on every
polygon). Flags bits 2-3 = the main axis (0 x, 1 y, 2 z), bits 4-9 = which
corners are the polygon box's minimum on x, y, z, bits 10-15 its maximum.
The normal points **into** the surface the way a body moves into it
(floors: +y, y being down).

## Walls and floors (read 2026-11-19, VS engine FDAT 209)

How an AC moves through a stage. In the engine since 2026-11-19
(`acr_gen1_stage_try` / `_move` / `_floor_probe`, `acr_gen1_sim.c`
`game_stage_tick`); acr-import writes the game's own data for it as
`stages/<NN>/collision.txt` (**needs a disc re-import**; without it the
older stand-in rules run).

**The body is a box**, not a cylinder: x and z AC +0x28 (100) either side,
y from the feet up AC +0x2A (212); square to the world axes whatever the
AC's facing. (The 75-a-tick step limit is three quarters of +0x28.)

**A movement handler** (walking `0x8007B60C`, and the others that load
their step through `0x80054124`):
1. On the ground the step also leans **down by a quarter of its length**
   (sqrt(x^2 + z^2) / 4), so the AC follows slopes and steps down.
2. A first try (`0x80078AB0`) at the destination: if the stage pushes it
   and the corrected step isn't downward, the lean is dropped (the floor
   already holds the AC).
3. The move (`0x80078C4C`): up to **six tries**; each puts the box at
   position + step and, if the stage meets it, replaces the step with the
   corrected one. Still meeting the stage after six: no move at all. (AC
   against AC and stage objects come in between; the engine keeps its own
   AC push.)
4. Afterwards, where the step taken differs by 7 or more from the step
   wanted on x or z, the speed becomes the step taken; the y speed is
   always set from it (so a floor stops a fall and a ceiling a rise).

**One try** (`0x80078AB0` -> `0x800758F0` -> `0x80073EB0`): the box goes
into each overlapping area's model space ((p - centre) x 4, turned by
-quarters, `0x80074210`) with the move x 4. For each polygon
(`0x80073A6C` -> `0x8007386C` -> `0x800734F8`):
- skipped if normal . move < 0 (it only stops bodies moving into it);
- the polygon's box must meet the body's (ends included, `0x800739D8`);
- the four box edges along the polygon's main axis, in the order
  (a min, b min), (a max, b min), (a min, b max), (a max, b max) (a, b the
  other axes: y, z for x; x, z for y; x, y for z): where the edge's line
  meets the plane (integer division) must lie within the box along the
  main axis, and, with the corner 0 flag, inside the outline (GTE NCLIP
  signs, either winding, `0x800732BC`). The first such edge is the hit;
- the corner pushed is that edge's end the normal points to (the far end
  along the main axis; jump table at `0x80051084`, `0x80073CD8`), and the
  correction (`0x80073C10`) is
  `move + n x (((-(n . corner + d)) >> 10) - 16) >> 14`: the corner put a
  quarter unit (4 model units) short of the plane, the move kept along the
  surface, so the AC slides.
The hit kept (`0x8007257C`) is the smallest |x| + |y| + |z| of the
corrected move minus priority x 64, among polygons whose class the body
doesn't pass through. Turned back to the world and divided by 4 rounding
away from zero, it is the new step.

**The floor under the AC** (`0x8007C964` -> `0x800792BC`, written to AC
+0x2C; AC +0x30 = that floor's area): a box AC +0x72 >> 1 (37) either
side, from the top of the AC to 4,096 below the feet; only polygons with
normal y > 0; the four corner columns (`0x80075DD8` -> `0x80075B58`) give
plane heights inside the box (and the outline, with the flag); world y =
centre + model y >> 2; the highest wins, one above the feet counts as at
the feet; none = 32,767. (AC +0x2E is the same for class 1 polygons, the
lowest: water?)

**On the ground** (`0x80083960` / `0x80085DF4`): the AC stays standing
while AC +0x2C - y < 50. Otherwise, unless a box 50 lower would meet the
stage (`0x80079230`), it falls: air state 0x4B, y speed 256, sideways speed
0 (kept during a ground boost, class 0xB0). Engine: touchdown is the fall
stopped by a floor (the game's own touchdown code not read yet).

Not ported: the slice bit tables (only a shortcut), `0x80074530`'s area
list (the engine tests every area whose box meets the body), polygon
classes 2-3 on corner 0 (`0x80078E80`: a callback, perhaps triggers),
moving stage objects.

Checked: `tests/test_gen1_stage.c` (a made-up stage: a step into a wall
comes back 100 short of it, walking stops at the wall, a class 1 wall lets
the AC through, walking off an edge falls straight down and lands on the
floor below; with `ACR_STAGE_DIR` Green Silo and Ocean Platform starts,
walks and two-minute computer fights).

## Out of area (2026-11-19)

The rule (recomp, VS on the default stage): stage objects, one per box,
test each AC every tick and set the box's bit in its player block +0x08
while it is inside (ends included). Box 0 is the whole level, boxes 1 and
2 overlap to make the arena floor. Outside box 1 only (bits 5): a warning;
outside both (bits 1): that side loses.

In the arena engine the boxes are stage-code objects too. Ocean Platform's
arena code (FDAT 197, loaded at `0x8009EFE8`) has the box handler at
`0x8009F490`: the box is the object's centre (+0 / +2 / +4) plus signed
bytes x 256 (min +0x22..+0x24, max +0x25..+0x27), bit +0x28, tested for
both player blocks (`0x80045198` and +0x3A0) by `0x8009F34C`; bits 1 and 2
also copy the box to the player block (+0x21E / +0x22E, for the map?).

**Where the boxes come from (read 2026-11-25).** The arena engine's stage
loader (FDAT 210, `0x80056338`-`0x800564A8`) hands **chunk 10** to
`0x80079798`: 128 stage objects of 32 bytes (s16 x, y, z; s16 kind at +6,
-1 = none; bytes +0x10..+0x1F go to the object's +0x22..+0x31 in RAM, 52
bytes an object from `0x801D6F40`). A kind k calls the stage code's word
40 + k (table at the code's start, `0x8009EFE8` + 0xA0 + k x 4). The box
creator starts a task running the handler. (Chunk 13's points are not
objects: the arena engine copies only records 1 and 2, the starts,
`0x80079E08`.) The handler works in 16 bits: centre + byte x 256 is
stored as a halfword, so Ocean Platform's box 0 (z -768, max byte -126)
wraps round to 32,512. The end check (`0x800528E4`) is player block
+0x08 & 7 == 1: inside box 0 and outside boxes 1 and 2. Outside box 0 as
well is not out of area.

| Stage | Code | Kind | Box handler | Box 0 (engine x) | Box 1 | Box 2 |
|---|---|---|---|---|---|---|
| Garden Plaza (25) | FDAT 127 | 2 | `0x8009F364` | x -31,386..31,846, y -32,017..2,799, z -31,488..32,000 | x -11,196..11,332, y -10,001..1,775, z -11,264..11,264 | x -13,005..13,107, y -16,657..2,287, z -13,056..13,056 |
| Green Silo (27) | FDAT 137 | - | none | - | - | - |
| Night City (36) | FDAT 182 | 2 | `0x8009F364` | x -29,684..30,988, y -30,720..0, z -29,479..30,425 | x -12,384..12,704, y -20,480..0, z -8,706..11,262 | x -17,291..17,781, y -29,696..0, z -13,854..16,354 |
| Ocean Platform (39) | FDAT 197 | 3 | `0x8009F490` | x -31,744..32,256, y -32,000..0, z -32,256..32,512 | x -13,696..13,952, y -10,496..0, z -13,568..14,080 | x -16,000..16,512, y -16,384..0, z -16,448..16,320 |

acr-import finds the handler by its code (the six box bytes and the bit
read from the object, then the player blocks) and writes the boxes to
`stage.toml` as `out_of_area` (engine axes). The engine loads them into
`acr_gen1_stage_bound`; `acr_gen1_battle_out_of_area` gives 0 / 1 warning
/ 2 out, and the round result counts out as down (both out: a draw).
AC TEST's F8 / Back + Right swaps in the old **test boundary** (box 1
1,500 past the starts, box 2 500 wider on x and narrower on z) and back.
`tests/test_gen1_stage.c` walks an AC out of a shrunk boundary and, with
`ACR_STAGE_DIR`, checks the real boxes: both starts inside; a walk
straight out ends at a wall on all three (Garden Plaza inside box 2, Ocean
Platform inside box 2 with the warning, Night City inside box 1), so the
area is left by flying over the edge.

## Stage lights (read and applied 2026-11-25)

- **Light sets:** 32 records of 44 bytes in **FDAT 205 chunk 7** (common
  to every stage), copied to `0x800B51C8` by the loader and expanded by
  `0x8005D9E8` into 104-byte sets at `0x801ACDF0`: light matrix +0x00
  (three light directions, one per row, 4096 = 1), the same matrix turned
  a quarter, half and three quarters (+0x14 / +0x28 / +0x3C, `0x8005DAE4`
  calling `0x800139F8`: each row (x, y, z) becomes (-z, y, x), (-x, y, -z),
  (z, y, -x)), colour matrix +0x50 (file +0x14), back colour +0x62 (file
  +0x26: r, g, b), fog value +0x66 (file +0x2A; when not 0:
  `SetFogNearFar(v / 16, v / 4, 160)`, far colour black).
- **Which set:** byte 0 of the area record's word +0x28 in the file
  (+0x30 in RAM, copied at `0x800784E0`). Only the first 512 records are
  areas (the bytes after them aren't). Garden Plaza and Ocean Platform: set
  0. Green Silo: set 0, with the four middle areas (model 12, the floor
  under the dome) on set 10 (2 more records carry 10 too). Night City: set 1 on most areas, set 0 on 87.
- Set 0: one light straight down plus two side lights, colour matrix 1.0,
  back colour 255, no fog. Set 1: the same three directions, colour 0.73,
  back colour 200, fog 20,000. Set 10: the light from above at 0.49, the two
  side lights at 0.34 / 0.49 / 0.34 (green), back colour 100, fog 30,000.
- **Setting the set:** the area draw `0x80063844` (FDAT 210) calls
  `SetColorMatrix(set + 0x50)` (`0x80029B84`), `SetBackColor(r, g, b)`
  (`0x80029CE4`: each << 4 into RBK/GBK/BBK) and
  `SetLightMatrix(set + k * 0x14)` (`0x80029B54`) with k = (-quarter) & 3,
  the area's quarter turns (RAM +0x16, already reduced to 0-3). The
  models' normals stay in model space; the turned light matrix makes up
  for the area's turn.
- **How it's drawn:** the stage draw is `0x8006E77C` (or `0x8006F57C`),
  not `0x8006CAEC`: that one is the object draw (ACs and other models,
  `0x8006C978`, records of 0x7C bytes), which does use `NCS` / `NCDS` /
  `DCPL`, the per-corner lamp table at `0x800A40A8` and fog. The stage
  draw switches on the TMD mode & 0xFC: 0x24 (textured triangle), 0x2C
  (textured quad), 0x6C (quad; bit 6 is dropped from the GPU code). Every
  face is lit once with **`NCCS`** (sf, lm): its normal (the word after
  the texture words) and the colour 0x404040 (`0x1F800390`). So per
  channel: IR = lim(LLM x N >> 12), then lim((BK << 4 << 12 + LCM x IR)
  >> 12), and the colour = (0x40 << 4) x IR >> 12 >> 4, 0-255, where 128
  is the texel as it is (the GPU doubles). The GPU code is the TMD mode
  itself, so modes with bit 0 (0x25 / 0x2D) draw the texture unlit, and
  bit 1 is semi-transparency. No depth cue in this draw, so the fog value
  doesn't touch the stage (only objects). Faces near the camera (+7 of a
  corner) take another path (`0x8006EB58`, not read); a corner with +0xB set takes its own
  colour word (`0x8006E744`) — not seen on the arena stages' floors and
  walls; not applied.
- **Applied:** acr-import (`acr_stages_moa.c`, `acr_moa_light_face`) works
  out every face's colour with the same integer maths and writes it on
  the face's own points in `visual.obj` (`v x y z r g b`, colour x 255 =
  the PS1 value). The engine's stage draw uses it instead of the fixed
  light (older imports and custom maps keep the fixed light). No fog.
- **Not read:** what the object draw's per-corner lamps (`0x80063FFC`
  builds 4 x 16 colours from a per-object list) light, and the far-level
  draw's colour (we draw every area at full detail).

## The sky (chunk 3, read 2026-11-19)

Chunk 3 is a model set loaded at model slot 0xA0; the arena engine's
stage-object routine draws slot 0xA0 + the object's +6 (`0x800932B4`).
Green Silo's entry 1 is a textured, unlit sphere of radius 4,096 around
its origin (42 points, 80 triangles, flag 1, mode 0xA4): a **starfield**
on page (640, 0), palette row 485 (RTIM 27's third picture; the faces use
its top-left 128 x 128). Its faces are the unlit layout (a colour where
the lit one has a normal: six words, the lit triangle is five);
acr-import reads that layout for triangles by their length.

Ocean Platform's chunk 3 has no sphere: entry 0 is a small object (24
points, up at y -1,000..-2,261, page (512, 0)), entry 1 a flat plane of
104 points, 47,000 model units (11,750 world units) across at y 0 (page
(320, 0), 4-bit, palette row 507): the sea. Garden Plaza's entry 0 is
the same kind of plane with grass. Both are mode 0x2E (see-through). How
the game places them is not read. acr-import writes them as `far.obj`
(lit like an area on light set 0); the engine tiles them out to the
horizon just under the stage (ARENA.md, "Backgrounds"). Night City's
chunk 3 is empty (its background is black in the game). Ocean's small
object isn't drawn.

acr-import writes spheres around their origin as `sky.obj` / `sky.mtl` /
`sky-NN.png` (only Green Silo has one). The engine draws them around the
eye, before the stage, without depth or fog (`acr_render_sky`). Not read
yet: which object draws the sky in the game, and whether it moves with
the camera (a sphere of 1,024 world units around the arena's middle would
sit inside the dome, so it must).

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
  the area boxes, and `out_of_area` (the stage's own out-of-area boxes,
  imports from 2026-11-25 on; none on Green Silo);
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
  rising at a ceiling. **Stand-ins**, now only for an import without
  `collision.txt`; the game's rules replace them ("Walls and floors").
- Shots and missiles stop at the stage (`battle.stage`).
- AC TEST loads Green Silo when the import has it (F7 / Back+Left: the
  open test floor), drawn from the textured models (an import from before
  the textures falls back to the flat green ones). Lit by the stage lights
  (2026-11-25; an older import: a fixed light).
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

## Mission maps (stages 0-19, read 2026-11-20)

Stages 0-19 are Master of Arena's mission maps: the same file layout as the
arenas (FDAT `5s+0..5s+4`, chunks, area records, RTIM <stage> textures),
only bigger (14 to 366 areas, up to 87 models). acr-import now writes all of
0-40; stage.toml says `kind = "mission"` for 0-19.

- **Tiles.** Every map is built from tiles on a 2,500-unit grid (half and
  quarter tiles for trims), each placed by an area record with a quarter
  turn, and most tiles are used many times (stage 0: 65 tiles, 54 of them
  reused). Collision is kept per tile, so a new layout of the same tiles
  (new area records) brings its collision with it.
- **Starts.** The two start records (chunk 13 records 1 and 2) are the
  arena's; missions don't use them as drops. Stage 0's sit side by side,
  below the floor. MIS.T turned out to be the briefings only (see
  "Missions" below); the drops are still to find in the mission code. The engine gives maps used by the mission
  tests their own starts (`demo_stage.c`, `k_mission_starts`): 11 so far.
- **Footing** (an AC set on start 1's floor and walked 400 ticks, game
  collision): 2-5 and 7-19 hold it and stop it at walls; 0 holds it once it
  stands on the real floor (y 0).
- **Maps 1 and 6 are sea maps** (read 2026-11-21). Their ground is the sea:
  map 1's base tile (model 0) is a class 4 plane at y 25 over a class 1
  plane at y 0, map 6's outer tiles a class 1 plane at -62.5. Both classes
  share a bit with the body mask 5, so ACs pass through: that is the water,
  not a floor bug. Every engine sets +0x43 to 5 the same way, the mission
  engine (FDAT 207, the one with `#Location Now`) included (`0x80084D54`).
  The arena start records sit over the water, so an AC dropped there sinks.
  The solid ground is class 0: on map 1 the ships (the long one along x from
  about game x 5,600 to 10,500, deck -250, 750 wide; a second near game
  (-19,000, -7,500), deck -500 / -750), on map 6 the middle platform (deck
  -500). AC TEST's starts (`k_mission_starts`): map 1 game (6,600, -250, 0)
  and (9,000, -250, 0) on the long ship facing each other; map 6 (0, -500,
  +-2,500). Checked: both hold an idle AC; a two-minute computer fight on
  map 6 stays on the platform, on map 1 one AC walks off the ship into the
  sea. Still to read: what the mission engine does to an AC in the water
  (probably lost, as in AC1's sea missions; not read); class 4 vs class 1 (perhaps the
  surface the splash is drawn at vs the bottom).
- **Mission map 11** (a square hall, four pillars; the P4 Qualifier test):
  floor at 3,000; starts (0, 3000, -8000) facing 0 and (0, 3000, 8000) facing
  2048. Two computer pilots fight two minutes there without leaving the
  hall (`tests/test_gen1_stage.c`).

## Missions (read 2026-11-21)

`tools/research/moa_missions.py <import>/unpacked` prints all of this.

- **Mission n is played on map n.** Briefing n, map n and mission code n
  line up: 1 "Escort Submarine" on the R&G steamship route is the open-sea
  map with ships, 6 "PROGTECH Marine Laboratory" the platform in the sea,
  11 "Destroy enemy AC (Goldy)" the four-pillar hall, 14 "Hercules in 3
  minutes", and so on (19 has no briefing).
- **`GG/MS/MIS.T` (409 entries) is the briefings**, 16 entries a mission:
  +1 the mail header (sender, requester, reward), the next text entries the
  pages ("Theater of operations", enemy forces, objective; `%n@1..@0` marks
  highlighted words), 12,288-byte entries are TIM pictures (the briefing
  maps), empty entries are unused slots. 384+ hold part descriptions and
  later mail. No placements or mission rules in it.
- **Mission code: FDAT `5s+0`** (33-55 KB), loaded at **`0x800A0DD0`**
  (found from its own calls landing on function starts; checked on missions 0, 1,
  6 and 11). Header: 16 pointers; slot 0 is the per-frame mission
  logic (mission 0: radio lines through
  `0x80058B1C(0, 0, line)`), slots 1-7 small hooks, **slots 9 on are
  object handlers**. FDAT `5s+1..3` are empty for missions.
- **Objects: chunk 13 records 3 on.** Record +6 picks the handler
  (slot 8 + n); +14 a type; +16 on per-object numbers. The counts match the
  briefings: 6 "MTs x 5" has 5 objects on handler 2; 12 "MTs x 6" 6 on
  handler 1; 13 "Trucks x 3, MTs x 4" 4 + 3; 14 "Hercules" one object;
  11 "Goldy" two. Map 1's long ship is objects 7-9 (handler 6, types 1, 49,
  50) at the ship's tiles, so it is probably the escorted vessel and moves.
- Still open: which records are the player's drop (records 0-2 or the
  code), the objective / failure rules in slot 0, what record +14 and
  +16 on mean (the five MTs on map 6 carry types 12, 11, 1, 33, 4).

## Mission objects in the mission engine (read 2026-11-23)

The mission engine is FDAT 207 (loaded at `0x80050DD8`, as the others).
Its stage loader (`0x80055850`-`0x800559F4`) reads the chunks in the
arena's order; chunk 12 goes to `0x800795A8`, chunk 13 to `0x80079774`.

- **Record table:** `0x80079774` copies chunk 13's first 64 records into
  44-byte entries at `0x801E6748`: byte 0 a state (1 = object made), byte
  1 the object's number, +2 a working copy of record +22, +4 the 40-byte
  record. A small bytecode script (interpreter around `0x80091D88`,
  opcodes `0x8000`, `0x8001`, `0x8004`, `0x8005`...) refers to entries by
  number: the mission's script, not read yet.
- **Objects** are 376 bytes, the AC object's size, in a block at
  `0x801E7248`; object 0 is the player's AC. `0x8007EB00(object, entry)`
  builds one: +0 = the entry, +4 = the chunk 12 entry of its handler,
  **+360 / +362 / +364 = record +22** (+360 is where an AC keeps its AP),
  +0x72 and +0x28 = the chunk 12 entry's first word (the height missiles
  aim at, for ACs).
- **AP = record +22** (map 6: the MTs 5,000, the big MT 18,000). The hit
  routine `0x8007AF00` takes damage off +360 and stops at 0. For a unit
  (not object 0) the damage is `0x8007AEA4`: a solid shot takes its
  attack (shot +2); an energy shot (shot byte 0 bits 4-5 = 2) takes
  attack x (8 - n) / 8 + 1, n = bits 8-11 of record +16 (a signed
  nibble). Map 6: the five MTs n = 2, the big MT 3, the tank AC 0. The
  player's AC goes through `0x8007AE34` instead, which is the VS engine's
  hit (`0x80079500`, `acr_gen1_hit_ap`): attack x the AC's shell or energy
  factor / 64 x 2 / 3 (corrected 2026-11-24; it was noted as "x 2"). So a
  unit's shot hurts an AC exactly as an AC gun of the same attack, and a
  unit's AP counts in raw attack points: a rifle (218) takes 218 off an MT,
  about 61 off the default AC.
- **Handlers** (mission code slots 9 on) only set an object up: map 6's
  MT handler (`0x800A3E48`) sets +0x36 = 2, four routines at +0x58
  (behaviour, called each frame with the state), +0x5C (start), +0x60,
  +0x64, +364 x 4, +0x4C = a random number (`0x80079508` = the boot
  program's rand), then runs the start routine and the behaviour once.
  The tank AC's (`0x800A3F24`) plays its animation 2 (`0x80054AC8`,
  object +108 = the animation, as for ACs).
- **Behaviour:** the MT's (`0x800A3430`) is a 12-state machine on object
  +69; each state plays a few of the unit's animations through
  `0x800A32AC(object, a, b, c, next)`. 7000 appears as a range there.
  Movement, aiming and firing: still to read (calls into the engine at
  `0x8007C104`, `0x8007C134`, `0x8007C20C`, `0x8007E238`, `0x8007D50C`,
  `0x80079828`).

## Mission unit behaviour (read 2026-11-24)

Map 6's mission code (FDAT 030 at `0x800A0DD0`), slots 9-12: the big MT
`0x800A2F3C`, the MT `0x800A3E48`, the tank AC `0x800A3F24`, the
model-less object `0x800A3F78` (only sets +0x36 = 1, +0x32 = 8). A handler
fills four routines: **+0x58 behaviour** (called with the state, +0x4A),
**+0x5C events** (`0x800A3A9C` for the MT), **+0x60 each tick** (gun and
death, `0x800A38B4`), **+0x64 destroyed**. The big MT's routines
(`0x800A2254`, `0x800A2B90`, `0x800A2784`, `0x800A2DB4`) have the same
decision code with its own tables, so it is the missions' common MT.

- **Body box (chunk 12).** u16 count, then `[u32 size][36-byte header][data]`;
  entry k belongs to handler k + 1 (`0x800795A8` fills 44-byte entries from
  `0x801E6614`, `0x8007EB00` indexes them by record +6). The object takes
  data halfwords 2 and 3, each / 4, into +0x28 / +0x2A (and +0x72 / +0x74),
  the fields an AC keeps its stage box in: **half width and height**. Map
  6: the MT 125 x 175, the big MT 200 x 340, the tank AC 112 x 150; the ACs
  on the other maps 100 x 225 or 75 x 212, the ships 750 x 375. acr-import
  writes them as `box` in objects.toml, with `ap` (+22), `attack` (+24) and
  `energy_n` (+16 bits 8-11).
- **Target** (`0x8007931C`): among the first 16 objects of another team
  (+0x36 & 7), the nearest, measured as sqrt((dx/2)^2 + (dz/2)^2) x 4, i.e.
  **2 x the flat distance**; it spreads picks over targets fewer of its side
  already chase. Kept at +0x50.
- **Decisions** (event routine, events 0, 3 and 4 while +0x4B = 1): measure >
  2,500 (1,250 away) -> state 1; < 1,000 (500) -> state 6; else rand(80)
  through a 36-entry table: 1 x6, 2-8 x2 each, 9 and 10 x4 each, the rest
  (44 of 80) nothing. The state is set with `0x8007B598`, which also keeps
  the target and a random offset (+-50 in x and z) at +0xF8..+0x103.
  Events 1-2: a third of the time state 1 or 6 at even odds. **Event 5**
  (sent by the hit routine `0x8007AF00` and the wall code) swaps the state
  for its pair: 1-6, 2-8, 3-7, 4-5, so the pairs are opposite steps.
- **Behaviour** (`0x800A3430`, jump table `0x800A1100`): states 1-8 call
  `0x800A32AC` with a walk, a start and two linking animations:
  state 1 walk 2 / start 1 (links 3, 5), 2: 4 / 3 (1, 5), 3: 6 / 5 (1, 3),
  4: 9 / 8, 5: 11 / 10 (no links), 6: 13 / 12 (14, 16), 7: 15 / 14
  (12, 16), 8: 17 / 16 (12, 14). (Read 2026-11-24; the order noted before,
  13/15/17 for states 4-6, was the code's order, not the states'.) In
  `0x800A32AC`: already in any walk (2, 4, 6, 9, 11, 13, 15, 17, table
  `0x800A10C0`) it plays the new walk straight away; in the start or a link
  it waits for that to end (`0x80054C04`), then plays the walk; otherwise,
  if `0x800A3194` agrees (not read), the start. 9 and 10 turn
  (`0x800A30F4` / `0x800A3144`: the turn speed +0xD4 grows by 8 a tick, at
  most 24, added to the facing); 0 idles (animation 7 or 18 via
  `0x8007C9B8`); 11 (`0x8007D50C`) not read.
- **Walking speed** = the animations' root tracks (`0x80055138` moves the
  object along the animation each frame; chunk 12's motion sets, below).
  The tracks' +x is the unit's left, so the states walk: 1 ahead, 2 ahead
  left, 3 ahead right, 4 left, 5 right, 6 back, 7 back left, 8 back right,
  and the hit pairs (1-6, 2-8, 3-7, 4-5) are opposites. Map 6's MT (handler
  2): starts 8 frames (168 / 4 ahead), walks 24 frames: ahead 646, sideways
  646, diagonals 446 x 445, back 592, back diagonals 446 x 446 (1/4 units),
  i.e. 161.5 units a loop, about 6.7 a tick. The big MT (handler 1): walk 2
  is 2,075 in 24 frames, 12-frame starts. acr-import writes them as
  `stages/MM/motion-HH.toml` (HH = handler, the AC format); the engine walks
  by them (acr_gen1_battle.c).
- **Gun** (`0x800A38B4` / `0x800A3714`): `0x80079828` gives the angles to
  the target for a gun at joint 7 with range 7,000, +-256 up / down and
  +-1,024 to the sides; the gun's angles (+0x7E / +0x80) turn 16 a tick
  (`0x800136E0`), on aim once a tick's turn reaches the target. On aim with
  no burst: 2,622 in 32,768 a tick a burst starts (328 of those without the
  line-of-sight test `0x8007DEA4`); the burst counter +0x47 is 6, a shot on
  each odd tick (3), the two muzzles in turn (+0x110 ^ 2), each with a
  muzzle light (+0x150 = 15, colour 180, 160, 140). The shot
  (`0x8007D774`) takes **attack = record +24** (map 6: MT 200, big MT 400,
  tank AC 10) and the shot table at `0x800AD13C`: speed 700, range 7,000
  (the same as the rifle's). +0x48 is a wait that counts down first.
- **Destroyed**: +0x49 leaves 255, `0x8007B0D8` plays the explosion, then
  `0x8007EE04` removes the object.
- **Big MT (U19):** placed 1,000 under the deck; nothing in its handler
  moves it: slot 0 brings it up with cutscene 3 (see "Mission script").

**In the engine** (2026-11-24): `acr_gen1_battle` has up to 64 units beside
the fighters (`acr_gen1_unit`, target number `ACR_GEN1_UNIT(u)`), stepped
with the fight so netplay stays in step (`acr_gen1_match` hashes them). They
can be locked (guns, missiles, the camera swing), shots and missiles hit
their box, damage follows the rule above, and a destroyed unit blows up and
is gone after 20 ticks. Brains: **MT** (10 or more animations) as above;
**turret** (armed, fewer animations) turns in place and fires the same way;
none for the unarmed (trucks, bunkers). Stand-ins: a step lasts 24 ticks,
the MT walks 16 a tick ahead (11 sideways or back) and turns to face its
target 24 a tick while walking; a step into a wall or off its floor is
turned around as a hit is. AC TEST joins
every unit of a mission map that has a model, AP and a floor under it
(map 6: 6 of 7; the big MT waits under the sea) as team 1, with the target
AC; the HUD shows the locked unit's AP, its marker and every unit on the
radar. `tests/test_gen1_units.c` covers lock, damage, destruction, both
brains and a repeat run.

Checked in AC TEST on map 6: the five MTs and the tank AC fire bursts, a
default AC standing still loses about 500 AP in 6 seconds and is down in
about 20 (the MTs aim at where it is, without leading, so moving spoils
their aim); a rifle shot takes 218 off an MT (5,000 -> 4,782 on the HUD).

**Blade, bodies and the mission win** (2026-11-25; ours, the game's own
code for these is still to be read):
- Blade: the swing's seek (3,000 ahead, +-45 degrees) and both arc
  crossings take units as well as ACs. A unit is reached at its near side
  (its middle less its half width, past an AC's radius of 75, so an
  AC-sized unit reads as an AC); a hit goes through the unit damage rule
  (the blade is energy: 738 -> 554 on map 6's MT), turns the MT's step as
  any hit does and ends the lunge, without knockback. The lunge also stops
  at a unit's body (half width + 75 ahead).
- Bodies: a unit's body is a square of its half width either side of its
  middle, turned with it, its height tall; an AC (hit cylinder 75 / 212)
  overlapping it is pushed straight out after the units move, so an MT
  walking into an AC shoves it. ACs still pass through each other (only
  the blade's lunge stops at them), as before.
- Result: a live unit in the fight keeps its side standing, so a mission
  map is won when the last enemy target, AC or unit, is down; a side made
  of units alone counts too. Dormant units (map 6's big MT under the sea)
  don't count until the script raises them. Time-up AP compares the ACs
  only. AC TEST on a mission map now ends this way (no clock): WIN once
  everything is down, LOSE when the player is.
- Tests: blade hit / miss on a unit, the lunge stopping at it, push-out
  (side, corner, clear, above), and the results.

**Record 0 is the player's drop** (2026-11-23): on 15 of the 20 maps it
stands exactly on the collision floor, facing along the map (map 2 in the
hangar facing the bay, map 8 in the tunnel, map 12 down the street); on
maps 3 and 5 it is above the floor (a drop from the air: map 3 at -10,500
over -5,000, map 5 at -3,840 over 1,525); on map 1 it is in the sea (the
submarine escort: the player may start aboard something); on map 13 it is
outside the map's collision. AC TEST now starts the player there on every
mission map but 1, 6, 11 (their own starts) and 13, with the target 2,500
ahead facing back.

## Mission script (map 6, read 2026-11-25)

Slot 0 of the mission code (map 6: `0x800A641C`) runs every tick. It does
nothing once the ending is requested (`0x800A0DBC` & `0x381`) or while the
player's AP (object 0 +360) is 0. Its own state is `0x800AD838` (start 0).
Record-table entries (44 bytes from `0x801E6748`, byte 0 the state: 1 made,
3 destroyed) are named by chunk 13 record number. Map 6: **4 the tank-legged
AC, 5 a model-less object beside it, 6 the big MT, 7-11 the five MTs.**

- **0:** once every entry from 4 on except 4, 5 and 6 is destroyed (or has
  no handler), stage nodes 132 and 133 (64-byte entries from `0x801CFAB0`,
  controller pointer at +56) get +6 = 0, +10 = 4: their animation 4, the
  deck opening. -> 1.
- **1:** waits for node 132's controller (+44) byte +16. Then object 5 is
  removed, **the tank AC is made** (`0x8007ECEC`, it isn't there before),
  cutscenes 1 and 2 run, the big MT gets a per-tick hook (`+0x60` =
  `0x800A5F5C`: water splashes at joints 8 and 11 while y > 0, camera shake
  and a sound near the surface) and **cutscene 3 brings it up**; radio line
  37. -> -1.
- **-1:** the tank AC destroyed (state 3, or its y past 30,000) gives radio
  123 and **event flag 18** (`0x800907D0`: a byte in the progress block; a
  bonus, the mission doesn't need it).
- **The big MT destroyed ends it.** Tank AC already down: radio 153 and
  `0x80052490(128)`, the "mission accomplished" request (the ending starts
  100 ticks later, `0x800A0DB0` +12 / +16). Tank AC still up: a 60-tick
  fade to white, the player moved, cutscene 4 (radio 153 in it), then the
  same request.

**Cutscenes** (chunk 5): u16 offsets to each; a cutscene is u16 0, u8 track
count, u8, u16 length in ticks, then u16 offsets to its tracks. A track is
u16 words: a type (0 general, 1 camera, 2 objects), then commands, `FFF0 n`
wait n ticks, `FFFF` end. `0x80093844(code, hook)` plays one while the fight
waits: code bits 0-7 the cutscene, 0x100 a per-tick hook (second argument).
Map 6 plays 0 at the start, 1 and 2 when the deck has opened, 3 for the big
MT, 4 in the second ending. Commands read so far (interpreter
`0x80091D88`): `1003 n` takes record entry n (makes its object if needed);
`8 / 9 a b c` sets the animation (`0x80054AC8`); `1001 8000 n 0` puts the
object at point n of the 52-byte table at `0x801E49D8`; `1002` a rotation;
`0 / 2 x y z` position / rotation keys (`0 8000 n 0`: toward point n; the
interpreter keeps the previous key at +8 and the new one's place in the
track at +14; the per-tick interpolation is not read); `14 ...` a sound; `100D` defines a point;
`1008 / 1009` drive stage nodes (132 and 133 in cutscene 2); `1004 n` radio.
**The points** (read 2026-11-24) are stage chunk 10: 128 entries of 32
bytes (s16 x, y, z, s16 at +6, ...), copied by `0x80078DE8` (called from
the stage loader at `0x800559A8`) into 128 x 52 bytes at `0x801E49D8`.
Map 6, game x, y down, z: **10 (300, 500, 11000), 11 (300, 500, 8000),
12 (300, -1200, 6500), 13 (300, -500, 3500)**; 5 / 6 are (100 / 800, -500,
300), 14 (300, -750, 3700). Cutscene 3 (270 ticks), the big MT's track:
wait 50; `1003 6`; a sound; put at point 10, animation 26; key to point 11,
wait 60; a sound, animation 29, key to point 12, wait 34; a sound; put at
point 13, rotation 2048, animation 27; wait 126. So it walks under water
from z 11,000 to 8,000, leaps 1,700 up and 1,500 ahead, and lands on the
deck at z 3,500 facing south.

**The deck** (nodes 132 / 133, animated instance at node +56: +6 frame,
+10 animation): cutscene 2 sets both to frame 161 (`1008 1 0x84 0xA1`,
`1008 1 0x85 0xA1`), likely the opening's last frame; not confirmed. Map
6's mission code has its own node controller (`0x800A3F8C` sets the
controller's +16 state; `0x800A3FDC` takes 0x8100 / 0x8200-2 commands from
cutscene `1009`) — not read through.

**Motion sets** (chunk 12): u16 count, then per handler `[u32 size]` a
36-byte header and a motion set in the AC format (`acr_motion_moa.c`),
pointer at entry +40 (`0x800795A8`); the object's animation state (+108)
takes it (+136). Map 6 (1/4 world units): the MT's walk (anim 2) is z -646
in 24 frames (about 161 world units, against the engine's stand-in of 16 a
tick), its diagonals 3-5; the big MT's walk z -2,075 in 24. `tools/research/
moa_unit_motion.py` prints them. Not used by the engine yet (next).

**In the engine** (`src/gen1/acr_gen1_mission.{h,c}`, part of the battle
and of netplay's hash): units keep their record number; a map with a script
names the ones that wait (map 6: the tank AC hidden, the big MT drawn under
the sea), and wins by its objective instead of "everything down" (the
player's side down still loses). Map 6 runs the steps above with the
cutscenes skipped; the deck takes 44 ticks to open (stand-in); the big MT
follows cutscene 3's points during the fight (keys taken as straight lines)
and joins at point 13 facing 2048. AC TEST logs each step; `ACR_UNITS_DOWN=1` starts with the
units in the fight destroyed (testing aid). Checked in AC TEST: the deck
opens, the tank AC joins; 2026-11-24 screenshots show the big MT leaping
over the deck edge and standing on the deck.

## Mission scripts, other maps (read 2026-11-24)

The end call is `0x80052490(128)` "accomplished" or `(256)` "failed"
(radio 153 goes with the first, 158 with the second). Failure is possible
on maps 1, 3, 12 and 14. A record "left" = its entry (44 bytes from
`0x801E6748`) has a handler (+10 != -1) and isn't destroyed (state 3),
made or not.

- **Map 2** (`0x800A39B0`): records 4-63 none left -> a counter starts
  at 1 and requests at 30 (29 ticks later). Radio 28 at tick 40, 120 / 121
  from area triggers (`0x801D0C36`, `0x801D0BF6`).
- **Map 15** (`0x800A29E0`): an intro cutscene (`0x80093844(0x800)`);
  none left -> the request at once.
- **Map 13** (`0x800A4FA0`): fewer than 8 left -> radio 93 and records
  11-14 made (the four drones, U10; list at `0x800AABEC`), once; none left
  -> as map 2. Records 4-10 trucks and MTs, 15 the jet (U36).
- **Map 12** (`0x800A4640`, "Objective: Save friendly units"): targets
  records 4-9 (list `0x800AA0D8`, handler 1 = slot 9 `0x800A263C`, team 2,
  U20); records 10-12 (list `0x800AA0F0`, handler 2 = slot 10
  `0x800A3724`, U31, **team 4**: each handler's set-up writes its team to
  object +54) are the friendly units. The player's object is team 1
  (object 0 at `0x801E7248`; its AP, +360, stops the script at 0). Read
  through 2026-11-25; its state is `0x800AA100`, each tick:
  - radio 101 at tick 20 (`0x800A0DB0` +18);
  - **0:** every target destroyed (record state 3) -> 1. Otherwise each
    friendly destroyed plays radio 102-104 once (flags `0x800AA110`); none
    left -> 2;
  - **1:** radio 153, `0x80052490(128)` accomplished -> -1;
  - **2:** a counter (`0x800AA10C`) = 20 -> -1. The counter counts down
    the same tick; at 0 radio 59 (counter -2); once the radio is done
    (`0x80058480`) radio 158 and `0x80052490(256)` **failed** (counter -3).
  **Targets.** Handler 1's think routine (`0x800A2290`) calls the common
  `0x8007931C` (objects of another team: the player and the friendlies).
  Handler 2's (`0x800A3378`) calls its own copy `0x800A2718(self, 0x7FFFFFFF)`:
  over the 16 objects of 376 bytes from `0x801E7248`, it skips empty ones,
  **team 5**, itself, and object 0 (the player) while `0x800A0DBC` has
  0x400; the rest (the targets, but also **the player and the other
  friendlies**, as read) score by half-distance x 2, and when there are 2
  or more team-5 objects the score is scaled by (1 + team-5 objects
  already aiming at it, object +80) / (team-5 objects): a spread for a
  team-5 squad. On map 12 nothing is team 5, so it is "the nearest object
  but itself". So, as read, the friendlies may aim at the player and at
  each other; whether their shots hurt them (the shot side, in the EXE)
  is not read. **To check in the game.**
- Maps 1, 3, 4, 5, 7-11, 14, 16-19: not read yet (timers in 14 "Hercules
  in 3 minutes"; 3, 12, 14 fail).

**In the engine**: maps 2, 12, 13 and 15 run these rules (records 11-14
of map 13 wait hidden); radio and cutscenes are left out. Units the engine
leaves out (not on a floor) don't count, unlike the game. Map 12
(2026-11-25): the friendlies are on the player's side (battle team 0);
every unit now aims at the nearest enemy AC **or unit**, so the targets go
for the friendlies and the friendlies for the targets (not for the player
or each other, unlike the read above: a stand-in until it is checked in
the game). Friendly units don't keep the player's side standing. The
failed ending is a result of its own (`ACR_GEN1_MISSION_FAILED`, the
battle's 2), shown as "MISSION FAILED"; radio 59's length is a stand-in
(66 ticks, about 3 s). AC TEST: `ACR_FRIENDS_DOWN=1` starts with the
friendly units destroyed (`ACR_UNITS_DOWN=1` now only the enemy ones).

## Mission units (read 2026-11-22)

`tools/research/moa_units.py <import>/unpacked` prints the roster below;
with `<stage> <slot> <out.png>` it draws one unit in its first pose.

- **Chunk 2 is the mission's unit models** (model set at slot 0x80, the
  arena loader's `0x8005A9A0(0x80)`; Green Silo's is empty): u16 count, then
  `[u32 size][entry]`, size < 5 = empty. Slot 0 and often the last are
  empty; **object handler n draws slot 0x80 + n** (map 6: handler 2's five
  objects are slot 2, the biped cannon MT; map 12 "MTs x 6" and map 13
  "MTs x 4" use the same model).
- **An entry** is an animated model like an AC: an animation block in PA
  entry 0's layout without its size word (byte 0 = animation count, byte 1
  = nodes, then the block length, 0x1C, joint table, animation list, end;
  offsets count from 4 bytes before the entry), then a u32 and `u32
  objects`, and the AC part mesh table: objects = pieces x LODs, LOD 0
  first, pieces = the joint table's slots. Joint table slot k draws piece
  k. Corrected 2026-11-23: the u32 is not the LOD count (1-3; U02 says 1
  and has 3 LODs); the jets (U09, U17, U36) and the big cannon (U40) have
  2 LODs, so the first pictures showed only 4 of the jets' 7 pieces. ACs have 57 animations and 23 jointed pieces (more for
  quads); MTs 2-30 animations; turrets, vehicles and buildings 1-4.
- The same unit (same bytes) is reused across maps: 43 different units
  in maps 0-19.

Roster (handler: objects x unit; names are by eye from the pictures):

| Map | Briefing | Units |
| --- | --- | --- |
| 0 | Capture fugitive | 1 box vehicle (U00), 30 tracked gun MTs (U01), 1 AC (U02) |
| 1 | Escort submarine | 3 helicopters (U03), 2 gun turrets (U04), box structures (U05, U07), 2 dome turrets (U06), 3 model-less objects (the moving ship) |
| 2 | Destroy all combat MTs | 31 spider MTs (U08) |
| 3 | Destroy the cannon | 3 jet fighters (U09), 8 small drones (U10), 1 reverse-legged AC (U11) |
| 4 | Destroy all enemy forces | 5 small hover guns (U12), 1 AC (U13) |
| 5 | Gun emplacements x 9 | 5 + 4 gun emplacements (U14, U15), 8 tanks (U16), 1 jet (U17), 1 AC (U18) |
| 6 | MTs x 5 | 1 big humanoid MT (U19), 5 biped cannon MTs (U20), 1 tank-legged AC (U21), 1 model-less |
| 7 | Destroy the armoured car | 1 helicopter (U22), 2 spiked armoured cars (U23), 1 box vehicle (U00), 5 humanoid MTs (U24), 1 quad AC (U25) |
| 8 | AC x 1, MTs | 10 heavy humanoid MTs (U26), 6 cannon turrets (U27), 1 AC (U28), 1 model-less |
| 9 | Recover secret material | 10 tracked gun MTs (U01), 15 light humanoid MTs (U29), 1 tank-legged AC (U30) |
| 10 | Locate research team | 8 light humanoid MTs (U29), 25 cannon turrets (U27), 1 cannon MT (U31), 1 AC (U32) |
| 11 | Goldy | 2 quad ACs (U33, U34) |
| 12 | MTs x 6 | 6 cannon MTs (U20), 3 cannon MTs (U31) |
| 13 | Trucks x 3, MTs x 4 | 4 drones (U10), 3 trucks (U35), 4 cannon MTs (U20), 1 jet (U36) |
| 14 | Hercules | 1 AC (U37) |
| 15 | Tank x 1 | 4 bunkers (U38), 1 long beam (U39), 2 big cannons (U40) |
| 16 | Destroy all enemies | 6 heavy humanoid MTs (U26), 10 cannon MTs (U20), 1 AC (U02), 1 model-less |
| 17 | Defend all gates | 6 tall humanoids (U41), 1 AC (U02), 3 model-less |
| 18 | Reach the factory's core | 3 ACs (U02) |
| 19 | (no briefing) | 1 AC (U42) |

**Textures** (read 2026-11-23): every unit's pages and palettes are in
its map's RTIM entry, the same VRAM load as the stage's textured models.
Units used on several maps look the same on each (checked texel by texel
on the faces' areas).

**Import** (2026-11-23, `tools/acr_import/acr_units_moa.c`): acr-import
writes `units/UNN/` for all 43 (numbered as above): `pieceK.obj` (LOD 0, at
its node's bind position as AC parts), `skeleton.toml` and `anims.txt` (the
AC set layout; a drawing node says `draws = "unit K"`), `unit.mtl` +
`texture-NN.png`, and `unit.toml` (maps, slots, pieces, LODs). Each mission
map gets `stages/MM/objects.toml`: records 0-2 as points, then each object
with its handler, unit, skin, place (engine x), facing, type and all its
raw words. All 43 load through the engine's parsers (skeleton, animations,
MTL, OBJ). Imports from older builds get them through `acr_import_refresh`.

Record 0 of each map is a third point next to the arena's two starts
(map 6: game (-8000, -675, 10500) facing 2048); perhaps the player's drop.
On map 6 it sits on a small structure out at sea, north-east of the
platform, not on the platform itself. Not checked further.

**In the engine** (2026-11-23, `src/platform/sdl3/demo_units.c`): on any
mission map (0-19) AC TEST draws every object that has a unit, at its
record's place and facing, in its animation 0, looping. (Since 2026-11-24
they fight: see "Mission unit behaviour".) The skeleton reader takes
`draws = "unit K"` as `ACR_PART_UNIT`. F7 now cycles Green Silo, Ocean
Platform, map 11, map 6, the open floor (`ACR_STAGE=NN` starts on any map).
Checked on map 6 (screenshots and a plan from the files): the five biped
cannon MTs (U20) and the tank-legged AC (U21) stand on the deck (their
lowest points at 499 against the deck's 500), all facing 0. The big
humanoid MT (U19, record 6) is placed at game y +500, 1,000 under the deck
and 560 under the sea surface, north of the platform: probably raised by
its mission code. Maps 0, 3, 7, 11, 12 and 14 load and draw all their
objects (map 0: 32).

Screenshots without a screen (Linux): `LIBGL_ALWAYS_SOFTWARE=1
SDL_VIDEO_FORCE_EGL=1 SDL_AUDIO_DRIVER=dummy ACR_STAGE=6
ACR_SHOT=out.bmp ACR_SHOT_MS=4000 xvfb-run -a -s "-screen 0 1280x720x24"
armored-corer <import> --test` (`ACR_HOLD` holds inputs). Without
`SDL_VIDEO_FORCE_EGL` SDL gets an OpenGL ES context there and nothing is
drawn.

Read since: AP, weapons and the MT's behaviour ("Mission unit behaviour").
Still open: state 11, which code sends events 0-4 (the decision timing),
`0x800A3194`, the mission script and slot 0 (objectives, the big MT's rise).

## Next

1. Done 2026-11-19: the game's own wall / floor response (see "Walls and
   floors"). `0x80074FA4` turned out to be the line test (sight, blade,
   missiles), not the body's. Out-of-area: see "Out of area".
2. The other stages: their textured models already import (chunk 1 +
   RTIM). Ocean Platform (39, the user's pick) is in AC TEST (F7) with the
   game's collision; the others next. The data after chunk 13 is padding
   in Ocean Platform. Page 448's texture.
5. The out-of-area boxes' records (see "Out of area"); the game's own
   touchdown code; AC against stage objects.
3. Done 2026-11-25: stage lights (see "Stage lights"). Fog isn't used by
   the stage draw.
4. Chunks 6, 9-12 and the stage objects in chunk 13.
