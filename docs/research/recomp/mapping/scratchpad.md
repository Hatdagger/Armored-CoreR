# Scratchpad (0x1F800000 … 0x1F8003FF)

Lane doc for the PS1's 1 KB of fast on-chip RAM. Found 2026-11-21 by a static scan of the boot
exe and FDAT 206–211 (`ram-xref-modules.json`), then by reading the code. VS (FDAT 209) addresses
below; arena, link, missions and replay run the same fight code at other addresses and use the same
layout. Rows in `ram-map.toml` point here.

## Who uses it

- **The fight engines only.** VS, arena, link, missions and replay use the same offsets from the
  same number of routines (missions has one extra block, below).
- **The menu (FDAT 206) never touches it.**
- **The exe's game code (below 0x80017800) never touches it.** The only `lui 0x1F80` in the exe
  is PsyQ library code addressing hardware registers (0x1F801xxx).
- **Nothing lasts.** Every block is filled and used inside one routine (or one routine and its
  helpers). There's no state to save for netplay or replays, and nothing to read from outside
  while the game runs.

## Layout

| Offset | Size | What | Users (VS) |
|---|---|---|---|
| +0x000 | 0xEC | **Per-routine scratch.** Each routine lays it out its own way. See "Habits" below. | almost every drawing routine |
| +0x0EC | 0x20 | **Object matrix** of the model being drawn: rotation, translation at +0x100 / +0x104 / +0x108. Copied from 0x800AAFC0 / 0x800AAFD4 (the AC model draw's object matrix). | AC mesh draw 0x8006A5A8 |
| +0x10C | 0x2C | **Joint matrix**: rotation = object · joint, stored as columns (halfwords at +0x10C / +0x112 / +0x118, +0x10E …, +0x110 …), translation at +0x12C / +0x130 / +0x134. Loaded into the GTE before each joint's mesh. | 0x8006A5A8 (also mission copy 0x8006B778); the subdividing stage drawers 0x8006F2F0 / 0x8007068C use +0x10C … +0x118 for their own temporaries |
| +0x120 | 0x68 | **Missions only:** +0x120 an origin (s16 x/y/z, model units), +0x128 a point copied in, ÷ 4, minus that origin; more temporaries up to +0x186. The routines call SetRotMatrix / SetTransMatrix with a MATRIX at +0x000 first. Purpose not known yet `?`. | missions 0x80093C94, 0x80093EF0, 0x80093F98, 0x80094640, 0x80094D78 (called from 0x80095078) |
| +0x200 | 0x10 | **Per-view camera work**: +0x200 SVECTOR −eye (for T = R · (−eye)), then each point of the list at 0x801ADC70 minus the eye; +0x208 / +0x20A / +0x20C the eye as s16. | per-view draw 0x800670C8 |
| +0x210 | 0x20 | **Ry**, the camera's yaw matrix from 0x80014344 (see `renderer.md`). Written through a pointer, so the scan doesn't list it. | 0x800670C8 |
| +0x300 | 0x0C | **View-cone test**: the object's position in view space (MVMVA), radius added to z at +0x308. | 0x8006647C |
| +0x350 | 0x44 | **Near stage draw context** (table below). | 0x8006CAEC, 0x8006D8EC and their helpers |
| +0x3D0 | 0x30 | **Sound distance**: offset from the listener (0x800441D8 / 0x800441EC, the camera eyes), squared with GTE SQR into +0x3D0 / +0x3E0, vector at +0x3F0, then SquareRoot0 (0x80028E74) for volume falloff. | 0x80052A10, 0x80052B18, 0x80052C88 (before the play routine 0x80053284) |

### Near stage draw context (+0x350)

Used while one stage area is drawn at its near, lit level (`renderer.md`, "One area, 0x80061700").
0x8006CAEC / 0x8006D8EC set it up. Polygons too close for one pass are sorted into four lists, and
at the end each list is drawn by a subdividing helper that splits a polygon into four and calls itself
(0x8006EA60 → 0x8006E5E4, 0x8006F2F0 → 0x8006EC70, 0x8006FB6C → 0x8006F5A0, 0x8007068C → 0x8006FE0C,
0x80070F94 → 0x800709C8, 0x80071A8C → 0x8007120C).

| Offset | What |
|---|---|
| +0x350 | current polygon header word (+0x351 size in words, +0x353 primitive code: 0x24, 0x2C, 0x6C …) |
| +0x354 | area model record (from 0x801A2568 +0x290) |
| +0x358 | model data base (record +8) |
| +0x35C | value from 0x801A2568 +0x8A4 |
| +0x360 / +0x364 | CLUT / TPAGE, already shifted to the high half, OR'd into the UV words |
| +0x368 | polygons left |
| +0x36C | OT base + 0x1E0 |
| +0x370 | OT base used by the helpers |
| +0x374 | read by the helpers `?` |
| +0x378 / +0x37C | depth shifts (0x801A2560 + 15 / + 3; the helpers use z ≫ shift for the OT slot) |
| +0x380 … +0x38C | four pointers to 2 KB lists on the stack, one per polygon kind, ended with 0 |
| +0x390 / +0x391 / +0x392 | RGB fed to GTE NCCS (64, 64, 64) |
| +0x393 | primitive code byte written into each split polygon |

## Habits in the per-routine area (+0x000 … +0x0EB)

- **Mesh drawers** (stage, AC, objects, effects: 0x8005C908, 0x8005CF1C, 0x8005E048, 0x800600A0,
  0x80060CB4, 0x80067D00, 0x8006882C, 0x80069298, 0x800699B0, 0x8006ACE8, 0x8006CAEC …) copy
  the primitive allocator's next-free pointer (R+0x130 +8, `renderer.md`) to **+0x04** and its
  end to **+0x08**. They add each primitive to +0x04 and write it back when done (0x8006D5B0).
  Projected vertices go to +0x0C … +0x28; +0x2C … +0x2F hold a colour / code word.
- **Sound upkeep** 0x80052EB4 passes +0x000 to SpuGetAllKeysStatus (0x8001BF88, 24 status bytes)
  and checks each voice for 3 (on, envelope off).
- **Animation player** 0x80058E90: +0x00 counter, +0x04 / +0x06 / +0x08 deltas blended with GTE GPF.
- **Missions** 0x80093C94: a MATRIX at +0x000 for SetRotMatrix / SetTransMatrix.

## No use found

No load or store anywhere in the scan touches: **+0x15E … +0x175, +0x18A … +0x1FF, +0x230 … +0x2FF,
+0x30C … +0x34F, +0x397 … +0x3CF**. They might be free for mods, but the scan misses accesses through
pointers handed to other routines (Ry at +0x210 is one), and addresses built from an index. So check
in a running game first (watchpoints in the recomp or DuckStation) before putting anything there.
