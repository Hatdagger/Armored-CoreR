# Design notes

The full plan lives in the
[design doc](https://claude.ai/artifact/PAAEo6cvjUi7qxFQqSGPrf). This file keeps
the decisions the code depends on.

## Target hardware: the original Xbox

Everything is sized so it could run on an original Xbox later. That keeps the
game light on PCs and handhelds too.

| Budget | Size | Use |
| --- | --- | --- |
| Game data | 32 MB | parts, ACs, stages, missions, simulation |
| Textures | 24 MB | all graphics |
| Audio | 8 MB | sound effects, voice, music buffers |
| Total | 64 MB | the Xbox's whole memory |

`src/core/acr_pools.c` enforces these: allocations past a pool's budget fail.

## Language and portability

- **C11**, no compiler extensions, so the code also builds with
  [nxdk](https://github.com/XboxDev/nxdk) for the Xbox.
- The core never calls the operating system. Windowing, input, time, files and
  audio go through `src/platform/`.

## Generations

Gen 1, 2 and 3 each get a **ruleset** (how ACs move, how energy, heat and stats
work) and an **importer** (reads that generation's discs). In a crossover, every
AC keeps its own generation's ruleset, so its quirks carry over.

## Enemy AI

AI flies an AC through the same commands a controller gives, sees the battle
only through a perception view, and is split into profile, tactics and brain
layers so later generations and modes can add their own. See [AI.md](AI.md).

## Deterministic simulation

- Game logic runs at a **fixed tick**. Rendering interpolates between ticks, so
  the feel never depends on the framerate.
- Gameplay math uses **20.12 fixed point** (`src/core/acr_fixed.h`), the
  precision the PS1 itself used. The same inputs give the same results on every
  PC and on the Xbox, which online play and replays depend on.
- Gen 1 runs **22.0 logic steps a second** (one step per 715 NTSC scanlines),
  measured from Master of Arena: [research/gen1-timing.md](research/gen1-timing.md).
  The engine draws as fast as the screen allows and interpolates between steps.

## Replays

A replay is the battle's **inputs**, not a recording of what happened. Because
the simulation is deterministic, playing the inputs back through it rebuilds the
battle exactly. Online play sends the same inputs, so replays come with it.

- **What a replay holds:**
  - a header: engine and ruleset versions, stage, the battle's random seed;
  - each AC's full build (parts, colours, Human PLUS, generation), and its AI
    profile if a computer flies it;
  - one command word per AC per tick: the same commands a controller or the AI
    gives (see Enemy AI), run-length encoded because commands change rarely.
- **Size:** at 22 ticks a second and a 16-bit command per AC, a raw minute is
  about 5 KB for two ACs and far less after run-length encoding, so replays
  stay tiny even inside the Xbox budget.
- **Nothing outside the simulation goes in.** The camera, HUD and sound are
  rebuilt during playback, so a replay can be watched from any camera (the
  spectator and replay cameras are the same code).
- **The random seed lives in the simulation state.** Gameplay never asks the
  platform for random numbers, or playback would diverge.
- **Checksums:** every few seconds the replay stores a checksum of the
  simulation state. Playback compares them, so a mismatch names the tick where
  two runs diverged. The same check catches online desyncs.
- **Version locked:** a replay plays only on the engine and ruleset versions in
  its header. Anything else is refused with a clear message rather than
  replaying wrongly.
- **Tests:** recorded replays with their expected checksums go in `tests/`, so
  any change that alters gameplay shows up as a failing replay.

## Connection handshake

Online, each machine runs the battle itself from the shared inputs, so both must
run exactly the same simulation. Before a battle the games compare:

- the **simulation version**: a hash of the gameplay code, not just a build
  number;
- each generation's **ruleset version**;
- a **hash of the imported part data**, since each player imports from their
  own discs;
- the **match rules** from the lobby (stage, time limit, Human PLUS, overweight,
  banned parts, best of N);
- each player's **AC build**, validated against those rules on both machines.

Any mismatch ends the connection with a message that says what differs (for
example "your opponent is on 0.6, you are on 0.7"). Only when everything
matches does the host send the random seed and the battle start. A replay
header carries the same fields, so a replay is checked the same way.

## Players and joining

- The simulation has **player slots** (up to 4), never a fixed "player 1 and
  player 2". Early versions may only use two; nothing in the core assumes it.
  (Master of Arena hard-wires two AC slots, which is what blocked 3-4 unit
  battles in the recomp.)
- **Joining mid-battle or mid-mission:** the host picks a future tick, sends a
  snapshot of the whole simulation state for that tick, and the new player's
  inputs count from that tick on. The connection handshake runs first, as for
  any connection.
- Snapshots are **self-contained** (IDs, no memory pointers), so the same
  snapshot serves rollback, joining, desync dumps and replay seeking.
- **Missions are written for "the players"**, not "the player": spawn points,
  objectives and scripted events take any number of player ACs. Original
  single-player missions get extra spawn points and co-op rules as data.

## Engine rules

Cheap to follow from the start, expensive to retrofit:

1. **IDs, not pointers,** in the simulation state (AC 3, bullet 120).
2. **Fixed iteration order** everywhere in the simulation: no loops over hash
   maps or anything whose order can differ between machines.
3. **Gameplay content is data with stable IDs:** parts, stages, missions and AI
   profiles live in versioned tables, not code. Their hash feeds the
   connection handshake.
4. **Saves and settings carry a version** and an upgrade step from older
   formats, so a new build never breaks an old save.
5. **All on-screen text comes from tables** (translations, controller-specific
   button prompts).
6. **The UI is resolution- and aspect-independent:** 4:3 for the Xbox, 16:9 and
   16:10 for PCs and handhelds.
7. **Debug view and desync dump:** the simulation state can be inspected live,
   and when checksums disagree both machines write their state at that tick so
   the two can be compared.
8. **Per-type budgets** (ACs, bullets, effects) inside the memory pools, so a
   large co-op battle degrades gracefully on Xbox-sized hardware.

## Legal

- No copyrighted data ships. The importer reads the player's own discs.
- Engine code is written new. The recomp is used to observe how the original
  behaves, not copied.
