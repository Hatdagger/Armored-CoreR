/* Gen 1 missions: list reading, the sortie check, the income and expense
 * report and the record in the save. Made-up numbers and text only (no
 * game data in the repo); they check the rules, not the game's values. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_battle.h"
#include "acr_gen1_missions.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const char *k_toml =
    "# test list\n"
    "\n[[mission]]\nslot     = 0\nname     = \"First Job\"\nreward   = 1200\nmap      = 0\n"
    "byte_1160 = 255\noffered  = [0, 1]\nsender   = \"Someone\"\nrequester = \"A \\\"Client\\\"\"\n"
    "pages    = [\n  \"Line one\\nline two\",\n  \"%0@1Word@0 here\",\n]\npictures = 1\n"
    "\n[[mission]]\nslot     = 7\nname     = \"Second\"\nreward   = 0\nmap      = 7\n"
    "sender   = \"\"\nrequester = \"-\"\npages    = [\n]\npictures = 0\n";

static void test_list(void) {
    static acr_gen1_mission_list l;
    EXPECT(acr_gen1_missions_build(&l, NULL) == 0);
    EXPECT(l.count == 1 && l.m[0].own && l.m[0].playable);   /* the own mission is always there */
    EXPECT(l.m[0].foe_set && l.m[0].foe_parts[ACR_GEN1_SLOT_CORE] != ACR_GEN1_NO_PART);   /* with its opponent */
    EXPECT(acr_gen1_missions_build(&l, k_toml) == 2);
    EXPECT(l.count == 3);
    const acr_gen1_mission_info *a = &l.m[1], *b = &l.m[2];
    EXPECT(!strcmp(a->id, "MOA-00") && !strcmp(a->name, "First Job"));
    EXPECT(a->reward == 1200 && a->map == 0 && !a->own && a->playable && a->time_ms == 0);   /* flown on its map (2026-11-27) */
    EXPECT(!strcmp(a->sender, "Someone") && !strcmp(a->requester, "A \"Client\""));
    EXPECT(a->pages == 2 && !strcmp(a->page[0], "Line one\nline two") && !strcmp(a->page[1], "%0@1Word@0 here"));
    EXPECT(a->pictures == 1);
    EXPECT(!strcmp(b->id, "MOA-07") && b->pages == 0 && b->sender[0] == 0 && b->map == 7);
}

static void test_sortie(void) {
    EXPECT(acr_gen1_sortie_from_fail(0) == ACR_GEN1_SORTIE_OK);
    EXPECT(acr_gen1_sortie_from_fail(ACR_GEN1_FAIL_OPTION_SLOTS) == ACR_GEN1_SORTIE_OK);   /* not checked */
    EXPECT(acr_gen1_sortie_from_fail(ACR_GEN1_FAIL_NO_BOOSTER) == ACR_GEN1_SORTIE_INCOMPLETE);
    EXPECT(acr_gen1_sortie_from_fail(ACR_GEN1_FAIL_ARM_LOAD) == ACR_GEN1_SORTIE_WEIGHT);
    EXPECT(acr_gen1_sortie_from_fail(ACR_GEN1_FAIL_ENERGY) == ACR_GEN1_SORTIE_ENERGY);
    /* In the game's order: incomplete before weight before energy. */
    EXPECT(acr_gen1_sortie_from_fail(ACR_GEN1_FAIL_ENERGY | ACR_GEN1_FAIL_WEIGHT | ACR_GEN1_FAIL_MISSING_PART) ==
           ACR_GEN1_SORTIE_INCOMPLETE);
    EXPECT(acr_gen1_sortie_from_fail(ACR_GEN1_FAIL_ENERGY | ACR_GEN1_FAIL_WEIGHT) == ACR_GEN1_SORTIE_WEIGHT);
    EXPECT(!strcmp(acr_gen1_sortie_message(ACR_GEN1_SORTIE_ENERGY), "You don't have enough energy."));
    EXPECT(acr_gen1_sortie_message(ACR_GEN1_SORTIE_OK) == NULL);
}

static void test_costs(void) {
    static acr_gen1_fighter f;
    memset(&f, 0, sizeof f);
    f.weapons = 3;
    f.weapon[0].ammo = 100; f.weapon[0].ammo_price = 7; f.weapon[0].solid = 1; f.ammo[0] = 40;  /* 60 x 7 */
    f.weapon[1].ammo = 20; f.weapon[1].ammo_price = 50; f.weapon[1].solid = 0; f.ammo[1] = 0;   /* energy: free */
    f.weapon[2].ammo = 8; f.weapon[2].ammo_price = 300; f.weapon[2].solid = 1; f.ammo[2] = 8;   /* unused */
    EXPECT(acr_gen1_mission_ammo_cost(&f) == 420);

    EXPECT(acr_gen1_mission_repair_cost(5000, 5000, 400000) == 0);
    /* (5000 - 1000) x (400099 / 100 = 4000) / 8000 = 2000 */
    EXPECT(acr_gen1_mission_repair_cost(5000, 1000, 400099) == 2000);
    /* rounds down: 3 x 4000 / 8000 = 1.5 -> 1 */
    EXPECT(acr_gen1_mission_repair_cost(5000, 4997, 400000) == 1);
    EXPECT(acr_gen1_mission_repair_cost(5000, -20, 400000) == 2500);   /* destroyed: AP 0 */

    static acr_gen1_catalog c;
    static acr_gen1_hangar h;
    memset(&c, 0, sizeof c);
    memset(&h, 0, sizeof h);
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) h.fitted[s] = ACR_GEN1_NO_PART;
    h.fitted[ACR_GEN1_SLOT_HEAD] = 1; c.part[ACR_GEN1_CAT_HEAD][1].price = 1000;
    h.fitted[ACR_GEN1_SLOT_LEGS] = 2; c.part[ACR_GEN1_CAT_LEGS][2].price = 2000;
    h.fitted[ACR_GEN1_SLOT_BACK_L] = h.fitted[ACR_GEN1_SLOT_BACK_R] = 3; c.part[ACR_GEN1_CAT_BACK][3].price = 500;
    h.options = 1; c.part[ACR_GEN1_CAT_OPTION][0].price = 99999;   /* options are left out */
    EXPECT(acr_gen1_mission_ac_price(&h, &c) == 1000 + 2000 + 500 + 500);
}

static void test_settle(void) {
    static acr_gen1_hangar h;
    acr_gen1_mission_report r;
    memset(&h, 0, sizeof h);
    h.credits = 1000;
    acr_gen1_mission_settle(&h, &r, 1, 5000, 300, 420, 2000, 0);
    EXPECT(r.reward == 5000 && r.addition == 300 && r.ammo == -420 && r.repair == -2000 && r.deduction == 0);
    EXPECT(r.balance == 2880 && r.credits == 3880 && h.credits == 3880 && r.success);
    /* A failure pays no reward or addition; the costs stay. */
    acr_gen1_mission_settle(&h, &r, 0, 5000, 300, 100, 200, 50);
    EXPECT(r.reward == 0 && r.addition == 0 && r.balance == -350 && h.credits == 3530 && !r.success);
    /* The total stays within the report's limits. */
    h.credits = 9999000;
    acr_gen1_mission_settle(&h, &r, 1, 5000, 0, 0, 0, 0);
    EXPECT(h.credits == ACR_GEN1_CREDITS_TOP && r.balance == 5000);
    h.credits = -999000;
    acr_gen1_mission_settle(&h, &r, 0, 0, 0, 5000, 0, 0);
    EXPECT(h.credits == ACR_GEN1_CREDITS_BOTTOM);
}

static void test_record(void) {
    static acr_gen1_mission_record rec, back;
    memset(&rec, 0, sizeof rec);
    acr_gen1_mission_book(&rec, "P4", 0);
    acr_gen1_mission_book(&rec, "P4", 1);
    acr_gen1_mission_book(&rec, "MOA-03", 1);
    EXPECT(rec.sorties == 3 && rec.successes == 2 && rec.failures == 1);
    EXPECT(acr_gen1_mission_flags(&rec, "P4") == (ACR_GEN1_MISSION_DONE_BIT | ACR_GEN1_MISSION_FAILED_BIT));
    EXPECT(acr_gen1_mission_flags(&rec, "MOA-03") == ACR_GEN1_MISSION_DONE_BIT);
    EXPECT(acr_gen1_mission_flags(&rec, "MOA-04") == 0);
    rec.sorties = 999;
    acr_gen1_mission_book(&rec, "P4", 1);
    EXPECT(rec.sorties == 999);   /* capped as the game caps it */

    /* Round trip through a whole save: the garage reader skips the section. */
    static acr_gen1_hangar h, h2;
    memset(&h, 0, sizeof h);
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) h.fitted[s] = ACR_GEN1_NO_PART;
    h.credits = 4321;
    strcpy(h.ac_name, "AC");
    strcpy(h.pilot_name, "RAVEN");
    static char text[16384];
    size_t n = acr_gen1_save_write(&h, text, sizeof text);
    n += acr_gen1_mission_record_write(&rec, text + n, sizeof text - n);
    EXPECT(n < sizeof text);
    EXPECT(acr_gen1_save_read(&h2, text) && h2.credits == 4321);
    acr_gen1_mission_record_read(&back, text);
    EXPECT(back.sorties == 999 && back.successes == 3 && back.failures == 1);
    EXPECT(acr_gen1_mission_flags(&back, "P4") == (ACR_GEN1_MISSION_DONE_BIT | ACR_GEN1_MISSION_FAILED_BIT));
    EXPECT(acr_gen1_mission_flags(&back, "MOA-03") == ACR_GEN1_MISSION_DONE_BIT);
    /* An older save with no [missions] section: an empty record. */
    acr_gen1_mission_record_read(&back, "format = 2\n[ac]\nhead = 1\n");
    EXPECT(back.sorties == 0 && back.n == 0);
}

/* P4's variants: the 40/40/20 roll, what each is briefed and flown as. */
static void test_variants(void) {
    int count[4] = { 0, 0, 0, 0 };
    for (uint32_t r = 0; r < 1000; ++r) ++count[acr_gen1_mission_roll(r)];
    EXPECT(count[1] == 400 && count[2] == 400 && count[3] == 200);
    static acr_gen1_mission_list l;
    acr_gen1_missions_build(&l, NULL);
    const acr_gen1_mission_info *p4 = &l.m[0];
    EXPECT(!strcmp(p4->id, "P4") && p4->variants == 3);
    static acr_gen1_mission_info v;
    acr_gen1_mission_variant(p4, 1, &v);
    EXPECT(v.variant == 1 && v.briefed == 1 && v.rule == ACR_GEN1_OWN_NONE && v.reward == 10000);
    acr_gen1_mission_variant(p4, 2, &v);
    EXPECT(v.variant == 2 && v.briefed == 2 && v.rule == ACR_GEN1_OWN_P4_ALTERNATE && v.reward == 12000);
    EXPECT(v.time_ms == 180000 && v.pages == 3 && strstr(v.page[2], "MT (drone) x2"));
    acr_gen1_mission_variant(p4, 3, &v);
    /* Never briefed: variant 1's pages and reward; the rule, the bonus and the late arrival. */
    EXPECT(v.variant == 3 && v.briefed == 1 && v.rule == ACR_GEN1_OWN_P4_ALTERED);
    EXPECT(v.reward == 10000 && v.bonus == 12000 && v.extra_ms == 120000 && v.time_ms == 180000);
    EXPECT(!strcmp(v.page[0], p4->page[0]) && v.late_ac[0]);
    acr_gen1_mission_variant(&l.m[0], 9, &v);   /* out of range: variant 1 */
    EXPECT(v.variant == 1 && v.rule == ACR_GEN1_OWN_NONE);
}

/* The own-mission rules: only fighter 1 counts (P4's drones don't), and
 * P4 Altered brings the challenger below half AP before it can be won. */
static void test_own_rules(void) {
    static acr_gen1_battle b;
    memset(&b, 0, sizeof b);
    b.f[0].ap = b.f[0].ap_max = 8000;
    b.f[1].ap = b.f[1].ap_max = 8000;
    b.units = 1;
    b.unit[0].active = 1; b.unit[0].team = 1; b.unit[0].ap = 950;
    acr_gen1_mission_start_own(&b, ACR_GEN1_OWN_P4_ALTERNATE);
    EXPECT(acr_gen1_battle_result(&b, 0) == 0);
    b.f[1].ap = 0;                                  /* examiner down, a drone still up: won */
    EXPECT(acr_gen1_battle_result(&b, 0) == 1);
    b.f[1].ap = 9000;                               /* time up: the ACs' AP alone */
    EXPECT(acr_gen1_battle_result(&b, 1) == 2);
    b.f[1].ap = 3000;
    EXPECT(acr_gen1_battle_result(&b, 1) == 1);

    memset(&b, 0, sizeof b);
    b.f[0].ap = b.f[0].ap_max = 8000;
    b.f[1].ap = b.f[1].ap_max = 8000;
    acr_gen1_mission_start_own(&b, ACR_GEN1_OWN_P4_ALTERED);
    acr_gen1_mission_tick(&b);
    EXPECT(b.mission.phase == 0 && !(b.mission.flags & ACR_GEN1_MISSION_SWAP));
    b.f[1].ap = 4000;                               /* half: not yet */
    acr_gen1_mission_tick(&b);
    EXPECT(b.mission.phase == 0);
    EXPECT(acr_gen1_battle_result(&b, 1) == 1);     /* time up before it comes: more AP wins */
    b.f[1].ap = 0;                                  /* one big hit to 0: the challenger still comes */
    acr_gen1_mission_tick(&b);
    EXPECT(b.mission.phase == 1 && (b.mission.flags & ACR_GEN1_MISSION_SWAP));
    EXPECT(acr_gen1_battle_result(&b, 0) == 0);     /* not won by that */
    b.f[1].ap = b.f[1].ap_max = 7000;               /* the platform swaps fighter 1 in */
    acr_gen1_mission_challenger_in(&b);
    EXPECT(b.mission.phase == 2 && !(b.mission.flags & ACR_GEN1_MISSION_SWAP));
    acr_gen1_mission_tick(&b);
    EXPECT(!(b.mission.flags & ACR_GEN1_MISSION_BONUS) && acr_gen1_battle_result(&b, 0) == 0);
    b.f[1].ap = 0;
    acr_gen1_mission_tick(&b);
    EXPECT((b.mission.flags & ACR_GEN1_MISSION_BONUS) && acr_gen1_battle_result(&b, 0) == 1);
    b.f[0].ap = 0;                                  /* the player down loses */
    EXPECT(acr_gen1_battle_result(&b, 0) == 3);
}

int main(void) {
    test_variants();
    test_own_rules();
    test_list();
    test_sortie();
    test_costs();
    test_settle();
    test_record();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 missions: all tests passed\n");
    return 0;
}
