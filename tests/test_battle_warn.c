/* The warning timing (src/platform/sdl3/battle_warn.c), against the rules
 * read from the VS / arena HUD routine (docs/feel/sound-hud.md). */
#include <stdio.h>
#include <string.h>

#include "../src/platform/sdl3/battle_warn.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static battle_warn_in base(void) {
    battle_warn_in in;
    memset(&in, 0, sizeof in);
    in.live = 1; in.ap = 8000; in.ap_max = 8000;
    in.energy = 28000; in.energy_max = 28000; in.red_zone = 7800;
    in.edge = -1;
    return in;
}

int main(void) {
    battle_warn w;
    /* Low AP: a tenth of full AP; beep every 8 ticks, box 5 of 8. */
    CHECK(!battle_warn_ap_low(801, 8000));
    CHECK(battle_warn_ap_low(800, 8000));
    {
        battle_warn_reset(&w);
        battle_warn_in in = base();
        in.ap = 700;
        int beeps = 0, lit = 0;
        for (int t = 0; t < 64; ++t) {
            if (battle_warn_tick(&w, &in) & BATTLE_WARN_BEEP_AP_LOW) ++beeps;
            if (w.boxes & BATTLE_WARN_BOX_ARMOR_LOW) ++lit;
        }
        CHECK(beeps == 8);
        CHECK(lit == 40);
    }
    /* Over-drained: same rhythm, three ticks apart from the AP beep. */
    {
        battle_warn_reset(&w);
        battle_warn_in in = base();
        in.ap = 700; in.drained = 1; in.energy = 0;
        for (int t = 0; t < 16; ++t) {
            const unsigned b = battle_warn_tick(&w, &in);
            CHECK(!((b & BATTLE_WARN_BEEP_AP_LOW) && (b & BATTLE_WARN_BEEP_DRAINED)));
            if (b & BATTLE_WARN_BEEP_DRAINED) CHECK(t % 8 == 5);
        }
    }
    /* Red zone: beeps at once on entering while the bar falls, then faster
     * the lower it is; never while it isn't falling. */
    {
        battle_warn_reset(&w);
        battle_warn_in in = base();
        int first = -1, beeps = 0;
        for (int t = 0; t < 400 && in.energy > 0; ++t, in.energy -= 100) {
            const unsigned b = battle_warn_tick(&w, &in);
            if (b & BATTLE_WARN_BEEP_RED_ZONE) { if (first < 0) first = in.energy; ++beeps; }
        }
        CHECK(first <= 7800 && first > 7600);
        CHECK(beeps > 3);
        const int before = beeps;
        in.energy = 3000;
        for (int t = 0; t < 50; ++t) if (battle_warn_tick(&w, &in) & BATTLE_WARN_BEEP_RED_ZONE) ++beeps;
        CHECK(beeps <= before + 1);   /* a steady bar: at most the one from the jump down */
    }
    /* Out of area: first beep at once; period = edge / 256 ticks, at least 3. */
    {
        battle_warn_reset(&w);
        battle_warn_in in = base();
        in.out_of_area = 1; in.edge = 2560;
        int beeps[40], n = 0;
        for (int t = 0; t < 40; ++t) if (battle_warn_tick(&w, &in) & BATTLE_WARN_BEEP_PERIMETER) beeps[n++] = t;
        CHECK(n >= 3 && beeps[0] == 0);
        CHECK(beeps[1] - beeps[0] == 11);   /* counts 10 down to -1 */
        in.edge = 100;
        battle_warn_reset(&w);
        n = 0;
        for (int t = 0; t < 40; ++t) if (battle_warn_tick(&w, &in) & BATTLE_WARN_BEEP_PERIMETER) ++n;
        CHECK(n == 10);
    }
    /* HIT / DAMAGED blink: 12 of 16. */
    {
        int lit = 0;
        for (int t = 1; t <= 16; ++t) lit += battle_warn_blink(t);
        CHECK(lit == 12);
    }
    if (!g_fail) printf("battle_warn: ok\n");
    return g_fail ? 1 : 0;
}
