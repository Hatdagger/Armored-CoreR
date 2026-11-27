/* Gen 1 missions: list, sortie check, report and record. See
 * acr_gen1_missions.h for where each rule comes from. */
#include "acr_gen1_missions.h"
#include "acr_gen1_mission.h"
#include "acr_gen1_battle.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Armored-CoreR's own missions ----
 * P4 "Qualifier", variant 1 (Standard) as the user designed it (mission
 * mockups page and docs/MISSIONS.md, 2026-11-20): Raven's Nest, mission map
 * 11 (the four-pillar hall), 10,000 C, 3:00, a 1v1 against a Nest examiner,
 * won by destruction or by more AP left when time runs out. The last page
 * follows MoA's "Theater of operations" page with the mockup's own facts.
 * Its other two variants are below (acr_gen1_mission_variant).
 *
 * The examiner's AC is the user's AI Battle card (Raven Card Builder,
 * format 4, checksum good; docs/missions/p4-examiner-card.png, 2026-11-25):
 * XCA-00, HD-REDEYE, AN-201, LN-1001, WG-MGA1, LS-200G, WC-CN35 (back
 * left), WM-X201 (back right), COMDEX-G0, GBG-10000, B-HP25, no options,
 * no Human PLUS, the BALANCED AI. Colours are the card's paint as the arena
 * record takes it (record bytes 16-27). Names (the user, 2026-11-25): pilot
 * NEST EXAMINER, AC Anfang (the card had the builder's defaults). */
static const uint8_t k_p4_foe[ACR_GEN1_SLOT_COUNT] = {
    [ACR_GEN1_SLOT_CORE] = 0, [ACR_GEN1_SLOT_BOOSTER] = 6, [ACR_GEN1_SLOT_HEAD] = 3, [ACR_GEN1_SLOT_ARMS] = 1,
    [ACR_GEN1_SLOT_BLADE] = 1, [ACR_GEN1_SLOT_HAND] = 2, [ACR_GEN1_SLOT_BACK_L] = 28, [ACR_GEN1_SLOT_BACK_R] = 7,
    [ACR_GEN1_SLOT_GENERATOR] = 5, [ACR_GEN1_SLOT_FCS] = 1, [ACR_GEN1_SLOT_LEGS] = 0,
};
static const uint8_t k_p4_foe_colours[4][3] = { { 37, 37, 35 }, { 26, 23, 23 }, { 43, 21, 12 }, { 10, 12, 15 } };

static void add_own(acr_gen1_mission_list *l) {
    acr_gen1_mission_info *m = &l->m[l->count++];
    memset(m, 0, sizeof *m);
    strcpy(m->id, "P4");
    strcpy(m->name, "Qualifier");
    strcpy(m->requester, "Raven's Nest");
    m->reward = 10000;
    m->map = 11;
    m->own = 1;
    m->playable = 1;
    m->time_ms = 3 * 60 * 1000;
    strcpy(m->page[0], "The Nest confirms a pilot's skill in a\n"
                       "direct test before it forwards \n"
                       "higher-grade contracts. Your opponent\n"
                       "is a @1Nest examiner@0.");
    strcpy(m->page[1], "Win, or keep more AP than the \n"
                       "examiner when @1time runs out@0.");
    strcpy(m->page[2], "Theater of operations: \n"
                       "  Nest test site (pillar hall)\n"
                       "Enemy forces:\n"
                       "  AC (Nest examiner) x1\n"
                       "Objective: Defeat Examiner\n"
                       "Time limit: 3:00");
    m->pages = 3;
    m->foe_set = 1;
    memcpy(m->foe_parts, k_p4_foe, sizeof m->foe_parts);
    m->foe_options = 0;
    memcpy(m->foe_colours, k_p4_foe_colours, sizeof m->foe_colours);
    strcpy(m->foe_pilot, "NEST EXAMINER");
    strcpy(m->foe_ac, "ANFANG");
    m->variants = 3;
}

/* ---- variants ----
 * P4 from the user's mockups (mission mockups page, 2026-11-20) and their
 * answers (2026-11-25):
 * - 2 Alternate layout: 12,000 C, 3:00. The player starts in the south-west
 *   corner, the examiner behind the north-east pillar, two drone MTs in the
 *   far (north) corners; only the examiner counts. The drones are MoA's
 *   small drone U10 with map 13's numbers (records 11-14). Places: the
 *   mockup's marks on map 11 (the hall's floor is +-11,800 across the
 *   mockup's picture, pillars at +-5,700), moved clear of the pillar and
 *   each other.
 * - 3 Altered circumstances: the same examiner (the user: "the same
 *   examiner I provided before"). Below half AP it withdraws and a
 *   challenger drops in from the east; the Nest's radio says "Continue the
 *   test." The challenger is a MoA arena Raven while versus rivals and
 *   ghosts don't exist: The Wolf (pilot Prime, main arena rank 14, no
 *   Human PLUS), flown by its own AI. 10,000 C, plus 12,000 C (special
 *   addition) for beating it. The briefing is variant 1's, so the clock
 *   starts at 3:00 and the challenger brings 2:00 more (the mockup's 5:00
 *   in all). */
const acr_gen1_mission_spot acr_gen1_p4_alt_start[2] = {
    { -8400, 3000, -8400, 512 },     /* south-west corner, facing the middle */
    {  7400, 3000,  7400, 2560 },    /* behind the north-east pillar, facing the player */
};
const acr_gen1_mission_spot acr_gen1_p4_alt_drone[2] = {
    {  9300, 3000,  9600, 2560 },    /* north-east corner */
    { -9300, 3000,  9600, 1536 },    /* north-west corner */
};
const acr_gen1_mission_spot acr_gen1_p4_challenger_drop = { 8000, 3000, 0, 3072 };   /* east side, facing west */

int acr_gen1_mission_roll(uint32_t r) {
    const uint32_t n = r % 100u;
    return n < 40 ? 1 : n < 80 ? 2 : 3;
}

const char *acr_gen1_mission_variant_name(int v) {
    return v == 2 ? "ALTERNATE LAYOUT" : v == 3 ? "ALTERED CIRCUMSTANCES" : "STANDARD";
}

void acr_gen1_mission_variant(const acr_gen1_mission_info *m, int v, acr_gen1_mission_info *out) {
    if (out != m) *out = *m;
    if (!m->variants || v < 1 || v > m->variants) v = 1;
    out->variant = (uint8_t)v;
    out->briefed = (uint8_t)(v == 3 ? 1 : v);
    out->rule = 0;
    out->bonus = 0;
    out->extra_ms = 0;
    out->late_ac[0] = 0;
    if (strcmp(m->id, "P4") || v == 1) return;
    if (v == 2) {
        out->reward = 12000;
        out->rule = ACR_GEN1_OWN_P4_ALTERNATE;
        strcpy(out->page[0], "Second form of the test. You start\n"
                             "in a corner, and the @1Nest examiner@0\n"
                             "starts behind cover.");
        strcpy(out->page[1], "Two @1drone MTs@0 add pressure. Only\n"
                             "the examiner counts: win, or keep\n"
                             "more AP than it when @1time runs out@0.");
        strcpy(out->page[2], "Theater of operations: \n"
                             "  Nest test site (pillar hall)\n"
                             "Enemy forces:\n"
                             "  AC (Nest examiner) x1\n"
                             "  MT (drone) x2\n"
                             "Objective: Defeat Examiner\n"
                             "Time limit: 3:00");
        out->pages = 3;
        return;
    }
    /* v == 3: briefed as Standard (pages and reward stay variant 1's). */
    out->rule = ACR_GEN1_OWN_P4_ALTERED;
    out->bonus = 12000;
    out->extra_ms = 2 * 60 * 1000;
    strcpy(out->late_ac, "The Wolf");
}

/* ---- missions.toml ---- */

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

/* A TOML basic string at p (after the quote is checked); returns past it. */
static const char *read_string(const char *p, char *out, size_t cap) {
    size_t o = 0;
    if (*p != '"') { if (cap) out[0] = 0; return p; }
    for (++p; *p && *p != '"' && *p != '\n'; ++p) {
        char c = *p;
        if (c == '\\' && p[1]) {
            ++p;
            c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
        }
        if (o + 1 < cap) out[o++] = c;
    }
    if (cap) out[o] = 0;
    return *p == '"' ? p + 1 : p;
}

int acr_gen1_missions_build(acr_gen1_mission_list *list, const char *toml) {
    memset(list, 0, sizeof *list);
    add_own(list);
    return toml ? acr_gen1_missions_append(list, toml) : 0;
}

int acr_gen1_missions_append(acr_gen1_mission_list *list, const char *toml) {
    if (!toml) return 0;
    /* Project Phantasma's list (missions-pp.toml) says game = "pp" at its top. */
    const char *pp = strstr(toml, "\ngame = \"pp\"");
    const char *prefix = pp && (!strstr(toml, "[[mission]]") || pp < strstr(toml, "[[mission]]")) ? "PP" : "MOA";
    int read = 0, in_pages = 0;
    acr_gen1_mission_info *m = NULL;
    for (const char *line = toml; line && *line; ) {
        const char *next = strchr(line, '\n');
        const char *p = skip_space(line);
        if (!strncmp(p, "[[mission]]", 11)) {
            in_pages = 0;
            m = list->count < ACR_GEN1_MISSIONS_MAX ? &list->m[list->count++] : NULL;
            if (m) {
                memset(m, 0, sizeof *m);
                m->map = -1;
                ++read;
            }
        } else if (m && in_pages) {
            if (*p == ']') in_pages = 0;
            else if (*p == '"' && m->pages < ACR_GEN1_MISSION_PAGES)
                read_string(p, m->page[m->pages++], ACR_GEN1_MISSION_TEXT);
        } else if (m && *p && *p != '#') {
            char key[16];
            size_t k = 0;
            while (*p && *p != ' ' && *p != '=' && *p != '\n' && k + 1 < sizeof key) key[k++] = *p++;
            key[k] = 0;
            p = skip_space(p);
            if (*p == '=') {
                p = skip_space(p + 1);
                if (!strcmp(key, "slot")) {
                    const long s = strtol(p, NULL, 10);
                    snprintf(m->id, sizeof m->id, "%s-%02ld", prefix, s < 0 || s > 99 ? 0L : s);
                } else if (!strcmp(key, "name")) read_string(p, m->name, sizeof m->name);
                else if (!strcmp(key, "sender")) read_string(p, m->sender, sizeof m->sender);
                else if (!strcmp(key, "requester")) read_string(p, m->requester, sizeof m->requester);
                else if (!strcmp(key, "reward")) m->reward = (int32_t)strtol(p, NULL, 10);
                else if (!strcmp(key, "map")) m->map = (int16_t)strtol(p, NULL, 10);
                else if (!strcmp(key, "pictures")) m->pictures = (uint8_t)strtol(p, NULL, 10);
                else if (!strcmp(key, "pages")) {
                    /* Pages one per line until "]"; a one-line list is read here. */
                    p = skip_space(p + 1);
                    while (*p == '"' && m->pages < ACR_GEN1_MISSION_PAGES) {
                        p = skip_space(read_string(p, m->page[m->pages++], ACR_GEN1_MISSION_TEXT));
                        if (*p == ',') p = skip_space(p + 1);
                    }
                    in_pages = *p != ']';
                }
            }
        }
        line = next ? next + 1 : NULL;
    }
    /* Master of Arena's missions fly on their own maps since 2026-11-27:
     * the player alone against the map's units, by the map's script
     * (acr_gen1_mission.h). Maps 3 and 14 run the game's mission clock. */
    for (int i = list->count - read; i < list->count; ++i) {
        acr_gen1_mission_info *mi = &list->m[i];
        if (mi->own || mi->playable || !acr_gen1_mission_map_ok(mi->map)) continue;
        mi->playable = 1;
        mi->time_ms = (int32_t)((int64_t)acr_gen1_mission_clock_ticks(mi->map) * 1000 / 22);
    }
    return read;
}

/* ---- sortie ---- */

acr_gen1_sortie acr_gen1_sortie_from_fail(unsigned fail) {
    if (fail & (ACR_GEN1_FAIL_MISSING_PART | ACR_GEN1_FAIL_NO_BOOSTER)) return ACR_GEN1_SORTIE_INCOMPLETE;
    if (fail & (ACR_GEN1_FAIL_ARM_LOAD | ACR_GEN1_FAIL_WEIGHT)) return ACR_GEN1_SORTIE_WEIGHT;
    if (fail & ACR_GEN1_FAIL_ENERGY) return ACR_GEN1_SORTIE_ENERGY;
    return ACR_GEN1_SORTIE_OK;
}

const char *acr_gen1_sortie_message(acr_gen1_sortie s) {
    switch (s) {
    case ACR_GEN1_SORTIE_INCOMPLETE: return "Your equipment is incomplete.";
    case ACR_GEN1_SORTIE_WEIGHT: return "You exceed the weight limit.";
    case ACR_GEN1_SORTIE_ENERGY: return "You don't have enough energy.";
    default: return NULL;
    }
}

/* ---- report ---- */

int32_t acr_gen1_mission_ac_price(const acr_gen1_hangar *h, const acr_gen1_catalog *c) {
    int64_t sum = 0;
    for (int s = 0; s < ACR_GEN1_SLOT_COUNT; ++s) {
        const unsigned p = h->fitted[s];
        /* Every filled slot, as 0x800976E0 adds them: a two-slot back unit
         * sits in both back slots and is counted twice. */
        if (p == ACR_GEN1_NO_PART || p >= ACR_GEN1_PART_MAX) continue;
        sum += c->part[acr_gen1_slot_category((acr_gen1_slot)s)][p].price;
    }
    return sum > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)sum;
}

int32_t acr_gen1_mission_ammo_cost(const acr_gen1_fighter *f) {
    int64_t sum = 0;
    for (int k = 0; k < f->weapons && k < ACR_GEN1_MAX_WEAPONS; ++k) {
        const acr_gen1_weapon *w = &f->weapon[k];
        if (!w->solid || w->ammo <= f->ammo[k]) continue;
        sum += (int64_t)(w->ammo - f->ammo[k]) * w->ammo_price;
    }
    return sum > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)sum;
}

int32_t acr_gen1_mission_repair_cost(int32_t ap_max, int32_t ap_left, int32_t ac_price) {
    if (ap_left < 0) ap_left = 0;
    if (ap_left >= ap_max || ac_price <= 0) return 0;
    const int64_t lost = (int64_t)ap_max - ap_left;
    return (int32_t)(lost * (ac_price / 100) / 8000);
}

void acr_gen1_mission_settle(acr_gen1_hangar *h, acr_gen1_mission_report *r, int success, int32_t reward,
                             int32_t addition, int32_t ammo_cost, int32_t repair_cost, int32_t deduction) {
    memset(r, 0, sizeof *r);
    r->success = (uint8_t)(success != 0);
    r->reward = success ? reward : 0;
    r->addition = success ? addition : 0;
    r->ammo = -ammo_cost;
    r->repair = -repair_cost;
    r->deduction = -deduction;
    r->balance = r->reward + r->addition + r->ammo + r->repair + r->deduction;
    int64_t c = (int64_t)h->credits + r->balance;
    if (c > ACR_GEN1_CREDITS_TOP) c = ACR_GEN1_CREDITS_TOP;
    if (c < ACR_GEN1_CREDITS_BOTTOM) c = ACR_GEN1_CREDITS_BOTTOM;
    h->credits = (int32_t)c;
    r->credits = h->credits;
}

/* ---- record ---- */

static int find(const acr_gen1_mission_record *rec, const char *id) {
    for (int i = 0; i < rec->n; ++i)
        if (!strcmp(rec->id[i], id)) return i;
    return -1;
}

static uint8_t *flags_of(acr_gen1_mission_record *rec, const char *id) {
    int i = find(rec, id);
    if (i < 0) {
        if (rec->n >= ACR_GEN1_MISSION_RECORD_MAX || !id[0]) return NULL;
        i = rec->n++;
        snprintf(rec->id[i], sizeof rec->id[i], "%s", id);
        rec->flags[i] = 0;
    }
    return &rec->flags[i];
}

static void bump(uint16_t *v) { if (*v < ACR_GEN1_MISSION_COUNT_CAP) ++*v; }

void acr_gen1_mission_book(acr_gen1_mission_record *rec, const char *id, int success) {
    bump(&rec->sorties);
    bump(success ? &rec->successes : &rec->failures);
    uint8_t *f = flags_of(rec, id);
    if (f) *f |= success ? ACR_GEN1_MISSION_DONE_BIT : ACR_GEN1_MISSION_FAILED_BIT;
}

unsigned acr_gen1_mission_flags(const acr_gen1_mission_record *rec, const char *id) {
    const int i = find(rec, id);
    return i < 0 ? 0u : rec->flags[i];
}

typedef struct out_buf { char *b; size_t size, len; } out_buf;

static void put(out_buf *o, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const size_t room = o->len < o->size ? o->size - o->len : 0;
    const int n = vsnprintf(room ? o->b + o->len : NULL, room, fmt, ap);
    va_end(ap);
    if (n > 0) o->len += (size_t)n;
}

static void put_ids(out_buf *o, const acr_gen1_mission_record *rec, unsigned bit) {
    int first = 1;
    put(o, "[");
    for (int i = 0; i < rec->n; ++i)
        if (rec->flags[i] & bit) { put(o, first ? "\"%s\"" : ", \"%s\"", rec->id[i]); first = 0; }
    put(o, "]\n");
}

size_t acr_gen1_mission_record_write(const acr_gen1_mission_record *rec, char *buf, size_t size) {
    out_buf o = { buf, size, 0 };
    if (size) buf[0] = 0;
    put(&o, "\n[missions]   # MISSIONS: sorties, results, and each mission completed / failed\n");
    put(&o, "sorties    = %u\n", rec->sorties);
    put(&o, "success    = %u\n", rec->successes);
    put(&o, "failure    = %u\n", rec->failures);
    put(&o, "completed  = ");
    put_ids(&o, rec, ACR_GEN1_MISSION_DONE_BIT);
    put(&o, "failed     = ");
    put_ids(&o, rec, ACR_GEN1_MISSION_FAILED_BIT);
    return o.len;
}

static uint16_t count_of(const char *p) {
    const long v = strtol(p, NULL, 10);
    return (uint16_t)(v < 0 ? 0 : v > ACR_GEN1_MISSION_COUNT_CAP ? ACR_GEN1_MISSION_COUNT_CAP : v);
}

static void read_ids(acr_gen1_mission_record *rec, const char *p, unsigned bit) {
    p = skip_space(p);
    if (*p != '[') return;
    for (p = skip_space(p + 1); *p == '"'; ) {
        char id[ACR_GEN1_MISSION_ID];
        p = skip_space(read_string(p, id, sizeof id));
        uint8_t *f = flags_of(rec, id);
        if (f) *f |= (uint8_t)bit;
        if (*p == ',') p = skip_space(p + 1);
    }
}

void acr_gen1_mission_record_read(acr_gen1_mission_record *rec, const char *save_text) {
    memset(rec, 0, sizeof *rec);
    int in = 0;
    for (const char *line = save_text; line && *line; ) {
        const char *next = strchr(line, '\n');
        const char *p = skip_space(line);
        if (*p == '[') in = !strncmp(p, "[missions]", 10);
        else if (in && *p && *p != '#') {
            char key[16];
            size_t k = 0;
            while (*p && *p != ' ' && *p != '=' && *p != '\n' && k + 1 < sizeof key) key[k++] = *p++;
            key[k] = 0;
            p = skip_space(p);
            if (*p == '=') {
                p = skip_space(p + 1);
                if (!strcmp(key, "sorties")) rec->sorties = count_of(p);
                else if (!strcmp(key, "success")) rec->successes = count_of(p);
                else if (!strcmp(key, "failure")) rec->failures = count_of(p);
                else if (!strcmp(key, "completed")) read_ids(rec, p, ACR_GEN1_MISSION_DONE_BIT);
                else if (!strcmp(key, "failed")) read_ids(rec, p, ACR_GEN1_MISSION_FAILED_BIT);
            }
        }
        line = next ? next + 1 : NULL;
    }
}
