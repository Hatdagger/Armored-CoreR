#include "demo_data.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

/* ---- a very small reader for the files acr-import writes ---- */

/* Value text after "key =" on its own line within [start, end), or NULL. */
static const char *find_key(const char *start, const char *end, const char *key) {
    const size_t n = strlen(key);
    for (const char *p = start; p && p < end; ) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        if (!line_end) line_end = end;
        if ((size_t)(line_end - p) > n && strncmp(p, key, n) == 0 &&
            (p[n] == ' ' || p[n] == '=')) {
            const char *v = p + n;
            while (v < line_end && (*v == ' ' || *v == '=')) ++v;
            return v;
        }
        p = line_end + 1;
    }
    return NULL;
}

static long get_int(const char *start, const char *end, const char *key, long fallback) {
    const char *v = find_key(start, end, key);
    return v ? strtol(v, NULL, 10) : fallback;
}

static void get_string(const char *start, const char *end, const char *key, char *out, size_t size) {
    out[0] = 0;
    const char *v = find_key(start, end, key);
    if (!v || *v != '"') return;
    ++v;
    size_t i = 0;
    while (v[i] && v[i] != '"' && i + 1 < size) { out[i] = v[i]; ++i; }
    out[i] = 0;
}

/* "[a, b, c]" -> 1 if `value` is in it. */
static int list_has(const char *start, const char *end, const char *key, long value) {
    const char *v = find_key(start, end, key);
    if (!v || *v != '[') return 0;
    for (++v; *v && *v != ']'; ) {
        char *next;
        const long x = strtol(v, &next, 10);
        if (next == v) { ++v; continue; }
        if (x == value) return 1;
        v = next;
    }
    return 0;
}

static char *load_text(const char *path) {
    return SDL_LoadFile(path, NULL);   /* SDL adds a terminating zero */
}

/* ---- parts ---- */

static const char *const k_category[SLOT_COUNT] = {
    "core", "booster", "head", "arms", "blade", "hand", "back", "back", "generator", "fcs", "legs"
};

/* Finds parts/<category>/.../NNN-*.toml for a game index, looking into
 * sub-folders (arms/regular, legs/humanoid...) itself so it works the same
 * on every system. */
typedef struct part_search { char prefix[8]; char found[1400]; int depth; } part_search;

static SDL_EnumerationResult SDLCALL search_cb(void *user, const char *dirname, const char *fname) {
    part_search *ps = user;
    char path[1400];
    const size_t n = strlen(dirname);
    const int has_sep = n && (dirname[n - 1] == '/' || dirname[n - 1] == '\\');
    SDL_snprintf(path, sizeof path, "%s%s%s", dirname, has_sep ? "" : "/", fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (ps->depth < 3) {
            ++ps->depth;
            SDL_EnumerateDirectory(path, search_cb, ps);
            --ps->depth;
        }
    } else if (strncmp(fname, ps->prefix, 4) == 0 && SDL_strstr(fname, ".toml")) {
        SDL_strlcpy(ps->found, path, sizeof ps->found);
    }
    return ps->found[0] ? SDL_ENUM_SUCCESS : SDL_ENUM_CONTINUE;
}

static char *load_part(const char *import_dir, int slot, int index) {
    char dir[1024];
    SDL_snprintf(dir, sizeof dir, "%s/parts/%s", import_dir, k_category[slot]);
    part_search ps;
    memset(&ps, 0, sizeof ps);
    SDL_snprintf(ps.prefix, sizeof ps.prefix, "%03d-", index);
    SDL_EnumerateDirectory(dir, search_cb, &ps);
    return ps.found[0] ? load_text(ps.found) : NULL;
}

static void read_part(int slot, const char *t, acr_gen1_part *p, char *name, size_t name_size) {
    const char *e = t + strlen(t);
    memset(p, 0, sizeof *p);
    p->present = 1;
    get_string(t, e, "name", name, name_size);
    p->weight = (uint16_t)get_int(t, e, "weight", 0);
    p->energy_drain = (uint16_t)get_int(t, e, "energy_drain", 0);
    switch (slot) {
    case SLOT_GEN:
        p->energy_drain = (uint16_t)get_int(t, e, "output", 0);
        p->stat_2c = (uint16_t)get_int(t, e, "capacity", 0);
        p->stat_2e = (uint16_t)get_int(t, e, "red_zone", 0);
        break;
    case SLOT_BOOSTER:
        p->stat_2c = (uint16_t)get_int(t, e, "power", 0);
        p->stat_2e = (uint16_t)get_int(t, e, "boost_drain", 0);
        break;
    case SLOT_BLADE:
        p->stat_2c = (uint16_t)get_int(t, e, "attack", 0);
        break;
    case SLOT_CORE:
        p->stat_2c = (uint16_t)get_int(t, e, "ap", 0);
        p->carry_limit = (uint16_t)get_int(t, e, "arm_load_limit", 0);
        break;
    case SLOT_LEGS:
        p->stat_2c = (uint16_t)get_int(t, e, "ap", 0);
        p->carry_limit = (uint16_t)get_int(t, e, "carry_limit", 0);
        p->drain_standing = (uint16_t)get_int(t, e, "drain_standing", 0);
        p->stability = (uint16_t)get_int(t, e, "stability", 0);
        p->boost_drain_legs = (uint16_t)get_int(t, e, "boost_drain", 0);
        p->turn_accel = (uint16_t)get_int(t, e, "turn_accel", 0);
        p->turn_speed = (uint8_t)get_int(t, e, "turn_speed", 0);
        p->fixed_boost = (uint8_t)get_int(t, e, "fixed_boost", 0);
        break;
    default:
        p->stat_2c = (uint16_t)get_int(t, e, "ap", 0);
        break;
    }
}

/* ---- motion ---- */

static int pick_model(const char *import_dir, const int parts[SLOT_COUNT]) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/motion/models.toml", import_dir);
    char *t = load_text(path);
    if (!t) return -1;
    int core = parts[SLOT_CORE];
    if (core == 3 || core == 4) core = 0;          /* as the game does */
    int found = -1;
    for (char *b = strstr(t, "[[model]]"); b && found < 0; ) {
        char *next = strstr(b + 9, "[[model]]");
        const char *e = next ? next : b + strlen(b);
        if (get_int(b, e, "core", -1) == core && list_has(b, e, "arms", parts[SLOT_ARMS]) &&
            list_has(b, e, "legs", parts[SLOT_LEGS]))
            found = (int)get_int(b, e, "id", -1);
        b = next;
    }
    SDL_free(t);
    return found;
}

static int load_tracks(const char *import_dir, int model, demo_ac *out) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/motion/model-%02d.toml", import_dir, model);
    char *t = load_text(path);
    if (!t) return 0;
    int used = 0;
    for (char *b = strstr(t, "[[anim]]"); b; ) {
        char *next = strstr(b + 8, "[[anim]]");
        const char *e = next ? next : b + strlen(b);
        const long index = get_int(b, e, "index", -1);
        const char *keys = find_key(b, e, "keys");
        if (index >= 0 && index < ACR_GEN1_ANIM_COUNT && keys) {
            acr_gen1_root_track *tr = &out->tracks[index];
            tr->length = (uint16_t)get_int(b, e, "length", 0);
            tr->keys = &out->keys[used];
            tr->count = 0;
            const char *p = keys + 1;   /* past the outer '[' */
            while (p < e && *p && *p != '\n') {
                if (*p == '[') {
                    long v[4] = { 0, 0, 0, 0 };
                    char *q = (char *)p + 1;
                    for (int k = 0; k < 4; ++k) {
                        v[k] = strtol(q, &q, 10);
                        while (*q == ',' || *q == ' ') ++q;
                    }
                    if (used < DEMO_MAX_KEYS) {
                        out->keys[used++] = (acr_gen1_root_key){ (uint16_t)v[0], (int16_t)v[1],
                                                                  (int16_t)v[2], (int16_t)v[3] };
                        ++tr->count;
                    }
                    p = strchr(q, ']');
                    if (!p) break;
                }
                ++p;
            }
            out->def.anim[index] = tr;
        }
        b = next;
    }
    SDL_free(t);
    return 1;
}

int demo_load(const char *import_dir, const int parts[SLOT_COUNT], demo_ac *out,
              char *err, int err_size) {
    memset(out, 0, sizeof *out);
    acr_gen1_build b;
    memset(&b, 0, sizeof b);
    acr_gen1_part *by_slot[SLOT_COUNT] = { &b.core, &b.booster, &b.head, &b.arms, &b.blade, &b.hand,
                                           &b.back_l, &b.back_r, &b.generator, &b.fcs, &b.legs };
    for (int s = 0; s < SLOT_COUNT; ++s) {
        if (parts[s] == 255) { SDL_strlcpy(out->names[s], "-", sizeof out->names[s]); continue; }
        char *t = load_part(import_dir, s, parts[s]);
        if (!t) {
            SDL_snprintf(err, err_size, "(No %s part %d in %s/parts.)", k_category[s], parts[s],
                         import_dir);
            return 0;
        }
        read_part(s, t, by_slot[s], out->names[s], sizeof out->names[s]);
        SDL_free(t);
    }
    acr_gen1_compile(&b, &out->def.stats);
    acr_gen1_motion_from_stats(&out->def.stats, &out->def.motion);
    out->def.landing_steps = 20;
    out->def.no_boost_hop = parts[SLOT_LEGS] >= 27;   /* caterpillars */

    out->model = pick_model(import_dir, parts);
    if (out->model < 0 || !load_tracks(import_dir, out->model, out)) {
        SDL_snprintf(err, err_size, "(No walking animation in %s/motion: import the disc again.)",
                     import_dir);
        return 0;
    }
    return 1;
}
