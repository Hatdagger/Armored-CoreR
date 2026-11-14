# Armored-CoreR

An original game engine that plays like the classic Armored Core games, built so
the first three generations can fight each other in one game.

- **Gen 1** (Armored Core, Project Phantasma, Master of Arena) comes first.
- **Gen 2** (Armored Core 2, Another Age) and **Gen 3** (Armored Core 3 and its
  follow-ups) join later. Each keeps its own quirks, strengths and weaknesses.
- Built to the **original Xbox spec** (64 MB, 733 MHz) so it runs smoothly on
  low-spec PCs and handhelds, and can be ported to the Xbox itself later.

**No game data is included.** You import it from your own discs.

## Current goal

A Master of Arena style gameplay loop with arenas and online play.

## Status

Phase 1, matching the feel. Nothing is playable yet.

| Part | State |
| --- | --- |
| Core: fixed-point math, fixed-step clock, Xbox memory budget | done |
| Disc importer (`acr-import`) for Master of Arena | works on a real disc |
| Window and input shell (SDL3) | first version |
| Gen 1 timing: 22.0 logic steps a second, measured from MoA | done |
| Gameplay | not started |

## Building

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The window shell needs SDL3 and downloads it on the first build. To build only
the core, tools and tests, add `-DACR_BUILD_GAME=OFF`.

## Importing your disc

```
build/acr-import "Armored Core - Master of Arena (USA) (Disc 1).cue" Data/moa
```

This copies every file off the disc into `Data/moa/disc/` and unpacks the
game's `.T` archives into folders of numbered entries.

## More

- [Design notes](docs/DESIGN.md)
- [Full design plan](https://claude.ai/artifact/PAAEo6cvjUi7qxFQqSGPrf)
- [AC1Recomp fork](https://github.com/Hatdagger/AC1Recomp), used as the
  reference for how the original plays
