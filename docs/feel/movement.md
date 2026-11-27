# Feel pass: movement (2026-11-27)

Goal: movement that feels like the original Master of Arena. Done unattended
(no questions asked, no play-test); every decision is written down here.
Branch `feel/movement`. Rollback point: `rollback/pre-feel-pass` (see
"Decisions" 1).

Sources, in order: the disc data (acr-import output: part tables, the
compile routine, `motion/model-NN.toml` root-motion tracks); then the MoA
recomp research (`docs/research/gen1-movement.md`, `gen1-stages.md`,
`gen1-models.md`, `recomp/mapping/battle-system.md`, and the disc copy in the
project file `ma-work.tar.xz`). Fight rules stay MoA's; content is
Phantasma's. Boost, jump and energy were **not touched** (owned by another
chat).

## Headline

An audit against the research found the engine already runs Master of Arena's
movement integer for integer: the compile formulas are checked against an
instruction-level transcription on 600 random builds, and the per-tick rules
against the recomp's measurements. So this pass changes little on purpose. One
real gap was found and fixed (diagonal walking), and one hard-coded value was
named so it can be tuned.

## Tick rate

| | Old | New | Source | Notes |
| --- | --- | --- | --- | --- |
| Logic step | 60 a second | 60 a second (unchanged) | user | Kept as asked. |
| Gen 1 rules | 22 ticks a second, run between the 60 steps (`step_rate` 60, `gen1_rate` 22) | unchanged | `gen1-timing.md`; VS measured 21.5-22 | This *is* the conversion of the original timings to 60 steps: every rule runs exactly as at 22 a second, and the drawing interpolates between ticks. Rescaling each constant to a 60 Hz integrator instead would add rounding drift, break the sim tests that compare against the 22 Hz script, and change netplay hashes, for no gain in feel. **Decision: keep the scheduler.** |

## Values

All speeds in 1/64 world units a tick (64 = 1 world unit a tick) unless a
row says world units. "Default AC" = MoA's starting AC (animation set 0).

| Value | Old | New | Source / address | Notes |
| --- | --- | --- | --- | --- |
| Walking, forward / back / strafe | root motion of the set's walk tracks (default: 31/37 gait, ~34 a tick) | unchanged | disc: `motion/model-NN.toml` anims 1/2, 8/9, 11/12, 14/15; stepper EXE `0x800135DC` | Per leg type through the animation set (72 sets, chosen from core, arms, legs; table `0x800D4A64`). Weight does not enter (measured). |
| **Walking, diagonal** | straight track turned 45°: ~34 a tick on the default AC | **the set's own diagonal track**: default AC ~39.8 a tick forward-diagonal, ~13.7-14.3 back-diagonal; LB-4400 (set 48) 41.9 | disc: anims 3/4 forward-left, 5/6 forward-right, 16/17 back-left, 18/19 back-right; the game's eight walking directions, `gen1-models.md` "Which pose" (tables `0x80051240`, `0x80051320`) | The only real change. Diagonal walking is 13-17 % faster than straight walking on every set that has the tracks, as in the game. Sets without root motion on those tracks fall back to the old rotation (in practice the four-legged / caterpillar sets, which walk by `drive_walk` anyway). The diagonal pose now also shows. |
| Walking, four-legged and caterpillar | pushed by ground-boost accel (+10 x 14) up to ground-boost top (+11 x 140); no stick: cut to top, slow by a quarter accel | unchanged | VS `0x80085704`, `0x80014790` | |
| Ground-boost acceleration | compile +10 x 14 (default 532) | unchanged | compile `0x800A8038-0x800A8238`, VS start `0x8008342C`; ground boost `0x8007F7BC` | +10 = thrust x 12 / 10, 1-64, fixed per legs 21-32 (`0x800AA96C`). Measured 531-532 default, 644 light, 897 LB-4400. |
| Ground-boost top speed | compile +11 x 140 (default 5,320 = 83 a tick, shown as 75) | unchanged | same | Limit on the speed vector's length. |
| 75-per-axis ground speed cap | `ACR_GEN1_AXIS_STEP_MAX` 75 → `acr_gen1_motion.axis_step_max` | unchanged (default) | VS `0x80054124`, three quarters of AC +0x28 (100) | Already a constant a ruleset can raise (or 0 for none). Diagonals are faster than straights because of it, as in the game. Applies in the air too. |
| Slide-out: full speed after a ground boost | `coast = 3` (literal) | `ACR_GEN1_BOOST_COAST_TICKS` = 3 | measured 2-3 ticks (`gen1-movement.md`) | **Decision: keep 3.** No code was found that holds the speed; part of the measured 2-3 may be the test driver's input delay, but with no code read the measurement wins. Named so it can be dropped to 1-2 after a play-test. Also used by the modern boost toggle and quick boost (their slide "as after any boost"). |
| Slide-out: slowing | boost accel a tick (default 532, ~10 ticks to a stop) | unchanged | measured -532 default, -896/-900 LB-4400 | |
| Ground friction byte +14 | kept, not applied | unchanged | `0x8007DFC0` never called in VS | Arena AI only. |
| Cannon-stance slide | an eighth a tick, 0 below 512 | unchanged | VS `0x8007E3E0` | |
| Turn acceleration | +12 x 5 / 16 (default 6) | unchanged | compile +12 = legs +0x38 x 3 x 65536 / 160 x 3573 / W >> 12; VS turn `0x8007E1DC` | Per leg type (legs +0x38) and weight. Stops at once on release (measured). |
| Top turn speed | +13 x 15 / 16 (default 38, 72° a second) | unchanged | compile +13 = legs +0x3A x 14635008 / W >> 12 | Per leg type and weight; measured 38 / 46 / 33 on three builds. |
| Crouched turning | back weapon +7 / +8 from legs +0x3C | unchanged | `0x8007E3E0` | |
| Step height | game rules when the import has `collision.txt`: the move leans down by a quarter of its length; a body box 100 either side, 212 tall, pushed out of polygons (6 tries); falls when the floor is 50+ below | unchanged | VS `0x8007B60C`, `0x80078C4C`, `0x8007C964`, `0x80083960` | There is no step-height number in the game: what an AC climbs comes from the box sliding along a slope's or step's polygon. The stand-ins `ACR_GEN1_STEP_UP` / `STEP_DOWN` = 120 are kept only for an import without `collision.txt` (re-import the disc to get the game's rules). |
| Wall sliding / collision response | the corrected move keeps its part along the surface (corner put a quarter unit short of the plane); where a sideways step was cut by 7+ the speed becomes the step taken | unchanged | `0x80073C10`, `gen1-stages.md` "Walls and floors" | Already the game's. |

## Modern controls (SP-MODERN)

Untouched and still relative to the base values: the boost toggle's top is
`ACR_GEN1_CRUISE_PCT` (150) / `CRUISE_HEAVY_PCT` (135) % of the **straight**
walking pace (`acr_gen1_walk_pace`, anim 2), never above the ground-boost top.
**Decision:** the toggle keeps the straight pace as its base, not the faster
diagonal, so it stays "faster than walking but not by much" and can't beat
ground boosting.

## Decisions

1. **Rollback point is a branch, not a tag.** Pushing the tag `pre-feel-pass`
   was refused by this session's git proxy (tag pushes and the GitHub refs API
   are blocked here), so `main` at `c795391` was pushed as branch
   `rollback/pre-feel-pass`. The tag exists only in the session's clone; to
   make it: `git tag pre-feel-pass c795391 && git push origin pre-feel-pass`.
2. 22-tick rules inside 60 steps kept (see "Tick rate").
3. Diagonal walking switched to the disc's diagonal tracks.
4. Ground-boost coast kept at 3 ticks, now a named constant.
5. Step height and walls left on the game's own collision rules; stand-ins
   only without `collision.txt`.
6. Boost, jump, energy and landing not touched (other chat). The open landing
   questions (slide on touchdown, recovery length) stay open.

## Still open (for a play-test)

- Whether the coast should be 1-2 ticks (input delay in the measurement).
- Diagonal walking with the walk-start tracks 3/5/16/18 (used here as the
  start, like the straight walks): not measured against the recomp.
- Turning while landing, and during the jump's take-off.
