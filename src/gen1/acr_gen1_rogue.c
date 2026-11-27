/* Rogue Arena. See acr_gen1_rogue.h; the rules follow AC1Recomp's
 * moa_arena_run.c (ladder, starting AC, ghosts) and moa_arena_run_boot.c
 * (prices in P, the prize screen, SWAP PARTS), scaled from 49 + 1 to 19 + 1
 * rungs where a rule counts rungs. */
#include "acr_gen1_rogue.h"

#include <string.h>

#define SLOTS ACR_GEN1_ROGUE_SLOTS
#define NEED  (SLOTS - 1)              /* opponents on the ladder */

/* Scaled from AC1Recomp's 49-opponent ladder (user 2026-11-19: 19 + 1, no
 * balance changes): KARASAWA users in the top 25 -> 10, Human PLUS users in
 * the top 20 -> 8, the strictly ordered top 21 -> 8. */
#define KARASAWA_TOP 10
#define PLUS_TOP     8
#define LINEUP_TIGHT 8
#define LINEUP_NOISE 25                /* score points of random lift at the bottom */

/* ---- seed ---- */

uint64_t acr_gen1_rogue_next(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

uint32_t acr_gen1_rogue_below(uint64_t *s, uint32_t n) {
    return n ? (uint32_t)(acr_gen1_rogue_next(s) % n) : 0;
}

uint64_t acr_gen1_rogue_seed_from_text(const char *t) {
    if (!t || !*t) return 0;
    int hex = 1;
    size_t len = 0;
    for (const char *c = t; *c; ++c, ++len)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F'))) hex = 0;
    if (hex && len <= 16) {
        uint64_t v = 0;
        for (const char *c = t; *c; ++c)
            v = v * 16u + (uint64_t)(*c <= '9' ? *c - '0' : (*c | 0x20) - 'a' + 10);
        return v;
    }
    uint64_t v = 0xCBF29CE484222325ull;
    for (const char *c = t; *c; ++c) v = (v ^ (uint8_t)*c) * 0x100000001B3ull;   /* FNV-1a */
    return v;
}

/* ---- prices ---- */

/* Credits to P by where the price sits among all parts (USA disc): bottom
 * 15% 1 P, to 30% 2, to 50% 3, to 70% 4, to 85% 5, to 95% 6, top 7. */
uint32_t acr_gen1_rogue_price(uint32_t credits) {
    static const uint32_t k_at[6] = { 23000u, 32000u, 48000u, 67000u, 89000u, 138000u };
    if (credits <= 7u) return credits;            /* already in P (or free) */
    uint32_t p = 1;
    for (int i = 0; i < 6; ++i) if (credits >= k_at[i]) ++p;
    return p;
}

/* ---- the roster ---- */

/* Difficulty bands by arena (AC1Recomp k_arena_band). */
enum { BAND_LEG_EX, BAND_LATER_EX, BAND_MASTER, BAND_MAIN };

int acr_gen1_rogue_score(const acr_gen1_rogue_foe *f) {
    const int n = f->entries > 1 ? f->entries : 2;
    const int rank = f->rank ? f->rank - 1 : 0;      /* 0 = top */
    const int up = n - 1 - rank;
    int base = 8, width = 70;                       /* main arena */
    if (f->arena >= 1 && f->arena <= 4) { base = 30; width = 30; }
    else if (f->arena >= 5 && f->arena <= 7) { base = 50 + 8 * (f->arena - 5); width = 20; }
    else if (f->arena == 8) { base = 72; width = 25; }
    const int s = base + width * up / (n - 1);
    return s < 0 ? 0 : s > 100 ? 100 : s;
}

const acr_gen1_rogue_foe *acr_gen1_rogue_find(const acr_gen1_rogue_world *w, uint8_t arena, uint8_t rank) {
    for (int i = 0; i < w->foes; ++i)
        if (w->foe[i].arena == arena && w->foe[i].rank == rank) return &w->foe[i];
    return NULL;
}

typedef struct cand { int foe, score; uint32_t jitter; } cand;

static void sort_cands(cand *c, int n, int by_jitter) {
    for (int i = 1; i < n; ++i) {                     /* insertion sort: small lists */
        const cand x = c[i];
        int k = i - 1;
        while (k >= 0 && (by_jitter ? c[k].jitter > x.jitter
                                    : (c[k].score > x.score || (c[k].score == x.score && c[k].jitter > x.jitter)))) {
            c[k + 1] = c[k];
            --k;
        }
        c[k + 1] = x;
    }
}

/* Keep opponents with `trait` out of the bottom: one drawn below the top
 * `top` rungs is replaced, in its place, by the unused candidate without
 * the trait (nor KARASAWA) closest in score, the easier one on a tie. */
static int foe_plus(const acr_gen1_rogue_foe *f) { return f->ai.rebel > 0; }

static void keep_top(const acr_gen1_rogue_world *w, cand *chosen, int need, cand *rest, int nrest,
                     uint8_t *taken, int top, int (*trait)(const acr_gen1_rogue_foe *)) {
    const int below = need > top ? need - top : 0;
    for (int b = 0; b < below; ++b) {
        if (!trait(&w->foe[chosen[b].foe])) continue;
        int best = -1, gap = 0;
        for (int i = 0; i < nrest; ++i) {
            const acr_gen1_rogue_foe *f = &w->foe[rest[i].foe];
            if (taken[i] || f->karasawa || trait(f)) continue;
            const int d = rest[i].score - chosen[b].score;
            const int g = d < 0 ? -2 * d + 1 : 2 * d;
            if (best < 0 || g < gap) { best = i; gap = g; }
        }
        if (best < 0) continue;
        taken[best] = 1;
        chosen[b] = rest[best];
    }
}

static int foe_karasawa(const acr_gen1_rogue_foe *f) { return f->karasawa; }

/* Draws `rungs * per` opponents on the ladder's difficulty curve, easiest
 * first, into `drawn` (roster indices); `per` opponents share a rung (1 solo,
 * 2 co-op pairs), so every rule that counts rungs counts them `per` times.
 * Returns the opponents drawn. */
static int draw_lineup(const acr_gen1_rogue_world *w, uint64_t rng, int rungs, int per, int *drawn) {
    static cand rest[ACR_GEN1_ROGUE_ROSTER];
    static uint8_t taken[ACR_GEN1_ROGUE_ROSTER];
    int nrest = 0;
    for (int i = 0; i < w->foes && nrest < ACR_GEN1_ROGUE_ROSTER; ++i) {
        const acr_gen1_rogue_foe *f = &w->foe[i];
        /* Main arena rank 1, Hustler One / Nine-Ball, is fought in the
         * story, never in the arena: not in the run. */
        if (f->arena == 0 && f->rank == 1) continue;
        /* Master of Arena's arenas only (0-8), at parity with AC1Recomp's run:
         * Project Phantasma's arena (9, the main Arena) isn't drawn. */
        if (f->arena >= ACR_GEN1_ROGUE_ARENAS) continue;
        rest[nrest].foe = i;
        rest[nrest].score = acr_gen1_rogue_score(f);
        rest[nrest].jitter = (uint32_t)acr_gen1_rogue_next(&rng);
        ++nrest;
    }
    memset(taken, 0, sizeof taken);
    int need = rungs * per < nrest ? rungs * per : nrest;
    sort_cands(rest, nrest, 0);
    /* The sorted roster cut into `need` buckets; each rung draws from a
     * window around its bucket, 6 rungs wide at the bottom down to 1 at
     * the top, so low ranks vary between runs while the top stays tight. */
    cand chosen[2 * SLOTS];
    for (int b = 0; b < need; ++b) {
        const double wd = need > 1 ? per * (6.0 - 5.0 * b / (need - 1)) : 1.0;
        int lo = (int)((double)nrest * (b + 0.5 - wd / 2) / need);
        int hi = (int)((double)nrest * (b + 0.5 + wd / 2) / need + 0.999);
        if (lo < 0) lo = 0;
        if (hi > nrest) hi = nrest;
        int free_n = 0;
        for (int i = lo; i < hi; ++i) free_n += !taken[i];
        int pick = -1;
        if (free_n) {
            int t = (int)acr_gen1_rogue_below(&rng, (uint32_t)free_n);
            for (int i = lo; i < hi; ++i) if (!taken[i] && t-- == 0) { pick = i; break; }
        } else {
            for (int i = 0; i < nrest; ++i) if (!taken[i]) { pick = i; break; }
        }
        if (pick < 0) { need = b; break; }
        taken[pick] = 1;
        chosen[b] = rest[pick];
    }
    sort_cands(chosen, need, 0);
    /* Loose order below the top: each pick below the top LINEUP_TIGHT gets
     * a random lift of up to LINEUP_NOISE score points (the most at the
     * bottom), so the first opponent isn't always one of the same few. */
    {
        const int loose = need > LINEUP_TIGHT * per ? need - LINEUP_TIGHT * per : 0;
        for (int b = 0; b < need; ++b) {
            const int j = b < loose ? LINEUP_NOISE * (loose - b) / loose : 0;
            chosen[b].jitter = (uint32_t)chosen[b].score * 1000u +
                               (j ? acr_gen1_rogue_below(&rng, (uint32_t)j * 1000u) : 0u);
        }
        sort_cands(chosen, need, 1);
    }
    keep_top(w, chosen, need, rest, nrest, taken, KARASAWA_TOP * per, foe_karasawa);
    keep_top(w, chosen, need, rest, nrest, taken, PLUS_TOP * per, foe_plus);
    for (int b = 0; b < need; ++b) drawn[b] = chosen[b].foe;
    return need;
}

int acr_gen1_rogue_lineup(const acr_gen1_rogue_world *w, uint64_t seed, acr_gen1_rogue_pick out[SLOTS]) {
    int pick[NEED];
    const int need = draw_lineup(w, seed ^ 0x4C494E45ull, NEED, 1, pick);   /* own stream: "LINE" */
    int filled = 0;
    out[filled].kind = ACR_GEN1_ROGUE_PLAYER;
    out[filled].arena = out[filled].rank = 0;
    ++filled;
    for (int b = 0; b < need; ++b, ++filled) {
        out[filled].kind = ACR_GEN1_ROGUE_FOE;
        out[filled].arena = w->foe[pick[b]].arena;
        out[filled].rank = w->foe[pick[b]].rank;
    }
    return filled;
}

int acr_gen1_rogue_lineup_pairs(const acr_gen1_rogue_world *w, uint64_t seed,
                                acr_gen1_rogue_pick out[SLOTS][2]) {
    int pick[2 * NEED];
    const int got = draw_lineup(w, seed ^ 0x50414952ull, NEED, 2, pick);    /* own stream: "PAIR" */
    memset(out, 0, sizeof(acr_gen1_rogue_pick) * SLOTS * 2);
    out[0][0].kind = out[0][1].kind = ACR_GEN1_ROGUE_PLAYER;
    const int rungs = got / 2;
    for (int r = 0; r < rungs; ++r)
        for (int k = 0; k < 2; ++k) {
            /* Easiest first: rung r gets the (2r)th and (2r+1)th. */
            const acr_gen1_rogue_foe *f = &w->foe[pick[2 * r + k]];
            out[r + 1][k].kind = ACR_GEN1_ROGUE_FOE;
            out[r + 1][k].arena = f->arena;
            out[r + 1][k].rank = f->rank;
        }
    return rungs + 1;
}

/* ---- builds ---- */

static const acr_gen1_catalog_part *part_of(const acr_gen1_catalog *c, int cat, unsigned i) {
    return i < ACR_GEN1_PART_MAX && c->part[cat][i].present ? &c->part[cat][i] : NULL;
}

static unsigned check_fitted(const acr_gen1_catalog *c, const uint8_t fitted[ACR_GEN1_SLOT_COUNT],
                             uint16_t options, int plus, int opts) {
    acr_gen1_hangar h;
    memset(&h, 0, sizeof h);
    memcpy(h.fitted, fitted, sizeof h.fitted);
    h.options = options;
    unsigned f = acr_gen1_garage_check(&h, c, NULL);
    if (plus) f &= ~(ACR_GEN1_FAIL_ENERGY | ACR_GEN1_FAIL_ARM_LOAD | ACR_GEN1_FAIL_WEIGHT);
    if (opts) f &= ~ACR_GEN1_FAIL_OPTION_SLOTS;
    return f;
}

unsigned acr_gen1_rogue_check(const acr_gen1_rogue *r, const acr_gen1_catalog *c) {
    return check_fitted(c, r->hangar.fitted, r->hangar.options, r->plus, r->opts);
}

static int weapon_arms(const acr_gen1_catalog *c, unsigned arms) {
    const acr_gen1_catalog_part *p = part_of(c, ACR_GEN1_CAT_ARMS, arms);
    return p && (p->flags & ACR_GEN1_FIT_ARM_WEAPON);
}

int acr_gen1_rogue_start_ac(const acr_gen1_rogue_world *w, uint64_t seed, uint8_t out[ACR_GEN1_SLOT_COUNT]) {
    uint64_t rng = seed ^ 0x53544152ull;              /* own stream: "STAR" */
    const acr_gen1_catalog *c = w->cat;
    uint8_t list[ACR_GEN1_PART_MAX];
    for (int tries = 0; tries < 20000; ++tries) {
        uint8_t f[ACR_GEN1_SLOT_COUNT];
        for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) {
            const int cat = acr_gen1_slot_category((acr_gen1_slot)s);
            f[s] = ACR_GEN1_NO_PART;
            int n = 0;
            for (unsigned i = 0; i < ACR_GEN1_PART_MAX; ++i) if (c->part[cat][i].present) list[n++] = (uint8_t)i;
            if (!n) continue;
            /* Weapons may be left empty; the frame parts never are. */
            if ((cat == ACR_GEN1_CAT_HAND || cat == ACR_GEN1_CAT_BLADE || cat == ACR_GEN1_CAT_BACK) &&
                acr_gen1_rogue_below(&rng, 4) == 0)
                continue;
            f[s] = list[acr_gen1_rogue_below(&rng, (uint32_t)n)];
        }
        /* The garage's rules on a loaded build (two-slot units, weapon arms, caterpillars). */
        acr_gen1_hangar h;
        memset(&h, 0, sizeof h);
        memcpy(h.fitted, f, sizeof f);
        acr_gen1_fit_build b;
        acr_gen1_garage_build(&h, c, &b);
        const uint32_t off = acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_COUNT);
        for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) if (off & (1u << s)) f[s] = ACR_GEN1_NO_PART;
        /* A real main gun: a hand weapon or weapon arms with attack power. */
        const unsigned gun_cat = weapon_arms(c, f[ACR_GEN1_SLOT_ARMS]) ? ACR_GEN1_CAT_ARMS : ACR_GEN1_CAT_HAND;
        const unsigned gun = gun_cat == ACR_GEN1_CAT_ARMS ? f[ACR_GEN1_SLOT_ARMS] : f[ACR_GEN1_SLOT_HAND];
        const int armed = gun < ACR_GEN1_PART_MAX && w->info && w->info->attack[gun_cat][gun] > 0;
        if (armed && !check_fitted(c, f, 0, 0, 0)) {
            memcpy(out, f, sizeof f);
            return 1;
        }
    }
    return 0;
}

void acr_gen1_rogue_paint(uint64_t seed, uint8_t attempt, uint8_t col[4][3]) {
    uint64_t k = seed ^ 0xC0105EEDull ^ (0x9E3779B97F4A7C15ull * (uint64_t)(attempt + 1));
    uint8_t c[12];
    for (int i = 0; i < 6; ++i) c[i] = (uint8_t)(10 + acr_gen1_rogue_below(&k, 47));   /* base, optional */
    for (int i = 0; i < 3; ++i) c[6 + i] = (uint8_t)(c[i] * 3 / 5);                   /* detail: darker base */
    const uint8_t j = (uint8_t)(8 + acr_gen1_rogue_below(&k, 14));                      /* joint: dark grey */
    c[9] = c[10] = c[11] = j;
    for (int i = 0; i < 12; ++i) col[i / 3][i % 3] = c[i];
}

static void copy_name(char *dst, const char *src) {
    size_t i = 0;
    for (; src && src[i] && i + 1 < ACR_GEN1_ROGUE_NAME; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static int begin_attempt(acr_gen1_rogue *r, const acr_gen1_rogue_world *w, uint64_t ac_seed) {
    uint8_t start[ACR_GEN1_SLOT_COUNT];
    if (!acr_gen1_rogue_start_ac(w, ac_seed, start)) return 0;
    char pilot[ACR_GEN1_ROGUE_NAME], ac[ACR_GEN1_ROGUE_NAME];
    copy_name(pilot, r->hangar.pilot_name);
    copy_name(ac, r->hangar.ac_name);
    acr_gen1_hangar_new(&r->hangar, start, 0, 0);
    copy_name(r->hangar.pilot_name, pilot);
    copy_name(r->hangar.ac_name, ac);
    acr_gen1_rogue_paint(r->seed, r->attempt, r->hangar.colours);
    r->wins = 0; r->over = 0; r->cleared = 0; r->prize_wins = 0; r->reroll_win = 0;
    r->income = 0; r->plus = 0; r->opts = 0; r->points = 0; r->spent = 0; r->saved_n = 0;
    return 1;
}

int acr_gen1_rogue_new(acr_gen1_rogue *r, const acr_gen1_rogue_world *w, uint64_t seed,
                       const char *pilot, const char *ac) {
    memset(r, 0, sizeof *r);
    r->seed = seed;
    copy_name(r->hangar.pilot_name, pilot);
    copy_name(r->hangar.ac_name, ac);
    if (acr_gen1_rogue_lineup(w, seed, r->slot) != SLOTS) return 0;
    return begin_attempt(r, w, seed);
}

/* The ghost goes in at rank `held`: the opponent there and every one below
 * it move down one, and the bottom opponent (rank 19) leaves the ladder. */
static void add_ghost(acr_gen1_rogue *r, int held) {
    int g = 0;
    while (g < SLOTS && r->ghost[g].used) ++g;
    if (g == SLOTS || held < 1 || held > SLOTS - 1) return;
    const acr_gen1_rogue_pick out = r->slot[1];
    if (out.kind == ACR_GEN1_ROGUE_GHOST && out.rank < SLOTS) {
        r->ghost[out.rank].used = 0;                 /* pushed off the bottom */
        --r->ghosts;
        if (out.rank < g) g = out.rank;
    }
    const int at = SLOTS - held;                     /* 1..19 */
    const acr_gen1_rogue_pick displaced = r->slot[at];
    const acr_gen1_rogue_pick ai = displaced.kind == ACR_GEN1_ROGUE_GHOST && displaced.rank < SLOTS
                                 ? r->ghost[displaced.rank].ai_from : displaced;
    memmove(&r->slot[1], &r->slot[2], (size_t)(at - 1) * sizeof r->slot[0]);
    acr_gen1_rogue_ghost *gh = &r->ghost[g];
    memset(gh, 0, sizeof *gh);
    /* The exact losing build, prize parts included (user, 2026-11-17). */
    memcpy(gh->parts, r->hangar.fitted, sizeof gh->parts);
    gh->options = r->hangar.options;
    gh->plus = r->plus;
    memcpy(gh->colours, r->hangar.colours, sizeof gh->colours);
    copy_name(gh->pilot, r->hangar.pilot_name);
    copy_name(gh->ac, r->hangar.ac_name);
    gh->used = 1;
    gh->attempt = r->attempt;
    gh->rank_held = (uint8_t)held;
    gh->ai_from = ai;
    r->slot[at].kind = ACR_GEN1_ROGUE_GHOST;
    r->slot[at].arena = 0;
    r->slot[at].rank = (uint8_t)g;
    ++r->ghosts;
}

int acr_gen1_rogue_retry(acr_gen1_rogue *r, const acr_gen1_rogue_world *w) {
    if (!r->over) return 0;
    if (r->wins > 0) add_ghost(r, SLOTS - r->wins);
    ++r->attempt;
    /* A new build each attempt: the seed and the attempt number. */
    uint64_t k = r->seed ^ (0x9E3779B97F4A7C15ull * (uint64_t)r->attempt);
    return begin_attempt(r, w, acr_gen1_rogue_next(&k));
}

const acr_gen1_rogue_pick *acr_gen1_rogue_next_pick(const acr_gen1_rogue *r) {
    if (r->cleared || r->wins + 1 >= SLOTS) return NULL;
    return &r->slot[r->wins + 1];
}

int acr_gen1_rogue_rung(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w, const acr_gen1_rogue_pick *p,
                        acr_gen1_rogue_foe *out) {
    if (p->kind == ACR_GEN1_ROGUE_FOE) {
        const acr_gen1_rogue_foe *f = acr_gen1_rogue_find(w, p->arena, p->rank);
        if (!f) return 0;
        *out = *f;
        return 1;
    }
    if (p->kind != ACR_GEN1_ROGUE_GHOST || p->rank >= SLOTS || !r->ghost[p->rank].used) return 0;
    const acr_gen1_rogue_ghost *g = &r->ghost[p->rank];
    memset(out, 0, sizeof *out);
    const acr_gen1_rogue_foe *src = acr_gen1_rogue_find(w, g->ai_from.arena, g->ai_from.rank);
    if (src) { out->ai = src->ai; out->arena = src->arena; out->entries = src->entries; }
    else acr_gen1_ai_preset(&out->ai, ACR_GEN1_AI_BALANCED);
    out->ai.rebel = g->plus ? 9 : 0;
    out->rank = (uint8_t)(SLOTS - (p - r->slot));
    memcpy(out->parts, g->parts, sizeof out->parts);
    out->options = g->options;
    memcpy(out->colours, g->colours, sizeof out->colours);
    copy_name(out->pilot, g->pilot);
    copy_name(out->ac, g->ac);
    return 1;
}

void acr_gen1_rogue_won(acr_gen1_rogue *r) {
    if (r->over || r->cleared || r->wins >= SLOTS - 1) return;
    ++r->wins;
    const unsigned p = 1u + r->income;
    r->points = (uint16_t)(r->points + p);
    r->hangar.credits += (int32_t)p;
    if (r->wins > r->best) r->best = r->wins;
    if (r->wins >= SLOTS - 1) { r->cleared = 1; r->prize_wins = r->wins; }   /* the top: no prize to spend */
}

void acr_gen1_rogue_lost(acr_gen1_rogue *r) {
    r->over = 1;
}

int acr_gen1_rogue_belt(int wins) {
    if (wins < 0) wins = 0;
    if (wins > NEED) wins = NEED;
    return wins * 10 / NEED;
}

const char *acr_gen1_rogue_belt_name(int belt) {
    static const char *const k[11] = { "WHITE", "YELLOW", "ORANGE", "GREEN", "BLUE", "PURPLE",
                                       "RED", "BROWN", "BLACK", "BLACK-RED", "BLACK-GOLD" };
    return belt >= 0 && belt < 11 ? k[belt] : "";
}

/* ---- prize screen ---- */

#define W_NORMAL       1000u
#define W_STANCE        400u            /* a crouch weapon, two-legged AC */
#define W_STANCE_TWO     80u            /* ... that would make two */
#define HPLUS_ODDS       13u
#define OPTS_ODDS        13u
#define INCOME_ODDS       4u
#define INCOME_MAX        3u
#define SET_PART_MAX      4u
#define OPTS_MASK    0x0FFFu            /* the 12 options the shop sells */

int acr_gen1_rogue_prize_due(const acr_gen1_rogue *r) {
    return !r->over && !r->cleared && r->prize_wins < r->wins;
}

int acr_gen1_rogue_can_reroll(const acr_gen1_rogue *r) {
    return acr_gen1_rogue_prize_due(r) && r->reroll_win != r->prize_wins + 1u && r->hangar.credits >= 1;
}

static int leg_type(unsigned legs) { return legs < 14 ? 0 : legs < 21 ? 1 : legs < 27 ? 2 : 3; }
static int two_legged(unsigned legs) { return legs != ACR_GEN1_NO_PART && leg_type(legs) <= 1; }

static int stance_n(const acr_gen1_rogue_world *w, const uint8_t f[ACR_GEN1_SLOT_COUNT]) {
    int n = 0;
    for (int s = ACR_GEN1_SLOT_BACK_L; s <= ACR_GEN1_SLOT_BACK_R; ++s)
        if (f[s] < ACR_GEN1_PART_MAX && w->info && (w->info->flags[ACR_GEN1_CAT_BACK][f[s]] & ACR_GEN1_ROGUE_STANCE)) ++n;
    return n;
}

static uint32_t part_p(const acr_gen1_catalog *c, int cat, unsigned i) {
    const acr_gen1_catalog_part *p = part_of(c, cat, i);
    return p ? acr_gen1_rogue_price(p->price) : 0;
}

/* Where part (cat, index) may go into the run's AC: each place's AC and
 * what it throws away. Nothing but the part it replaces may come off
 * (weapon arms also take the hand weapon and blade, caterpillars the
 * booster, a two-slot back unit both back slots). `owned`: the run already
 * has the copy (SWAP PARTS). 1 = fits somewhere. */
static int offer_fit(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w, int cat, unsigned index,
                     int owned, acr_gen1_rogue_offer *o) {
    const acr_gen1_catalog *c = w->cat;
    const uint8_t *now = r->hangar.fitted;
    int slots[2] = { -1, -1 };
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s)
        if ((int)acr_gen1_slot_category((acr_gen1_slot)s) == cat) { if (slots[0] < 0) slots[0] = s; else slots[1] = s; }
    o->nput = 0;
    int free_at = -1;
    for (int k = 0; k < 2 && slots[k] >= 0; ++k) {
        const int s = slots[k];
        acr_gen1_hangar t = r->hangar;
        if (!owned && t.owned[cat][index] < ACR_GEN1_OWN_MAX) ++t.owned[cat][index];
        uint32_t removed = 0;
        if (acr_gen1_garage_fit(&t, c, (acr_gen1_slot)s, index, &removed) != ACR_GEN1_OK) continue;
        uint32_t allowed = 1u << s;
        const acr_gen1_catalog_part *np = part_of(c, cat, index);
        if (cat == ACR_GEN1_CAT_BACK) {
            const int other = s == ACR_GEN1_SLOT_BACK_L ? ACR_GEN1_SLOT_BACK_R : ACR_GEN1_SLOT_BACK_L;
            const acr_gen1_catalog_part *op = part_of(c, ACR_GEN1_CAT_BACK, now[other]);
            if ((np && (np->flags & ACR_GEN1_FIT_TWO_SLOT)) || (op && (op->flags & ACR_GEN1_FIT_TWO_SLOT)))
                allowed |= 1u << other;
        }
        if (cat == ACR_GEN1_CAT_ARMS && np && (np->flags & ACR_GEN1_FIT_ARM_WEAPON))
            allowed |= 1u << ACR_GEN1_SLOT_HAND | 1u << ACR_GEN1_SLOT_BLADE;
        if (cat == ACR_GEN1_CAT_LEGS && np && (np->flags & ACR_GEN1_FIT_CATERPILLAR))
            allowed |= 1u << ACR_GEN1_SLOT_BOOSTER;
        uint32_t lost = 0;
        int bad = 0;
        for (int b = 0; b < ACR_GEN1_SLOT_COUNT && !bad; ++b) {
            if (t.fitted[b] == now[b] || now[b] == ACR_GEN1_NO_PART) continue;
            if (!(allowed & (1u << b))) bad = 1;
            else lost |= 1u << b;
        }
        if (bad) continue;
        if (check_fitted(c, t.fitted, t.options, r->plus, r->opts)) continue;
        int dup = 0;                                   /* the same AC either way: one place */
        for (int q = 0; q < o->nput; ++q) dup |= !memcmp(o->put_fitted[q], t.fitted, sizeof t.fitted);
        if (dup) continue;
        const int n = o->nput++;
        o->put_slot[n] = (uint8_t)s;
        memcpy(o->put_fitted[n], t.fitted, sizeof t.fitted);
        o->put_lost[n] = lost;
        if (!lost && free_at < 0) free_at = n;
    }
    if (free_at > 0) {                                 /* an empty back slot: no need to ask */
        o->put_slot[0] = o->put_slot[free_at];
        memcpy(o->put_fitted[0], o->put_fitted[free_at], sizeof o->put_fitted[0]);
        o->put_lost[0] = 0;
    }
    if (free_at >= 0) o->nput = 1;
    return o->nput > 0;
}

/* The ARMS SET for an AC on weapon arms: regular arms + hand weapon + blade,
 * none owned, each 4 P or less, fitting together. 1 = made. */
static int set_roll(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w, uint64_t *rng, acr_gen1_rogue_offer *o) {
    const acr_gen1_catalog *c = w->cat;
    static const int k_cat[3] = { ACR_GEN1_CAT_ARMS, ACR_GEN1_CAT_HAND, ACR_GEN1_CAT_BLADE };
    uint8_t ids[3][ACR_GEN1_PART_MAX];
    unsigned n[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; ++k)
        for (unsigned i = 0; i < ACR_GEN1_PART_MAX; ++i) {
            const uint32_t p = part_p(c, k_cat[k], i);
            if (!p || p > SET_PART_MAX || r->hangar.owned[k_cat[k]][i]) continue;
            if (k_cat[k] == ACR_GEN1_CAT_ARMS && weapon_arms(c, i)) continue;
            ids[k][n[k]++] = (uint8_t)i;
        }
    if (!n[0] || !n[1] || !n[2]) return 0;
    const uint8_t *now = r->hangar.fitted;
    for (int t = 0; t < 64; ++t) {
        uint8_t f[ACR_GEN1_SLOT_COUNT];
        memcpy(f, now, sizeof f);
        f[ACR_GEN1_SLOT_ARMS] = ids[0][acr_gen1_rogue_below(rng, n[0])];
        f[ACR_GEN1_SLOT_HAND] = ids[1][acr_gen1_rogue_below(rng, n[1])];
        f[ACR_GEN1_SLOT_BLADE] = ids[2][acr_gen1_rogue_below(rng, n[2])];
        acr_gen1_hangar h;
        memset(&h, 0, sizeof h);
        memcpy(h.fitted, f, sizeof f);
        acr_gen1_fit_build b;
        acr_gen1_garage_build(&h, c, &b);
        const uint32_t off = acr_gen1_fit_repair(&b, ACR_GEN1_SLOT_ARMS);
        if (off & (1u << ACR_GEN1_SLOT_ARMS | 1u << ACR_GEN1_SLOT_HAND | 1u << ACR_GEN1_SLOT_BLADE)) continue;
        if (off) continue;                             /* something else would come off */
        if (check_fitted(c, f, r->hangar.options, r->plus, r->opts)) continue;
        memset(o, 0, sizeof *o);
        o->kind = ACR_GEN1_OFFER_SET;
        o->cat = ACR_GEN1_CAT_ARMS; o->index = f[ACR_GEN1_SLOT_ARMS];
        o->cost = ACR_GEN1_ROGUE_SET_COST; o->band = 2;
        o->xn = 2;
        o->xcat[0] = ACR_GEN1_CAT_HAND; o->xindex[0] = f[ACR_GEN1_SLOT_HAND];
        o->xcat[1] = ACR_GEN1_CAT_BLADE; o->xindex[1] = f[ACR_GEN1_SLOT_BLADE];
        o->nput = 1; o->put_slot[0] = ACR_GEN1_SLOT_ARMS;
        memcpy(o->put_fitted[0], f, sizeof f);
        o->put_lost[0] = 1u << ACR_GEN1_SLOT_ARMS;
        o->weight = W_NORMAL;
        return 1;
    }
    return 0;
}

static uint64_t prize_rng(const acr_gen1_rogue *r, unsigned win, uint64_t salt) {
    return r->seed ^ (0x5052495A32ull * (win + 1u)) ^ salt ^ (0x9E3779B97F4A7C15ull * (uint64_t)(r->attempt + 1u));
}

static int popcount4(unsigned v) { int n = 0; for (; v; v &= v - 1) ++n; return n; }

/* One draw of the offers; `excl` (n pairs cat, index) are left out (a
 * re-roll leaves out what the first draw showed). */
static int draw_offers(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w, unsigned win, uint64_t salt,
                       const uint8_t (*excl)[2], int nexcl, acr_gen1_rogue_offer out[3]) {
    const acr_gen1_catalog *c = w->cat;
    const uint8_t *now = r->hangar.fitted;
    const int warms = weapon_arms(c, now[ACR_GEN1_SLOT_ARMS]);
    const int two = two_legged(now[ACR_GEN1_SLOT_LEGS]);
    const int stance_now = stance_n(w, now);
    static acr_gen1_rogue_offer cand[3][128];
    unsigned nc[3] = { 0, 0, 0 };
    int n_out = 0;
    for (int cat = 0; cat < ACR_GEN1_CAT_COUNT; ++cat) {
        if (cat == ACR_GEN1_CAT_OPTION) continue;      /* options come only as ALL OPTIONS */
        for (unsigned i = 0; i < ACR_GEN1_PART_MAX; ++i) {
            const uint32_t p = part_p(c, cat, i);
            if (!p || p > 7u || r->hangar.owned[cat][i]) continue;
            const uint8_t fl = w->info ? w->info->flags[cat][i] : 0;
            if (cat == ACR_GEN1_CAT_BACK && (fl & (ACR_GEN1_ROGUE_RADAR | ACR_GEN1_ROGUE_STEALTH))) continue;
            if (cat == ACR_GEN1_CAT_ARMS && warms && !weapon_arms(c, i)) continue;   /* the ARMS SET instead */
            int skip = 0;
            for (int e = 0; e < nexcl && !skip; ++e) skip = excl[e][0] == cat && excl[e][1] == i;
            if (skip) continue;
            const unsigned band = p <= 2u ? 0u : p <= 4u ? 1u : 2u;
            if (nc[band] >= 128u) continue;
            acr_gen1_rogue_offer *o = &cand[band][nc[band]];
            memset(o, 0, sizeof *o);
            o->kind = ACR_GEN1_OFFER_PART;
            o->cat = (uint8_t)cat; o->index = (uint8_t)i; o->cost = (uint8_t)p; o->band = (uint8_t)band;
            if (!offer_fit(r, w, cat, i, 0, o)) continue;
            o->weight = W_NORMAL;
            if (cat == ACR_GEN1_CAT_BACK && two && (fl & ACR_GEN1_ROGUE_STANCE)) {
                int two_st = 0;
                for (int k = 0; k < o->nput; ++k) two_st |= stance_n(w, o->put_fitted[k]) >= 2;
                o->weight = two_st ? W_STANCE_TWO : W_STANCE;
            }
            if (cat == ACR_GEN1_CAT_LEGS && two_legged(i) && stance_now >= 2) o->weight /= 4u;
            ++nc[band];
        }
    }
    /* Legs: one leg type for this screen, each type as likely, then its
     * parts take all the legs' share. */
    uint64_t rng = prize_rng(r, win, salt);
    unsigned have = 0;
    for (unsigned b = 0; b < 3u; ++b)
        for (unsigned i = 0; i < nc[b]; ++i)
            if (cand[b][i].cat == ACR_GEN1_CAT_LEGS) have |= 1u << leg_type(cand[b][i].index);
    int leg_t = -1;
    if (have) {
        unsigned k = acr_gen1_rogue_below(&rng, (uint32_t)popcount4(have));
        for (int t = 0; t < 4; ++t) if ((have >> t) & 1u) { if (!k--) { leg_t = t; break; } }
    }
    unsigned nl = 0, nk = 0;
    for (unsigned b = 0; b < 3u; ++b)
        for (unsigned i = 0; i < nc[b]; ++i)
            if (cand[b][i].cat == ACR_GEN1_CAT_LEGS) { ++nl; nk += leg_type(cand[b][i].index) == leg_t; }
    for (unsigned b = 0; b < 3u; ++b)
        for (unsigned i = 0; i < nc[b]; ++i) {
            acr_gen1_rogue_offer *o = &cand[b][i];
            if (o->cat != ACR_GEN1_CAT_LEGS) continue;
            if (leg_type(o->index) != leg_t) { o->weight = 0; continue; }
            o->weight = (uint32_t)((uint64_t)o->weight * nl / (nk ? nk : 1));
            if (!o->weight) o->weight = 1;
        }
    unsigned used = 0;                                 /* categories already up */
    acr_gen1_rogue_offer set;
    const int has_set = warms && set_roll(r, w, &rng, &set);
    if (has_set) used |= 1u << ACR_GEN1_CAT_ARMS | 1u << ACR_GEN1_CAT_HAND | 1u << ACR_GEN1_CAT_BLADE;
    for (unsigned b = 0; b < 3u; ++b) {
        if (b == 2u && has_set) { out[n_out++] = set; break; }
        if (!nc[b]) continue;
        uint64_t fresh = 0, all = 0;
        for (unsigned i = 0; i < nc[b]; ++i) {
            all += cand[b][i].weight;
            if (!(used & (1u << cand[b][i].cat))) fresh += cand[b][i].weight;
        }
        if (!all) continue;
        const int only_fresh = fresh != 0u;
        uint32_t pick = acr_gen1_rogue_below(&rng, (uint32_t)(only_fresh ? fresh : all));
        for (unsigned i = 0; i < nc[b]; ++i) {
            if (only_fresh && (used & (1u << cand[b][i].cat))) continue;
            if (pick < cand[b][i].weight) { out[n_out++] = cand[b][i]; used |= 1u << cand[b][i].cat; break; }
            pick -= cand[b][i].weight;
        }
    }
    /* The specials take the expensive offer's place now and then: HUMAN
     * PLUS and ALL OPTIONS once per attempt, +1 P PER WIN up to 3 times and
     * not in the last fights (where it could not pay its price back). */
    int special = 0;
    const struct { uint8_t kind, cost; uint32_t odds; int allowed; } k_sp[3] = {
        { ACR_GEN1_OFFER_HPLUS, ACR_GEN1_ROGUE_HPLUS_COST, HPLUS_ODDS, !r->plus },
        { ACR_GEN1_OFFER_OPTS, ACR_GEN1_ROGUE_OPTS_COST, OPTS_ODDS, !r->opts },
        { ACR_GEN1_OFFER_INCOME, ACR_GEN1_ROGUE_INCOME_COST, INCOME_ODDS,
          r->income < INCOME_MAX && win + 1u + ACR_GEN1_ROGUE_INCOME_COST < (unsigned)NEED },
    };
    for (int k = 0; k < 3 && !has_set && !special; ++k) {
        if (!k_sp[k].allowed || acr_gen1_rogue_below(&rng, k_sp[k].odds) != 0u) continue;
        acr_gen1_rogue_offer *o = NULL;
        for (int i = 0; i < n_out; ++i) if (out[i].band == 2u) o = &out[i];
        if (!o && n_out < 3) o = &out[n_out++];
        if (!o) continue;
        memset(o, 0, sizeof *o);
        o->kind = k_sp[k].kind; o->cat = 0xFF; o->cost = k_sp[k].cost; o->band = 2; o->weight = W_NORMAL;
        special = k < 2;                               /* +1 P PER WIN rolls after the other two */
    }
    return n_out;
}

int acr_gen1_rogue_offers(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w, acr_gen1_rogue_offer out[3]) {
    if (!acr_gen1_rogue_prize_due(r)) return 0;
    const unsigned win = r->prize_wins;
    int n = draw_offers(r, w, win, 0, NULL, 0, out);
    if (r->reroll_win == win + 1u) {
        uint8_t excl[3][2];
        for (int i = 0; i < n; ++i) { excl[i][0] = out[i].cat; excl[i][1] = out[i].index; }
        const int ne = n;
        n = draw_offers(r, w, win, 0x5245524F4C4Cull, (const uint8_t (*)[2])excl, ne, out);   /* "REROLL" */
        if (!n) n = draw_offers(r, w, win, 0x5245524F4C4Cull, NULL, 0, out);
    }
    return n;
}

static void add_saved(acr_gen1_rogue *r, int cat, int index) {
    if (r->saved_n >= ACR_GEN1_ROGUE_SAVED_MAX) return;
    r->saved[r->saved_n][0] = (uint8_t)cat;
    r->saved[r->saved_n][1] = (uint8_t)index;
    ++r->saved_n;
}

static void own(acr_gen1_rogue *r, int cat, int index) {
    if (r->hangar.owned[cat][index] < ACR_GEN1_OWN_MAX) ++r->hangar.owned[cat][index];
}

/* Equip place `put` of `o`: what it replaces is gone for good. */
static void equip(acr_gen1_rogue *r, const acr_gen1_rogue_offer *o, int put) {
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) {
        if (!(o->put_lost[put] & (1u << s))) continue;
        const int cat = acr_gen1_slot_category((acr_gen1_slot)s);
        const uint8_t was = r->hangar.fitted[s];
        if (was < ACR_GEN1_PART_MAX && r->hangar.owned[cat][was]) --r->hangar.owned[cat][was];
    }
    memcpy(r->hangar.fitted, o->put_fitted[put], sizeof r->hangar.fitted);
}

int acr_gen1_rogue_take(acr_gen1_rogue *r, const acr_gen1_rogue_world *w, const acr_gen1_rogue_offer *o, int put) {
    (void)w;
    if (!acr_gen1_rogue_prize_due(r) || r->hangar.credits < (int32_t)o->cost) return 0;
    const int part = o->kind == ACR_GEN1_OFFER_PART || o->kind == ACR_GEN1_OFFER_SET;
    if (part && put >= o->nput) return 0;
    r->hangar.credits -= o->cost;
    r->spent = (uint16_t)(r->spent + o->cost);
    switch (o->kind) {
    case ACR_GEN1_OFFER_HPLUS: r->plus = 1; break;
    case ACR_GEN1_OFFER_INCOME: ++r->income; break;
    case ACR_GEN1_OFFER_OPTS:
        r->opts = 1;
        for (unsigned b = 0; b < ACR_GEN1_OPTION_MAX; ++b)
            if ((OPTS_MASK >> b & 1u) && w->cat->part[ACR_GEN1_CAT_OPTION][b].present) {
                if (!r->hangar.owned[ACR_GEN1_CAT_OPTION][b]) own(r, ACR_GEN1_CAT_OPTION, (int)b);
                r->hangar.options |= (uint16_t)(1u << b);
            }
        break;
    default:
        own(r, o->cat, o->index);
        for (int k = 0; k < o->xn; ++k) own(r, o->xcat[k], o->xindex[k]);
        if (put >= 0) equip(r, o, put);
        else {
            add_saved(r, o->cat, o->index);
            for (int k = 0; k < o->xn; ++k) add_saved(r, o->xcat[k], o->xindex[k]);
        }
        break;
    }
    ++r->prize_wins;
    return 1;
}

void acr_gen1_rogue_pass(acr_gen1_rogue *r) {
    if (!acr_gen1_rogue_prize_due(r)) return;
    r->hangar.credits += 1;
    r->points = (uint16_t)(r->points + 1u);
    ++r->prize_wins;
}

int acr_gen1_rogue_reroll(acr_gen1_rogue *r) {
    if (!acr_gen1_rogue_can_reroll(r)) return 0;
    r->hangar.credits -= 1;
    r->spent = (uint16_t)(r->spent + 1u);
    r->reroll_win = (uint8_t)(r->prize_wins + 1u);
    return 1;
}

int acr_gen1_rogue_saved_fit(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w, int n, acr_gen1_rogue_offer *o) {
    memset(o, 0, sizeof *o);
    if (n < 0 || n >= r->saved_n) return 0;
    o->kind = ACR_GEN1_OFFER_PART;
    o->cat = r->saved[n][0];
    o->index = r->saved[n][1];
    if (!acr_gen1_spare_copies(&r->hangar, (acr_gen1_category)o->cat, o->index)) return 0;
    return offer_fit(r, w, o->cat, o->index, 1, o);
}

int acr_gen1_rogue_swap(acr_gen1_rogue *r, const acr_gen1_rogue_world *w, int n, int put) {
    acr_gen1_rogue_offer o;
    if (!acr_gen1_rogue_saved_fit(r, w, n, &o) || put < 0 || put >= o.nput) return 0;
    equip(r, &o, put);
    for (int j = n + 1; j < r->saved_n; ++j) { r->saved[j - 1][0] = r->saved[j][0]; r->saved[j - 1][1] = r->saved[j][1]; }
    --r->saved_n;
    return 1;
}

/* ---- the run file ---- */

typedef struct outbuf { char *p; size_t size, len; } outbuf;

static void put(outbuf *o, const char *s) {
    for (; *s; ++s, ++o->len)
        if (o->len + 1 < o->size) o->p[o->len] = *s;
    if (o->size) o->p[o->len < o->size ? o->len : o->size - 1] = 0;
}

static void put_u(outbuf *o, uint64_t v) {
    char t[24];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    char s[24];
    for (int i = 0; i < n; ++i) s[i] = t[n - 1 - i];
    s[n] = 0;
    put(o, s);
}

static void put_kv(outbuf *o, const char *k, uint64_t v) { put(o, k); put(o, " = "); put_u(o, v); put(o, "\n"); }

static void put_list(outbuf *o, const char *k, const uint8_t *v, int n) {
    put(o, k); put(o, " = [");
    for (int i = 0; i < n; ++i) { if (i) put(o, ", "); put_u(o, v[i]); }
    put(o, "]\n");
}

static void put_str(outbuf *o, const char *k, const char *v) {
    put(o, k); put(o, " = \"");
    char c[2] = { 0, 0 };
    for (; *v; ++v) if (*v != '"' && *v != '\\' && *v >= 0x20) { c[0] = *v; put(o, c); }
    put(o, "\"\n");
}

size_t acr_gen1_rogue_write(const acr_gen1_rogue *r, char *buf, size_t size) {
    outbuf o = { buf, size, 0 };
    if (size) buf[0] = 0;
    put(&o, "# Armored-CoreR Rogue Arena run (its garage is in rogue-garage.toml).\n");
    put_kv(&o, "format", ACR_GEN1_ROGUE_FORMAT);
    {
        char hex[17];
        for (int i = 0; i < 16; ++i) hex[i] = "0123456789ABCDEF"[(r->seed >> (60 - 4 * i)) & 15u];
        hex[16] = 0;
        put_str(&o, "seed", hex);
    }
    put_kv(&o, "attempt", r->attempt); put_kv(&o, "wins", r->wins); put_kv(&o, "best", r->best);
    put_kv(&o, "over", r->over); put_kv(&o, "cleared", r->cleared); put_kv(&o, "prize_wins", r->prize_wins);
    put_kv(&o, "reroll_win", r->reroll_win); put_kv(&o, "income", r->income); put_kv(&o, "plus", r->plus);
    put_kv(&o, "opts", r->opts); put_kv(&o, "points", r->points); put_kv(&o, "spent", r->spent);
    put_kv(&o, "stage", r->stage);
    put_list(&o, "saved", &r->saved[0][0], 2 * r->saved_n);
    uint8_t lad[3 * SLOTS];
    for (int i = 0; i < SLOTS; ++i) { lad[3 * i] = r->slot[i].kind; lad[3 * i + 1] = r->slot[i].arena; lad[3 * i + 2] = r->slot[i].rank; }
    put(&o, "# the ladder from rank 20 up: kind (0 you, 1 arena opponent, 2 ghost), arena, rank / ghost\n");
    put_list(&o, "ladder", lad, 3 * SLOTS);
    for (int g = 0; g < SLOTS; ++g) {
        const acr_gen1_rogue_ghost *gh = &r->ghost[g];
        if (!gh->used) continue;
        put(&o, "[ghost]\n");
        put_kv(&o, "index", (uint64_t)g);
        put_kv(&o, "attempt", gh->attempt); put_kv(&o, "rank_held", gh->rank_held); put_kv(&o, "plus", gh->plus);
        put_list(&o, "parts", gh->parts, ACR_GEN1_SLOT_COUNT);
        put_kv(&o, "options", gh->options);
        put_list(&o, "colours", &gh->colours[0][0], 12);
        const uint8_t ai[3] = { gh->ai_from.kind, gh->ai_from.arena, gh->ai_from.rank };
        put_list(&o, "ai_from", ai, 3);
        put_str(&o, "pilot", gh->pilot);
        put_str(&o, "ac", gh->ac);
    }
    return o.len;
}

/* ---- reading ---- */

static const char *line_key(const char *p, const char *end, const char *key) {
    const size_t n = strlen(key);
    while (p < end) {
        const char *e = p;
        while (e < end && *e != '\n') ++e;
        const char *q = p;
        while (q < e && (*q == ' ' || *q == '\t')) ++q;
        if ((size_t)(e - q) > n && !strncmp(q, key, n) && (q[n] == ' ' || q[n] == '=' || q[n] == '\t')) {
            q += n;
            while (q < e && (*q == ' ' || *q == '\t' || *q == '=')) ++q;
            return q;
        }
        p = e < end ? e + 1 : end;
    }
    return NULL;
}

static long get_num(const char *p, const char *end, const char *key, long fallback) {
    const char *v = line_key(p, end, key);
    if (!v || v >= end || *v < '0' || *v > '9') return fallback;
    long x = 0;
    while (v < end && *v >= '0' && *v <= '9') x = x * 10 + (*v++ - '0');
    return x;
}

static int get_list(const char *p, const char *end, const char *key, uint8_t *out, int max) {
    const char *v = line_key(p, end, key);
    if (!v || *v != '[') return -1;
    int n = 0;
    ++v;
    while (v < end && *v != ']' && *v != '\n') {
        while (v < end && (*v == ' ' || *v == ',')) ++v;
        if (v >= end || *v < '0' || *v > '9') break;
        long x = 0;
        while (v < end && *v >= '0' && *v <= '9') x = x * 10 + (*v++ - '0');
        if (n < max) out[n] = (uint8_t)(x > 255 ? 255 : x);
        ++n;
    }
    return n;
}

static void get_text(const char *p, const char *end, const char *key, char *out, size_t size) {
    out[0] = 0;
    const char *v = line_key(p, end, key);
    if (!v || *v != '"') return;
    ++v;
    size_t n = 0;
    while (v < end && *v != '"' && *v != '\n' && n + 1 < size) out[n++] = *v++;
    out[n] = 0;
}

int acr_gen1_rogue_read(acr_gen1_rogue *out, const char *text) {
    if (!text) return 0;
    const char *end = text + strlen(text);
    const char *first_ghost = strstr(text, "[ghost]");
    const char *head_end = first_ghost ? first_ghost : end;
    if (get_num(text, head_end, "format", 0) != ACR_GEN1_ROGUE_FORMAT) return 0;
    acr_gen1_rogue r;
    memset(&r, 0, sizeof r);
    r.hangar = out->hangar;                            /* the garage is read on its own */
    char seed[24];
    get_text(text, head_end, "seed", seed, sizeof seed);
    if (!seed[0]) return 0;
    r.seed = acr_gen1_rogue_seed_from_text(seed);
    r.attempt = (uint8_t)get_num(text, head_end, "attempt", 0);
    r.wins = (uint8_t)get_num(text, head_end, "wins", 0);
    r.best = (uint8_t)get_num(text, head_end, "best", 0);
    r.over = (uint8_t)get_num(text, head_end, "over", 0);
    r.cleared = (uint8_t)get_num(text, head_end, "cleared", 0);
    r.prize_wins = (uint8_t)get_num(text, head_end, "prize_wins", 0);
    r.reroll_win = (uint8_t)get_num(text, head_end, "reroll_win", 0);
    r.income = (uint8_t)get_num(text, head_end, "income", 0);
    r.plus = (uint8_t)get_num(text, head_end, "plus", 0);
    r.opts = (uint8_t)get_num(text, head_end, "opts", 0);
    r.points = (uint16_t)get_num(text, head_end, "points", 0);
    r.spent = (uint16_t)get_num(text, head_end, "spent", 0);
    r.stage = (uint8_t)get_num(text, head_end, "stage", 0);
    uint8_t sv[2 * ACR_GEN1_ROGUE_SAVED_MAX];
    const int ns = get_list(text, head_end, "saved", sv, 2 * ACR_GEN1_ROGUE_SAVED_MAX);
    for (int i = 0; i + 1 < ns && i / 2 < ACR_GEN1_ROGUE_SAVED_MAX; i += 2) {
        if (sv[i] >= ACR_GEN1_CAT_COUNT || sv[i + 1] >= ACR_GEN1_PART_MAX) continue;
        r.saved[r.saved_n][0] = sv[i]; r.saved[r.saved_n][1] = sv[i + 1]; ++r.saved_n;
    }
    uint8_t lad[3 * SLOTS];
    if (get_list(text, head_end, "ladder", lad, 3 * SLOTS) != 3 * SLOTS) return 0;
    for (int i = 0; i < SLOTS; ++i) { r.slot[i].kind = lad[3 * i]; r.slot[i].arena = lad[3 * i + 1]; r.slot[i].rank = lad[3 * i + 2]; }
    if (r.wins >= SLOTS || r.prize_wins > r.wins) return 0;
    for (const char *g = first_ghost; g; ) {
        const char *next = strstr(g + 7, "[ghost]");
        const char *ge = next ? next : end;
        const long ix = get_num(g, ge, "index", -1);
        if (ix >= 0 && ix < SLOTS) {
            acr_gen1_rogue_ghost *gh = &r.ghost[ix];
            gh->used = 1;
            gh->attempt = (uint8_t)get_num(g, ge, "attempt", 0);
            gh->rank_held = (uint8_t)get_num(g, ge, "rank_held", 0);
            gh->plus = (uint8_t)get_num(g, ge, "plus", 0);
            if (get_list(g, ge, "parts", gh->parts, ACR_GEN1_SLOT_COUNT) != ACR_GEN1_SLOT_COUNT) gh->used = 0;
            gh->options = (uint16_t)get_num(g, ge, "options", 0);
            get_list(g, ge, "colours", &gh->colours[0][0], 12);
            uint8_t ai[3] = { 0, 0, 0 };
            get_list(g, ge, "ai_from", ai, 3);
            gh->ai_from.kind = ai[0]; gh->ai_from.arena = ai[1]; gh->ai_from.rank = ai[2];
            get_text(g, ge, "pilot", gh->pilot, sizeof gh->pilot);
            get_text(g, ge, "ac", gh->ac, sizeof gh->ac);
            if (gh->used) ++r.ghosts;
        }
        g = next;
    }
    *out = r;
    return 1;
}
