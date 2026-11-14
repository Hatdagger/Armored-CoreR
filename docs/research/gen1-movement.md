# Gen 1 movement, measured

Measured in the recomp of Master of Arena (USA), VS Mode, Lunar Base stage, both
players on the **default starting AC**, player 2 idle. Every value is per
**logic step** (see [gen1-timing.md](gen1-timing.md)). Positions use the
game's units: the world is 16-bit, height (y) is negative upward, a full turn
is 4096. Scripts: `tools/measure/`.

## Where the state lives

| Field | Where |
| --- | --- |
| Position x, y, z | AC object +8, +10, +12 (signed 16-bit) |
| Facing | AC object +18 (4096 = 360°) |
| AP | AC object +360 (u16, AP x 4) |
| Energy | player block +22 (u16; full = 28,000 on the default AC) |
| Buttons | player block +4 (active-high PS1 layout) |

Player blocks start at `0x80045198` (928 bytes each); the AC object pointer is
at player block +920. In VS Mode the objects were at `0x801D4BC0` and
`0x801D4D38`. The position is written once per step by code at `0x80055060`
(VS engine), the movement integrator.

## Step rate in practice

Split-screen VS ran at **2.79 screen refreshes per step** (about 21.5 steps a
second), a little under the 2.72-refresh cap (22.0). So even a quiet fight
runs slightly below the cap.

## Walking (Up held)

| Steps | Units per step |
| --- | --- |
| 1-4 | 10-11 |
| 5-8 | 24-25 |
| then | repeating gait: 4 steps at 31, 4 steps at 37 (average 34) |

Speed follows the leg cycle rather than staying constant.

## Turning (Left held)

Turn speed rises by 6 per step (6, 12, 18 ... 36) to a top speed of **38 per
step** (3.3°, about 72° a second), and stops at once on release.

## Rising (X held)

| Phase | Behaviour |
| --- | --- |
| Start-up | about 9 steps with no movement before lift-off |
| Climb | 50 units per step, speeding up by 1 per step (speed changes every other step) |
| Energy | 436 per step while held |
| Release | keeps climbing while gravity slows it by about 4 per step (again changing every other step), then hovers |
| Refill | 210 per step after release |

A full 28,000 bar is about 64 steps (3 seconds) of continuous rise.

## Ground boost (X + Up held)

| Phase | Behaviour |
| --- | --- |
| Start | from walking pace 10-11, up to **75 units per step** in about 6 steps (roughly +8 per step) |
| Energy | 218 per step, half the cost of rising |
| Release | coasts at 75 for about 10 steps, then slows by about 8 per step |
| Refill | 127 per step while coasting (210 when settled, so refill depends on state) |

X alone rises; X with a direction boosts along the ground.

## The compiled AC: formulas from the game's own code

When a match starts, the menu module's compile routine (`0x800A7BB4`, FDAT 206;
called from VS START at `0x800B3058`) turns the AC's parts into a 324-byte stat
block at player block +592. The battle engine reads movement values from it.
Found by tracing writes to the block in the recomp, then reading the routine.

**Part tables** (menu module, loaded at `0x80050DD8`): head `0x800CE690` (60
bytes per part), core `0x800CEAC8` (56), legs `0x800CED30` (72), arms
`0x800CF870` (52), hand weapon `0x800CFD50` (44), back weapon `0x800D01C8`
(52), blade `0x800D0DF8` (52), booster `0x800D0F30` (48), FCS `0x800D11D0`
(96), generator `0x800D17D0` (48). Record: name (18 bytes), then 16-bit stats;
+0x28 weight, +0x2A energy drain (legs: drain while moving; generator: output),
+0x2C AP / booster rise power / generator capacity, +0x2E booster drain /
generator red zone, +0x32 legs: carry limit, +0x34 legs: drain standing,
+0x40 legs: turning.

**Garage parts** (`0x80036FB0`, 12 bytes): core, booster, head, arms, blade,
hand weapon, back L, back R, generator, FCS, (unused), legs. Default AC:
XCA-00, B-P320, HD-GRY-NX, AN-201, LS-2001, WG-RF35, WM-S40/1, RXA-01WE,
GPS-VVA, COMDEX-C7, LN-1001-PX-0.

| Compiled field | Formula | Default AC |
| --- | --- | --- |
| +18 energy capacity | generator +0x2C | 28,000 |
| +20 rise drain per step | booster +0x2E / 10 (no booster: legs +0x44 / 10) | 436 |
| +22 refill standing | (generator output - standing drain) x 64 / 1000, limited to 1-384 | 210 |
| +24 red zone | generator +0x2E | 7,800 |
| +26 refill moving | (generator output - moving drain) x 64 / 1000, limited to 1-192 | 127 |
| +28 blade power | blade +0x2C | 2,050 |
| +36 turn | (legs turning x 4096 / 1018) x (carried weight x 256 / 1000 / 512) / 16384, limited to 32-255 | 38 |

Standing drain = head + arms + core + FCS drains + legs standing drain (2,706);
moving drain uses the legs' moving drain instead (4,009). Carried weight = all
parts except the generator, capped at legs weight + legs carry limit (5,496).
Integer division throughout (the game uses multiply-by-reciprocal tricks that
round down). Every value above was checked against the live compiled block.

Note: the AC happened to rise exactly 2,050 units in the rise test, the same
as +28 (blade power); that was a coincidence, not the rise height.

## Still to measure

- The remaining compiled fields (walk speed, boost speed, rise speed, gravity),
  then confirm each formula with a second, very different AC.
- Turn speed rises with weight in the formula above; check with a heavy build
  whether the engine uses it as a speed or as a limit.
- Air boost (X with a direction while airborne), backward and sideways boost,
  landing, and energy running out.
- Heat, weapons and lock-on.
- The arena engine (FDAT 210) alongside the VS engine, to confirm they match.
