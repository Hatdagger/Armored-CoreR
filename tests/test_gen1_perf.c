/* Gen 1 garage PERFORMANCE page and GRADE. Made-up parts only, to check
 * the sums; the game's own figures come from the player's disc. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_garage.h"
#include "acr_gen1_perf.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_perf_part g_head, g_core, g_arms, g_legs, g_boost, g_hand, g_blade, g_back, g_gen, g_fcs;

static void make_parts(void) {
    memset(&g_head, 0, sizeof g_head); g_head.ap = 1000; g_head.defense_shell = 100; g_head.defense_energy = 50;
    g_head.radar_range = 6000;
    memset(&g_core, 0, sizeof g_core); g_core.ap = 2000; g_core.defense_shell = 300; g_core.defense_energy = 200;
    memset(&g_arms, 0, sizeof g_arms); g_arms.ap = 1500; g_arms.defense_shell = 200; g_arms.defense_energy = 150;
    memset(&g_legs, 0, sizeof g_legs); g_legs.ap = 3000; g_legs.defense_shell = 400; g_legs.defense_energy = 300;
    g_legs.moving_speed = 250; g_legs.turn_accel = 100; g_legs.stability = 1000;
    memset(&g_boost, 0, sizeof g_boost); g_boost.power = 9000;
    memset(&g_hand, 0, sizeof g_hand); g_hand.attack = 300;
    memset(&g_blade, 0, sizeof g_blade); g_blade.attack = 700;
    memset(&g_back, 0, sizeof g_back); g_back.attack = 800;
    memset(&g_gen, 0, sizeof g_gen);
    memset(&g_fcs, 0, sizeof g_fcs); g_fcs.max_locks = 3; g_fcs.fcs_range = 5000; g_fcs.lock_time = 50;
    const int16_t w[4] = { 200, 150, 100, 50 }, h[4] = { 200, 150, 100, 55 };
    memcpy(g_fcs.lock_w, w, sizeof w);
    memcpy(g_fcs.lock_h, h, sizeof h);
}

static void put(acr_gen1_perf_build *b, acr_gen1_slot s, const acr_gen1_perf_part *p, uint32_t weight, uint32_t price) {
    b->part[s] = p; b->weight[s] = weight; b->price[s] = price;
}

static acr_gen1_perf_build base(void) {
    acr_gen1_perf_build b;
    memset(&b, 0, sizeof b);
    put(&b, ACR_GEN1_SLOT_HEAD, &g_head, 200, 1000);
    put(&b, ACR_GEN1_SLOT_CORE, &g_core, 1000, 5000);
    put(&b, ACR_GEN1_SLOT_ARMS, &g_arms, 800, 3000);
    put(&b, ACR_GEN1_SLOT_LEGS, &g_legs, 2000, 4000);
    put(&b, ACR_GEN1_SLOT_BOOSTER, &g_boost, 200, 2000);
    put(&b, ACR_GEN1_SLOT_HAND, &g_hand, 400, 1500);
    put(&b, ACR_GEN1_SLOT_BLADE, &g_blade, 100, 1200);
    put(&b, ACR_GEN1_SLOT_BACK_L, &g_back, 300, 1800);
    put(&b, ACR_GEN1_SLOT_GENERATOR, &g_gen, 500, 2500);
    put(&b, ACR_GEN1_SLOT_FCS, &g_fcs, 20, 900);
    return b;
}

static void test_page(void) {
    acr_gen1_perf_build b = base();
    acr_gen1_perf o;
    acr_gen1_performance(&b, &o);
    EXPECT(o.ap == 7500);
    EXPECT(o.weight == 5520);
    EXPECT(o.price == 22900);
    EXPECT(o.offensive == 1800);          /* hand + blade + back */
    EXPECT(o.defensive == 1700);          /* shell 1000 + energy 700 */
    EXPECT(o.stability == 1000);
    EXPECT(o.moving_speed == 2500 && o.turning_speed == 1000);
    EXPECT(o.boost_ability == 1000);      /* 9000 / 5520 = 1, then x 1000 */
    EXPECT(o.mobility == 4500);
    EXPECT(o.radar_range == 600);
    EXPECT(o.fcs_performance == 400);     /* 500 + 50 + 50 + 300 - 500 */
    EXPECT(o.support == 1000);
    EXPECT(o.overall == 10000);
    EXPECT(o.grade == ACR_GEN1_GRADE_OK);

    /* Options: price, screens (x1.15 in 1/4096ths), balancers. */
    b = base();
    b.option_price = 777;
    b.options = ACR_GEN1_OPTION_SHELL_SCREEN | ACR_GEN1_OPTION_ENERGY_SCREEN;
    g_head.defense_shell = 101;   /* shell 1001: 1001 x 4096 x 115 / 100 = 4715110 */
    acr_gen1_performance(&b, &o);
    EXPECT(o.price == 22900 + 777);
    EXPECT(o.defensive == (4715110 + 700 * 4096 * 115 / 100) >> 12);   /* 1956 */
    g_head.defense_shell = 100;
    b.options = ACR_GEN1_OPTION_BALANCER;
    acr_gen1_performance(&b, &o);
    EXPECT(o.stability == 1800);
    b.options = ACR_GEN1_OPTION_BALANCER | ACR_GEN1_OPTION_BALANCER_RE;
    acr_gen1_performance(&b, &o);
    EXPECT(o.stability == 2600);          /* SP-ABS_Re instead of SP-ABS, not both */
    b.options = ACR_GEN1_OPTION_BALANCER | ACR_GEN1_OPTION_ABSORBER;
    acr_gen1_performance(&b, &o);
    EXPECT(o.stability == 2600);

    /* Weapon arms: their own attack, the hand weapon and blade don't count. */
    acr_gen1_perf_part warms = g_arms;
    warms.attack = 2000;
    b = base();
    b.part[ACR_GEN1_SLOT_ARMS] = &warms;
    b.weapon_arms = 1;
    acr_gen1_performance(&b, &o);
    EXPECT(o.offensive == 2800);

    /* Caterpillars: 9,800 in place of the booster. */
    b = base();
    b.part[ACR_GEN1_SLOT_BOOSTER] = NULL;
    b.caterpillar = 1;
    acr_gen1_performance(&b, &o);
    EXPECT(o.weight == 5320);
    EXPECT(o.boost_ability == 1000);      /* 9800 / 5320 = 1 */
    put(&b, ACR_GEN1_SLOT_LEGS, &g_legs, 8000, 4000);
    acr_gen1_performance(&b, &o);
    EXPECT(o.boost_ability == 0);         /* 9800 / 11320 = 0 */
}

static void test_radar(void) {
    acr_gen1_perf_part radar;
    memset(&radar, 0, sizeof radar);
    radar.radar_unit = 1;
    radar.radar_range = 9990;
    acr_gen1_perf_build b = base();
    acr_gen1_perf o;
    put(&b, ACR_GEN1_SLOT_BACK_R, &radar, 100, 100);
    acr_gen1_performance(&b, &o);
    EXPECT(o.radar_range == 999);
    radar.radar_range = 3000;             /* a back radar replaces the head's, even a shorter one */
    acr_gen1_performance(&b, &o);
    EXPECT(o.radar_range == 300);
    acr_gen1_perf_part radar2 = radar;
    radar2.radar_range = 4500;
    put(&b, ACR_GEN1_SLOT_BACK_L, &radar2, 100, 100);
    acr_gen1_performance(&b, &o);
    EXPECT(o.radar_range == 450);         /* the better of two */

    /* Human PLUS: 5,120 only when no radar at all. */
    acr_gen1_perf_part blind = g_head;
    blind.radar_range = 0;
    b = base();
    b.part[ACR_GEN1_SLOT_HEAD] = &blind;
    acr_gen1_performance(&b, &o);
    EXPECT(o.radar_range == 0);
    b.human_plus = 1;
    acr_gen1_performance(&b, &o);
    EXPECT(o.radar_range == 512);
    b.part[ACR_GEN1_SLOT_HEAD] = &g_head;
    acr_gen1_performance(&b, &o);
    EXPECT(o.radar_range == 600);
}

static void test_grade(void) {
    EXPECT(acr_gen1_grade_of(-5) == ACR_GEN1_GRADE_POOR);
    EXPECT(acr_gen1_grade_of(9999) == ACR_GEN1_GRADE_POOR);
    EXPECT(acr_gen1_grade_of(10000) == ACR_GEN1_GRADE_OK);
    EXPECT(acr_gen1_grade_of(14999) == ACR_GEN1_GRADE_OK);
    EXPECT(acr_gen1_grade_of(15000) == ACR_GEN1_GRADE_FINE);
    EXPECT(acr_gen1_grade_of(19999) == ACR_GEN1_GRADE_FINE);
    EXPECT(acr_gen1_grade_of(20000) == ACR_GEN1_GRADE_GREAT);
    EXPECT(acr_gen1_grade_of(24999) == ACR_GEN1_GRADE_GREAT);
    EXPECT(acr_gen1_grade_of(25000) == ACR_GEN1_GRADE_SUPERFINE);
    EXPECT(!strcmp(acr_gen1_grade_name(ACR_GEN1_GRADE_SUPERFINE), "SUPERFINE"));
    EXPECT(!strcmp(acr_gen1_grade_name(ACR_GEN1_GRADE_POOR), "POOR"));
}

static void test_complete(void) {
    acr_gen1_perf_build b = base();
    EXPECT(acr_gen1_perf_complete(&b));
    b.part[ACR_GEN1_SLOT_HAND] = b.part[ACR_GEN1_SLOT_BLADE] = b.part[ACR_GEN1_SLOT_BACK_L] = NULL;
    EXPECT(acr_gen1_perf_complete(&b));   /* weapons are optional */
    b.part[ACR_GEN1_SLOT_BOOSTER] = NULL;
    EXPECT(!acr_gen1_perf_complete(&b));
    b.caterpillar = 1;
    EXPECT(acr_gen1_perf_complete(&b));
    b.part[ACR_GEN1_SLOT_FCS] = NULL;
    EXPECT(!acr_gen1_perf_complete(&b));
}

/* The garage hands the page its fitted parts, option prices and leg / arm types. */
static void test_garage(void) {
    static acr_gen1_catalog c;
    memset(&c, 0, sizeof c);
    const struct { acr_gen1_category cat; acr_gen1_slot slot; unsigned index; uint16_t weight; uint32_t price; } k[] = {
        { ACR_GEN1_CAT_CORE, ACR_GEN1_SLOT_CORE, 0, 1000, 5000 },
        { ACR_GEN1_CAT_HEAD, ACR_GEN1_SLOT_HEAD, 0, 200, 1000 },
        { ACR_GEN1_CAT_ARMS, ACR_GEN1_SLOT_ARMS, 12, 800, 3000 },      /* weapon arms */
        { ACR_GEN1_CAT_LEGS, ACR_GEN1_SLOT_LEGS, 27, 2000, 4000 },     /* caterpillars */
        { ACR_GEN1_CAT_GENERATOR, ACR_GEN1_SLOT_GENERATOR, 0, 500, 2500 },
        { ACR_GEN1_CAT_FCS, ACR_GEN1_SLOT_FCS, 0, 20, 900 },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        acr_gen1_catalog_part *p = &c.part[k[i].cat][k[i].index];
        p->present = 1;
        p->price = k[i].price;
        p->stats.present = 1;
        p->stats.weight = k[i].weight;
        p->flags = acr_gen1_fit_flags_moa(k[i].slot, k[i].index);
    }
    c.part[ACR_GEN1_CAT_ARMS][12].perf.attack = 2000;
    c.part[ACR_GEN1_CAT_OPTION][3].present = 1;
    c.part[ACR_GEN1_CAT_OPTION][3].price = 450;
    c.part[ACR_GEN1_CAT_OPTION][7].present = 1;
    c.part[ACR_GEN1_CAT_OPTION][7].price = 50;
    acr_gen1_hangar h;
    const uint8_t start[ACR_GEN1_SLOT_COUNT] = { 0, ACR_GEN1_NO_PART, 0, 12, ACR_GEN1_NO_PART, ACR_GEN1_NO_PART,
                                                 ACR_GEN1_NO_PART, ACR_GEN1_NO_PART, 0, 0, 27 };
    acr_gen1_hangar_new(&h, start, (1u << 3) | (1u << 7), 0);
    acr_gen1_perf_build b;
    acr_gen1_garage_perf_build(&h, &c, 0, &b);
    EXPECT(b.weapon_arms && b.caterpillar && !b.human_plus);
    EXPECT(b.option_price == 500);
    EXPECT(b.part[ACR_GEN1_SLOT_BOOSTER] == NULL && b.part[ACR_GEN1_SLOT_ARMS] == &c.part[ACR_GEN1_CAT_ARMS][12].perf);
    EXPECT(acr_gen1_perf_complete(&b));
    acr_gen1_perf o;
    acr_gen1_performance(&b, &o);
    EXPECT(o.weight == 4520 && o.price == 16400 + 500);
    EXPECT(o.offensive == 2000);
    EXPECT(o.stability == 800);           /* option 3 = SP-ABS */
}

int main(void) {
    make_parts();
    test_page();
    test_radar();
    test_grade();
    test_complete();
    test_garage();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 perf: all passed\n");
    return 0;
}
