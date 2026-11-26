/* Gen 1 garage PERFORMANCE page and GRADE, with Master of Arena's own
 * maths (menu module 0x80092BB4). Source: AC1Recomp
 * docs/mapping/menus-screens.md §2k and stat-block.md (+0x1E radar range);
 * the launcher's moa_parts.c moa_performance copies the same sums.
 *
 *   AP           head + core + arms + legs AP
 *   WEIGHT       every fitted part
 *   PRICE        every fitted part plus every fitted option
 *   OFFENSIVE    weapon arms' attack, or hand weapon + blade; plus both back units
 *   DEFENSIVE    shell + energy defence of head, core, arms, legs
 *                (SP-S/SCR: shell part x1.15; SP-E/SCR: energy part x1.15)
 *   STABILITY    legs +0x40, +800 SP-SAP, +1600 SP-ABS_Re or else +800 SP-ABS
 *   MOBILITY     MOVING SPEED (legs +0x36 x10) + TURNING SPEED (legs +0x38 x10)
 *                + BOOST ABILITY ((booster power / weight) x1000, whole numbers,
 *                9800 in place of the booster on caterpillars)
 *   SUPPORT      RADAR RANGE (compiled radar range / 10) + FCS PERFORMANCE
 *   OVERALL      OFFENSIVE + DEFENSIVE + STABILITY + MOBILITY + SUPPORT
 *   GRADE        25,000+ SUPERFINE, 20,000+ GREAT, 15,000+ FINE, 10,000+ OK, else POOR
 *
 * No operating-system calls; the platform fills acr_gen1_perf_part from
 * acr-import's part files. */
#ifndef ACR_GEN1_PERF_H
#define ACR_GEN1_PERF_H

#include <stdint.h>

#include "acr_gen1_fit.h"

/* What the PERFORMANCE page reads from one part (offsets in the game's
 * part record). Unused fields stay 0. */
typedef struct acr_gen1_perf_part {
    uint16_t ap;               /* head, core, arms, legs +0x2C */
    uint16_t defense_shell;    /* +0x2E */
    uint16_t defense_energy;   /* +0x30 */
    uint16_t attack;           /* hand / back / weapon arms: weapon detail +0x14; blade +0x30 */
    uint16_t moving_speed;     /* legs +0x36 */
    uint16_t turn_accel;       /* legs +0x38 */
    uint16_t stability;        /* legs +0x40 */
    uint16_t power;            /* booster +0x2C */
    uint16_t radar_range;      /* head +0x38; back radar +0x2E */
    uint8_t  radar_unit;       /* back: a radar unit (back parts 40-51 in Master of Arena) */
    /* FCS */
    uint8_t  max_locks;        /* +0x2C */
    uint16_t fcs_range;        /* +0x2E: the first lock class's range */
    int16_t  lock_w[4];        /* +0x30 + 12k: each lock class's box width */
    int16_t  lock_h[4];        /* +0x32 + 12k: box height */
    int16_t  lock_time;        /* +0x38: the first lock class's lock time */
} acr_gen1_perf_part;

/* A build as the page sees it. */
typedef struct acr_gen1_perf_build {
    const acr_gen1_perf_part *part[ACR_GEN1_SLOT_COUNT];   /* NULL = slot empty */
    uint32_t weight[ACR_GEN1_SLOT_COUNT];
    uint32_t price[ACR_GEN1_SLOT_COUNT];
    uint32_t option_price;     /* all fitted options together */
    uint16_t options;          /* option mask (ACR_GEN1_OPTION_*) */
    uint8_t  weapon_arms;      /* the arms are weapon arms */
    uint8_t  caterpillar;      /* the legs are caterpillars */
    uint8_t  human_plus;       /* Human PLUS: 5,120 radar range with no radar fitted */
} acr_gen1_perf_build;

typedef enum acr_gen1_grade {
    ACR_GEN1_GRADE_POOR, ACR_GEN1_GRADE_OK, ACR_GEN1_GRADE_FINE,
    ACR_GEN1_GRADE_GREAT, ACR_GEN1_GRADE_SUPERFINE
} acr_gen1_grade;

typedef struct acr_gen1_perf {
    int32_t ap, weight, price;
    int32_t offensive, defensive, stability;
    int32_t mobility, moving_speed, turning_speed, boost_ability;
    int32_t support, radar_range, fcs_performance;
    int32_t overall;
    acr_gen1_grade grade;
} acr_gen1_perf;

/* The page opens only with a head, core, arms, legs, generator, FCS and,
 * unless the legs are caterpillars, a booster ("Your equipment is
 * incomplete." otherwise). 1 = it opens. */
int acr_gen1_perf_complete(const acr_gen1_perf_build *b);

void acr_gen1_performance(const acr_gen1_perf_build *b, acr_gen1_perf *out);

acr_gen1_grade acr_gen1_grade_of(int32_t overall);
const char *acr_gen1_grade_name(acr_gen1_grade g);   /* "SUPERFINE", "GREAT", ... */

#endif
