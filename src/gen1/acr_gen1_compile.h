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
} acr_gen1_stats;

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

void acr_gen1_compile(const acr_gen1_build *b, acr_gen1_stats *out);

#endif
