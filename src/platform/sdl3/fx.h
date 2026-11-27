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

/* Where a fighter's booster nozzles and blade arm are as drawn this frame
 * (world units, y up), from the posed model; the draw sets it before
 * fx_draw. Without one (no model) the effects go on a stand-in spot. */
typedef struct fx_rig {
    int   nozzles;            /* 0: no booster drawn */
    float nozzle[4][3];
    float back[3];            /* the AC's backward, flat */
    int   blade_ok;
    float blade_root[3], blade_tip[3];   /* the blade arm's shoulder and hand */
} fx_rig;
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
