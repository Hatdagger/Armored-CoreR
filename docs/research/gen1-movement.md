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
| +28 blade power | blade +0x2C | 2,050 |
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
(32, 75). Diagonal boosting is faster than straight boosting. Where the
limit is applied is not found yet (not a literal in the VS engine or the
EXE); it holds in every measurement.

**Walking is animation-driven.** Walking speed follows the legs' gait
animation: humanoid LN-1001-PX-0 repeats 31/37 units per step, reverse
joint LB-4400 repeats 32/48/32. Weight barely changes it (the stripped-down
AC walked 32/38 on the same legs). The engine needs each legs model's
animation root motion, which the importer can read from the disc.

**Rise take-off is animation-driven too.** After about 7 steps of crouch,
the AC leaves the ground with an upward speed taken from the jump
animation's motion (`0x8007B200`: displacement x 64 / frames): 3,200
(50 a step) on humanoid legs whatever the weight, 6,016 (94 a step) on
LB-4400. Rise acceleration then applies while the button is held, and
gravity after release.

The engine port is in `src/gen1/acr_gen1_compile.c`
(`acr_gen1_compile` and `acr_gen1_motion_from_stats`), tested against an
instruction-level transcription on 600 random builds.

## Still to measure

- Air boosting (+8/+9) and the rise top speed, which these runs did not
  reach; both are clear from the code.
- Boost and landing friction after release (`0x80014790` takes the
  acceleration / 4 as deceleration).
- Where the 75-per-axis limit is applied.
- Walking and jump animation root motion per legs model.
