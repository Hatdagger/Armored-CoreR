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
/* The sky around the eye (world position), before demo_stage_draw. */
void demo_stage_draw_sky(const demo_stage *s, const float eye[3]);

#endif
