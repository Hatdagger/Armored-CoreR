/* Original Xbox (nxdk) smoke test: proves the engine core builds and runs
 * on the console's CPU and memory. It reserves the memory pools, runs a
 * made-up AC through a few seconds of movement and lighting, and shows the
 * results on screen. The real Xbox shell comes later. */
#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "acr_clock.h"
#include "acr_pools.h"
#include "acr_gen1_sim.h"
#include "acr_light.h"

int main(void) {
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    void *block = malloc(ACR_POOLS_TOTAL_BYTES);
    acr_pools pools;
    const int pools_ok = block && acr_pools_init(&pools, block, ACR_POOLS_TOTAL_BYTES);

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
    d.landing_steps = 20;
    d.step_rate = 60;                       /* 60 steps a second, the game's 22 ticks */
    d.gen1_rate = 22;

    acr_gen1_ac ac;
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    /* A step of walking first: from a stand the booster jumps. */
    const acr_gen1_input walk = { 1, 0, 0, 0, 0 }, boost = { 1, 0, 0, 1, 0 };
    const DWORD t0 = GetTickCount();
    for (int i = 0; i < 60 * 3; ++i) acr_gen1_ac_step(&ac, &d, i < 3 ? walk : boost);
    const DWORD t1 = GetTickCount();

    acr_lighting l;
    acr_light_default(&l);
    uint8_t c[3];
    acr_light_color(&l, 0, 4096, 0, 160, 160, 160, c);

    debugPrint("Armored-CoreR core on Xbox\n\n");
    debugPrint("memory pools: %s (%d KB)\n", pools_ok ? "ok" : "FAILED", (int)(ACR_POOLS_TOTAL_BYTES / 1024));
    debugPrint("3 s of ground boost: z = %d, energy = %d (%d ms)\n",
               (int)(ac.z / 64), (int)ac.energy, (int)(t1 - t0));
    debugPrint("lit top face: %d %d %d\n", c[0], c[1], c[2]);
    for (;;) Sleep(1000);
    return 0;
}
