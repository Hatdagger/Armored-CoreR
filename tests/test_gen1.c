/* Gen 1 compile: the C port must match a straight transcription of the
 * game's instructions (tools/gen1/make_compile_vectors.py) for every input. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_compile.h"
#include "gen1_compile_vectors.h"

static void part(acr_gen1_part *p, const uint16_t *v) {
    memset(p, 0, sizeof *p);
    p->present = (uint8_t)v[0];
    p->weight = v[1];
    p->energy_drain = v[2];
    p->stat_2c = v[3];
    p->stat_2e = v[4];
    p->carry_limit = v[5];
    p->drain_standing = v[6];
    p->stability = v[7];
    p->boost_drain_legs = v[8];
    p->turn_accel = v[9];
    p->turn_speed = (uint8_t)v[10];
    p->fixed_boost = (uint8_t)v[11];
}

int main(void) {
    const size_t n = sizeof k_gen1_vectors / sizeof k_gen1_vectors[0];
    size_t failures = 0;
    for (size_t i = 0; i < n; ++i) {
        const gen1_vector *t = &k_gen1_vectors[i];
        acr_gen1_build b;
        part(&b.core, t->in[0]);    part(&b.booster, t->in[1]); part(&b.head, t->in[2]);
        part(&b.arms, t->in[3]);    part(&b.blade, t->in[4]);   part(&b.hand, t->in[5]);
        part(&b.back_l, t->in[6]);  part(&b.back_r, t->in[7]);  part(&b.generator, t->in[8]);
        part(&b.fcs, t->in[9]);     part(&b.legs, t->in[10]);
        b.option_mask = t->opt;
        acr_gen1_stats s;
        acr_gen1_compile(&b, &s);
        const uint32_t got[14] = { s.energy_capacity, s.rise_drain, s.refill_standing, s.red_zone,
                                   s.refill_moving, s.blade_power, s.stability, s.carried_weight,
                                   s.thrust, s.thrust_80, s.thrust_120, s.turn_accel, s.turn_speed,
                                   s.friction };
        if (memcmp(got, t->out, sizeof got) != 0) {
            if (failures < 5) {
                fprintf(stderr, "FAIL vector %zu:", i);
                for (int k = 0; k < 14; ++k) fprintf(stderr, " %u/%u", (unsigned)got[k], (unsigned)t->out[k]);
                fputc('\n', stderr);
            }
            ++failures;
        }
    }
    /* Beyond the vectors (AC1Recomp docs/mapping/stat-block.md):
     * - back weapon slots +7 / +8 use the +12 / +13 formulas with legs +0x3C;
     * - SP-CND-K adds 12,000 to the energy store;
     * - Human PLUS 6+: rise drain halved, refills doubled and capped by it;
     * - a hand weapon's 64 / 64 gives the crouch turn 11 / 31. */
    {
        acr_gen1_build b;
        memset(&b, 0, sizeof b);
        b.core.present = b.head.present = b.arms.present = b.legs.present = b.generator.present = b.fcs.present = 1;
        b.legs.weight = 2000; b.legs.carry_limit = 5000; b.legs.stability = 1000;
        b.legs.turn_accel = 400; b.legs.turn_speed = 40; b.legs.back_turn = 400;
        b.generator.stat_2c = 28000; b.generator.energy_drain = 6000; b.booster.present = 1;
        b.booster.stat_2e = 4360; b.booster.stat_2c = 9000; b.booster.weight = 200;
        acr_gen1_stats s;
        acr_gen1_compile(&b, &s);
        if (s.back_turn_accel != s.turn_accel) { fprintf(stderr, "FAIL back turn accel\n"); ++failures; }
        b.option_mask = 0x20;
        acr_gen1_compile(&b, &s);
        if (s.energy_capacity != 40000) { fprintf(stderr, "FAIL charge expander\n"); ++failures; }
        const uint16_t rise = s.rise_drain;
        acr_gen1_human_plus_energy(&s, 5);
        if (s.rise_drain != rise) { fprintf(stderr, "FAIL human plus 5\n"); ++failures; }
        acr_gen1_human_plus_energy(&s, 9);
        if (s.rise_drain != rise / 2 || s.refill_standing > s.rise_drain) { fprintf(stderr, "FAIL human plus 9\n"); ++failures; }
        acr_gen1_stats h;
        memset(&h, 0, sizeof h);
        h.back_turn_accel = h.back_turn_speed = 64;
        acr_gen1_motion m;
        acr_gen1_motion_from_stats(&h, &m);
        if (m.crouch_turn_accel != 11 || m.crouch_turn_max != 31) { fprintf(stderr, "FAIL crouch turn\n"); ++failures; }
    }
    if (failures) { fprintf(stderr, "%zu of %zu vectors failed\n", failures, n); return 1; }
    printf("gen1 compile: %zu vectors match\n", n);
    return 0;
}
