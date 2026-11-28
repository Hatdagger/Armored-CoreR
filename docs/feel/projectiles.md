# Feel pass: projectiles (and a generator check)

Branch `feel/projectiles`, 2026-11-28, done unattended overnight. Goal: shots
that look and move like Master of Arena's. The weapon numbers already came
from the disc; what was missing was how shots are drawn, how they spread, and
how many leave per trigger. **Nothing here is play-tested.** It builds, the
tests pass, and there are reference screenshots for every weapon type
(`projectiles/*.jpg`).

## Sources, in the order they win

1. **ACG1PLUS** (Hatdagger/ACG1PLUS, read-only, `main` on 2026-11-28):
   `docs/research/weapon-map.md` (shot families, tables, flash and debris
   words, atlas), `docs/research/mapping/battle-system.md` (pool record,
   missiles, energy), `mapping/renderer.md` (pool draw kinds, projection),
   `mapping/feel-diff.md` (PP vs MoA). Its tool
   `reference/ac1recomp/branches/weapon-map/tools/moa_weapon_map.py` was run on
   the `ma-work` pack to list every weapon's table values.
2. **Our own reading of the disc** where the mapping left a gap, from FDAT 209
   in `ma-work` (VS engine, MIPS disassembly). These are marked **[ours]**:
   - **[ours] Streak length.** Bullet streaks (A, update `0x80088674`) keep their
     tail at the muzzle while the counter +0x2A (set from the table's trail
     field, +14) counts down, then move it with the head. So a streak grows to
     *trail x speed* and slides. When the shot ends (life < 2), +0x2A gets
     0x100 added and the life is set to what is left of the trail, so the
     tail catches up with the stopped head. Energy streaks (D, spawner
     `0x8008D8CC`, update `0x8008D584`) do the same with +0x28 = 2, moving the
     tail once it goes below 0: **3 ticks**.
   - **[ours] Streak width.** The streak draw `0x800645D8` makes a quad whose
     half-width is `width x 40 / (SZ / 4)` pixels (the `0x70` / `0x74` depths are
     SZ >> 2). With the VS projection H = 160 that is **`width` world units**
     either side.
   - **[ours] Spread.** Spawners `0x8008AC30` (A), `0x8008DEC0` (B) and
     `0x8008D8CC` (D) add `(rand & mask) - mask / 2` to each axis of the
     4096-long direction and do not renormalise; velocity = direction x speed >> 12.
   - **[ours] Life.** `0x8008A898`: life = range / |velocity| + 1 (rifle 13,
     as measured in the mapping).
3. The owner's rules: Gen 2 is out; content stays Phantasma's ("PP parts keep
   PP numbers"); the fight must stay deterministic.

Units: tick = one Gen 1 step (22 a second). Speeds in world units a tick.
Colours in the tables are PS1 colours, 128 = 1.0.

## How it works now

- `acr-import` writes each gun's, cannon's and missile's shot look into its part
  file (`shot_family`, `pellets`, `shot_spread`, `shot_sprite`, `shot_width`,
  `shot_trail`, `shot_tint`, `shot_glow`, `shot_glow_size`, `shot_glow_tint`,
  `hit_flash` / `hit_sparks`; C shots `shot_model`), and `fx/sprites.txt`, the
  effect atlas (`0x80091A28`, 297 sprites) mapped onto the pictures it already
  exported. Nothing from the disc is in the repository; **re-import the disc**
  to get the new looks. An older import keeps the old by-type looks and plays
  exactly as before (one shot a trigger, no spread).
- The fight (`acr_gen1_battle.c`) fires the pellets and adds the spread with the
  battle's own random numbers, so replays and netplay stay in step.
- `fx.c` draws A and D shots as streaks and B shots as their atlas sprite with
  the trailing glow, and plays each weapon's own impact flash on hits.

## Part A: comparison table

"Fixed" = changed tonight. "Matched" = it was already right (checked).

### Simulation (changes hit results)

| Item | Original (source) | Ours before | Fixed |
| --- | --- | --- | --- |
| Shot speed, guns A-D | per model from the shot tables (weapon-map.md 2) | the same (import) | matched |
| Shot speed, dispensers (ids 70-89) | E table `0x80092910` +0: WM-X5-AA 70, WM-X10 70, WM-X15-EX 120 | read past the D table: 100, 70, **30,752** | **yes** (importer) |
| Shot speed, cannons | the B / C / D row the cannon redirects to (WC-ST120 / SPGUN 700, WC-LN350 720, WC-XP4000 840, WC-XC8000 900, WC-01QL 544); cannon table `0x80091310` +0 for models 0, 3, 7 (WC-CN35 500, WC-GN230 600, WC-IR24 800) | cannon table +0 for all: **500** for the five redirected ones | **yes** (importer) |
| Life / range | life = range / speed + 1 (`0x8008A898` [ours]; rifle 13, measured) | flies until travelled > range: rifle 13 ticks (test) | matched. The last step can reach up to one step past the range (the game stops at the range point); not changed |
| Shots per trigger | detail +2 bits 0-1: 1, 2, 4 | the same | matched |
| Pellets | B +14 (WG-HG235 / HG1 3, WC-ST120 / SPGUN 7), WA-Finger 5 (A id 8, 5 rounds), WG-XW11 2 (D id 66, second from the other side) | always 1 | **yes** |
| Spread | mask per axis: A +10 (MGs 127, Finger 511, MG500/E 127), B +2 (hand guns 383, slugs 255, pulse 63, linear 127, flamethrower 127), D +2 (AW-XC65 32) [ours] | none (classic controls) | **yes** |
| Missile launch | 100 + the launcher's motion, random start spread (launcher table), kind 4 vertical, kind 3 splits in 4 after 6 ticks | the same | matched |
| Missile flight | flight table: thrust / top, turn a tick (110 / 100 / 90 / 50 / 60), give-up cone, fuel then drop | the same | matched (new test: turn <= 110 a tick, speed <= 350) |
| Cannon kick | `0x80081E80` | the same (weapons pass) | matched |
| WG-HG770 | flies 1/4 range, then re-spawns as B #11 with 3x range | one normal shot | **no**: not modelled |
| Dispensers (WM-X5-AA, X10, X15-EX) | dropped behind / below, 8 / 16 / 4 bombs (E +8), no aim | fire straight ahead like a gun, one at a time | **no**: E update `0x8008C948` not traced |
| Mines (WRR-10) | lie until an AC comes near (`0x8008D0A4`, not traced) | a gun shot | **no** |
| Timed shells (WC-GN230, WC-IR24) | burst at the lock's lead point (`0x8008B1C0`) | a straight shot | **no** |
| Burn (WG-FGI-00), lock jammer (WR-RS7) | weapon-map.md 5 | not modelled | **no** (not shots: hit effects; left for a hit pass) |

### Drawing (presentation only)

| Item | Original (source) | Ours before | Fixed |
| --- | --- | --- | --- |
| How a shot is drawn | by family: A / D streak, B billboard, C 3D model, E billboard (weapon-map.md 2) | by HUD type name: blob, streak, energy capsule or rocket | **yes** for A, B, D; C and E keep the by-type look |
| Picture | atlas sprite: A rifles and MGs 4 (round head with tail), chain gun 7 (diamond), D lasers 5 (wide capsule), AW-XC65 34 (smoke), B hand guns / gatling / slugs / linear 292 (sparkle dot), pulse 291 (ring), WG-HG770 36 (orange flare), WR-RS7 284 (purple orbs), flamethrower 8 (fireball) | MGs and chain gun the glowing capsule (0), hand guns and pulse a blob / capsule, flamethrower a streak | **yes** |
| Colour | A / D the table tint: rifles `fa64fa`, MG500 / AW-MG25 `fa9600`, MG500/E `7878fa`, chain gun `fa3200`, XC4 `60a0f0`, KARASAWA / XFwPPk / XC8000 `60f0a0`, XW11 `808020`, RF/E `32fa32`, AW-XC65 `ff5a1e`; B the sprite's own | rifles 200,130,255; MGs 255,205,120; every energy shot 120,190,255 | **yes** |
| Width | streak half-width = width (rifle 30, MGs 10, Finger 20, chain gun 20, lasers 10, XW11 7) [ours]; B size (hand guns 48 / 40, slugs 32, pulse 60 / 80) | rifle blob 46 wide, MG streak 16, energy 26 | **yes** (full width 2 x width) |
| Trail / tracer length | trail x speed: rifle 1,400, MG 700, Finger 2,000, chain gun 1,500, lasers 2,700, RF/E 8,100; shrinks into the hit point [ours] | speed per frame x 1.6 (60-280); rifle 80; gone at the hit | **yes** |
| Glow | B trailing glow (`0x8008A700`): AR1000 / GT2000 hot glow 80, hand guns 80, pulse ring 80 / 128, slugs / linear magenta flare 160, RS7 cyan glow 80, each with its tint | a round glow 50 on every energy shot | **yes**. Each glow lasts 4 ticks here (its life is not traced) |
| Muzzle flash | not in the mapping | hot glow / energy glow / missile flash by type | not changed |
| Hit sprite | the flash word (`0x8008B60C`): kind (fireball 8-19, hot glow 20-31, spark spray 32-33, flames 42-49, lens flares 36-38), size (rifle 160, MG500 260, RF/5 320, missiles 400 / 600 / 900, KARASAWA 256 ...), frames a tick | sparks + flash (solid), glow + flare (energy), an explosion for rockets and missiles | **yes**. Size taken as the sprite's width (a guess) |
| Spark burst (B, `0x8008B7B8`) | B shots whose +12 byte isn't 1 | as other hits | **yes**, drawn as the spark spray (matched by eye) |
| Hit light colour | flash word bits 10-12 light the area (orange, yellow-green, blue, green) | none | **no** (needs per-area lights) |
| Debris chips | debris word: triangles cut from the target (`0x8008BDF8`) | none | **no** |
| Hit sound | flash word bits 27-31: id 6 @ 90 / 50, id 22 @ 90 / 50, record 7 silent (AW-DC/2, WX-C/4, AW-R/4) | id 6 @ 90 on any AP loss | **no** (sfx only sees AP drops, not which weapon) |
| C shots (snipers, bazookas, rockets, grenades, arm cannons, WX-ED2 / C/4) | 3D models 48-65 | rocket / blob look | **no** (the models aren't imported) |
| Missiles | posed model (draw kind 0), exhaust puffs | flame plume, white trail | not changed; their hit is now their flash word (fireball 400 / 600 / 900) |

## Part B: generator and energy

Checked against feel-diff.md 1.2 and 5 and `docs/research/gen1-movement.md`.

| Item | Original | Ours | Result |
| --- | --- | --- | --- |
| Refill standing | (output - core, head, arms, FCS, legs standing drain) x 64 / 1000, 1-384 | the same (`acr_gen1_compile.c`, compile vectors) | matched |
| Refill moving | the same with the legs' moving drain, 1-192 | the same | matched |
| Red zone | generator +0x2E; below it cost and refill / 4 | the same | matched |
| Booster lock | cost > energy left: energy 0, lock; refill / 2 (no / 4) until full; lock clears only at full; refill adds before the cap | the same, except a cost **equal** to what is left locks at once (one tick early) | left: ours can't boost at 0 energy, so the game's "spend to 0, lock next tick" would never lock here (tried and reverted) |
| Ground boost cost | half the rise cost | the same | matched |
| **Generator output** | PP and MoA differ on five shared parts (feel-diff 5): GPS-VVA / GPS-V6 4,728 (MoA 6,000), GRD-RX5 5,300 (6,560), GRD-RX6 6,000 (6,800), GRD-RX7 6,810 (7,440) | MoA's (the import is from the MoA disc) | **fixed**: the loader puts PP's back for these five. Starting AC: refill 129 standing / 46 moving (was 210 / 127). MoA-only generators (GBG-10000, GBG-XR, GBX-TL, GBX-XL) keep MoA's |

Other parts that MoA changed and PP also has (legs carry limits, FCS lock boxes,
some defences and weapon attacks; feel-diff 5) still carry MoA's numbers. They
were outside this pass; listed for a later one.

## Tests

`tests/test_gen1_battle.c`, "Projectiles pass": rifle speed 700 and life 13
(range 8,500); WG-HG235 3 pellets for 1 round, spread inside 383 / 2 and
different per pellet; WA-Finger 5 pellets for 5 rounds; WG-XW11 2 shots from
both sides for 1 round; no spread mask = identical burst shots; small missile
turning at most 110 a tick and not past 350. The replay test's fixed fight is
unchanged (same hash), only the rules number moved.

## Reference screenshots

`projectiles/<weapon>.jpg`: 8 frames out of 48 in a row (the ones with the most
effect light), the test AC firing at a target 1,800 away (missiles 3,000) on map
00. Made headless with:

```
LIBGL_ALWAYS_SOFTWARE=1 SDL_VIDEO_FORCE_EGL=1 SDL_AUDIO_DRIVER=dummy ACR_STAGE=0 \
ACR_HOLD="0,0,0,0,1" ACR_DIST=1800 ACR_WEAPON=0 ACR_SHOT=/tmp/seq/%03d.bmp \
ACR_SHOT_MS=1200 ACR_SHOT_SEQ=48 xvfb-run -a -s "-screen 0 1280x720x24" \
armored-corer <import> --hand 1          # back weapons: --hand 1 --back N, ACR_WEAPON=1
```

Weapons: rifle (WG-RF35), machinegun (WG-MGA1), mg500, ar1000, handgun_hg235,
handgun_hg512, handgun_hg770, sniper_rf5, sniper_rfe, flamethrower, bazooka,
pulse_xp1000, plasma_xc4, laser_karasawa, laser_xw11, grenade_pb26, finger,
chaingun, slug_st120, linear_ln350, pulse_cannon, plasma_cannon, laser_cannon,
grenade_gn230, small_missile, multi_missile, large_missile, rocket,
special_rocket, dispenser.

## To check in the morning (judgement calls)

1. **Streak length.** Rifles now leave a 1,400-unit streak (lasers 2,700, RF/E
   8,100). That is what the code says, but it is long next to a 212-tall AC.
   If it looks wrong, the guess to doubt is that a world unit draws as one of
   ours at H = 160 (`fx.c`, the streak block in `fx_draw`).
2. **Hit flash size.** Flash sizes (rifle 160, missiles 400-900) are drawn as the
   sprite's width in world units. Missile and slug hits are now much bigger
   than before. If too big, halve `size` in `flash_word`.
3. **Pulse rings and slug glows** near the camera look large (B size taken as
   the half-size, like the streak width). Same guess as 1.
4. **Machine gun spread** (127, about +- 0.9 degrees an axis) and hand gun /
   slug spread (383 / 255): now part of the fight. Feel them in play.
5. **Energy refill** with the starting generator is back to Phantasma's (129 /
   46): boosting has to be rationed much more. That is the PP feel the rules
   ask for, but it is the biggest change tonight.
6. In `small_missile.jpg` (frames 7-9) one missile bursts close in front of
   the launcher without doing damage. Not looked into: it may be the corridor
   test map (map 00) or the sim, not the drawing; worth a look in play.
7. Weapons still on the old look: snipers, bazookas, rockets, grenades and arm
   cannons (their 3D models), dispensers, mines.

## Open

- C shots' 3D models (48-65), dispensers, mines, timed shells, WG-HG770's
  re-spawn, debris chips, hit lights, per-weapon hit sounds: see the tables.
- The B billboard draw (`0x80064C68`) and the trailing glow's life are not
  traced; their sizes and timing are guesses.
- Online (rollback) fights still update effects once a frame, so a one-tick
  shot can be missed there.

## Changelog (2026-11-28)

Each line is its own commit and can be reverted on its own (`git revert <commit>`;
find it with `git log --oneline -- <file>`).

| Commit | Files | Old -> new |
| --- | --- | --- |
| acr-import: redirected cannons' shot speed | `tools/acr_import/acr_parts_moa.c` | WC-ST120 / SPGUN 500 -> 700, WC-LN350 500 -> 720, WC-XP4000 500 -> 840, WC-XC8000 500 -> 900, WC-01QL 500 -> 544 (needs a re-import) |
| acr-import: shot looks, atlas, dispenser speeds | `tools/acr_import/acr_parts_moa.c`, `acr_hud_moa.c` | part files gain the shot-look fields; `fx/sprites.txt` is new; dispenser `shot_speed` 100 / 70 / 30,752 -> 70 / 70 / 120 (needs a re-import) |
| Shots: pellets and spread | `src/gen1/acr_gen1_battle.{c,h}`, `src/platform/sdl3/demo_data.c` (pellets block), `tests/test_gen1_battle.c` | 1 shot a trigger, no spread -> table pellets (3 / 5 / 7 / 2) and spread masks; new fields `pellets`, `pellet_ammo`, `spread` |
| Draw shots and hits as the disc does | `src/platform/sdl3/fx.{c,h}`, `demo_data.{c,h}` (looks), `main.c` (`fx_set_looks`) | by-type looks -> the disc's per-weapon look and impact flash (old looks stay for older imports and C / E shots) |
| Energy: shared generators keep PP's output | `src/platform/sdl3/demo_data.c` (`pp_generator_output`) | GPS-VVA / V6 6,000 -> 4,728; GRD-RX5 6,560 -> 5,300; RX6 6,800 -> 6,000; RX7 7,440 -> 6,810 |
| Testing aids | `src/platform/sdl3/main.c` | new `--hand N`, `ACR_WEAPON`, `ACR_SHOT_SEQ` |
| Effects watch every local step | `src/platform/sdl3/main.c` | `fx_update` once a frame -> also after each offline step |
| Replay / netplay rules 9 | `src/net/acr_replay.h`, `tests/test_replay.c` | 8 -> 9 (rules-8 replays are refused) |
| Docs and screenshots | `docs/feel/projectiles.md`, `docs/feel/projectiles/*.jpg`, `docs/feel/README.md`, `docs/CHANGELOG.md`, `docs/research/gen1-effects.md` | new |

Reverting the energy commit alone brings back MoA's generator outputs; reverting
the pellets commit alone brings back one shot a trigger (raise the rules number
again if you do, since fights change).
