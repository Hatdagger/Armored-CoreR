# Feel pass: camera (2026-11-27)

Goal: a camera that behaves like the original Master of Arena. Done
unattended (no questions asked, no play-test); every decision is written down
here. Branch `feel/camera`, after `feel/movement` (#34) and
`feel/boost-energy` (#35), both checked as merged first.

Sources, in order: the disc (`ma-work.tar.xz`, FDAT 209 = VS engine, FDAT 207
= missions engine, read straight from the disc with a MIPS disassembler);
then the research already in the repo (`docs/research/gen1-models.md`
"Camera swing and the FCS box", `recomp/mapping/stat-block.md` "Camera
setup", `recomp/mapping/renderer.md` "Camera (VS)"). Logic still runs at 60
steps a second with the Gen 1 rules at 22 ticks a second between them (see
`movement.md`, "Tick rate").

## Headline

- **Single player and VS use the same camera on the disc.** The missions
  engine's camera setup (`0x8007F6E0`) and chase camera (`0x80088E1C`) are
  the VS ones (`0x8007DC24`, `0x800878FC`) instruction for instruction: the
  same stat values (+0x36 distance 650, +0x38 height 40, +0x3C smoothing
  shift 2), the eye at half the distance, the same +-800 pitch clamp. So
  there is no second, closer set of numbers to copy.
- **Why ours sat further back:** the trailing. The disc eases the eye a
  quarter of the way each tick, so an AC moving v a tick has the eye 3v
  behind its spot (and the look-at point v behind). The engine's per-second
  version of it (`0.75 ^ (22 dt)`) trails by about 3.5v and the look-at
  by 1.44v: on a ground boost (83 a tick) the eye sat ~40 further back than
  the game's, and the aim point lagged more on turns. Now matched to the
  disc's steady trailing, so the camera sits closer behind a moving unit,
  as the play-tests asked, while standing it is unchanged (325 behind,
  already the single-player camera).
- **Walls and ceilings now follow the disc** (before: only kept 100 above
  the floor).

## Values

World units (an AC is 212 tall). "Tick" = Gen 1 tick (22 a second).

| Value | Old | New | Source | Notes |
| --- | --- | --- | --- | --- |
| Follow point | 186 up the AC | unchanged, `CAM_PIVOT` | `0x80087B90`: AC y - height + height / 8 | |
| Eye distance / height | 328 at 7.5 deg up (325 back, 40 up) | unchanged, `CAM_DIST_HALF` 325 + height 40 | stat +0x36 / 2, +0x38; same in FDAT 207 | Decision: keep the disc value. A closer number would not be the original. |
| Look-at point | 82 ahead of the follow point | unchanged, `CAM_LOOK_AHEAD` | 650 / 8 | |
| **Eye trailing** | `1 - 0.75^(22 dt)` (~6.3 a second; 3.5v behind) | **`1 - exp(-22/3 dt)`** (3v behind, the disc's) | +0x3C = 2: (target - eye) >> 2 a tick | Frame-rate independent; same steady trailing as the disc when boosting or turning. |
| **Look-at trailing** | `1 - 0.5^(22 dt)` (1.44v behind) | **`1 - exp(-22 dt)`** (v behind) | `0x80088008`: >> 1 a tick | The aim point keeps up better on turns. |
| Snap | jump of 2,000+ or a 0.5 s pause | unchanged | camera +0x08 bit 0 copies instead of easing | Reset, respawn. |
| Swing toward the target | 3/4 at 64 a tick (missile flying), 1/2 at 20 (target in sight), else 0 at 20 | unchanged | `0x8008774C` | Already the disc's (battle side). |
| View tilt (L2/R2) | pitch = tilt + swing, clamped +-800 | unchanged | `0x800878FC` | +-800 = +-70 deg. Looking down the eye stretches to ~365 (measured, kept). |
| **Walls / ceilings** | none (floor only) | **the line from the follow point to the eye's spot stops at the first polygon; the spot's box (60 across, 100 up/down) is pushed out of the stage (up to 6 tries); after easing the eye's box (60 every way) is pushed out again; then kept 100 above the floor** | `0x80087B90`: line test `0x80074FA4`, box `0x800758F0` with mask 5, 6 tries, twice | Uses the stage's own collision (`collision.txt`); without it (old import) only the floor rule applies, as before. Open floor (no stage) unchanged. |
| Field of view | 41 deg vertical (H 320 on 240), widened to keep 53 deg across | unchanged | renderer.md: single view H 320; VS halves H 160 on 160x240 | Decision: keep. On widescreen it shows more at the sides, same height. |
| Split-screen | one camera per half, same rule | unchanged | VS draws two 160x240 halves | On a 16:9 window each half is 8:9, so the 53-deg-across rule widens the vertical to ~58 deg, between single player and the PS1's tall halves. Decision: keep the VS rule (it is the original split-screen framing). |
| Online | full screen, own player | unchanged | | |

## Round end

Checked, no change needed: once the round is decided the battle keeps
stepping with every pilot's controls let go (AI too, `main.c` "Round over"),
so ACs drop to the ground where they are, shots and effects keep going, and
the camera keeps its chase and swing (it is drawn every frame from the
running battle). Nothing freezes.

## HUD

Unchanged: 4:3 framing by default (`settings.hud_wide` = 0), WIDE as an
option (SETTINGS > VIDEO, F9).

## Modern controls (SP-MODERN)

Untouched, as documented in `MODERN-CONTROLS.md`: the right stick's free
camera adds its yaw on top of the base camera (`cam_yaw`), so it now trails,
clips at walls and ceilings and frames exactly like the new base camera.

## Decisions

1. Distance and height kept at the disc's 650 / 2 and 40; the "closer"
   play-test note is answered by matching the disc's trailing (the gap only
   showed while moving). If it still feels far after a play-test, the one
   knob is `CAM_DIST_HALF` in `main.c`.
2. Trailing matched by steady lag (per-second rates 22/3 and 22), not by
   running the ease once a tick: same feel, no stepping at high frame rates.
3. Wall/ceiling rule from the disc, using the engine's existing stage tests
   (`acr_gen1_stage_segment`, `acr_gen1_stage_try` with a zero move).
4. FOV, split-screen framing, round end, HUD framing: kept (already the
   original's).

## Still open (for a play-test)

- Does the camera now feel close enough while boosting? (knob: decision 1)
- Tight corridors and low ceilings: the eye may now come in close to the AC
  at walls, as on the disc; check nothing pokes through on Phantasma maps.
- First-person view (START + Triangle + Square) not touched.
