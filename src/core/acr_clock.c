#include "acr_clock.h"

void acr_clock_init(acr_clock *c, uint32_t ticks_per_second, uint32_t max_catch_up) {
    c->tick_ns = ticks_per_second ? 1000000000ull / ticks_per_second : 1000000000ull;
    c->accumulated_ns = 0;
    c->tick = 0;
    c->max_catch_up = max_catch_up ? max_catch_up : 1;
}

uint32_t acr_clock_advance(acr_clock *c, uint64_t elapsed_ns) {
    const uint64_t cap = (uint64_t)c->max_catch_up * c->tick_ns;
    uint64_t total = c->accumulated_ns + elapsed_ns;
    if (total > cap + c->tick_ns - 1) total = cap + (total % c->tick_ns);
    const uint32_t ticks = (uint32_t)(total / c->tick_ns);
    c->accumulated_ns = total - (uint64_t)ticks * c->tick_ns;
    c->tick += ticks;
    return ticks;
}

acr_fx acr_clock_alpha(const acr_clock *c) {
    return (acr_fx)((c->accumulated_ns << ACR_FX_SHIFT) / c->tick_ns);
}
