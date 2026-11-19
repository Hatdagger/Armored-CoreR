/* Gen 1 arena stages: the collision polygons ACs stand on and run into, and
 * that shots hit. Integer only, like the rest of the fight.
 *
 * Read from acr-import's stages/<NN>/collision.obj (Master of Arena FDAT
 * 5s+4 chunk 7, placed by the area records; docs/research/gen1-stages.md).
 * The OBJ is in the engine's world with y up; here y is down, as in the
 * fight (feet at y, the floor of Green Silo at -662). All in world units.
 *
 * What the game does with these polygons (step heights, sliding along
 * walls, ceilings) is not mapped yet; the rules here are stand-ins, noted
 * at each function. */
#ifndef ACR_GEN1_STAGE_H
#define ACR_GEN1_STAGE_H

#include <stdint.h>

typedef struct acr_gen1_stage_poly {
    int32_t v[4][3];          /* corners, world units, y down */
    uint8_t corners;          /* 3 or 4 */
    uint8_t flat;             /* floor or ceiling: steeper than 45 degrees is a wall */
    int32_t n[3];             /* unit normal x 4096, from the corners */
    int64_t d;                /* n . p + d = 0 for points p on the polygon */
    int32_t lo[3], hi[3];     /* bounding box */
} acr_gen1_stage_poly;

typedef struct acr_gen1_stage {
    acr_gen1_stage_poly *poly;
    uint32_t polys;
    int32_t lo[3], hi[3];     /* bounding box of everything */
} acr_gen1_stage;

/* Parses the collision OBJ text (v / f lines; anything else is skipped).
 * 1 on success; free with acr_gen1_stage_free. */
int  acr_gen1_stage_parse(const char *obj_text, acr_gen1_stage *out);
void acr_gen1_stage_free(acr_gen1_stage *s);
/* Adds one polygon (corners y down); 0 when out of memory. For tests and tools. */
int  acr_gen1_stage_add(acr_gen1_stage *s, const int32_t (*v)[3], int corners);

/* The floor under (x, z): the highest flat polygon facing up whose surface
 * is not above y_top (y down: surface y >= y_top). 1 and *floor_y when found. */
int acr_gen1_stage_floor(const acr_gen1_stage *s, int32_t x, int32_t z, int32_t y_top, int32_t *floor_y);

/* The lowest ceiling over (x, z) whose surface is not below y_bottom
 * (surface y <= y_bottom). 1 and *ceiling_y when found. */
int acr_gen1_stage_ceiling(const acr_gen1_stage *s, int32_t x, int32_t z, int32_t y_bottom, int32_t *ceiling_y);

/* Pushes an upright cylinder (feet at feet_y, `height` tall, `radius`) out
 * of the walls, sideways only. The part below feet_y - step is free to
 * overlap (it steps up instead). Stand-in for the game's wall response:
 * the AC slides along a wall at the speed left after the push. Returns 1
 * if it moved. */
int acr_gen1_stage_push(const acr_gen1_stage *s, int32_t *x, int32_t *z, int32_t feet_y, int32_t height,
                        int32_t radius, int32_t step);

/* First polygon hit going from a to b: 1 and *t (0..1024 along the way). */
int acr_gen1_stage_segment(const acr_gen1_stage *s, const int32_t a[3], const int32_t b[3], int32_t *t);

#endif
