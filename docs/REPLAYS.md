# Replays

A replay is a fight saved as **the controls that were pressed**, not as a
film. The fight's rules give the same result from the same controls on every
machine (rollback netplay relies on the same thing), so the pads are all it
takes to watch a fight again, from any camera. Design: [DESIGN.md](DESIGN.md),
"Replays" and "Spectating".

## Where it stands (2026-11-25)

**In:** the engine part, `src/net/acr_replay.{h,c}`, with its tests
(`tests/test_replay.c`). No files, clocks or memory allocation in it, so it
goes to the Xbox as it is.

- Recording a match step by step, or an **online match** from the pads that
  are final behind rollback: both machines end up with the same file, byte
  for byte.
- Playing back: a step at a time (for watching) or all at once (for checking
  a file). Jumping back is copying the reader along with the match.
- Saving **while the fight goes on**: a replay that was never closed (the
  game shut mid-fight) still plays as far as it goes.
- Refusing what can't play: damaged files, files from a build whose fights
  play out differently, files made from different game data. Each with its
  own message.

**Not in yet (needs the game's main loop, so it waits for the open PRs):**

- The game recording its fights and saving the files.
- The **REC** light on the HUD: on only while a replay is being recorded,
  in the Arena and VS, never in missions (user, 2026-11-19).
- A REPLAY screen to pick and watch one, and the replay cameras.
- Spectating, which is a replay fed live.

## Size

Measured in the tests, at 60 steps a second:

| Fight | Steps | Bytes |
|---|---|---|
| Two players holding their controls | 4,000 (64 s) | 454 |
| Two restless players (about ten changes a second each) | 4,000 (64 s) | 11,690 |
| The same as raw pads | 4,000 | 24,000 |
| Co-op, two players and two computer pilots | 3,000 (48 s) | 8,565 |

A pad only costs bytes when it changes. Computer pilots that fly inside the
match (co-op) cost nothing: they are flown again from the seed.

## What a file holds

Little-endian. A 32-byte header, the game's setup block, then records.

| Bytes | Header field |
|---|---|
| 0-3 | `ACRP` |
| 4 | format (1) |
| 5 | kind: 0 a match (`acr_gen1_match_step`), 1 a fight the game runs itself |
| 6 | tracks: pads saved each step, 1 to 4 |
| 7 | bytes in a pad (3) |
| 8-11 | rules version (`ACR_REPLAY_RULES`) |
| 12-15 | data check (the online one: parts, motion, stage) |
| 16-17 | step length, ms |
| 18-19 | steps between checks |
| 20-23 | random seed |
| 24-25 | length of the setup block |
| 28-31 | hash of the header and the setup block |

The **setup block** is the game's own: who fights whom and where (builds,
colours, stage, mode, names, computer pilots' profiles). The replay code
keeps it safe and hands it back; it doesn't read it.

| Record | Meaning |
|---|---|
| `00`-`7F` | that many + 1 steps with the pads as they stand |
| `80` + track, 3 bytes | that track's pad from the next step on |
| `C0`, 4 bytes | a check: the fight's hash after the steps so far |
| `FE`, 16 bytes | the end: steps, last hash, result, rounds, wins, and a hash of every record before it |

Playback compares the checks (every 60 steps, the same ones online play
uses for its desync check), so a fight that goes differently is found at
the step where it happens.

## The tripwire test

`tests/test_replay.c` holds one fixed fight and the numbers it must end on
(`GOLDEN_*`). They are the same on every compiler and on Windows and
Linux; a build that disagrees would also fall out of step online.

When a change makes fights play out differently, that test fails and
prints what to do:

- **Meant** (a rule was changed): raise `ACR_REPLAY_RULES` in
  `src/net/acr_replay.h` and put the printed numbers in the test. Replays
  from before are then refused with "MADE BY ANOTHER VERSION OF THE GAME"
  instead of playing out wrongly.
- **Not meant:** the change broke something. Fix the change.

## Hooking it up (next)

- **Online** (`acr_rb`): `acr_replay_rec_rollback` after every
  `acr_rb_tick` / `acr_rb_settle`; `acr_replay_rec_end_rollback` when the
  match stops. The setup block can be what WELCOME already carries.
- **Local fights** (the main loop around `acr_gen1_battle_step`): kind 1,
  one track per AC, the computer pilot's pad saved like a player's
  (`acr_rb_pack`), with a check from the fight's hash every 60 steps.
- **Files:** a `replays/` folder beside the save, written a buffer at a
  time (`acr_replay_rec_drop`).
