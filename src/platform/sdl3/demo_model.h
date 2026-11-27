/* The real AC model for the test build: loads a build's pieces, skeleton
 * and joint animations from acr-import's models folder, builds textures
 * from the player's colours and draws the AC with PS1-style lighting. */
#ifndef DEMO_MODEL_H
#define DEMO_MODEL_H

#include <stdint.h>

#include "demo_data.h"
#include "../../model/acr_model.h"
#include "../../render/acr_light.h"

#define DEMO_MAX_PIECES 9

typedef struct demo_part_model {
    int present;
    int index;                        /* game index, for the folder name */
    acr_mtl mtl;
    int pieces;
    acr_mesh piece[DEMO_MAX_PIECES];
    /* Each piece's joint: the bind position (game space) the export moved
     * its vertices by, in the animation set it was exported from. */
    int has_joint[DEMO_MAX_PIECES];
    int joint[DEMO_MAX_PIECES][3];
    /* A piece that is one untextured triangle: a launch / muzzle point
     * (every back unit's second piece, the weapon arms' extra pieces), not
     * meant to be seen. Kept for demo_model_part_tip, never drawn. */
    int marker[DEMO_MAX_PIECES];
    uint32_t texture;                 /* this part's own texture.png, 0 = none */
} demo_part_model;

typedef struct demo_model {
    int ok;
    acr_skeleton skel;
    acr_anim_set anims;
    demo_part_model part[ACR_PART_KINDS];
} demo_model;

/* Loads the model for `parts` (garage order, see demo_data.h) on animation
 * set `set`. `colours` (4 slots of RGB, 0..63) recolours the patterned
 * parts; NULL keeps the colours in the exported textures. Returns 0 (and
 * leaves the stand-in to be drawn) when the import has no models. */
int  demo_model_load(const char *import_dir, int set, const int parts[SLOT_COUNT],
                     const uint8_t colours[4][3], demo_model *m);
/* The same with each body part painted on its own, as CHANGE COLOR keeps
 * it: paint[part][colour][rgb], parts head, core, arms, legs; colours BASE,
 * OPTIONAL, DETAIL, JOINT; 0..64 per channel. NULL: the textures' own colours. */
int  demo_model_load_paint(const char *import_dir, int set, const int parts[SLOT_COUNT],
                           const uint8_t paint[4][4][3], demo_model *m);
void demo_model_free(demo_model *m);

/* Draws the AC standing at (x, y, z) (engine world, y up) facing `yaw`
 * (radians, forward = (sin, cos)), posed by animation `anim` at `tick`,
 * at `scale` times its model's size. */
void demo_model_draw(const demo_model *m, int anim, unsigned tick, float x, float y, float z,
                     float yaw, float scale, const acr_lighting *light);
/* The same with a pose already worked out (smooth playback, crossfades). */
void demo_model_draw_pose(const demo_model *m, const acr_pose *pose, float x, float y, float z,
                          float yaw, float scale, const acr_lighting *light);

/* The AC's box posed by `anim` at `tick` (right, up, forward of its feet,
 * world units at scale 1, facing yaw 0). 0 when nothing is drawn. */
int demo_model_bounds(const demo_model *m, int anim, unsigned tick, float mn[3], float mx[3]);

/* The forward-most point of the parts in `kinds` (bits 1 << ACR_PART_*),
 * posed: right, up and forward of the AC's feet, in world units (`scale`
 * as drawn). Where a weapon's shots leave from. 0 when none is drawn. */
int demo_model_part_tip(const demo_model *m, const acr_pose *pose, unsigned kinds, float scale, float out[3]);

/* A garage model on its own (acr-import's models/menu/<kind>/NN: one OBJ,
 * its MTL and texture.png), as the OPTIONAL PARTS page shows a part. */
typedef struct demo_object {
    int ok;
    acr_mtl mtl;
    acr_mesh mesh;
    uint32_t texture;
    float center[3], radius;   /* the model's middle and how far it reaches */
} demo_object;

int  demo_object_load(const char *import_dir, const char *kind, int index, demo_object *o);
/* Frees the mesh (the renderer keeps textures, as demo_model_free does). */
void demo_object_free(demo_object *o);
/* Draws it centred on the origin, `size` world units across, turned by
 * `yaw` (radians) about the vertical and tipped by `pitch`. */
void demo_object_draw(const demo_object *o, float size, float yaw, float pitch, const acr_lighting *light);

#endif
