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

int main(void) {
    test_parse();
    test_auto_door();
    test_locked_pair();
    test_blade_hatch();
    test_stage_move();
    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("gimmick tests passed\n");
    return 0;
}
