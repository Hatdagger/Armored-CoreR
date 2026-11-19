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
| Gen 1 movement: walk, strafe, turn, boost, rise, landing, energy, at the game's own 22 ticks a second at any step rate | second version |
| AC graphics | real models from your disc: parts on the skeleton, joint animations, your colours (PS1-style lighting) |
| Lock-on, guns, missiles (lock cursor, volleys, flight, giving up), blade, interceptor, damage: following the AC1Recomp fork's mapping of the game code | first version |
| Two players on one machine (split screen) | first version |
| Renderer: OpenGL 2.1, PS1-style lighting and fog, native resolution | first version |
| Original Xbox build of the core (nxdk test program) | in automatic builds |
| Computer opponent, online | not started |

## Building

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The window shell needs SDL3 and downloads it on the first build. To build only
the core, tools and tests, add `-DACR_BUILD_GAME=OFF`.

## Importing your disc

Re-import after updating to this version: the animation sets now carry
the game's aim tables (the gun arm, waist and back units follow the
target), the blade's attack and charge drain were swapped in older
imports, and missiles, cores and legs gained fields the fight now uses.
Without a re-import the game still runs; the AC just doesn't aim.

```
build/acr-import "Armored Core - Master of Arena (USA) (Disc 1).cue" Data/moa
```

This copies every file off the disc into `Data/moa/disc/`, unpacks the
game's `.T` archives into folders of numbered entries, and writes every part
(including optional parts) as a readable file in `Data/moa/parts/`, sorted by
type: `legs/humanoid`, `back/missile`, `option` and so on. The garage's
own part models (every part, FCS and options included) go to
`Data/moa/models/menu/`.

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
   (Master of Arena part numbers, as in the part file names); `--back N`
   picks the back weapon.
   The test build runs 60 steps a second (Back / T cycles 22, 30, 45, 60).
   At every rate the game's rules run 22 times a second, as on the PS1, so
   walking, boosting, rising, falling, turning and energy all play at the
   game's pace; faster rates only draw the AC more smoothly between ticks.
   X / B and Y / G cycle the booster and generator.

| Action | Controller | Keyboard |
| --- | --- | --- |
| Walk forward / back | left stick or d-pad up / down | W / S |
| Turn | left stick or d-pad left / right | A / D |
| Strafe | LB / RB (or right stick) | Q / E |
| Boost (with a direction) / rise (alone) | A or RT | Space |
| Fire | X | F |
| Switch weapon (rifle, missile, blade) | Y | Tab |
| Next booster / generator / back weapon / legs | Back + X / Y / B / RB | B / G / C / L |
| Human PLUS on/off | Back + A | H |
| Two players, split screen | Back + LB | F2 |
| Landing pause: guess / landing animation's length (test) | Back + R3 | F3 |
| Change step rate (22, 30, 45, 60 steps a second; same game pace) | Back | T |
| Reset | Start | R |
| Hide help | | F1 |

Two players: player 2 uses the second controller (or the only one, leaving
player 1 the keyboard). Both fly the same build; the match ends when one
AC's AP reaches 0, and Start starts the next.

## More

- [Roadmap](docs/ROADMAP.md)
- [Design notes](docs/DESIGN.md)
- [Full design plan](https://claude.ai/artifact/PAAEo6cvjUi7qxFQqSGPrf)
- [AC1Recomp fork](https://github.com/Hatdagger/AC1Recomp), used as the
  reference for how the original plays; its `docs/mapping/` is the main
  source for the Gen 1 rules
