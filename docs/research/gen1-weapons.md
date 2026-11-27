# Gen 1 weapons, FCS and damage

First read from Master of Arena's menu module (compile routine `0x800A7BB4`,
FDAT 206) and VS engine (FDAT 209) and checked in the recomp. Since
2026-11-18 it follows the AC1Recomp fork's mapping of the same code
([battle-system.md], [stat-block.md], [parts-tables.md]), which settled
most of the open points below. Where the two disagreed, the mapping (read
from the code, then run-tested) wins, and the change is noted.

Time: "tick" = one Gen 1 game step (22 a second). The engine keeps every
counter in ticks and spreads movement over its own steps, so a fight plays
the same per second at 60 steps a second.

[battle-system.md]: recomp/mapping/battle-system.md
[stat-block.md]: recomp/mapping/stat-block.md
[parts-tables.md]: recomp/mapping/parts-tables.md

## Weapon detail tables (24 bytes per weapon)

Separate from the shop tables (which hold only price, weight and drain):
hand weapons `0x800D1F64`, back weapons `0x800D21D4`, weapon arms
`0x800D1DE4` (index arms - 12). Weapon type names (16 bytes each, "RIFLE",
"SMALL MISSILE"...) at `0x800D5488`, indexed by byte +7. `acr-import`
writes every field below into the part file.

| Byte | Meaning | Part file | WG-RF35 | WM-S40/1 |
| --- | --- | --- | --- | --- |
| +0 | kind: 0 gun, 1 missile, 2 cannon | `kind` | 0 | 1 |
| +1 | model id: picks the shot / missile tables | `model_id` | 0 | 0 |
| +2 | guns: bits 0-1 shots per trigger (0 one, 1 two, 2 four, 3 one a side), bit 2 fixed direction (dispensers), bit 3 weapon-arm aim; cannons: base recoil | `fire_pattern` | | |
| +3 | firing animation (second animation layer; right back slot +1; 255 none) | `firing_anim` | 0 | 1 |
| +4 | post-shot hold (guns and cannons; 255 none) | `post_shot_hold` | 0 | |
| +5 | sight: bit 0 lock box, bit 1 aiming guide, bit 2 fires straight ahead (rockets) | `sight` | 1 | 1 |
| +6 | lock class: picks the FCS's range, lock box and lock value (4 = no lock) | `lock_class` | 0 | 1 |
| +7 | type name | `type_id` | 6 RIFLE | 13 SMALL MISSILE |
| +8 | ammo | `ammo` | 200 | 40 |
| +0xA | energy per shot (energy weapons) | `energy_per_shot` | 0 | 0 |
| +0xC | ammo price | `ammo_price` | 18 | 130 |
| +0xE | weapon range | `range` | 8,500 | 9,000 |
| +0x10 | locks at once (missiles), capped by the FCS | `salvo` | 1 | 1 |
| +0x11 | fire interval, **in ticks** (garage RELOAD TIME) | `fire_interval` | 5 | 10 |
| +0x12 | flags: 0x10 solid / 0x20 energy damage; 0x40 stagger (WR-RS7); 0x80 burn (WG-FGI-00) | `ammo_flags` | 0x17 | 0x17 |
| +0x14 | attack | `attack` | 218 | 830 |
| +0x16 | impact | `impact` | 3 | 12 |

## FCS (96-byte records at `0x800D11D0`)

+0x2C: most targets locked at once (COMDEX-C7: 4); +0x2D: the garage's
lock-on area label (text only). Then four 12-byte groups, one per lock
class: range, lock box (outer width and height, inner width and height),
lock value.

| Class | Range | Box | Lock value |
| --- | --- | --- | --- |
| 0 (rifles, handguns, machine guns) | 6,100 | 240x240 / 200x200 | 60 |
| 1 (missiles) | 8,000 | 200x200 / 160x160 | 128 |
| 2 | 8,500 | 160x160 / 125x125 | 60 |
| 3 | 8,000 | 90x90 / 72x72 | 35 |

A weapon's **lock range** (slot +20) is the smaller of its own range and
its class's FCS range (rifle on the COMDEX-C7: 6,100); its shots still fly
the weapon's own range (slot +22: 8,500). The box columns are pitch first,
then yaw (slot +24 / +28 bound the pitch in the sight sweep). See
[docs/feel/lockon.md](../feel/lockon.md). The lock value works opposite ways by kind:
for guns and cannons it is a time (bigger = slower), for missiles a speed
(bigger = faster). SP-AXL scales it by 1.15 for solid weapons and 0.85 for
the rest (so missiles and energy guns lock faster, solid guns slower).

## Damage (VS engine `0x80079500`, checked in game)

Each AC has a shell and an energy damage factor (compiled +0x22, +0x23):

    x = head + core + arms + legs defense (shell or energy)
        (x 1.15 with SP-S/SCR or SP-E/SCR)
    factor = (184320 - (x * 4096 - C) / 50) >> 12, limited to 1..45
    C = 3,051,520 (shell), 3,584,000 (energy)

A hit takes `((attack * factor) >> 6) * 2 / 3` AP (integer maths), shell
or energy by the weapon's flags. Default AC: factors 27 (shell) and 32
(energy), so the rifle does 60, the missile 233 and the LS-2001 blade 246,
all as measured. No distance fall-off.

Impact sets a knockback level, `impact x 256 / (stability x 25 / 4)`,
0 to 5 (`0x80079AA4`): the blade (11) is level 5 on the default AC, the
rifle (3) level 3.

**Hit stagger** (read 2026-11-26, VS `0x80082500`). A hit of level 1 or
more clears the target's firing animation (AC +0x150, +0x164), so its
post-shot hold ends, and:
- crouched (state 34, the cannon stance): `0x8007B454`, taken in the stance;
- in the air (+0x46 = 192, or pushed up hard): hit state 129, states 41-44
  by the quarter it was hit from (front / right / back / left), the push
  added to its motion;
- on the ground: hit state 128, states 37-40, `0x8007BC6C` plays the hit
  animation, slows the AC's own motion and moves it by the push.
The push (`0x80079ADC`): impact x 3,276 x 8 >> 12 along the line from the
shot to the AC, half that sideways (about 3.2 units a tick per point of
impact, at most 98). The stun is the level itself: AC +0xDA = level,
counted down a tick at a time by `0x8007FECC` (ACG1PLUS
docs/research/mapping/feel-diff.md 4.2), so most hits freeze a normal AC
for 3-5 ticks; energy weapons (impact 0) never stun. The engine:
`ACR_GEN1_STAGGER_*` (the push halving each tick is ours).

## Hit cylinder

An AC is a cylinder for shots, missiles and the blade: radius 75, 212 tall
(AC object +0x72 / +0x74), standing on its feet; missiles aim at its
middle (y - 106). Was a 65 sphere at 100 up (stand-in).

## Guns

- **Cadence.** After a shot the refire wait (AC +0x48) is the fire
  interval byte **in ticks** (5 for WG-RF35) and counts down a tick at a
  time. Read 2026-11-26 (`0x80081020`-`0x80081040`): a tick that finds the
  wait above 0 only counts it down, and the gun fires on a tick that finds
  it at 0, so a wait of n fires every **n + 1** ticks (WG-MGA1, wait 1:
  every other tick, 11 a second; the engine had every tick). The count
  also stops while a firing animation plays (+0x6D not 255), for every
  gun, not only cannons. Missiles: the same (`0x800818F0`). The earlier "byte x 118 ms" came from the WG-RF35 alone, whose
  13-tick cadence (590 ms) is really its **post-shot hold**: weapons with
  detail +4 below 255 (WG-RF35 and WG-PB26 0, cannons WC-GN230 / WC-IR24 1,
  WX-ED2 / WX-C/4 2) play a firing animation on the AC's second layer, and
  no gun or cannon fires until it ends (the cannon's wait pauses during
  it). The engine uses 13 ticks for a hold of 0 (measured cadence) and
  13 + 2 x hold for the others (stand-in: the base length lives in the
  animation data).
- **Shots per trigger**: detail +2 bits 0-1 (2 or 4), one round each.
- **Straight fire**: sight bit 2 (rockets) and dispensers (pattern bit 2)
  fire along the facing with no aiming, lock or not.
- **Lock** (`0x800800E4`, read 2026-11-19). The sight sweep `0x80078230`
  (shared with missiles and the blade) looks from the weapon's joint
  (`0x80078888`) out to the **FCS class range** (not the weapon's) and
  marks the target "in sight" (lock record +0x16 bit 1) inside the outer
  box. The boxes are **angles** (4096 a turn) off the facing (AC +0x12)
  and the view tilt (+0x218): 240 = 21 degrees, 200 = 17.6. Each tick, with
  the target in the **inner** box (bit 2):
  `T = (target speed a tick x lock value >> 8) x distance >> 15`, where the
  speed is how far the target moved since the last tick and the distance
  is lock record +0x04 (the "r" that wasn't read before). The first T
  ticks count +0x08; then +0x0A counts and the lock (bit 3) is on once it
  reaches T. Out of the inner box both reset and the lock is gone that
  tick. So a still target locks at once, and the old flat 0.44 s (a
  stand-in from the missile) made every re-lock slow.
- **Aiming ahead** (main program `0x800155DC`): while +0x0A runs, the aim
  point is the target's middle plus its movement times the shot's flight
  ticks (distance / shot speed), refined `+0x0A / 4` times (at most 8).
  The angles land in lock record +0x18 / +0x1A; a locked shot leaves along
  them (`0x80081150`), from the weapon's joint, and flies straight.
- **The HUD sight** (`0x800576E0`) is the inner box: half-sizes
  `sin(angle) x 4` at 16,384 out, an octagon with the corners cut by 512.
  It is a little smaller than the lock area (sin against the angle), and the
  lock is judged on the target's middle, so an AC half out of the sight
  stays locked.
- Shot speeds: from the model id, VS engine `0x80080E38` picks one of five
  tables by id range: below 0x14 `0x80091198` (28 bytes each, speed at +8),
  0x14-0x27 `0x80092980` (40), 0x28-0x3B `0x80092C20` (36), 0x3C-0x59
  `0x80092824` (24), 0x5A on `0x800928E4` (22); speed at +0 in the last
  four. Cannons: `0x80091310` (6 bytes per model).

| Weapon | Speed (world units a tick) | Note |
| --- | --- | --- |
| WG-RF35 rifle, machine guns | 700 | traced in game: exactly +700 a step |
| WG-HG235 handgun | 720 | |
| WG-AR1000, WG-HG512, WG-XP1000 | 800 | |
| WG-RF/P | 840 | |
| WG-1-KARASAWA, WG-XC4 | 900 | |
| WA-Finger | 1,000 | |
| WG-RF/E | 2,700 | |
| WG-B2120 / WG-B2180 bazookas | 512 / 576 | |

## Missiles (VS `0x80080638`, `0x80081888`, `0x8008907C`)

**Lock.** A lock cursor (player block +0x20 / +0x22) turns toward the
target by `lock value x 3 / 16` a tick on each axis. Once it sits on the
target a counter runs; passing `(256 - lock value) / 8` (16 ticks for the
COMDEX-C7's 128) adds a lock and sends the cursor back to the centre, so
every extra lock costs the travel again. A new lock needs the target in
the inner box; locks hold while it stays in the outer one. Locks stop at
the smaller of the FCS's and the weapon's limit, the ammo left and 6.
SP-JAM on the target breaks the locks with a 5 % chance a tick.

**Firing.** With at least one lock and fire held (or SP-M/AUTO with every
lock taken) a volley run starts: locks x 4 ticks, and every fourth tick a
volley leaves from both sides (launcher table `0x800912B0`, 8 bytes per
model id: flight kind, ignition delay, missiles from side A and side B,
random spread). Each volley uses a lock; after the last one the refire wait
is the fire interval. Holding fire on a target straight ahead: 16 ticks to
lock + 4 to launch, about 0.95 s, against 0.93 s measured in the game.

**Flight** (table `0x80093114`, 40 bytes per flight kind, **not** per model
id as first thought; `acr-import` now follows the launcher table):

| Kind | Fuel | Life | Thrust | Top speed | Turn a tick | Cone |
| --- | --- | --- | --- | --- | --- | --- |
| 0, 9 | 50 | 60 | 80 | 350 | 110 (9.7 degrees) | +-90 degrees |
| 1 | 80 | 100 | 70 | 320 | 100 | +-90 |
| 2, 3, 4, 12 | 120 | 160 | 60 | 300 | 90 | +-90 |
| 5 | 180 | 200 | 30 | 100 | 50 | +-90 |
| 11 | 250 | 300 | 30 | 100 | 60 | +-135 |

Each tick: before ignition it coasts under gravity. Kind 4 (WM-SMSS24,
the vertical missile, ignition 20) doesn't: it flies along its heading, 8
faster a tick up to its top speed, turning toward the target (with lead,
`0x80088F60`) at its turn rate (`0x80089168` -> `0x8008951C`). Missiles
leave along the launcher's own joints (`0x800815A4` -> `0x80078888`), which
for kind 4 point up; the engine launches kind 4 straight up until the
joints are read;
at ignition it thrusts along its heading up to the top speed, the heading
turning at most "turn" toward the target. Once the target has been inside
the cone, leaving it makes the missile **give up for good** (it flies on,
wobbling now and then); kinds 5 and 11 never give up. After the fuel the
nose drops. Kind 3 splits into four kind-0 missiles 6 ticks after
ignition. Launch: toward the target plus the spread, at 100 plus the
launcher's own motion. So missiles can now be dodged by making one
overshoot, as in the game; before, they homed forever.

## Missile interceptor (core +0x34 / +0x35, VS `0x800829E4`)

Each tick, with chance `reaction / 128`, the core looks for an enemy missile
within 5,000 and within `+-angle x 4` of the facing (both axes). A find
starts a 15-tick burst; each shot of it (odd ticks) has a 1 in 32 chance
to destroy the missile. XCA-00: 48 / 48 (37.5 %, +-17 degrees).

## Blade (VS `0x80083224` / `0x8007EB48`)

- **Fields were swapped.** Blade +0x30 is the **attack** (LS-2001 738 ...
  MOONLIGHT 2,801); +0x2C (2,050) is the **charge drain**, the energy a
  swing takes (compiled +0x1C, which this doc called "blade power").
  `acr-import` now writes `attack` and `charge_drain`; re-import a disc
  imported before 2026-11-18.
- **Damage.** An energy hit: 738 on the default AC = **246**. The "492
  seen" was two hits in one swing, not one hit at a different rate.
- **Start.** Circle, a new press, needs energy above the charge drain and
  takes it; not while the booster is locked or a swing is running.
- **Steering.** At the start the game looks for an AC up to 3,000 ahead
  within +-45 degrees, and the AC turns toward it at its own top turn rate
  until the lunge (default AC: 38 a tick x 7 = 266, the measured "about
  22 degrees"; 63 degrees off: no turn).
- **Lunge** (measured): 125, 125, 125, 50, 44, 24, 24 a tick from 330 ms,
  with or without a target. Bodies stop it at contact.
- **Hits.** The game tests the blade line twice a tick over ticks 7-11
  and has no "already hit" guard, so a close target is hit twice. The
  engine crosses the front twice, 2 and 3 lunge ticks in, with reaches 450
  and 420; a hit ends the lunge and pushes the target 10 back. Fitted to
  the recomp's harness (11 swings at each distance): **2 hits from 650 and
  closer, 1 at 700, none at 800**. (An older measurement here, a miss at
  730, disagrees with the harness; the harness wins.)
- Swing to swing: ~1.45 s (measured).
- Other blades' reach (MOONLIGHT is longer) not measured yet.

## Cannon stance

The VS cannon routine (`0x80081B18`) makes two-legged bodies crouch
(AC state 34) before a **kind-2** cannon fires; caterpillars fire at once;
Human PLUS level 4+ turns a two-legged body into body frame 3, which fires
cannons standing. While crouched the AC turns at the crouch rates (legs
+0x3C through the back-weapon slot formulas: accel `+7 x 5 / 32 + 1`, top
`+8 x 15 / 32 + 1`) and slides to a stop. Four legs: "only on the ground"
is from a player's description, not from code yet.

Changed: the two-slot energy cannons WX-ED2 and WX-C/4 are kind 0 (guns)
in the detail table, and the gun routine has no crouch check, so they no
longer require the stance. **To confirm in the recomp**: does a biped
kneel to fire them?

## In the engine

`src/gen1/acr_gen1_battle.{h,c}`; tests in `tests/test_gen1_battle.c`
(measured damage, rifle cadence, blade hit counts at 300-800 at both 22
and 60 steps a second, missile lock time, volleys, giving up, SP-JAM,
interceptor, round result).

Still stand-ins: the gun lock's distance factor, post-shot hold lengths
other than WG-RF35, knockback distances, the blade arc (fitted), the
four-legged cannon rule, cannon kick (player block +0x214) and recoil.

## Earlier measurements (kept)

- Lock box: at 4,000 ahead the rifle locks with the target 1,000 to the
  side and not 1,250 (tan 0.29-0.32). Box 240 -> tan 0.3125 with 384 units
  for 45 degrees (`ACR_GEN1_BOX_UNITS`).
- Range: the rifle (FCS class 0, 6,100) locks out to about 6,250 and the
  small missile (class 1, 8,000) to about 7,985.
- World scale: the game draws ACs at a quarter of their model's units
  (confirmed in the VS model draw `0x80062C00`), `ACR_GEN1_MODEL_SCALE`.
- A VS fight starts with the ACs 8,000 apart.
