/* Gen 2: the Armored Core 2 test AC that acr-import writes from an AC2 disc
 * (<import>/gen2/ac2/test-ac/ac.glb; tools/acr_import/acr_models_ac2.c),
 * drawn in AC TEST in place of the opponent's Gen 1 model. The opponent
 * still runs on its Gen 1 build (stats, AI, hits): only the look is Gen 2.
 *
 * Since 2026-11-28 it is animated: the glb's parts are posed every frame from
 * its AC2 motions (75 named clips, 60 fps). Which clip plays is picked from
 * how the AC moves (gen2_ac_draw sees only its position): standing = idle,
 * walking on the ground = walk loop, in the air = jump. Textures: the part's
 * own (greyscale in AC2: the game tints them with the paint), tinted by the
 * material colour. Notes: docs/research/gen2-models.md. */
#ifndef GEN2_AC_H
#define GEN2_AC_H

#include <stdint.h>

#include "../../render/acr_light.h"

typedef struct gen2_ac gen2_ac;

/* The test AC (loads once per import folder; NULL if the import has none).
 * Re-run acr-import if an older import only has ac.obj. */
gen2_ac *gen2_ac_load(const char *import_dir);
void     gen2_ac_free(gen2_ac *g);
/* At world x, y (feet), z, turned by yaw, as demo_model_draw_pose draws a
 * Gen 1 AC (`scale` = ACR_GEN1_MODEL_SCALE). `flash`: hit blink. */
void gen2_ac_draw(gen2_ac *g, float x, float y, float z, float yaw, float scale, int flash,
                  const acr_lighting *light);
/* Plays a clip by name prefix ("47" or "47_idle_loop"), overriding the
 * automatic pick until called with NULL. 1 if found. */
int  gen2_ac_force_clip(gen2_ac *g, const char *prefix);
/* Test key: auto -> clip 0 -> 1 ... -> last -> auto. The playing clip's name. */
void        gen2_ac_cycle_clip(gen2_ac *g);
const char *gen2_ac_clip_name(const gen2_ac *g);
int         gen2_ac_forced(const gen2_ac *g);

#endif
