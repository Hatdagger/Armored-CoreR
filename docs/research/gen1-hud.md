# Gen 1 battle HUD (Master of Arena)

How the arena / VS HUD is drawn and where its pieces are, as brought into the
engine (`src/platform/sdl3/battle_hud.c`). Positions are the PS1's 320 x 240
screen. They were measured against a recomp screenshot of Ocean Platform
(single player, rendered at 1915 x 1438): sprites and text were fitted
pixel for pixel. Most fits were exact; the weapon lines are right-aligned
by eye. The HUD draw code in the arena engine (FDAT 210) has not been read
yet, so these are measured positions, not code.

## The pictures: RTIM 61

RTIM entry 61 is loaded for every fight. Two of its TIMs are the HUD:

| VRAM | Size | Holds |
|---|---|---|
| (896, 256), 4-bit, palettes at (256, 500) x 4 rows | 256 x 256 | the HUD sheet |
| (832, 256), 4-bit, palettes at (256, 500) x 4 rows | 256 x 256 | text: letters, digits, kana, kanji (layer 0 only) |

Each 4-bit sheet holds **four one-colour pictures**: palette row n is white
(with the semi-transparency bit) wherever bit n of the colour index is set,
and see-through elsewhere. So the game picks a layer with the palette and
tints it with the primitive's colour. `acr-import` writes each layer as
`<import>/hud/hud-0..3.png` and the text sheet as `hud/font.png`.

HUD sheet, layer 0 (u, v, w, h):

| Piece | Rect | Notes |
|---|---|---|
| 6 x 7 font, signs | (0, 0), 6 a glyph | `0123456789+-*/.,?!#%()_|:;<>` then symbols |
| 6 x 7 font, letters | (0, 8), 6 a glyph | A-Z, then a-z (q-z on v 17) |
| 4 x 5 font, signs | (0, 24), 4 a glyph | same order as the 6 x 7 |
| 4 x 5 font, letters | (0, 30), 4 a glyph | A-Z |
| Warning boxes | (72, 40 + 8k, 72, 7) | k: HIT, DAMAGED, LEAVING AREA, ARMOR LOW, !CHARGING!, SYSTEM ERROR (filled box, text cut out) |
| Radar disc | (160, 16, 48, 48) | the gap at the top is the view ahead; player mark at its centre |
| Energy gauge | (4, 72, 16, 168) | frame with F / M / E |
| Big digits | (24 + 16n, 113, 16, 22) | AP; `:` after 9 |
| Middle digits | (24 + 16n, 136, 16, 15) | not placed yet (mission time?) |
| Compass | (0, 244, 256, 4) ticks, (0, 250, 256, 5) letters | 256 texels a turn: S at 0, W 64, N 128, E 192 |
| Also | | octagon sight, 4-way grid, TARGET / FRIEND tags, AREA MAP, PAUSE, AUTO MAPPING SYSTEM, button icons, weapon silhouettes |

Layer 1: CONTINUE? / yes / no, PUSH START TO REPLAY, `◆REC` with its line
and corner (50, 72, 78, 8), PAUSE (versus), small split-screen gauge and
WIN / LOSE / READY GO / DRAW / TIME UP. Layer 2: big WIN (0, 1, 120, 62),
LOSE (0, 65, 158, 62), DRAW (0, 129, 184, 62), TIME UP (0, 193, 254, 62).
Layer 3: big READY (0, 0, 198, 63), GO (0, 65, 78, 62).

Text sheet: 6 x 10 letters (A-Z a-p at v 14, q-z at v 27), signs at v 0,
then kana and kanji. Used for the AC's name.

## Layout (single player, 320 x 240)

| Piece | Where | Colour |
|---|---|---|
| ENERGY (4 x 5) | (8, 28) | (58, 214, 58) |
| Energy bar | x 8-15, y 37-194, filled from the bottom | green (64, 144, 56) above the red zone, brown (160, 80, 48) below it |
| Gauge frame | (12, 36) | (58, 214, 58) |
| AP, big digits | 16 apart, ending at x 96, y 23 | (58, 214, 58) |
| ENEMY LIMIT (4 x 5) | (40, 50) | (58, 214, 58) |
| Enemy AP (6 x 7) | ending at x 96, y 56 | (58, 214, 58) |
| Compass band | x 96-225, y 16-30 | screen x 0.88 |
| Compass ticks / letters | y 17 / 23, window x 97-225, facing at x 161 | (48, 128, 40) / (42, 198, 42) |
| REC + line | (228, 20); red line (229, 28, 80 x 1) | (80, 255, 170); (120, 16, 16) |
| Radar frame | x 261-307, y 58-101 | bright green |
| Radar disc | (260, 56), added to the screen | + (64, 120, 24) |
| Selected weapon (6 x 7) | right-aligned at x 312, y 112; ammo `%3d/%4d` at y 120 | yellow (255, 255, 0) |
| Other weapons (4 x 5) | right-aligned, from y 130 | (58, 214, 58) |
| Warning boxes | (240, 184), stacked 8 apart | HIT green, the rest orange |
| AC name (text sheet) | (13, 214), letters 16 apart | pale, semi-transparent |

- The big number is the AP; the vertical bar is energy (its label is
  ENERGY). The name at the bottom left is the AC's name.
- Weapons are shown by type (RIFLE, SMALL MISSILE), not part name.
- REC shows only while the game records replay data: arena and VS fights,
  never missions. The engine has no replays yet, so it stays off.
- The FCS sight (big frame) is drawn through the camera
  ([gen1-models.md](gen1-models.md), "Camera swing and the FCS box").

## Still open

- Read the HUD draw code (FDAT 210) for exact positions and blend modes,
  and the split-screen (VS) layout: layer 1's small gauge and words.
- ARMOR LOW's threshold (the engine guesses a quarter of max AP).
- Radar: scale against radar range, the stage area box drawn in it
  (the grey square in the screenshot), missile blips (radar flag bit 0),
  the yellow line seen at its left edge.
- What the red line under REC measures.
- READY / GO at the start of a round.
