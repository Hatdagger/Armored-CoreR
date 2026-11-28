/* Gen 2 AC compile: parts -> the movement and energy numbers the battle code
 * uses, as Armored Core 2 computes them when an AC is set up (SLUS_200.14
 * 0x145180, helpers 0x146260-0x146b68; formulas and evidence in
 * docs/research/gen2-movement.md).
 *
 * The game does this in single-precision floats, and so does this: every
 * step is the game's own operation in the game's order, with each result
 * cut off towards zero as the PS2's FPU does (acr_gen2_fp.h). The outputs
 * match the game's own code, run in an emulator, bit for bit, and are the
 * same on every machine.
 *
 * Units: distance in AC2 units (1 unit = 1 metre: the game's own HUD shows
 * speed x 216 as km/h), speeds per frame (60 frames a second), turning in
 * radians per frame, energy in the generator's own units. */
#ifndef ACR_GEN2_COMPILE_H
#define ACR_GEN2_COMPILE_H

#include <stdint.h>

/* Movement kind (AC +0x70): which state table and which formulas. */
enum {
    ACR_GEN2_LEGS_TWO = 0,          /* humanoid and reverse joint: walk by animation */
    ACR_GEN2_LEGS_CATERPILLAR = 1,  /* glide at the legs' moving speed, own booster */
    ACR_GEN2_LEGS_FOUR = 2,         /* glide at the legs' moving speed */
    ACR_GEN2_LEGS_HOVER = 3         /* float at the legs' moving speed, own booster */
};

/* Optional parts, bit n = the n-th part in the shop (F 3750 + n). Only the
 * ones that change movement or energy are named here. */
#define ACR_GEN2_OPT_CHARGE (1u << 2)   /* SP-BCNDR: +12000 energy */
#define ACR_GEN2_OPT_TURN   (1u << 5)   /* SP-CIR-K: turning stat x 1.2 */
#define ACR_GEN2_OPT_BRAKE  (1u << 14)  /* SP-CBRK: braking stat x 2 */

/* A fitted part's weight and energy drain (record words 0 and 1). */
typedef struct acr_gen2_part {
    uint8_t  present;
    uint16_t weight;
    uint16_t energy_drain;
} acr_gen2_part;

/* A build, by meaning. wN = the part record's u16 word N (gen2-archive.md,
 * "Parts record stat fields"). A weapon that takes both back slots counts
 * once. */
typedef struct acr_gen2_build {
    acr_gen2_part head, core, arms, legs, booster, fcs, generator, radiator,
                  inside, extension, back_l, back_r, arm_r, arm_l;
    /* legs */
    uint8_t  leg_type;          /* w11 low byte: 0 humanoid, 1 reverse joint, 2 four legs,
                                   3 caterpillar, 4 hover */
    uint8_t  jump;              /* w11 high byte (0 = cannot jump) */
    uint16_t max_weight;        /* w12 */
    uint16_t moving_speed;      /* w13 */
    uint16_t braking;           /* w14 */
    uint16_t stationary_drain;  /* w18: the legs' drain while standing */
    uint16_t leg_boost_power;   /* w19: caterpillar and hover legs' own booster */
    uint16_t leg_boost_drain;   /* w20 */
    uint16_t turn_accel;        /* w21: percent of the top turn speed gained a frame (10 on all) */
    uint16_t turning;           /* w22 (the shop shows w22 x 4) */
    uint16_t cano_turn_accel;   /* w23: as w21, while kneeling to fire */
    /* core */
    uint16_t ob_power;          /* w14 */
    uint16_t ob_start_drain;    /* w15 */
    uint16_t ob_drain;          /* w16 */
    /* booster */
    uint16_t boost_power;       /* w7 */
    uint16_t boost_drain;       /* w8 (the shop's charge drain) */
    /* generator */
    uint16_t energy_output;     /* w7 */
    uint16_t max_charge;        /* w8 */
    uint16_t red_zone;          /* w9 */
    uint16_t option_mask;
    uint8_t  pilot_kind;        /* AC +0x71: 0 for a player; 5 and up (some enemy pilots)
                                   boost at half cost and refill twice as fast */
} acr_gen2_build;

/* The compiled block (AC +0x410..+0x490; the names are the game's own
 * debug-menu labels). Ints first, then floats. */
typedef struct acr_gen2_stats {
    int32_t leg_kind;           /* ACR_GEN2_LEGS_* */
    int32_t weight;             /* all parts, capped at legs weight + max weight */
    int32_t upper_weight;       /* weight without the legs */
    int32_t boost_sub;          /* energy a frame boosting in the air (half on the ground) */
    int32_t boost_add;          /* refill a frame standing */
    int32_t move_boost_add;     /* refill a frame moving */
    int32_t oboost_sub;         /* overboost: energy a frame */
    int32_t oboost_1stsub;      /* overboost: energy at ignition */
    int32_t boost_max;          /* energy capacity */
    int32_t boost_redarea;      /* red zone */
    float boost_factor;         /* +0x424: the booster's share of the speeds below */
    float flymv_accel, flymv_maxspd;    /* air (hover: all) horizontal movement */
    float flyup_accel, flyup_maxspd;    /* rising */
    float dash_accel, dash_maxspd;      /* ground boost (caterpillar, four legs: ground movement) */
    float dash_brake;                   /* ground braking */
    float oboost_spd0, oboost_acc;      /* overboost: first-frame kick, then a frame */
    float turn_maxspd, turn_accel;
    float canoturn_maxspd, canoturn_accel;
    float jumpspd;                      /* take-off speed */
} acr_gen2_stats;

/* The tuning values the compile and the movement read. AC2's and Another
 * Age's differ in eight of them (energy rates, the booster and weight
 * factors, air speeds). */
typedef struct acr_gen2_tuning {
    float std_total_weight, std_upper_weight, upper_k;
    float turn_std, turn_k, turn_lo, turn_hi, turn_stdspd, canoturn_stdspd;
    float avg_move_speed, move_k, move_lo, move_hi, move_stdacc, move_stdmaxspd;
    float boostadd_rate, boostsub_rate;
    float std_boost_power, weight_k, weight_lo, weight_hi, boost_k, boost_lo, boost_hi;
    float std_flymv_acc, std_flymv_maxspd, std_flyup_acc, std_flyup_maxspd;
    float std_dash_acc, std_dash_maxspd;
    float brake_std, std_brake, brake_k, brake_lo, brake_hi;
    float opt_charge_add, opt_turn_rate, opt_brake_rate;
    float ob_std_power, ob_k, ob_speed, ob_acc_rate;
    float jump_rate;
    /* Read by the movement step, the same for every AC: */
    float dashjumpspd, land_dmgspd, gravity, fall_maxspd, dash_mintime;
} acr_gen2_tuning;

extern const acr_gen2_tuning acr_gen2_tuning_ac2;   /* SLUS_200.14: the base */
extern const acr_gen2_tuning acr_gen2_tuning_aa;    /* SLUS_202.49: Another Age */

/* 1 + (x - 1) * k, limited to lo..hi: how the game turns "this part against
 * the standard part" into a factor (0x146220). */
float acr_gen2_clampf(float x, float k, float lo, float hi);

void acr_gen2_compile(const acr_gen2_tuning *t, const acr_gen2_build *b, acr_gen2_stats *out);

#endif
