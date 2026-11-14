#include "acr_gen1_garage.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

acr_gen1_category acr_gen1_slot_category(acr_gen1_slot slot) {
    if (slot <= ACR_GEN1_SLOT_BACK_L) return (acr_gen1_category)slot;
    if (slot == ACR_GEN1_SLOT_BACK_R) return ACR_GEN1_CAT_BACK;
    return (acr_gen1_category)(slot - 1);
}

static int required(acr_gen1_slot s) {
    return s == ACR_GEN1_SLOT_HEAD || s == ACR_GEN1_SLOT_CORE || s == ACR_GEN1_SLOT_ARMS ||
           s == ACR_GEN1_SLOT_LEGS || s == ACR_GEN1_SLOT_GENERATOR || s == ACR_GEN1_SLOT_FCS;
}

void acr_gen1_hangar_new(acr_gen1_hangar *h, const uint8_t start[ACR_GEN1_SLOT_COUNT],
                         uint16_t start_options, int32_t credits) {
    memset(h, 0, sizeof *h);
    h->credits = credits;
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) {
        h->fitted[s] = start[s];
        if (start[s] < ACR_GEN1_PART_MAX) ++h->owned[acr_gen1_slot_category((acr_gen1_slot)s)][start[s]];
    }
    h->options = start_options;
    for (unsigned b = 0; b < ACR_GEN1_OPTION_MAX; ++b)
        if (start_options & (1u << b)) ++h->owned[ACR_GEN1_CAT_OPTION][b];
}

unsigned acr_gen1_fitted_copies(const acr_gen1_hangar *h, acr_gen1_category cat, unsigned index) {
    if (cat == ACR_GEN1_CAT_OPTION) return index < ACR_GEN1_OPTION_MAX && (h->options >> index) & 1u;
    unsigned n = 0;
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s)
        if (acr_gen1_slot_category((acr_gen1_slot)s) == cat && h->fitted[s] == index) ++n;
    return n;
}

unsigned acr_gen1_spare_copies(const acr_gen1_hangar *h, acr_gen1_category cat, unsigned index) {
    if (index >= ACR_GEN1_PART_MAX) return 0;
    const unsigned owned = h->owned[cat][index], fitted = acr_gen1_fitted_copies(h, cat, index);
    return owned > fitted ? owned - fitted : 0;
}

static const acr_gen1_catalog_part *find(const acr_gen1_catalog *c, acr_gen1_category cat, unsigned index) {
    if (index >= ACR_GEN1_PART_MAX || !c->part[cat][index].present) return NULL;
    return &c->part[cat][index];
}

acr_gen1_result acr_gen1_shop_buy(acr_gen1_hangar *h, const acr_gen1_catalog *c,
                                  acr_gen1_category cat, unsigned index) {
    if (h->credits < 0) return ACR_GEN1_IN_DEBT;
    const acr_gen1_catalog_part *p = find(c, cat, index);
    if (!p || !p->for_sale) return ACR_GEN1_NOT_IN_SHOP;
    if (h->owned[cat][index] >= ACR_GEN1_OWN_MAX) return ACR_GEN1_STOCK_FULL;
    if ((int64_t)h->credits < (int64_t)p->price) return ACR_GEN1_NO_CREDITS;
    h->credits -= (int32_t)p->price;
    ++h->owned[cat][index];
    return ACR_GEN1_OK;
}

acr_gen1_result acr_gen1_shop_sell(acr_gen1_hangar *h, const acr_gen1_catalog *c,
                                   acr_gen1_category cat, unsigned index) {
    if (h->credits < 0) return ACR_GEN1_IN_DEBT;
    const acr_gen1_catalog_part *p = find(c, cat, index);
    if (!p) return ACR_GEN1_NOT_IN_SHOP;
    if (!acr_gen1_spare_copies(h, cat, index)) return ACR_GEN1_NONE_SPARE;
    const int64_t back = (int64_t)p->price * c->sell_percent / 100;
    const int64_t total = (int64_t)h->credits + back;
    h->credits = total > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)total;
    --h->owned[cat][index];
    return ACR_GEN1_OK;
}

void acr_gen1_garage_build(const acr_gen1_hangar *h, const acr_gen1_catalog *c, acr_gen1_fit_build *out) {
    memset(out, 0, sizeof *out);
    acr_gen1_part *by_slot[ACR_GEN1_SLOT_COUNT] = {
        &out->parts.core, &out->parts.booster, &out->parts.head, &out->parts.arms, &out->parts.blade,
        &out->parts.hand, &out->parts.back_l, &out->parts.back_r, &out->parts.generator,
        &out->parts.fcs, &out->parts.legs };
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) {
        const acr_gen1_catalog_part *p = find(c, acr_gen1_slot_category((acr_gen1_slot)s), h->fitted[s]);
        if (!p) continue;
        *by_slot[s] = p->stats;
        by_slot[s]->present = 1;
        out->flags[s] = p->flags;
        if (s == ACR_GEN1_SLOT_CORE) out->core_option_slots = p->slots;
    }
    for (unsigned b = 0; b < ACR_GEN1_OPTION_MAX; ++b) {
        const acr_gen1_catalog_part *o = find(c, ACR_GEN1_CAT_OPTION, b);
        out->option_cost[b] = o ? o->slots : 0;
    }
    out->parts.option_mask = h->options;
}

unsigned acr_gen1_garage_check(const acr_gen1_hangar *h, const acr_gen1_catalog *c,
                               acr_gen1_fit_numbers *out) {
    acr_gen1_fit_build b;
    acr_gen1_garage_build(h, c, &b);
    return acr_gen1_fit_check(&b, out);
}

acr_gen1_result acr_gen1_garage_fit(acr_gen1_hangar *h, const acr_gen1_catalog *c,
                                    acr_gen1_slot slot, unsigned index, uint32_t *removed) {
    if (removed) *removed = 0;
    if (index == ACR_GEN1_NO_PART) {
        if (required(slot)) return ACR_GEN1_REQUIRED;
        h->fitted[slot] = ACR_GEN1_NO_PART;
        return ACR_GEN1_OK;
    }
    const acr_gen1_category cat = acr_gen1_slot_category(slot);
    if (!find(c, cat, index)) return ACR_GEN1_NOT_IN_SHOP;
    if (h->fitted[slot] == index) return ACR_GEN1_ALREADY_FITTED;
    if (!acr_gen1_spare_copies(h, cat, index)) return ACR_GEN1_NONE_SPARE;

    acr_gen1_hangar t = *h;
    t.fitted[slot] = (uint8_t)index;
    acr_gen1_fit_build b;
    acr_gen1_garage_build(&t, c, &b);
    const uint32_t off = acr_gen1_fit_repair(&b, slot);
    if (off & (1u << slot)) return ACR_GEN1_NOT_ALLOWED;
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s)
        if (off & (1u << s)) t.fitted[s] = ACR_GEN1_NO_PART;
    *h = t;
    if (removed) *removed = off;
    return ACR_GEN1_OK;
}

acr_gen1_result acr_gen1_garage_option(acr_gen1_hangar *h, const acr_gen1_catalog *c,
                                       unsigned bit, int on) {
    if (bit >= ACR_GEN1_OPTION_MAX) return ACR_GEN1_NOT_IN_SHOP;
    const uint16_t m = (uint16_t)(1u << bit);
    if (!on) { h->options &= (uint16_t)~m; return ACR_GEN1_OK; }
    const acr_gen1_catalog_part *o = find(c, ACR_GEN1_CAT_OPTION, bit);
    if (!o) return ACR_GEN1_NOT_IN_SHOP;
    if (h->options & m) return ACR_GEN1_ALREADY_FITTED;
    if (!acr_gen1_spare_copies(h, ACR_GEN1_CAT_OPTION, bit)) return ACR_GEN1_NONE_SPARE;
    acr_gen1_fit_build b;
    acr_gen1_garage_build(h, c, &b);
    unsigned used = 0;
    for (unsigned i = 0; i < ACR_GEN1_OPTION_MAX; ++i)
        if (h->options & (1u << i)) used += b.option_cost[i];
    if (used + o->slots > b.core_option_slots) return ACR_GEN1_NO_OPTION_SLOTS;
    h->options |= m;
    return ACR_GEN1_OK;
}

/* ---------------- save file ---------------- */

static const char *const k_slot_key[ACR_GEN1_SLOT_COUNT] = {
    "core", "booster", "head", "arms", "blade", "hand", "back_l", "back_r", "generator", "fcs", "legs"
};
static const char *const k_cat_key[ACR_GEN1_CAT_COUNT] = {
    "core", "booster", "head", "arms", "blade", "hand", "back", "generator", "fcs", "legs", "option"
};

typedef struct out_buf { char *p; size_t size, len; } out_buf;

static void put(out_buf *o, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const size_t room = o->len < o->size ? o->size - o->len : 0;
    const int n = vsnprintf(room ? o->p + o->len : NULL, room, fmt, ap);
    va_end(ap);
    if (n > 0) o->len += (size_t)n;
}

/* Names go between quotes: keep them to printable characters without quotes. */
static void put_name(out_buf *o, const char *key, const char *name) {
    char clean[20];
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof clean; ++i) {
        const unsigned char ch = (unsigned char)name[i];
        clean[i] = (char)(ch < 32 || ch > 126 || ch == '"' || ch == '\\' ? '_' : ch);
    }
    clean[i] = 0;
    put(o, "%-10s = \"%s\"\n", key, clean);
}

size_t acr_gen1_save_write(const acr_gen1_hangar *h, char *buf, size_t size) {
    out_buf o = { buf, size, 0 };
    if (size) buf[0] = 0;
    put(&o, "# Armored-CoreR save. Plain text: it can be read and edited, but keep the layout.\n");
    put(&o, "format     = %d\n", ACR_GEN1_SAVE_FORMAT);
    put(&o, "generation = \"gen1\"\n");
    put_name(&o, "ac_name", h->ac_name);
    put_name(&o, "pilot_name", h->pilot_name);
    put(&o, "credits    = %ld\n", (long)h->credits);
    put(&o, "colours    = [");
    for (int k = 0; k < 12; ++k) put(&o, k ? ", %d" : "%d", h->colours[k / 3][k % 3]);
    put(&o, "]   # 4 colour slots as red, green, blue 0..63\n");
    put(&o, "\n[ac]   # fitted parts by part number; -1 = none\n");
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s)
        put(&o, "%-10s = %d\n", k_slot_key[s], h->fitted[s] == ACR_GEN1_NO_PART ? -1 : h->fitted[s]);
    put(&o, "%-10s = [", "options");
    for (unsigned b = 0, first = 1; b < ACR_GEN1_OPTION_MAX; ++b)
        if (h->options & (1u << b)) { put(&o, first ? "%u" : ", %u", b); first = 0; }
    put(&o, "]\n");
    put(&o, "\n[owned]   # part numbers, once per copy (fitted ones included)\n");
    for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c) {
        put(&o, "%-10s = [", k_cat_key[c]);
        int first = 1;
        for (int i = 0; i < ACR_GEN1_PART_MAX; ++i)
            for (int n = 0; n < h->owned[c][i]; ++n) { put(&o, first ? "%d" : ", %d", i); first = 0; }
        put(&o, "]\n");
    }
    return o.len;
}

/* ---- reading ---- */

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

/* Parses "[a, b, c]" calling back for each number; 0 if malformed. */
typedef int (*list_fn)(void *user, long v);

static int parse_list(const char *p, list_fn fn, void *user) {
    p = skip_space(p);
    if (*p != '[') return 0;
    ++p;
    for (;;) {
        p = skip_space(p);
        if (*p == ']') return 1;
        char *end;
        const long v = strtol(p, &end, 10);
        if (end == p || !fn(user, v)) return 0;
        p = skip_space(end);
        if (*p == ',') ++p;
        else if (*p != ']') return 0;
    }
}

typedef struct own_ctx { acr_gen1_hangar *h; int cat; } own_ctx;
static int add_owned(void *user, long v) {
    own_ctx *c = user;
    if (v < 0 || v >= ACR_GEN1_PART_MAX || c->h->owned[c->cat][v] >= ACR_GEN1_OWN_MAX) return 0;
    ++c->h->owned[c->cat][v];
    return 1;
}
static int add_option(void *user, long v) {
    acr_gen1_hangar *h = user;
    if (v < 0 || v >= ACR_GEN1_OPTION_MAX) return 0;
    h->options |= (uint16_t)(1u << v);
    return 1;
}
typedef struct colour_ctx { acr_gen1_hangar *h; int n; } colour_ctx;
static int add_colour(void *user, long v) {
    colour_ctx *c = user;
    if (c->n >= 12 || v < 0 || v > 63) return 0;
    c->h->colours[c->n / 3][c->n % 3] = (uint8_t)v;
    ++c->n;
    return 1;
}

static int parse_string(const char *p, char *out, size_t size) {
    p = skip_space(p);
    if (*p != '"') return 0;
    ++p;
    size_t i = 0;
    while (*p && *p != '"' && *p != '\n') {
        if (i + 1 < size) out[i++] = *p;
        ++p;
    }
    out[i] = 0;
    return *p == '"';
}

int acr_gen1_save_read(acr_gen1_hangar *h, const char *text) {
    acr_gen1_hangar t;
    memset(&t, 0, sizeof t);
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) t.fitted[s] = ACR_GEN1_NO_PART;
    char section[16] = "";
    long format = -1;
    int gen1 = 0;
    for (const char *line = text; line && *line; ) {
        const char *next = strchr(line, '\n');
        const char *p = skip_space(line);
        if (*p == '[') {
            size_t i = 0;
            for (++p; *p && *p != ']' && *p != '\n' && i + 1 < sizeof section; ++p) section[i++] = *p;
            section[i] = 0;
        } else if (*p && *p != '#' && *p != '\n' && *p != '\r') {
            char key[16];
            size_t i = 0;
            while (*p && *p != ' ' && *p != '=' && *p != '\n' && i + 1 < sizeof key) key[i++] = *p++;
            key[i] = 0;
            p = skip_space(p);
            if (*p != '=') return 0;
            p = skip_space(p + 1);
            int ok = 1;
            if (!section[0]) {
                if (!strcmp(key, "format")) format = strtol(p, NULL, 10);
                else if (!strcmp(key, "generation")) {
                    char g[8];
                    gen1 = parse_string(p, g, sizeof g) && !strcmp(g, "gen1");
                } else if (!strcmp(key, "ac_name")) ok = parse_string(p, t.ac_name, sizeof t.ac_name);
                else if (!strcmp(key, "pilot_name")) ok = parse_string(p, t.pilot_name, sizeof t.pilot_name);
                else if (!strcmp(key, "credits")) t.credits = (int32_t)strtol(p, NULL, 10);
                else if (!strcmp(key, "colours")) {
                    colour_ctx cc = { &t, 0 };
                    ok = parse_list(p, add_colour, &cc) && cc.n == 12;
                }
            } else if (!strcmp(section, "ac")) {
                if (!strcmp(key, "options")) ok = parse_list(p, add_option, &t);
                else
                    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s)
                        if (!strcmp(key, k_slot_key[s])) {
                            const long v = strtol(p, NULL, 10);
                            if (v >= ACR_GEN1_PART_MAX || v < -1) ok = 0;
                            else t.fitted[s] = (uint8_t)(v < 0 ? ACR_GEN1_NO_PART : v);
                        }
            } else if (!strcmp(section, "owned")) {
                for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c)
                    if (!strcmp(key, k_cat_key[c])) {
                        own_ctx oc = { &t, c };
                        ok = parse_list(p, add_owned, &oc);
                    }
            }
            if (!ok) return 0;
        }
        line = next ? next + 1 : NULL;
    }
    /* Later formats get an upgrade step here; newer ones than this build can't be read. */
    if (format < 1 || format > ACR_GEN1_SAVE_FORMAT || !gen1) return 0;
    /* A hand-edited file may list fewer copies than are fitted: own what is fitted. */
    for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c)
        for (unsigned i = 0; i < ACR_GEN1_PART_MAX; ++i) {
            const unsigned f = acr_gen1_fitted_copies(&t, (acr_gen1_category)c, i);
            if (t.owned[c][i] < f) t.owned[c][i] = (uint8_t)f;
        }
    *h = t;
    return 1;
}
