/* Fixed-step game clock. Logic advances in whole ticks; rendering reads
 * `alpha` (0..ACR_FX_ONE) to interpolate between the last two ticks. */
#ifndef ACR_CLOCK_H
#define ACR_CLOCK_H

#include <stdint.h>
#include "acr_fixed.h"

typedef struct acr_clock {
    uint64_t tick_ns;        /* length of one logic tick */
    uint64_t accumulated_ns; /* time not yet turned into ticks */
    uint64_t tick;           /* ticks run since start */
    uint32_t max_catch_up;   /* most ticks run in one frame after a stall */
} acr_clock;

void acr_clock_init(acr_clock *c, uint32_t ticks_per_second, uint32_t max_catch_up);

/* Feed the real time since the last frame. Returns how many ticks to run now.
 * Time past max_catch_up ticks is dropped, so a long stall never makes the
 * game run in fast motion afterwards. */
uint32_t acr_clock_advance(acr_clock *c, uint64_t elapsed_ns);

/* Fraction of the next tick already elapsed, for render interpolation. */
acr_fx acr_clock_alpha(const acr_clock *c);

#endif
