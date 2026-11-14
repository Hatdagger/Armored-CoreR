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
- **Step rate**: 60 steps a second is the target; movement per step as the
  game has it, energy, turning, shots and lock times kept per second.

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

Play-test feedback (2026-11-14, at 60 steps a second, kept):
- Lock-on range still feels far too short.
- The blade now has its own button (circle / B; keyboard V), is out of the
  weapon cycle and lunges at a target ahead. Swing time, lunge speed and
  steering are stand-ins (`ACR_GEN1_SWING_MS` etc.): measure in the recomp.
- The camera was brought closer (1700 back, 950 up) by eye: match it to
  the game's single-player camera (new game, first mission).
- Missing animations (boost, blade, firing, landing) make the feel hard
  to judge.

- Lock range feels short in play (numbers match the game): check world
  scale and lock box width once real models are in.
- Placeholders listed in `docs/research/gen1-weapons.md`.
