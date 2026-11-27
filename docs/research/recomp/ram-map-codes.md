# Armored Core: Master of Arena (NTSC-U) — RAM address map from cheat codes

Derived from GameShark / Codebreaker codes (Almar's Guides; ViperByte, MofG, Salvo & Kagato 210-code list;
gamegenie.53lu.com). Unverified unless noted — confirm in a memory viewer before relying on anything.
Shorter, code-checked list: `mapping/ram-map-gameshark.md`.

## ⚠ Two NTSC-U code sets exist

gamegenie.53lu.com lists a separate **"Version 1.1"** code set. Same layout, shifted:
- Save/profile/inventory data: **+0x64** (credits 3EA74 → 3EAD8, heads 36FEC → 37050)
- Battle data, AP, weapon slots, state flags: **+0x60** (AP 1E73B0 → 1E7410, slot 4547E → 454DE, flag 7B7CA → 7B82A)

All addresses below are the **v1.0** set (our disc, SLUS-01030, COPY.TXT "Version 2.0") unless marked.
(2026-11-20: the Japanese disc SLPS-01855 says "Version 1.1" in its COPY.TXT — check whether the
"v1.1" code set is really for that disc before chasing a US 1.1 pressing. See `disc-versions.md`.)

## Code format
- `80AAAAAA VVVV` 16-bit write · `30AAAAAA 00VV` 8-bit write · `D0AAAAAA VVVV` if equal, run next line
- `5000NNSS VVVV` repeat next line NN times, address += SS, value += VVVV · RAM base `0x80000000`, little-endian

## 0. Equipped AC loadout (from v1.1 "Modifier" codes)
One byte per slot = equipped part ID. v1.0 = v1.1 − 0x64. Confirmed in code as `MOA_GARAGE_PARTS 0x80036FB0`
(12 part ids, then paint at `0x80036FBC`).

| v1.1 | v1.0 | Slot |
|---|---|---|
| 37014 | 36FB0 | Core |
| 37015 | 36FB1 | Booster |
| 37016 | 36FB2 | Head |
| 37017 | 36FB3 | Arms |
| 37018 | 36FB4 | Arm weapon L |
| 37019 | 36FB5 | Arm weapon R |
| 3701A | 36FB6 | Back weapon L |
| 3701B | 36FB7 | Back weapon R |
| 3701C | 36FB8 | Generator |
| 3701D | 36FB9 | FCS |
| 3701E | 36FBA | ? |
| 3701F | 36FBB | Legs |

## 1. Parts inventory — `0x80036FEC`–`0x800370E2` (v1.0)
One byte per part, value = owned count (save +640; shop stock is the next 256 bytes at `0x800370EC`, save +896).
Index = category base + table row (head 0, core 18, arms 29, legs 53, generator 93, FCS 108, booster 143,
back 157, hand 217, blade 243).

| Range | Category |
|---|---|
| 36FEC–36FFA | Heads (HD-01-Srut … HD-4004) |
| 36FFE–37002 | Cores (XCA-00, XCL-01, XCH-01, XXA-50, XXL-D0) |
| 37009–37011 | Arms An- (An-101 … An-891-S) |
| 37015–3701D | Arms Aw- (Aw-M625/2 … Aw-DC/2) |
| 37021–37041 | Legs (LN, LB, LF, LFH, LC) |
| 37049–37051 | Generators (GPS-VVA … GBX-XL) |
| 37058–37061 | FCS (Comdex-C7 … Rator) |
| 37068–37074 | Optional parts (Sp-Maw … Sp-DEtq, SP-ABS/RE) |
| 3707B–37081 | Boosters (B-P320 … B-HP25) |
| 37089–370BF | Back weapons (WM, WR, WC, RXA/RZ, WX, XCS) |
| 370C2–370DC | Right arm weapons (WG/WA, P77 ST, WG FG100) |
| 370DF–370E2 | Left arm (LS-2001 … LS-99-Moonlight) |

The arm codes in every list say `0x80070009+`: a typo for `0x80037009+`.

### Per-part bytes
Heads 36FEC: Srut, 2002, X1487, Redeye, D-9066, GRY-NX, 06-Radar, One, 08-Dish, Zero, 6780, 12-Radar, Helm, H10, 4004 (→36FFA)

Cores: FE XCA-00, FF XCL-01, 00 XCH-01, 01 XXA-50, 02 XXL-D0

Arms: 09 An-101, 0A An-201, 0B An-K1, 0C An-D-7001, 0D An-3001, 0E Anks-1A46J, 0F An-863-B, 10 An-25, 11 An-891-S,
15 Aw-M625/2, 16 Aw-GT2000, 17 Aw-RF105, 18 Aw-30/3, 19 Aw-RF120, 1A Aw-S60/2, 1B Aw-XC5500, 1C Aw-XC65, 1D Aw-DC/2

Legs: 21 LN-1001, 22 LN-SSVT, 23 LN-3001, 24 LN-1001-PX-0, 25 LN-501, 26 LN-SSVR, 27 LN-1001B, 29 LN-3001C, 2B LN-502,
2C LN-D-8000R, 2D LN-2KZ-SP, 2E LNKS-1B46J, 2F LB-4400, 31 LB-4401, 32 LB-4303, 33 LB-1000-P, 34 LBKS-2B45A, 35 LF-TF-0,
36 LF-205-SF, 37 LFH-X3, 38 LF-Dex-1, 39 LB-H230, 3B LFH-X5X, 3C LC-MOS18, 3D LC-UKI60, 3F LC-HTP-AAA, 40 LC-MOS4545, 41 LC-HTP-H5

Generators: 49 GPS-VVA, 4A GPS-V6, 4B GAD-RX5, 4C GAD-RX6, 4D GAD-RX7, 4E 6B6-10000, 4F 6B6-XR, 50 GBX-TL, 51 GBX-XL

FCS: 58 Comdex-C7, 59 Comdex-60, 5A Comdex-68, 5B QX-21, 5C QX-AF, 5D Tryx-Boxer, 5E Tryx-Quad, 5F QX-9009, 60 Fbmb-18x, 61 Rator

Optional: 68 Sp-Maw, 69 Jam, 6A M/Auto, 6B Abs, 6C Sap, 6D Cnd-K, 6E Axl, 6F S/SCR, 70 E/SCR, 71 Eh, 72 E+, 73 DEtq, 74 ABS/RE

Boosters: 7B B-P320, 7C B-P350, 7D B-T001, 7E B-T2, 7F B-P351, 80 B-UR-33, 81 B-HP25

Back weapons: 89 WM-S40/1, 8A WM-SA0/2, 8B WM-S60/4, 8C WM-S60/6, 8D WM-MVG404, 8E WM-MVG802, 8F WM-L201, 90 WM-X201,
91 WM-X5-AA, 92 WM-X10, 93 WM-94001, 94 WM-PS-2, 95 WM-AT, 96 WM-T0100, 97 WM-SMSS24, 98 M118-TD, 9B WR-550, 9C WR-S100,
9D WR-M50, 9E WR-M70, 9F WR-L24, A5 WC-CM35, A6 WC-ST120, A7 WC-LN350, A8 WC-GN230, A9 WC-X94000, AA WC-XC8000, AB WC-01QL,
AC WC-SPGun, AD WC-IR24, B1 RXA-01WE, B2 RZ-A0, B3 RXA-99, B4 RXA-77, B5 RZ-A1, B6 RZT-333, B7 RZ-BBP, B8 RZ-Fw2,
BD WX-S800/2, BE WX-S800-GF, BF XCS-9900

Right arm: C2 P77 ST, C6 WG-RF35, C7 MGA1, C8 M6500, C9 AR1000, CA HG235, CB RF/5, CC RF/P, CD HG512, CE F699, CF B2120,
D0 B2180, D1 XP1000, D2 XP2000, D3 XC4, D4 1-Karasawa, D5 RFM118, D6 XF PPk, D7 HG1, D8 PB26, D9 WA-Finger, DC WG FG100

Left arm: DF LS-2001, E0 LS-2006, E1 LS-3303, E2 LS-99-Moonlight

### Gaps (possible hidden/cut parts)
36FFB–36FFD, 37003–37008, 37012–37014, 3701E–37020, 37028, 3702A, 37030, 3703A, 3703E, 37042–37048, 37052–37057,
37062–37067, 37075–3707A, 37082–37088, 37099–3709A, 370A0–370A4, 370AE–370B0, 370B9–370BC, 370C0–370C1, 370C3–370C5,
370DA–370DB, 370DD–370DE

## 2. Player profile — `0x8003EA70`–`0x8003EACB` (v1.1 +0x64)
| v1.0 | v1.1 | Field |
|---|---|---|
| 3EA70 (16) | 3EAD0 | Equipped optional parts bitmask |
| 3EA74 (32) | 3EAD8 | Credits (code-confirmed; save +456) |
| 3EAC8 (8) | 3EB28 | Human Plus level (0–0x0F) |
| 3EAC9 (8) | — | Champion arena unlocked |
| 3EACA (8) | 3EB2A | Overweight build ability |
| 3EACB (8) | — | Guest arena unlocked |

## 3. Arena progression — `0x8004B600`–`0x8004B685` (v1.0)
| League | Rival flags | Finished flag |
|---|---|---|
| Humanoid legs | ~4B5FF–4B608 | 4B67E |
| Four legs | ~4B609–4B612 | 4B67F |
| Reverse joint | ~4B613–4B61C | 4B680 |
| Caterpillar | ~4B61D–4B626 | 4B681 |
| Champions | 4B630 (16) | ? |
| Guests | 4B640 (30) | ? |
| FromSoft | ~4B656–4B65F | 4B684 |
| Masters | 4B668 (20) | 4B685 |

## 4. Live weapon slots — struct 0x3E (v1.1 +0x60)
Slots: arm `0x8004547E`, back R `0x800454BC`, back L `0x800454FA`.
+0x00 max ammo, +0x02 current ammo, +0x06/+0x08 range, +0x0A lock speed, +0x0E/+0x10 lock box, +0x14 missile lock count / autofire.
Booster/energy `0x800451AE`.

## 5. AC objects / state (v1.1 +0x60)
- Mission player AP `0x801E73B0`, gated by `0x8007B7CA == 0x1040`
- Arena player AP `0x801D8D70`; enemy AP `0x801D8EE8`, gated by `0x8007BBAE == 0x1040` → stride 0x178 (AC object = 376 bytes, confirmed in `battle-system.md`)

## 6. NTSC-J code set (Almar's Guides "Japan" CodeBreaker list) — added 2026-11-24

A third code set, labelled NTSC-J. Its shifts against our US v1.0 disc are **not** the "v1.1" shifts above,
so the "v1.1" set and this list are for different builds.

| Code | NTSC-J | US v1.0 match | Shift |
|---|---|---|---|
| Credit Max | 3EA98 / 3EA9A | 3EA74 `credits` | **+0x24** |
| All Options | 3EA94 FFFF (gate D0084708 == 002A) | 3EA70 `options_fitted` | +0x24 |
| Booster Max | 451CE EA60 | 451AE (player1 +0x16) | **+0x20** |
| Bullet 1000 | 4549E/454A0, 454DC/454DE, 4551A/4551C | 4547E … (weapon slots) | +0x20 |
| Arena AP Max | 801D8590 (gate D007B3E2 == 1040) | 801D8D70 (gate 7BBAE) | −0x7E0 (gate −0x7CC) |
| Arena Enemy AP 0 | 801D8708 | 801D8EE8 | −0x7E0 (stride 0x178 unchanged) |
| Scenario AP Max | 801E6BB8 (gate D007B006 == 1040) | 801E73B0 (gate 7B7CA) | −0x7F8 (gate −0x7C4) |
| Scenario Time | 800A0DCC = 5000 (gate D0052726 == 2462) | 800A0DC4 = missions `vs_match` +0x14, time left in steps | +0x08 (likely; not checked) |

Reading it:
- Exe save/profile data moves by +0x24 and the player blocks by +0x20; the fight objects move the other
  way by about 0x7E0. The AC object stride (0x178) and the slot layout are the same.
- Our Japanese dump (SLPS-01855) says "Version 1.1" in COPY.TXT. That doesn't tell us which list fits it:
  this NTSC-J list could be for a JP 1.0 pressing, and the "v1.1" set could be the JP 1.1 disc, a US 1.1 pressing or PAL.
  **Quick test once the JP exe is loaded:** read the u32 at 0x8003EA98 (credits if this list fits) and at 0x8003EAD8
  (credits if the "v1.1" list fits). Whichever one matches the save's credits names the code set, and then
  the matching shifts give first-guess addresses for the JP fresh recomp.
- "Scenario Time" 0x5000 = 20,480 steps written to time left: the cheat stops the mission clock running out.
  Its US address is already mapped (`vs_match` row, `missions = 0x800A0DB0`).

Source: almarsguides.com/retro/walkthroughs/ps1/games/armoredcoremasterofarena/codebreaker/japan/

## Sources
Almar's Guides (MoA Codebreaker, NTSC-U); ViperByte, MofG, Salvo & Kagato 210-code list;
gamegenie.53lu.com/cheats/ps1/armored-core-master-of-arena/3.html. Its "Infinite Ammo" entry is actually the optional-parts unlock.
