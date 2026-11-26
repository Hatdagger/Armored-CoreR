/* Rogue Arena co-op rules. Made-up parts, prices and opponents only (no
 * game data in the repo; gen1_rogue_fixture.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acr_gen1_coop.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#include "gen1_rogue_fixture.h"

#define SLOTS ACR_GEN1_ROGUE_SLOTS

static const char *k_pilot[2] = { "RAVEN", "HAWK" }, *k_ac[2] = { "FIRST", "SECOND" };

static acr_gen1_coop g_c;   /* large: off the stack */

static void test_pairs(void) {
    const acr_gen1_rogue_world w = world();
    for (uint64_t seed = 1; seed < 200; ++seed) {
        acr_gen1_rogue_pick l[SLOTS][2];
        EXPECT(acr_gen1_rogue_lineup_pairs(&w, seed, l) == SLOTS);
        EXPECT(l[0][0].kind == ACR_GEN1_ROGUE_PLAYER && l[0][1].kind == ACR_GEN1_ROGUE_PLAYER);
        for (int i = 1; i < SLOTS; ++i)
            for (int k = 0; k < 2; ++k) {
                const acr_gen1_rogue_foe *f = acr_gen1_rogue_find(&w, l[i][k].arena, l[i][k].rank);
                EXPECT(l[i][k].kind == ACR_GEN1_ROGUE_FOE && f != NULL);
                if (!f) continue;
                EXPECT(!(f->arena == 0 && f->rank == 1));       /* Nine-Ball sits out */
                const int rank = SLOTS - i;
                if (rank > 10) EXPECT(!f->karasawa);             /* counted in pairs */
                if (rank > 8) EXPECT(f->ai.rebel == 0);
                /* 38 different opponents. */
                for (int j = 1; j <= i; ++j)
                    for (int m = 0; m < 2; ++m)
                        if (j < i || m < k)
                            EXPECT(!(l[j][m].arena == l[i][k].arena && l[j][m].rank == l[i][k].rank));
            }
    }
    acr_gen1_rogue_pick a[SLOTS][2], b[SLOTS][2];
    acr_gen1_rogue_lineup_pairs(&w, 42, a);
    acr_gen1_rogue_lineup_pairs(&w, 42, b);
    EXPECT(!memcmp(a, b, sizeof a));
    /* The pairs run easiest to hardest on the curve: the bottom pair's
     * average score is below the top pair's. */
    const int lo = acr_gen1_rogue_score(acr_gen1_rogue_find(&w, a[1][0].arena, a[1][0].rank)) +
                   acr_gen1_rogue_score(acr_gen1_rogue_find(&w, a[1][1].arena, a[1][1].rank));
    const int hi = acr_gen1_rogue_score(acr_gen1_rogue_find(&w, a[SLOTS - 1][0].arena, a[SLOTS - 1][0].rank)) +
                   acr_gen1_rogue_score(acr_gen1_rogue_find(&w, a[SLOTS - 1][1].arena, a[SLOTS - 1][1].rank));
    EXPECT(lo < hi);
}

static void test_new(void) {
    const acr_gen1_rogue_world w = world();
    for (uint64_t seed = 1; seed < 60; ++seed) {
        EXPECT(acr_gen1_coop_new(&g_c, &w, seed, k_pilot, k_ac));
        EXPECT(g_c.wins == 0 && g_c.stash_n == 0 && acr_gen1_coop_ready(&g_c));
        for (int p = 0; p < 2; ++p) {
            const acr_gen1_rogue *r = &g_c.player[p];
            EXPECT(r->hangar.credits == 0 && acr_gen1_rogue_check(r, &g_cat) == 0);
            EXPECT(!strcmp(r->hangar.pilot_name, k_pilot[p]) && !strcmp(r->hangar.ac_name, k_ac[p]));
            const uint8_t arms = r->hangar.fitted[ACR_GEN1_SLOT_ARMS], hand = r->hangar.fitted[ACR_GEN1_SLOT_HAND];
            EXPECT(arms >= 12 || (hand != ACR_GEN1_NO_PART && g_info.attack[ACR_GEN1_CAT_HAND][hand] > 0));
        }
        acr_gen1_rogue_foe f;
        EXPECT(acr_gen1_coop_next_pair(&g_c) == g_c.pair[1]);
        EXPECT(acr_gen1_coop_rung(&g_c, &w, 1, 0, &f) && acr_gen1_coop_rung(&g_c, &w, 1, 1, &f));
    }
    /* Each player has their own build and their own prize screens. */
    EXPECT(acr_gen1_coop_new(&g_c, &w, 77, k_pilot, k_ac));
    EXPECT(g_c.player[0].seed != g_c.player[1].seed);
}

/* Wins (with prize screens passed) until player p holds at least `want` P. */
static void earn(const acr_gen1_rogue_world *w, int want) {
    (void)w;
    while (g_c.player[0].hangar.credits < want || g_c.player[1].hangar.credits < want) {
        acr_gen1_coop_won(&g_c, 0, 0);
        for (int p = 0; p < 2; ++p) if (acr_gen1_coop_prize_due(&g_c, p)) acr_gen1_rogue_pass(&g_c.player[p]);
    }
}

/* Player p's current prize screen: save the first affordable part for later. */
static int save_one(const acr_gen1_rogue_world *w, int p) {
    acr_gen1_rogue_offer o[3];
    const int n = acr_gen1_rogue_offers(&g_c.player[p], w, o);
    for (int i = 0; i < n; ++i) {
        if (o[i].kind != ACR_GEN1_OFFER_PART || (int32_t)o[i].cost > g_c.player[p].hangar.credits) continue;
        if (!acr_gen1_coop_stash_room(&g_c, &o[i]) || !acr_gen1_rogue_take(&g_c.player[p], w, &o[i], -1)) return 0;
        acr_gen1_coop_collect(&g_c, p);
        return 1;
    }
    return 0;
}

static void test_win_and_rejoin(void) {
    const acr_gen1_rogue_world w = world();
    EXPECT(acr_gen1_coop_new(&g_c, &w, 1234, k_pilot, k_ac));
    /* A win with player 2 destroyed: both score, player 2 chooses first. */
    acr_gen1_coop_won(&g_c, 0, 1);
    EXPECT(g_c.wins == 1 && g_c.player[0].hangar.credits == 1 && g_c.player[1].hangar.credits == 1);
    EXPECT(!g_c.rejoin[0] && g_c.rejoin[1]);
    EXPECT(acr_gen1_coop_prize_due(&g_c, 0) && !acr_gen1_coop_prize_due(&g_c, 1));
    EXPECT(!acr_gen1_coop_ready(&g_c));
    /* NEW RANDOM AC: free, a legal AC, P kept. */
    const uint8_t before[ACR_GEN1_SLOT_COUNT];
    memcpy((void *)before, g_c.player[1].hangar.fitted, sizeof before);
    EXPECT(acr_gen1_coop_rejoin(&g_c, &w, 1, 0));
    EXPECT(!g_c.rejoin[1] && g_c.player[1].hangar.credits == 1);
    EXPECT(acr_gen1_rogue_check(&g_c.player[1], &g_cat) == 0);
    EXPECT(memcmp(before, g_c.player[1].hangar.fitted, sizeof before) != 0);
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) {
        const uint8_t f = g_c.player[1].hangar.fitted[s];
        if (f != ACR_GEN1_NO_PART) EXPECT(g_c.player[1].hangar.owned[acr_gen1_slot_category((acr_gen1_slot)s)][f] >= 1);
    }
    EXPECT(acr_gen1_coop_prize_due(&g_c, 1));
    EXPECT(!acr_gen1_coop_rejoin(&g_c, &w, 1, 0));          /* only once */
    acr_gen1_rogue_pass(&g_c.player[0]);
    acr_gen1_rogue_pass(&g_c.player[1]);
    EXPECT(acr_gen1_coop_ready(&g_c));

    /* Both save parts into the stash, then player 1 is destroyed and keeps
     * the old AC: their parts and P go, player 2's stay. */
    earn(&w, 8);
    acr_gen1_coop_won(&g_c, 0, 0);
    EXPECT(save_one(&w, 0));
    EXPECT(save_one(&w, 1));
    EXPECT(g_c.stash_n == 2 && g_c.stash[0].owner == 0 && g_c.stash[1].owner == 1);
    EXPECT(acr_gen1_coop_ready(&g_c));
    const acr_gen1_coop_item mine = g_c.stash[0];
    const uint8_t mine_owned = g_c.player[0].hangar.owned[mine.cat][mine.index];
    const uint8_t fitted0[ACR_GEN1_SLOT_COUNT];
    memcpy((void *)fitted0, g_c.player[0].hangar.fitted, sizeof fitted0);
    acr_gen1_coop_won(&g_c, 1, 0);
    int parts = -1;
    int32_t pts = -1;
    acr_gen1_coop_keep_cost(&g_c, 0, &parts, &pts);
    EXPECT(parts == 1 && pts == g_c.player[0].hangar.credits && pts > 0);
    EXPECT(acr_gen1_coop_rejoin(&g_c, &w, 0, 1));
    EXPECT(g_c.player[0].hangar.credits == 0);
    EXPECT(!memcmp(fitted0, g_c.player[0].hangar.fitted, sizeof fitted0));   /* the old AC */
    EXPECT(g_c.stash_n == 1 && g_c.stash[0].owner == 1);
    EXPECT(g_c.player[0].hangar.owned[mine.cat][mine.index] == mine_owned - 1);
    /* With nothing to pay for, a prize screen still comes (PASS: +1 P). */
    EXPECT(acr_gen1_coop_prize_due(&g_c, 0));
    acr_gen1_rogue_pass(&g_c.player[0]);
    EXPECT(g_c.player[0].hangar.credits == 1);
}

static void test_stash_lock(void) {
    const acr_gen1_rogue_world w = world();
    int tried = 0;
    for (uint64_t seed = 300; seed < 340 && tried < 5; ++seed) {
        EXPECT(acr_gen1_coop_new(&g_c, &w, seed, k_pilot, k_ac));
        earn(&w, 8);
        acr_gen1_coop_won(&g_c, 0, 0);
        if (!save_one(&w, 0)) continue;
        acr_gen1_rogue_pass(&g_c.player[1]);
        /* Player 1 highlights it: player 2 can't highlight or take it. */
        EXPECT(acr_gen1_coop_hold(&g_c, 0, 0));
        EXPECT(!acr_gen1_coop_hold(&g_c, 1, 0));
        acr_gen1_rogue_offer o;
        EXPECT(!acr_gen1_coop_stash_fit(&g_c, &w, 1, 0, &o));
        EXPECT(!acr_gen1_coop_stash_take(&g_c, &w, 1, 0, 0));
        EXPECT(acr_gen1_coop_hold(&g_c, 0, -1));
        EXPECT(acr_gen1_coop_hold(&g_c, 1, 0));
        if (!acr_gen1_coop_stash_fit(&g_c, &w, 1, 0, &o)) continue;
        ++tried;
        /* Player 2 equips player 1's part: the copy changes hands. */
        const acr_gen1_coop_item it = g_c.stash[0];
        const uint8_t o0 = g_c.player[0].hangar.owned[it.cat][it.index], o1 = g_c.player[1].hangar.owned[it.cat][it.index];
        EXPECT(acr_gen1_coop_stash_take(&g_c, &w, 1, 0, 0));
        EXPECT(g_c.stash_n == 0 && g_c.hold[1] == -1);
        EXPECT(g_c.player[0].hangar.owned[it.cat][it.index] == o0 - 1);
        EXPECT(g_c.player[1].hangar.owned[it.cat][it.index] == o1 + 1);
        EXPECT(g_c.player[1].hangar.fitted[o.put_slot[0]] == it.index);
        EXPECT(acr_gen1_rogue_check(&g_c.player[1], &g_cat) == 0);
    }
    EXPECT(tried > 0);
}

static void test_retry_ghosts(void) {
    const acr_gen1_rogue_world w = world();
    EXPECT(acr_gen1_coop_new(&g_c, &w, 99, k_pilot, k_ac));
    acr_gen1_rogue_pick ladder[SLOTS][2];
    memcpy(ladder, g_c.pair, sizeof ladder);
    for (int k = 0; k < 5; ++k) {
        acr_gen1_coop_won(&g_c, 0, 0);
        for (int p = 0; p < 2; ++p) acr_gen1_rogue_pass(&g_c.player[p]);
    }
    uint8_t lost_build[2][ACR_GEN1_SLOT_COUNT];
    for (int p = 0; p < 2; ++p) memcpy(lost_build[p], g_c.player[p].hangar.fitted, sizeof lost_build[p]);
    acr_gen1_coop_lost(&g_c);
    EXPECT(g_c.over && !acr_gen1_coop_next_pair(&g_c) && !acr_gen1_coop_ready(&g_c));
    EXPECT(acr_gen1_coop_retry(&g_c, &w));
    EXPECT(!g_c.over && g_c.wins == 0 && g_c.attempt == 1 && g_c.best == 5 && g_c.ghosts == 2);
    for (int p = 0; p < 2; ++p) EXPECT(g_c.player[p].hangar.credits == 0 && acr_gen1_rogue_check(&g_c.player[p], &g_cat) == 0);
    /* The ghost pair holds rank 15 (rung 5); the old bottom pair left. */
    EXPECT(g_c.pair[5][0].kind == ACR_GEN1_ROGUE_GHOST && g_c.pair[5][1].kind == ACR_GEN1_ROGUE_GHOST);
    for (int i = 1; i < 5; ++i) EXPECT(!memcmp(g_c.pair[i], ladder[i + 1], sizeof g_c.pair[i]));
    for (int i = 6; i < SLOTS; ++i) EXPECT(!memcmp(g_c.pair[i], ladder[i], sizeof g_c.pair[i]));
    for (int p = 0; p < 2; ++p) {
        acr_gen1_rogue_foe g;
        EXPECT(acr_gen1_coop_rung(&g_c, &w, 5, p, &g));
        EXPECT(!memcmp(g.parts, lost_build[p], sizeof lost_build[p]) && !strcmp(g.ac, k_ac[p]) && g.rank == 15);
    }
    /* The run file keeps it all. */
    static char text[32768];
    const size_t len = acr_gen1_coop_write(&g_c, text, sizeof text);
    EXPECT(len > 0 && len < sizeof text);
    static acr_gen1_coop back;
    memset(&back, 0, sizeof back);
    for (int p = 0; p < 2; ++p) back.player[p].hangar = g_c.player[p].hangar;
    EXPECT(acr_gen1_coop_read(&back, text));
    EXPECT(back.seed == g_c.seed && back.attempt == g_c.attempt && back.best == g_c.best && back.ghosts == 2);
    EXPECT(!memcmp(back.pair, g_c.pair, sizeof g_c.pair));
    for (int p = 0; p < 2; ++p) {
        const int gi = g_c.pair[5][p].rank;
        EXPECT(!memcmp(&back.ghost[gi], &g_c.ghost[gi], sizeof back.ghost[0]));
        EXPECT(back.player[p].seed == g_c.player[p].seed);
    }
}

static void test_save_mid_run(void) {
    const acr_gen1_rogue_world w = world();
    EXPECT(acr_gen1_coop_new(&g_c, &w, 4242, k_pilot, k_ac));
    earn(&w, 8);
    acr_gen1_coop_won(&g_c, 1, 0);                           /* player 1's rejoin due, player 2's prize */
    EXPECT(save_one(&w, 1));
    g_c.stage = 39;
    static char text[32768];
    acr_gen1_coop_write(&g_c, text, sizeof text);
    static acr_gen1_coop back;
    memset(&back, 0, sizeof back);
    for (int p = 0; p < 2; ++p) back.player[p].hangar = g_c.player[p].hangar;
    EXPECT(acr_gen1_coop_read(&back, text));
    EXPECT(back.wins == g_c.wins && back.stage == 39 && back.rejoin[0] && !back.rejoin[1]);
    EXPECT(back.stash_n == 1 && back.stash[0].owner == 1 && back.stash[0].cat == g_c.stash[0].cat &&
           back.stash[0].index == g_c.stash[0].index);
    for (int p = 0; p < 2; ++p) {
        EXPECT(back.player[p].prize_wins == g_c.player[p].prize_wins && back.player[p].wins == g_c.wins);
        EXPECT(acr_gen1_coop_prize_due(&back, p) == acr_gen1_coop_prize_due(&g_c, p));
    }
    /* The same prize screen comes back after a resume. */
    acr_gen1_rogue_offer a[3], b[3];
    g_c.rejoin[0] = back.rejoin[0] = 0;
    const int na = acr_gen1_rogue_offers(&g_c.player[0], &w, a), nb = acr_gen1_rogue_offers(&back.player[0], &w, b);
    EXPECT(na == nb && na > 0 && !memcmp(a, b, sizeof a[0] * (size_t)na));
}

static void test_duel(void) {
    const acr_gen1_rogue_world w = world();
    EXPECT(acr_gen1_coop_new(&g_c, &w, 5, k_pilot, k_ac));
    for (int k = 0; k < SLOTS - 1; ++k) {
        EXPECT(acr_gen1_coop_next_pair(&g_c) != NULL);
        acr_gen1_coop_won(&g_c, 0, 0);
        for (int p = 0; p < 2; ++p) if (acr_gen1_coop_prize_due(&g_c, p)) acr_gen1_rogue_pass(&g_c.player[p]);
    }
    /* Rank 1's pair beaten: no prize at the top, the duel is next. */
    EXPECT(g_c.wins == SLOTS - 1 && g_c.duel_due && !g_c.cleared && !acr_gen1_coop_next_pair(&g_c));
    EXPECT(!acr_gen1_coop_prize_due(&g_c, 0) && !acr_gen1_coop_prize_due(&g_c, 1));
    EXPECT(acr_gen1_coop_rank(&g_c, 0) == 1 && acr_gen1_coop_rank(&g_c, 1) == 1);
    acr_gen1_coop_duel(&g_c, 1);
    EXPECT(g_c.cleared && !g_c.duel_due && g_c.duel_winner == 1);
    EXPECT(acr_gen1_coop_rank(&g_c, 1) == 1 && acr_gen1_coop_rank(&g_c, 0) == 2);
    acr_gen1_coop_lost(&g_c);                                /* nothing after it's decided */
    EXPECT(!g_c.over);
}

int main(void) {
    make_catalog();
    make_roster();
    test_pairs();
    test_new();
    test_win_and_rejoin();
    test_stash_lock();
    test_retry_ghosts();
    test_save_mid_run();
    test_duel();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("rogue arena co-op: all passed\n");
    return 0;
}
