#include "acr_gen1_compile.h"

/* The game divides with multiply-by-reciprocal tricks; these reproduce the
 * exact instructions so results match for every input, not just typical ones. */
static uint32_t mulhi_u(uint32_t a, uint32_t b) {
    return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32);
}
static int32_t mulhi_s(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 32);
}
/* Arithmetic right shift, written out so it never depends on the compiler. */
static int32_t sra(int32_t v, unsigned n) {
    return v >= 0 ? (int32_t)((uint32_t)v >> n) : (int32_t)~(~(uint32_t)v >> n);
}

/* (x << 18) * (2^38 / 1000) >> 50: about x * 64 / 1000 (refill). The shift
 * wraps in 32 bits like the original, which matters when drain > output. */
static uint32_t refill(uint32_t output, uint32_t drain, uint32_t max) {
    const uint32_t x = (uint32_t)(output - drain) << 18;
    uint32_t v = mulhi_u(x, 0x10624DD3u) >> 18;
    if (v == 0) v = 1;
    if (v > max) v = max;
    return v;
}

void acr_gen1_compile(const acr_gen1_build *b, acr_gen1_stats *out) {
    const acr_gen1_part *parts[] = { &b->core, &b->booster, &b->head, &b->arms, &b->blade,
                                     &b->hand, &b->back_l, &b->back_r, &b->fcs, &b->legs };
    /* Carried weight: everything except the generator, capped at what the
     * legs can hold (their own weight + carry limit). */
    uint32_t weight = 0;
    for (unsigned i = 0; i < sizeof parts / sizeof parts[0]; ++i)
        if (parts[i]->present) weight += parts[i]->weight;
    const uint32_t cap = (uint32_t)b->legs.weight + b->legs.carry_limit;
    if (cap < weight) weight = cap;
    out->carried_weight = weight;

    out->energy_capacity = b->generator.stat_2c;
    out->red_zone = b->generator.stat_2e;
    out->blade_power = b->blade.present ? b->blade.stat_2c : 0;

    /* Rise drain: booster's boost drain / 10 (legs' own value without one),
     * at least 1. (x << 12) * 0x66666667 >> 34, then >> 12. */
    {
        const uint32_t src = b->booster.present ? b->booster.stat_2e : b->legs.boost_drain_legs;
        const int32_t x = (int32_t)(src << 12);
        int32_t v = sra(mulhi_s(x, 0x66666667), 2) - sra(x, 31);
        v = sra(v, 12);
        out->rise_drain = (uint16_t)(v > 0 ? v : 1);
    }

    /* Refill per step: generator output minus the frame's drain. Standing
     * uses the legs' standing drain, moving their moving drain. */
    const uint32_t frame_drain = (uint32_t)b->head.energy_drain + b->arms.energy_drain +
                                 b->core.energy_drain + b->fcs.energy_drain;
    out->refill_standing = (uint16_t)refill(b->generator.energy_drain,
                                            frame_drain + b->legs.drain_standing, 384);
    out->refill_moving = (uint16_t)refill(b->generator.energy_drain,
                                          frame_drain + b->legs.energy_drain, 192);

    /* Stability: legs' stability (plus a Balancer's bonus) / 1018 * 4096,
     * times carried weight * 256 / 1000 / 512, / 16384, limited to 32..255.
     * Heavier ACs are steadier. */
    {
        uint32_t stability = b->legs.stability;
        if (b->option_mask & ACR_GEN1_OPTION_BALANCER_RE) stability += 0x640;
        else if (b->option_mask & ACR_GEN1_OPTION_BALANCER) stability += 0x320;
        const int32_t x = (int32_t)(stability << 12);
        const int32_t a = sra((int32_t)((uint32_t)mulhi_s(x, (int32_t)0x80C121B3u) + (uint32_t)x), 9)
                          - sra(x, 31);
        const uint32_t w = mulhi_u(weight << 8, 0x10624DD3u) >> 9;
        int32_t v = sra((int32_t)((uint32_t)a * w), 14);
        if (v < 33) v = 32;
        if (v > 255) v = 255;
        out->stability = (uint8_t)v;
    }
}
