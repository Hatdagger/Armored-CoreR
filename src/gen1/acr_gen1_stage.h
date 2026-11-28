/* Gen 1 arena stages: the collision polygons ACs stand on and run into, and
 * that shots hit. Integer only, like the rest of the fight.
 *
 * Read from acr-import's stages/<NN>/collision.obj (Master of Arena FDAT
 * 5s+4 chunk 7, placed by the area records; docs/research/gen1-stages.md).
 * The OBJ is in the engine's world with y up; here y is down, as in the
 * fight (feet at y, the floor of Green Silo at -662). All in world units.
 *
 * The game's own rules (one box through the stage, pushed out of the
 * nearest polygon; docs/research/gen1-stages.md, "Walls and floors") are at
 * the end and need acr-import's collision.txt; the floor / ceiling / push
 * functions before them are the stand-ins used for an older import. Shots
 * use the segment test either way. */
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
    int16_t area;             /* the area record that placed it (the OBJ's "o areaN"), -1 unknown */
} acr_gen1_stage_poly;

/* The collision as the game keeps it (acr-import's collision.txt), for the
 * port of the game's own box sweep below. Game axes: x NOT mirrored, y down.
 * A model's points are in model units (4 a world unit); an area places a
 * model at its centre, turned by quarter turns. */
typedef struct acr_gen1_stage_gpoly {
    uint16_t flags;           /* bit 0 four corners, bits 2-3 main axis (0 x, 1 y, 2 z) */
    int16_t  n[3];            /* the game's normal x 4096: points the way a body moves into it */
    int32_t  d;               /* n . p + d = 0, model units x 4096 */
    uint16_t c[4];            /* corner words: vertex x 8 in bits 3-14; low 3 bits flags
                                 (c0: 1 = test inside the outline; c1: class, skipped by
                                 the body's mask; c3: priority) */
} acr_gen1_stage_gpoly;

typedef struct acr_gen1_stage_gmodel {
    int16_t (*v)[3];
    uint32_t verts;
    acr_gen1_stage_gpoly *p;
    uint32_t polys;
} acr_gen1_stage_gmodel;

typedef struct acr_gen1_stage_area {
    int16_t lo[3], hi[3];     /* world units, game axes */
    int16_t centre[3];
    uint8_t quarter;
    int16_t model;            /* index into gmodel, or -1 */
} acr_gen1_stage_area;

/* An out-of-area box (stage objects: VS stage code's box handler, recomp
 * 0x80098124; arena stage code, e.g. Ocean Platform's 0x8009F490): centre
 * plus signed bytes x 256 either way, and a bit the AC sets in its player
 * block +0x08 while inside (ends included). Engine axes, world units. */
typedef struct acr_gen1_stage_bound {
    int32_t lo[3], hi[3];
    uint8_t bit;
} acr_gen1_stage_bound;

#define ACR_GEN1_STAGE_BOUNDS 8
#define ACR_GEN1_STAGE_MOVED  160   /* areas that can be moved: doors + breakables (map 2: 53) + lifts + ships */
/* Box 0 is the whole level; boxes 1 and 2 overlap to make the arena floor.
 * Inside box 0 and outside one inner box: a warning. Inside box 0 and
 * outside both: that side loses (bits 0-2 exactly 1, the arena engine's
 * end check 0x800528E4; measured in the recomp: bits 7 inside, 5 warning,
 * 1 lost). Read from the disc by acr-import (stage.toml out_of_area). */
#define ACR_GEN1_AREA_INNER 6

typedef struct acr_gen1_stage {
    acr_gen1_stage_poly *poly;
    uint32_t polys;
    int32_t lo[3], hi[3];     /* bounding box of everything */
    /* The game's own collision (0 areas: an import from before it; the
     * stand-in rules below are used then). */
    acr_gen1_stage_area *area;
    uint32_t areas;
    acr_gen1_stage_gmodel *gmodel;
    uint32_t gmodels;
    /* Out-of-area boxes (0: none known for this stage; the rule is off). */
    acr_gen1_stage_bound bound[ACR_GEN1_STAGE_BOUNDS];
    uint8_t bounds;
    int16_t next_area;        /* while parsing: the area of the polygons that follow */
    /* Areas moved from where the disc puts them, and by how much (game axes). */
    struct { int16_t area; int32_t off[3]; } moved[ACR_GEN1_STAGE_MOVED];
    uint8_t moveds;
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

/* Moves area `area` (the importer's number) by `step` (game axes: x not
 * mirrored, y down): its polygons and, with the game's collision, its
 * record (the game's 0x80077D8C adds the step to the centre and boxes).
 * For the moving map parts (acr_gen1_gimmick.h). */
void acr_gen1_stage_move_area(acr_gen1_stage *s, int area, const int32_t step[3]);
/* Puts area `area` `off` from where the disc has it (moving it by the
 * difference from where it is now). */
void acr_gen1_stage_place_area(acr_gen1_stage *s, int area, const int32_t off[3]);

/* First polygon hit going from a to b: 1 and *t (0..1024 along the way). */
int acr_gen1_stage_segment(const acr_gen1_stage *s, const int32_t a[3], const int32_t b[3], int32_t *t);

/* ---- the game's own rules (VS engine, FDAT 209; gen1-stages.md, "Walls
 * and floors"). Positions and steps in the engine's world (x mirrored
 * against the game's), whole world units, y down. ---- */

/* Adds the game's collision (collision.txt) to a parsed stage. 1 on success. */
int acr_gen1_stage_parse_game(const char *text, acr_gen1_stage *s);

/* AC +0x28 / +0x2A (the stage box: 100 either side, 212 tall), +0x72 (75),
 * +0x43 (the mask of polygon classes an AC passes through: 5). */
#define ACR_GEN1_STAGE_BOX_HALF   100
#define ACR_GEN1_STAGE_BOX_HEIGHT 212
#define ACR_GEN1_STAGE_PROBE_HALF  37   /* 75 >> 1, the floor probe (0x8007C964) */
#define ACR_GEN1_STAGE_AC_MASK      5
/* On the ground while the floor is less than this below the feet (0x80083960). */
#define ACR_GEN1_STAGE_GROUNDED    50

/* One try (0x80078AB0): the box `half` either side, `height` tall, its
 * bottom at pos + step; if it meets the stage, *step becomes the step the
 * game corrects it to (the move plus the push out of the nearest polygon,
 * to a quarter unit off it) and 1 is returned. */
int acr_gen1_stage_try(const acr_gen1_stage *s, const int32_t pos[3], int32_t step[3], int32_t half, int32_t height,
                       uint8_t mask);

/* The move (0x80078C4C, the stage part): up to six tries, each with the
 * step the last one corrected to; still meeting the stage after six, or a
 * try that changes nothing, and the AC stays where it is. *step becomes
 * the step taken. 1 if the stage changed it. */
int acr_gen1_stage_move(const acr_gen1_stage *s, const int32_t pos[3], int32_t step[3], int32_t half, int32_t height,
                        uint8_t mask);

/* The floor under the AC (0x8007C964 / 0x800792BC, AC +0x2C): the highest
 * floor (normal facing up) met by the four corner columns of a box `half`
 * either side, from the top of the AC (feet - height) to 4,096 below the
 * feet; one above the feet counts as at the feet. 0 when none (the game's
 * 32,767). */
int acr_gen1_stage_floor_probe(const acr_gen1_stage *s, const int32_t pos[3], int32_t half, int32_t height,
                               uint8_t mask, int32_t *floor_y);

/* The out-of-area bits for an AC at (x, y, z): bit k set while inside a
 * box with bit k (player block +0x08). */
uint8_t acr_gen1_stage_area_bits(const acr_gen1_stage *s, int32_t x, int32_t y, int32_t z);
/* Does the stage have the inner boxes the out-of-area rule needs? */
int acr_gen1_stage_has_area_rule(const acr_gen1_stage *s);

#endif
