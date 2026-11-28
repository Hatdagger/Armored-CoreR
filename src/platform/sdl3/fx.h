/* Weapon effects: muzzle flashes, tracers, missile flames and smoke trails,
 * hits, explosions and a destroyed AC's blast, drawn with Master of Arena's
 * own effect pictures (RTIM 61, exported by acr-import to <import>/fx/;
 * docs/research/gen1-effects.md).
 *
 * Presentation only, like the camera and HUD: it watches the fight's shots
 * and AP from outside and never changes them, so replays, rollback and
 * spectators see the same fight whatever is drawn. Boosting and the blade
 * are watched the same way: the booster's flame and ignition flash, and the
 * blade's beam with its slash trail. A shot appearing is a
 * muzzle flash, a shot ending is a hit (on an AC, a wall, or nothing when it
 * ran out of range), and AP reaching zero is the AC's blast. */
#ifndef ACR_FX_H
#define ACR_FX_H

#include <stdint.h>

#include "../../gen1/acr_gen1_battle.h"

/* Loads the effect pictures (once); without them effects draw as plain
 * glows. 1 when the pictures were found. */
int  fx_load(const char *import_dir);

/* The HUD's weapon names (RIFLE, MACHINE GUN, ...) for a fighter's weapon
 * slots, in the fight's order: they pick how each weapon's shots look. The
 * names are kept by pointer; call again when a build changes. */
void fx_set_weapons(int fighter, const char (*types)[24], int count);

/* How one weapon's shots look and hit, read by acr-import from the disc's
 * shot tables (the part files: shot_family ... hit_flash; ACG1PLUS
 * weapon-map.md; docs/feel/projectiles.md). family < 0: an older import
 * without them, drawn by the weapon's type as before. */
enum { FX_FAM_NONE = -1, FX_FAM_STREAK = 0, FX_FAM_BILLBOARD = 1, FX_FAM_MODEL = 2, FX_FAM_ENERGY = 3,
       FX_FAM_DISPENSER = 4, FX_FAM_MINE = 5 };
typedef struct fx_look {
    int8_t   family;
    uint16_t sprite;          /* effect atlas index (fx/sprites.txt) */
    uint16_t model;           /* C shots: their model id (not drawn yet) */
    uint8_t  tint[3];         /* streaks: RGB, 128 = 1.0 (0,0,0: the atlas colour) */
    uint16_t width;           /* streak half-width / billboard size, world units */
    uint16_t trail;           /* streaks: ticks the tail waits at the muzzle */
    uint16_t glow, glow_size; /* B: trailing glow sprite and size (0 none) */
    uint8_t  glow_tint[3];
    uint32_t hit_flash;       /* impact flash word (0x8008B60C); 0 = by type */
    uint8_t  hit_sparks;      /* B: a spark burst instead of the flash */
    /* C model shots and timed shells (ACG1PLUS mapping/weapon-effects.md
     * 2, 3): the light they cast in flight (C +0x0C strength, +0x10 RGB;
     * drawn as a glow, there is no per-area light here), the explosion
     * model on impact (C +0x17: 0 the big one, 5 the medium one, 255 none)
     * or, without one, a white flare (C +0x18 x 64; 0 none), and whether
     * it is a timed shell (burst: three fireballs, three ticks apart). */
    uint16_t light;
    uint8_t  light_tint[3];
    uint8_t  blast;
    uint16_t flare;
    uint8_t  timed;
    uint32_t debris;          /* debris word (0x8008C164): chips cut off an AC it hits; 0 none */
} fx_look;

/* The colours a fighter's chips come off in (its paint: up to 4 RGB,
 * 0..255); n = 0 a neutral grey. */
void fx_set_chip_colours(int fighter, const uint8_t rgb[][3], int n);

/* The looks of a fighter's weapons, in the fight's order (kept by pointer;
 * call again when a build changes). NULL: by type only. */
void fx_set_looks(int fighter, const fx_look *looks, int count);

/* Where a fighter's booster nozzles and blade arm are as drawn this frame
 * (world units, y up), from the posed model; the draw sets it before
 * fx_draw. Without one (no model) the effects go on a stand-in spot. */
typedef struct fx_rig {
    int   nozzles;            /* 0: no booster drawn */
    float nozzle[4][3];
    float back[3];            /* the AC's backward, flat */
    int   blade_ok;
    float blade_root[3], blade_tip[3];   /* the blade arm's shoulder and hand */
    uint8_t flame[3], beam[3];            /* the booster's flame and the blade's beam colour (fx_part_colour) */
} fx_rig;

/* The flame colour of a booster and the beam colour of a blade, by part
 * name (any disc). PLACEHOLDERS, not yet read from the game: to check in
 * play (user, 2026-11-27). Unknown parts get the default orange / blue. */
void fx_part_colour(const char *booster, const char *blade, uint8_t flame[3], uint8_t beam[3]);
void fx_set_rig(int fighter, const fx_rig *rig);

/* Forget every shot seen and every effect in flight (a new round). */
void fx_reset(void);

/* Once a frame, after the fight has stepped: spawns effects for what
 * changed and ages the ones in flight. `now_ns` is a steady clock. */
void fx_update(const acr_gen1_battle *b, uint64_t now_ns);

/* Draws the shots and effects into the current 3D view (after the solid
 * scene and the stage's see-through faces). `eye` is the camera position
 * and `fwd` the way it looks, both in the drawn world (y up). */
void fx_draw(const acr_gen1_battle *b, const float eye[3], const float fwd[3]);

#endif
