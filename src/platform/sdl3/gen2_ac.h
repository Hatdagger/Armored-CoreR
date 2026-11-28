/* Gen 2 proof of concept (2026-11-27): the Armored Core 2 test AC that
 * acr-import writes from an AC2 disc (<import>/gen2/ac2/test-ac/ac.obj,
 * ac.mtl; tools/acr_import/acr_models_ac2.h), drawn in AC TEST in place of
 * the opponent's Gen 1 model. The opponent still runs on its Gen 1 build
 * (stats, AI, hits): only the look is Gen 2. One rigid model, no
 * animation yet. Textures: the part's own (greyscale in AC2: the game
 * tints them with the paint), tinted by the material colour.
 * Notes: docs/research/gen2-models.md. */
#ifndef GEN2_AC_H
#define GEN2_AC_H

#include "../../model/acr_model.h"
#include "../../render/acr_light.h"

typedef struct gen2_ac {
    int ok, tried;
    acr_mtl mtl;
    acr_mesh mesh;
    uint32_t tex[ACR_MTL_MAX];   /* per material, 0 = flat colour */
} gen2_ac;

/* 1 if the import has the test AC (tries once; free resets). */
int  gen2_ac_load(const char *import_dir, gen2_ac *g);
void gen2_ac_free(gen2_ac *g);
/* At world x, y (feet), z, turned by yaw, as demo_model_draw_pose draws a
 * Gen 1 AC (`scale` = ACR_GEN1_MODEL_SCALE). `flash`: hit blink. */
void gen2_ac_draw(const gen2_ac *g, float x, float y, float z, float yaw, float scale, int flash,
                  const acr_lighting *light);

#endif
