/* Warning timing (battle_warn.h). Every rule here is the VS engine's HUD
 * routine 0x8005675C (FDAT 209), with the arena engine's bar height (FDAT
 * 210, 0x80057020). Addresses are VS unless marked. */
#include "battle_warn.h"

#include <string.h>

void battle_warn_reset(battle_warn *w) { memset(w, 0, sizeof *w); }

int battle_warn_ap_low(int ap, int ap_max) {
    /* 0x80056BF0: lit unless AC +0x16A (full AP x 4) < 10 x AC +0x168 (AP x 4). */
    return ap > 0 && ap_max > 0 && (long long)ap * 10 <= (long long)ap_max;
}

int battle_warn_blink(int ticks_left) { return ticks_left > 0 && ((ticks_left >> 2) & 3) != 0; }

unsigned battle_warn_tick(battle_warn *w, const battle_warn_in *in) {
    unsigned beeps = 0, boxes = 0;
    const uint32_t f = w->frame++;

    if (!in->live) {
        w->area_count = w->area_on = 0;
        w->red_count = 0;
        w->boxes = 0;
        return 0;
    }

    /* Energy red zone (arena 0x80057020-0x800571F0, VS 0x800567D4): the bar
     * in whole pixels, empty part s2 and the empty part at the red line s0.
     * While the bar falls inside the red zone a countdown loses how deep
     * into it the bar is, each tick; at 0 or below it beeps and restarts
     * at twice the red zone's height. So it beeps at once on entering,
     * faster the lower the bar, and only while energy is going down. */
    if (in->energy_max > 0) {
        const int level = (int)((long long)(in->energy < 0 ? 0 : in->energy) * BATTLE_WARN_BAR / in->energy_max);
        const int red = (int)((long long)in->red_zone * BATTLE_WARN_BAR / in->energy_max);
        const int s2 = BATTLE_WARN_BAR - (level > BATTLE_WARN_BAR ? BATTLE_WARN_BAR : level);
        const int s0 = BATTLE_WARN_BAR - red;
        if (s2 != BATTLE_WARN_BAR && !in->drained) {   /* empty, or the forced refill: no red-zone beep */
            if (s2 < s0) w->red_count = 0;
            else {
                if (w->red_prev < s2) {
                    w->red_count = (int16_t)(w->red_count - (s2 - s0));
                    if (w->red_count <= 0) {
                        beeps |= BATTLE_WARN_BEEP_RED_ZONE;
                        w->red_count = (int16_t)(2 * (BATTLE_WARN_BAR - s0));
                    }
                }
            }
            w->red_prev = (int16_t)s2;
        }
    }

    /* Out of area (0x80056A6C): a countdown; the box shows while it is at
     * or above its "on" mark, the beep plays when it reaches -1, and then
     * it restarts at the distance to the edge / 256 ticks (at least 3), on
     * for the first 5/8 of that. Closer to the edge = faster. */
    if (in->out_of_area) {
        if (w->area_count >= w->area_on) boxes |= BATTLE_WARN_BOX_LEAVING_AREA;
        if (--w->area_count == -1) {
            beeps |= BATTLE_WARN_BEEP_PERIMETER;
            int period = in->edge >= 0 ? in->edge >> 8 : 20;   /* unknown edge: about 0.9 s */
            if (period < 3) period = 3;
            if (period > 32000) period = 32000;
            w->area_count = (int16_t)period;
            w->area_on = (int16_t)((period >> 1) - (period >> 3));
        }
    } else w->area_count = w->area_on = 0;

    /* Low AP (0x80056BE0): beep on frame % 8 == 0, box on 5 of 8 frames. */
    if (battle_warn_ap_low(in->ap, in->ap_max) || in->burning) {
        if ((f & 7) == 0) beeps |= BATTLE_WARN_BEEP_AP_LOW;
        if ((f & 7) < 5) boxes |= BATTLE_WARN_BOX_ARMOR_LOW;
    }

    /* Over-drained (0x80056C70, player block +0x18 bit 0): the same
     * rhythm three frames later, so it never lands on the low-AP beep. */
    if (in->drained) {
        const uint32_t g = (f + 3) & 7;
        if (g == 0) beeps |= BATTLE_WARN_BEEP_DRAINED;
        if (g < 5) boxes |= BATTLE_WARN_BOX_CHARGING;
    }

    w->boxes = boxes;
    return beeps;
}
