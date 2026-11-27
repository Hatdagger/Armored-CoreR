# Gen 2 (Armored Core 2) AC models

First notes, 2026-11-27: enough to put one complete AC2 AC in AC TEST.
Code: `tools/acr_import/acr_models_ac2.c` (import), `src/platform/sdl3/gen2_ac.c`
(drawing). Disc: Armored Core 2 USA (SLUS-200.14).

## AC2DATA.BIN

- First 32 KB: `u32 sector, u32 sectors` pairs; the pair's index is the file
  id. Files are not compressed.
- A part file holds its model header at 0, or at 0x8000 behind 32 KB of other
  data (texture pages, not decoded yet).
- A model can run past the table's end for its file (legs 381: its last two
  meshes are in the first 0x24B0 bytes of 382's slot). The import reads each
  file with 32 KB more after it and stops at the header's size.

## Part model

- Header: `u32 mesh count, u32 bytes, 0xFFFFFFFF, 0`.
- Per mesh 16 bytes: `u32, u32 chain start, u16, u16, u32 0`. Mesh m's DMA
  chain runs from its start to mesh m + 1's (the last to the header's size).
- Per mesh a 0x50 block: joint position (3 floats), rotation (3 floats),
  scale, counts/offsets, -1 (likely the parent; all -1 so far).
- Geometry: VIF UNPACK V4-32 (0x6C) blocks. Each is a GIF tag (NLOOP =
  vertices, triangle strip) and per vertex 4 qwords: position, normal, UV,
  colour (128s).
- Vertex = joint position + vertex. **Parts are stored already assembled**:
  the joint positions place every part where it goes on a standard AC, so a
  core, head, arms, legs and booster drawn together make the AC without a
  skeleton. Hand weapons (and some heads) sit at 0 and go on the hand (the
  arms' last joint).
- AC2 space: y down, the AC faces -z. Gen 1 model space (what the import
  writes): y and z negated, so x stays and the AC faces +z.
- The joint positions are the pivots: legs are hip, then per side thigh,
  shin, foot (+ a piece); arms per side shoulder, upper arm, forearm, hand.
  Walking and aiming can be built on them later.

## Ids (checked by drawing them)

| Ids | What |
|---|---|
| 301-306 | cores (+ 4 joint pieces each: shoulders, back mounts) |
| 310-325 | boosters (a pair on the back, x +-0.88) |
| 332-342, 351 | heads (some at 0: put on the core) |
| 352-373 | arms (8 meshes: 4 a side) |
| 381-411 | legs |
| 501-525 | arm weapons (at 0) |

Low-detail copies follow at +600 (951 = 351's). Not placed yet: 421-492,
531-555, 561-589, 601-828 (back units and the rest).

## The test AC

core 301, head 332, arms 352, legs 390, booster 310, arm weapon 501 on the
right hand. `<import>/gen2/ac2/test-ac/` (`ac.obj`, `ac.mtl`, `build.toml`);
every part on its own in `<import>/gen2/ac2/parts/<kind>/NNNN.obj`.

## Size against Gen 1

Not settled. What we have:

- Gen 1: an AC is 212 world units tall (body height, also the camera's) =
  848 model units; legs piece centre at 462.
- AC2: the test AC is 10.16 AC2 units sole to top, hip joint at 7.2.
- AC2's legs are longer for the body: matching the hip (x64) makes Gen 2 ACs
  a quarter shorter than Gen 1, matching the height (x83.5) puts Gen 2 hips
  higher.

Default: **the same height (x83.5, `ACR_AC2_SCALE`)**. Fan-calculated sizes
are not used. To settle it from the games themselves: each game's speed is
shown in km/h, so km/h against world units per tick in both programs (Gen 1:
`docs/research/gen1-movement.md`; AC2: needs SLUS_200.14) gives metres per
unit in each, and the AC heights in metres follow. AC2's own body height
(its collision box) would also do.

## Not done yet

- Textures (UVs are there; texture pages in the first 32 KB not decoded).
- Animation (rigid model; the joints are known).
- Gen 2 stats and handling: the opponent runs on its Gen 1 build.
- Back units, the low-detail copies, Another Age.
