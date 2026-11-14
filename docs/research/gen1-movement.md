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

## Still to measure

- How each figure depends on parts: legs (weight, type), boosters, generator.
- Air boost (X with a direction while airborne), backward and sideways boost,
  landing, and energy running out.
- Heat, weapons and lock-on.
- The arena engine (FDAT 210) alongside the VS engine, to confirm they match.
