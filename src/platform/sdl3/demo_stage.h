/* An arena stage from acr-import's stages/<NN>/ files: the collision the
 * fight uses, the start points, and the models drawn. */
#ifndef DEMO_STAGE_H
#define DEMO_STAGE_H

#include <stdint.h>

#include "acr_gen1_stage.h"
#include "acr_render.h"

typedef struct demo_stage {
    int ok;
    int id;
    char name[32];
    acr_gen1_stage collision;
    int32_t start[2][4];            /* x, y (down), z, facing */
    acr_vtx *tris, *semi, *edges;   /* drawn: solid faces, see-through faces, outlines */
    int tri_count, semi_count, edge_count;
    /* Textured faces, two batches per texture (the game's texture pages):
     * the faces, then those lying over another face in the same plane
     * (drawn pulled toward the eye so they don't flicker through it). */
    struct { acr_vtx *v; int count; uint32_t texture; uint8_t semi, overlay; } batch[32];
    int batches;
    /* The sky (sky.obj, imports from 2026-11-19 on): drawn around the eye. */
    struct { acr_vtx *v; int count; uint32_t texture; } sky[4];
    int skies;
    /* The far plane (far.obj, chunk 3: Ocean Platform's sea, Garden Plaza's
     * grass), tiled out to the horizon just under the stage. */
    struct { acr_vtx *v; int count; uint32_t texture; } far[4];
    int fars;
    /* Our own backdrop (not from the disc) for stages without a sky: a
     * colour gradient around the eye and a cloud layer high up. */
    int backdrop;
    uint8_t zenith[3], horizon[3], cloud[3];
    float cloud_cover;              /* 0..1 */
    acr_vtx *dome; int dome_count;
    uint32_t cloud_tex;
    int textured;                   /* 1: the textured models; 0: the flat green ones */
    int32_t half;                   /* half width of a square inside the walls, for the AI */
    /* The stage's own out-of-area boxes (stage.toml out_of_area, imports
     * from 2026-11-25 on), also put in collision.bound when loaded; kept
     * here so AC TEST's test boundary can be swapped back. */
    acr_gen1_stage_bound own_bound[ACR_GEN1_STAGE_BOUNDS];
    uint8_t own_bounds;
} demo_stage;

/* Loads <import_dir>/stages/<id>/; 0 (and nothing kept) if it isn't there. */
int  demo_stage_load(const char *import_dir, int id, demo_stage *out);
void demo_stage_free(demo_stage *s);
/* Draws it in the current 3D view: pass 0 = solid faces and outlines,
 * pass 1 = see-through faces (after everything else). */
void demo_stage_draw(const demo_stage *s, int pass);
/* The sky around the eye (world position), before demo_stage_draw; `t`
 * seconds move the clouds. */
void demo_stage_draw_sky(const demo_stage *s, const float eye[3], float t);
/* 1 and the distance haze to use when the stage has our backdrop (the far
 * planes fade into its horizon colour); 0 to keep the default. */
int  demo_stage_fog(const demo_stage *s, acr_fog *out);

#endif
