/* Rogue Arena run rules. Made-up parts, prices and opponents only (no game
 * data in the repo); the fitting flags are the Master of Arena index rules. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acr_gen1_rogue.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_catalog g_cat;
static acr_gen1_rogue_parts g_info;
static acr_gen1_rogue_foe g_foe[80];
static int g_foes;

static void add(acr_gen1_category cat, acr_gen1_slot slot, unsigned index, uint32_t price, uint16_t weight) {
    acr_gen1_catalog_part *p = &g_cat.part[cat][index];
    memset(p, 0, sizeof *p);
    p->present = p->for_sale = 1;
    p->price = price;
    p->stats.present = 1;
    p->stats.weight = weight;
    p->stats.energy_drain = 10;
    p->flags = slot < ACR_GEN1_SLOT_COUNT ? acr_gen1_fit_flags_moa(slot, index) : 0;
}

/* Prices spread over the P tiers (1..7). */
static uint32_t price_of(unsigned i) {
    static const uint32_t k[7] = { 15000, 25000, 40000, 55000, 75000, 100000, 200000 };
    return k[i % 7];
}

static void make_catalog(void) {
    memset(&g_cat, 0, sizeof g_cat);
    memset(&g_info, 0, sizeof g_info);
    g_cat.sell_percent = 100;
    for (unsigned i = 0; i < 8; ++i) {
        add(ACR_GEN1_CAT_CORE, ACR_GEN1_SLOT_CORE, i, price_of(i), 100);
        g_cat.part[ACR_GEN1_CAT_CORE][i].stats.carry_limit = 30000;
        g_cat.part[ACR_GEN1_CAT_CORE][i].slots = 4;
        add(ACR_GEN1_CAT_HEAD, ACR_GEN1_SLOT_HEAD, i, price_of(i + 1), 100);
        add(ACR_GEN1_CAT_BOOSTER, ACR_GEN1_SLOT_BOOSTER, i, price_of(i + 2), 100);
        add(ACR_GEN1_CAT_FCS, ACR_GEN1_SLOT_FCS, i, price_of(i + 3), 50);
        add(ACR_GEN1_CAT_BLADE, ACR_GEN1_SLOT_BLADE, i % 5, price_of(i + 4), 50);
        add(ACR_GEN1_CAT_GENERATOR, ACR_GEN1_SLOT_GENERATOR, i, price_of(i + 5), 100);
        g_cat.part[ACR_GEN1_CAT_GENERATOR][i].stats.energy_drain = 60000;   /* output */
    }
    for (unsigned i = 0; i < 16; ++i) {                /* regular arms 0-11, weapon arms 12-15 */
        add(ACR_GEN1_CAT_ARMS, ACR_GEN1_SLOT_ARMS, i, price_of(i + 2), 100);
        g_info.attack[ACR_GEN1_CAT_ARMS][i] = i >= 12 ? 500 : 0;
    }
    for (unsigned i = 1; i < 20; ++i) {
        add(ACR_GEN1_CAT_HAND, ACR_GEN1_SLOT_HAND, i, price_of(i), 50);
        g_info.attack[ACR_GEN1_CAT_HAND][i] = 100;
    }
    for (unsigned i = 0; i < 33; ++i) {                /* every leg type */
        add(ACR_GEN1_CAT_LEGS, ACR_GEN1_SLOT_LEGS, i, price_of(i + 6), 200);
        g_cat.part[ACR_GEN1_CAT_LEGS][i].stats.carry_limit = 30000;
    }
    for (unsigned i = 0; i < 58; ++i) {
        add(ACR_GEN1_CAT_BACK, ACR_GEN1_SLOT_BACK_L, i, price_of(i), 50);
        if (i >= 40 && i <= 47) g_info.flags[ACR_GEN1_CAT_BACK][i] |= ACR_GEN1_ROGUE_RADAR;
        if (i == 57) g_info.flags[ACR_GEN1_CAT_BACK][i] |= ACR_GEN1_ROGUE_STEALTH;
        if (i >= 28 && i <= 36) g_info.flags[ACR_GEN1_CAT_BACK][i] |= ACR_GEN1_ROGUE_STANCE;
    }
    for (unsigned b = 0; b < 12; ++b) {
        add(ACR_GEN1_CAT_OPTION, ACR_GEN1_SLOT_COUNT, b, 20000, 0);
        g_cat.part[ACR_GEN1_CAT_OPTION][b].slots = 1;
    }
}

static void make_roster(void) {
    static const uint8_t k_entries[9] = { 50, 10, 10, 10, 10, 16, 31, 10, 20 };
    g_foes = 0;
    uint64_t s = 7;
    for (int a = 0; a < 9 && g_foes < 80; ++a)
        for (int r = 1; r < k_entries[a] && r <= 9 && g_foes < 80; ++r) {
            acr_gen1_rogue_foe *f = &g_foe[g_foes++];
            memset(f, 0, sizeof *f);
            f->arena = (uint8_t)a; f->rank = (uint8_t)r; f->entries = k_entries[a];
            for (int k = 0; k < ACR_GEN1_SLOT_COUNT; ++k) f->parts[k] = 0;
            f->parts[ACR_GEN1_SLOT_ARMS] = 1;
            f->parts[ACR_GEN1_SLOT_HAND] = 1;
            f->parts[ACR_GEN1_SLOT_BACK_R] = 1;
            f->karasawa = acr_gen1_rogue_below(&s, 8) == 0;
            acr_gen1_ai_preset(&f->ai, ACR_GEN1_AI_BALANCED);
            f->ai.rebel = acr_gen1_rogue_below(&s, 7) == 0 ? 9 : 0;
            snprintf(f->pilot, sizeof f->pilot, "P%d-%d", a, r);
            snprintf(f->ac, sizeof f->ac, "AC%d-%d", a, r);
        }
}

static acr_gen1_rogue_world world(void) {
    acr_gen1_rogue_world w = { g_foe, g_foes, &g_cat, &g_info };
    return w;
}

static void test_lineup(void) {
    const acr_gen1_rogue_world w = world();
    for (uint64_t seed = 1; seed < 200; ++seed) {
        acr_gen1_rogue_pick l[ACR_GEN1_ROGUE_SLOTS];
        EXPECT(acr_gen1_rogue_lineup(&w, seed, l) == ACR_GEN1_ROGUE_SLOTS);
        EXPECT(l[0].kind == ACR_GEN1_ROGUE_PLAYER);
        for (int i = 1; i < ACR_GEN1_ROGUE_SLOTS; ++i) {
            const acr_gen1_rogue_foe *f = acr_gen1_rogue_find(&w, l[i].arena, l[i].rank);
            EXPECT(f != NULL);
            if (!f) continue;
            EXPECT(!(f->arena == 0 && f->rank == 1));               /* Nine-Ball sits out */
            const int rank = ACR_GEN1_ROGUE_SLOTS - i;
            if (rank > 10) EXPECT(!f->karasawa);
            if (rank > 8) EXPECT(f->ai.rebel == 0);
            for (int j = 1; j < i; ++j) EXPECT(!(l[j].arena == l[i].arena && l[j].rank == l[i].rank));
        }
    }
    /* One seed, one ladder. */
    acr_gen1_rogue_pick a[ACR_GEN1_ROGUE_SLOTS], b[ACR_GEN1_ROGUE_SLOTS];
    acr_gen1_rogue_lineup(&w, 42, a);
    acr_gen1_rogue_lineup(&w, 42, b);
    EXPECT(!memcmp(a, b, sizeof a));
}

static void test_start_ac(void) {
    const acr_gen1_rogue_world w = world();
    for (uint64_t seed = 1; seed < 100; ++seed) {
        acr_gen1_rogue r;
        EXPECT(acr_gen1_rogue_new(&r, &w, seed, "RAVEN", "TEST"));
        EXPECT(r.hangar.credits == 0 && r.wins == 0);
        EXPECT(acr_gen1_rogue_check(&r, &g_cat) == 0);
        const uint8_t arms = r.hangar.fitted[ACR_GEN1_SLOT_ARMS], hand = r.hangar.fitted[ACR_GEN1_SLOT_HAND];
        EXPECT(arms >= 12 || (hand != ACR_GEN1_NO_PART && g_info.attack[ACR_GEN1_CAT_HAND][hand] > 0));
        EXPECT(!strcmp(r.hangar.pilot_name, "RAVEN"));
    }
}

static void test_prizes(void) {
    const acr_gen1_rogue_world w = world();
    acr_gen1_rogue r;
    EXPECT(acr_gen1_rogue_new(&r, &w, 1234, "RAVEN", ""));
    EXPECT(!acr_gen1_rogue_prize_due(&r));
    acr_gen1_rogue_won(&r);
    EXPECT(r.wins == 1 && r.hangar.credits == 1 && acr_gen1_rogue_prize_due(&r));
    acr_gen1_rogue_offer o[3], o2[3];
    const int n = acr_gen1_rogue_offers(&r, &w, o);
    EXPECT(n >= 1 && n <= 3);
    EXPECT(acr_gen1_rogue_offers(&r, &w, o2) == n && !memcmp(o, o2, sizeof o[0] * (size_t)n));   /* reopens the same */
    for (int i = 0; i < n; ++i) {
        if (o[i].kind != ACR_GEN1_OFFER_PART) continue;
        EXPECT(!r.hangar.owned[o[i].cat][o[i].index]);
        EXPECT(!(o[i].cat == ACR_GEN1_CAT_BACK && (g_info.flags[ACR_GEN1_CAT_BACK][o[i].index] &
                                                   (ACR_GEN1_ROGUE_RADAR | ACR_GEN1_ROGUE_STEALTH))));
        EXPECT(o[i].band == (o[i].cost <= 2 ? 0 : o[i].cost <= 4 ? 1 : 2));
    }
    /* RE-ROLL: 1 P, once per screen, different offers. */
    EXPECT(acr_gen1_rogue_reroll(&r));
    EXPECT(r.hangar.credits == 0 && !acr_gen1_rogue_can_reroll(&r));
    const int n2 = acr_gen1_rogue_offers(&r, &w, o2);
    EXPECT(n2 >= 1);
    /* PASS: +1 P, the screen is done. */
    acr_gen1_rogue_pass(&r);
    EXPECT(r.hangar.credits == 1 && !acr_gen1_rogue_prize_due(&r));
    /* Earn enough for a cheap part, take it into the AC. */
    for (int k = 0; k < 6; ++k) { acr_gen1_rogue_won(&r); acr_gen1_rogue_pass(&r); }
    acr_gen1_rogue_won(&r);
    const int m = acr_gen1_rogue_offers(&r, &w, o);
    int took = 0;
    for (int i = 0; i < m && !took; ++i) {
        if (o[i].kind != ACR_GEN1_OFFER_PART || (int32_t)o[i].cost > r.hangar.credits) continue;
        const int32_t before = r.hangar.credits;
        EXPECT(acr_gen1_rogue_take(&r, &w, &o[i], 0));
        EXPECT(r.hangar.credits == before - o[i].cost);
        EXPECT(r.hangar.fitted[o[i].put_slot[0]] == o[i].index);
        EXPECT(r.hangar.owned[o[i].cat][o[i].index] >= 1);
        EXPECT(acr_gen1_rogue_check(&r, &g_cat) == 0);
        took = 1;
    }
    EXPECT(took);
    /* Save for later, then SWAP PARTS. */
    acr_gen1_rogue_won(&r);
    const int m2 = acr_gen1_rogue_offers(&r, &w, o);
    for (int i = 0; i < m2; ++i) {
        if (o[i].kind != ACR_GEN1_OFFER_PART || (int32_t)o[i].cost > r.hangar.credits) continue;
        EXPECT(acr_gen1_rogue_take(&r, &w, &o[i], -1));
        EXPECT(r.saved_n == 1 && r.saved[0][0] == o[i].cat && r.saved[0][1] == o[i].index);
        acr_gen1_rogue_offer s;
        if (acr_gen1_rogue_saved_fit(&r, &w, 0, &s)) {
            EXPECT(acr_gen1_rogue_swap(&r, &w, 0, 0));
            EXPECT(r.saved_n == 0 && r.hangar.fitted[s.put_slot[0]] == o[i].index);
        }
        break;
    }
}

static void test_retry_ghost(void) {
    const acr_gen1_rogue_world w = world();
    acr_gen1_rogue r;
    EXPECT(acr_gen1_rogue_new(&r, &w, 99, "RAVEN", "FIRST"));
    const acr_gen1_rogue_pick ladder[ACR_GEN1_ROGUE_SLOTS];
    memcpy((void *)ladder, r.slot, sizeof r.slot);
    uint8_t lost_build[ACR_GEN1_SLOT_COUNT];
    for (int k = 0; k < 5; ++k) { acr_gen1_rogue_won(&r); acr_gen1_rogue_pass(&r); }
    memcpy(lost_build, r.hangar.fitted, sizeof lost_build);
    acr_gen1_rogue_lost(&r);
    EXPECT(r.over);
    EXPECT(acr_gen1_rogue_retry(&r, &w));
    EXPECT(!r.over && r.wins == 0 && r.attempt == 1 && r.hangar.credits == 0 && r.best == 5);
    EXPECT(r.ghosts == 1);
    /* The ghost holds rank 15 (slot 5); the old rank 19 left; ranks below moved down. */
    EXPECT(r.slot[5].kind == ACR_GEN1_ROGUE_GHOST);
    for (int i = 1; i < 5; ++i) EXPECT(!memcmp(&r.slot[i], &ladder[i + 1], sizeof r.slot[i]));
    for (int i = 6; i < ACR_GEN1_ROGUE_SLOTS; ++i) EXPECT(!memcmp(&r.slot[i], &ladder[i], sizeof r.slot[i]));
    acr_gen1_rogue_foe g;
    EXPECT(acr_gen1_rogue_rung(&r, &w, &r.slot[5], &g));
    EXPECT(!memcmp(g.parts, lost_build, sizeof lost_build) && !strcmp(g.ac, "FIRST"));
    /* The run file keeps it all. */
    static char text[16384];
    const size_t len = acr_gen1_rogue_write(&r, text, sizeof text);
    EXPECT(len > 0 && len < sizeof text);
    acr_gen1_rogue back;
    memset(&back, 0, sizeof back);
    back.hangar = r.hangar;
    EXPECT(acr_gen1_rogue_read(&back, text));
    EXPECT(back.seed == r.seed && back.attempt == r.attempt && back.best == r.best && back.ghosts == r.ghosts);
    EXPECT(!memcmp(back.slot, r.slot, sizeof r.slot));
    EXPECT(!memcmp(&back.ghost[r.slot[5].rank], &r.ghost[r.slot[5].rank], sizeof r.ghost[0]));
}

static void test_clear(void) {
    const acr_gen1_rogue_world w = world();
    acr_gen1_rogue r;
    EXPECT(acr_gen1_rogue_new(&r, &w, 5, "RAVEN", ""));
    for (int k = 0; k < ACR_GEN1_ROGUE_SLOTS - 1; ++k) {
        EXPECT(acr_gen1_rogue_next_pick(&r) != NULL);
        acr_gen1_rogue_won(&r);
        if (acr_gen1_rogue_prize_due(&r)) acr_gen1_rogue_pass(&r);
    }
    EXPECT(r.cleared && r.wins == ACR_GEN1_ROGUE_SLOTS - 1 && !acr_gen1_rogue_next_pick(&r));
    EXPECT(acr_gen1_rogue_belt(r.wins) == 10 && acr_gen1_rogue_belt(0) == 0);
}

static void test_misc(void) {
    EXPECT(acr_gen1_rogue_price(10800) == 1 && acr_gen1_rogue_price(500000) == 7 && acr_gen1_rogue_price(48000) == 4);
    EXPECT(acr_gen1_rogue_seed_from_text("00000000000000FF") == 255);
    EXPECT(acr_gen1_rogue_seed_from_text("RAVEN") == acr_gen1_rogue_seed_from_text("RAVEN"));
}

int main(void) {
    make_catalog();
    make_roster();
    test_lineup();
    test_start_ac();
    test_prizes();
    test_retry_ghost();
    test_clear();
    test_misc();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("rogue arena: all passed\n");
    return 0;
}
