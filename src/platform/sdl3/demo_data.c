#include "demo_data.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <limits.h>
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
        /* Compiled +0x1C is the swing's charge drain (blade +0x2C). Imports
         * before the AC1Recomp mapping called it "attack". */
        p->stat_2c = (uint16_t)get_int(t, e, "charge_drain", get_int(t, e, "attack", 0));
        break;
    case SLOT_CORE:
        p->stat_2c = (uint16_t)get_int(t, e, "ap", 0);
        p->carry_limit = (uint16_t)get_int(t, e, "arm_load_limit", 0);
        p->intercept_chance = (uint8_t)get_int(t, e, "interceptor_reaction", 0);
        p->intercept_angle = (uint8_t)get_int(t, e, "interceptor_angle", 0);
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
        p->back_turn = (uint16_t)get_int(t, e, "back_turn", 0);
        break;
    default:
        p->stat_2c = (uint16_t)get_int(t, e, "ap", 0);
        break;
    }
}

/* ---- motion ---- */

int demo_pick_model(const char *import_dir, const int parts[SLOT_COUNT]) {
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
    long fcs[4][6], fcs_max_locks = 0;
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
        /* Radar: the head's (fields radar / radar_range, or words 9 / 10 of
         * `raw` in older imports) or a back radar unit's (type RADAR; range word 5). */
        if (s == SLOT_HEAD && get_int(t, e, "radar", list_at(t, e, "raw", 9, 0)))
            out->radar_range = SDL_max(out->radar_range, (int32_t)get_int(t, e, "radar_range", list_at(t, e, "raw", 10, 0)));
        char ptype[32] = "";
        get_string(t, e, "type", ptype, sizeof ptype);
        if ((s == SLOT_BACK_L || s == SLOT_BACK_R) && (get_int(t, e, "radar", 0) || SDL_strstr(ptype, "RADAR")))
            out->radar_range = SDL_max(out->radar_range, (int32_t)get_int(t, e, "radar_range", list_at(t, e, "raw", 5, 0)));
        if (s == SLOT_FCS) {
            for (int k = 0; k < 4; ++k) {
                char key[16];
                SDL_snprintf(key, sizeof key, "lock_class_%d", k);
                for (int j = 0; j < 6; ++j) fcs[k][j] = list_at(t, e, key, j, 0);
            }
            fcs_max_locks = get_int(t, e, "max_locks", 0);
        }
        /* Weapon arms (arms 12 and up) are the AC's gun: the game's slot 0. */
        /* Both back slots are weapons (a two-slot unit sits in both: one weapon). */
        const int back_r_own = s == SLOT_BACK_R && !(parts[SLOT_BACK_R] == parts[SLOT_BACK_L] && parts[SLOT_BACK_L] >= 52);
        if ((s == SLOT_HAND || s == SLOT_BACK_L || back_r_own || s == SLOT_BLADE || (s == SLOT_ARMS && parts[SLOT_ARMS] >= 12)) &&
            get_int(t, e, "attack", 0) > 0 && out->weapons < ACR_GEN1_MAX_WEAPONS) {
            acr_gen1_weapon *w = &out->weapon[out->weapons];
            memset(w, 0, sizeof *w);
            SDL_strlcpy(w->name, out->names[s], sizeof w->name);
            /* The HUD names weapons by type, as the game does (weapon arms
             * are only "ARM UNIT": those keep the part's name). */
            SDL_strlcpy(out->weapon_type[out->weapons], ptype[0] && SDL_strcmp(ptype, "ARM UNIT") ? ptype : out->names[s],
                        sizeof out->weapon_type[out->weapons]);
            w->attack = (uint16_t)get_int(t, e, "attack", 0);
            {   /* knockback: detail +0x16 (blade +0x32) */
                const long imp = get_int(t, e, "impact", 0);
                w->impact = (uint8_t)(imp < 0 ? 0 : imp > 255 ? 255 : imp);
            }
            if (s == SLOT_BLADE) {
                w->kind = ACR_GEN1_BLADE;
                w->energy = (uint8_t)((get_int(t, e, "hit_flags", 0x20) & 0x20) != 0);
                w->charge_drain = (uint16_t)get_int(t, e, "charge_drain", 0);
                w->range = ACR_GEN1_BLADE_REACH;    /* fitted to LS-2001 measurements; others not measured */
            } else {
                const long cls = get_int(t, e, "lock_class", 4);
                /* Kind from the weapon detail (0 gun, 1 missile, 2 cannon); old
                 * imports without it: the back weapon was taken as a missile. */
                const long kind = get_int(t, e, "kind", s == SLOT_BACK_L || s == SLOT_BACK_R ? 1 : 0);
                w->kind = kind == 1 ? ACR_GEN1_MISSILE : ACR_GEN1_GUN;
                w->ammo = (uint16_t)get_int(t, e, "ammo", 0);
                w->ammo_price = (uint16_t)get_int(t, e, "ammo_price", 0);
                w->solid = (uint8_t)((get_int(t, e, "ammo_flags", 0) & 0x10) != 0);
                w->range = (int32_t)get_int(t, e, "range", 6000);
                w->energy = (uint8_t)get_int(t, e, "energy_damage", 0);
                w->lock_ms = (uint16_t)(cls < 4 ? 1 : 0);
                w->box_w = (uint16_t)(cls < 4 ? cls + 1 : 0);   /* class for now; FCS applied below */
                w->mount = (uint8_t)(s == SLOT_HAND ? ACR_GEN1_MOUNT_HAND
                                     : s == SLOT_ARMS ? ACR_GEN1_MOUNT_ARMS : ACR_GEN1_MOUNT_BACK);
                /* Refire wait: detail +0x11 in game ticks (AC +0x48 counts it
                 * down; 5 after a WG-RF35 shot). */
                w->interval = (uint16_t)get_int(t, e, "fire_interval", 5);
                /* Post-shot hold (detail +4, 255 none): the firing animation,
                 * during which the refire wait doesn't count down. Its length
                 * is the set's layer-2 base (7 in all 72 sets) + detail +4
                 * (VS 0x80054DE4). WG-RF35 (hold 0, wait 5): 7 + 5 + 1 = its
                 * measured 13-tick cadence. */
                const long hold = get_int(t, e, "post_shot_hold", 255);
                w->hold = (uint8_t)(hold == 255 || hold < 0 || kind == 1 ? 0 : 7 + hold);
                const long pattern = get_int(t, e, "fire_pattern", 0);
                w->shots = (uint8_t)((pattern & 3) == 1 ? 2 : (pattern & 3) == 2 ? 4 : 1);
                w->straight = (uint8_t)((get_int(t, e, "sight", 1) & 4) != 0 || (pattern & 4) != 0);
                /* Aim table (detail +3; "firing_anim" in older part files' notes)
                 * and its axis: the gun routine passes fire pattern bit 3, the
                 * cannon routine always y (VS 0x80080FB4, 0x80081CE4). */
                const long aim = get_int(t, e, "firing_anim", 255);
                w->aim_table = (uint8_t)(aim < 0 || aim > 254 ? 255 : aim);
                w->aim_y = (uint8_t)((pattern & 8) != 0 || kind == 2);
                w->speed = (int32_t)get_int(t, e, "shot_speed", 700);
                w->stance = (uint8_t)get_int(t, e, "stance", 0);
                /* Cannon kick: base recoil (detail +2) and the cannon table's
                 * +2 for the model (32 for most; imports before the weapons
                 * pass lack it: 32). Finished after the compile, below. */
                if (kind == 2) {
                    w->kick = (uint8_t)(pattern & 0xFF);
                    w->kick_table = (uint16_t)get_int(t, e, "cannon_kick", 32);
                }
                w->max_locks = (uint8_t)get_int(t, e, "salvo", 1);
                if (w->kind == ACR_GEN1_MISSILE) {
                    w->volley_a = (uint8_t)get_int(t, e, "volley_a", 1);
                    w->volley_b = (uint8_t)get_int(t, e, "volley_b", 0);
                    w->ignition = (uint8_t)get_int(t, e, "ignition_delay", 1);
                    w->spread_pitch = (uint16_t)get_int(t, e, "spread_pitch", 0);
                    w->spread_yaw = (uint16_t)get_int(t, e, "spread_yaw", 0);
                    /* Flight (table 0x80093114); defaults are kind 0 (WM-S40/1). */
                    acr_gen1_flight *fl = &w->flight;
                    fl->kind = (uint8_t)get_int(t, e, "flight_kind", 0);
                    fl->top = (uint16_t)get_int(t, e, "shot_speed", 350);
                    fl->thrust = (uint16_t)get_int(t, e, "homing", 80);
                    fl->fuel = (uint16_t)get_int(t, e, "fuel", 50);
                    fl->life = (uint16_t)get_int(t, e, "life", 60);
                    fl->turn = (uint16_t)get_int(t, e, "turn", 110);
                    fl->cone = (uint16_t)get_int(t, e, "cone", 1024);
                    acr_gen1_flight *sp = &w->split;
                    sp->kind = 0;
                    sp->top = (uint16_t)get_int(t, e, "split_top", 350);
                    sp->thrust = (uint16_t)get_int(t, e, "split_thrust", 80);
                    sp->fuel = (uint16_t)get_int(t, e, "split_fuel", 50);
                    sp->life = (uint16_t)get_int(t, e, "split_life", 60);
                    sp->turn = (uint16_t)get_int(t, e, "split_turn", 110);
                    sp->cone = (uint16_t)get_int(t, e, "split_cone", 1024);
                }
            }
            ++out->weapons;
        }
        SDL_free(t);
    }
    /* Order as the AC carries them: arm weapon, back weapons (left first),
     * blade. */
    for (int i = 0; i < out->weapons; ++i)
        for (int j = i + 1; j < out->weapons; ++j)
            if (out->weapon[j].kind < out->weapon[i].kind) {
                const acr_gen1_weapon tmp = out->weapon[i];
                out->weapon[i] = out->weapon[j];
                out->weapon[j] = tmp;
                char tn[24];
                SDL_memcpy(tn, out->weapon_type[i], sizeof tn);
                SDL_memcpy(out->weapon_type[i], out->weapon_type[j], sizeof tn);
                SDL_memcpy(out->weapon_type[j], tn, sizeof tn);
            }
    /* Apply the FCS: each weapon's lock class sets its range cap, box and lock time. */
    for (int i = 0; i < out->weapons; ++i) {
        acr_gen1_weapon *w = &out->weapon[i];
        if (w->kind == ACR_GEN1_BLADE || !w->box_w) { w->box_w = 0; w->lock_ms = 0; continue; }
        const int cls = w->box_w - 1;
        /* Lock range = weapon slot +20, the smaller of the FCS class range
         * and the weapon's own; the shots keep the weapon's range (slot +22,
         * passed to the shot spawners in VS 0x800812D4 / 0x8008138C), so a
         * rifle on the COMDEX-C7 locks to 6,100 but its shots fly 8,500
         * (menu compile 0x800A8C88-0x800A8CC8; docs/feel/lockon.md). */
        w->lock_range = fcs[cls][0] && fcs[cls][0] < w->range ? (int32_t)fcs[cls][0] : w->range;
        /* The FCS group's first box value bounds the pitch, the second the
         * yaw (compile copies +2..+8 to slot +24..+30; the sight sweep VS
         * 0x80078230 tests the pitch against slot +24 / +28 and the yaw
         * against +26 / +30). box_w / inner_w are the yaw (left / right). */
        w->box_h = (uint16_t)fcs[cls][1];
        w->box_w = (uint16_t)fcs[cls][2];
        w->inner_h = (uint16_t)fcs[cls][3];
        w->inner_w = (uint16_t)fcs[cls][4];
        /* Guns: lock time from the FCS value, the target's speed and
         * distance (VS 0x800800E4, see acr_gen1_battle.h). Missiles: cursor
         * speed and ticks per lock from the FCS value (VS 0x80080638). */
        w->lock_value = (uint8_t)(fcs[cls][5] > 255 ? 255 : fcs[cls][5]);
        w->lock_ticks = (uint8_t)((256 - w->lock_value) / 8);
        if (fcs_max_locks && w->max_locks > fcs_max_locks) w->max_locks = (uint8_t)fcs_max_locks;
    }
    out->ap = ap;
    out->shell_factor = acr_gen1_damage_factor(shell, 0, 0);
    out->energy_factor = acr_gen1_damage_factor(energy, 1, 0);
    acr_gen1_compile(&b, &out->def.stats);
    for (int i = 0; i < out->weapons && i < ACR_GEN1_MAX_WEAPONS; ++i) {
        acr_gen1_weapon *w = &out->weapon[i];
        if (!w->stance || !w->kick) { w->kick = 0; continue; }
        uint32_t st = b.legs.stability;
        if (b.option_mask & ACR_GEN1_OPTION_ABSORBER) st += 800;      /* SP-SAP */
        w->kick = acr_gen1_cannon_kick(w->kick, w->kick_table, st, out->def.stats.carried_weight);
    }
    acr_gen1_motion_from_stats(&out->def.stats, &out->def.motion);
    out->def.landing_steps = 20;
    out->def.no_boost_hop = parts[SLOT_LEGS] >= 27;   /* caterpillars */
    out->def.drive_walk = parts[SLOT_LEGS] >= 21;     /* four legs and caterpillars */
    out->def.no_jump = parts[SLOT_LEGS] >= 27;        /* caterpillars: rise without a jump */

    out->model = demo_pick_model(import_dir, parts);
    if (out->model < 0 || !load_tracks(import_dir, out->model, out)) {
        SDL_snprintf(err, err_size, "(No walking animation in %s/motion: import the disc again.)",
                     import_dir);
        return 0;
    }
    return 1;
}

/* ---- the shop catalogue ---- */

typedef struct catalog_walk {
    acr_gen1_catalog *cat;
    char (*names)[ACR_GEN1_PART_MAX][24];
    int category, depth;
    acr_gen1_rogue_parts *info;   /* Rogue Arena's part info, when wanted */
    demo_part_specs *specs;       /* PART SPECS' numbers, when wanted */
} catalog_walk;

/* PART SPECS: the TOML key of each number (demo_data.h DS_*). */
static const char *const k_spec_key[DS_COUNT] = {
    "weight", "energy_drain", "output", "ap", "defense_shell", "defense_energy", "radar", "radar_range",
    "computer_type", "arm_load_limit", "option_slots", "carry_limit", "moving_speed", "turn_accel", "stability",
    "power", "boost_drain", "capacity", "red_zone", "max_locks", "lock_area", "attack", "ammo", "range",
    "fire_interval", "energy_per_shot", "salvo", "kind", "ammo_flags", "impact", "radar_flags",
};

static const char *const k_cat_folder[ACR_GEN1_CAT_COUNT] = {
    "core", "booster", "head", "arms", "blade", "hand", "back", "generator", "fcs", "legs", "option"
};
/* The demo slot whose stats a category's files hold (back: either back slot). */
static const int k_cat_slot[ACR_GEN1_CAT_COUNT] = {
    SLOT_CORE, SLOT_BOOSTER, SLOT_HEAD, SLOT_ARMS, SLOT_BLADE, SLOT_HAND, SLOT_BACK_L, SLOT_GEN,
    SLOT_FCS, SLOT_LEGS, -1
};

/* What the garage's PERFORMANCE page reads from a part file (acr_gen1_perf.h).
 * Older imports without the named keys fall back to the record's words
 * (`raw` starts at byte 0x24). */
static void read_perf(int category, const char *t, const char *e, acr_gen1_perf_part *q) {
    memset(q, 0, sizeof *q);
    q->ap = (uint16_t)get_int(t, e, "ap", 0);
    q->defense_shell = (uint16_t)get_int(t, e, "defense_shell", 0);
    q->defense_energy = (uint16_t)get_int(t, e, "defense_energy", 0);
    const long atk = get_int(t, e, "attack", 0);   /* radars have none */
    q->attack = (uint16_t)(atk > 0 && atk < 65536 ? atk : 0);
    switch (category) {
    case ACR_GEN1_CAT_LEGS:
        q->moving_speed = (uint16_t)get_int(t, e, "moving_speed", list_at(t, e, "raw", 9, 0));
        q->turn_accel = (uint16_t)get_int(t, e, "turn_accel", list_at(t, e, "raw", 10, 0));
        q->stability = (uint16_t)get_int(t, e, "stability", list_at(t, e, "raw", 14, 0));
        break;
    case ACR_GEN1_CAT_BOOSTER:
        q->power = (uint16_t)get_int(t, e, "power", list_at(t, e, "raw", 4, 0));
        break;
    case ACR_GEN1_CAT_HEAD:
        q->radar_range = (uint16_t)get_int(t, e, "radar_range", list_at(t, e, "raw", 10, 0));
        break;
    case ACR_GEN1_CAT_BACK:
        if (get_int(t, e, "radar", 0)) {
            q->radar_unit = 1;
            q->radar_range = (uint16_t)get_int(t, e, "radar_range", list_at(t, e, "raw", 5, 0));
        }
        break;
    case ACR_GEN1_CAT_FCS: {
        static const char *const k_class[4] = { "lock_class_0", "lock_class_1", "lock_class_2", "lock_class_3" };
        q->max_locks = (uint8_t)get_int(t, e, "max_locks", list_at(t, e, "raw", 4, 0));
        /* each lock class: range, box pitch, yaw, inner pitch, yaw, lock value (raw words 5 + 6k ...);
         * the garage page's lock_w / lock_h keep the file's order (only summed) */
        q->fcs_range = (uint16_t)list_at(t, e, k_class[0], 0, list_at(t, e, "raw", 5, 0));
        q->lock_time = (int16_t)list_at(t, e, k_class[0], 5, list_at(t, e, "raw", 10, 0));
        for (int k = 0; k < 4; ++k) {
            q->lock_w[k] = (int16_t)list_at(t, e, k_class[k], 1, list_at(t, e, "raw", 6 + 6 * k, 0));
            q->lock_h[k] = (int16_t)list_at(t, e, k_class[k], 2, list_at(t, e, "raw", 7 + 6 * k, 0));
        }
        break;
    }
    default:
        break;
    }
}

static void catalog_file(catalog_walk *w, const char *path) {
    char *t = load_text(path);
    if (!t) return;
    const char *e = t + strlen(t);
    const int option = w->category == ACR_GEN1_CAT_OPTION;
    const long index = get_int(t, e, option ? "bit" : "game_index", -1);
    if (index >= 0 && index < ACR_GEN1_PART_MAX) {
        acr_gen1_catalog_part *p = &w->cat->part[w->category][index];
        memset(p, 0, sizeof *p);
        char name[24];
        if (option) {
            get_string(t, e, "name", name, sizeof name);
            p->slots = (uint8_t)get_int(t, e, "slots", 1);
        } else {
            read_part(k_cat_slot[w->category], t, &p->stats, name, sizeof name);
            p->flags = acr_gen1_fit_flags_moa((acr_gen1_slot)k_cat_slot[w->category], (unsigned)index);
            if (w->category == ACR_GEN1_CAT_CORE) p->slots = (uint8_t)get_int(t, e, "option_slots", 0);
            read_perf(w->category, t, e, &p->perf);
        }
        p->present = 1;
        p->for_sale = 1;   /* the shop's stock is not decoded yet: everything is for sale */
        p->price = (uint32_t)get_int(t, e, "price", 0);
        if (w->names) SDL_strlcpy(w->names[w->category][index], name, sizeof w->names[0][0]);
        if (w->specs) {
            demo_part_specs *sp = w->specs;
            for (int k = 0; k < DS_COUNT; ++k) {
                const long v = get_int(t, e, k_spec_key[k], LONG_MIN);
                sp->v[w->category][index][k] = v == LONG_MIN ? DEMO_SPEC_NONE : (int32_t)v;
            }
            /* back radars keep their range in the same key as heads */
            get_string(t, e, option ? "full_name" : "type", sp->type[w->category][index], sizeof sp->type[0][0]);
            if (option) {   /* the option's own shop line */
                char d[3 * 76];
                get_string(t, e, "description", d, sizeof d);
                SDL_strlcpy(sp->text[w->category][index][0], d, sizeof sp->text[0][0][0]);
                sp->text_n[w->category][index] = d[0] ? 1 : 0;
            }
            sp->present[w->category][index] = 1;
        }
        if (w->info) {
            const long atk = get_int(t, e, "attack", 0);
            w->info->attack[w->category][index] = (uint16_t)(atk > 0 && atk < 65536 ? atk : 0);
            uint8_t fl = 0;
            if (w->category == ACR_GEN1_CAT_BACK) {
                if (SDL_strstr(path, "radar")) fl |= ACR_GEN1_ROGUE_RADAR;
                if (SDL_strstr(name, "P77-ST")) fl |= ACR_GEN1_ROGUE_STEALTH;
                if (get_int(t, e, "stance", 0)) fl |= ACR_GEN1_ROGUE_STANCE;
            }
            w->info->flags[w->category][index] = fl;
        }
    }
    SDL_free(t);
}

static SDL_EnumerationResult SDLCALL catalog_cb(void *user, const char *dirname, const char *fname) {
    catalog_walk *w = user;
    char path[1400];
    const size_t n = strlen(dirname);
    const int has_sep = n && (dirname[n - 1] == '/' || dirname[n - 1] == '\\');
    SDL_snprintf(path, sizeof path, "%s%s%s", dirname, has_sep ? "" : "/", fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (w->depth < 3) { ++w->depth; SDL_EnumerateDirectory(path, catalog_cb, w); --w->depth; }
    } else if (SDL_strstr(fname, ".toml")) {
        catalog_file(w, path);
    }
    return SDL_ENUM_CONTINUE;
}

int demo_catalog(const char *import_dir, acr_gen1_catalog *cat, char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]) {
    memset(cat, 0, sizeof *cat);
    if (names) memset(names, 0, sizeof(char) * ACR_GEN1_CAT_COUNT * ACR_GEN1_PART_MAX * 24);
    cat->sell_percent = 100;   /* parts sell back for what they cost */
    int found = 0;
    for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c) {
        char dir[1024];
        SDL_snprintf(dir, sizeof dir, "%s/parts/%s", import_dir, k_cat_folder[c]);
        catalog_walk w = { cat, names, c, 0, NULL, NULL };
        SDL_EnumerateDirectory(dir, catalog_cb, &w);
    }
    for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) found += cat->part[ACR_GEN1_CAT_CORE][i].present;
    return found > 0;
}

/* ---- Rogue Arena ---- */

int demo_rogue_parts(const char *import_dir, acr_gen1_rogue_parts *info) {
    static acr_gen1_catalog scratch;
    memset(info, 0, sizeof *info);
    memset(&scratch, 0, sizeof scratch);
    for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c) {
        char dir[1024];
        SDL_snprintf(dir, sizeof dir, "%s/parts/%s", import_dir, k_cat_folder[c]);
        catalog_walk w = { &scratch, NULL, c, 0, info, NULL };
        SDL_EnumerateDirectory(dir, catalog_cb, &w);
    }
    return 1;
}

typedef struct roster_walk {
    acr_gen1_rogue_foe *out;
    int max, n, depth;
    char (*names)[ACR_GEN1_PART_MAX][24];
} roster_walk;

static void roster_file(roster_walk *w, const char *path) {
    char *t = load_text(path);
    if (!t) return;
    const char *e = t + strlen(t);
    if (w->n < w->max && find_key(t, e, "pilot") && find_key(t, e, "core")) {
        acr_gen1_rogue_foe *f = &w->out[w->n];
        memset(f, 0, sizeof *f);
        f->arena = (uint8_t)get_int(t, e, "arena", 0);
        f->rank = (uint8_t)get_int(t, e, "rank", 0);
        f->entries = (uint8_t)get_int(t, e, "entries", 0);
        static const char *const k_key[SLOT_COUNT] = {
            "core", "booster", "head", "arms", "blade", "hand", "back_l", "back_r", "generator", "fcs", "legs" };
        for (int s = 0; s < SLOT_COUNT; ++s) {
            const long v = get_int(t, e, k_key[s], 255);
            f->parts[s] = (uint8_t)(v >= 0 && v < ACR_GEN1_PART_MAX ? v : ACR_GEN1_NO_PART);
        }
        f->options = (uint16_t)get_int(t, e, "options", 0);
        /* The Arena's prizes (acr-import writes them from the reward records). */
        const long money = get_int(t, e, "prize_money", 0);
        f->prize_money = (uint32_t)(money > 0 && money < 100000000L ? money : 0);
        const long prize = get_int(t, e, "prize_part", -1);
        f->prize_part = (uint8_t)(prize >= 0 && prize < 32 ? prize : 255);   /* ACR_GEN1_ARENA_PRIZES */
        for (int k = 0; k < 12; ++k) {
            const long c = list_at(t, e, "colours", k, 32);
            f->colours[k / 3][k % 3] = (uint8_t)(c >= 0 && c <= 63 ? c : 32);
        }
        get_string(t, e, "pilot", f->pilot, sizeof f->pilot);
        get_string(t, e, "ac", f->ac, sizeof f->ac);
        /* The AI editor's rows, as acr-import read them from its record. */
        acr_gen1_ai_profile *p = &f->ai;
        acr_gen1_ai_preset(p, ACR_GEN1_AI_BALANCED);
        p->preset = ACR_GEN1_AI_CUSTOM;
        static const char *const k_ai[ACR_GEN1_AI_SLIDERS] = { "shoot", "blade_use", "lock", "avoid", "fly", "range" };
        for (int k = 0; k < ACR_GEN1_AI_SLIDERS; ++k) {
            const long v = get_int(t, e, k_ai[k], 50);
            p->slider[k] = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
        }
        const long rebel = get_int(t, e, "rebel", 0);
        p->rebel = (uint8_t)(rebel < 0 ? 0 : rebel > 9 ? 9 : rebel);
        const uint8_t hand = f->parts[SLOT_HAND];
        f->karasawa = (uint8_t)(w->names && hand < ACR_GEN1_PART_MAX && SDL_strstr(w->names[ACR_GEN1_CAT_HAND][hand], "KARASAWA") != NULL);
        if (f->rank && f->entries && f->parts[SLOT_CORE] != ACR_GEN1_NO_PART) ++w->n;
    }
    SDL_free(t);
}

static SDL_EnumerationResult SDLCALL roster_cb(void *user, const char *dirname, const char *fname) {
    roster_walk *w = user;
    char path[1400];
    const size_t n = strlen(dirname);
    const int has_sep = n && (dirname[n - 1] == '/' || dirname[n - 1] == '\\');
    SDL_snprintf(path, sizeof path, "%s%s%s", dirname, has_sep ? "" : "/", fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (w->depth < 1) { ++w->depth; SDL_EnumerateDirectory(path, roster_cb, w); --w->depth; }
    } else if (w->depth == 1 && SDL_strstr(fname, ".toml")) {
        roster_file(w, path);
    }
    return SDL_ENUM_CONTINUE;
}

int demo_rogue_roster(const char *import_dir, acr_gen1_rogue_foe *out, int max,
                      char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]) {
    char dir[1024];
    SDL_snprintf(dir, sizeof dir, "%s/arena", import_dir);
    roster_walk w = { out, max, 0, 0, names };
    SDL_EnumerateDirectory(dir, roster_cb, &w);
    if (w.n >= max) SDL_Log("arena: the roster is full (%d): some opponents were left out", max);
    /* A steady order whatever the file system lists first. */
    for (int i = 1; i < w.n; ++i) {
        const acr_gen1_rogue_foe x = out[i];
        int k = i - 1;
        while (k >= 0 && (out[k].arena > x.arena || (out[k].arena == x.arena && out[k].rank > x.rank))) { out[k + 1] = out[k]; --k; }
        out[k + 1] = x;
    }
    return w.n;
}

/* ---- PART SPECS ---- */

/* The shop's part text: GG/MS/MIS.T entry 385, one 76-byte line per part in
 * the game's owned-table order (head 0, core 18, arms 29, legs 53, generator
 * 93, FCS 108, booster 143, back 157, hand 217, blade 243), "\r\n" written
 * out where the shop breaks the line (AC1Recomp, moa_arena_run_boot.c). */
static const int k_text_base[ACR_GEN1_CAT_COUNT] = { 18, 143, 0, 29, 243, 217, 157, 93, 108, 53, -1 };

static void shop_text(const char *import_dir, demo_part_specs *sp) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/unpacked/GG/MS/MIS/385.bin", import_dir);
    size_t size = 0;
    char *d = SDL_LoadFile(path, &size);
    if (!d) return;
    for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c) {
        if (k_text_base[c] < 0) continue;
        for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) {
            const size_t at = (size_t)(k_text_base[c] + i) * 76u;
            if (!sp->present[c][i] || at + 76u > size) continue;
            const char *t = d + at;
            int n = 0, k = 0;
            char (*out)[76] = sp->text[c][i];
            for (unsigned j = 0; j < 74u && n < 3; ++j) {
                const char ch = t[j];
                if (ch == '\\' && j + 3u < 76u && t[j + 1] == 'r' && t[j + 2] == '\\' && t[j + 3] == 'n') {
                    out[n][k] = 0; if (k) ++n; k = 0; j += 3; continue;
                }
                if (ch == '\r' || ch == '\n' || ch == 0) break;
                if (k < 75) out[n][k++] = (char)((unsigned char)ch >= 32u && (unsigned char)ch < 127u ? ch : ' ');
            }
            if (n < 3) { while (k && out[n][k - 1] == ' ') --k; out[n][k] = 0; if (k) ++n; }
            sp->text_n[c][i] = (uint8_t)n;
        }
    }
    SDL_free(d);
}

int demo_part_specs_load(const char *import_dir, demo_part_specs *sp) {
    static acr_gen1_catalog scratch;
    memset(sp, 0, sizeof *sp);
    memset(&scratch, 0, sizeof scratch);
    for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c) {
        char dir[1024];
        SDL_snprintf(dir, sizeof dir, "%s/parts/%s", import_dir, k_cat_folder[c]);
        catalog_walk w = { &scratch, NULL, c, 0, NULL, sp };
        SDL_EnumerateDirectory(dir, catalog_cb, &w);
    }
    shop_text(import_dir, sp);
    return 1;
}
