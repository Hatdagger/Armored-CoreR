/* The Arena's ranked ladders. See acr_gen1_arena.h. */
#include "acr_gen1_arena.h"

#include <string.h>

static const char *const k_name[ACR_GEN1_ARENA_COUNT] = {
    "ARENA", "HUMANOID LEGS", "FOUR LEGS", "REVERSE JOINT", "CATERPILLAR",
    "CHAMPION", "GUEST", "FROMSOFT", "MASTER",
};

const char *acr_gen1_arena_name(int arena) {
    return arena >= 0 && arena < ACR_GEN1_ARENA_COUNT ? k_name[arena] : "ARENA";
}

int32_t acr_gen1_arena_timer_ms(int timer) {
    return timer == ACR_GEN1_ARENA_TIMER_5MIN ? 300000 : timer == ACR_GEN1_ARENA_TIMER_NONE ? 0 : 120000;
}

const char *acr_gen1_arena_timer_name(int timer) {
    return timer == ACR_GEN1_ARENA_TIMER_5MIN ? "5:00" : timer == ACR_GEN1_ARENA_TIMER_NONE ? "NONE" : "2:00";
}

/* ---- prizes (arena-prizes.md section 4) ---- */

/* Prize id -> the game's owned-table index (menu module table 0x800D5C9C). */
static const uint8_t k_prize_owned[ACR_GEN1_ARENA_PRIZES] = {
    247, 241, 13, 14, 37, 150, 239, 238, 242, 173, 174, 213,
    62, 73, 77, 85, 180, 181, 11, 50, 240, 214, 136,
};

/* The owned table's categories: first index of each, in table order (head
 * 0, core 18, arms 29, legs 53, generator 93, FCS 108, booster 143, back
 * 157, hand 217, blade 243), and how many parts each table has. FCS stops
 * at 124: 124-142 are not one of the ten part tables (prize 22). */
static const struct { uint8_t first, count, cat; } k_owned[] = {
    {   0, 18, ACR_GEN1_CAT_HEAD },     {  18, 11, ACR_GEN1_CAT_CORE },
    {  29, 24, ACR_GEN1_CAT_ARMS },     {  53, 40, ACR_GEN1_CAT_LEGS },
    {  93, 15, ACR_GEN1_CAT_GENERATOR },{ 108, 16, ACR_GEN1_CAT_FCS },
    { 143, 14, ACR_GEN1_CAT_BOOSTER },  { 157, 60, ACR_GEN1_CAT_BACK },
    { 217, 26, ACR_GEN1_CAT_HAND },     { 243, 13, ACR_GEN1_CAT_BLADE },
};

int acr_gen1_arena_prize_part(int prize_id, int *cat, int *index) {
    if (prize_id < 0 || prize_id >= ACR_GEN1_ARENA_PRIZES) return 0;
    const int owned = k_prize_owned[prize_id];
    for (size_t i = 0; i < sizeof k_owned / sizeof k_owned[0]; ++i) {
        if (owned >= k_owned[i].first && owned < k_owned[i].first + k_owned[i].count) {
            const int ix = owned - k_owned[i].first;
            if (ix >= ACR_GEN1_PART_MAX) return 0;
            if (cat) *cat = k_owned[i].cat;
            if (index) *index = ix;
            return 1;
        }
    }
    return 0;
}

/* ---- the roster ---- */

const acr_gen1_rogue_foe *acr_gen1_arena_foe(const acr_gen1_arena_world *w, int a, int disc_rank) {
    if (!w || !w->foe) return NULL;
    for (int i = 0; i < w->foes; ++i)
        if (w->foe[i].arena == a && w->foe[i].rank == disc_rank) return &w->foe[i];
    return NULL;
}

/* Places in arena a as the import has it: every opponent 1..places-1
 * present, and their `entries` agreeing. 0 = not usable. */
static int import_places(const acr_gen1_arena_world *w, int a) {
    int places = 0;
    for (int i = 0; i < w->foes; ++i)
        if (w->foe[i].arena == a) { places = w->foe[i].entries; break; }
    if (places < 2 || places > ACR_GEN1_ARENA_PLACES) return 0;
    for (int r = 1; r < places; ++r) {
        const acr_gen1_rogue_foe *f = acr_gen1_arena_foe(w, a, r);
        if (!f || f->entries != places) return 0;
    }
    return places;
}

static void ladder_new(acr_gen1_arena_ladder *l, int places) {
    memset(l, 0, sizeof *l);
    l->places = (uint8_t)places;
    for (int r = 1; r < places; ++r) l->holder[r - 1] = (uint8_t)r;
    if (places) l->holder[places - 1] = ACR_GEN1_ARENA_YOU;
}

void acr_gen1_arena_new(acr_gen1_arena *s, const acr_gen1_arena_world *w) {
    memset(s, 0, sizeof *s);
    for (int a = 0; a < ACR_GEN1_ARENA_COUNT; ++a) ladder_new(&s->a[a], import_places(w, a));
}

/* A ladder holds each opponent once and the player once. */
static int ladder_ok(const acr_gen1_arena_ladder *l) {
    if (!l->places) return 1;
    if (l->places > ACR_GEN1_ARENA_PLACES) return 0;
    uint8_t seen[ACR_GEN1_ARENA_PLACES + 1];
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < l->places; ++i) {
        const int h = l->holder[i] == ACR_GEN1_ARENA_YOU ? 0 : l->holder[i];
        if (h >= l->places || seen[h]) return 0;
        seen[h] = 1;
    }
    return 1;
}

int acr_gen1_arena_sync(acr_gen1_arena *s, const acr_gen1_arena_world *w) {
    int reset = 0;
    for (int a = 0; a < ACR_GEN1_ARENA_COUNT; ++a) {
        const int places = import_places(w, a);
        if (s->a[a].places != places || !ladder_ok(&s->a[a])) {
            ladder_new(&s->a[a], places);
            ++reset;
        }
    }
    return reset;
}

int acr_gen1_arena_rank(const acr_gen1_arena *s, int a) {
    if (a < 0 || a >= ACR_GEN1_ARENA_COUNT) return 0;
    const acr_gen1_arena_ladder *l = &s->a[a];
    for (int i = 0; i < l->places; ++i)
        if (l->holder[i] == ACR_GEN1_ARENA_YOU) return i + 1;
    return 0;
}

int acr_gen1_arena_holder(const acr_gen1_arena *s, int a, int rank) {
    if (a < 0 || a >= ACR_GEN1_ARENA_COUNT) return 0;
    const acr_gen1_arena_ladder *l = &s->a[a];
    if (rank < 1 || rank > l->places || l->holder[rank - 1] == ACR_GEN1_ARENA_YOU) return 0;
    return l->holder[rank - 1];
}

int acr_gen1_arena_is_sub(int a, int rank) { return a == 0 && rank > ACR_GEN1_ARENA_RANKED; }

/* ---- what may be fought ---- */

const char *acr_gen1_arena_why_text(acr_gen1_arena_why why) {
    switch (why) {
    case ACR_GEN1_ARENA_OK:         return "";
    case ACR_GEN1_ARENA_NO_DATA:    return "NO ARENA DATA EXISTS.";
    case ACR_GEN1_ARENA_LOCKED:     return "YOU MAY NOT ENTER THIS ARENA NOW.";
    case ACR_GEN1_ARENA_WRONG_LEGS: return "REQUIRES A SPECIFIC LEG TYPE.";
    case ACR_GEN1_ARENA_REFERENCE:  return "REFERENCE ONLY.";
    case ACR_GEN1_ARENA_DONE:       return "UNABLE TO CHALLENGE AT THIS TIME.";
    case ACR_GEN1_ARENA_SUB_FIRST:  return "UNABLE TO CHALLENGE AT THIS TIME.";
    case ACR_GEN1_ARENA_INCOMPLETE: return "YOUR EQUIPMENT IS INCOMPLETE.";
    }
    return "";
}

static int topped(const acr_gen1_arena *s, int a) { return s->a[a].places && s->a[a].best == 1; }

acr_gen1_arena_why acr_gen1_arena_open(const acr_gen1_arena *s, int a) {
    if (a < 0 || a >= ACR_GEN1_ARENA_COUNT || !s->a[a].places) return ACR_GEN1_ARENA_NO_DATA;
    const int main_best = s->a[0].best;
    if (a == 0) return ACR_GEN1_ARENA_OK;
    if (a <= 4) return main_best && main_best <= ACR_GEN1_ARENA_RANKED ? ACR_GEN1_ARENA_OK : ACR_GEN1_ARENA_LOCKED;
    if (a <= 7) return main_best == 1 ? ACR_GEN1_ARENA_OK : ACR_GEN1_ARENA_LOCKED;
    return topped(s, 5) && topped(s, 6) && topped(s, 7) ? ACR_GEN1_ARENA_OK : ACR_GEN1_ARENA_LOCKED;
}

acr_gen1_arena_why acr_gen1_arena_can_challenge(const acr_gen1_arena *s, int a, int rank, int legs_kind,
                                                unsigned build_fail) {
    const acr_gen1_arena_why open = acr_gen1_arena_open(s, a);
    if (open != ACR_GEN1_ARENA_OK) return open;
    const acr_gen1_arena_ladder *l = &s->a[a];
    const int me = acr_gen1_arena_rank(s, a);
    if (rank < 1 || rank > l->places || rank >= me) return ACR_GEN1_ARENA_REFERENCE;
    const int foe = l->holder[rank - 1];
    if (acr_gen1_arena_is_sub(a, rank)) {
        if (l->beaten[foe - 1]) return ACR_GEN1_ARENA_DONE;
    } else if (a == 0 && me > ACR_GEN1_ARENA_RANKED + 1) {
        return ACR_GEN1_ARENA_SUB_FIRST;
    }
    if (a >= 1 && a <= 4 && legs_kind != a) return ACR_GEN1_ARENA_WRONG_LEGS;
    if (build_fail) return ACR_GEN1_ARENA_INCOMPLETE;
    return ACR_GEN1_ARENA_OK;
}

/* ---- a fight's result ---- */

static int all_beaten(const acr_gen1_arena_ladder *l) {
    for (int r = 1; r < l->places; ++r) if (!l->beaten[r - 1]) return 0;
    return 1;
}

int acr_gen1_arena_result(acr_gen1_arena *s, const acr_gen1_arena_world *w, int a, int rank, int won,
                          acr_gen1_arena_payout *out) {
    acr_gen1_arena_payout p;
    memset(&p, 0, sizeof p);
    p.prize_id = p.part_cat = p.part_index = -1;
    if (a < 0 || a >= ACR_GEN1_ARENA_COUNT) return 0;
    acr_gen1_arena_ladder *l = &s->a[a];
    const int me = acr_gen1_arena_rank(s, a);
    if (!me || rank < 1 || rank >= me) return 0;
    const int foe_rank = l->holder[rank - 1];
    const acr_gen1_rogue_foe *f = acr_gen1_arena_foe(w, a, foe_rank);
    p.old_rank = p.new_rank = me;
    if (l->challenges < 0xFFFFu) ++l->challenges;
    if (!won) {
        if (l->losses < 0xFFFFu) ++l->losses;
        if (!l->best) l->best = (uint8_t)me;
        if (out) *out = p;
        return 1;
    }
    p.won = 1;
    if (l->wins < 0xFFFFu) ++l->wins;
    /* The beaten opponent drops to just below the player, who moves up
     * one; everyone between moves up to fill its place. */
    for (int i = rank - 1; i < me - 2; ++i) l->holder[i] = l->holder[i + 1];
    l->holder[me - 2] = ACR_GEN1_ARENA_YOU;
    l->holder[me - 1] = (uint8_t)foe_rank;
    p.new_rank = me - 1;
    if (!l->best || p.new_rank < l->best) l->best = (uint8_t)p.new_rank;
    p.money = f ? f->prize_money : 0;
    const int first = !l->beaten[foe_rank - 1];
    l->beaten[foe_rank - 1] = 1;
    if (a == 5 || a == 6) {
        /* Champion and Guest: record 0's part once all are beaten. */
        const acr_gen1_rogue_foe *top = acr_gen1_arena_foe(w, a, 1);
        if (!l->set_given && all_beaten(l) && top && top->prize_part < ACR_GEN1_ARENA_PRIZES) {
            l->set_given = 1;
            p.prize_id = top->prize_part;
            p.set_prize = 1;
        }
    } else if (first && f && f->prize_part < ACR_GEN1_ARENA_PRIZES && !l->part_given[foe_rank - 1]) {
        l->part_given[foe_rank - 1] = 1;
        p.prize_id = f->prize_part;
    }
    if (p.prize_id >= 0 && !acr_gen1_arena_prize_part(p.prize_id, &p.part_cat, &p.part_index))
        p.part_cat = p.part_index = -1;
    if (out) *out = p;
    return 1;
}

void acr_gen1_arena_pay(acr_gen1_hangar *h, const acr_gen1_arena_payout *p) {
    if (!p->won) return;
    int64_t c = (int64_t)h->credits + p->money;
    if (c > ACR_GEN1_ARENA_CREDITS_MAX) c = ACR_GEN1_ARENA_CREDITS_MAX;
    h->credits = (int32_t)c;
    if (p->part_cat >= 0 && p->part_cat < ACR_GEN1_CAT_COUNT && p->part_index >= 0 && p->part_index < ACR_GEN1_PART_MAX) {
        /* Owned indexes 157-208 (back 0-51) are the paired back weapons:
         * the game adds one more copy for them. */
        const int add = p->part_cat == ACR_GEN1_CAT_BACK && p->part_index <= 208 - 157 ? 2 : 1;
        int n = h->owned[p->part_cat][p->part_index] + add;
        if (n > ACR_GEN1_OWN_MAX) n = ACR_GEN1_OWN_MAX;
        h->owned[p->part_cat][p->part_index] = (uint8_t)n;
    }
}

/* ---- save file ---- */

typedef struct outbuf { char *p; size_t size, len; } outbuf;

static void put(outbuf *o, const char *s) {
    for (; *s; ++s, ++o->len)
        if (o->len + 1 < o->size) o->p[o->len] = *s;
    if (o->size) o->p[o->len < o->size ? o->len : o->size - 1] = 0;
}

static void put_u(outbuf *o, unsigned long v) {
    char t[24];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    char s[24];
    for (int i = 0; i < n; ++i) s[i] = t[n - 1 - i];
    s[n] = 0;
    put(o, s);
}

static void put_kv(outbuf *o, const char *k, unsigned long v) { put(o, k); put(o, " = "); put_u(o, v); put(o, "\n"); }

static void put_list(outbuf *o, const char *k, const uint8_t *v, int n) {
    put(o, k); put(o, " = [");
    for (int i = 0; i < n; ++i) { if (i) put(o, ", "); put_u(o, v[i]); }
    put(o, "]\n");
}

size_t acr_gen1_arena_write(const acr_gen1_arena *s, char *buf, size_t size) {
    outbuf o = { buf, size, 0 };
    if (size) buf[0] = 0;
    put(&o, "# Armored-CoreR Arena standings (the garage is in its own save).\n");
    put_kv(&o, "format", ACR_GEN1_ARENA_FORMAT);
    put_kv(&o, "timer", s->timer);
    put_kv(&o, "stage", s->stage);
    for (int a = 0; a < ACR_GEN1_ARENA_COUNT; ++a) {
        const acr_gen1_arena_ladder *l = &s->a[a];
        if (!l->places) continue;
        put(&o, "[arena]\n");
        put_kv(&o, "number", (unsigned long)a);
        put_kv(&o, "places", l->places);
        put(&o, "# from rank 1 down: the opponent's disc rank, 255 = you\n");
        put_list(&o, "holder", l->holder, l->places);
        put_list(&o, "beaten", l->beaten, l->places);
        put_list(&o, "part_given", l->part_given, l->places);
        put_kv(&o, "set_given", l->set_given);
        put_kv(&o, "best", l->best);
        put_kv(&o, "challenges", l->challenges);
        put_kv(&o, "wins", l->wins);
        put_kv(&o, "losses", l->losses);
    }
    return o.len;
}

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
    while (v < end && *v >= '0' && *v <= '9' && x < 100000000L) x = x * 10 + (*v++ - '0');
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
        while (v < end && *v >= '0' && *v <= '9' && x < 100000L) x = x * 10 + (*v++ - '0');
        if (n < max) out[n] = (uint8_t)(x > 255 ? 255 : x);
        ++n;
    }
    return n;
}

int acr_gen1_arena_read(acr_gen1_arena *out, const char *text) {
    if (!text) return 0;
    const char *end = text + strlen(text);
    const char *first = strstr(text, "[arena]");
    const char *head_end = first ? first : end;
    if (get_num(text, head_end, "format", 0) != ACR_GEN1_ARENA_FORMAT) return 0;
    acr_gen1_arena s;
    memset(&s, 0, sizeof s);
    const long t = get_num(text, head_end, "timer", 0);
    s.timer = (uint8_t)(t >= 0 && t < ACR_GEN1_ARENA_TIMERS ? t : 0);
    s.stage = (uint8_t)get_num(text, head_end, "stage", 0);
    for (const char *b = first; b; ) {
        const char *next = strstr(b + 7, "[arena]");
        const char *be = next ? next : end;
        const long a = get_num(b, be, "number", -1);
        const long places = get_num(b, be, "places", 0);
        if (a >= 0 && a < ACR_GEN1_ARENA_COUNT && places >= 2 && places <= ACR_GEN1_ARENA_PLACES) {
            acr_gen1_arena_ladder *l = &s.a[a];
            l->places = (uint8_t)places;
            if (get_list(b, be, "holder", l->holder, ACR_GEN1_ARENA_PLACES) != places) return 0;
            if (get_list(b, be, "beaten", l->beaten, ACR_GEN1_ARENA_PLACES) != places) return 0;
            if (get_list(b, be, "part_given", l->part_given, ACR_GEN1_ARENA_PLACES) != places) return 0;
            l->set_given = (uint8_t)(get_num(b, be, "set_given", 0) != 0);
            const long best = get_num(b, be, "best", 0);
            l->best = (uint8_t)(best >= 0 && best <= places ? best : 0);
            l->challenges = (uint16_t)get_num(b, be, "challenges", 0);
            l->wins = (uint16_t)get_num(b, be, "wins", 0);
            l->losses = (uint16_t)get_num(b, be, "losses", 0);
            if (!ladder_ok(l)) return 0;
        }
        b = next;
    }
    *out = s;
    return 1;
}
