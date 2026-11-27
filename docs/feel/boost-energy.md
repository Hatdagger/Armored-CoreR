# Feel pass: boosting, jumping, landing and energy

Branch `feel/boost-energy` (2026-11-27). Goal: boosting, jumping, landing and
energy behave like Master of Arena. Fight rules are MoA's; content stays
Phantasma's. Done unattended, so every call made is written down here.

Sources, in the order they win:

1. **Disc**: MoA (USA) `GG/COM/FDAT.T` entry 209 (VS engine) and 210 (arena
   engine), loaded at `0x80050DD8`, read with a MIPS disassembler; the
   animation lengths from `acr-import`'s `motion/model-NN.toml`.
2. **MoA recomp research** (`ma-work`, and what is already in
   `docs/research/gen1-movement.md`, measured in the recomp).
3. The user's own design rules (caterpillars, modern controls).

Units: speeds are 1/64 world units per Gen 1 tick; energy is the game's raw
bar (default AC: 28,000). The engine still runs 60 steps a second and fits
the game's 22 ticks a second between them (`acr_gen1_ac_step`); nothing here
changes that.

## What changed

Only the landing. Everything else in scope was already a port of the game's
code; this pass re-read that code on the disc to check it (see "Checked,
unchanged") and found it matching.

| Value | Old | New | Source | Notes |
| --- | --- | --- | --- | --- |
| Soft / hard landing line | none (one curve) | touchdown fall speed **< 4,481** soft, **>= 4,481** hard | VS `0x8007BA68` `slti v0, 0x1181`; arena engine `0x8007D730`, same constant | The game has two landing states: 0x60 and 0x61. A plain hop lands soft (falls back at about 3,200-3,500); a fall from a full rise (up to 9,600) lands hard. The controller rumble is also 4 vs 8 ticks long. |
| Soft landing length | 0 below a 2,000 fall, ramping up to 20 at 5,000 (stand-in) | animation 23's length: **8** default AC (sets: 8, 9 or 12) | handler `0x80084834` plays animation 0x17 to its end, then standing | Fallback 8 if a set has no animation 23. |
| Soft landing cancel | none | **any direction or turn ends it at once** | `0x80084834`: pad states 1-10 (walk, strafe, back, turn) start their animation the same tick | The booster alone does not cancel it (pad states outside 1-10 wait for the animation). Engine: `ac->landing_soft`. |
| Hard landing (kneel) length | 20 at 5,000+ (`landing_steps`, stand-in) | animation 36's length: **16** default AC; caterpillars 12; some humanoid / weapon-arm sets 20 | handler `0x80084C20` plays animation 0x24 to its end, then cuts to standing (`0x8007A884`) | Nothing cancels it. Turning while landing stays off, as the engine had it (not re-checked on the disc). Fallback 16. |
| Landing skid | sideways speed kept, slowed by the boost acceleration each tick | **stopped dead** (vx, vz = 0) | the movement dispatcher skips states 0x60 / 0x61 (`0x80083934-0x80083940`), so no movement handler runs; agrees with the recomp measurement "touching down stops the AC dead" | The open question in gen1-movement.md ("whether sideways speed survives a touchdown") is closed by the code: it doesn't. |
| Old stand-in | default | kept as a test switch, `acr_gen1_ac_def.landing_guess` (F3 / Back + R3 in the test build, HUD shows `PAUSE: DISC` / `OLD GUESS`) | | Renamed from `landing_from_anim`, whose meaning (landings last the animation) is now the default. `landing_steps` only feeds the old guess. |
| Pose | animation 36 for every landing | 23 for soft, 36 for hard | | `acr_gen1_pose_shown`; both cut in, as 36 did. |
| Replay / netplay rules | `ACR_REPLAY_RULES` 3 | **4** | | Landings play out differently, so older replays are refused instead of desyncing; the replay test's golden numbers are updated. `landing_soft` is in the match hash. |

**Conflict to know about:** the recomp measured "about 20 ticks" in the
landing state after a short hop. The disc code makes a hop a soft landing
(8 ticks, and cut short by any direction). The disc wins per the brief. The
20 came from screen refreshes divided by the measured refresh rate, during a
run whose step rate wandered, and the driver may have held no input for a
while; worth one re-measurement in the recomp (hop, no input, count ticks in
state 0x60) when convenient.

Not modelled: the landing handler only runs every couple of ticks (the AC
+0x6E countdown) and counts "finished" at `last_tick - 1`, which can add or
take a tick. Lengths are the animation's length as is.

## Checked, unchanged

Re-read on the disc this pass (VS engine) or already ported from it; the
engine already does exactly this.

| Value | Engine value (default AC) | Source | Notes |
| --- | --- | --- | --- |
| Jump take-off wait | animation 22's still frames: 7 still + 2 rising = **9 ticks** before free flight | animation track, gen1-movement.md "Walking, strafing and the jump take-off are animation-driven" | 7 on caterpillar and four-legged sets, 8-10 on others. |
| Jump take-off speed (the "jump bonus") | **3,200** up (50 units a tick), from the track's first upward key | same | Bipeds (humanoid, reverse-joint) and quads start their arc with it, then the rise acceleration adds on top. |
| Heavy bipeds' weapon-arm jump (known quirk, user 2026-11-27) | already reproduced; now pinned by a test | disc animation sets (`motion/models.toml`, jump animation 22) | The game picks the animation set from core, arms and legs. The heavy humanoid legs LN-3001, LN-3001C, LN-SSVR, LN-S3 and LNKS-1B46J (game legs 2, 5, 8, 9, 13) with normal arms use sets 8 / 16 / 25, whose take-off rises 249 over 2 frames: **1,984** a tick. With weapon arms (12-21: sets 32 / 35 / 38) it rises 249 in 1 frame: **3,968**, twice the kick, so a visibly higher jump. Every other biped is the same either way (light humanoids 3,968, default-style 3,200; reverse joints 5,984 / 5,856). The engine reads the take-off from the chosen set's track, so no code change was needed; `tests/test_gen1_sim.c` checks both shapes. |
| Rise (vertical boost) acceleration | +6 x 2 = **64** a tick | VS match start `0x8008342C`; air handler `0x8007FBAC` | |
| Rise top speed | +7 x 200 = **5,000** | same | No per-axis limit going up. |
| Gravity, fall limit | **256** a tick, **9,600** | air handler | |
| Air boost (sideways while airborne) | accel +8 x 14 = 448, top +9 x 140 = 3,500, release slows by accel / 4 | `0x8007FBAC`, `0x80014790` | Booster only decides up or down. |
| Ground boost | accel +10 x 14 = 532, top +11 x 140 = 5,320, 75 a tick per axis | gen1-movement.md | Out of scope for walking; unchanged. |
| Caterpillars | no ground boost, no jump, no boost hop; the booster goes straight into the rise with **no take-off kick** (first tick at -rise accel) | user rule (2026-11-19) | `no_jump`, `no_boost_hop`. Their booster power is the game's flat 9,800. |
| Boost drain: rising and air boost | rise drain +20 = booster +0x2E / 10 = **436** a tick | energy routine `0x80082D90`: drains only with the booster held **and** movement class 0xC0 (air) or 0xB0 (ground boost) | Rising and air boosting cost the same (both class 0xC0). |
| Boost drain: ground boost | **half**, 218 | `0x80082EF8` `sra a2, 1` for class 0xB0 | |
| Jump take-off crouch | no drain, **refills** while the booster is held | class is not 0xC0 / 0xB0 during the crouch, so the code falls through to the refill | Engine: the crouch state sets no drain. |
| Recharge, standing / air / turning | +22 = (output - standing drain) x 64 / 1000, 1-384: **210** | `0x80082E1C` | |
| Recharge, moving on the ground | +26, 1-192: **127** | `0x80082DF8` / `0x80082E48` | Read this pass: a per-player byte (+0x278) of 0 or 3 gives walking (class 0x10) the moving rate; other values give it only to class 0xB0 with speed. The engine's "on the ground and moving" gives the same result for every leg type it has, because four-legged and caterpillar walking always carries speed. What +0x278 is (probably the leg type) is not confirmed. |
| Recharge delay | **none**: the refill starts the tick boosting stops | `0x80082D90` has no timer | The feel of a "delay" is the red zone's quarter rate. |
| Red-zone threshold | generator +0x2E: **7,800** default | compile +24 | |
| Red zone effect | below it, drain **and** refill run at a quarter | `0x80082E98-0x80082EA8` | So the last part of the bar lasts longer and comes back slowly. |
| Over-drain lockout | cost > what is left: bar to 0, booster locked (player block +0x18 bit 0) | `0x80082F18-0x80082F28` | |
| Forced recharge | while locked: refill at **half**, no red-zone quarter, booster unusable | `0x80082E7C-0x80082E88` | |
| Lock lifts | when the bar is found full (after a one-refill overshoot), not before | `0x80082F44-0x80082F64` | |
| SP-CND-K, Human PLUS energy | +12,000 capacity; level 6+ halves rise drain, doubles refills | gen1-movement.md (AC1Recomp mapping) | Unchanged. |
| Tick rate | 60 steps a second, game rules 22 ticks a second between them | `acr_gen1_ac_step` | Kept as asked. |

## Modern controls

Untouched and still working. Every SP-MODERN value is a percentage of the
base values above, applied at use (`ACR_GEN1_MODERN_JUMP_PCT` 50 of the
take-off, `ACR_GEN1_MODERN_RISE_PCT` 60 of the rise accel and top,
`ACR_GEN1_MODERN_RISE_EN_PCT` 125 of the rise drain, the boost toggle's
`CRUISE_*` percentages, the quick boost's cost from the normal boost). None
of the base values changed, so none of those results changed. Landings
apply the same to modern and original pilots (soft landings cut short by a
direction or turn either way), so neither gets an edge.

## Not touched

Ground walking, turning and collision (owned by `feel/movement`). The only
lines touched near them: the soft-landing cancel hands the AC back to the
ground state before the turn step, so the turn and walk code run as normal
on that tick.

## Files

- `src/gen1/acr_gen1_sim.{h,c}`: landing rule, `ACR_GEN1_LAND_*`, `ACR_GEN1_ANIM_LAND_SOFT`, `landing_soft`, `landing_guess`.
- `src/gen1/acr_gen1_pose.c`: soft landing pose.
- `src/gen1/acr_gen1_match.c`: `landing_soft` in the state hash.
- `src/net/acr_replay.h`, `tests/test_replay.c`: rules 4.
- `src/platform/sdl3/main.c`: the test switch now picks the old guess.
- `tests/test_gen1_sim.c`: soft, hard, cancel, fallback and old-guess checks.
