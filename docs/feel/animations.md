# Feel: AC animations (Master of Arena)

Branch `feel/animations`. Goal: every body animation the game shows, per
leg type, playing as in Master of Arena, so the feel can be judged.

**Rule kept: animations never change gameplay.** Everything here is
drawing. The sim, the battle's timings (stun, kneel, swing, landing) and
the root motion are the ones already merged (`feel/lockon`,
`feel/weapons`, `feel/blade-hit`, `feel/camera` and earlier). Where an
animation's length and a gameplay timer disagree, the animation is
stretched or cut to the timer, never the other way round. The only battle
change is three presentation fields on the fighter (`hit_seq`,
`hit_side`, `hit_air`) that the sim never reads.

Not play-tested (written overnight without a run). It builds, and the 28
tests pass, including new ones in `tests/test_gen1_pose.c`.

## Source

- **Animation data**: the player's disc through `acr-import`
  (`models/sets/set-NN-anims.txt`, all 57 animations of all 72 sets; the
  `ma-work` pack's `install/` is that output). No import change was
  needed: the importer already wrote every animation, the engine just
  didn't show most of them.
- **Which animation, when, how it blends**: Master of Arena's VS engine
  (FDAT 209, loads at `0x80050DD8`), disassembled from the same disc
  (addresses below), plus the notes in `docs/research/gen1-models.md`
  ("Blending and which pose shows") and the AC1Recomp mapping
  (`docs/research/recomp/mapping/weapon-map.md` for the hit states).
- Timing: the game's 22 ticks a second (`docs/research/gen1-timing.md`).
  On a 60 steps/s display every animation is drawn between ticks
  (smooth playback, F5), so 60 Hz shows ~2.7 drawn frames per game tick.

## Leg types and animation sets

The set is picked from core, arms and legs (`motion/models.toml`). By leg
type:

| Legs | Sets | Notes |
| --- | --- | --- |
| Biped (humanoid) | 0, 4, 7, 8, 10, 11, 13, 14, 16, 18-20, 22, 23, 25, 27-29, 31, 32, 34, 35, 37, 38 (plus 5, 9, 12, 15, 17, 21, 24, 26, 30, 33, 36, 39 shared with other legs) | everything below |
| Reverse joint | 48-56, 66-68 | everything below |
| Four legs | 6, 40-47, 63-65 | walking is a **single pose** on the disc; no ground-boost poses, no kneel |
| Caterpillar | 1-3, 57-62, 69-71 | walking is a single pose; no ground-boost poses, no kneel |

Weapon-arm sets (31-39, 63-71) have no blade swing (56) or air slash (47).

## The animations

Frame times are in game ticks (1/22 s). "Frames" = key frames in the
record, "ticks" = the record's length (`last_tick`). Where sets differ the
common values are listed; the engine always uses the AC's own set.

| Animation | Number | Biped | Reverse joint | Four legs | Caterpillar | When it shows | Blend in | Source |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Idle | 0 | 1 pose | 1 pose | 1 pose | 1 pose | standing | 8 ticks (0x200); four legs / caterpillar 16 (0x100) | `0x8008449C`, `0x800859F4` |
| Walk start | 1 (and 3, 5, 8, 11, 14, 16, 18) | 9 frames / 8-10 ticks | 9 / 8 | — | — | first steps | 8 ticks | `0x80084890` |
| Walk loop | 2 (and 4, 6, 9, 12, 15, 17, 19) | 17 frames / 16-20 ticks | 17 / 16 | 1 pose | 1 pose | walking, strafing, backing off, 8 directions | start runs straight in (cut); four legs / caterpillar 16 ticks | root motion tracks, `0x800859F4` |
| Turn in place | 21 | 1 pose | 1 pose | 1 pose | 1 pose | turning while standing | 8 ticks | `0x8007A95C` |
| Boost | 48-55 | 1 pose each | 1 pose each | (walk pose) | (walk pose) | ground boost by stick direction; after letting go, the opposite one as a brake | 8 ticks | `0x8007FFF0`, `0x80084EA0`, tables `0x80051320`, `0x800517F8` |
| Jump take-off | 22 | 10-11 frames / 9-10 ticks | 10 / 9 | 8 / 7 | 8 / 7 | crouch and lift-off, plays on into the air | 4 ticks (0x400) | `0x80084648` |
| Rise / air | 25 / 24, 26-35 | 1 pose each | | | | booster alone / nothing / by direction and turn | 8 ticks | `0x8008389C` states 0x40-0x4B |
| Landing (soft) | 23 | 9-13 frames / 8-12 ticks | 10 / 9 | 10 / 9 | 9 / 8 | touchdown below the hard-landing speed | cut | `0x80084BCC` |
| Landing kneel (hard) | 36 | 15-21 frames / 16-20 ticks | 16 / 16 | 14 / 16 | 11-12 / 12 | hard touchdown, plays to the end | cut in and out | `0x80084C98`, `0x80084C58` |
| Cannon kneel | 45 (left back), 46 (right back) | 9-18 frames / 8-17 ticks | 9 / 8 | — | — | cannon stance: down, held, back up | 8 ticks | `0x80081C04` (anim 0x2A + slot) |
| Blade swing | 56 | 16-24 frames / 26 ticks | 11 / 26 | 15-16 / 26 | 14-17 / 26 | ground swing | 4 ticks | `0x80084ED8`, `0x80084F98` |
| Air slash | 47 | 17 frames / 16 ticks | 17 / 16 | 14-17 / 16 | 17 / 16 | swing started in the air | 4 ticks | `0x80084FA4` (state 0x21) |
| Hit flinch | 37 front, 38 back, 39 left, 40 right | 12-21 frames / 16-20 ticks | 11-13 / 16 | 14 / 16 | 8-12 / 12 | a stunning hit on the ground | 4 ticks | `0x80082500`, `0x8007BC6C` |
| Air hit | 41-44 (same order) | 1 pose each | | | | a stunning hit in the air | 4 ticks | state 0x81 |
| Destroyed | — | — | — | — | — | AP 0: the last drawn pose is kept | — | `0x800828BC` |

## What changed in this pass

Already in before this pass (unchanged): idle, biped / reverse-joint walk
start and loops from root motion, turn in place, boost and brake poses,
jump take-off, air poses, soft landing, landing kneel, blade swing, the
cross-fades and in-between easing.

New:

1. **Hit flinch (37-40, air 41-44).** Starts on every stunning hit (each
   hit restarts it, as the game restarts the hit state), facing the side
   the shot came from. It shows while the stun holds the controls, and
   then plays out to its end while the AC just stands or turns. If the
   pilot moves or jumps first, that takes over (our merged stun gives the
   controls back after the stun ticks; the game holds the hit state until
   the animation ends; we keep the gameplay). An AC in the cannon stance
   gets no flinch (the game pushes it in its stance, `0x8007B454`).
   *Side*: 90-degree sectors around the facing, from the shot's travel.
   The mapping notes list the order front, back, the two sides; the disc
   agrees (37 / 38 lean the core opposite ways, 39 / 40 roll it opposite
   ways). **Which of 39 / 40 is the left is assumed** (39 = left).
2. **Cannon kneel (45 / 46).** Played from the battle's stance clocks:
   frame time = how far the stance has got (`kneel_ms` of the time the
   stance needs) times the animation's length, so it is fully down
   exactly when the stance lets the cannon fire; held there; standing up
   plays it **backwards** over `stand_ms` (the game starts it with a
   negative speed, starter `0x80054A2C`). 46 is used when the current
   weapon is the right back slot (its aim table is the left one + 1, i.e.
   4 for cannons). The stance time itself comes from animation 45's root
   track as before; 46 is stretched to it on the 4 sets where it differs.
3. **Air slash (47).** A swing started in the air now shows 47 (16 ticks,
   matching the merged air-slash length) instead of the ground swing 56.
4. **Four-legged and caterpillar walking.** These legs move by velocity,
   so the pose picker took every walk for a slide and asked for a brake
   pose those sets don't have: they stood in the idle pose while walking.
   Now their walking pose shows while they walk (and while they ground
   boost, as they have no boost poses), and changes to and from it fade
   over 16 ticks (0x100, every caller of `0x800859F4`). Their walk loop's
   pace follows speed / top speed. On the current disc that changes
   nothing you can see (their loops are one pose); it matters only if a
   set with a real loop turns up.
5. **Destroyed.** At AP 0 the game skips the AC's whole state update
   (`0x800828BC` jumps to its end while AC +0x49 is not 255) and starts
   no animation, so Master of Arena has **no destruction or falling
   animation**: the wreck keeps the pose it had while the blast plays.
   The engine now freezes the drawn pose at that moment. Other wrecked
   ACs stay on screen (before, they vanished at AP 0). If the sim moves a
   wreck (it falls to the ground), the frozen pose goes with it.

## Walk cycle and speed

- Biped and reverse joint: the walk tracks' root motion **is** the
  movement (EXE stepper `0x800135DC`), and the drawn frame is the sim's
  frame plus the fraction of the current tick. The feet match the ground
  exactly, at any step rate.
- Four legs and caterpillar: single walking pose (see above); a pace by
  speed is in place for any multi-frame loop.
- The game's starter takes a speed argument, but every walk-loop call
  passes 0 (normal speed), so there is no per-AC speed scaling to copy.

## Not done / open

- The second animation layer (firing, AC +0x6D, table `b`) is still not
  drawn (unchanged from before).
- The cannon kneel's fade-in step was not read (`0x80081C04` area);
  uses 8 ticks.
- Which hit side is left (39 vs 40), as above.
- The game's quad / caterpillar handler (`0x80085D40`) has its own pose
  rules beyond walking and fades; not mapped further.
