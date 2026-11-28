#include "acr_gen2_compile.h"

#include "acr_gen2_fp.h"

/* Armored Core 2 (SLUS_200.14): the block at 0x429190 and the values the
 * compile reads through gp at 0x43dba4. tools/gen2/movement.py prints them
 * from the disc's executable. */
const acr_gen2_tuning acr_gen2_tuning_ac2 = {
    7357.0f, 5602.0f, 1.6f,
    100.0f, 1.0f, 0.8f, 2.0f, 0.0155f, 0.011f,
    350.0f, 0.7f, 0.01f, 2.0f, 0.033f, 0.8f,
    0.027f, 0.033f,
    14800.0f, 0.65f, 0.2f, 1.18f, 1.0f, 0.9f, 1.15f,
    0.045f, 1.2f, 0.015f, 0.8f,
    0.047f, 1.3f,
    100.0f, 0.06f, 1.0f, 0.3f, 2.0f,
    12000.0f, 1.2f, 2.0f,
    3000.0f, 0.8f, 4.0f, 0.05f,
    1.15f,
    0.55f, 1.48f, 0.042f, 1.74f, 13.0f
};

/* Another Age (SLUS_202.49): the same code, eight values changed. */
const acr_gen2_tuning acr_gen2_tuning_aa = {
    7357.0f, 5602.0f, 1.6f,
    100.0f, 1.0f, 0.8f, 2.0f, 0.0155f, 0.011f,
    350.0f, 0.7f, 0.01f, 2.0f, 0.033f, 0.8f,
    0.02f, 0.038f,
    15000.0f, 0.75f, 0.2f, 1.4f, 1.0f, 0.8f, 1.47f,
    0.042f, 1.04f, 0.015f, 0.75f,
    0.047f, 1.3f,
    100.0f, 0.06f, 1.0f, 0.3f, 2.0f,
    12000.0f, 1.2f, 2.0f,
    3000.0f, 0.8f, 4.0f, 0.05f,
    1.15f,
    0.55f, 1.48f, 0.042f, 1.74f, 13.0f
};

float acr_gen2_clampf(float x, float k, float lo, float hi)
{
    float v = g2_add(g2_mul(g2_sub(x, 1.0f), k), 1.0f);
    if (v < lo) v = lo;
    if (hi < v) v = hi;
    return v;
}

static int leg_kind(uint8_t leg_type)
{
    switch (leg_type) {
    case 2:  return ACR_GEN2_LEGS_FOUR;
    case 3:  return ACR_GEN2_LEGS_CATERPILLAR;
    case 4:  return ACR_GEN2_LEGS_HOVER;
    default: return ACR_GEN2_LEGS_TWO;
    }
}

void acr_gen2_compile(const acr_gen2_tuning *t, const acr_gen2_build *b, acr_gen2_stats *out)
{
    const acr_gen2_part *slot[14];
    int32_t total = 0, drain_moving = 0, drain_standing, cap, power, charge_drain, add, madd, sub;
    float wf, bf, f, m, r, a, turn_rate, brake_rate, ob;
    int i, kind = leg_kind(b->leg_type);

    slot[0] = &b->head;      slot[1] = &b->core;      slot[2] = &b->arms;   slot[3] = &b->legs;
    slot[4] = &b->booster;   slot[5] = &b->fcs;       slot[6] = &b->generator;
    slot[7] = &b->radiator;  slot[8] = &b->inside;    slot[9] = &b->extension;
    slot[10] = &b->back_l;   slot[11] = &b->back_r;   slot[12] = &b->arm_r; slot[13] = &b->arm_l;
    for (i = 0; i < 14; i++) {
        if (!slot[i]->present) continue;
        total += slot[i]->weight;
        drain_moving += slot[i]->energy_drain;
    }
    /* 0x146120: an AC over its legs' limit weighs exactly the limit. */
    cap = (int32_t)b->legs.weight + b->max_weight;
    out->leg_kind = kind;
    out->weight = total <= cap ? total : cap;
    out->upper_weight = out->weight - b->legs.weight;
    drain_standing = drain_moving - b->legs.energy_drain + b->stationary_drain;

    /* 0x145710: caterpillars, hovers and ACs without a booster fly on the legs' own. */
    if (!b->booster.present || kind == ACR_GEN2_LEGS_CATERPILLAR || kind == ACR_GEN2_LEGS_HOVER) {
        power = b->leg_boost_power;
        charge_drain = b->leg_boost_drain;
    } else {
        power = b->boost_power;
        charge_drain = b->boost_drain;
    }

    /* 0x146490: one factor from weight and booster power scales six standard values. */
    wf = acr_gen2_clampf(g2_div(t->std_total_weight, (float)out->weight), t->weight_k, t->weight_lo, t->weight_hi);
    bf = acr_gen2_clampf(g2_div((float)power, t->std_boost_power), t->boost_k, t->boost_lo, t->boost_hi);
    f = g2_mul(wf, bf);
    out->boost_factor = bf;
    out->flymv_accel = g2_mul(f, t->std_flymv_acc);
    out->flymv_maxspd = g2_mul(f, t->std_flymv_maxspd);
    out->flyup_accel = g2_mul(f, t->std_flyup_acc);
    out->flyup_maxspd = g2_mul(f, t->std_flyup_maxspd);
    out->dash_accel = g2_mul(f, t->std_dash_acc);
    out->dash_maxspd = g2_mul(f, t->std_dash_maxspd);
    brake_rate = (b->option_mask & ACR_GEN2_OPT_BRAKE) ? t->opt_brake_rate : 1.0f;
    out->dash_brake = g2_mul(acr_gen2_clampf(g2_mul(g2_div((float)b->braking, t->brake_std), brake_rate),
                                             t->brake_k, t->brake_lo, t->brake_hi), t->std_brake);
    /* Legs that glide move at their moving-speed stat; two legs walk by animation. */
    if (kind != ACR_GEN2_LEGS_TWO) {
        m = acr_gen2_clampf(g2_div((float)b->moving_speed, t->avg_move_speed), t->move_k, t->move_lo, t->move_hi);
        if (kind == ACR_GEN2_LEGS_HOVER) {
            out->flymv_accel = g2_mul(t->move_stdacc, m);
            out->flymv_maxspd = g2_mul(t->move_stdmaxspd, m);
        } else {
            out->dash_accel = g2_mul(t->move_stdacc, m);
            out->dash_maxspd = g2_mul(t->move_stdmaxspd, m);
        }
    }

    /* 0x146408: overboost. Top speed and the first frame's kick are the same value. */
    ob = g2_mul(t->ob_speed, acr_gen2_clampf(g2_div((float)b->ob_power, t->ob_std_power), t->ob_k, 0.5f, 1.0f));
    out->oboost_spd0 = ob;
    out->oboost_acc = g2_mul(ob, t->ob_acc_rate);

    /* 0x146798: turning, slowed by a heavy upper body. */
    r = 1.0f;
    if (!((float)out->upper_weight <= t->std_upper_weight))
        r = g2_div(t->std_upper_weight, (float)out->upper_weight);
    a = acr_gen2_clampf(r, t->upper_k, 0.01f, 1.0f);
    turn_rate = (b->option_mask & ACR_GEN2_OPT_TURN) ? t->opt_turn_rate : 1.0f;
    f = g2_mul(a, acr_gen2_clampf(g2_div((float)(int32_t)g2_mul((float)b->turning, turn_rate), t->turn_std),
                                  t->turn_k, t->turn_lo, t->turn_hi));
    out->turn_maxspd = g2_mul(f, t->turn_stdspd);
    out->turn_accel = g2_div(g2_mul(out->turn_maxspd, (float)b->turn_accel), 100.0f);
    out->canoturn_maxspd = g2_mul(f, t->canoturn_stdspd);
    out->canoturn_accel = g2_div(g2_mul(out->canoturn_maxspd, (float)b->cano_turn_accel), 100.0f);

    out->jumpspd = g2_div(g2_mul((float)b->jump, t->jump_rate), 100.0f);    /* 0x145a74 */

    /* 0x1468c8: energy. Refill is what the generator has left over. */
    sub = (int32_t)g2_mul(t->boostsub_rate, (float)charge_drain);
    if (sub <= 0) sub = 1;
    add = (int32_t)g2_mul((float)((int32_t)b->energy_output - drain_standing), t->boostadd_rate);
    if (add < 50) add = 50;
    if (add > 0x180) add = 0x180;
    madd = (int32_t)g2_mul((float)((int32_t)b->energy_output - drain_moving), t->boostadd_rate);
    if (madd <= 0) madd = 1;
    if (madd > 0xc0) madd = 0xc0;
    if (b->pilot_kind >= 5) {
        madd <<= 1;
        sub >>= 1;
        add <<= 1;
    }
    /* A refill faster than the booster drains is pulled halfway down to it. */
    if (sub < add) add = (add + sub) / 2;
    if (sub < madd) madd = (madd + sub) / 2;
    out->boost_sub = sub;
    out->boost_add = add;
    out->move_boost_add = madd;
    out->oboost_sub = (int32_t)g2_mul((float)b->ob_drain, t->boostsub_rate);
    out->oboost_1stsub = (int32_t)g2_mul((float)b->ob_start_drain, t->boostsub_rate);
    out->boost_max = (int32_t)g2_add((float)b->max_charge,
                                     (b->option_mask & ACR_GEN2_OPT_CHARGE) ? t->opt_charge_add : 0.0f);
    out->boost_redarea = b->red_zone;
}
