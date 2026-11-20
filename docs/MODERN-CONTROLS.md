# Modern controls (SP-MODERN)

An optional part, not on the disc. It takes every option slot of the core
(so nothing else can be fitted with it) and fits any core. Every save owns
one from the start, and the shop sells it for 0 C. Fitting it needs a
twin-stick controller; with it fitted the keyboard does nothing for that
pilot.

The trade: free twin-stick movement and a hard lock, paid for in aim, so a
skilled pilot on the original controls stays competitive. The AC's own turn
speed, weapons and weapon switching stay exactly as in AC1.

## Layout (after Armored Core VI's Type A)

| Control | Does |
|---|---|
| Left stick | move and strafe |
| Right stick | turn and tilt the view |
| Cross | boost (hold: jump / rise; with a direction while moving: full ground boost) |
| Circle | boost toggle: slow ground boost |
| Square | quick boost |
| R2 | fire the selected weapon |
| L2 | blade |
| Triangle | switch weapon (as AC1: one weapon selected at a time) |
| R3 | hard lock on / off |
| D-pad down | view back to level |

## Rules

| What | Value (stand-in) | Where |
|---|---|---|
| FCS lock boxes (inner and outer) | 80 % of the FCS's | `ACR_GEN1_MODERN_BOX_PCT` |
| Locked shots lead the target | 60 % of the way | `ACR_GEN1_MODERN_LEAD_PCT` |
| FCS sight follows the facing | 2 ticks late (~0.1 s), no faster than the AC turns | `ACR_GEN1_MODERN_SIGHT_LAG` |
| Missile lock time | 140 % | `ACR_GEN1_MODERN_MISSILE_PCT` |
| Blade energy per swing | 125 % | `ACR_GEN1_MODERN_BLADE_EN_PCT` |
| Blade lunge steering | 115 % of the turn rate (ignores the weaker sight) | `ACR_GEN1_MODERN_STEER_PCT` |
| Standing still after a swing | 100 ms after a hit, 300 ms after a miss | `ACR_GEN1_MODERN_RECOVER_*` |
| Boost toggle | ground boost at 20 % of the top speed and of the cost | `ACR_GEN1_CRUISE_PCT` |
| Quick boost | the AC's own top boost speed for 4 ticks (~0.18 s), 12-tick cool-down | `ACR_GEN1_QUICK_*` |

- **Sight lag.** A target that crosses quickly slips out of the sight and the
  lock breaks, even though it stays on screen.
- **Hard lock.** While the target is in the current weapon's sight (the
  smaller, lagging one), the AC turns and tilts toward it at its normal rates
  whenever the pilot isn't turning or tilting. It turns off when the target
  leaves the sight; press R3 again to turn it back on.
- **Boost toggle.** Counts as a ground boost (slide, coast and stage rules).
  It never jumps or rises, and does nothing in the air.
- **Quick boost.** Never faster than the AC's normal boost.

The FCS and blade values can be changed per match (`acr_gen1_battle.modern`;
0 = the default above), ready for a tuning page. Tune them with mirror
matches, modern against original.

Online: the option mask travels with the parts in the handshake (net
version 2). The pad is 3 bytes (rollback version 2) for the boost toggle,
quick boost and hard lock.
