# Feel pass: sounds and HUD

Branch `feel/sound-hud` (2026-11-27). Goal: the fight's sounds and the HUD
respond like Master of Arena. Done unattended: no play-testing, every call
is written down here. Presentation only: nothing in the fight's rules
changed, so replays and netplay are untouched (`ACR_REPLAY_RULES` stays).

Sources, in the order they win:

1. **Disc**: MoA (USA) `GG/COM/FDAT.T` 209 (VS engine) and 210 (arena
   engine), loaded at `0x80050DD8`, read with a MIPS disassembler; the
   sound bank and id table in FDAT 205; `acr-import`'s `motion/models.toml`
   for which animation set goes with which legs.
2. **MoA research** (`ma-work`; `docs/research/recomp/sound-map.md`).
3. The user's own rules (AP red below 1,000, clock above the radar, REC).

Prerequisites: `feel/lockon` (#39), `feel/weapons` (#41), `feel/blade-hit`
(#40) and `feel/camera` (#38) are merged. `feel/animations` (#42) merged
while this pass was under way, so the footsteps were moved onto its
animation frames before the PR (see the footstep row).

A tick is one game logic step: 22 a second (the game draws one HUD frame
per step). Volumes are the game's, 0-127.

## The open samples, resolved

Each animation set has a 20-byte **sound record** at VS `0x80091340` + set
x 20 (player block +0x44; byte-identical in all five fight engines). It is
five (frame, sound id, volume, 0) entries. The 72 sets use only three:

| Legs (sets) | Step 1 | Step 2 | Hard landing | Jump | Turn (byte 0x11) |
|---|---|---|---|---|---|
| humanoid, reverse joint | frame 4, id 3 (#01), 50 | frame 11, id 3 (#01), 50 | frame 2, id 5 (#12), 80 | frame 1, id 13 (#37), 60 | id 13 (#37) |
| four legs | frame 4, id 18 (#02), 50 | frame 11, id 3, 50 | same | same | id 13 (#37) |
| caterpillars | frame 4, id 16 (#04), 50 | frame 11, id 3, 50 | same | same | id 16 (#04) |

(Some humanoid sets step on frames 3 / 9 or 4 / 14; the engine has no set
number in the AC, so it uses 4 / 11 for all.)

Who reads it (VS):

- **Walk steps**, entries 0-1: every walk, strafe, back and diagonal
  animation (2, 4, 6, 9, 12, 15, 17, 19) is started by `0x8007A9E0` with
  this list, count 2. The event fires when the frame counter (+0x76) reaches
  the frame + 1 (the same compare as the foot dust at `0x8007DF14`).
- **Hard landing**, entry 2: `0x80054ADC(anim, 1, record + 8)` in the
  hard-landing handler `0x80084C20`: frame 2 of animation 36.
- **Jump take-off**, entry 3: `0x8007AD7C` when animation 22 starts
  (`0x80084614`).
- **Turning**, byte 0x11: the three turn handlers `0x8007E1DC`,
  `0x8007E2DC`, `0x8007E3E0` play it every tick with a per-AC handle
  (+0x1200), volume 40, cut-off 8,000 / fade 10,000: a held sound.
- **Drive**, byte 1 again: the per-tick routine `0x80087108` (AC +0x60,
  set at `0x80087604`) plays it with handle +0x1300 at volume 50 + s / 128
  and pitch + s / 128 sixteenths of a semitone, s = AC +0x116. +0x116
  climbs 512 a tick toward the speed in the ground-boost handler
  (`0x800858E4`, which four legs and caterpillars also walk through) and
  falls 256 a tick.

So:

| # | Id | Answer |
|---|---|---|
| 02 | 18 | Four legs: first step of the walk cycle; also their drive drone (a true loop) |
| 03 | 17 | **Not played in fights.** No constant id 17 in any fight engine, and not in the sound record |
| 04 | 16 | Caterpillar treads: first step, turning, and the drive drone (a true loop) |
| 06 | 52 | **Not played in fights.** Only `0x8008F034` (VS; arena `0x80095F6C`) plays it, an effect spawner with no caller in either engine |
| 37 | 13 | The turning hum (a true loop) of legs and four legs; the jump take-off |

Also settled on the way: **#13** (id 14) is the soft landing, played at 60
on touchdown by `0x80084834` (not on hard landings); **#12** (id 5, the
cannon's sample) is the hard landing's thud. #01 being a footstep is now
code as well as ear.

Loop or not is the sample's own ADPCM flags (6 / 3 = loop; 7 at the end =
one-shot): loops are #02, #04, #19, #31, #37.

## Sounds: what changed

| Sound | Old | New | Source |
|---|---|---|---|
| Footsteps | none | the record's two steps per walk cycle, volume 50, **on the walk animation's frames**: when the sim's frame (`ac->anim_frame`) of a walk loop (2, 4, 6, 9, 12, 15, 17, 19) reaches 5 and 12, as the game fires at frame counter = record frame + 1. Four legs and caterpillars walk in a single pose (feel/animations), and a set without a long enough track has no frames to key on: those step by distance walked, one 16-frame cycle per 16 ticks of walking pace (`acr_gen1_walk_pace`). Not while boosting, in the air or landing. | record entries 0-1, `0x8007A9E0` |
| Turning | none | held while the AC turns (`turn_rate` not 0), volume 40: #37, caterpillars #04 | byte 0x11 |
| Drive drone | none | four legs #02, caterpillars #04, held the whole fight at 50 + s / 128 with the pitch rise. Two-legged ACs' drive sound is #01, a one-shot: with a handle it can't sustain, so it's left out (call made; the game may re-key it in a way the static read can't show) | `0x80087108` |
| Jump take-off | none | #37 at 60 when the take-off crouch starts (caterpillars have none) | entry 3 |
| Soft landing | #13 on every landing | #13 at 60, soft landings only | `0x80084848` |
| Hard landing | #13 | #12 at 80, two ticks after touchdown (frame 2 of the kneel), priority 900 | entry 2 |
| Boost start | #09 at its own pitch | #09 at 80, **-80 sixteenths (a fourth lower)**, the first boosting tick | `0x8007E698` (`+0x150` low nibble 0 = flame was off) |
| Boost loop | #31 at its own pitch | #31 at 80, **-48 (three semitones lower)**, held while boosting | same, handle +0x1000 |
| Boost stop | loop ends | unchanged: the game has no stop sound, the loop's handle is just not refreshed | same |
| Blade lunge | | frames 3-11 of a swing sound like a boost (start + loop) | `0x8007EBD0` calls the boost routine |
| Distance | 1 / (1 + d / 2,500), from the AC | the game's: volume x (fade - d) / fade, silent at the cut-off, nothing under 30; 8,000 / 10,000, explosions 12,000 / 14,000; measured from about 650 behind the AC, where the game's camera ears are | `0x80052D9C`, `0x80052C88` |
| Priority | oldest non-loop voice taken | the game's voice pick: a free voice; else one playing the same sound (restarts); else the lowest-weight voice below the new sound's weight; else the sound is dropped. Held sounds are never taken. Weights from the id table: alarms 8 / 9 = 5,000; footstep, guns, boost loop 200; boost start, hard landing 900; hit 500; big explosion 600; the rest 1,000 | `0x80053154`, FDAT 205 id table +8 |
| Pitch | none | the mixer plays a voice at any rate: note + offset / 16 semitones | play routine `0x80053284` |
| Playback | 22,050 Hz | unchanged: 22,050 Hz | user's ear check |

## Warnings (sound and box together)

All from the VS HUD routine `0x8005675C` (the arena engine's copy at
`0x80056F9C` has the same code with its own bar height). They run off the
HUD frame counter (`0x801A8C34`, one a tick, never reset), so the engine's
`battle_warn.c` runs once a game tick for every AC (`main.c` counts the
ticks each battle step runs). The HUD shows the boxes of the AC on screen,
the sounds play the listening AC's beeps.

| Warning | Box | Beep | Old |
|---|---|---|---|
| #27 out of area (id 8, vol 90, weight 5,000) | LEAVING AREA while a countdown is at or above its mark | when the countdown reaches -1; then it restarts at **distance to the edge / 256 ticks (min 3)**, the box on for the first 5/8 of it. So it beeps at once, and faster the closer the AC gets to the line it loses at. The engine measures to the nearest side of the inner box that still holds the AC (`area_edge` in main.c); a stage without boxes uses 20 ticks | every 0.9 s; box steady |
| #28 low AP (id 9, vol 90, weight 5,000) | ARMOR LOW on 5 of every 8 ticks | every 8 ticks (0.36 s) | below a quarter, every 1.5 s, box steady |
| Low AP threshold | | **AP at or below a tenth of the AC's AP** (`0x80056BF0`: full AP x 4 >= 10 x AP x 4) | a quarter (guess) |
| #29 red zone (id 41, vol 100) | | the bar in whole pixels (157 tall); while it **falls** inside the red zone a countdown loses the depth into the zone each tick, and at 0 or below it beeps and restarts at twice the zone's height. Beeps on entering, faster the lower the bar, silent while the bar holds or refills, and never while over-drained | on entering, and on every boost press / energy shot while red |
| #30 over-drained (id 40, vol 90) | !CHARGING! on 5 of 8 ticks, three ticks after the low-AP rhythm | every 8 ticks, the same offset (never on the same tick as #28) | every 0.45 s, box steady |
| SYSTEM ERROR (id 59, vol 127) | 5 of 8, offset 6 | every 8 | not in the engine (no such state yet); not added |
| HIT / DAMAGED | lit while (ticks left >> 2) & 3 is not 0: 12 of every 16 ticks | | lit on bit 2 of the countdown (8 of 16) |

The labels' note on #29 ("repeats on each boost press or energy weapon use
while red") is what this rule sounds like: those are the moments the bar
falls.

## HUD

| Piece | Old | New | Source |
|---|---|---|---|
| Energy bar | smooth fraction | **whole pixels, 157 for a full bar**, straight from the energy: no easing. It moves in steps at the game's 22 a second because energy only changes on ticks | arena `0x80057020` (VS split screen: 60 steps) |
| AP digits | the AP | unchanged: the game prints (AP x 4 + 3) / 4 each frame, no counting down | arena `0x800572A4` |
| AP colour | red below 1,000 | **unchanged, the user's rule.** The game itself reddens it at a tenth of the AC's AP (same test as ARMOR LOW); noted, not followed, because the brief asked for 1,000 | arena `0x80057280`, VS `0x80056F60` |
| Empty weapon | name and ammo red | unchanged: the game draws an empty weapon (slot +0x10 = 0) in colour 1 for the selected one and the list | VS `0x80056D98`, `0x80056EBC` |
| Round clock | above the radar | unchanged (user's layout) | |
| REC | (228, 20), never on | moved to **(228, 8)**, line at y 16, so it doesn't sit on the clock (the game's spot is now the clock's). Lit only while a replay is being captured **and** the fight is not a mission (`g_rogue_fight != 3`, no mission map / own mission). Nothing records fights yet (docs/REPLAYS.md), so `g_replay_capturing` stays 0 until the recorder sets it | user, 2026-11-19 |
| Blinking | steady boxes | as the table above | |

## Not done / open

- Four legs and caterpillars: whether the game's step events still fire
  while their walk is a single pose (the event compares the frame
  counter) isn't settled; the engine gives them distance-timed steps.
- The per-set step frames (3 / 9, 4 / 14) need the set number in the AC.
- The two-legged drive (#01 with a handle): what the voice manager
  (`0x80052ED0`, a 60-count release) does with a held one-shot can't be
  settled statically; one listen in the recomp (stand still: is there a
  repeating thump?) would settle it.
- Held loops stop the frame their action stops. The game's handled voices
  are released by that same manager; the exact delay isn't read.
- Shot and hit volumes (by ear: shots 110), the missile flight loop (#19),
  match start / end, SYSTEM ERROR.

## Check

`tests/test_battle_warn.c` pins the warning rules (low-AP tenth, 8-tick
rhythm and 5/8 box, over-drain offset, red-zone countdown, area period from
the edge, HIT blink 12 / 16). The full build and every test pass. Nothing
was played.
