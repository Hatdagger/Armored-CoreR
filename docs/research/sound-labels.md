# Fight sound labels (shared fight bank, FDAT 205)

Repo copy of the project doc `claude/sound-labels.md`, with the open samples
resolved from the game's code (`feel/sound-hud`, 2026-11-27; details in
[../feel/sound-hud.md](../feel/sound-hud.md)).

Sample number (#NN = VAG / `sfx/NN.wav`) -> what it is. Playback rate: 22050 Hz ("normal" confirmed by ear, 2026-11-19).
"ear" = user heard it; "code" = from the game code only.

| # | Sound id(s) | What | From |
|---|---|---|---|
| 01 | 3 | Footstep: both steps of a two-legged walk cycle; the second step of four legs and caterpillars | ear + code |
| 02 | 18 | Four legs: first step of each walk cycle; also their drive drone (loops, louder and higher with ground speed) | code |
| 03 | 17 | Not played in fights: no fight engine code or sound record uses id 17 | code |
| 04 | 16 | Caterpillar treads (loops): first step of each walk cycle, turning, and the drive drone | code |
| 05 | 6, 22 | Explosion (small) / hit | ear + code |
| 06 | 52 | Not played in fights: only an effect spawner that nothing calls (VS 0x8008F034, arena 0x80095F6C) | code |
| 07 | 51 | Laser / energy shot | ear |
| 08 | 30 | Missions only | code |
| 09 | 1 | Boost start (played a fourth lower) | ear + code |
| 10 | 42 | Menu / UI; gun locked | ear + code |
| 11 | 45, 46, 47, 59 | Lock-on beep (gun lock, missile lock tick, alarm; different pitches); 59 = SYSTEM ERROR alarm | code |
| 12 | 0, 5 | Cannon shot (0); hard landing thud (5) | ear + code |
| 13 | 14 | Soft landing (on touchdown) | code |
| 14 | 10 | Machine gun shot | ear |
| 15 | 48 | Machine gun shot | ear |
| 16 | 54, 31 | Match start / end (VS); missions message | code |
| 17 | 32 | Missions only (loops) | code |
| 18 | 2 | Missile launch | ear + code |
| 19 | 12 | Missile flying (loops) | ear + code |
| 20 | 50 | Pulse / plasma shot | ear |
| 21 | 4, 20 | Rifle shot | ear + code |
| 22 | 53 | Pulse / plasma shot; also plays with gun and cannon fire | ear + code |
| 23 | 15 | Blade swing | ear + code |
| 24 | 38 | Missions only | code |
| 25 | 39 | Missions only (near mission start music) | code |
| 26 | 35 | Missions only | code |
| 27 | 8 | Perimeter / out-of-area warning (priority 5,000) | ear + code |
| 28 | 9 | Low AP warning (priority 5,000) | ear + code |
| 29 | 41 | Energy in red zone: beeps while the bar falls inside it, faster the lower | ear + code |
| 30 | 40, 55 | Energy over-drained: repeats during forced charging | ear + code |
| 31 | 11 | Boost (loop, played three semitones lower) | ear + code |
| 32 | 43 | Missile lock: new target locked | code |
| 33 | 44 | Missile lock: last free lock filled | code |
| 34 | 7, 23 | Explosion (big) | ear |
| 35 | 56 | Enemy AC opens transmission (start of radio call) | ear |
| 36 | 57 | End of radio call / message box | ear + code |
| 37 | 13 | Turning hum for legs and four legs (loops, held while turning); jump take-off | code |
| 38 | 58 | Damaged / electric sound (story missions) | ear |

Nothing is open for the arena any more. #03 and #06 are worth one listen in
game only if a mission turns out to use them.
