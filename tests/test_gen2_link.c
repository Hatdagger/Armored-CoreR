/* Gen 2 in a Gen 1 fight (acr_gen2_link.h): axes, units and the 60-a-second
 * clock against Gen 1's step rates. Gen 2 PR. */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "acr_gen2_link.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void run(acr_gen2_link *l, acr_gen1_ac *g1, acr_gen1_input in, int rate, int steps)
{
    acr_gen2_input i = acr_gen2_link_input(&in, 0);
    for (int k = 0; k < steps; ++k) acr_gen2_link_step(l, g1, &i, rate, NULL, 20000);
}

static void fresh(acr_gen2_link *l, acr_gen1_ac *g1, uint16_t yaw)
{
    memset(g1, 0, sizeof *g1);
    g1->yaw = yaw;
    acr_gen2_link_init(l, &acr_gen2_tuning_ac2, &acr_gen2_test_build, &acr_gen2_test_gait);
}

int main(void)
{
    acr_gen2_link l;
    acr_gen1_ac g1;
    acr_gen1_input fwd, none, right, boost;
    memset(&none, 0, sizeof none);
    fwd = none; fwd.move = 1;
    right = none; right.turn = 1;
    boost = none; boost.boost = 1;

    /* Walking forward at yaw 0 goes along Gen 1's +z (its forward), at the
     * walk clip's pace: 30 units in 64 frames after the start clip. */
    fresh(&l, &g1, 0);
    run(&l, &g1, fwd, 60, 120);
    EXPECT(l.frames == 120);
    EXPECT(g1.z > 0);
    EXPECT(labs((long)g1.x) < 64);
    {
        const float metres = (float)g1.z / ACR_GEN2_SIM_PER_UNIT;
        EXPECT(metres > 40.0f && metres < 60.0f);   /* ~0.47 m a frame, less the start */
        EXPECT(fabsf(acr_gen2_link_kmh(&l) - 101.25f) < 3.0f);
    }

    /* Facing a quarter turn right (Gen 1 yaw 1024): forward is +x. */
    fresh(&l, &g1, 1024);
    run(&l, &g1, fwd, 60, 60);
    EXPECT(g1.x > 0 && labs((long)g1.z) < 64);
    EXPECT(g1.yaw == 1024);

    /* Turning right grows the Gen 1 yaw. */
    fresh(&l, &g1, 0);
    run(&l, &g1, right, 60, 20);
    EXPECT(g1.yaw > 0 && g1.yaw < 2048);

    /* Gen 1's own pace (22 steps a second) still runs 60 AC2 frames a second. */
    fresh(&l, &g1, 0);
    run(&l, &g1, fwd, 22, 44);
    EXPECT(l.frames == 120);
    {
        acr_gen2_link l60;
        acr_gen1_ac g60;
        fresh(&l60, &g60, 0);
        run(&l60, &g60, fwd, 60, 120);
        EXPECT(g1.z == g60.z);
    }

    /* A plain jump: squat on the ground, then up (y negative), then back down. */
    fresh(&l, &g1, 0);
    run(&l, &g1, boost, 60, 10);
    EXPECT(g1.y == 0 && l.ac.state == ACR_GEN2_ST_CROUCH);
    run(&l, &g1, none, 60, 40);
    EXPECT(g1.y < 0);
    run(&l, &g1, none, 60, 240);
    EXPECT(g1.y == 0);

    /* The arena edge stops it. */
    fresh(&l, &g1, 0);
    run(&l, &g1, fwd, 60, 60 * 60);
    EXPECT(g1.z / 64 <= 20000 && g1.z / 64 > 19900);

    /* Something else moves the Gen 1 AC (a respawn): the body follows. */
    g1.x = 64 * 5000; g1.z = 0; g1.y = 0;
    run(&l, &g1, none, 60, 1);
    EXPECT(labs((long)(g1.x - 64 * 5000)) < 64);

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen2 link: ok\n");
    return 0;
}
