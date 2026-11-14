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

/* Element k of "[a, b, c]", or fallback. */
static long list_at(const char *start, const char *end, const char *key, int k, long fallback) {
    const char *v = find_key(start, end, key);
    if (!v || *v != '[') return fallback;
    char *p = (char *)v + 1;
    for (int i = 0; ; ++i) {
        char *next;
        const long x = strtol(p, &next, 10);
        if (next == p) return fallback;
        if (i == k) return x;
        p = next;
        while (*p == ',' || *p == ' ') ++p;
    }
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
    int32_t ap = 0;
    uint32_t shell = 0, energy = 0;
    long fcs[4][6];
    memset(fcs, 0, sizeof fcs);
    acr_gen1_part *by_slot[SLOT_COUNT] = { &b.core, &b.booster, &b.head, &b.arms, &b.blade, &b.hand,
                                           &b.back_l, &b.back_r, &b.generator, &b.fcs, &b.legs };
    for (int s = 0; s < SLOT_COUNT; ++s) {
        if (parts[s] == 255 || (s == SLOT_BOOSTER && parts[SLOT_LEGS] >= 27)) {   /* caterpillars: no booster */
            SDL_strlcpy(out->names[s], "-", sizeof out->names[s]);
            continue;
        }
        char *t = load_part(import_dir, s, parts[s]);
        if (!t) {
            SDL_snprintf(err, err_size, "(No %s part %d in %s/parts.)", k_category[s], parts[s],
                         import_dir);
            return 0;
        }
        read_part(s, t, by_slot[s], out->names[s], sizeof out->names[s]);
        const char *e = t + strlen(t);
        if (s == SLOT_HEAD || s == SLOT_CORE || s == SLOT_ARMS || s == SLOT_LEGS) {
            ap += get_int(t, e, "ap", 0);
            shell += (uint32_t)get_int(t, e, "defense_shell", 0);
            energy += (uint32_t)get_int(t, e, "defense_energy", 0);
        }
        if (s == SLOT_FCS) {
            for (int k = 0; k < 4; ++k) {
                char key[16];
                SDL_snprintf(key, sizeof key, "lock_class_%d", k);
                for (int j = 0; j < 6; ++j) fcs[k][j] = list_at(t, e, key, j, 0);
            }
        }
        if ((s == SLOT_HAND || s == SLOT_BACK_L || s == SLOT_BLADE) && get_int(t, e, "attack", 0) > 0) {
            acr_gen1_weapon *w = &out->weapon[out->weapons];
            memset(w, 0, sizeof *w);
            SDL_strlcpy(w->name, out->names[s], sizeof w->name);
            w->attack = (uint16_t)get_int(t, e, "attack", 0);
            if (s == SLOT_BLADE) {
                w->kind = ACR_GEN1_BLADE;
                w->energy = 1;
                w->range = 740;          /* stand-in: the blade's reach is not decoded yet */
                w->interval_ms = 1000;
            } else {
                const long cls = get_int(t, e, "lock_class", 4);
                w->kind = s == SLOT_BACK_L ? ACR_GEN1_MISSILE : ACR_GEN1_GUN;
                w->ammo = (uint16_t)get_int(t, e, "ammo", 0);
                w->range = (int32_t)get_int(t, e, "range", 6000);
                w->energy = (uint8_t)get_int(t, e, "energy_damage", 0);
                w->lock_ms = (uint16_t)(cls < 4 ? cls : 0);   /* class for now; FCS applied below */
                w->box_w = (uint16_t)(cls < 4 ? cls + 1 : 0);
                /* Measured: rifle (interval byte 5) fires every ~590 ms. */
                w->interval_ms = (uint16_t)(get_int(t, e, "fire_interval", 5) * 118);
                w->speed = w->kind == ACR_GEN1_MISSILE ? 290 : 650;     /* measured rifle; missile estimated */
                w->homing = w->kind == ACR_GEN1_MISSILE ? 700 : 0;      /* stand-in */
            }
            ++out->weapons;
        }
        SDL_free(t);
    }
    /* Order as the AC carries them: hand weapon, back weapon, blade. */
    for (int i = 0; i < out->weapons; ++i)
        for (int j = i + 1; j < out->weapons; ++j)
            if (out->weapon[j].kind < out->weapon[i].kind) {
                const acr_gen1_weapon tmp = out->weapon[i];
                out->weapon[i] = out->weapon[j];
                out->weapon[j] = tmp;
            }
    /* Apply the FCS: each weapon's lock class sets its range cap, box and lock time. */
    for (int i = 0; i < out->weapons; ++i) {
        acr_gen1_weapon *w = &out->weapon[i];
        if (w->kind == ACR_GEN1_BLADE || !w->box_w) { w->box_w = 0; w->lock_ms = 0; continue; }
        const int cls = w->box_w - 1;
        if (fcs[cls][0] && w->range > fcs[cls][0]) w->range = (int32_t)fcs[cls][0];
        w->box_w = (uint16_t)fcs[cls][1];
        w->box_h = (uint16_t)fcs[cls][2];
        w->lock_ms = (uint16_t)(fcs[cls][5] * 1000 / 60);   /* lock time read as 1/60 s */
    }
    out->ap = ap;
    out->shell_factor = acr_gen1_damage_factor(shell, 0, 0);
    out->energy_factor = acr_gen1_damage_factor(energy, 1, 0);
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
