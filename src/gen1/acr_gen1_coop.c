/* Rogue Arena co-op. See acr_gen1_coop.h; each player's side runs on the
 * solo rules (acr_gen1_rogue.c), this file adds the pairs, the shared
 * stash, the rejoin choice and the final duel. */
#include "acr_gen1_coop.h"
#include "acr_gen1_garage.h"

#include <string.h>

#define SLOTS   ACR_GEN1_ROGUE_SLOTS
#define NEED    (SLOTS - 1)
#define PLAYERS ACR_GEN1_COOP_PLAYERS

static void copy_name(char *dst, const char *src) {
    size_t i = 0;
    for (; src && src[i] && i + 1 < ACR_GEN1_ROGUE_NAME; ++i) dst[i] = src[i];
    dst[i] = 0;
}

/* Each player's own stream: their prize screens and paint differ. */
static uint64_t player_seed(const acr_gen1_coop *c, int p) {
    uint64_t k = c->seed ^ 0x434F4F50ull ^ (0x9E3779B97F4A7C15ull * (uint64_t)(p + 1));   /* "COOP" */
    return acr_gen1_rogue_next(&k);
}

/* A build seed: the run, the attempt, the player and `extra` (0 at an
 * attempt's start, the rung for a rejoin's new AC). */
static uint64_t ac_seed(const acr_gen1_coop *c, int p, unsigned extra) {
    uint64_t k = c->seed ^ 0x53544152ull ^ (0x9E3779B97F4A7C15ull * (uint64_t)(c->attempt + 1u)) ^
                 (0xBF58476D1CE4E5B9ull * (uint64_t)(p + 1)) ^ (0x94D049BB133111EBull * (uint64_t)extra);
    return acr_gen1_rogue_next(&k);
}

static int own_count(const acr_gen1_rogue *r, int cat, unsigned i) {
    return i < ACR_GEN1_PART_MAX ? r->hangar.owned[cat][i] : 0;
}

static void own_add(acr_gen1_rogue *r, int cat, unsigned i, int d) {
    if (i >= ACR_GEN1_PART_MAX) return;
    const int v = r->hangar.owned[cat][i] + d;
    r->hangar.owned[cat][i] = (uint8_t)(v < 0 ? 0 : v > ACR_GEN1_OWN_MAX ? ACR_GEN1_OWN_MAX : v);
}

/* Player p's attempt starts: a random legal AC with a real gun, 0 P. */
static int start_player(acr_gen1_coop *c, const acr_gen1_rogue_world *w, int p) {
    acr_gen1_rogue *r = &c->player[p];
    uint8_t start[ACR_GEN1_SLOT_COUNT];
    if (!acr_gen1_rogue_start_ac(w, ac_seed(c, p, 0), start)) return 0;
    char pilot[ACR_GEN1_ROGUE_NAME], ac[ACR_GEN1_ROGUE_NAME];
    copy_name(pilot, r->hangar.pilot_name);
    copy_name(ac, r->hangar.ac_name);
    memset(r, 0, sizeof *r);
    acr_gen1_hangar_new(&r->hangar, start, 0, 0);
    copy_name(r->hangar.pilot_name, pilot);
    copy_name(r->hangar.ac_name, ac);
    r->seed = player_seed(c, p);
    r->attempt = c->attempt;
    acr_gen1_rogue_paint(r->seed, r->attempt, r->hangar.colours);
    return 1;
}

static int begin_attempt(acr_gen1_coop *c, const acr_gen1_rogue_world *w) {
    c->wins = 0; c->over = 0; c->duel_due = 0; c->cleared = 0; c->duel_winner = 0;
    memset(c->rejoin, 0, sizeof c->rejoin);
    c->stash_n = 0;
    for (int p = 0; p < PLAYERS; ++p) {
        c->hold[p] = -1;
        if (!start_player(c, w, p)) return 0;
    }
    return 1;
}

int acr_gen1_coop_new(acr_gen1_coop *c, const acr_gen1_rogue_world *w, uint64_t seed,
                      const char *pilot[PLAYERS], const char *ac[PLAYERS]) {
    memset(c, 0, sizeof *c);
    c->seed = seed;
    for (int p = 0; p < PLAYERS; ++p) {
        copy_name(c->player[p].hangar.pilot_name, pilot ? pilot[p] : "");
        copy_name(c->player[p].hangar.ac_name, ac ? ac[p] : "");
    }
    if (acr_gen1_rogue_lineup_pairs(w, seed, c->pair) != SLOTS) return 0;
    return begin_attempt(c, w);
}

const acr_gen1_rogue_pick *acr_gen1_coop_next_pair(const acr_gen1_coop *c) {
    if (c->over || c->duel_due || c->cleared || c->wins + 1 >= SLOTS) return NULL;
    return c->pair[c->wins + 1];
}

int acr_gen1_coop_rung(const acr_gen1_coop *c, const acr_gen1_rogue_world *w, int rung, int k,
                       acr_gen1_rogue_foe *out) {
    if (rung < 1 || rung >= SLOTS || k < 0 || k > 1) return 0;
    const acr_gen1_rogue_pick *p = &c->pair[rung][k];
    if (p->kind == ACR_GEN1_ROGUE_FOE) {
        const acr_gen1_rogue_foe *f = acr_gen1_rogue_find(w, p->arena, p->rank);
        if (!f) return 0;
        *out = *f;
        return 1;
    }
    if (p->kind != ACR_GEN1_ROGUE_GHOST || p->rank >= ACR_GEN1_COOP_GHOSTS || !c->ghost[p->rank].used) return 0;
    const acr_gen1_rogue_ghost *g = &c->ghost[p->rank];
    memset(out, 0, sizeof *out);
    const acr_gen1_rogue_foe *src = acr_gen1_rogue_find(w, g->ai_from.arena, g->ai_from.rank);
    if (src) { out->ai = src->ai; out->arena = src->arena; out->entries = src->entries; }
    else acr_gen1_ai_preset(&out->ai, ACR_GEN1_AI_BALANCED);
    out->ai.rebel = g->plus ? 9 : 0;
    out->rank = (uint8_t)(SLOTS - rung);
    memcpy(out->parts, g->parts, sizeof out->parts);
    out->options = g->options;
    memcpy(out->colours, g->colours, sizeof out->colours);
    copy_name(out->pilot, g->pilot);
    copy_name(out->ac, g->ac);
    return 1;
}

void acr_gen1_coop_won(acr_gen1_coop *c, int down0, int down1) {
    if (c->over || c->duel_due || c->cleared || c->wins >= NEED) return;
    ++c->wins;
    if (c->wins > c->best) c->best = c->wins;
    const int down[PLAYERS] = { down0 != 0, down1 != 0 };
    for (int p = 0; p < PLAYERS; ++p) {
        /* Everyone scores, the destroyed partner too (user, 2026-11-25). */
        acr_gen1_rogue_won(&c->player[p]);
        c->rejoin[p] = (uint8_t)down[p];
    }
    if (c->wins >= NEED) c->duel_due = 1;
}

void acr_gen1_coop_lost(acr_gen1_coop *c) {
    if (c->cleared) return;
    c->over = 1;
    for (int p = 0; p < PLAYERS; ++p) acr_gen1_rogue_lost(&c->player[p]);
}

/* ---- ghosts ---- */

static int free_ghost(const acr_gen1_coop *c, int not_this) {
    for (int g = 0; g < ACR_GEN1_COOP_GHOSTS; ++g) if (!c->ghost[g].used && g != not_this) return g;
    return -1;
}

/* The losing duo goes in at rank `held` as a pair: the pair there and
 * every one below it move down a rung, and the bottom pair leaves. */
static void add_ghost_pair(acr_gen1_coop *c, int held) {
    if (held < 1 || held > NEED) return;
    for (int k = 0; k < 2; ++k) {                       /* pushed off the bottom */
        const acr_gen1_rogue_pick out = c->pair[1][k];
        if (out.kind == ACR_GEN1_ROGUE_GHOST && out.rank < ACR_GEN1_COOP_GHOSTS && c->ghost[out.rank].used) {
            c->ghost[out.rank].used = 0;
            --c->ghosts;
        }
    }
    const int g0 = free_ghost(c, -1), g1 = free_ghost(c, g0);
    if (g0 < 0 || g1 < 0) return;
    const int at = SLOTS - held;                        /* 1..19 */
    acr_gen1_rogue_pick ai[2];
    for (int k = 0; k < 2; ++k) {
        const acr_gen1_rogue_pick d = c->pair[at][k];
        ai[k] = d.kind == ACR_GEN1_ROGUE_GHOST && d.rank < ACR_GEN1_COOP_GHOSTS ? c->ghost[d.rank].ai_from : d;
    }
    memmove(&c->pair[1], &c->pair[2], (size_t)(at - 1) * sizeof c->pair[0]);
    const int gi[2] = { g0, g1 };
    for (int p = 0; p < PLAYERS; ++p) {
        const acr_gen1_rogue *r = &c->player[p];
        acr_gen1_rogue_ghost *gh = &c->ghost[gi[p]];
        memset(gh, 0, sizeof *gh);
        memcpy(gh->parts, r->hangar.fitted, sizeof gh->parts);   /* the exact losing build */
        gh->options = r->hangar.options;
        gh->plus = r->plus;
        memcpy(gh->colours, r->hangar.colours, sizeof gh->colours);
        copy_name(gh->pilot, r->hangar.pilot_name);
        copy_name(gh->ac, r->hangar.ac_name);
        gh->used = 1;
        gh->attempt = c->attempt;
        gh->rank_held = (uint8_t)held;
        gh->ai_from = ai[p];
        c->pair[at][p].kind = ACR_GEN1_ROGUE_GHOST;
        c->pair[at][p].arena = 0;
        c->pair[at][p].rank = (uint8_t)gi[p];
        ++c->ghosts;
    }
}

int acr_gen1_coop_retry(acr_gen1_coop *c, const acr_gen1_rogue_world *w) {
    if (!c->over) return 0;
    if (c->wins > 0) add_ghost_pair(c, SLOTS - c->wins);
    ++c->attempt;
    return begin_attempt(c, w);
}

/* ---- the stash ---- */

static void stash_remove(acr_gen1_coop *c, int n) {
    for (int j = n + 1; j < c->stash_n; ++j) c->stash[j - 1] = c->stash[j];
    --c->stash_n;
    for (int p = 0; p < PLAYERS; ++p) {
        if (c->hold[p] == n) c->hold[p] = -1;
        else if (c->hold[p] > n) --c->hold[p];
    }
}

int acr_gen1_coop_keep_cost(const acr_gen1_coop *c, int p, int *parts, int32_t *points) {
    int n = 0;
    for (int i = 0; i < c->stash_n; ++i) n += c->stash[i].owner == p;
    if (parts) *parts = n;
    if (points) *points = c->player[p].hangar.credits;
    return n;
}

int acr_gen1_coop_rejoin(acr_gen1_coop *c, const acr_gen1_rogue_world *w, int p, int keep) {
    if (p < 0 || p >= PLAYERS || !c->rejoin[p]) return 0;
    acr_gen1_rogue *r = &c->player[p];
    if (keep) {
        /* The old AC back: every part this player saved and all their P. */
        for (int i = c->stash_n - 1; i >= 0; --i) {
            if (c->stash[i].owner != p) continue;
            own_add(r, c->stash[i].cat, c->stash[i].index, -1);
            stash_remove(c, i);
        }
        r->spent = (uint16_t)(r->spent + (r->hangar.credits > 0 ? r->hangar.credits : 0));
        r->hangar.credits = 0;
    } else {
        /* A new random AC, free: the old one's parts are gone. Upgrades
         * (HUMAN PLUS, ALL OPTIONS, +1 P PER WIN), P and saved parts stay. */
        uint8_t start[ACR_GEN1_SLOT_COUNT];
        if (!acr_gen1_rogue_start_ac(w, ac_seed(c, p, 100u + c->wins), start)) return 0;
        for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) {
            const int cat = acr_gen1_slot_category((acr_gen1_slot)s);
            if (r->hangar.fitted[s] != ACR_GEN1_NO_PART) own_add(r, cat, r->hangar.fitted[s], -1);
        }
        for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) {
            const int cat = acr_gen1_slot_category((acr_gen1_slot)s);
            if (start[s] != ACR_GEN1_NO_PART) own_add(r, cat, start[s], +1);
        }
        memcpy(r->hangar.fitted, start, sizeof start);
    }
    c->rejoin[p] = 0;
    return 1;
}

int acr_gen1_coop_prize_due(const acr_gen1_coop *c, int p) {
    return p >= 0 && p < PLAYERS && !c->rejoin[p] && acr_gen1_rogue_prize_due(&c->player[p]);
}

void acr_gen1_coop_collect(acr_gen1_coop *c, int p) {
    acr_gen1_rogue *r = &c->player[p];
    for (int i = 0; i < r->saved_n && c->stash_n < ACR_GEN1_COOP_STASH_MAX; ++i) {
        acr_gen1_coop_item *it = &c->stash[c->stash_n++];
        it->cat = r->saved[i][0];
        it->index = r->saved[i][1];
        it->owner = (uint8_t)p;
    }
    r->saved_n = 0;
}

int acr_gen1_coop_stash_room(const acr_gen1_coop *c, const acr_gen1_rogue_offer *o) {
    const int n = o->kind == ACR_GEN1_OFFER_SET ? 1 + o->xn : o->kind == ACR_GEN1_OFFER_PART ? 1 : 0;
    return c->stash_n + n <= ACR_GEN1_COOP_STASH_MAX;
}

int acr_gen1_coop_ready(const acr_gen1_coop *c) {
    if (c->over || c->cleared) return 0;
    for (int p = 0; p < PLAYERS; ++p)
        if (c->rejoin[p] || acr_gen1_rogue_prize_due(&c->player[p])) return 0;
    return 1;
}

int acr_gen1_coop_hold(acr_gen1_coop *c, int p, int n) {
    if (p < 0 || p >= PLAYERS) return 0;
    if (n < 0) { c->hold[p] = -1; return 1; }
    if (n >= c->stash_n || c->hold[1 - p] == n) return 0;
    c->hold[p] = (int8_t)n;
    return 1;
}

/* Player p's run with stash item n as its last saved part (the copy
 * counted as theirs). 0 = it can't be. */
static int with_item(const acr_gen1_coop *c, int p, int n, acr_gen1_rogue *t) {
    if (p < 0 || p >= PLAYERS || n < 0 || n >= c->stash_n || c->hold[1 - p] == n) return 0;
    *t = c->player[p];
    if (t->saved_n >= ACR_GEN1_ROGUE_SAVED_MAX) return 0;
    const acr_gen1_coop_item *it = &c->stash[n];
    if (it->owner != p) {
        if (own_count(t, it->cat, it->index) >= ACR_GEN1_OWN_MAX) return 0;
        own_add(t, it->cat, it->index, +1);
    }
    t->saved[t->saved_n][0] = it->cat;
    t->saved[t->saved_n][1] = it->index;
    ++t->saved_n;
    return 1;
}

int acr_gen1_coop_stash_fit(const acr_gen1_coop *c, const acr_gen1_rogue_world *w, int p, int n,
                            acr_gen1_rogue_offer *out) {
    static acr_gen1_rogue t;
    memset(out, 0, sizeof *out);
    if (!with_item(c, p, n, &t)) return 0;
    return acr_gen1_rogue_saved_fit(&t, w, t.saved_n - 1, out);
}

int acr_gen1_coop_stash_take(acr_gen1_coop *c, const acr_gen1_rogue_world *w, int p, int n, int put) {
    static acr_gen1_rogue t;
    if (!with_item(c, p, n, &t) || !acr_gen1_rogue_swap(&t, w, t.saved_n - 1, put)) return 0;
    const acr_gen1_coop_item it = c->stash[n];
    c->player[p] = t;
    if (it.owner != p) own_add(&c->player[it.owner], it.cat, it.index, -1);   /* the copy changes hands */
    stash_remove(c, n);
    return 1;
}

/* ---- the duel ---- */

void acr_gen1_coop_duel(acr_gen1_coop *c, int winner) {
    if (!c->duel_due || winner < 0 || winner >= PLAYERS) return;
    c->duel_due = 0;
    c->cleared = 1;
    c->duel_winner = (uint8_t)winner;
}

int acr_gen1_coop_rank(const acr_gen1_coop *c, int p) {
    if (c->cleared) return p == c->duel_winner ? 1 : 2;
    return SLOTS - c->wins;
}

/* ---- the run file ---- */

typedef struct outbuf { char *p; size_t size, len; } outbuf;

static void put(outbuf *o, const char *s) {
    for (; *s; ++s, ++o->len)
        if (o->len + 1 < o->size) o->p[o->len] = *s;
    if (o->size) o->p[o->len < o->size ? o->len : o->size - 1] = 0;
}

static void put_u(outbuf *o, uint64_t v) {
    char t[24], s[24];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    for (int i = 0; i < n; ++i) s[i] = t[n - 1 - i];
    s[n] = 0;
    put(o, s);
}

static void put_kv(outbuf *o, const char *k, uint64_t v) { put(o, k); put(o, " = "); put_u(o, v); put(o, "\n"); }

static void put_pkv(outbuf *o, int p, const char *k, uint64_t v) {
    char key[32] = "p1_";
    key[1] = (char)('1' + p);
    size_t i = 3;
    for (; *k && i + 1 < sizeof key; ++k) key[i++] = *k;
    key[i] = 0;
    put_kv(o, key, v);
}

static void put_list(outbuf *o, const char *k, const uint8_t *v, int n) {
    put(o, k); put(o, " = [");
    for (int i = 0; i < n; ++i) { if (i) put(o, ", "); put_u(o, v[i]); }
    put(o, "]\n");
}

static void put_str(outbuf *o, const char *k, const char *v) {
    put(o, k); put(o, " = \"");
    char ch[2] = { 0, 0 };
    for (; *v; ++v) if (*v != '"' && *v != '\\' && *v >= 0x20) { ch[0] = *v; put(o, ch); }
    put(o, "\"\n");
}

static void put_hex(outbuf *o, const char *k, uint64_t v) {
    char hex[17];
    for (int i = 0; i < 16; ++i) hex[i] = "0123456789ABCDEF"[(v >> (60 - 4 * i)) & 15u];
    hex[16] = 0;
    put_str(o, k, hex);
}

size_t acr_gen1_coop_write(const acr_gen1_coop *c, char *buf, size_t size) {
    outbuf o = { buf, size, 0 };
    if (size) buf[0] = 0;
    put(&o, "# Armored-CoreR Rogue Arena co-op run, kept by the host\n"
            "# (each player's garage is in rogue-coop-p1.toml / rogue-coop-p2.toml).\n");
    put_kv(&o, "format", ACR_GEN1_COOP_FORMAT);
    put_hex(&o, "seed", c->seed);
    put_kv(&o, "attempt", c->attempt); put_kv(&o, "wins", c->wins); put_kv(&o, "best", c->best);
    put_kv(&o, "over", c->over); put_kv(&o, "duel_due", c->duel_due); put_kv(&o, "cleared", c->cleared);
    put_kv(&o, "duel_winner", c->duel_winner); put_kv(&o, "stage", c->stage);
    for (int p = 0; p < PLAYERS; ++p) {
        const acr_gen1_rogue *r = &c->player[p];
        put_pkv(&o, p, "rejoin", c->rejoin[p]);
        put_pkv(&o, p, "prize_wins", r->prize_wins); put_pkv(&o, p, "reroll_win", r->reroll_win);
        put_pkv(&o, p, "income", r->income); put_pkv(&o, p, "plus", r->plus); put_pkv(&o, p, "opts", r->opts);
        put_pkv(&o, p, "points", r->points); put_pkv(&o, p, "spent", r->spent);
    }
    uint8_t st[3 * ACR_GEN1_COOP_STASH_MAX];
    for (int i = 0; i < c->stash_n; ++i) { st[3 * i] = c->stash[i].cat; st[3 * i + 1] = c->stash[i].index; st[3 * i + 2] = c->stash[i].owner; }
    put(&o, "# parts saved for later: category, part, who saved it (0 player 1, 1 player 2)\n");
    put_list(&o, "stash", st, 3 * c->stash_n);
    uint8_t lad[6 * SLOTS];
    for (int i = 0; i < SLOTS; ++i)
        for (int k = 0; k < 2; ++k) {
            const acr_gen1_rogue_pick *q = &c->pair[i][k];
            lad[6 * i + 3 * k] = q->kind; lad[6 * i + 3 * k + 1] = q->arena; lad[6 * i + 3 * k + 2] = q->rank;
        }
    put(&o, "# the ladder from rank 20 up, two a rank: kind (0 you, 1 arena opponent, 2 ghost), arena, rank / ghost\n");
    put_list(&o, "ladder", lad, 6 * SLOTS);
    for (int g = 0; g < ACR_GEN1_COOP_GHOSTS; ++g) {
        const acr_gen1_rogue_ghost *gh = &c->ghost[g];
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

static long get_pnum(const char *p, const char *end, int pl, const char *key) {
    char k[32] = "p1_";
    k[1] = (char)('1' + pl);
    size_t i = 3;
    for (; *key && i + 1 < sizeof k; ++key) k[i++] = *key;
    k[i] = 0;
    return get_num(p, end, k, 0);
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

int acr_gen1_coop_read(acr_gen1_coop *out, const char *text) {
    if (!text) return 0;
    const char *end = text + strlen(text);
    const char *first_ghost = strstr(text, "[ghost]");
    const char *he = first_ghost ? first_ghost : end;
    if (get_num(text, he, "format", 0) != ACR_GEN1_COOP_FORMAT) return 0;
    static acr_gen1_coop c;
    memset(&c, 0, sizeof c);
    char seed[24];
    get_text(text, he, "seed", seed, sizeof seed);
    if (!seed[0]) return 0;
    c.seed = acr_gen1_rogue_seed_from_text(seed);
    c.attempt = (uint8_t)get_num(text, he, "attempt", 0);
    c.wins = (uint8_t)get_num(text, he, "wins", 0);
    c.best = (uint8_t)get_num(text, he, "best", 0);
    c.over = (uint8_t)get_num(text, he, "over", 0);
    c.duel_due = (uint8_t)get_num(text, he, "duel_due", 0);
    c.cleared = (uint8_t)get_num(text, he, "cleared", 0);
    c.duel_winner = (uint8_t)get_num(text, he, "duel_winner", 0);
    c.stage = (uint8_t)get_num(text, he, "stage", 0);
    if (c.wins > NEED || c.duel_winner >= PLAYERS) return 0;
    for (int p = 0; p < PLAYERS; ++p) {
        acr_gen1_rogue *r = &c.player[p];
        r->hangar = out->player[p].hangar;               /* the garages are read on their own */
        r->seed = player_seed(&c, p);
        r->attempt = c.attempt;
        r->wins = c.wins;
        r->best = c.best;
        r->over = c.over;
        r->cleared = (uint8_t)(c.wins >= NEED);
        c.rejoin[p] = (uint8_t)(get_pnum(text, he, p, "rejoin") != 0);
        r->prize_wins = (uint8_t)get_pnum(text, he, p, "prize_wins");
        r->reroll_win = (uint8_t)get_pnum(text, he, p, "reroll_win");
        r->income = (uint8_t)get_pnum(text, he, p, "income");
        r->plus = (uint8_t)get_pnum(text, he, p, "plus");
        r->opts = (uint8_t)get_pnum(text, he, p, "opts");
        r->points = (uint16_t)get_pnum(text, he, p, "points");
        r->spent = (uint16_t)get_pnum(text, he, p, "spent");
        if (r->prize_wins > r->wins) return 0;
        c.hold[p] = -1;
    }
    uint8_t st[3 * ACR_GEN1_COOP_STASH_MAX];
    const int ns = get_list(text, he, "stash", st, 3 * ACR_GEN1_COOP_STASH_MAX);
    for (int i = 0; i + 2 < ns && i / 3 < ACR_GEN1_COOP_STASH_MAX; i += 3) {
        if (st[i] >= ACR_GEN1_CAT_COUNT || st[i + 1] >= ACR_GEN1_PART_MAX || st[i + 2] >= PLAYERS) continue;
        acr_gen1_coop_item *it = &c.stash[c.stash_n++];
        it->cat = st[i]; it->index = st[i + 1]; it->owner = st[i + 2];
    }
    uint8_t lad[6 * SLOTS];
    if (get_list(text, he, "ladder", lad, 6 * SLOTS) != 6 * SLOTS) return 0;
    for (int i = 0; i < SLOTS; ++i)
        for (int k = 0; k < 2; ++k) {
            acr_gen1_rogue_pick *q = &c.pair[i][k];
            q->kind = lad[6 * i + 3 * k]; q->arena = lad[6 * i + 3 * k + 1]; q->rank = lad[6 * i + 3 * k + 2];
        }
    for (const char *g = first_ghost; g; ) {
        const char *next = strstr(g + 7, "[ghost]");
        const char *ge = next ? next : end;
        const long ix = get_num(g, ge, "index", -1);
        if (ix >= 0 && ix < ACR_GEN1_COOP_GHOSTS) {
            acr_gen1_rogue_ghost *gh = &c.ghost[ix];
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
            if (gh->used) ++c.ghosts;
        }
        g = next;
    }
    *out = c;
    return 1;
}

/* ---- online: the whole run, and one player's side ---- */


static const char *const k_mark[4] = { "#--- acr-coop garage 1 ---\n", "#--- acr-coop garage 2 ---\n",
                                       "#--- acr-coop net ---\n", "#--- acr-coop side ---\n" };

static size_t append(char *buf, size_t size, size_t at, const char *s) {
    for (; *s; ++s, ++at)
        if (at + 1 < size) buf[at] = *s;
    if (size) buf[at < size ? at : size - 1] = 0;
    return at;
}

size_t acr_gen1_coop_state_write(const acr_gen1_coop *c, char *buf, size_t size) {
    size_t n = acr_gen1_coop_write(c, buf, size);
    for (int p = 0; p < PLAYERS; ++p) {
        n = append(buf, size, n, k_mark[p]);
        n += acr_gen1_save_write(&c->player[p].hangar, n < size ? buf + n : NULL, n < size ? size - n : 0);
    }
    n = append(buf, size, n, k_mark[2]);
    outbuf o = { n < size ? buf + n : NULL, n < size ? size - n : 0, 0 };
    put_kv(&o, "hold1", (uint64_t)(c->hold[0] + 1));
    put_kv(&o, "hold2", (uint64_t)(c->hold[1] + 1));
    return n + o.len;
}

/* Cuts text at the marks (into a copy): part[i] starts after mark i - 1. */
static int split(const char *text, char *copy, size_t cap, const char *const marks[], int nmarks, char *part[]) {
    const size_t len = strlen(text);
    if (len + 1 > cap) return 0;
    memcpy(copy, text, len + 1);
    part[0] = copy;
    for (int i = 0; i < nmarks; ++i) {
        char *m = strstr(part[i], marks[i]);
        if (!m) return 0;
        *m = 0;
        part[i + 1] = m + strlen(marks[i]);
    }
    return 1;
}

int acr_gen1_coop_state_read(acr_gen1_coop *out, const char *text) {
    if (!text) return 0;
    static char copy[96 * 1024];
    static acr_gen1_coop c;
    char *part[4];
    if (!split(text, copy, sizeof copy, k_mark, 3, part)) return 0;
    memset(&c, 0, sizeof c);
    for (int p = 0; p < PLAYERS; ++p)
        if (!acr_gen1_save_read(&c.player[p].hangar, part[1 + p])) return 0;
    if (!acr_gen1_coop_read(&c, part[0])) return 0;
    const char *ne = part[3] + strlen(part[3]);
    for (int p = 0; p < PLAYERS; ++p) {
        const long h = get_num(part[3], ne, p ? "hold2" : "hold1", 0) - 1;
        c.hold[p] = (int8_t)(h >= 0 && h < c.stash_n ? h : -1);
    }
    if (c.hold[0] >= 0 && c.hold[0] == c.hold[1]) c.hold[1] = -1;
    *out = c;
    return 1;
}

size_t acr_gen1_coop_side_write(const acr_gen1_coop *c, int p, char *buf, size_t size) {
    if (p < 0 || p >= PLAYERS) { if (size) buf[0] = 0; return 0; }
    const acr_gen1_rogue *r = &c->player[p];
    size_t n = acr_gen1_save_write(&r->hangar, buf, size);
    n = append(buf, size, n, k_mark[3]);
    outbuf o = { n < size ? buf + n : NULL, n < size ? size - n : 0, 0 };
    put_kv(&o, "player", (uint64_t)p);
    put_kv(&o, "prize_wins", r->prize_wins); put_kv(&o, "reroll_win", r->reroll_win);
    put_kv(&o, "income", r->income); put_kv(&o, "plus", r->plus); put_kv(&o, "opts", r->opts);
    put_kv(&o, "points", r->points); put_kv(&o, "spent", r->spent);
    uint8_t sv[2 * ACR_GEN1_ROGUE_SAVED_MAX];
    for (int i = 0; i < r->saved_n; ++i) { sv[2 * i] = r->saved[i][0]; sv[2 * i + 1] = r->saved[i][1]; }
    put_list(&o, "saved", sv, 2 * r->saved_n);
    return n + o.len;
}

int acr_gen1_coop_side_read(acr_gen1_coop *c, int p, const char *text) {
    if (!text || p < 0 || p >= PLAYERS) return 0;
    static char copy[32 * 1024];
    char *part[2];
    if (!split(text, copy, sizeof copy, &k_mark[3], 1, part)) return 0;
    static acr_gen1_rogue r;
    r = c->player[p];
    if (!acr_gen1_save_read(&r.hangar, part[0])) return 0;
    const char *b = part[1], *e = b + strlen(b);
    if (get_num(b, e, "player", -1) != p) return 0;
    r.prize_wins = (uint8_t)get_num(b, e, "prize_wins", 0);
    r.reroll_win = (uint8_t)get_num(b, e, "reroll_win", 0);
    r.income = (uint8_t)get_num(b, e, "income", 0);
    r.plus = (uint8_t)get_num(b, e, "plus", 0);
    r.opts = (uint8_t)get_num(b, e, "opts", 0);
    r.points = (uint16_t)get_num(b, e, "points", 0);
    r.spent = (uint16_t)get_num(b, e, "spent", 0);
    if (r.prize_wins > c->wins) return 0;
    uint8_t sv[2 * ACR_GEN1_ROGUE_SAVED_MAX];
    const int ns = get_list(b, e, "saved", sv, 2 * ACR_GEN1_ROGUE_SAVED_MAX);
    if (ns < 0 || ns % 2 || ns > 2 * ACR_GEN1_ROGUE_SAVED_MAX) return 0;
    r.saved_n = 0;
    for (int i = 0; i < ns; i += 2) {
        if (sv[i] >= ACR_GEN1_CAT_COUNT || sv[i + 1] >= ACR_GEN1_PART_MAX) return 0;
        r.saved[r.saved_n][0] = sv[i];
        r.saved[r.saved_n][1] = sv[i + 1];
        ++r.saved_n;
    }
    c->player[p] = r;
    return 1;
}
