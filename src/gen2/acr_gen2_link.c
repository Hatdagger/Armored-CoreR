/* Gen 2 in a Gen 1 fight: see acr_gen2_link.h. */
#include "acr_gen2_link.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Compile test vector "heavy two legs, test AC parts" (tests/gen2_compile_vectors.h). */
const acr_gen2_build acr_gen2_test_build = {
    {1, 114, 350}, {1, 1016, 1083}, {1, 1400, 1510}, {1, 3428, 2414}, {1, 204, 27}, {1, 16, 24},
    {1, 348, 0}, {1, 153, 201}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 405, 6}, {0, 0, 0},
    0, 88, 7630, 162, 140, 906, 0, 0, 10, 109, 10,   /* legs */
    2460, 20000, 3600,  9800, 4350,  9947, 34000, 4500,  0x0000, 0
};

/* B 453 by the walk table (gen2-movement.md): loops of 30 units in 64 frames,
 * back 15 in 76; start clip 1.5 units in 20; the group's longest squat and
 * landing. The game reads all of these from the clips: gen2_ac.c overrides
 * them from the imported ac.glb when it has one. */
const acr_gen2_gait acr_gen2_test_gait = {
    { 30.0f / 64, 30.0f / 64, 30.0f / 64, 30.0f / 64, 30.0f / 64, 15.0f / 76, 15.0f / 76, 15.0f / 76 },
    { 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.0f, 1.0f, 1.0f },
    { 20, 20, 20, 20, 20, 20, 20, 20 },
    28, 24, 52
};

#define TWO_PI 6.2831853f

void acr_gen2_link_init(acr_gen2_link *l, const acr_gen2_tuning *t, const acr_gen2_build *b,
                        const acr_gen2_gait *g)
{
    memset(l, 0, sizeof *l);
    l->tuning = t;
    l->gait = *g;
    acr_gen2_compile(t, b, &l->stats);
    acr_gen2_move_init(&l->ac, &l->stats);
}

static uint16_t yaw_to_gen1(float yaw)
{
    int32_t u = (int32_t)lroundf(yaw * (4096.0f / TWO_PI));
    return (uint16_t)(u & 4095);
}

static float yaw_from_gen1(uint16_t yaw)
{
    float r = (float)(yaw & 4095) * (TWO_PI / 4096.0f);
    return r > 3.14159265f ? r - TWO_PI : r;
}

void acr_gen2_link_sync(acr_gen2_link *l, const acr_gen1_ac *g1)
{
    int32_t energy = l->ac.energy;
    uint8_t charging = l->ac.charging;
    const int keep = l->synced;
    acr_gen2_move_init(&l->ac, &l->stats);
    if (keep) { l->ac.energy = energy; l->ac.charging = charging; }   /* a teleport, not a new AC */
    l->ac.x = -(float)g1->x / ACR_GEN2_SIM_PER_UNIT;
    l->ac.y = (float)g1->y / ACR_GEN2_SIM_PER_UNIT;
    l->ac.z = -(float)g1->z / ACR_GEN2_SIM_PER_UNIT;
    l->ac.yaw = yaw_from_gen1(g1->yaw);
    l->last_x = g1->x; l->last_y = g1->y; l->last_z = g1->z;
    l->frame_acc = 0;
    l->frames = 0;
    l->synced = 1;
}

acr_gen2_input acr_gen2_link_input(const acr_gen1_input *in, int overboost)
{
    acr_gen2_input o;
    memset(&o, 0, sizeof o);
    o.forward = in->move > 0;
    o.back = in->move < 0;
    o.right = in->strafe > 0;
    o.left = in->strafe < 0;
    o.turn = (int8_t)(in->turn > 0 ? 1 : in->turn < 0 ? -1 : 0);   /* both: + = right */
    o.boost = (uint8_t)(in->boost || in->cruise);
    o.overboost = (uint8_t)(overboost != 0);
    return o;
}

/* World units (Gen 1) of an AC2 position. */
static void to_world(const acr_gen2_ac *a, int32_t w[3])
{
    w[0] = (int32_t)lroundf(-a->x * ACR_GEN2_WORLD_PER_UNIT);
    w[1] = (int32_t)lroundf(a->y * ACR_GEN2_WORLD_PER_UNIT);
    w[2] = (int32_t)lroundf(-a->z * ACR_GEN2_WORLD_PER_UNIT);
}

static int in_air(const acr_gen2_ac *a)
{
    return a->state == ACR_GEN2_ST_AIR || a->state == ACR_GEN2_ST_OVERBOOST || a->state == ACR_GEN2_ST_CROUCH;
}

/* The floor under the body, in AC2 units; also lets a two-step drop be
 * walked down rather than fallen off (Gen 1's ACR_GEN1_STEP_DOWN). */
static float ground_under(acr_gen2_link *l, const acr_gen1_stage *stage)
{
    int32_t w[3], fy;
    if (!stage) return 0.0f;
    to_world(&l->ac, w);
    if (stage->areas ? !acr_gen1_stage_floor_probe(stage, w, ACR_GEN1_STAGE_PROBE_HALF, ACR_GEN1_STAGE_BOX_HEIGHT,
                                                   ACR_GEN1_STAGE_AC_MASK, &fy)
                     : !acr_gen1_stage_floor(stage, w[0], w[2], w[1] - ACR_GEN1_STEP_UP, &fy))
        return 1.0e6f;     /* nothing below: keep falling (out of the area rules take over) */
    if (!in_air(&l->ac) && fy > w[1] && fy - w[1] <= ACR_GEN1_STEP_DOWN)
        l->ac.y = (float)fy / ACR_GEN2_WORLD_PER_UNIT;
    return (float)fy / ACR_GEN2_WORLD_PER_UNIT;
}

/* After an AC2 frame moved the body from `from` (world units): walls and
 * ceilings by the Gen 1 stage move. Sideways the box rides ACR_GEN1_STEP_UP
 * above the feet, so a ledge lower than that is climbed (the AC2 step puts
 * the feet on it next frame); up, only while rising. */
static void collide(acr_gen2_link *l, const int32_t from[3], const acr_gen1_stage *stage, int32_t arena_half)
{
    int32_t to[3];
    to_world(&l->ac, to);
    if (stage && !stage->areas) {
        /* Only the drawn stage's polygons (no game collision): Gen 1's stand-in push. */
        int32_t x = to[0], z = to[2];
        if (acr_gen1_stage_push(stage, &x, &z, to[1], ACR_GEN1_STAGE_BOX_HEIGHT, 75, ACR_GEN1_STEP_UP)) {
            if ((int64_t)(x - from[0]) * (to[0] - from[0]) <= 0) l->ac.vx = 0.0f;
            if ((int64_t)(z - from[2]) * (to[2] - from[2]) <= 0) l->ac.vz = 0.0f;
            l->ac.x = -(float)x / ACR_GEN2_WORLD_PER_UNIT;
            l->ac.z = -(float)z / ACR_GEN2_WORLD_PER_UNIT;
        }
    } else if (stage) {
        int32_t pos[3] = { from[0], from[1] - ACR_GEN1_STEP_UP, from[2] };
        int32_t step[3] = { to[0] - from[0], 0, to[2] - from[2] };
        if ((step[0] || step[2]) &&
            acr_gen1_stage_move(stage, pos, step, ACR_GEN1_STAGE_BOX_HALF, ACR_GEN1_STAGE_BOX_HEIGHT - ACR_GEN1_STEP_UP,
                                ACR_GEN1_STAGE_AC_MASK)) {
            const int32_t want_x = to[0] - from[0], want_z = to[2] - from[2];
            l->ac.x = -(float)(from[0] + step[0]) / ACR_GEN2_WORLD_PER_UNIT;
            l->ac.z = -(float)(from[2] + step[2]) / ACR_GEN2_WORLD_PER_UNIT;
            /* slide along: keep the velocity's part the wall let through */
            if ((int64_t)step[0] * want_x <= 0) l->ac.vx = 0.0f;
            if ((int64_t)step[2] * want_z <= 0) l->ac.vz = 0.0f;
        }
        if (to[1] < from[1]) {
            int32_t p2[3] = { -(int32_t)lroundf(l->ac.x * ACR_GEN2_WORLD_PER_UNIT), from[1], -(int32_t)lroundf(l->ac.z * ACR_GEN2_WORLD_PER_UNIT) };
            int32_t up[3] = { 0, to[1] - from[1], 0 };
            if (acr_gen1_stage_move(stage, p2, up, ACR_GEN1_STAGE_BOX_HALF, ACR_GEN1_STAGE_BOX_HEIGHT,
                                    ACR_GEN1_STAGE_AC_MASK)) {
                l->ac.y = (float)(from[1] + up[1]) / ACR_GEN2_WORLD_PER_UNIT;
                if (l->ac.vy < 0.0f) l->ac.vy = 0.0f;   /* head on the ceiling */
            }
        }
    } else {
        const float lim = (float)arena_half / ACR_GEN2_WORLD_PER_UNIT;
        if (l->ac.x > lim) { l->ac.x = lim; l->ac.vx = 0.0f; }
        if (l->ac.x < -lim) { l->ac.x = -lim; l->ac.vx = 0.0f; }
        if (l->ac.z > lim) { l->ac.z = lim; l->ac.vz = 0.0f; }
        if (l->ac.z < -lim) { l->ac.z = -lim; l->ac.vz = 0.0f; }
    }
}

void acr_gen2_link_step(acr_gen2_link *l, acr_gen1_ac *g1, const acr_gen2_input *in, int step_rate,
                        const acr_gen1_stage *stage, int32_t arena_half)
{
    const int32_t one_m = (int32_t)ACR_GEN2_SIM_PER_UNIT;
    if (step_rate <= 0) step_rate = 60;
    if (!l->synced || labs((long)(g1->x - l->last_x)) > one_m || labs((long)(g1->y - l->last_y)) > one_m ||
        labs((long)(g1->z - l->last_z)) > one_m)
        acr_gen2_link_sync(l, g1);

    /* 60 AC2 frames a second whatever the Gen 1 step rate. */
    if (in->overboost) l->ob_pending = 1;
    l->frame_acc += 60;
    while (l->frame_acc >= step_rate) {
        int32_t from[3];
        acr_gen2_input i = *in;
        float gy;
        l->frame_acc -= step_rate;
        /* The overboost button is a press: only the first frame of it. */
        i.overboost = l->ob_pending;     /* a press: one frame */
        l->ob_pending = 0;
        gy = ground_under(l, stage);
        to_world(&l->ac, from);
        acr_gen2_move_step(&l->ac, l->tuning, &l->stats, &l->gait, &i, gy);
        collide(l, from, stage, arena_half);
        l->frames++;
    }

    /* Into the Gen 1 AC: this step's position, nothing for its own rules to add. */
    g1->x = g1->px = g1->sx = (int32_t)lroundf(-l->ac.x * ACR_GEN2_SIM_PER_UNIT);
    g1->y = g1->py = g1->sy = (int32_t)lroundf(l->ac.y * ACR_GEN2_SIM_PER_UNIT);
    g1->z = g1->pz = g1->sz = (int32_t)lroundf(-l->ac.z * ACR_GEN2_SIM_PER_UNIT);
    g1->yaw = g1->pyaw = g1->syaw = yaw_to_gen1(l->ac.yaw);
    g1->vx = g1->vy = g1->vz = 0;
    g1->turn_rate = 0;
    l->last_x = g1->x; l->last_y = g1->y; l->last_z = g1->z;
}

float acr_gen2_link_kmh(const acr_gen2_link *l)
{
    return l->ac.speed_shown * ACR_GEN2_KMH_PER_SPEED;
}
