# Gen 1 weapon effects (Master of Arena)

## Where the pictures are

RTIM entry 61, loaded for every arena fight, holds the battle's effect
pictures next to the HUD and text sheets (`gen1-hud.md`). `acr-import`
writes each one to `<import>/fx/fx-NN.png`, where NN is its place in the
entry (`fx-NN-R.png` for palette rows past the first). Colour 0 is
see-through.

This is also the "effects page" the model notes were missing: VRAM
(512, 256) is picture 15, and palette row 510 belongs to picture 24.

| NN | VRAM | What | Used for (engine) |
| --- | --- | --- | --- |
| 00 | 320,256 | hot glow, 4 frames growing | muzzle and hit flash |
| 01, 02 | 384/448,256 | glow then debris, 4 frames each | (not yet) |
| 03 | 320,320 | fireball growing, 4 frames | explosions |
| 04 | 384,320 | fireball breaking up, 4 frames | explosions |
| 05 | 448,320 | smoke and debris thinning, 4 frames | explosions (tail) |
| 06, 07 | 320/384,384 | flames, 4 frames each | fire after an AC's blast |
| 08 | 448,384 | spark spray, 2 frames | shell hits |
| 09 | 304,480 | small triangle marker | (HUD?) |
| 10, 17-21 | 480,384 | fire gradient, 6 palettes (yellow, red, cream, green, cyan, pale) | (boost flames?) |
| 11 | 496,384 | lens flare | energy hits, AC blast |
| 12 | 480,448 | smoke cloud | missile and rocket trails, puffs |
| 13 | 496,448 | lava-like texture | (not yet) |
| 14 | 496,448 | dark blob, 2 palettes | (not yet) |
| 15 | 512,256 | shot sprites, grey (tinted when drawn): see below | shots |
| 16 | 288,490 | small gradient | (not yet) |
| 22, 23 | 528/480,448 | dark and blue noise | (energy?) |
| 24 | 544,320 | round glow, 8-bit, palette row 510 | energy shots and hits |
| 25, 26 | 528,384 / 496,448 | green ring, teal glow | (not yet) |
| 27 | 528,320 | eight small sparkles | (not yet) |
| 28 | 560,448 | metal panel | (not yet) |
| 29, 30 | 496/320,448 | grey puff, purple orbs | (not yet) |

Picture 15 (256 x 64), in texels: 0-31 glowing capsule; 32-47 and 48-63
small round blobs; 64-95 banded gradient; 96-127 lightning; 128-159 a
round head with a tail narrowing below it (rifle shot, rocket flame);
160-191 wide capsule; 192-223 two thin lines; 224-255 a diamond with
streaks.

## How the engine uses them (src/platform/sdl3/fx.c)

Outside the fight, like the camera: it watches the shots and AP and never
changes them. A shot appearing is a muzzle flash; a shot ending is a hit on
an AC (when its path passes the AC's cylinder), a wall hit, or nothing if
it ran out of range; AP reaching zero is the AC's blast. Glows, fire and
sparks are drawn additively (`ACR_MESH_ADD`), smoke half see-through.

Since 2026-11-28 shots and hits follow the disc's shot tables and effect
atlas when the import has them (`docs/feel/projectiles.md`); what follows is
the older by-eye look, still used for older imports, model shots (C) and
dispensers. Timing, sizes and colours are matched by eye for now. The game picks each
weapon's shot from tables chosen by the part's `model_id`, not mapped yet;
until then the look goes by weapon type (play-test 2026-11-19: rifles fire a
purple-ish blob, round in front with a narrower tail; machine guns a
streak).

## Boosting and the blade (fx.c, matched by eye)

- Booster: while the AC burns its booster (the button with energy to
  spend, the modern toggle with a direction held, or a quick-boost burst)
  each nozzle shows a flickering plume (picture 15's rocket flame, orange
  with a white core) pointing backward and a little down, with a glow on
  the nozzle. Lighting it gives a hot flash (picture 00), briefly longer
  plumes, and a dust puff when it lights on the ground. Nozzles sit on the
  back face of each drawn booster piece, one per pod (a piece whose back
  face has a gap across counts as two pods; up to 4); with no booster
  piece, the core's back. Cosmetic only: pod count changes nothing.
- Blade: during a swing a light-blue beam (picture 15's wide capsule, white
  core) leaves the left hand along the forearm, about 3/4 of the blade's
  reach long, with a glow at the hand and a fading sheet over the last
  ~0.1 s of the slash. It lights (over 60 ms) only once the arm has wound
  up to its rightmost and starts sweeping left (followed on the posed
  model; by 700 ms at the latest), holds to 900 ms, and is gone
  by 1,150 ms of the 1,450 ms swing. The game's own blade sprite and
  per-blade colours (MOONLIGHT) are not mapped yet. Each booster's flame
  and each blade's beam now take a colour by part name from a PLACEHOLDER
  table in fx.c (fx_part_colour), to be checked in play.
