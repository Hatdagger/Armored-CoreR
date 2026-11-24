/* The Arena's ranked ladders. Made-up opponents only (no game data in the
 * repo): a 50-place main arena, a 10-place leg arena and a 16-place
 * Champion arena. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acr_gen1_arena.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_rogue_foe g_foe[120];
static acr_gen1_catalog g_cat;
static acr_gen1_arena_world g_w;

static void add_arena(int arena, int places) {
    for (int r = 1; r < places; ++r) {
        acr_gen1_rogue_foe *f = &g_foe[g_w.foes++];
        memset(f, 0, sizeof *f);
        f->arena = (uint8_t)arena;
        f->rank = (uint8_t)r;
        f->entries = (uint8_t)places;
        f->prize_money = (uint32_t)(1000 * (places - r));
        f->prize_part = 255;
        snprintf(f->pilot, sizeof f->pilot, "P%d-%d", arena, r);
    }
}

static void make_world(void) {
    memset(&g_w, 0, sizeof g_w);
    memset(&g_cat, 0, sizeof g_cat);
    g_w.foe = g_foe;
    g_w.cat = &g_cat;
    add_arena(0, 50);
    add_arena(1, 10);
    add_arena(5, 16);
    /* Prizes as on the disc: arena 0 rank 3 (entry 2) -> id 4 (arms 8),
     * rank 9 (entry 8) -> id 10 (back 17, a paired back weapon); Champion
     * record 0 -> id 22 (no part table). */
    for (int i = 0; i < g_w.foes; ++i) {
        acr_gen1_rogue_foe *f = &g_foe[i];
        if (f->arena == 0 && f->rank == 3) f->prize_part = 4;
        if (f->arena == 0 && f->rank == 9) f->prize_part = 10;
        if (f->arena == 5 && f->rank == 1) f->prize_part = 13;   /* a part, to see it given */
        if (f->arena == 5 && f->rank == 2) f->prize_part = 0;    /* ignored: Champion gives record 0's only */
    }
}

/* Win against the opponent just above until rank `to`, in arena a. */
static void climb(acr_gen1_arena *s, int a, int to) {
    for (int guard = 0; guard < 200 && acr_gen1_arena_rank(s, a) > to; ++guard) {
        const int me = acr_gen1_arena_rank(s, a);
        int target = 0;
        for (int r = me - 1; r >= 1 && !target; --r)
            if (acr_gen1_arena_can_challenge(s, a, r, a >= 1 && a <= 4 ? a : 0, 0) == ACR_GEN1_ARENA_OK) target = r;
        if (!target) break;
        EXPECT(acr_gen1_arena_result(s, &g_w, a, target, 1, NULL));
    }
}

static void test_new(void) {
    acr_gen1_arena s;
    acr_gen1_arena_new(&s, &g_w);
    EXPECT(s.a[0].places == 50);
    EXPECT(s.a[1].places == 10);
    EXPECT(s.a[2].places == 0);
    EXPECT(acr_gen1_arena_rank(&s, 0) == 50);
    EXPECT(acr_gen1_arena_holder(&s, 0, 1) == 1);
    EXPECT(acr_gen1_arena_holder(&s, 0, 49) == 49);
    EXPECT(acr_gen1_arena_holder(&s, 0, 50) == 0);
    EXPECT(acr_gen1_arena_open(&s, 0) == ACR_GEN1_ARENA_OK);
    EXPECT(acr_gen1_arena_open(&s, 1) == ACR_GEN1_ARENA_LOCKED);
    EXPECT(acr_gen1_arena_open(&s, 2) == ACR_GEN1_ARENA_NO_DATA);
    EXPECT(acr_gen1_arena_timer_ms(ACR_GEN1_ARENA_TIMER_2MIN) == 120000);
    EXPECT(acr_gen1_arena_timer_ms(ACR_GEN1_ARENA_TIMER_5MIN) == 300000);
    EXPECT(acr_gen1_arena_timer_ms(ACR_GEN1_ARENA_TIMER_NONE) == 0);
}

static void test_challenge_rules(void) {
    acr_gen1_arena s;
    acr_gen1_arena_new(&s, &g_w);
    /* Sub arena: anyone above in it; the ranked list stays closed. */
    EXPECT(acr_gen1_arena_can_challenge(&s, 0, 49, 0, 0) == ACR_GEN1_ARENA_OK);
    EXPECT(acr_gen1_arena_can_challenge(&s, 0, 16, 0, 0) == ACR_GEN1_ARENA_OK);
    EXPECT(acr_gen1_arena_can_challenge(&s, 0, 15, 0, 0) == ACR_GEN1_ARENA_SUB_FIRST);
    EXPECT(acr_gen1_arena_can_challenge(&s, 0, 50, 0, 0) == ACR_GEN1_ARENA_REFERENCE);
    EXPECT(acr_gen1_arena_can_challenge(&s, 0, 40, 0, ACR_GEN1_FAIL_WEIGHT) == ACR_GEN1_ARENA_INCOMPLETE);
    /* Beat rank 20 from 50: you go to 49, it drops to 50, 21-49 move up. */
    acr_gen1_arena_payout p;
    EXPECT(acr_gen1_arena_result(&s, &g_w, 0, 20, 1, &p));
    EXPECT(p.won && p.old_rank == 50 && p.new_rank == 49);
    EXPECT(p.money == 1000u * 30u);
    EXPECT(acr_gen1_arena_rank(&s, 0) == 49);
    EXPECT(acr_gen1_arena_holder(&s, 0, 50) == 20);
    EXPECT(acr_gen1_arena_holder(&s, 0, 20) == 21);
    EXPECT(acr_gen1_arena_holder(&s, 0, 48) == 49);
    EXPECT(acr_gen1_arena_holder(&s, 0, 19) == 19);
    EXPECT(s.a[0].beaten[19] == 1);
    /* A loss changes no rank. */
    EXPECT(acr_gen1_arena_result(&s, &g_w, 0, 30, 0, &p));
    EXPECT(!p.won && p.new_rank == 49 && acr_gen1_arena_rank(&s, 0) == 49);
    EXPECT(s.a[0].challenges == 2 && s.a[0].wins == 1 && s.a[0].losses == 1);
    /* Can't book a fight against someone below. */
    EXPECT(!acr_gen1_arena_result(&s, &g_w, 0, 50, 1, &p));
}

static void test_sub_never_strands(void) {
    /* Win the sub arena from the bottom in a scrambled order: every
     * opponent above stays unbeaten, so the player always has a fight. */
    acr_gen1_arena s;
    acr_gen1_arena_new(&s, &g_w);
    unsigned seed = 7;
    for (int guard = 0; guard < 100 && acr_gen1_arena_rank(&s, 0) > 16; ++guard) {
        const int me = acr_gen1_arena_rank(&s, 0);
        int ok[50], n = 0;
        for (int r = 16; r < me; ++r) if (acr_gen1_arena_can_challenge(&s, 0, r, 0, 0) == ACR_GEN1_ARENA_OK) ok[n++] = r;
        EXPECT(n == me - 16);       /* everyone above in the sub arena */
        if (!n) break;
        seed = seed * 1103515245u + 12345u;
        acr_gen1_arena_result(&s, &g_w, 0, ok[(seed >> 8) % (unsigned)n], 1, NULL);
    }
    EXPECT(acr_gen1_arena_rank(&s, 0) == 16);
    /* Below the player: the 34 beaten sub-arena opponents, none can be refought. */
    for (int r = 17; r <= 50; ++r) EXPECT(s.a[0].beaten[acr_gen1_arena_holder(&s, 0, r) - 1]);
    /* The ranked list is open now; leg arenas not yet (rank 16 is not 15). */
    EXPECT(acr_gen1_arena_can_challenge(&s, 0, 1, 0, 0) == ACR_GEN1_ARENA_OK);
    EXPECT(acr_gen1_arena_open(&s, 1) == ACR_GEN1_ARENA_LOCKED);
    EXPECT(acr_gen1_arena_result(&s, &g_w, 0, 9, 1, NULL));
    EXPECT(acr_gen1_arena_rank(&s, 0) == 15);
    EXPECT(acr_gen1_arena_open(&s, 1) == ACR_GEN1_ARENA_OK);
    EXPECT(acr_gen1_arena_open(&s, 5) == ACR_GEN1_ARENA_LOCKED);
}

static void test_prizes(void) {
    acr_gen1_arena s;
    acr_gen1_arena_new(&s, &g_w);
    climb(&s, 0, 16);
    /* Rank 3 (prize 4: AN-891-S, owned 37 = arms 8), first time only. */
    acr_gen1_hangar h;
    memset(&h, 0, sizeof h);
    h.credits = 100;
    acr_gen1_arena_payout p;
    EXPECT(acr_gen1_arena_result(&s, &g_w, 0, 3, 1, &p));
    EXPECT(p.prize_id == 4 && p.part_cat == ACR_GEN1_CAT_ARMS && p.part_index == 8);
    acr_gen1_arena_pay(&h, &p);
    EXPECT(h.credits == 100 + 1000 * 47);
    EXPECT(h.owned[ACR_GEN1_CAT_ARMS][8] == 1);
    /* It dropped to 16 (just below). Climb past it, then beat it again:
     * money again, no second part. */
    climb(&s, 0, 2);
    int rank_of_3 = 0;
    for (int r = 1; r <= 50; ++r) if (acr_gen1_arena_holder(&s, 0, r) == 3) rank_of_3 = r;
    EXPECT(rank_of_3 > 2);
    /* Paired back weapon: prize 10 (owned 174 = back 17) gives 2 copies. */
    acr_gen1_arena_payout q;
    memset(&q, 0, sizeof q);
    q.won = 1; q.prize_id = 10;
    EXPECT(acr_gen1_arena_prize_part(10, &q.part_cat, &q.part_index));
    EXPECT(q.part_cat == ACR_GEN1_CAT_BACK && q.part_index == 17);
    acr_gen1_arena_pay(&h, &q);
    EXPECT(h.owned[ACR_GEN1_CAT_BACK][17] == 2);
    /* P77-ST (prize 21, owned 214 = back 57) is single. */
    EXPECT(acr_gen1_arena_prize_part(21, &q.part_cat, &q.part_index));
    EXPECT(q.part_cat == ACR_GEN1_CAT_BACK && q.part_index == 57);
    acr_gen1_arena_pay(&h, &q);
    EXPECT(h.owned[ACR_GEN1_CAT_BACK][57] == 1);
    /* Prize 22 is not in the ten part tables. */
    EXPECT(!acr_gen1_arena_prize_part(22, NULL, NULL));
    /* Other table spots from arena-prizes.md. */
    int c, i;
    EXPECT(acr_gen1_arena_prize_part(0, &c, &i) && c == ACR_GEN1_CAT_BLADE && i == 4);
    EXPECT(acr_gen1_arena_prize_part(15, &c, &i) && c == ACR_GEN1_CAT_LEGS && i == 32);
    EXPECT(acr_gen1_arena_prize_part(18, &c, &i) && c == ACR_GEN1_CAT_HEAD && i == 11);
    EXPECT(acr_gen1_arena_prize_part(5, &c, &i) && c == ACR_GEN1_CAT_BOOSTER && i == 7);
    /* Credits cap. */
    h.credits = ACR_GEN1_ARENA_CREDITS_MAX - 5;
    memset(&q, 0, sizeof q);
    q.won = 1; q.money = 1000; q.part_cat = -1;
    acr_gen1_arena_pay(&h, &q);
    EXPECT(h.credits == ACR_GEN1_ARENA_CREDITS_MAX);
}

static void test_second_win_no_part(void) {
    acr_gen1_arena s;
    acr_gen1_arena_new(&s, &g_w);
    climb(&s, 0, 16);
    acr_gen1_arena_payout p;
    EXPECT(acr_gen1_arena_result(&s, &g_w, 0, 9, 1, &p));
    EXPECT(p.prize_id == 10);
    /* Rank 9's opponent is now 16; the player at 15. Lose the place back
     * isn't possible (losses don't move ranks), so check refights from a
     * fresh ladder where it stays above: beaten flag + part_given hold. */
    EXPECT(s.a[0].part_given[8] == 1);
    s.a[0].beaten[8] = 0;   /* even if beaten were cleared (stock refights) */
    const int at = 16;
    EXPECT(acr_gen1_arena_holder(&s, 0, at) == 9);
}

static void test_leg_arena_and_champion(void) {
    acr_gen1_arena s;
    acr_gen1_arena_new(&s, &g_w);
    climb(&s, 0, 15);
    EXPECT(acr_gen1_arena_can_challenge(&s, 1, 5, ACR_GEN1_ARENA_LEGS_FOUR, 0) == ACR_GEN1_ARENA_WRONG_LEGS);
    EXPECT(acr_gen1_arena_can_challenge(&s, 1, 5, ACR_GEN1_ARENA_LEGS_HUMANOID, 0) == ACR_GEN1_ARENA_OK);
    climb(&s, 1, 1);
    EXPECT(acr_gen1_arena_rank(&s, 1) == 1);
    EXPECT(s.a[1].best == 1);
    /* Champion opens at main-arena rank 1. */
    EXPECT(acr_gen1_arena_open(&s, 5) == ACR_GEN1_ARENA_LOCKED);
    climb(&s, 0, 1);
    EXPECT(acr_gen1_arena_rank(&s, 0) == 1);
    EXPECT(acr_gen1_arena_open(&s, 5) == ACR_GEN1_ARENA_OK);
    EXPECT(acr_gen1_arena_open(&s, 8) == ACR_GEN1_ARENA_NO_DATA);
    /* Champion: no part until all 15 beaten, then record 0's, once. */
    int parts = 0, set = 0, fights = 0;
    for (int guard = 0; guard < 100 && acr_gen1_arena_rank(&s, 5) > 1; ++guard) {
        const int me = acr_gen1_arena_rank(&s, 5);
        acr_gen1_arena_payout p;
        acr_gen1_arena_result(&s, &g_w, 5, me - 1, 1, &p);
        ++fights;
        if (p.prize_id >= 0) { ++parts; set = p.set_prize; EXPECT(p.prize_id == 13); }
    }
    EXPECT(fights == 15 && parts == 1 && set == 1);
    EXPECT(s.a[5].set_given);
}

static void test_save_round_trip(void) {
    acr_gen1_arena s;
    acr_gen1_arena_new(&s, &g_w);
    s.timer = ACR_GEN1_ARENA_TIMER_NONE;
    s.stage = 39;
    acr_gen1_arena_result(&s, &g_w, 0, 30, 1, NULL);
    acr_gen1_arena_result(&s, &g_w, 0, 31, 0, NULL);
    static char text[16384];
    const size_t n = acr_gen1_arena_write(&s, text, sizeof text);
    EXPECT(n > 0 && n < sizeof text);
    acr_gen1_arena r;
    memset(&r, 0xAB, sizeof r);
    EXPECT(acr_gen1_arena_read(&r, text));
    EXPECT(memcmp(&r, &s, sizeof r) == 0);
    EXPECT(acr_gen1_arena_sync(&r, &g_w) == 0);
    /* A broken ladder (the player twice) is refused. */
    char *bad = strstr(text, "holder = [");
    EXPECT(bad != NULL);
    if (bad) { bad += 10; bad[0] = '2'; bad[1] = '5'; bad[2] = '5'; }
    acr_gen1_arena keep = r;
    EXPECT(!acr_gen1_arena_read(&r, text));
    EXPECT(memcmp(&r, &keep, sizeof r) == 0);
    EXPECT(!acr_gen1_arena_read(&r, "format = 9\n"));
    /* An import that lost an arena: that arena starts over. */
    r.a[2].places = 10;
    EXPECT(acr_gen1_arena_sync(&r, &g_w) == 1);
    EXPECT(r.a[2].places == 0);
}

int main(void) {
    make_world();
    test_new();
    test_challenge_rules();
    test_sub_never_strands();
    test_prizes();
    test_second_win_no_part();
    test_leg_arena_and_champion();
    test_save_round_trip();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 arena: all passed\n");
    return 0;
}
