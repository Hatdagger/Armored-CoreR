/* Gen 1 AC compile: parts -> the stats the battle code uses, as Master of
 * Arena computes them (menu module routine 0x800A7BB4; formulas and
 * evidence in docs/research/gen1-movement.md). Integer maths copied
 * instruction for instruction, including the game's rounding and limits. */
#ifndef ACR_GEN1_COMPILE_H
#define ACR_GEN1_COMPILE_H

#include <stdint.h>

/* One part's stats, by meaning. Unused fields stay 0. */
typedef struct acr_gen1_part {
    uint8_t  present;          /* 0 = slot empty */
    uint16_t weight;
    uint16_t energy_drain;     /* legs: while moving; generator: output */
    uint16_t stat_2c;          /* generator capacity, blade attack, booster power, AP */
    uint16_t stat_2e;          /* booster boost drain, generator red zone */
    uint16_t carry_limit;      /* legs */
    uint16_t drain_standing;   /* legs */
    uint16_t stability;        /* legs (+0x40) */
    uint16_t boost_drain_legs; /* legs: used when no booster is fitted */
    uint16_t turn_accel;       /* legs +0x38 */
    uint8_t  turn_speed;       /* legs +0x3A */
    uint8_t  fixed_boost;      /* legs: ground boost byte set by the game for some
                                  four-legged and caterpillar legs (0 = computed) */
    uint16_t back_turn;        /* legs +0x3C: turning while crouched with a back weapon */
    uint8_t  intercept_chance; /* core +0x34: missile interceptor, chance per step / 128 */
    uint8_t  intercept_angle;  /* core +0x35: interceptor cone, +- this x 4 (4096 a turn) */
} acr_gen1_part;

typedef struct acr_gen1_build {
    acr_gen1_part core, booster, head, arms, blade, hand, back_l, back_r, generator, fcs, legs;
    uint16_t option_mask;      /* optional parts fitted, bit n = option n */
} acr_gen1_build;

typedef struct acr_gen1_stats {
    uint16_t energy_capacity;  /* compiled +18 */
    uint16_t rise_drain;       /* +20: energy per step while rising */
    uint16_t refill_standing;  /* +22 */
    uint16_t red_zone;         /* +24 */
    uint16_t refill_moving;    /* +26 */
    uint16_t blade_power;      /* +28 */
    uint8_t  stability;        /* +36: resistance to impact shock */
    uint32_t carried_weight;   /* all parts but the generator, capped by the legs */
    /* Movement bytes +6..+14 (1..64 unless noted). Booster power (9800 without
     * a booster) and the legs' turn stats, each divided by carried weight. */
    uint8_t  thrust;           /* +6 = +8: power * 4096 / weight / 224 */
    uint8_t  thrust_80;        /* +7 = +9: thrust * 8 / 10, 1..50 */
    uint8_t  thrust_120;       /* +10 = +11: thrust * 12 / 10, or the legs' fixed_boost */
    uint8_t  turn_accel;       /* +12 */
    uint8_t  turn_speed;       /* +13, 0..64 */
    uint8_t  friction;         /* +14: weight * 8 / 1000, 32..96 */
    /* Not in the compile test vectors (AC1Recomp docs/mapping/stat-block.md): */
    uint8_t  intercept_chance; /* +0x0F, core +0x34 */
    uint8_t  intercept_angle;  /* +0x10, core +0x35 */
    uint8_t  back_turn_accel;  /* back weapon slots +7: the +12 formula with legs +0x3C */
    uint8_t  back_turn_speed;  /* back weapon slots +8: the +13 formula with legs +0x3C, 1..64 */
} acr_gen1_stats;

/* What the battle engine derives at match start (VS engine 0x8008342C).
 * Velocities are in 1/64 world units per step; turning in 1/4096 turns. */
typedef struct acr_gen1_motion {
    uint16_t rise_accel;       /* +6 x 2, per tick while rising */
    uint16_t rise_max;         /* +7 x 200 */
    uint16_t air_accel;        /* +8 x 14, boosting in the air; / 4 slowing down after */
    uint16_t air_max;          /* +9 x 140 */
    uint16_t boost_accel;      /* +10 x 14, boosting on the ground; also the slowdown after */
    uint16_t boost_max;        /* +11 x 140 */
    uint16_t turn_accel;       /* +12 x 5 / 16 */
    uint16_t turn_max;         /* +13 x 15 / 16 */
    uint16_t friction;         /* +14, taken off each ground axis per tick (not used in VS) */
    uint16_t axis_step_max;    /* world units per tick on each axis; 0 = no limit */
    /* Turning while crouched in the cannon stance (VS 0x8007E3E0 with player
     * block +0x210 / +0x212): slot +7 x 5 / 32 + 1 and +8 x 15 / 32 + 1 for a
     * back weapon (hand weapons give 64 -> 11 / 31). */
    uint16_t crouch_turn_accel;
    uint16_t crouch_turn_max;
} acr_gen1_motion;

/* Fixed by the engine, not by parts. */
#define ACR_GEN1_GRAVITY        256   /* added to vertical speed per tick */
#define ACR_GEN1_FALL_MAX       9600
#define ACR_GEN1_AXIS_STEP_MAX  75    /* world units per tick on each axis (measured);
                                         acr_gen1_motion.axis_step_max starts here and a ruleset may raise it */

/* Optional parts (bit n = option n, Master of Arena's option table). */
#define ACR_GEN1_OPTION_RADAR          0x0001u /* SP-MAW */
#define ACR_GEN1_OPTION_JAMMER         0x0002u /* SP-JAM */
#define ACR_GEN1_OPTION_AUTO_LAUNCHER  0x0004u /* SP-M/AUTO */
#define ACR_GEN1_OPTION_BALANCER       0x0008u /* SP-ABS: legs stability +800 */
#define ACR_GEN1_OPTION_ABSORBER       0x0010u /* SP-SAP */
#define ACR_GEN1_OPTION_CHARGE_EXPANDER 0x0020u /* SP-CND-K */
#define ACR_GEN1_OPTION_FCS_ACCEL      0x0040u /* SP-AXL */
#define ACR_GEN1_OPTION_SHELL_SCREEN   0x0080u /* SP-S/SCR */
#define ACR_GEN1_OPTION_ENERGY_SCREEN  0x0100u /* SP-E/SCR */
#define ACR_GEN1_OPTION_RAPID_CHARGE   0x0200u /* SP-EH */
#define ACR_GEN1_OPTION_ENERGY_AMP     0x0400u /* SP-E+ */
#define ACR_GEN1_OPTION_ENERGY_REDUCE  0x0800u /* SP-DEtq */
#define ACR_GEN1_OPTION_BALANCER_RE    0x1000u /* SP-ABS_Re: legs stability +1600 */
/* Not on the disc: Armored-CoreR's own option. Modern controls (twin
 * sticks, hard lock, boost toggle, quick boost) for a weaker FCS and blade
 * rules (acr_gen1_battle.h). It takes every option slot the core has, so
 * nothing else can be fitted with it, and it fits any core. */
#define ACR_GEN1_OPTION_MODERN_BIT     13u
#define ACR_GEN1_OPTION_MODERN         0x2000u /* SP-MODERN */

void acr_gen1_compile(const acr_gen1_build *b, acr_gen1_stats *out);

/* Match start with Human PLUS level 6 and up (VS 0x80083560): rise drain
 * halved, both refills doubled, each refill then capped at the new rise
 * drain. Lower levels change nothing here. */
void acr_gen1_human_plus_energy(acr_gen1_stats *s, unsigned level);

/* Human PLUS with SP-MODERN fitted: only its no-kneel benefit (cannons fire
 * without the stance, acr_gen1_fighter.human_plus). Its energy rules, and
 * any Human PLUS benefit added later, don't apply: this gives the level to
 * use for them (0 = none). */
static inline unsigned acr_gen1_human_plus_level(unsigned level, uint16_t option_mask) {
    return (option_mask & ACR_GEN1_OPTION_MODERN) ? 0u : level;
}
void acr_gen1_motion_from_stats(const acr_gen1_stats *s, acr_gen1_motion *out);

#endif
