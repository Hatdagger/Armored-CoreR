/* Gen 1 movement stepping, with made-up stats and tracks. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_sim.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const acr_gen1_root_key k_walk[] = { { 2, 0, 0, -248 }, { 4, 0, 0, -548 } };
static const acr_gen1_root_key k_jump[] = { { 7, 0, 0, 0 }, { 9, 0, -400, 0 } };

static acr_gen1_ac_def make_def(void) {
    acr_gen1_ac_def d;
    memset(&d, 0, sizeof d);
    d.stats.energy_capacity = 28000;
    d.stats.rise_drain = 400;
    d.stats.refill_standing = 200;
    d.stats.refill_moving = 100;
    d.stats.red_zone = 8000;
    d.stats.thrust = 32; d.stats.thrust_80 = 25; d.stats.thrust_120 = 38;
    d.stats.turn_accel = 22; d.stats.turn_speed = 41; d.stats.friction = 43;
    acr_gen1_motion_from_stats(&d.stats, &d.motion);
    static acr_gen1_root_track walk = { 4, 2, k_walk }, jump = { 9, 2, k_jump };
    d.anim[ACR_GEN1_ANIM_WALK] = &walk;
    d.anim[ACR_GEN1_ANIM_JUMP] = &jump;
    d.landing_steps = 20;
    return d;
}

int main(void) {
    for (int a = 0; a < 4096; a += 64) {
        const long s = acr_gen1_sin((uint16_t)a), c = acr_gen1_cos((uint16_t)a);
        EXPECT(s * s + c * c > 4090L * 4090L && s * s + c * c < 4102L * 4102L);
    }
    const acr_gen1_ac_def d = make_def();
    acr_gen1_ac ac;

    /* Walking forward at facing 0 moves +z by the track / 4: 31, 31, 37, 38. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    const acr_gen1_input fwd = { 1, 0, 0, 0, 0, 0 };
    int32_t last = ac.z, got[4];
    for (int i = 0; i < 4; ++i) { acr_gen1_ac_step(&ac, &d, fwd); got[i] = (ac.z - last) / 64; last = ac.z; }
    EXPECT(got[0] + got[1] == 62 && got[2] + got[3] == 75);

    /* Walking forward and right at facing 0 moves along the diagonal. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    const acr_gen1_input diag = { 1, 0, 1, 0, 0, 0 };
    for (int i = 0; i < 4; ++i) acr_gen1_ac_step(&ac, &d, diag);
    EXPECT(ac.x > 0 && ac.z > 0 && ac.x - ac.z < 4 * 64 && ac.z - ac.x < 4 * 64);

    /* Turning: +6 a step up to 38. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    const acr_gen1_input turn = { 0, 1, 0, 0, 0, 0 };
    for (int i = 0; i < 10; ++i) acr_gen1_ac_step(&ac, &d, turn);
    EXPECT(ac.turn_rate == 38);

    /* Ground boost: limited to 75 units a step on an axis. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    const acr_gen1_input boost = { 1, 0, 0, 1, 0, 0 };
    for (int i = 0; i < 20; ++i) { last = ac.z; acr_gen1_ac_step(&ac, &d, boost); }
    EXPECT(ac.z - last == 75 * 64);
    EXPECT(ac.energy == 28000 - 20 * 200);

    /* Rise: the press, 7 still steps, lift-off at 3,200, then rise acceleration. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    const acr_gen1_input up = { 0, 0, 0, 1, 0, 0 };
    for (int i = 0; i < 9; ++i) acr_gen1_ac_step(&ac, &d, up);
    EXPECT(ac.state == ACR_GEN1_AIR && ac.vy == -3200 - 64);
    /* Let go: falls back and lands, then waits. */
    const acr_gen1_input none = { 0, 0, 0, 0, 0, 0 };
    int steps = 0;
    while (ac.state == ACR_GEN1_AIR && steps < 200) { acr_gen1_ac_step(&ac, &d, none); ++steps; }
    EXPECT(ac.state == ACR_GEN1_LANDING && ac.y == 0);
    for (int i = 0; i < 20; ++i) acr_gen1_ac_step(&ac, &d, none);
    EXPECT(ac.state == ACR_GEN1_GROUND);

    /* Boost hop: boost forward, let go one step, press again -> airborne at speed. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    for (int i = 0; i < 12; ++i) acr_gen1_ac_step(&ac, &d, boost);
    const acr_gen1_input fwd_only = { 1, 0, 0, 0, 0, 0 };
    acr_gen1_ac_step(&ac, &d, fwd_only);
    EXPECT(ac.state == ACR_GEN1_GROUND && ac.vz > 0);
    acr_gen1_ac_step(&ac, &d, boost);
    EXPECT(ac.state == ACR_GEN1_AIR && ac.vy < 0 && ac.vz > 0);

    /* Energy: a quarter of the cost below the red zone; running dry locks the
     * booster until the bar is full, refilling at half speed meanwhile. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    ac.energy = 9000;                            /* above the red zone (8000) */
    ac.state = ACR_GEN1_AIR; ac.y = -100000;
    acr_gen1_ac_step(&ac, &d, up);
    EXPECT(ac.energy == 9000 - 400);
    ac.energy = 7000;
    acr_gen1_ac_step(&ac, &d, up);
    EXPECT(ac.energy == 7000 - 100);
    ac.energy = 50;
    acr_gen1_ac_step(&ac, &d, up);
    EXPECT(ac.energy == 0 && ac.drained);
    ac.state = ACR_GEN1_GROUND; ac.y = 0; ac.vx = ac.vy = ac.vz = 0;
    acr_gen1_ac_step(&ac, &d, none);
    EXPECT(ac.energy == 100);                    /* standing refill 200, halved */
    ac.energy = 27950;
    acr_gen1_ac_step(&ac, &d, up);               /* still locked: no rise */
    EXPECT(ac.state == ACR_GEN1_GROUND && ac.drained == 0 && ac.energy == 28000);

    /* A tap shorter than a step (released and pressed again between steps). */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    for (int i = 0; i < 12; ++i) acr_gen1_ac_step(&ac, &d, boost);
    const acr_gen1_input tap = { 1, 0, 0, 1, 1, 0 };
    acr_gen1_ac_step(&ac, &d, tap);
    EXPECT(ac.state == ACR_GEN1_AIR);

    /* View tilt: L2 up to the limit, held there; L2+R2 back to level. */
    {
        acr_gen1_ac t;
        acr_gen1_ac_spawn(&t, &d, 0, 0, 0);
        acr_gen1_input look = { 0, 0, 0, 0, 0, 1 };
        acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == ACR_GEN1_PITCH_RATE);
        for (int k = 0; k < 40; ++k) acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == ACR_GEN1_PITCH_MAX);
        look.look = 0;
        acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == ACR_GEN1_PITCH_MAX);
        look.look = 2;
        for (int k = 0; k < 5; ++k) acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == 0);
        look.look = -1;
        for (int k = 0; k < 40; ++k) acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == -ACR_GEN1_PITCH_MAX);
    }
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 sim: all passed\n");
    return 0;
}
