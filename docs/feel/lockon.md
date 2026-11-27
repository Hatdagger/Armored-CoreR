# Feel pass: lock-on and FCS (2026-11-27)

Goal: lock-on and the FCS that feel like the original Master of Arena. Done
unattended (no questions asked, no play-test); every decision is written down
here. Branch `feel/lockon`, on top of `feel/movement` (#34) and
`feel/boost-energy` (#35), both checked merged before starting. Rollback
point: `rollback/pre-feel-pass`.

Sources, in order: the disc data (acr-import part files; the FCS records
`0x800D11D0`); the game code, read for this pass from the disc copy in the
project file `ma-work.tar.xz` (VS engine = **FDAT 209** loaded at
`0x80050DD8`; menu module FDAT 206 for the compile); then the existing
research (`docs/research/gen1-weapons.md`,
`recomp/mapping/stat-block.md`, `battle-system.md`). Fight rules MoA's,
content Phantasma's, 60 steps a second with the Gen 1 rules at 22 ticks a
second (unchanged, see `movement.md`).

## Headline: why the lock range felt short

The sight **already used the disc's FCS ranges** (COMDEX-C7: 6,100 / 8,000 /
8,500 / 8,000 for classes 0-3; the game measured locking out to ~6,250 with
the rifle). The real values are not longer than that, so they were not
raised. What made the range *feel* far too short were three real
differences from the game, all fixed:

1. **Shots died at the FCS range.** The engine capped the weapon's range by
   the FCS, so a WG-RF35 (8,500) on the COMDEX-C7 stopped shooting past
   6,100, a WG-RF/E (22,000) past 8,500. In the game the cap only applies to
   the *lock* (weapon slot +20); the shots get the weapon's own range (slot
   +22, read by the shot spawners at VS `0x800812D4`, `0x8008138C`). Now:
   `lock_range` = min(FCS class range, weapon range), `range` = the weapon's.
   Unlocked shots past the lock range now fly on and can still hit.
2. **The range was a sphere; the game's is a cylinder.** The sight sweep
   (`0x80078230`, `0x80078360`-`0x800783B8`) checks each axis against the
   range and then only the flat distance (x and z). A target high above
   (jumping, on a ledge) at 6,000 across used to fall out of a 6,100 range;
   it no longer does.
3. **Asymmetric FCS boxes were turned 90 degrees.** The compile copies an FCS
   class's box words to slot +24..+30 in file order (`0x800A8CE8`-
   `0x800A8D5C`), and the sweep tests the **pitch** against +24 (+28 inner)
   and the **yaw** against +26 (+30). The engine had read the first word as
   the width. Square boxes (COMDEX, QX, RATOR) are unchanged; TRYX-BOXER
   (320 x 220 class 0) is now 220 wide and 320 tall, TRYX-QUAD 340 wide and
   220 tall, FBMB-18X 220 wide / 140 tall, P/CV 140 / 120.

## Values

Angles in 4096 a turn; ranges in world units.

| Value | Old | New | Source / address | Notes |
| --- | --- | --- | --- | --- |
| Lock range | FCS class range | **min(FCS class range, weapon range)** | slot +20, compile `0x800A8C88`-`0x800A8CC8` | Rifle / C7: 6,100. Handgun HG235 (4,800): 4,800, not 6,100. |
| Shot range | capped by the FCS | **the weapon's** | slot +22; VS `0x800812D4`, `0x8008138C` | The AI now keeps to the lock range (`acr_gen1_ai.c`). |
| Range test | 3-D distance from the weapon | **each axis <= range, flat x/z distance <= range** | VS `0x80078360`-`0x800783B8` | From the weapon's joint to the target's middle, as before. |
| Outer box | w = word 1 (yaw), h = word 2 | **yaw = word 2, pitch = word 1** | slot +24 / +26; sweep `0x80078418`-`0x800784A8` | Importer comment fixed. |
| Inner box | w = word 3, h = word 4 | **yaw = word 4, pitch = word 3** | slot +28 / +30; gun lock `0x800802F8`-`0x80080330` | |
| Gun lock build-up | T = (speed x value >> 8) x dist >> 15; T ticks waiting, T aiming ahead | unchanged | VS `0x800800E4` (re-read) | Still target: locked the tick it enters the inner box. |
| Gun lock breaking | out of the inner box: lock gone that tick | unchanged | `0x8008049C` | |
| Missile cursor / lock time | value x 3 / 16 a tick; (256 - value) / 8 ticks per lock | unchanged | `0x80080638` | C7 class 1: 24 a tick, 16 ticks; ~0.95 s to the first volley. |
| Max locks | min(FCS +0x2C, weapon salvo, ammo, 6) | unchanged | slot +34, `0x80080638` | |
| Missile lock breaking | target leaves the outer box (inner needed to start one); SP-JAM 5 % a tick | unchanged | | |
| Sight on screen | inner box as an octagon, 16,384 out along the facing and view tilt; camera swings 1/2 toward a target in the sight | unchanged | HUD `0x800576E0`, camera `0x8008774C` | So the box drifts off the screen's centre as in the game. |

## Sounds (project doc `claude/sound-labels.md`)

Read from the code this pass (`0x800804A4`-`0x800804C4`, `0x80080470`, and
`battle-system.md` for missiles):

| Event | Game sound id | Sample | Before | Now |
| --- | --- | --- | --- | --- |
| Gun: target in the sight, not locked yet (every other tick) | 46 (0x2E) | #11 | none | **#11**, every other Gen 1 tick |
| Gun: locked | 42 (0x2A) | #10 | #11 | **#10** (`S_GUN_LOCKED`) |
| Missile: cursor working toward a lock (every 4th tick) | 47 (0x2F) | #11 | none | **#11** |
| Missile: a new lock | 43 (0x2B) | #32 | #32 | #32 |
| Missile: the last free lock | 44 (0x2C) | #33 | #33 when locks = FCS / weapon cap | #33 when locks = the full cap (also ammo and 6) |

**Decision:** the gun "locked" beep follows the code (sound 42 = sample #10,
which the listening round heard as a menu beep) rather than #11. If it sounds
wrong in play, it is one constant, `S_GUN_LOCKED` in `sfx.c`. The ids 45-47
are pitch variants of #11; the mixer has no per-id pitch yet, so they play at
the sample's rate. The beeps are driven by a counter the battle keeps
(`tones`, presentation only, never read by the sim), so several ticks in one
frame are not lost. Unsure: the gun tick's gate (`0x801D6EBC` bit 0) was
taken as "every other tick"; if it is a mode flag the game beeps every tick.

## Target switching (rules already decided; checked, unchanged)

- A held target stays locked while it is in the sight.
- A new lock picks the nearest enemy in the sight.
- Select (Back) switches to the next enemy in the sight.
- With only one gun / missile (the blade is not in the cycle), Triangle
  (weapon switch) switches target instead.

All four were already in `acr_gen1_battle_retarget_acts` and the input
mapping; nothing changed.

## Modern controls (SP-MODERN)

Still percentages of the base: `BOX_PCT` scales both box axes (now the right
ones), `MISSILE_PCT` the lock ticks, `LEAD_PCT` the lead. The lock range and
the range test are the base ones. No modern value changed. Note for the
play-test: a modern pilot sees no sight at all without hard lock and only
30 % of the box with it; if "too short" came from a modern session, that is
the knob, not the FCS.

## Tests

`tests/test_gen1_battle.c`: an off-centre target in a tall-and-narrow vs a
wide-and-short box (yaw is `box_w`), the flat range edge (6,000 in, 6,200
out of 6,100), and the missile locking beep (runs while locking, stops at
the cap). All 28 test programs pass; the game builds.

## Still open (for a play-test)

- Gun "locked" beep: #10 by the code vs #11 by the labels doc.
- Per-id pitch for the #11 beeps (45 / 46 / 47).
- Netplay: a fighter gained two bytes (`tone_tick`, `tones`); both sides
  compute them the same, but replays recorded before this change will not
  match hashes if the hash covers the whole fighter.
