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

## Legal

- No copyrighted data ships. The importer reads the player's own discs.
- Engine code is written new. The recomp is used to observe how the original
  behaves, not copied.
