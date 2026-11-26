#include "acr_gen1_perf.h"

#include <string.h>

/* Radar range with Human PLUS on and no radar fitted (stat-block.md +0x1E). */
#define HUMAN_PLUS_RADAR 5120

int acr_gen1_perf_complete(const acr_gen1_perf_build *b) {
    static const acr_gen1_slot need[] = {
        ACR_GEN1_SLOT_HEAD, ACR_GEN1_SLOT_CORE, ACR_GEN1_SLOT_ARMS, ACR_GEN1_SLOT_LEGS,
        ACR_GEN1_SLOT_GENERATOR, ACR_GEN1_SLOT_FCS
    };
    for (unsigned i = 0; i < sizeof need / sizeof need[0]; ++i)
        if (!b->part[need[i]]) return 0;
    return b->caterpillar || b->part[ACR_GEN1_SLOT_BOOSTER] != NULL;
}

acr_gen1_grade acr_gen1_grade_of(int32_t overall) {
    if (overall >= 25000) return ACR_GEN1_GRADE_SUPERFINE;
    if (overall >= 20000) return ACR_GEN1_GRADE_GREAT;
    if (overall >= 15000) return ACR_GEN1_GRADE_FINE;
    if (overall >= 10000) return ACR_GEN1_GRADE_OK;
    return ACR_GEN1_GRADE_POOR;
}

const char *acr_gen1_grade_name(acr_gen1_grade g) {
    switch (g) {
    case ACR_GEN1_GRADE_SUPERFINE: return "SUPERFINE";
    case ACR_GEN1_GRADE_GREAT: return "GREAT";
    case ACR_GEN1_GRADE_FINE: return "FINE";
    case ACR_GEN1_GRADE_OK: return "OK";
    case ACR_GEN1_GRADE_POOR: return "POOR";
    }
    return "";
}

void acr_gen1_performance(const acr_gen1_perf_build *b, acr_gen1_perf *o) {
    memset(o, 0, sizeof *o);
    const acr_gen1_perf_part *const *p = b->part;

    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s)
        if (p[s]) { o->weight += (int32_t)b->weight[s]; o->price += (int32_t)b->price[s]; }
    o->price += (int32_t)b->option_price;

    /* OFFENSIVE: weapon arms alone, else hand weapon + blade; plus both back units. */
    if (b->weapon_arms) {
        if (p[ACR_GEN1_SLOT_ARMS]) o->offensive += p[ACR_GEN1_SLOT_ARMS]->attack;
    } else {
        if (p[ACR_GEN1_SLOT_HAND]) o->offensive += p[ACR_GEN1_SLOT_HAND]->attack;
        if (p[ACR_GEN1_SLOT_BLADE]) o->offensive += p[ACR_GEN1_SLOT_BLADE]->attack;
    }
    if (p[ACR_GEN1_SLOT_BACK_L]) o->offensive += p[ACR_GEN1_SLOT_BACK_L]->attack;
    if (p[ACR_GEN1_SLOT_BACK_R]) o->offensive += p[ACR_GEN1_SLOT_BACK_R]->attack;

    /* AP and DEFENSIVE: head, core, arms, legs. The screens scale in 1/4096ths. */
    static const acr_gen1_slot body[] = {
        ACR_GEN1_SLOT_HEAD, ACR_GEN1_SLOT_CORE, ACR_GEN1_SLOT_ARMS, ACR_GEN1_SLOT_LEGS
    };
    int64_t shell = 0, energy = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const acr_gen1_perf_part *q = p[body[i]];
        if (!q) continue;
        o->ap += q->ap;
        shell += q->defense_shell;
        energy += q->defense_energy;
    }
    shell <<= 12;
    energy <<= 12;
    if (b->options & ACR_GEN1_OPTION_SHELL_SCREEN) shell = shell * 115 / 100;
    if (b->options & ACR_GEN1_OPTION_ENERGY_SCREEN) energy = energy * 115 / 100;
    o->defensive = (int32_t)((shell + energy) >> 12);

    /* STABILITY */
    const acr_gen1_perf_part *legs = p[ACR_GEN1_SLOT_LEGS];
    if (legs) o->stability = legs->stability;
    if (b->options & ACR_GEN1_OPTION_ABSORBER) o->stability += 800;
    if (b->options & ACR_GEN1_OPTION_BALANCER_RE) o->stability += 1600;
    else if (b->options & ACR_GEN1_OPTION_BALANCER) o->stability += 800;

    /* MOBILITY: the division comes first, in whole numbers. */
    if (legs) {
        o->moving_speed = legs->moving_speed * 10;
        o->turning_speed = legs->turn_accel * 10;
    }
    const int32_t power = b->caterpillar ? 9800
                        : p[ACR_GEN1_SLOT_BOOSTER] ? p[ACR_GEN1_SLOT_BOOSTER]->power : 0;
    if (o->weight > 0) o->boost_ability = power / o->weight * 1000;
    o->mobility = o->moving_speed + o->turning_speed + o->boost_ability;

    /* RADAR RANGE: a back radar replaces the head's (the best one if two);
     * Human PLUS with no radar at all gives 5,120. */
    int32_t radar = 0, back_radar = 0;
    for (int s = ACR_GEN1_SLOT_BACK_L; s <= ACR_GEN1_SLOT_BACK_R; ++s)
        if (p[s] && p[s]->radar_unit) {
            back_radar = 1;
            if (p[s]->radar_range > radar) radar = p[s]->radar_range;
        }
    if (!back_radar && p[ACR_GEN1_SLOT_HEAD]) radar = p[ACR_GEN1_SLOT_HEAD]->radar_range;
    if (radar == 0 && b->human_plus) radar = HUMAN_PLUS_RADAR;
    o->radar_range = radar / 10;

    /* FCS PERFORMANCE */
    const acr_gen1_perf_part *fcs = p[ACR_GEN1_SLOT_FCS];
    if (fcs) {
        int32_t w = 0, h = 0;
        for (int k = 0; k < 4; ++k) { w += fcs->lock_w[k]; h += fcs->lock_h[k]; }
        o->fcs_performance = fcs->fcs_range / 10 + w / 10 + h / 10 + fcs->max_locks * 100
                           - fcs->lock_time * 10;
    }
    o->support = o->radar_range + o->fcs_performance;

    o->overall = o->offensive + o->defensive + o->stability + o->mobility + o->support;
    o->grade = acr_gen1_grade_of(o->overall);
}
