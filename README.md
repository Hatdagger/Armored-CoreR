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

Phase 1, matching the feel. A first test build lets you drive one AC in an
empty arena with Gen 1 movement.

| Part | State |
| --- | --- |
| Core: fixed-point math, fixed-step clock, Xbox memory budget | done |
| Disc importer (`acr-import`) for Master of Arena | works on a real disc |
| Window and input shell (SDL3) | first version |
| Gen 1 timing: 22.0 logic steps a second, measured from MoA | done |
| Part stats exported as readable files (187 parts incl. optional parts) | done |
| Gen 1 compile (parts -> AC stats), checked against the game | done |
| Gen 1 fitting rules (what makes an AC legal) | done |
| Gen 1 movement: walk, strafe, turn, boost, rise, landing, energy | first version |
| AC graphics | stand-in boxes; real models not imported yet |
| Weapons, damage, opponents, online | not started |

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

This copies every file off the disc into `Data/moa/disc/`, unpacks the
game's `.T` archives into folders of numbered entries, and writes every part
(including optional parts) as a readable file in `Data/moa/parts/`, sorted by
type: `legs/humanoid`, `back/missile`, `option` and so on.

## Test build

1. Import your disc, any of these ways (it goes into a folder called
   `import` next to the program, once):
   - start `armored-corer` and drag your disc's `.cue` or `.bin` onto its
     window;
   - drop the `.cue` or `.bin` onto `armored-corer` or `acr-import` in your
     file manager (Windows Explorer, or Dolphin on the Steam Deck);
   - from a terminal: `acr-import "your disc.cue"`.
2. Run `armored-corer`. You fly Master of Arena's starting AC. Try other
   parts with `--legs N`, `--booster N`, `--core N`, `--arms N`, `--gen N`
   (Master of Arena part numbers, as in the part file names).
   The test build runs 60 steps a second for now (Back / T cycles 22, 30,
   45, 60; 22 is Gen 1's own pace). Movement per step is the game's, so it
   runs faster at higher rates; energy use and landing recovery stay the same
   per second. X / B and Y / G cycle the booster and generator.

| Action | Controller | Keyboard |
| --- | --- | --- |
| Walk forward / back | left stick or d-pad up / down | W / S |
| Turn | left stick or d-pad left / right | A / D |
| Strafe | LB / RB (or right stick) | Q / E |
| Boost (with a direction) / rise (alone) | A or RT | Space |
| Reset | Start | R |
| Hide help | Back | F1 |

## More

- [Design notes](docs/DESIGN.md)
- [Full design plan](https://claude.ai/artifact/PAAEo6cvjUi7qxFQqSGPrf)
- [AC1Recomp fork](https://github.com/Hatdagger/AC1Recomp), used as the
  reference for how the original plays
