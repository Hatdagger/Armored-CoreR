# Gen 2 (Armored Core 2 / Another Age): archive map and formats

Research notes from taking apart the AC2 and AC2: Another Age data archives.
Companion to `gen2-models.md` (in-game part models and the AC TEST proof of concept, PR #64).
Scripts are in `tools/gen2/`.

## The archives

### Armored Core 2 (USA, SLUS_200.14): AC2DATA.BIN
- AC2DATA.BIN is 710,377,472 bytes. Skipped: ADS/ (ADX audio), MOVIE/ (PSS), DUMMY.OUT, IOP/.
- The first 32 KB is a table of uint32 LE pairs (offset, size) in 2048-byte sectors. Pair index = file ID. Pair 0 = the table region (1584 sectors).
- IDs 1..3801: 1,537 files. The valid table stops at pair 4096 and covers sector 346,848 of 346,864.

### Another Age (USA, SLUS_202.49): AC25DATA.BIN
- AC25DATA.BIN is 1,143,963,648 bytes. Same format (table region 1580 sectors), but the table runs past pair 4096: IDs up to 4907.
- 1,956 files, covering sector 558,481 of 558,576. Skipped: ADPCM/ (VVV briefings), ADS/, MOVIE/, DRIVER/ (modem), IOP/, DUMMY.OUT.
- Entry filter: offset >= table end, offset+size within the archive, offsets non-decreasing by ID.

## Files overlap: stitch before reading
In the part blocks, each ID's real data usually starts 0x8000 (32 KB) into its file and runs into the next ID's file. The first 32 KB of a file is the tail of the previous ID.
- Fix used here: concatenate consecutive IDs of a block, in order, into one stream, then scan for headers (2 KB alignment works). See `stitch.py`.
- The importer in PR #64 instead reads each file plus 32 KB and takes the header at 0 or 0x8000.
- The likely cause is that the table offsets are being read one 32 KB slot off. This is worth re-checking against the raw BIN.

## General
- Nothing is compressed and there are no TIM2 files. AC2 uses its own raw PS2 formats.
- Text is ASCII. `@/` = line break, `@*` = page break. The first 16 bytes of a message = sender name.

## Texture format (decoded)
- 0x20-byte header: `u32 total, u32 pixel_bytes, u32 count (1), u32 0`, then the GS TEX0 register (u64) at +0x10.
- TEX0: PSM = bits 20-25 (0x13 = 8-bit indexed, 0x14 = 4-bit), width = 2^bits 26-29, height = 2^bits 30-33.
- Pixels start at +0x20, linear (not swizzled). The palette follows the pixels: 256 x RGBA8 with the standard PS2 CSM1 row swap (swap index bits 3 and 4). Alpha 0x80 = opaque.
- Part textures are greyscale on purpose, because the game tints them with the player's paint colours.
- The F block holds 423 textures: 227 at 128x128, 116 at 256x256 and 80 at 64x64. Script: `tex.py`.

## Part model format
Covered in detail in `gen2-models.md`. In short:
- Header `u32 mesh_count, u32 size, 0xFFFFFFFF, 0`, then 16-byte mesh entries (`first_packet_end, dma_start, u16, u16, 0`), then a 0x50-byte block per mesh (first 3 floats = mesh position/pivot).
- Mesh data is a PS2 DMA chain carrying VIF codes. Geometry comes in VIF UNPACK V4-32 (0x6C) blocks: a GIF tag (NLOOP = vertex count, triangle strip), then 4 qwords per vertex: position, normal, UV (s,t,1,0), colour.
- PS2 Y points down.
- Mesh counts per part: core 5, booster 2, head 1, arms 8, legs 7-13.

## F block (3400-3801): the full parts catalogue
This is the Assembly/Shop data. Each entry is a `JOIN` package: `JOIN`, a u8 mesh count, a joint/parent table (bytes, 0xFF = none), then an offset table (relative to JOIN) pointing to model data and textures. The group sizes match the Prima guide exactly, so this is the authoritative category list.

| F IDs | Count | Category |
|---|---|---|
| 3400-3411 | 12 | Heads |
| 3420-3425 | 6 | Cores |
| 3430-3452 | 23 | Arms |
| 3460-3490 | 31 | Legs |
| 3500-3506 | 7 | Boosters |
| 3520-3529 | 10 | FCS |
| 3540-3547 | 8 | Generators |
| 3560-3567 | 8 | Radiators |
| 3580-3586 | 7 | Inside |
| 3600-3607 | 8 | Extensions |
| 3620-3651 | 32 | Back units (set 1) |
| 3660-3691 | 32 | Back units (set 2) |
| 3700-3724 | 25 | Right-arm weapons |
| 3730-3738 | 9 | Left-arm weapons |
| 3750-3765 | 16 | Optional parts (SP-…) |
| 3780, 3800-3801 | 3 | Unknown |

## B / C blocks: in-game models
B (301-828) holds the full-detail models, using the overlap layout above. C (900s) holds self-contained low-detail copies: for example, arm 351 has 527 triangles and 951 has 33.

| B IDs | C IDs (low detail) | Count | What |
|---|---|---|---|
| 301-306 | 901-906 | 6 | Cores |
| 310-331 | 910-925 | 16 | Boosters (sizes come in identical pairs: 8 x 2?) |
| 331-351 | 931-942 | 12 | Heads |
| 351-373 | 951-973 | 23 | Arms |
| 381-411 | 981-1011 | 31 | Legs. Biped (381), reverse-joint (394), quad (399), tank (404), hover (409) |
| 421-492 | - | 72 | Unknown (large) |
| 501-555 | 1021-1045 | 25 | Right-arm weapons |
| 561-589 | 1051-1059 | 9 | Left-arm weapons |
| 601-672 | 1071-1102 | 32 | Back units, set 1 |
| 681-752 | 1111-1142 | 32 | Back units, set 2 |
| 761-788 | 1151-1158 | 8 | Extensions? |
| 801-828 | 1171-1178 | 8 | Inside? |
| - | 1201-1478 | | Third copy of the list (same overlap layout as B) |

## Other AC2 files
| IDs | What |
|---|---|
| 1-5, 10 | System/common data (unknown) |
| 101-113+ | Stages/maps (ELF path `map/m%03d`). Each has a sound bank (IECS) |
| 1700-1707 | UI model + textures |
| 1996-1999 | 1996 textures, 1998 help text (part stat glossary), 1999 mail titles |
| 2001-2060 | E-mails. Some carry part codes: op15, fcs10, exm02rn, insa03, wbds01rn, wrr05n, awe02n, wbr02ln, wbdc03ln |
| 2080-2095 | 3 MB each, image/screen data |
| 2101-2137 | 608 KB each. 2101 is named "Raven Test" |
| 2138-2140, 2150-2179 | Image-like data |
| 2500-3350 | SCEI sound banks |

Reference: the Prima guide's AC2 counts are Head 12, Core 6, Arms 23, Legs 31, Booster 7, FCS 10, Generator 8, Radiator 8, Inside 7.
The public CodeBreaker cheats are encrypted and only touch "owned" flags, so they don't help with the file map.

## Parts table (DECODED 2026-11-27, session 2)
- Part names and stats are NOT in SLUS_200.14. They live inside each F-block JOIN package: the last offset in the 2nd offset table (JOIN+0x34) points to an info record.
- Info record: `name[18] type[18] u32 price, u8 maker (0 = Zio "Z…", 1 = Emeraude "E…", 2 = third maker), desc[73]`, then stats at +114: `u16 weight, u16 energy drain`, … Armour parts have AP / defence / energy defence a few fields later (head ZHD-GE/OHR: wt 114, drain 350, AP 745, def 155, edef 145).
- Weapon damage/ammo are NOT in this record (probably in the ELF) — still open.
- 235 of 240 entries read (`tools/gen2/stats.py` output). The LAST entry of every group (3411, 3425, 3452, 3490, 3506, 3529, 3547, 3567, 3586, 3607, 3651, 3691, 3765) is cut off, because its tail would be in the next ID, which doesn't exist. This needs AC2DATA.BIN read again with corrected offsets (strong evidence that the table offsets are read 32 KB off).
- 3750-3765 = optional parts (SP-…: shell/energy screens, balancers, FCS accelerator, rapid charge…). 3660-3691 is an exact copy of 3620-3651 (back units).
- Examples: heads ZHD-GE/OHR, EHD-NIGHTEYE; legs ZLN-XX0/TP (humanoid), ELB-… (reverse joint), ZLF/ELF (four-legged), ZLC/ELC (caterpillar), ZLR (float); weapons KARASAWA-MK2, LS-MOONLIGHT.
- SLUS_200.14 holds the internal model codes in catalogue order (h01-h12; cn01 cn02 cl01 cl02 ch01 ch02; b01-b07; an/al/ah/awb/awa/awe/awm arms; l2n/l2l/l2h/lr/l4/lt/la legs; wrr/wrm/wrh/wrb/wrsh/wre/wrsp; wlb/wls; wbm/wbr/wbc/wbo/wbd*; exb/exa/exm) as paths `/a2/ac2/model/ac/parts/<kind>/<code>/tp2/<code>` at file offset 0x2ecda0. Counts match the F and B blocks, so the k-th code = k-th F entry = k-th B model of that category.
- Scripts: `tools/gen2/join.py`, `tools/gen2/stats.py`.

## Open questions
- Weapon stats (damage, ammo, range) — look in SLUS_200.14.
- The 13 truncated last-of-group entries.
- Which texture each strip uses (JOIN offset table: entries 2-3 look like the textures, entry 4 is per-mesh transform data) → textured renders.
- JOIN joint table: byte 4 = slot count (13 for head?), then a parent list (e.g. 00 00 01 01 02 03 ff…) — not confirmed yet.
- glTF export of a full textured AC; Another Age comparison.
- The 421-492 block.
