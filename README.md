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
| Arenas: Green Silo and Ocean Platform from your disc, with the game's own wall and floor rules | first version |
| Weapon effects: muzzle flashes, shots by weapon type, missile smoke trails, hits, explosions, an AC's blast, from the game's own pictures | first version |
| Lock-on, guns, missiles (lock cursor, volleys, flight, giving up), blade, interceptor, damage: following the AC1Recomp fork's mapping of the game code | first version |
| Two players on one machine (split screen) | first version |
| Renderer: OpenGL 2.1, PS1-style lighting and fog, native resolution | first version |
| Original Xbox build of the core (nxdk test program) | in automatic builds |
| Computer opponent (AI presets, AI against AI) | first version |
| Online: 1v1 and co-op with rollback, over LAN, direct IP, or the internet with mission-briefing codes | first version |
| Rogue Arena: 19 + 1 ladder, prizes, ghosts (from the AC1Recomp fork) | first version |

## Building

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The window shell needs SDL3 and downloads it on the first build. To build only
the core, tools and tests, add `-DACR_BUILD_GAME=OFF`.

## Importing your disc

Re-import after updating to this version: Ocean Platform's sea and Garden
Plaza's grass now reach the horizon (`far.obj`). Before that, the stages gained the game's own
stage lights (an older import keeps one fixed light). Before that, Rogue Arena needed the game's
arena opponents (`arena/`), the fight sounds need the game's shared fight
bank (`sfx/`; without it fights are silent) and the weapon effects need the
game's effect pictures (`fx/`; without them shots draw as plain glows).
`ACR_SFX_VOLUME=0..100` sets the sounds' volume (70 by default, 0 turns them off).
Before that, Green Silo gained its sky (stars behind the arches). Before that, the arenas gained the
game's own collision data (`collision.txt`), so walls and floors follow
the game's rules (an older import keeps the stand-in rules). Before that: the animation sets now carry
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
   `--ai sniper` (or any preset) starts with a computer opponent;
   `--ai-file my-pilot.toml` loads your own profile (see `docs/AI.md`);
   `--ai-vs-ai` puts player 1 on autopilot too. With an AI opponent the
   round is two minutes, as in VS.

The controller buttons for boost, fire, switch weapon, blade, strafe and
view tilt can be changed on **SYSTEM > SETTINGS > CONTROLS** (pick a row, then
press the button you want; a button already in use swaps places). Moving
and turning stay on the left stick / d-pad; Start and Back keep their jobs.
Left / right on that page switches to the **KEYBOARD** tab, where every
key in the table below (moving and turning included) can be changed the
same way; the arrow keys always move and turn, and Esc, Enter, Backspace,
the F and number keys and the test keys (R, T, B, G, C, L, H) keep their
jobs. Both mappings are saved in `controls.toml` beside the save. The
keyboard column below is the standard layout.

**GARAGE > PERFORMANCE** shows the AC's ratings as Master of Arena works
them out (offensive, defensive, stability, mobility, support, OVERALL) and
its GRADE: 25,000+ SUPERFINE, 20,000+ GREAT, 15,000+ FINE, 10,000+ OK,
else POOR.

**GARAGE > OPTIONAL PARTS** fits and removes optional parts, with each
one's slots, the disc's description and its model. **GARAGE > CHANGE
COLOR > EDIT COLOR** paints the head, core, arms and legs (four colours
each; GENERAL paints them all; DEFAULT goes back to the game's colours):
up / down picks R, G or B, left / right changes it (LB / RB by 2),
Y / Tab the next colour, A keeps, B cancels.

**SYSTEM > SETTINGS > VIDEO** (fullscreen, window size, fight HUD 4:3 /
wide, smooth or stepped animation) and **AUDIO** (fight and menu sound
volume) are saved in `settings.toml` beside the save.

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
| Opponent: still target, then AI BALANCED, AGGRESSOR, SNIPER, DUELIST, EVASIVE | Back + d-pad up | F4 |
| Arena: Green Silo, Ocean Platform, open test floor | Back + d-pad left | F7 |
| Test boundary for out of area (on / off) | Back + d-pad right | F8 |
| AI against AI (player 1 on autopilot, to watch) | Back + d-pad down | F6 |
| Landing pause: guess / landing animation's length (test) | Back + R3 | F3 |
| Switch target (2+ enemies in the sight; with one gun or missile, weapon switch does it too) | Back (tap) | X |
| Tilt the view up / down (both: level) | LT / RT | Page Up / Page Down |
| Blade (its own button) | B | V |
| Change step rate (22, 30, 45, 60 steps a second; same game pace) | | T |
| Reset | Start | R |
| Hide help | | F1 |

Netplay (1v1, or co-op against 1 or 2 AI, over the network, with
rollback): **NETWORK** on the main menu, or `host-match.bat` (`host-coop.bat` for
co-op) on one PC and `join-match.bat` on the other (or `--host` / `--join
ADDRESS`). Both PCs need the same disc imported. Details: `docs/NETPLAY.md`.

Two players: player 2 uses the second controller (or the only one, leaving
player 1 the keyboard). Both fly the same build; the match ends when one
AC's AP reaches 0, and Start starts the next.

## Arena

Main menu -> **ARENA**. Master of Arena's ranked ladders with your own
garage AC. The main arena starts you at rank 50 in the sub arena; beat
everyone there to open the ranked list (L1 / R1 flips the pages). Pick an
opponent above you and challenge; a win moves you up one rank and pays
their prize money the first time, and some opponents give a part. The Ex Arenas open as
you climb (four of them need a specific leg type). MATCH TIMER (2:00,
5:00, none) and STAGE are on the arena list. Rules: `docs/ARENA.md`.

## Rogue Arena

Main menu -> **ROGUE ARENA**. One AC, one life, 19 rivals from the
game's own arenas. You start at rank 20 with a random AC and 0 P. Each win
pays 1 P and opens a prize screen (pick a part, an upgrade, PASS for +1 P,
or RE-ROLL once for 1 P). At the hub: NEXT BATTLE, SWAP PARTS (saved
prizes), STAGE, GARAGE. One loss ends the run: RETRY ARENA keeps the same
ladder and leaves your old AC on it as a ghost to beat. Reach rank 1 to
clear it. Esc twice in a fight gives up. Same keys as the garage (arrows /
WASD, Enter, Esc; Q / E or LB / RB pick a random stage). Tab / Triangle
on a prize or a saved part shows its PART SPECS against what you have
fitted. Enter on the VS screen skips the countdown.

## Custom maps

Maps can be built in Tinkercad: export as .OBJ, drag the zip onto
`tools/maps/play-tinkercad-map.bat`, and AC TEST opens on it. Size, colours
for the two starts and limits: [docs/MAPS.md](docs/MAPS.md).

## More

- [Roadmap](docs/ROADMAP.md)
- [Design notes](docs/DESIGN.md)
- [Story map](docs/STORY.md): the Gen 1 campaign, arcs and endings, with diagrams
- [Ghost rivals and dynamic missions](docs/GHOSTS.md)
- [Missions](docs/MISSIONS.md): ideas, mockup sorties, progress
- [Rogue Arena co-op](docs/ROGUE-ARENA-COOP.md)
- [Full design plan](https://claude.ai/artifact/PAAEo6cvjUi7qxFQqSGPrf)
- [AC1Recomp fork](https://github.com/Hatdagger/AC1Recomp), used as the
  reference for how the original plays; its `docs/mapping/` is the main
  source for the Gen 1 rules
