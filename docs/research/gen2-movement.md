# Gen 2 movement, from the game's code

How an AC moves in Armored Core 2 (USA, SLUS_200.14), read from the executable's AC
setup and state code, with Another Age (SLUS_202.49) compared. Companion to
[gen2-archive.md](gen2-archive.md) (parts, motions, weapon effects) and the Gen 1
counterpart [gen1-movement.md](gen1-movement.md). Script: `tools/gen2/movement.py`.
C port: `src/gen2/` (`acr_gen2_compile`, `acr_gen2_move`), tests `tests/test_gen2_move.c`.

Everything here is from the code and the disc's data. How far each part is checked:

- **The compile** (parts to numbers) is checked against the game's own code run in a
  MIPS emulator: 131 builds, all 32 values of the movement block, no differences
  (`tools/gen2/emu_compile.py`).
- **Turning, braking, the speed limit, falling, rising, the direction numbers and the
  whole energy routine** are checked the same way: 4000 random cases through the game's
  routines and the C port's, no differences (`tools/gen2/emu_move.py`).
- **The states and what the buttons do in them** are read from the code and state
  tables, not run.
- **Nothing has been measured in the running game.** "Unconfirmed" at the end lists what
  to check and gives numbers a recording can confirm.

## Units

- One game update is a **frame**; AC2 runs 60 a second. Speeds below are per frame.
- **1 unit = 1 metre.** The HUD speed readout (`0x11b1cc`) is
  `speed x 60 x 60 x 60 x 10 / 1000`, drawn as `%3d.` and one more digit: so
  **km/h = units per frame x 216**, with one decimal.
- y points down (up is negative). An AC at yaw 0 faces -z; +x is its left. Angles are radians.
- The speed shown is smoothed: each frame it moves a tenth of the way to the smaller of
  "distance covered this frame" and "ground speed" (`0x138f08`, unit +0xd80).

## Where the state lives

Each AC is a 0x38e0-byte unit (`0x2bb8f0 + n x 0x38e0`; the current one's pointer is at
gp-0x4d44). The debug menu's "tuning" block is the compiled AC inside it, at unit +0x730.

| Field | Where |
| --- | --- |
| Position, previous position | unit +0x50, +0x70 |
| Yaw, turn speed | unit +0x64, +0xa8 |
| Velocity x, y, z | unit +0x90, +0x94, +0x98 |
| Energy | unit +0x08 (int) |
| Flags (bit 2 = charging after running dry) | unit +0x10 |
| State number, sub-step, queued state, frames in state | unit +0x0c, +0xce0, +0xce2, +0xce4 |
| Buttons held / just pressed | unit +0xb4 / +0xb8; masks at +0x730 (forward), +0x732 (back), +0x734 / +0x736 (turn), +0x73a (booster), +0x740 (left), +0x742 (right) |
| Compiled AC | unit +0x730 (leg kind +0x70, pilot kind +0x71, weight +0xd8, upper weight +0xd4, movement block +0x410..+0x490) |

## The compile: parts to movement numbers

Run when an AC is set up (`0x145180`). Every "this part against the standard part"
goes through one helper (`0x146220`):

`factor(x, k, lo, hi) = 1 + (x - 1) x k`, limited to lo..hi.

Part fields are the record's u16 words (wN, gen2-archive.md "Parts record stat fields").
The PS2's FPU cuts every result off towards zero instead of rounding to nearest, and
float to int conversions do the same, so 18000 x 0.033 is 593, not 594. The script and
the C port do every operation that way.

**Weight** (`0x146120`): the sum of every fitted part; an AC over `legs weight + legs max
weight (w12)` counts as exactly that. Upper weight = weight - legs weight.

**Leg kind** (unit +0x7a0, from the motion set number): 0 two legs (humanoid and
reverse joint), 1 caterpillar, 2 four legs, 3 hover. It picks the state table
(`0x3c8368` two legs, `0x3c8020` caterpillar and four legs, `0x3c86d8` hover) and the
formulas below.

**Booster used** (`0x145710`): caterpillars, hovers and any AC with no booster use the
legs' own (legs w19 power, w20 charge drain; every caterpillar and hover leg has them).
Everyone else: booster w7 power, w8 charge drain.

| Value (the game's own label) | Formula | Standard AC |
| --- | --- | --- |
| speed factor | `factor(7357 / weight, 0.65, 0.2, 1.18)` x `factor(power / 14800, 1.0, 0.9, 1.15)` | 1.0 |
| `flymv_accel`, `flymv_maxspd` (air, sideways) | 0.045, 1.2 x speed factor | 259 km/h |
| `flyup_accel`, `flyup_maxspd` (rising) | 0.015, 0.8 x speed factor | 173 km/h |
| `dash_accel`, `dash_maxspd` (ground boost) | 0.047, 1.3 x speed factor | 281 km/h |
| `dash_brake` | 0.06 x `factor(legs braking w14 / 100 [x 2 with SP-CBRK], 1.0, 0.3, 2.0)` | 0.06 |
| caterpillar, four legs: `dash_*` instead | 0.033, 0.8 x `factor(legs moving speed w13 / 350, 0.7, 0.01, 2.0)` | 173 km/h |
| hover: `flymv_*` instead | the same two values | |
| `oboost_spd0` = `oboost_maxspd` | 4.0 x `factor(core OB power w14 / 3000, 0.8, 0.5, 1.0)` | 864 km/h at 3000 |
| `oboost_acc` | `oboost_spd0` x 0.05 | |
| `turn_maxspd` | 0.0155 x `factor(5602 / upper weight (1 when lighter), 1.6, 0.01, 1.0)` x `factor(int(legs w22 [x 1.2 with SP-CIR-K]) / 100, 1.0, 0.8, 2.0)` | 53 deg/s at w22 = 100 |
| `turn_accel` | `turn_maxspd` x legs w21 / 100 (w21 is 10 on every leg: top turn speed in 10 frames) | |
| `canoturn_maxspd`, `canoturn_accel` (kneeling to fire) | 0.011 x the same two factors; x legs w23 / 100 | |
| `jumpspd` | legs jump (w11 high byte) x 1.15 / 100 | 1.32 at 115 |
| `dashjumpspd`, `gravity`, `fall_maxspd`, `dash_mintime` | 0.55, 0.042, 1.74 (376 km/h), 13 frames: the same for every AC | |
| `land_dmgspd` | 1.48 for every AC. A formula from legs landing stability (w15) and upper weight runs first (`0x1466f0`) but is overwritten (`0x145ab8`), so **landing stability does nothing** | |
| `blade_spd0`, `blade_brk` | 3.0, 0.3 for every AC | |

The booster factor has a floor and a ceiling, so AC2's seven boosters fall into three
steps: 13,320 power and under all give 0.9 (EBT-GE, EBT-GE2100, EBT-GR4300), 14,700
gives 0.99 (EBT-V55), 17,020 and over all give 1.15 (ZBT-H4/T, ZBT-GEX/3000,
ZBT-Z1/ARTERE). They still differ in cost.

**Energy** (`0x1468c8`). Drain = the energy drain (w1) of every fitted part; the
standing drain swaps the legs' w1 for their stationary drain (w18).

| Value | Formula | Limits |
| --- | --- | --- |
| `boost_max` | generator max charge (w8), + 12000 with SP-BCNDR | |
| `boost_redarea` | generator red zone (w9) | |
| `boost_sub` (booster, a frame) | charge drain x 0.033 | at least 1 |
| `boost_add` (refill standing, a frame) | (generator output w7 - standing drain) x 0.027 | 50 to 384 |
| `move_boost_add` (refill moving) | (generator output - drain) x 0.027 | 1 to 192 |
| `oboost_1stsub` (overboost ignition) | core w15 x 0.033 (593 to 791) | |
| `oboost_sub` (overboost, a frame) | core w16 x 0.033 (118 to 221) | |

Then: a refill faster than `boost_sub` is pulled halfway down to it (both refills). Pilot
kind 5 and up (some enemy pilots; the player is 0) get twice the refill and half the
booster cost. The 1-384 and 1-192 limits are Gen 1's own (gen1-movement.md, compiled
+22 and +26).

**Optional parts**: bit n of the option mask (build +0x1c) is the n-th optional part
in the shop (F 3750 + n). Bit 0 SP-S/SCR shell defence x 1.15, 1 SP-ENE-SCR energy
defence x 1.15, 2 SP-BCNDR + 12000 energy, 3 SP-BSI-LE (+1500, impact), 4 SP-SAB/J
(+800, cannon recoil), 5 SP-CIR-K turning x 1.2, 6 SP-BE++ x 1.15, 8 SP-BF5/LOSP
x 0.85, 9 SP-ENE-ACC x 0.833, 10 SP-E/SAVER x 0.75, 13 SP-BLS x 1.08, 14 SP-CBRK
braking x 2, 15 SP-VIECH x 1.2. Only bits 2, 5 and 14 touch movement.

## One frame

`0x139050`, in order:

1. The state's own handler, then (unless the handler switched them off) what the held
   buttons ask of that state. A state change takes effect at once: the new state's
   button rules run the same frame. Frames-in-state goes up only when nothing changed.
2. The move and the ground.
3. Gravity, for every state except jump take-off, air, air blade and overboost (those
   handle their own).
4. Energy, then heat.

Four small routines do nearly all the work:

- **Turn** (`0x140af8`, accel, top): a held turn button adds accel to the turn speed;
  with none held it drops by accel towards 0; limited to top; added to yaw.
- **Push** (`0x140bb8` / `0x140d18`, direction, accel, top): adds accel along one of
  eight directions of the AC's own frame, then holds ground speed to top. An AC already
  above top keeps its speed and loses `flymv_accel` a frame instead: that is how an
  overboost's speed bleeds off.
- **Brake** (`0x140c58`, amount): ground speed down by amount, to a stop.
- **Fall** (`0x140e58`): vertical speed up by `gravity` to `fall_maxspd`. **Rise**
  (`0x143858`): vertical speed down by `flyup_accel` to `-flyup_maxspd`.

Directions (`0x13fa68`): 0 forward, 1 forward-left, 2 forward-right, 3 left, 4 right,
5 back, 6 back-left, 7 back-right, 8 none.

## States and buttons

State numbers are the game's. "Booster" means the button is held, the AC is not
charging, and a direction counts first when both are listed.

### Two legs

| State | Each frame | Buttons |
| --- | --- | --- |
| 0 stand | brake `dash_brake` | booster with no direction: 7 (jump). Booster with a direction: 5. Direction: 1. Turn: 4. Run dry, booster, no direction: still 7 (a plain jump) |
| 1 walk start | the start clip's own movement; no turning; buttons ignored for 7 frames; at the clip's end go to 3, or to a queued state | booster: queue 5. Turn only: 4. Nothing: 0 |
| 3 walk | turn; the walk clip's own movement | booster: 5. No direction: 0 |
| 4 turn on the spot | turn; brake | direction: 1. Nothing: 0. **The booster does nothing here** |
| 5 ground boost | turn; push `dash_accel` to `dash_maxspd`. The stick steers while the button is held; with no direction it keeps going the way it was. Ends when the button is let go (or the AC runs dry, or no direction for 13 frames): not before frame 16, and then after 14 more frames of boosting, into 6 | let go, then booster again before the slide: hop into the air at `dashjumpspd` (state 8) |
| 6 slide | turn; brake `dash_brake`. At least 21 frames, then 17 more once stopped, then 0 | none: it cannot be cut short |
| 7 jump take-off | turn; when the squat clip (motion 35, 20-28 frames by set) ends: ground speed 0, vertical speed `-jumpspd`, state 8 | none |
| 8 air | turn; a direction pushes `flymv_accel` to `flymv_maxspd`; none brakes at a quarter of `flymv_accel` | booster: rise. Otherwise: fall. Never both |
| 9 landing | brake; turn; ends with the landing clip (motion 38, 16-24 frames) | a direction goes straight to 3 |
| 10 hard landing | brake; no buttons until motion 51 (52 frames) ends | none |
| 16 overboost | first frame: push `oboost_spd0`; after: push `oboost_acc`; top `oboost_maxspd`; turn. Booster held: rise, else fall | |

- Walking speed is **not** the legs' moving-speed stat. See "Walking" below.
- Touching down (`0x13f494`): faster than `land_dmgspd` (1.48) means the hard landing,
  with ground speed cut to 30%; otherwise the normal one, with ground speed cut to 70%
  if falling faster than a third of `fall_maxspd`. From rest it takes 35 frames of
  free fall (about 26 m) to reach 1.48. Caterpillars never land hard; hovers do not
  land at all (straight to stand).
- Overboost moves forward, or sideways while a strafe is held (`0x3c7ee8`); it starts
  90 frames after the button (`0x141df8`) if the AC then has `oboost_1stsub` energy.

### Caterpillar and four legs

Same table shape, different ground rules (`0x143a70`): states 1 and 3 are one glide
(turn; a direction pushes `dash_accel` to `dash_maxspd`, none brakes `dash_brake`). The
booster never boosts along the ground: with no direction four legs jump (state 7,
caterpillars cannot), otherwise it lifts the AC straight off (vertical speed
`-flyup_accel`, state 8).

### Hover

Standing and turning brake at a quarter of `flymv_accel`; moving pushes `flymv_accel`
to `flymv_maxspd` (their moving-speed values). The booster with a direction is the
ground boost (state 5, on the legs' own booster); with none it lifts off. Hovers can
boost again out of the slide.

## Energy

`0x139c50`, every frame:

| Doing | Energy a frame |
| --- | --- |
| Booster held in the air | - `boost_sub` |
| Booster held in a ground boost | - `boost_sub` / 2 |
| Overboost | - `oboost_sub`, and - `boost_sub` / 2 more with the booster held |
| Standing | + `boost_add` |
| Two legs walking, or in a ground boost with the button let go | + `move_boost_add` |
| Anything else (in the air without the booster, gliding, sliding) | + `boost_add` |

- **Red zone** (energy under `boost_redarea`): both the cost and the refill drop to a
  quarter.
- **Running dry**: a cost the bar cannot pay empties it and sets "charging". While
  charging the booster does nothing and the bar refills at **half** rate; it clears when
  the bar is full.
- A swing of the blade costs `boost_blade_sub` once (left-arm part w8).

## Walking

For two legs the walk state moves the AC by the walk clip's own root movement
(`0x127f38` takes the root node's step from one frame to the next, `0x144a40` applies
it). The legs' moving-speed stat (w13) is read only for caterpillars, four legs and
hovers. So **walking speed is set by the motion set** (B 421-492, picked from the core,
arms and legs groups: gen2-archive.md "Set choice").

Every walk loop covers 30 units forward; what differs is how many frames it takes.

| Legs group | Core 0, arms 0 | Lightest (core 2, arms 1-2) | Back | Start clip |
| --- | --- | --- | --- | --- |
| 0 heavy humanoid (ZLN-2A/GR, ELN-STVM, ZLN-9001/A) | 64 frames = 101 km/h | 58 = 112 km/h | 15 units in 76-68 | 20-18 frames, 1.5 units |
| 1 mid humanoid (ZLN-XX0/TP ... ZLN-XA2/FF) | 56 = 116 km/h | 50 = 130 km/h | 18 in 68-60 | 18-16 |
| 2 light humanoid (ZLN-EK1/SRRT, ELN-701, ZLN-WA02/SS) | 42 = 154 km/h | 36 = 180 km/h | 18 in 54-48 | 14-12 |
| 3 heavy reverse joint (ZLB-7744/VAT, ELB-XXE-LAPIN) | 56 = 116 km/h | 50 = 130 km/h | 18 in 68-60 | 18-16 |
| 4 light reverse joint (ELB-A1001, ZLB-3322/OP, ELB-5005B) | 50 = 130 km/h | 44 = 147 km/h | 18 in 60-52 | 16-14 |

Lighter cores and arms shorten the loop one step at a time; `movement.py --walk` lists
all 72 sets with strafe speeds and the squat and landing lengths. Within a group the
moving-speed stat barely varies (266-275 on the mid humanoid legs), which fits: it
describes the group's clip rather than driving anything.

## Armored Core 2 against Another Age

Same code, same constants, except eight movement values (`movement.py SLUS_200.14
SLUS_202.49`):

| Value | AC2 | Another Age | Effect |
| --- | --- | --- | --- |
| refill rate | 0.027 | 0.02 | refills a quarter slower |
| booster cost rate | 0.033 | 0.038 | boosting and overboost cost 15% more |
| standard booster power | 14800 | 15000 | |
| weight factor: k, top | 0.65, 1.18 | 0.75, 1.4 | light ACs gain more |
| booster factor: floor, top | 0.9, 1.15 | 0.8, 1.47 | boosters spread out: 12,000 to 22,050 power all count |
| air speed (std) | 1.2 | 1.04 | slower in the air at standard weight |
| air acceleration (std) | 0.045 | 0.042 | |
| rise speed (std) | 0.8 | 0.75 | |

Ground boost, braking, turning, jumping, gravity, falling, landing and overboost
values are identical. **AC2 stays the base.** Another Age is a second tuning table
(`acr_gen2_tuning_aa`), not different rules.

## Against Gen 1

Gen 1's default AC (gen1-movement.md, 22 steps a second, AC 212 world units tall)
beside a mid-weight Gen 2 AC (the "mid two legs" build in `movement.py`, 60 frames a
second, test AC 10.16 units tall), in AC heights:

| | Gen 1 | Gen 2 |
| --- | --- | --- |
| Walk | 34 a step = 3.5 heights/s | 0.6 a frame = 3.5 heights/s |
| Ground boost | 75 a step = 7.8 | 1.27 a frame = 7.5 |
| Rise | 50 a step = 5.2 | 0.78 a frame = 4.6 |
| Turn | 72 deg/s | 60 deg/s |
| Gravity | about 4 a step, each step = 9 heights/s^2 | 0.042 a frame, each frame = 15 |
| Jump take-off wait | about 9 steps = 0.41 s | 26 frames = 0.43 s |
| Ground boost against air cost | half | half |
| After a ground boost | coasts about 10 steps, then slides | boosts on 14 frames, then a slide of 38 frames or more |
| Refill limits | 1-384 standing, 1-192 moving | the same |

What is the same: the shape of the rules (the booster alone jumps, with a direction it
boosts along the ground at half cost; walking by animation; two refill rates) and,
measured in the AC's own height, the walk, boost and rise speeds.

What Gen 2 adds or changes: overboost; a red zone that quarters cost and refill; a
charging lock when the bar empties; heavier gravity with a top fall speed; a hard
landing that only a long fall causes; the slide after a ground boost cannot be
cancelled; no booster while turning on the spot; speed that depends on weight and
booster through one factor with hard limits.

### Scale

AC2 gives its own answer for size: 1 unit is 1 metre, so the test AC is 10.2 m tall.
Gen 1 has no metre of its own that we have found. With both ACs drawn the same height
(the x83.5 in gen2-models.md), walk, ground boost and rise speeds agree within about
10% across the generations, so **same height is consistent with how both games move**
and there is no evidence here for making Gen 2 ACs smaller or larger. By that scale
1 m = 20.875 Gen 1 world units. The table at `0x3c8a20` holds one more per-leg-kind
length (11.5 two legs, 10.0 four legs and caterpillar, 12.5 hover; compiled AC +0x98)
whose use is not traced: probably a body or camera height.

## In Armored-CoreR

- `src/gen2/acr_gen2_compile.c`: the compile above. The unit test holds 38 builds (seven
  picked by hand, one per legs part) with the values `movement.py` and the emulated game
  code agree on.
- `src/gen2/acr_gen2_move.c`: one call per AC2 frame; the states and energy above on a
  ground plane.
- `src/gen2/acr_gen2_fp.h`: single-precision arithmetic with the PS2's rounding, done by
  hand (each operation exact in a double, then cut to a float). It gives the console's
  bits and does not depend on the machine's float settings: a 3000-frame scripted run
  has the same checksum from gcc and clang at every optimisation level tried. Not yet
  tried on the Xbox's x87.
- The gait (walk speeds, squat and landing lengths) is an input: it comes from the AC's
  motion set, so `acr-import` should write it next to the Gen 2 AC.
- **Wired into AC TEST** (`src/gen2/acr_gen2_link.c`, branch `gen2-movement-wire`):
  with the Gen 2 test AC on (F10 / BACK+L3) the opponent moves by these rules. Its
  pilot's Gen 1 controls (the AI's or player 2's) become AC2 buttons (move, strafe, the
  two turns, booster; F12 = overboost, as Gen 1 has no button for it); the Gen 1 rules
  get no movement input for it, and after each Gen 1 step the link runs that step's
  share of 60 AC2 frames a second (so 22 or 60 steps a second give the same path) and
  writes the body back as the Gen 1 AC's position and facing. Hits, AP, lock-on, the
  AI and the HUD stay Gen 1's and follow the body. Gen 1 energy is not drained for it;
  the AC2 energy is shown on the AC TEST help line.
- Units and axes: 1 AC2 unit = 20.875 Gen 1 world units = 1,336 Gen 1 sim units. Both
  have y down; AC2 faces -z at yaw 0 with +x on its left, Gen 1 faces (sin, cos), so
  Gen 1 x, z = -(AC2 x, z) and the two yaws are the same angle (both grow turning right).
- Stage: the floor under the body is Gen 1's floor probe (the game's 75-wide box; the
  drawn polygons' floor if a stage has no game collision), dropping up to 50 world units
  is walked down, walls are the Gen 1 stage move with the box raised by the 75 step-up
  (lower ledges are climbed), ceilings stop a rise. Open floor: the arena square. These
  are Gen 1's rules lent to a Gen 2 AC: AC2's own stage collision is not read yet.
- The drawing follows the AC2 state (stand 47, walk start / loop 1-16 by direction, turn
  33 / 34, ground boost 17-24, slide 25-32, squat 35, air 37, landing 38, hard landing
  51, overboost 63-65) instead of guessing from the position.
- Gait: taken from the imported `ac.glb` clip lengths (walk loop 30 units, back 15,
  start 1.5; squat, landing and hard landing lengths), with the walk table's B 453
  numbers when there is no import. `acr-import` writing the gait itself is still to do.
- Tested by `tests/test_gen2_link.c` (axes, 101 km/h walk, 22 vs 60 steps, jump, arena
  edge, re-sync). **Not yet played in the running game.**

## Unconfirmed

Read from code but not run, in the emulator or the game:

- **Yaw direction.** The rotation helper is VU0 code this disassembler does not read;
  which turn button is left, and the sign of the rotation, are assumed.
- **How an overboost ends.** The start (90 frames, ignition cost) and the flight are
  read; the off switch is not. The C port ends it on a second press or an empty bar.
- **Landing and hard-landing lengths** include a blend whose time is filled in at run
  time (unit +0x7c0); the port uses the clip lengths alone.
- **The move itself** (position += velocity, walls, slopes) is inside the collision
  code (`0x13ee38`), not read. The HUD formula implies position moves by velocity each
  frame.
- States 2 (idle), 11-13 (blade), 14-15 (kneel), 17-19 (hit, destroyed) are outside
  this pass.

Numbers a recording of AC2's HUD can confirm (any build: `movement.py SLUS_200.14
--parts P.json --build "..."`):

| Check | Prediction |
| --- | --- |
| Walking forward, mid humanoid legs, heavy core and arms | steady 115.7 km/h |
| Walking forward, heavy humanoid legs (ELN-STVM) | 101.2 km/h |
| Ground boost, top speed, at 7357 total weight with EBT-V55 | 278.9 km/h |
| Overboost with a 3000-power core (ZCL-XA/2) | 864.0 km/h |
| Standing turn | reaches top speed in 10 frames |
| Booster while turning on the spot (two legs) | nothing until the turn is let go |

## Corrections to earlier notes

- **Motion 35** is the jump take-off squat (state 7), not "walk stop".
- **States 11 and 13** are the blade on the ground and in the air (entered by the
  left-arm action, cost `boost_blade_sub`, use `blade_spd0` / `blade_brk`), so motions
  49 then 66 and 50 then 67 are blade swings, not overboost. State 12 shares their exit
  handler: probably the blade's recovery.
- **State 16** is the overboost (it reads `oboost_spd0`, `oboost_acc`, `oboost_maxspd`
  and drains `oboost_sub`), so motions 63-65 are overboost poses (forward, left, right),
  not blade swings.
- **Core w15** is the overboost's ignition cost and **w16** its cost a frame (both
  x 0.033); w16 was unnamed.
- **Legs w19 / w20** are the legs' own boost power and charge drain, on caterpillars as
  well as hovers. **w21** and **w23** are turn accelerations (10 on every leg).
- **Booster w7 / w8** (boost power, charge drain) are confirmed by the code.
- **Legs landing stability (w15)** is computed and then thrown away; **moving speed
  (w13)** is not used by two-legged ACs.
