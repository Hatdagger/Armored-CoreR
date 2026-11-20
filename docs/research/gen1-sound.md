# Gen 1 fight sounds (Master of Arena)

What the engine plays and where it comes from. Two sources: the user's listening
(two rounds on the sound check pages, 2026-11-19) and the sound chat's static read of
the game's code (its `sound.md`, from the USA disc 1 at recomp 0.8.4.4.2). "ear" = heard
and named; "code" = from the code only; "?" = open.

## The bank

FDAT entry 205 holds the shared fight bank, a VAB ("pBAV") at 0x6434: 4 programs, 51
tones, 38 samples. Sample data starts 4 bytes after the size table (sizes are u16 x 8);
size 0 is the unused slot 0. Samples are PS1 ADPCM (16-byte blocks, 28 frames each;
flag 1 ends, 2 repeats, 4 marks the loop start). **22,050 Hz** sounds right by ear.
`acr-import` writes `sfx/NN.wav` and `sfx/sounds.toml` (frames, loop start).

The game plays sounds by **sound id** (256 ids, a 10-byte record each: bank slot,
program, tone, note, weight), not by sample. Ids 0-63 are this bank. Several ids
share a sample at another pitch or volume (ids 20-23 are quieter copies of 4, 10, 6, 7).
Weight 5,000 (ids 8, 9) = plays over everything else.

## Samples

| # | Id(s) | What | From |
|---|---|---|---|
| 01 | 3 | Footstep (humanoid legs?) | ear |
| 02 | 18 | ? loops; picked from data, likely a leg movement loop | open |
| 03 | 17 | ? picked from data, likely a leg step | open |
| 04 | 16 | ? loops; picked from data, maybe tank treads | open |
| 05 | 6, 22 | Small explosion / hit (the hit table, 0x800930A0) | ear + code |
| 06 | 52 | ? (0x8008F034) | open |
| 07 | 51 | Laser / energy shot | ear |
| 08 | 30 | Missions only | code |
| 09 | 1 | Boost start | ear |
| 10 | 42 | Menu / UI | ear |
| 11 | 45, 46, 47, 59 | Lock beep: gun lock (46), missile lock tick (47), alarm (45) | code |
| 12 | 0, 5 | Cannon shot | ear + code |
| 13 | 14 | Landing? (plays when an animation ends, 0x8008389C) | code |
| 14 | 10, 21 | Machine gun shot | ear |
| 15 | 48 | Machine gun shot | ear |
| 16 | 54, 31 | Match start / end (VS main loop) | code |
| 17 | 32 | Missions only (loops) | code |
| 18 | 2 | Missile launch | ear + code |
| 19 | 12 | Missile flying (loops) | ear + code |
| 20 | 50 | Pulse / plasma shot | ear |
| 21 | 4, 20 | Rifle shot | ear + code |
| 22 | 53 | Pulse / plasma shot; also with gun and cannon fire | ear + code |
| 23 | 15 | Blade swing | ear + code |
| 24-26 | 38, 39, 35 | Missions only | code |
| 27 | 8 | Perimeter (out of area) warning | ear |
| 28 | 9 | Low AP warning | ear |
| 29 | 41 | Energy into the red zone; again on each boost press or energy shot while red | ear |
| 30 | 40, 55 | Energy over-drained: repeats while it force-charges | ear |
| 31 | 11 | Boost (loop) | ear |
| 32 | 43 | Missile lock: new lock | code |
| 33 | 44 | Missile lock: last free lock | code |
| 34 | 7, 23 | Big explosion | ear |
| 35 | 56 | Enemy AC opens a transmission | ear |
| 36 | 57 | End of a radio call / message box | ear + code |
| 37 | 13 | ? quiet low loop, picked from data | open |
| 38 | 58 | Electric damage sound (story missions) | ear |

The leg record's byte +17 is the footstep sound id (VS 0x8007E1DC / 0x8007E2DC /
0x8007E3E0), which is probably why ids 16-18 have no fixed caller.

## What the engine plays (src/platform/sdl3/sfx.c)

Presentation only, like the weapon effects: it watches the fight and never changes it.

- A shot appears: the weapon's firing sound by its HUD type (rifles and hand guns #21,
  machine and chain guns #14, cannons, rockets and grenades #12, lasers #07, pulse and
  plasma #20, missiles #18). The game reads the id from the projectile kind's table;
  until those are mapped, the type picks it.
- A missile or rocket ends: #34, quieter. AP drops: #05. AP reaches zero: #34.
- Boost: #09 when it starts, #31 held while boosting. Blade swing: #23. Landing: #13.
- The listening AC only: lock beeps (#11 guns, #32 / #33 missiles), red zone #29,
  forced charging #30 every 0.45 s, perimeter #27 every 0.9 s, low AP #28 every 1.5 s
  (below a quarter of AP, as the HUD's ARMOR LOW).
- Volume falls off with distance (half at 2,500 units); pan by side. All by ear.

Not yet: footsteps, missile flight loop, match start / end, the leg loops.
