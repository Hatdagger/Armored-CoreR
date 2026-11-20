# Modern controls (SP-MODERN)

An optional part, not on the disc. It takes every option slot of the core
and fits any core. Equipping it unequips every other optional part, and
while it is fitted no other optional part can go on. Every save owns
one from the start, and the shop sells it for 0 C. Fitting it needs a
twin-stick controller; with it fitted the keyboard does nothing for that
pilot.

The trade: free twin-stick movement and a hard lock, paid for in aim, so a
skilled pilot on the original controls stays competitive. The AC's own turn
speed, weapons and weapon switching stay exactly as in AC1.

## Layout (after Armored Core VI's Type A)

| Control | Does |
|---|---|
| Left stick | move, relative to the camera |
| Right stick | spin the camera (the AC turns to follow); tilt the view |
| Cross | jump; with the boost toggle on, hold to rise (never a ground boost) |
| L1 | boost toggle on / off: the only ground boost |
| Square | quick boost |
| R2 | fire the selected weapon |
| L2 | blade |
| Triangle | switch weapon (as AC1: one weapon selected at a time) |
| R3 | hard lock on / off: the camera stays on the target |
| D-pad down | view back to level |

## Rules

| What | Value (stand-in) | Where |
|---|---|---|
| FCS lock boxes (inner and outer) | 30 % of the FCS's (aiming is automatic) | `ACR_GEN1_MODERN_BOX_PCT` |
| Locked shots lead the target | 60 % of the way | `ACR_GEN1_MODERN_LEAD_PCT` |
| FCS sight follows the facing | 2 ticks late (~0.1 s), no faster than the AC turns | `ACR_GEN1_MODERN_SIGHT_LAG` |
| Missile lock time | 140 % | `ACR_GEN1_MODERN_MISSILE_PCT` |
| Blade energy per swing | 125 % | `ACR_GEN1_MODERN_BLADE_EN_PCT` |
| Blade lunge steering | 115 % of the turn rate (ignores the weaker sight) | `ACR_GEN1_MODERN_STEER_PCT` |
| Standing still after a swing | 100 ms after a hit, 300 ms after a miss | `ACR_GEN1_MODERN_RECOVER_*` |
| Boost toggle | ground boost at 150 % of the AC's walking pace (135 % for four legs and caterpillars), never above its ground boost; 20 % of a ground boost's cost, also while standing | `ACR_GEN1_CRUISE_PCT`, `_HEAVY_PCT`, `_DRAIN_PCT` |
| Quick boost | the AC's own top boost speed for 4 ticks (~0.18 s) in the stick's direction | `ACR_GEN1_QUICK_TICKS` |
| Quick boost cost | at the press: what a normal boost from a stand would use to cover the same distance | `acr_gen1_quick_cost` |
| Chain boost | up to 3 quick boosts in a row, each within 8 ticks (~0.36 s) of the last ending; then 16 ticks (~0.7 s) cool-down | `ACR_GEN1_QUICK_CHAIN*`, `_COOL` |

- **Sight lag.** A target that crosses quickly slips out of the sight and the
  lock breaks, even though it stays on screen.
- **Free camera.** The right stick spins the camera at about 180 degrees a
  second (`ACR_GEN1_MODERN_CAM_RATE`), and the AC turns in place to face
  where the camera looks, at its own legs' turn speed. The left stick moves
  relative to the camera. So the camera turns freely, but the FCS sight
  (which follows the AC's facing) still turns no faster than the legs allow.
- **Hard lock.** Keeps the camera on the target wherever it is, at any
  distance (`ACR_GEN1_MODERN_HARD_LOCK_START`, 0 = no limit). It does no
  aiming: the AC still has to turn round at its own rate and the FCS still
  has to lock as usual. It stays on until R3 is pressed again or the target
  is down.
- **Rockets with hard lock.** While hard lock is on their refire wait is 3x
  normal (`ACR_GEN1_MODERN_ROCKET_ROF_PCT`), a third of the damage per
  second. With it off they fire at the normal rate.
- **Boost toggle (L1).** A modern pilot's only ground boost; counts as one
  (slide, coast and stage rules). Walking pace comes from each AC's own walk
  animation, so heavy legs cruise slower. Left on while standing, it keeps
  draining at the same rate.
- **Cross.** Never boosts along the ground. Toggle off: a plain jump from the
  legs (no rise, no energy, works even when dry), or a hop while sliding.
  Toggle on: jump and hold to rise as normal. Caterpillars don't jump, so for
  them cross lifts off and rises either way.
- **Quick boost.** Pick a direction with the left stick and press square: the AC
  thrusts that way. Never faster than the AC's normal boost. Nothing drains or
  refills during the burst. A press during a burst waits for it to end.
- **Running dry.** The boost toggle cuts out and switches off, and there is no
  quick boost and no energy weapons until the bar is full again. The blade already needs energy for everyone.

**Human PLUS** (when it comes as the player's option part): the one exception
to "nothing else fits". It can be equipped together with SP-MODERN, and
equipping SP-MODERN leaves it on. A modern pilot gets only its no-kneel benefit
(cannons fire without the stance). Its energy rules, and any Human PLUS
benefit added later, don't apply (`acr_gen1_human_plus_level`).

The FCS and blade values can be changed per match (`acr_gen1_battle.modern`;
0 = the default above), ready for a tuning page. Tune them with mirror
matches, modern against original.

Online: the option mask travels with the parts in the handshake (net
version 2). The pad is 3 bytes (rollback version 2) for the boost toggle,
quick boost and hard lock.
