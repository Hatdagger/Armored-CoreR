/* Mission units on a mission map (stages 0-19): the objects of
 * stages/MM/objects.toml drawn with their models from units/UNN/ (both
 * written by acr-import, tools/acr_import/acr_units_moa.c). Drawn only:
 * they don't move, fight or collide yet (their mission code is not read). */
#ifndef DEMO_UNITS_H
#define DEMO_UNITS_H

#include <stdint.h>

#include "../../model/acr_model.h"
#include "../../render/acr_light.h"

#define DEMO_UNIT_MODELS   16    /* different units on one map (the most is 5) */
#define DEMO_UNIT_PIECES   32
#define DEMO_UNIT_OBJECTS  64

typedef struct demo_unit_model {
    char id[8];                         /* "U20" */
    char skin[32];                      /* its .mtl on this map */
    acr_skeleton skel;
    acr_anim_set anims;
    acr_mtl mtl;
    uint32_t texture[ACR_MTL_MAX];      /* per material, 0 = flat colour */
    int pieces;
    acr_mesh piece[DEMO_UNIT_PIECES];
} demo_unit_model;

typedef struct demo_unit_object {
    int record, handler;
    int model;                          /* index into model[], -1 = none (not drawn) */
    float pos[3];                       /* engine world, y up */
    float yaw;                          /* radians, forward = (sin, cos) */
    int type;
} demo_unit_object;

typedef struct demo_units {
    int map;                            /* -1 = nothing loaded */
    int models, objects, drawn;         /* drawn: objects with a model */
    demo_unit_model model[DEMO_UNIT_MODELS];
    demo_unit_object object[DEMO_UNIT_OBJECTS];
} demo_units;

/* Loads mission map `map`'s objects and their units; returns the number of
 * objects with a model (0: none, or an import from before 2026-11-23). */
int  demo_units_load(const char *import_dir, int map, demo_units *u);
void demo_units_free(demo_units *u);
/* Draws every object in its unit's animation 0, looping, `tick` game ticks in. */
void demo_units_draw(const demo_units *u, float tick, const acr_lighting *light);

#endif
