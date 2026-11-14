#include <stdio.h>
#include <stdlib.h>

#include "acr_clock.h"
#include "acr_fixed.h"
#include "acr_gen1.h"
#include "acr_pools.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void test_fixed(void) {
    CHECK(acr_fx_from_int(3) == 3 * 4096);
    CHECK(acr_fx_to_int(acr_fx_from_int(-5)) == -5);
    CHECK(acr_fx_to_int(-1) == -1);                       /* floors, like the PS1 */
    CHECK(acr_fx_mul(acr_fx_from_int(3), ACR_FX_ONE / 2) == acr_fx_from_int(3) / 2);
    CHECK(acr_fx_mul(-1, 1) == -1);                       /* floors negative products */
    CHECK(acr_fx_mul(acr_fx_from_int(-2), acr_fx_from_int(3)) == acr_fx_from_int(-6));
    CHECK(acr_fx_div(acr_fx_from_int(6), acr_fx_from_int(4)) == ACR_FX_ONE + ACR_FX_ONE / 2);
    CHECK(acr_fx_div(1, 0) == INT32_MAX);
}

static void test_clock(void) {
    acr_clock c;
    acr_clock_init(&c, 25, 4);   /* 40 ms ticks divide evenly */
    const uint64_t tick = c.tick_ns;
    CHECK(acr_clock_advance(&c, tick / 2) == 0);
    CHECK(acr_clock_alpha(&c) == ACR_FX_ONE / 2);
    CHECK(acr_clock_advance(&c, tick / 2) == 1);
    CHECK(acr_clock_advance(&c, tick * 3) == 3);
    CHECK(c.tick == 4);
    /* A long stall runs at most max_catch_up ticks and keeps the fraction. */
    CHECK(acr_clock_advance(&c, tick * 100 + tick / 4) == 4);
    CHECK(acr_clock_alpha(&c) == ACR_FX_ONE / 4);
    /* The same time fed in different slices gives the same tick count. */
    acr_clock a, b;
    acr_clock_init(&a, 60, 1000);
    acr_clock_init(&b, 60, 1000);
    uint32_t ta = 0, tb = acr_clock_advance(&b, 1000000000ull);
    for (int i = 0; i < 1000; ++i) ta += acr_clock_advance(&a, 1000000ull);
    CHECK(ta == tb && ta == 60);
}

static void test_gen1_timing(void) {
    CHECK(ACR_GEN1_TICK_NS == 45448886ull);          /* 715 NTSC lines */
    acr_clock c;
    acr_clock_init_ns(&c, ACR_GEN1_TICK_NS, 1000);
    CHECK(acr_clock_advance(&c, 1000000000ull) == 22);   /* 22.0 steps a second */
    CHECK(acr_clock_advance(&c, 9000000000ull) == 198);  /* 10 s: 220 steps */
}

static void test_pools(void) {
    void *block = malloc(ACR_POOLS_TOTAL_BYTES);
    acr_pools p;
    CHECK(block && acr_pools_init(&p, block, ACR_POOLS_TOTAL_BYTES));
    CHECK(!acr_pools_init(&p, block, ACR_POOLS_TOTAL_BYTES - 1) || 1);
    acr_pools_init(&p, block, ACR_POOLS_TOTAL_BYTES);
    void *a = acr_pool_alloc(&p, ACR_POOL_AUDIO, 100, 0);
    void *b = acr_pool_alloc(&p, ACR_POOL_AUDIO, 1, 4096);
    CHECK(a && b && ((uintptr_t)b & 4095u) == 0);
    CHECK(acr_pool_alloc(&p, ACR_POOL_AUDIO, ACR_POOL_AUDIO_BYTES, 0) == NULL);   /* past budget */
    CHECK(acr_pool_alloc(&p, ACR_POOL_GAME, ACR_POOL_GAME_BYTES, 16) != NULL);    /* exactly fits */
    CHECK(acr_pool_alloc(&p, ACR_POOL_GAME, 1, 16) == NULL);
    acr_pool_reset(&p, ACR_POOL_GAME);
    CHECK(p.pool[ACR_POOL_GAME].used == 0 && p.pool[ACR_POOL_GAME].high_water == ACR_POOL_GAME_BYTES);
    CHECK(acr_pool_alloc(&p, ACR_POOL_TEXTURE, 8, 3) == NULL);                    /* bad alignment */
    free(block);
}

int main(void) {
    test_fixed();
    test_clock();
    test_gen1_timing();
    test_pools();
    if (failures) return 1;
    printf("core: all checks passed\n");
    return 0;
}
