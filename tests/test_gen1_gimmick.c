/* Moving map parts: the mission maps' doors (acr_gen1_gimmick.h). The
 * door lines are acr-import's gimmicks.txt format; the numbers are map 0's
 * door by the botanical zone (area 290: 750 up at 8 a tick, opens when the
 * player comes near) and its locked pair (153 / 154). */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_battle.h"
#include "acr_gen1_gimmick.h"
#include "acr_gen1_stage.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const char *k_doors =
    "# a comment\n"
    "door 290 290 0 22500 0 24375 813 750 8 750 0 4 74 0 0 1 0\n"
    "door 153 153 0 2500 0 -15000 500 750 6 500 159 3 0 0 0 0 1 154\n"
    "door 154 154 0 2500 0 -15000 500 750 6 500 159 1 73 0 0 0 1 153\n"
    "door 155 155 0 -2500 0 0 500 750 0 0 161 1 0 0 0 0 1 -1\n";

static void run(acr_gen1_gimmicks *g, const int32_t p[3], int ticks, acr_gen1_gimmick_events *ev) {
    int32_t move[ACR_GEN1_MAX_DOORS][3];
    for (int t = 0; t < ticks; ++t) acr_gen1_gimmicks_tick(g, p, 100, 212, move, ev);
}

static void test_parse(void) {
    acr_gen1_gimmicks g;
    acr_gen1_gimmicks_parse(k_doors, &g);
    EXPECT(g.doors == 4);
    const acr_gen1_door *d = &g.door[0];
    EXPECT(d->game_area == 290 && d->axis == 4 && d->opens_near == 1);
    EXPECT(d->ticks == 93);                 /* 750 / 8 */
    EXPECT(d->step_lo == 8 && d->step_hi == 9);
    EXPECT(g.door[1].radio == 159 && g.door[1].partners == 1 && g.door[1].partner[0] == 154);
    EXPECT(g.door[3].ticks == 0);           /* speed 0: never moves */
    EXPECT(acr_gen1_door_find(&g, 290) == 0 && acr_gen1_door_find(&g, 1) == -1);
}

static void test_auto_door(void) {
    acr_gen1_gimmicks g;
    acr_gen1_gimmicks_parse(k_doors, &g);
    const int32_t far[3] = { 0, 0, 0 }, near[3] = { 22500, 0, 24000 };
    run(&g, far, 50, NULL);
    EXPECT(g.door[0].state == 0 && g.door[0].offset[1] == 0);
    run(&g, near, 1, NULL);
    EXPECT(g.door[0].state == 1);
    run(&g, near, 92, NULL);
    /* 93 ticks: exactly 750 up (y down), then open and holding. */
    EXPECT(g.door[0].offset[1] == -750 && g.door[0].offset[0] == 0 && g.door[0].offset[2] == 0);
    EXPECT(g.door[0].state == 3);
    run(&g, near, 100, NULL);
    EXPECT(g.door[0].state == 3 && g.door[0].offset[1] == -750);
    /* The player leaves: it shuts, back exactly where it was. */
    run(&g, far, 200, NULL);
    EXPECT(g.door[0].state == 0 && g.door[0].offset[1] == 0);
    /* Back while it is shutting: it opens again from where it got to. */
    run(&g, near, 93, NULL);
    run(&g, far, 30, NULL);
    EXPECT(g.door[0].state == 2);
    const int32_t was = g.door[0].offset[1];
    run(&g, near, 1, NULL);
    EXPECT(g.door[0].state == 1 && g.door[0].offset[1] <= was);
}

static void test_locked_pair(void) {
    acr_gen1_gimmicks g;
    acr_gen1_gimmicks_parse(k_doors, &g);
    const int32_t at[3] = { 2500, 0, -15000 }, far[3] = { 0, 0, 0 };
    acr_gen1_gimmick_events ev;
    int32_t move[ACR_GEN1_MAX_DOORS][3];
    acr_gen1_gimmicks_tick(&g, at, 100, 212, move, &ev);
    EXPECT(ev.radio == 159);                /* locked: the radio line, no move */
    EXPECT(g.door[1].state == 0 && g.door[2].state == 0);
    run(&g, at, 20, NULL);
    EXPECT(g.door[1].offset[2] == 0);
    /* The script's unlock (message 0x8201): both pieces open for good, apart. */
    acr_gen1_door_unlock(&g, 1);
    EXPECT(g.door[1].state == 4 && g.door[2].state == 4 && g.door[1].radio == 0);
    run(&g, far, 200, NULL);
    EXPECT(g.door[1].state == 6 && g.door[2].state == 6);
    /* Axis 3 -> +x, axis 1 -> -x (quarter 0): they slide apart, 500 each. */
    EXPECT(g.door[1].offset[0] == 500 && g.door[2].offset[0] == -500);
}

/* Map 1's hatch (area 57): doesn't open on its own; the blade opens it,
 * standing on it or beside it. */
static void test_blade_hatch(void) {
    acr_gen1_gimmicks g;
    acr_gen1_gimmicks_parse("door 57 57 2 9625 -250 0 750 125 10 630 0 1 74 0 1 0 0\n", &g);
    const int32_t on[3] = { 9625, -380, 0 }, far[3] = { 12000, -380, 0 };
    acr_gen1_gimmicks_blade(&g, far, 100, 212, 300, NULL);
    EXPECT(g.door[0].state == 0);
    acr_gen1_gimmicks_blade(&g, on, 100, 212, 300, NULL);
    EXPECT(g.door[0].state == 1);
    run(&g, on, 70, NULL);
    EXPECT(g.door[0].state == 3 && g.door[0].offset[0] != 0);   /* stays open */
    /* A locked door only plays its line. */
    acr_gen1_gimmicks_parse(k_doors, &g);
    const int32_t at[3] = { 2500, 0, -15000 };
    acr_gen1_gimmick_events ev = { 0 };
    acr_gen1_gimmicks_blade(&g, at, 100, 212, 300, &ev);
    EXPECT(ev.radio == 159 && g.door[1].state == 0);
    /* Map 0's door 61 (to the generator): the blade opens it too. */
    acr_gen1_gimmicks_parse("door 61 61 0 263 0 14738 813 750 8 750 0 4 74 0 0 1 0\n", &g);
    const int32_t by61[3] = { 263, 0, 13700 };
    acr_gen1_gimmicks_blade(&g, by61, 100, 212, 300, NULL);
    EXPECT(g.door[0].state == 1);
}

/* Map 0's generator (area 285, 3,000 HP): destroyed, it unlocks and opens
 * the locked pair 153 / 154 for good. */
static void test_generator(void) {
    char text[1024];
    snprintf(text, sizeof text, "%sbreakable 285 285 -2496 0 17478 450 1213 3000 1 2 153\n", k_doors);
    acr_gen1_gimmicks g;
    acr_gen1_gimmicks_parse(text, &g);
    EXPECT(g.breakables == 1 && g.breakable[0].hp == 3000);
    const int32_t miss[3] = { 0, -500, 0 }, at[3] = { -2496, -500, 17478 };
    EXPECT(!acr_gen1_gimmicks_hit(&g, miss, 100, 1000));
    EXPECT(acr_gen1_gimmicks_hit(&g, at, 100, 1000));
    acr_gen1_gimmicks_hit(&g, at, 100, 1000);
    EXPECT(g.breakable[0].hp == 1000 && g.door[1].radio == 159);
    acr_gen1_gimmicks_hit(&g, at, 100, 1000);
    EXPECT(g.breakable[0].hp == 0 && g.breakable[0].offset[1] != 0);
    EXPECT(g.door[1].radio == 0 && g.door[1].state == 4 && g.door[2].state == 4);
    EXPECT(!acr_gen1_gimmicks_hit(&g, at, 100, 1000));   /* gone */
    /* Map 2's kind: message 1 just opens (0x8200), lock or not. */
    acr_gen1_gimmicks_parse("door 72 72 0 0 2125 -1375 1000 625 12 500 159 4 74 68 0 0 0\n"
                            "breakable 70 70 5256 2125 -6375 375 300 2000 1 1 72\n", &g);
    const int32_t at2[3] = { 5256, 2000, -6375 };
    acr_gen1_gimmicks_hit(&g, at2, 100, 5000);
    EXPECT(g.door[0].state == 1);
}

static void test_stage_move(void) {
    acr_gen1_stage s;
    memset(&s, 0, sizeof s);
    const char *obj = "o area7-model1\nv 0 0 0\nv 100 0 0\nv 100 0 100\nv 0 0 100\nf 1 2 3 4\n"
                      "o area8-model1\nv 0 0 500\nv 100 0 500\nv 100 0 600\nf 5 6 7\n";
    EXPECT(acr_gen1_stage_parse(obj, &s));
    EXPECT(s.polys == 2 && s.poly[0].area == 7 && s.poly[1].area == 8);
    int32_t fy = 0;
    EXPECT(acr_gen1_stage_floor(&s, 50, 50, -100, &fy) && fy == 0);
    /* 750 up (game y down) and 200 along game x (engine x mirrored). */
    const int32_t off[3] = { 200, -750, 0 };
    acr_gen1_stage_place_area(&s, 7, off);
    EXPECT(acr_gen1_stage_floor(&s, -150, 50, -1000, &fy) && fy == -750);
    EXPECT(!acr_gen1_stage_floor(&s, 50, 50, -100, &fy));
    EXPECT(s.poly[1].v[0][1] == 0);         /* the other area stays */
    const int32_t zero[3] = { 0, 0, 0 };
    acr_gen1_stage_place_area(&s, 7, zero);
    EXPECT(acr_gen1_stage_floor(&s, 50, 50, -100, &fy) && fy == 0);
    acr_gen1_stage_free(&s);
}

/* Map 18's lift 58 (-7,875 to -2,250 at 25, starting at the top) with
 * its call button 99, and the core 96 (game numbers as acr-import writes
 * them). */
static const char *k_lifts =
    "lift 58 58 -5625 -7875 11875 1000 438 25 -7875 -2250 0 0 0 0 0 114 78 78\n"
    "lift 95 96 0 -2250 22375 1000 438 20 -2250 0 0 0 0 0 0 114 78 78\n"
    "button 98 99 -5963 -2562 12663 125 125 58 78\n";

static void test_lift(void) {
    acr_gen1_gimmicks g;
    acr_gen1_gimmicks_parse(k_lifts, &g);
    EXPECT(g.lifts == 2 && g.buttons == 1);
    const int l = acr_gen1_lift_find(&g, 58);
    EXPECT(l == 0 && acr_gen1_lift_find(&g, 96) == 1 && g.lift[0].way == 0);
    int32_t move[ACR_GEN1_MAX_LIFTS][3];
    const int32_t away[3] = { 20000, 0, 0 };
    for (int t = 0; t < 30; ++t) acr_gen1_lifts_tick(&g, away, 100, 212, move);
    EXPECT(g.lift[l].offset[1] == 0);             /* at its end: stays */
    /* The call button: the lift comes down, speeding up to 25 a tick and
     * slowing to a stop exactly at -2,250. */
    const int32_t at_button[3] = { -5963, -2562, 12663 };
    acr_gen1_lifts_tick(&g, at_button, 100, 212, move);
    EXPECT(g.lift[l].way == 1);
    int top = 0;
    for (int t = 0; t < 600; ++t) {
        acr_gen1_lifts_tick(&g, away, 100, 212, move);
        if (move[l][1] > top) top = move[l][1];
    }
    EXPECT(top == 25);
    EXPECT(g.lift[l].centre[1] + g.lift[l].offset[1] == -2250);
    /* Standing on it: carried and bumped (it flips back up). */
    const int32_t on[3] = { -5625, -2250 - 438, 11875 };
    EXPECT(acr_gen1_lift_under(&g.lift[l], on, 100));
    acr_gen1_lifts_tick(&g, on, 100, 212, move);
    EXPECT(g.lift[l].way == 0);
    for (int t = 0; t < 600; ++t) acr_gen1_lifts_tick(&g, on, 100, 212, move);
    EXPECT(g.lift[l].centre[1] + g.lift[l].offset[1] == -7875);   /* no second flip while on it */
    /* The core: bumped -> state 1 (map 18's win). */
    const int32_t core[3] = { 0, -2250, 22375 - 600 };
    acr_gen1_lifts_tick(&g, core, 100, 212, move);
    EXPECT(g.lift[1].way == 1);
}

int main(void) {
    test_lift();
    test_parse();
    test_auto_door();
    test_locked_pair();
    test_blade_hatch();
    test_generator();
    test_stage_move();
    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("gimmick tests passed\n");
    return 0;
}
