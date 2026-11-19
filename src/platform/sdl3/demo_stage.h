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
    /* Textured faces, one batch per texture (the game's texture pages). */
    struct { acr_vtx *v; int count; uint32_t texture; } batch[16];
    int batches;
    int textured;                   /* 1: the textured models; 0: the flat green ones */
    int32_t half;                   /* half width of a square inside the walls, for the AI */
} demo_stage;

/* Loads <import_dir>/stages/<id>/; 0 (and nothing kept) if it isn't there. */
int  demo_stage_load(const char *import_dir, int id, demo_stage *out);
void demo_stage_free(demo_stage *s);
/* Draws it in the current 3D view: pass 0 = solid faces and outlines,
 * pass 1 = see-through faces (after everything else). */
void demo_stage_draw(const demo_stage *s, int pass);

#endif
