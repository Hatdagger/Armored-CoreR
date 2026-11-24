/* Mission units on a mission map (stages 0-19): the objects of
 * stages/MM/objects.toml drawn with their models from units/UNN/ (both
 * written by acr-import, tools/acr_import/acr_units_moa.c). Joined to a
 * battle they fight (acr_gen1_battle.h, "Mission units"): lockable, hit,
 * destroyed, and MTs and turrets fire back; drawn where the battle has them. */
#ifndef DEMO_UNITS_H
#define DEMO_UNITS_H

#include <stdint.h>

#include "../../model/acr_model.h"
#include "../../render/acr_light.h"
#include "../../gen1/acr_gen1_battle.h"

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
    int half, height;                   /* body box (chunk 12), 0 = not in the import */
    int ap, attack, energy_n;           /* record +22, +24, +16 bits 8-11 */
    int unit;                           /* its unit in the joined battle, -1 none */
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
/* Puts map `map`'s units into battle b (loading them first if needed) as
 * team 1: those with a model, AP and floor under them (stage given). A
 * unit with 10 or more animations gets the MT brain, an armed one with
 * fewer the turret brain. Clears b's units first; map -1: none. Returns
 * the number joined. */
int  demo_units_join(demo_units *u, const char *import_dir, int map, acr_gen1_battle *b, const acr_gen1_stage *stage);
/* Draws every object, `tick` game ticks in: joined ones where battle b has
 * them, in their state's animation (MTs) or animation 0, blinking when hit
 * and gone once destroyed; the rest standing in animation 0. b may be NULL. */
void demo_units_draw(const demo_units *u, const acr_gen1_battle *b, float tick, const acr_lighting *light);

#endif
