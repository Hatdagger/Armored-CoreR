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
+0x40 legs: stability. Bytes 18-35 hold the game's type text ("HUMANOID
LEGS", "SNIPER RIFLE"...).

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
| +28 blade charge drain (energy a swing takes; was called "blade power") | blade +0x2C | 2,050 |
| +36 stability | (legs stability [+800 with SP-ABS, +1600 with SP-ABS_Re] x 4096 / 1018) x (carried weight x 256 / 1000 / 512) / 16384, limited to 32-255 | 38 |

Standing drain = head + arms + core + FCS drains + legs standing drain (2,706);
moving drain uses the legs' moving drain instead (4,009). Carried weight = all
parts except the generator, capped at legs weight + legs carry limit (5,496).
Integer division throughout (the game uses multiply-by-reciprocal tricks that
round down). Every value above was checked against the live compiled block of
three builds: the default AC, reverse-joint legs (LB-4400), and caterpillar
legs with no booster (LC-UKI60, whose rise drain comes from the legs). The C
port (`src/gen1/acr_gen1_compile.c`) matches an instruction-level
transcription on 600 random builds (`tests/test_gen1.c`).

**Correction:** +36 was first read as turn speed because it equals the
measured top turn rate (38) on the default AC. The two bits that raise it are
both Balancers ("Decreases Impact shock"), and it grows with weight, so it is
stability. The turn speed is most likely the byte pair at +10/+11 (38, 38),
which the battle engine scales at match start (x14 and x140); to confirm.

## Optional parts

13 options, 44-byte records at `0x800D1AA0` (code name, full name, u32 price,
u16 slot cost); bit n of the AC's option mask (`0x8003EA70` in the garage)
fits option n. A core's option slots limit the total slot cost, and the game
refuses to start a match with an illegal AC (both players are checked).

| Bit | Option | Effect (game's text) | Slots | Where it acts |
| --- | --- | --- | --- | --- |
| 0 | SP-MAW Radar option | Adds missile indicator to radar | 1 | flag copied to the compiled AC |
| 1 | SP-JAM Missile jammer | Missile lock-on interference | 3 | flag copied |
| 2 | SP-M/AUTO Auto launcher | Automatic missile launcher | 1 | weapon records |
| 3 | SP-ABS Balancer | Decreases impact shock | 1 | stability (+36) |
| 4 | SP-SAP Absorber | Decreases cannon recoil | 1 | weapon records |
| 5 | SP-CND-K Charge expander | Increases energy output | 4 | flag copied; applied in battle |
| 6 | SP-AXL FCS accelerator | Accelerates lock-on time | 2 | compile |
| 7 | SP-S/SCR Shell screen | Decreases projectile damage | 2 | shell defense |
| 8 | SP-E/SCR Energy screen | Decreases energy damage | 1 | energy defense |
| 9 | SP-EH Rapid charge | Increases energy weapon recharge | 1 | weapon records |
| 10 | SP-E+ Energy amplifier | Increases energy weapon power | 1 | weapon records |
| 11 | SP-DEtq Energy reduce | Decreases energy used | 5 | weapon records |
| 12 | SP-ABS_Re Balancer (not sold) | Decreases impact shock | 2 | stability (+36) |

Effects that are only flags (radar, jammer, charge expander) do not change
the AC's listed stats; the battle engine applies them during a fight.

Note: the AC happened to rise exactly 2,050 units in the rise test, the same
as +28 (blade power); that was a coincidence, not the rise height.

## Movement stats: compile bytes +6..+14

Also from the compile routine (`0x800A8038-0x800A8238`), and checked
against the live compiled block of 8 builds (all match, including a
caterpillar, reverse-joint legs, a heavy booster and a stripped-down AC).
`W` is the carried weight above. Booster power is booster `+0x2C`; the game
uses a flat 9,800 for caterpillar legs (they carry no booster).

| Byte | Formula | Default AC |
| --- | --- | --- |
| +6, +8 thrust | power x 4096 / W, then x 74898 >> 24 (about / 224); 1-64 | 32 |
| +7, +9 | thrust x 8 / 10; 1-50 | 25 |
| +10, +11 | thrust x 12 / 10; 1-64. Fixed instead for legs 21 (26), 22 (23), 23 (19), 24 (26), 26 (24), 27 (6), 28 (7), 30 (13), 31 (11), 32 (13) (`0x800AA96C`) | 38 |
| +12 turn acceleration | legs `+0x38` x 3 x 65536 / 160 x 3573 / W >> 12; 1-64 | 22 |
| +13 turn speed | legs `+0x3A` (byte) x 14635008 / W >> 12; 0-64 | 41 |
| +14 friction | W x 8 / 1000; 32-96 | 43 |

"Thrust" uses the unclamped value for +7 and +10, so a strong booster on a
light AC caps +6 at 64 and +7 at 50 independently.

## How the battle engine uses them

At match start the VS engine (`0x8008342C`, FDAT 209) scales them into the
player block (+0x200 on). Speeds are in 1/64 world units per step; position
moves by speed / 64 each step.

| Value | From | Default AC | Use |
| --- | --- | --- | --- |
| Rise acceleration | +6 x 2 | 64 | added to upward speed per step while rising |
| Rise top speed | +7 x 200 | 5,000 | upward speed limit |
| Air boost acceleration | +8 x 14 | 448 | horizontal, boosting in the air |
| Air boost top speed | +9 x 140 | 3,500 | |
| Ground boost acceleration | +10 x 14 | 532 | horizontal, along the stick direction |
| Ground boost top speed | +11 x 140 | 5,320 | limit on the length of the speed vector |
| Turn acceleration | +12 x 5 / 16 | 6 | facing units per step, per step |
| Top turn speed | +13 x 15 / 16 | 38 | facing units per step |
| Ground friction | +14 | 43 | taken off each horizontal axis per step |
| Gravity | fixed | 256 | added to downward speed per step |
| Fall limit | fixed | 9,600 | |

Measured in the game (VS Mode, parts set before VS START):

| AC | Turn (accel / top) | Ground boost (accel / top) | Rise accel |
| --- | --- | --- | --- |
| Default | 6 / 38 | 531 / 5,340 | 64 |
| Light (no weapons) | 8 / 46 | 644 / 6,451 | 78 |
| LB-4400 + B-PT000 | 6 / 33 | 897 / 9,000 | 128 |

All as predicted (boost overshoots its limit slightly for one step before
the limit is applied).

**Ground speed limit per axis.** Position never moves more than 75 units
per step along x or along z, whatever the speed: the LB-4400 build reached
speed 9,000 (140 a step) but moved 75 on each axis; the light AC moved
(32, 75). Diagonal boosting is faster than straight boosting. Taken as
measured (where the game applies it was not searched for further); the
engine keeps it as `acr_gen1_motion.axis_step_max`, so a ruleset can raise
or remove it.

**Walking, strafing and the jump take-off are animation-driven.** Each
animation carries a root-motion track: keys of (frame, x, y, z), the
root's displacement since the animation began, in 1/4 world units. The
game moves the AC by the difference between keys, spread over the frames
between them by an integer stepper (EXE `0x800135DC`, run per step by
`0x80054FF8`), one animation frame per step.

| Default AC (set 0) | Track | Per step |
| --- | --- | --- |
| Walk (anim 2, 16 frames) | z -249, -499, -799, ... every 2 frames | 31, 31, 37, 38 ... (measured 31/37) |
| Jump (anim 22) | 7 frames still, then y -399 over 2 frames | take-off 3,200 = 50 a step (measured) |
| LB-4400 walk (set 48) | z -382, -765 ... every 2 or 3 frames | 48, 32 (measured 48/32) |

Weight does not enter, which is why the stripped-down AC walked almost
the same as the default.

**Animation sets.** The game picks one of 72 sets from the core, arms and
legs (table at `0x800D4A64` in the menu module: core, then lists of arms
and legs indices; cores 3 and 4 count as core 0). Set n is entry 1 of
`GG/P<n/20>/PA<nn>.T`. Animations seen in play: 0 idle, 1/2 walk start and
loop, 8/9 strafe left, 11/12 strafe right, 14/15 walk back, 21 turn,
22 jump, 24 in the air, 48/49 boost. `acr-import` writes every set's tracks
to `<out>/motion/model-NN.toml` and the table to `motion/models.toml`; the
engine side (`src/gen1/acr_gen1_anim.{h,c}`) has the stepper and track
lookup.

The engine port is in `src/gen1/acr_gen1_compile.c`
(`acr_gen1_compile` and `acr_gen1_motion_from_stats`), tested against an
instruction-level transcription on 600 random builds.

## Air boost, rise limit and slowing down (measured)

Same method, default AC and LB-4400 + B-PT000:

| What | Default AC | LB-4400 + B-PT000 | Formula |
| --- | --- | --- | --- |
| Rise top speed | 5,000 (78-79 a step) | 10,000 (156-157 a step) | +7 x 200; no per-axis limit going up |
| Air boost acceleration | 448 | - | +8 x 14 |
| Air boost top speed | 3,500 (54 a step) | - | +9 x 140 |
| Air boost release | -112 to -113 a step | - | +8 x 14 / 4 |
| Ground boost release | -532 a step | -896 to -900 a step | the ground boost acceleration, +10 x 14 |
| Gravity | 256 a step | 256 a step | fixed |

- Holding X with a direction in the air keeps rising (rise acceleration
  still applies) while the air boost pushes sideways.
- Rising stopped on its own after a few seconds of holding X in both runs,
  when the energy ran out.
- After letting go of a ground boost the AC kept full speed for 2-3 steps,
  then slowed by its boost acceleration each step to a stop (about 10 steps
  for the default AC). The earlier note "coasts at 75 for about 10 steps"
  was the 75-per-axis limit hiding the first part of the slowdown. Part of
  the 2-3 steps may be input delay in the test driver.
- The ground friction byte (+14) has no effect in VS Mode: the only code
  that uses it there (`0x8007DFC0`, take +14 off each ground axis per
  step) is never called. The arena engine (FDAT 210) reads it in its
  opponent AI and AI setup instead. The engine keeps it as
  `acr_gen1_motion.friction` but VS rules do not apply it.

## Landing (measured)

AC states (object +0x45): 0 standing, 1 walking, 64 jump take-off, 65
rising, 75 falling, 97 landing.

- Touching down stops the AC dead: any sideways speed left is dropped
  and it does not slide.
- After a short hop the default AC stayed in the landing state for 56
  screen refreshes (about 20 steps) before it could move again.
- Longer falls (both ACs after a full rise) were still landing when the
  recording ended, so whether recovery grows with fall speed is open.

## Energy rules (VS engine `0x80082D90`)

Read from the code after a player's report; matches what players know.

- Boosting costs the rise drain (+20) each step; a ground boost costs half.
- Not boosting, the generator refills: the moving or standing rate (+26 or
  +22) depending on what the AC is doing.
- Below the red zone (+24), both the cost and the refill run at a quarter,
  so the last part of the bar lasts longer.
- If the bar runs dry (cost greater than what is left), the booster is
  locked (player block +0x18, bit 0). The bar then refills at half the
  usual rate, and the lock lifts only when it is completely full.

## From the AC1Recomp mapping (2026-11-18)

- **SP-CND-K** adds 12,000 to the energy store (compiled +0x12).
- **Human PLUS level 6+** at match start (`0x80083560`): rise drain halved,
  both refills doubled, each then capped at the new rise drain
  (`acr_gen1_human_plus_energy`; the test build uses level 9, "full").
- **Crouched turning**: back weapon slots +7 / +8 are the +12 / +13 turn
  formulas with legs +0x3C (the turning-with-a-back-weapon figure); the
  selected back weapon gives player block +0x210 = +7 x 5 / 32 + 1 and
  +0x212 = +8 x 15 / 32 + 1 (hand weapons 11 / 31), used only while
  crouched in the cannon stance (`0x8007E3E0`, which also bleeds speed by
  1/8 a step). Engine: `acr_gen1_motion.crouch_turn_*`.
- **The 75-per-axis limit** is three quarters of the AC's width (AC object
  +0x28 = 100, from an object-type record, not the parts): a move step is
  cut to it to keep ACs out of walls. With width 150 the default AC reaches
  its true top speed, 83 a step.
- **Boost** is player block +0x298's mask (Cross by default).

## Movement pass (2026-11-18)

**Ticks at any step rate.** The engine's movement (`acr_gen1_ac_tick`) is
one Gen 1 tick of the rules above, integer for integer. At 60 steps a
second the ticks fall between steps (22 in every 60), so the AC moves
exactly as it would at 22 steps a second: `tests/test_gen1_sim.c` runs a
script (walk, ground boost, slide, hop, air boost while turning, fall,
landing, jump, rise) both ways and compares the state after every tick.
The AC is drawn from `sx, sy, sz, syaw`, which slide from the tick before
to the latest one; the fight reads the tick state. Before this, boosting,
rising, gravity and the air boost ran a whole tick's worth every step, 2.7
times the game's pace at 60.

Rules added from the AC1Recomp mapping:

- **The booster from a stand jumps**, direction or not; a direction and the
  booster boost along the ground only once the AC is already moving
  (walking, or sliding after a boost). Recomp, VS AI chat: "✕ on the first
  step still jumps".
- **Refill classes** (`0x80082D90`): the moving refill is for movement
  class 0x10 (walking) and 0xB0 (ground boost); the air (0xC0) and turning
  on the spot (2) get the standing refill. The engine used the moving
  refill in the air.
- **Refill overshoot**: the refill is added before the cap is checked, so
  a full bar reads up to one refill over for a tick, then the capacity
  (measured +210 then -57). The booster lock lifts on the tick the bar is
  found full, after that overshoot.
- **The 75 limit in the air**: every movement handler loads its step
  through `0x80054124`; the air boost's measured 54 a tick rules out the
  half-size limit there, so the air uses the same 75 as the ground (it
  only matters for a strong booster on a light AC, or a hop carrying a
  fast ground boost).
- **Cannon stance slide** (`0x8007E3E0`, an eighth a tick, zero below 512)
  is part of the tick (the `crouch` input), not the fight's step.

## Still to measure

- Landing recovery after longer falls, and whether it depends on fall
  speed, weight or the legs. The engine's recovery curve below a 5,000
  touchdown is a stand-in. The game's landing states (0x60 / 0x61) end in
  the animation-finished callback `0x8008389C`, so recovery may simply be
  the landing animation's length (animation 36).
- Whether sideways speed survives a touchdown. "Landing (measured)" above
  says it is dropped; the engine keeps it as a slide because that
  measurement had none left. Test: air boost forward and let go just
  before landing.
- The 2-3 ticks at full speed after letting go of a ground boost: with the
  75 limit hiding the first slowdown tick, the rest may be the test
  driver's input delay rather than a rule.
- The boost hop (release and re-press while sliding): from a player's
  description, not measured.
- Diagonal walking (forward or back with a strafe).
- Caterpillar and four-legged ACs: the mapping gives them the moving
  refill only for class 0xB0 with speed; whether their walking is class
  0x10 (standing refill) or 0xB0 is not known.
- Turning while landing (the engine doesn't), and during the jump's
  take-off (the engine does).
- Knock-back and stagger movement: the handlers with the half-size step
  limit (50 / 106: `0x8007B108`, `0x8007B454`, `0x8007BC6C`,
  `0x8007E714`).
- The exact rounding from track units (1/4) to world units when the AC
  faces an angle (the track is rotated by the facing before stepping).
