# Roadmap (agreed 2026-11-14)

Where the Gen 1 (Master of Arena) game goes next, in order, with the
decisions behind it. Earlier work: movement, fitting rules, lock-on,
weapons, damage, two-player test build (see `docs/research/`).

## Decisions

- **Model files**: `acr-import` writes each part as **OBJ + PNG textures +
  a small rig file** (readable, like the part files). AC1 parts are rigid
  pieces on joints, so OBJ fits; animations stay in our own text files. The
  engine keeps a compact cached copy for speed and the Xbox memory budget.
- **Garage**: starts from Master of Arena's layout; restructuring (less
  scrolling) comes later.
- **Shop economy**: the same as Master of Arena (prices, selling), so builds
  can be tried freely.
- **Step rate**: 60 steps a second is the target. The game's movement rules
  run at its own 22 ticks a second between steps, exactly as on the PS1, and
  the AC is drawn sliding from one tick to the next, so every step rate plays
  at the game's pace (changed 2026-11-18: before, boosting, rising and
  falling moved a whole Gen 1 tick's distance every step, 2.7 times the
  game's speed at 60). Shots and lock times also count Gen 1 ticks.

## Order

0. **Renderer** (first version done): OpenGL 2.1 fixed function, loaded at
   run time; lighting done on the CPU like the PS1 (ambient + up to 3
   directional lights, per face or vertex) plus distance fog, so every
   platform shows the same picture. Native resolution and any screen shape;
   the HUD sits on a 480-high grid whose width follows the screen. The
   engine core also builds for the original Xbox (nxdk) in the automatic
   builds. Still to come: paletted textures and a per-platform cook step,
   and settings for render scale, anti-aliasing, texture filtering and an
   authentic (low resolution) mode.
1. **Model formats** (research): trace in the recomp which files the game
   loads for an AC and how it draws them; decode mesh, texture and skeleton
   formats. The animation records already read for root motion also hold
   joint rotations.
2. **Model import and display** (first version done): OBJ/PNG/rig export
   per part; the engine (`src/model/`) reads the export, puts the build's
   parts on its animation set's skeleton, plays the joint animations with
   their hold times and builds the patterned textures from the player's
   colours. The test build draws both ACs this way. Still to come: the
   effects texture ("fx" faces), per-texel semi-transparency, aiming joints
   (tables a/b), kneel/landing poses and colour patterns 1-15.
3. **Garage, shop, test mode** (first version done): the game opens in the
   garage. `src/gen1/acr_gen1_garage.c` (no OS calls) holds what a player
   owns, credits, buying and selling (full price back; no trading while in
   debt, as in the PS1 games), fitting under the game's rules (a part the
   build rules out is refused; parts the new one can't be used with come
   off) and a readable, versioned save file. The PC build shows MoA's slot
   order (HEAD ... ARM WEAPON R), the parts you own per slot, the shop by
   category with buy/sell, the build's totals and the VS START check, with
   the real AC turning in the middle. AC TEST is the test arena with that
   build; Esc / Back+Start returns. **Test builds** (`k_test_kit` in the PC
   main): a new save owns one of every part and the shop starts empty; parts
   sold to it stay in its stock and can be bought back, so any part can be
   tried while the game's feel is restored. A release turns this off: the
   starting AC, the shop's real stock and starting credits scaled for it
   (Project Phantasma started at 0; `--credits N` sets it for now).
   Still to come: the shop's real stock (hidden and prize parts), part
   preview while choosing, paint (EDIT COLOR,
   the 15 patterns), AC/pilot names, player 2's garage, MoA's part
   parameter pages.
4. **Arena**: stages from the disc (same mesh decoder), then collision
   (walls, floors, heights), then rankings, opponents and rewards.

## Shortcuts

- The garage and shop need part data, not models: build them in parallel.
- One mesh/texture decoder serves ACs, weapons, effects and arenas.
- Test mode is the existing test build.
- The recomp is the reference picture for checking decoded models.

## Revisit

Play-test feedback (2026-11-14), and what the AC1Recomp mapping settled
(2026-11-18):
- Lock-on range felt short: the world scale (ACs at a quarter of model
  units) is confirmed by the game's model draw; ranges and boxes are the
  FCS's.
- Blade: own button, lunge, steering at the AC's top turn rate toward an
  AC up to 3,000 ahead, energy cost, 246 a hit and the close-range double
  hit, all from the mapped code (`docs/research/gen1-weapons.md`).
- Camera: the game's chase camera (650 / 2 behind, eases a quarter of the
  way each tick) is in.
- Missing animations (boost, blade, firing, landing) still make the feel
  hard to judge. Firing animations are now known: weapon detail +3 on the
  AC's second animation layer.

## Movement and feel (2026-11-18, first priority)

Done on branch `gen1-movement-feel`:
- Movement runs on Gen 1 ticks at any step rate (see Decisions).
- From a stand, the booster jumps even with a direction held; a ground
  boost needs the AC already moving (recomp, VS AI chat).
- Energy: the air (not boosting) refills at the standing rate; the refill
  goes over a full bar for one tick, then back (recomp, battle chat).
- The 75-a-tick limit applies in the air as well as on the ground (every
  movement handler loads its step through the same limit).
- The cannon stance's slide (an eighth a tick) moved into the movement
  tick, so it no longer runs 2.7 times too fast at 60 steps a second.

Still open, to check in the recomp (`gen1-movement.md`, "Still to
measure"): landing recovery after longer falls and whether boosting just
before touchdown shortens it; whether sideways speed survives a landing;
the 2-3 ticks at full speed after letting go of a ground boost (may be the
test driver's input delay); the boost hop (from a player's description);
diagonal walking; caterpillar and four-legged refill while walking; turning
while landing; knock-back and stagger movement (the half-size step limit
handlers).

## Parity with the AC1Recomp mapping (2026-11-18)

Brought over from the fork's `docs/mapping/` (battle system, stat block,
part tables, renderer, menus): see `docs/research/gen1-weapons.md` for the
fight, `gen1-movement.md` for energy and turning, `gen1-fitting-rules.md`
for the garage and shop. Still to bring over:

- Garage: PERFORMANCE page and GRADE (OVERALL 25,000+ SUPERFINE, 20,000+
  GREAT, 15,000+ FINE, 10,000+ OK, else POOR), the 15 paint patterns
  (HEAD_T head + 18v, CORE_T core + 11v, ARMS_T arms + 24v, LEG_T legs +
  40v; colours MENU_TIM 26 + 4v + scheme), default paint, EDIT EMBLEM
  (64 x 64, 16 colours, colour 0 see-through), shop texts (MIS.T 385),
  option texts (`0x800CDE8C`), the garage's part models (now imported to
  `models/menu/`).
- Fight: gun lock distance factor (lock record +0x04), knockback and
  stagger distances, burn (flag 0x80), cannon kick and recoil, the
  first-person camera (START + Triangle + Square), radar (range, missile
  blips, display types), Human PLUS levels below "full".
- Arena (step 4): stage area boxes (out of bounds: inside both inner boxes
  or lose), prizes (`arena-prizes.md`), opponent AI records (36 bytes), the
  match timer options (2:00, 5:00, none).
- Missions later: `missions.md` (mission code and maps are FDAT 0-99).

