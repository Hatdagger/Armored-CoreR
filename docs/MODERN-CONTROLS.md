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
| Right stick | spin the camera (the AC turns to follow); tilt the view. With hard lock: turn and tilt the AC while standing |
| Cross | jump; with the boost toggle on, hold to rise (never a ground boost) |
| L1 | boost toggle on / off: the only ground boost |
| Square | quick boost |
| R2 | fire the selected weapon |
| L2 | blade |
| Triangle | switch weapon (as AC1: one weapon selected at a time) |
| R3 | hard lock on / off: the target stays in the middle of the screen |
| D-pad down | view back to level |

## Rules

| What | Value (stand-in) | Where |
|---|---|---|
| FCS lock boxes (inner and outer) | 30 % of the FCS's (aiming is automatic) | `ACR_GEN1_MODERN_BOX_PCT` |
| Locked shots lead the target | 60 % of the way | `ACR_GEN1_MODERN_LEAD_PCT` |
| FCS sight follows the facing | no faster than the AC's legs turn (no extra delay) | `ACR_GEN1_MODERN_SIGHT_LAG` (0) |
| Shot spread | up to ~0.7 degrees either side (half that up / down), plus the bloom | `ACR_GEN1_MODERN_SPREAD` |
| Bloom | +10 a shot (4096 a turn), up to ~5 degrees more; falls 2 a tick | `ACR_GEN1_MODERN_BLOOM_*` |
| Missile lock time | 140 % | `ACR_GEN1_MODERN_MISSILE_PCT` |
| Blade energy per swing | 125 % | `ACR_GEN1_MODERN_BLADE_EN_PCT` |
| Blade lunge steering | 115 % of the turn rate (ignores the weaker sight) | `ACR_GEN1_MODERN_STEER_PCT` |
| Standing still after a swing | 100 ms after a hit, 300 ms after a miss | `ACR_GEN1_MODERN_RECOVER_*` |
| Boost toggle | ground boost at 150 % of the AC's walking pace (135 % for four legs and caterpillars), never above its ground boost; 20 % of a ground boost's cost, also while standing | `ACR_GEN1_CRUISE_PCT`, `_HEAVY_PCT`, `_DRAIN_PCT` |
| Quick boost | the AC's own top boost speed for 4 ticks (~0.18 s) in the stick's direction | `ACR_GEN1_QUICK_TICKS` |
| Quick boost cost | at the press: what a normal boost from a stand would use to cover the same distance | `acr_gen1_quick_cost` |
| Chain boost | up to 3 quick boosts in a row, each within 8 ticks (~0.36 s) of the last ending; then 16 ticks (~0.7 s) cool-down | `ACR_GEN1_QUICK_CHAIN*`, `_COOL` |

- **Sight follows the legs.** The FCS sight turns with the AC, so how fast it
  can follow a target is set by the legs' turn speed. A target that crosses
  faster than the legs can turn slips out of the sight and the lock breaks,
  even though it stays on screen. (A delay on top, in ticks, can still be set
  with `ACR_GEN1_MODERN_SIGHT_LAG`; it is 0.)
- **Free camera.** The right stick spins the camera at about 180 degrees a
  second (`ACR_GEN1_MODERN_CAM_RATE`), and the AC turns in place to face
  where the camera looks, at its own legs' turn speed. The left stick moves
  relative to the camera. So the camera turns freely, but the FCS sight
  (which follows the AC's facing) still turns no faster than the legs allow.
- **Hard lock.** Pins the target to the middle of the screen wherever it is,
  at any distance (`ACR_GEN1_MODERN_HARD_LOCK_START`, 0 = no limit). The
  left stick moves relative to that view. Not firing, the AC turns to face
  the way it moves (dodging always turns it, at the legs' turn speed);
  standing, the right stick turns and tilts it. While R2 (fire) is held or
  L2 (blade) is pressed, the AC snaps to face the target, and strafes if
  moving. The FCS sight does not snap: it swings round from where the AC
  was facing at the legs' turn speed, and unlocked shots go where the sight
  points. So an AC already facing the target is ready at once, and one that
  was dodging has to wait for the sight to catch up. Every normal FCS
  condition still applies: the target has to come into the (smaller) lock
  box, in range, and the lock has to build up as normal. It stays on until
  R3 is pressed again or the target is down.
- **FCS only with hard lock.** Without hard lock there is no FCS: no sight on
  screen, no gun lock, no missile lock; shots go where the AC faces. With it
  on the sight comes up and locks work as above.
- **Rockets.** A modern pilot's rockets refire at a third of the rate
  (`ACR_GEN1_MODERN_ROCKET_ROF_PCT`, 300 % wait) and fly at 60 % of their
  speed with the same range (`ACR_GEN1_MODERN_ROCKET_SPEED_PCT`), with or
  without hard lock.
- **Spread and bloom.** Every modern shot (guns and rockets, locked or not)
  goes off by a random angle. Each shot widens the spread and it settles
  when the trigger is let go, so machine guns spray after a few shots while
  slow guns like rifles stay tight.
- **Boost toggle (L1).** A modern pilot's only ground boost; counts as one
  (slide, coast and stage rules). Walking pace comes from each AC's own walk
  animation, so heavy legs cruise slower. Left on while standing, it keeps
  draining at the same rate.
- **Weaker jump and rise.** The jump's take-off is 50 % (`ACR_GEN1_MODERN_JUMP_PCT`);
  rising has 60 % of the lift, acceleration and top speed
  (`ACR_GEN1_MODERN_RISE_PCT`), and costs 125 % energy (`ACR_GEN1_MODERN_RISE_EN_PCT`).
- **Cross.** Never boosts along the ground. Toggle off: a plain jump from the
  legs (no rise, no energy, works even when dry), or a hop while sliding.
  Toggle on: jump and hold to rise as normal. Caterpillars don't jump, so for
  them cross lifts off and rises either way; while moving (cruising, sliding
  or walking) they lift off at once and keep their speed.
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
