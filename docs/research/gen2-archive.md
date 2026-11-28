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

## File offsets (SOLVED 2026-11-27, session 4)
The table offsets point too early. Real start = table offset **+16 sectors (32 KB) in AC2, +32 sectors (64 KB) in Another Age**; 96% of recognisable files agree, the rest are detected per file.
- `tools/gen2/split_bin.py` splits the BIN (or the .iso) on the user's PC with this rule, checks every shop package (JOIN category index + name record) and writes `split/<group>/NNNNN.bin` plus a split log.
- Results: AC2 all 234 shop packages (including the 13 last-of-group parts that were cut off before); AA 270 of 272 (3790/3800 are in the unknown range).
- Stitching (`stitch.py`, the +32 KB read in PR #64) is no longer needed with split files.

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
This is the Assembly/Shop data. Each entry is a `JOIN` package: `JOIN`, u8 category count, a section-kind list (see Joints below), the index within the category, then section sizes and offsets (relative to JOIN) for the shop model, textures, a settings block and the name/stats record. The group sizes match the Prima guide exactly, so this is the authoritative category list.

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
| 1-5 | System/common data (unknown) |
| 10 | Game tables: weapon table at 0x280b0 and ballistics table at 0x180b0 (range, velocity; same row index). See Weapon stats |
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
- Weapon damage/ammo are NOT in this record: they are in file 10 (see Weapon stats). Guns carry `u8 solid(0)/energy(1), u16 class, u16 weapon-table row` at +128; blades and shields keep their stats in this record.
- 235 of 240 entries read (`tools/gen2/stats.py` output). The LAST entry of every group (3411, 3425, 3452, 3490, 3506, 3529, 3547, 3567, 3586, 3607, 3651, 3691, 3765) is cut off, because its tail would be in the next ID, which doesn't exist. This needs AC2DATA.BIN read again with corrected offsets (strong evidence that the table offsets are read 32 KB off).
- 3750-3765 = optional parts (SP-…: shell/energy screens, balancers, FCS accelerator, rapid charge…). 3660-3691 is an exact copy of 3620-3651 (back units).
- Examples: heads ZHD-GE/OHR, EHD-NIGHTEYE; legs ZLN-XX0/TP (humanoid), ELB-… (reverse joint), ZLF/ELF (four-legged), ZLC/ELC (caterpillar), ZLR (float); weapons KARASAWA-MK2, LS-MOONLIGHT.
- SLUS_200.14 holds the internal model codes in catalogue order (h01-h12; cn01 cn02 cl01 cl02 ch01 ch02; b01-b07; an/al/ah/awb/awa/awe/awm arms; l2n/l2l/l2h/lr/l4/lt/la legs; wrr/wrm/wrh/wrb/wrsh/wre/wrsp; wlb/wls; wbm/wbr/wbc/wbo/wbd*; exb/exa/exm) as paths `/a2/ac2/model/ac/parts/<kind>/<code>/tp2/<code>` at file offset 0x2ecda0. Counts match the F and B blocks, so the k-th code = k-th F entry = k-th B model of that category.
- Scripts: `tools/gen2/join.py`, `tools/gen2/stats.py`.

## Parts record stat fields (NAMED 2026-11-27, session 5)
u16 words from +114 (w0 = +114, w7 = +128, w14 = +142). Checked against published stats for heads ZHD-MO/EGRET, EHD-GN-92, EHD-DOME, ZHD-06/RW, ZHD-8008/S, core ZCH-GR/1, legs ZLB-3322/OP and ZLR-ZIO/MATRIX, and arms ZAW-XP/T9. Every checked value matches unless marked. `tools/gen2/stats.py` writes them as named fields.

| Word | All frame parts | Head | Core | Legs |
|---|---|---|---|---|
| w0 | weight | | | |
| w1 | energy drain | | | |
| w4 | (always 0x4120-ish: float 10.0 with w3, meaning unknown) | | | |
| w7 (+128) | armor points | | | |
| w8 | def shell | | | |
| w9 | def energy | | | |
| w10 | cooling | | | |
| w11 | | system recovery | max weight | low byte leg type (0 humanoid, 1 reverse joint, 2 four-legged, 3 caterpillar, 4 hover; matches every name prefix N/B/F/C/R); high byte jump (0 = no jump function) |
| w12 | | auto balancer | ? (710 on GR/1) | max weight |
| w13 | | low nibble computer type (0 rough, 1 standard, 2 detailed); next nibble voice (1 male, 0 female); high byte map type (0 no memory, 1 area memory, 3 area & place name; 2 on EHD-DOME, which the wiki lists as "no memory") | low byte optional slots | moving speed |
| w14 (+142) | | low byte hacking function; high byte flags: 0x01 radar, 0x02 noise canceler, 0x04 bio sensor | OB power | braking |
| w15-w16 | | float: radar range (398 EGRET, 270 RW, 475 8008/S) | w15 OB energy drain; w16 ? (4800 on GR/1, wiki's OB limit is 4200) | w15 landing stability, w16 defensive stability |
| w17-w18 | | WACS: float 240 (second radar value?) | w17 7, w18 ? | w17 ? (relates to turning speed: 1320 → 520, 2555 → 472); w18 stationary drain |
| w19-w20 | | | | hover only: 20000, 5350 (hover boost?) |

**Internal parts and the rest** (w0 weight, w1 energy drain as above; no AP or defences):

| Category | Fields | Checked against |
|---|---|---|
| Generator | w7 energy output, w8 max charge, w9 redzone, w10 calorific value; energy drain is 0 | GPS-VA: 5782 / 5800 / 2420 match. The wiki says max charge 2800, but the record has 28000; all generators are 28000-53000, so the wiki has probably dropped a zero |
| Radiator | w7 cooling, w8 forced cooling | RBG-CM6: 6672 / 5976 |
| FCS | w8 low byte max lock, w9 precision. Lock speed, sight range/ability and reticle are not found as plain numbers (w12-w13 and w16 look like floats, w17-w21 are small values) | DOX-105: 5 locks, precision 8. The wiki says weight 15; the record has 16 |
| Core (extra) | w13 low byte optional slots | ECM-XR00: 11 slots, OB power 2800, drain 18000, cooling 343. VS-MG response/angle (48/64) not found; w16 is 4800 on both XR00 and GR/1, but the wiki's OB limits are 2400 and 4200, so w16 is not OB limit |
| Booster | w7 boost power?, w8 charge drain?, w9 booster heat? | No AC2 source yet. The names follow later games' booster stats and the values fit (boost power 8800-21000) |
| Optional | w7 slots used | SP-S/SCR 2 and SP-E(NE)-SCR 1, the same as in Master of Arena |
| Inside | w7 = 4 (kind), w9 weapon row in file 10 (116-122) | Rows follow on from the extensions' 110-115 |
| Extension | w7 kind (2 or 3 = weapon, 0x62xx = booster). Weapons: w11 weapon row (110-115). Boosters: w8 boost power?, w9 charge drain? | |

- **Derived on the wiki, not stored:** defensive points (shell + energy), and probably computer ability, EDPS, moving ability, turning speed and Over Boost.
- **Not found in this record:** head sensor interval and scanning interval. They may be in the package's settings section (kind 2).
- **Arms:** w7 AP and w10 cooling match ZAW-XP/T9. Weapon-arm firing stats come from the weapon table (file 10), as guns do.
- **Still open:** FCS lock speed, sight and reticle; booster names (no published AC2 numbers found); head sensor/scan intervals; core w12/w16 and VS-MG.

## Weapon stats (DECODED 2026-11-27, session 3)
- Table in **file 10** (AC2: offset 0x280b0 in `00010.bin`), 0x80 bytes per row. The row number is the u16 at +132 of the part's catalogue record (right-arm guns 32-56, back weapons 64-87, extensions 110-115; rows 0-31 and others are enemy weapons).
- Row fields (u16 index): w0 high byte = projectile id; w2 = number of ammo; w3 = ammo price (solid) or energy per shot (energy); w8 = reload; w12 = 0x17 solid / 0x27 energy; **w13 = attack power**; w14 = ammo heat; w15 = stun (probably).
- Checked against published stats: ZWG-SRF/8 521 atk / 80 ammo / 90 price / 44 reload / 93 heat; EWG-SRF-9 608/60/100/52/132; ZWG-HC-IR/K99 2515 atk / 12 ammo / 92 reload / 82 heat. All match.
- **Reload:** w8 for arm guns, missiles and inside/extension weapons. Back cannons (w11 high byte 0x23) keep reload in **w7** (EWC-XP0808: w7 26 = published 26; w3 5100 = usage drain); their large w8 (5180, 8312…) is something else, not decoded. Missiles have small w7 values (10/18/24) that are not reload, probably lock time.
- **Ballistics (range, velocity):** file 10 at 0x180b0 (AA 0x200f0), 0x100 bytes per record, indexed by the **same row number** as the weapon table. +0 u16 projectile kind (1 missile, 2 bullet, 4 spread); **+0x24 float velocity ÷ 100**; **+0x28 float range**. Checked: ZWG-SRF/8 1050/1000, EWG-SRF-9 1150/910, ZWG-HC-IR/K99 870/620, EWC-XP0808 range 600. For missiles +0x28 is 450-600 (lock range) and +0x24 is 1.0, so missile speed is elsewhere. The earlier "range at 0x24fca" was a coincidence: that offset is record 172 (an enemy projectile).
- **Locks (weapon row):** w5 low byte = max locks (EWM-S602 2, S608 8, S612 12, M444 4, others 1); w5 high byte = lock time, lower is faster (LD2 80, S612 90, M24/1MU 100, normal 110 for S602/M444, M24/1MI 120, S608 125, FIN-B00 255). Matches Zaku IX's AA weapons FAQ labels in order (Fast / Normal / Barely slower / Extremely slow). w6 low byte = projectiles per lock (M24/1MI 4, LD2/1DA 2). w15 = stun is now confirmed (K99 15, SRF/8 2).
- **Missile flight (ballistics record, kind 1):** +0x20 u16 lifetime in frames (180-600); +0x24 launch speed (1.0, M444 0.5); +0x28 lock range; +0x2c acceleration (0.045-0.075); +0x30 probably guidance strength (0.8 normal, 0.05 M444, 0.3 FIN); +0x34 max speed (3.0-3.5, FIN 1.0); +0x38/+0x3c turn rates in radians per frame, exact whole degrees (S602/S608 1°, S612 6°, M24/1MU 8°, M444/MI 2°, LD2 2-4°). Same per-frame units as gun velocity (gun 10.5 = shop 1050), so missile top speed is about 350 on the shop scale. No published missile speeds exist to check against (the FAQ says AC2 removed the travel-speed stat).
- **Back cannon w8:** high byte is probably recoil (XP0808 0x0E1A, high 14 = published recoil 14; others LQ 20, GN44 42, FLUX 32, LNT 24, MAC 36, GCN77 16). Only one published value, so unconfirmed. Low byte unknown. Missile w7 (10 / 18 / 0) is not reload and not lock time; probably the delay between missiles in one volley.
- Script: `tools/gen2/weapons.py` (JSON of every shop weapon with its stats).

## Textures per strip (DECODED 2026-11-27, session 3)
- In each in-game (B block) mesh, the DMA chain sends a small UNPACK to VU address 256 before its strips: a GIF A+D packet. **Register 0x06 = TEX0_1** selects the texture for the strips that follow (0 = untextured). Register 0x14 is also set (not needed).
- Each texture in the part's F-block JOIN package stores the **same TEX0 value** in its header (+0x10). Rule: strip texture = the package texture with an identical TEX0. All tested parts match (arms use one extra TEX0, 0x…9fd0, that isn't in their own package — probably a shared texture).
- UVs are 0-1 floats (vertex qword 3); vertex colour (qword 4) is 128.0 (neutral). Textures are greyscale; the game tints them with paint colours.
- **Markers:** untextured single triangles (small equilateral) are locator points, not geometry: rifle muzzle (z = -3.7), core weapon mounts, head camera, foot contacts on legs. Exported as empty nodes.
- B model ↔ catalogue: in the stitched B stream (202 models), categories run cores 0-5, boosters 6-21 (pairs), heads 22-33, arms 34-56, legs 57-87, right weapons 88-112, left weapons 113-121, back units 122-153, extensions/inside after. The k-th model of a category = the k-th F entry.
- Script: `tools/gen2/textured.py` (loader + small software renderer).

## Joints (DECODED 2026-11-27, session 3)
- The JOIN header bytes are **not a joint table**. Layout: `JOIN`, u8 category count, u8 0, then a **section-kind list** (0 model, 1 texture, 2 settings block, 3 name/stats record, 0xFF = none), then at +0x10 the part's index within its category, +0x14 eight section sizes, +0x34 eight section offsets. The JOIN model (section 0) is a different, shop-screen format.
- The in-game per-mesh 0x50 block is `pos[3] 1, rot[3] (radians) 1, scale[3] 1, 3 x (count, offset), 0, 0xFFFFFFFF`. The counts are strip count, unique-vertex count and a third list (shadow/collision?). The last word looks like a parent slot but is always -1.
- So **no parent links are stored**. Parts are stored assembled in rest pose, every mesh pivot is already in place, and the game code knows the skeleton. Inferred rule used for export: arms and legs chain each mesh to the previous mesh on the same side (x sign); centre pieces hang off mesh 0. Biped legs: 0 hip, 1-4 right thigh/shin/foot/toe, 5-8 left, 9 back piece. Arms: 0-3 and 4-7 = shoulder/upper/fore/hand per side.
- Rotations are non-zero only on small core pieces. Euler order assumed X, Y, Z (unconfirmed).

## glTF export (2026-11-27, session 3)
- `tools/gen2/gltf.py OUT.glb` writes the PR #64 test AC (core 0, head 0, arms 1, legs 9, booster 0, rifle 0) as one binary glTF: 64 nodes, 27 meshes, 12 embedded textures, about 0.5 MB.
- Axes are already Armored-CoreR's (x, -y, -z): y up, feet at y = 0, right hand at -x. Units are AC2 units (about 10 tall); PR #64 scales by 83.5 to Gen 1 height.
- The weapon is a child of the right-hand node, so it follows the arm when animated. Markers are empty nodes named `<part>_meshN_markerK`.
- Checked by reloading with an independent glTF library (trimesh) and rendering it.

## Another Age vs AC2 (2026-11-27, session 3)
- **Extraction problem:** in the AA F block, each package's tail (the name/stats record) is cut off: the next package starts about 0x400-0x500 bytes before the current one ends, and the start drifts 0 / 32 KB / 64 KB into each file. Only 86 of 271 AA packages are readable. **AC25DATA.BIN needs re-extracting with corrected table offsets/sizes.** (Same root cause as AC2's 13 truncated last-of-group entries.) `tools/gen2/catalogue.py` reads packages from one stitched stream instead of by file ID.
- **New parts (23, from the model codes in SLUS_202.49, `/a2/ac5/model/...`; none removed):** heads h13 h14; legs l2n06 l2l04 l2h04 la06; right-arm weapons wrr07 wre05 wre06 wrsp05; left-arm wlb06; back units wbm09-wbm12 (missiles), wbr05 wbr06 (rockets), wbc07-wbc09 (cannons), wbdo01; extensions exs01 exb03. Names read so far: ZHD-102/ROCK, EWG-XC213 (laser rifle), EWG-HM-04 (hand missile), EWM-NAP-02 (napalm), EWM-M03, ZWM-GT/00 (ground torpedo), EWR-M40, ZWC-CN/500, EWC-GN-81. (EHD-WACS, ZXR-S/STEALTH and ZES-99/MIRROR were listed here by mistake: they are AC2 parts that were truncated.) Full list: `gen2-aa-diff.md`.
- **Weapon table:** same layout, AA file 10 at 0x380f0. New rows 57-60 (right arm), 89-97 (back), 123-125. About 30 existing weapons rebalanced, e.g. rifles +20 ammo, SRF/8 521→551 and SRF-9 608→628 attack, bazooka heat 374→574 / 453→673, laser/pulse/plasma rifle heat lowered (KARASAWA 68→42, K99 82→50), ZWX-E90/MAC 3850→4450 attack and 10→20 ammo, EWX-VLS241 1060→880 attack. `weapons.py --diff` lists them all.
- **AA reload:** raw reload values are unchanged in AA, but AA's published reloads are floor(raw × 5/6) for all 8 known pairs (SRF/8 44→36, K99 92→76, XP0808 26→21, S612 48→40, M444 57→47, INW-EM-RRD 45→37, ZAW-XP/T9 24→20, EAW-S604 41→34). So AA scales reload in code (a 50/60 time base), not in data. ACR uses AC2 values as-is.
- Frame parts: of the 90 AA records read, only 2 changed (ZLR-K196/RJ stats, EWX-GCN77-4 weight 1756→1556). The full comparison needs the re-extraction.
- **Base game:** AC2 stays the base. AA is a superset (same formats, +23 parts, rebalanced numbers), so AA can be layered on later as an option.

## Open questions
- Parts record: head, core and legs fields named (see above). Still open: core w12/w16, legs turning (w17), head sensor/scan intervals, FCS lock speed/sight/reticle, and confirming the booster fields.
- Confirm back-cannon recoil (need more published values); missile w7; +0x30 guidance.
- Hierarchy confirmation (game code) and rotation order; animation (see Animation search: probably procedural).
- Arms' extra shared texture (TEX0 …9fd0); boosters/back units/extensions in the glTF build.
- The 421-492 block.

## Animation search (2026-11-27, session 6)
No stored keyframe animation has been found for ACs. Checked:
- Leg/arm/core packages (B 301-411) end exactly at their model size; nothing follows the meshes.
- B 421-492 (65 MB, 9 sets of 8 files) is VIF draw packets (UNPACK 0x6c.., GIF tags, colour 128.0): geometry, not motion.
- A 1-5 and 150-190, D 2080-2095 and the 2200s: images, height maps, settings, geometry.
- A scan of A, C and D for long runs of smooth radian tracks at any joint stride (1-20 joints) found none.
- The ELF has no motion file paths (only `ac2/parts/data/%s%03d.bin`, map and arena paths). Its `motion_type` strings belong to the MPEG decoder.

Evidence for procedural motion: a developer tuning table in the ELF (labels at 0x439100-0x4397f0; entries `label ptr, variable ptr, step, max` at 0x429fb0-0x42a1a0) with `walk_waist / walk_center / walk_core`, `land_*`, `dash_start / dash_brake`, `jump`, `weapon_react`, `react speedstep`, shoulder/core aim limits (`Kata minX/maxX/minY`, `Core minX/maxX/maxY`; kata = shoulder) and aim speeds (`hand / arm / 2Ude / Kata / core spd`; ude = arm). The variables live in RAM at 0x01d27fd8+ (filled at run time). Leading theory: AC2 poses legs and body in code from these parameters plus leg stats, instead of playing clips as Gen 1 does. Not yet confirmed in code.
