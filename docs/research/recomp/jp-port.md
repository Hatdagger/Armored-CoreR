# Porting addresses to the Japanese disc (SLPS-01855)

Every hook and address in this repo is for US disc 1 (SLUS-01030). The Japanese disc's code is mostly the
same code at other addresses, so addresses are moved over by matching code, not by guessing an offset.

## Tool: `tools/port_addrs.py`
Builds a memory image for each disc from the recompiled C (`generated/`, `generated-jp/`) and/or raw files
loaded at an address, cuts the code into pieces at each `jr ra`, pairs pieces that have the same shape on
both discs (immediates and jump targets ignored, only if unique), then reads the answer off the pairs:
- **code address**: its position in the paired piece (`code` in the output);
- **data address / call target**: the addresses the paired code builds (lui + addiu/ori/load/store, jal),
  tallied over every use (`refs N`, N = how many places agree).
The first code word on each side is printed so a hook's guard word can be copied straight from it.

Small routines often look alike (LoadImage, StoreImage and MoveImage are the same shape), so a second pass
pairs a look-alike only when exactly one candidate on the other side sits at the same distance as the
nearest unique pairs before or after it. Without it the four boot-exe GPU calls below were not found
(boot exe: 1,991 of 2,741 pieces paired; with it 2,735).

Quick start's list: `tools/port/quick-start-us.txt`. Run, with both menu modules (FDAT 206) at hand:

    python3 tools/port_addrs.py --us gen=generated --us bin=US-206.bin@0x80050DD8 \
        --jp gen=generated-jp --jp bin=JP-206.bin@0x80050DF8 --file tools/port/quick-start-us.txt

Self-test: US against US maps all 64 addresses to themselves; a module moved by 0x40 maps code by +0x40.

## Found so far (boot EXE only, 2026-11-23)
| What | US | JP | How sure |
|---|---|---|---|
| LoadImage | `8002C75C` | `8002C7C0` | same code |
| StoreImage | `8002C7BC` | `8002C820` | same code |
| MoveImage | `8002C81C` | `8002C880` | same code |
| DrawSync | `8002C4B0` | `8002C514` | same code (its data ref `8004E6FA` → `8004E71E`) |
| Menu module load address | `80050DD8` | **`80050DF8`** (likely) | 2 uses agree (and `80050DD0` → `80050DF0`, 14 uses) |

- Exe data moves **+0x24** near `8003EA64` and `80036C94` but **+0x20** from about `8003EA90`, so the
  variables the menu uses (`8003EA7A`, `8003EA94`, `80036FB0`, …) can't be offset-guessed: they come from the
  menu module's code once the JP module is matched.
- Still needs the JP disc's FDAT 206 (in `ma-jp-work.tar.xz`). Then also check the JP save layout (LOAD's
  unpack `80073610..80073B90`, "SC" header, checksum range, version byte) before LOAD GAME is turned on.

## Quick start on the Japanese disc (done 2026-11-23)
All 64 addresses are in `game/acg_addrs_jp.h`; `ACG_START_ALL` is 1 for both discs.
- JP FDAT 206 cut from `GG/COM/FDAT.T` (u16 count, then u16 start sector per entry): sectors 7802..8101,
  612,352 bytes (US 618,496). It **loads at `0x80050DF8`**: the title routine stores its own address.
- Matcher: 3,876 of 4,033 pieces paired. 58 of 64 answered; every variable unanimous (no `others:`).
- Checked by hand:
  - Title open `800AA118` (US `800AAE94`): same routine, smaller stack (guard `27BDFFE8`); a0 is the same
    "skip logos" switch. Its task slots +0x74/+0x78/+0x7C/+0xA3 are the same, and +0x7C is `80057F38`
    (null draw), which agrees with the matcher.
  - Weapon / hub RECTs (matcher had no answer): found by value; same order of copies, and every module data
    item used sits 0x1270 lower than on the US (emblem RECT, matched by code, too).
  - Exe variables: +0x24 up to ~`8003EA90`, +0x20 after, as expected.
  - LOAD (`80073004..`): 584 words, 54 differ, all of them calls, jumps or RAM addresses; no save offset or
    length differs. Save writer `800710E0` (US `8007155C`): same 3,712-byte buffer, checksum range 432 + 3,240,
    version byte at 3,672. So the save layout is the US one.
  - Every guard word in `k_code` read back from the JP module: all match.
- Memory card: the Japanese game names its saves **`BISLPSP01855`** + letter (a P, not a dash); the module
  also knows `BISLPS-00900` (AC1) and `BISLPS-01130` (Project Phantasma). The launcher uses that name in the JP
  program. Pilot/AC names in kana show as `?` in the launcher's list (its name reader only knows Latin letters).

## Address tables
`game/acg_quick_start.c` is one file for both discs. Its addresses live in `game/acg_addrs_us.h` and
`game/acg_addrs_jp.h` (same names, same order; `ACG_REGION_JP` picks one). `ACG_START_ALL` in
`game/acg_start.h` can turn a disc's START GAME extras off (no hook, LAUNCH GAME only) for a future disc
whose table isn't done.
