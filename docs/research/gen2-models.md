# Gen 2 (Armored Core 2) AC models

First notes, 2026-11-27: enough to put one complete AC2 AC in AC TEST.
Code: `tools/acr_import/acr_models_ac2.c` (import), `src/platform/sdl3/gen2_ac.c`
(drawing). Disc: Armored Core 2 USA (SLUS-200.14).

## AC2DATA.BIN

- First 32 KB: `u32 sector, u32 sectors` pairs; the pair's index is the file
  id. Files are not compressed.
- **The table's offsets are 16 sectors (32 KB) early** (solved 2026-11-27,
  session 4; `gen2-archive.md`): a file really starts at table offset + 16
  and runs to the next file's start. The import checks the start against the
  file's header (part model, or a JOIN package with its category index),
  trying +16, 0, +32, -16, and otherwise uses +16, as `tools/gen2/split_bin.py`
  does. Every part file then has its model header at 0. (The first version
  read the table offset as is and found models at 0 or 0x8000, reading 32 KB
  more to catch tails that ran into the next id: no longer needed.)
- `acr-import <split folder>` reads split_bin.py's output
  (`<group>/NNNNN.bin`) instead of the disc.

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
| 501-525 | right-arm weapons (at 0) |
| 561-569 | left-arm weapons |
| 601-632 | back units (681-712 is the same set again) |
| 761-768 | extensions |

Low-detail copies follow at +600 (951 = 351's). Not placed yet: 421-492,
531-555, 581-589, 641-672, 721-752, 781-828.

Shop package (F block) per model, k-th model = k-th package of its
category: cores 3420, heads 3400 (351 = 3411), arms 3430, legs 3460,
right weapons 3700, left weapons 3730, back units 3620, extensions 3600.
Checked by the TEX0 values (each category's models use the TEX0s its
packages hold). Two gaps: boosters use a TEX0 (…9e80) that is in none of
3500-3506, so they stay untextured; arms have 23 packages (14 with three
textures, 9 with two) but 22 models (13 and 9), so one of 3430-3443 has no
model here and arms 352-364 may show a neighbour's textures.

## Textures

- A GIF A+D packet sent to VU address 256 before a mesh's strips sets
  register 0x06 (TEX0_1): the strips after it use the package texture whose
  header holds the same TEX0. TEX0 0 = untextured.
- Texture: 0x20 header (`total, pixel bytes, count, 0, TEX0`), linear 4- or
  8-bit pixels, then the palette (8-bit: CSM1 row swap). Alpha x2, 0x80 =
  opaque. Greyscale: the game tints them with the paint.
- Untextured lone triangles are markers (muzzle, mounts, head camera, feet):
  left out of the OBJs, empty nodes in the glTFs.
- Still untextured: boosters, the arms' extra TEX0 (…9fd0) and some leg and
  core pieces with a TEX0 outside their own package (probably a shared page).

## glTF

`parts/<kind>/NNNN.glb` and `test-ac/ac.glb` (binary glTF 2.0, textures
embedded): a node per mesh at its joint, rotations from the 0x50 block
(R = Rz Ry Rx, unconfirmed), parents inferred (none are stored: arms and legs
chain each mesh to the previous one on the same side, centre pieces hang off
mesh 0), markers as empty child nodes, the hand weapon under the right hand.
Axes as the OBJs (y up, right hand at -x) in AC2 units; the test AC's root
lifts the feet to 0. Same output as `tools/gen2/gltf.py` (65 nodes, 27
meshes, 12 textures for the test AC).

## The test AC

core 301, head 332, arms 352, legs 390, booster 310, arm weapon 501 on the
right hand. `<import>/gen2/ac2/test-ac/` (`ac.obj`, `ac.mtl`, its PNGs,
`ac.glb`, `build.toml`); every part on its own in
`<import>/gen2/ac2/parts/<kind>/` (`NNNN.obj`, `.mtl`, `-K.png`, `.glb`).
AC TEST draws it textured (PS2 modulate: texel x vertex colour x 2).

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

- Animation (rigid model; the joints are known).
- Gen 2 stats and handling: the opponent runs on its Gen 1 build.
- Back units and extensions on the AC (mount markers), the low-detail
  copies, Another Age.
