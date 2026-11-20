/* Gen 1 computer pilot, first version. See acr_gen1_ai.h and docs/AI.md. */
#include "acr_gen1_ai.h"

#include <string.h>

/* ---- profiles ---------------------------------------------------------- */

static const struct { const char *name; uint8_t s[ACR_GEN1_AI_SLIDERS]; } k_presets[ACR_GEN1_AI_PRESETS] = {
    /*                shoot blade lock avoid fly range   (AC1Recomp moa_card.c) */
    { "CUSTOM",    {  50,  50,  50,  50,  50,  50 } },
    { "BALANCED",  {  50,  50,  50,  50,  50,  50 } },
    { "AGGRESSOR", {  90,  85,  95,  85,  80,  30 } },
    { "SNIPER",    {  95,  10,  95,  50,  30,  90 } },
    { "DUELIST",   {  40, 100,  70,  60,  70,   0 } },
    { "EVASIVE",   {  60,  30,  60, 100,  90,  70 } },
};

static const char *const k_slider_key[ACR_GEN1_AI_SLIDERS] = {
    "shoot", "blade", "lock", "avoid", "fly", "range"
};

const char *acr_gen1_ai_preset_name(int preset) {
    return preset > 0 && preset < ACR_GEN1_AI_PRESETS ? k_presets[preset].name : "CUSTOM";
}

void acr_gen1_ai_preset(acr_gen1_ai_profile *p, int preset) {
    if (preset <= 0 || preset >= ACR_GEN1_AI_PRESETS) preset = ACR_GEN1_AI_BALANCED;
    memset(p, 0, sizeof *p);
    p->preset = (uint8_t)preset;
    memcpy(p->slider, k_presets[preset].s, sizeof p->slider);
    p->skill = 50;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int word_is(const char *a, int n, const char *b) {
    for (int i = 0; i < n; ++i)
        if (!b[i] || lower(a[i]) != lower(b[i])) return 0;
    return b[n] == 0;
}

int acr_gen1_ai_parse(acr_gen1_ai_profile *p, const char *text) {
    int got = 0;
    const char *s = text;
    while (s && *s) {
        const char *end = s;
        while (*end && *end != '\n') ++end;
        const char *k = s;
        while (k < end && (*k == ' ' || *k == '\t')) ++k;
        const char *ke = k;
        while (ke < end && ((*ke >= 'a' && *ke <= 'z') || (*ke >= 'A' && *ke <= 'Z') || *ke == '_')) ++ke;
        const char *v = ke;
        while (v < end && (*v == ' ' || *v == '\t')) ++v;
        if (ke > k && v < end && *v == '=') {
            ++v;
            while (v < end && (*v == ' ' || *v == '\t' || *v == '"' || *v == '\'')) ++v;
            const char *ve = v;
            while (ve < end && *ve != '"' && *ve != '\'' && *ve != '#' && *ve != ' ' && *ve != '\t' && *ve != '\r') ++ve;
            const int kn = (int)(ke - k), vn = (int)(ve - v);
            int num = 0, is_num = vn > 0;
            for (const char *c = v; c < ve; ++c) {
                if (*c < '0' || *c > '9') { is_num = 0; break; }
                if (num < 1000) num = num * 10 + (*c - '0');
            }
            if (num > 100) num = 100;
            if (word_is(k, kn, "preset")) {
                for (int i = 1; i < ACR_GEN1_AI_PRESETS; ++i)
                    if (word_is(v, vn, k_presets[i].name)) {
                        const uint8_t rebel = p->rebel, skill = p->skill;
                        acr_gen1_ai_preset(p, i);
                        p->rebel = rebel;
                        if (got) p->skill = skill;
                        got = 1;
                    }
                if (word_is(v, vn, "custom")) { p->preset = ACR_GEN1_AI_CUSTOM; got = 1; }
            } else if (is_num) {
                for (int i = 0; i < ACR_GEN1_AI_SLIDERS; ++i)
                    if (word_is(k, kn, k_slider_key[i])) {
                        if (p->slider[i] != num) p->preset = ACR_GEN1_AI_CUSTOM;
                        p->slider[i] = (uint8_t)num;
                        got = 1;
                    }
                if (word_is(k, kn, "skill")) { p->skill = (uint8_t)num; got = 1; }
                if (word_is(k, kn, "rebel")) { p->rebel = (uint8_t)(num > 9 ? 9 : num); got = 1; }
            }
        }
        s = *end ? end + 1 : NULL;
    }
    return got;
}

static int put(char *out, int size, int at, const char *s) {
    while (*s) {
        if (at + 1 < size) out[at] = *s;
        ++at; ++s;
    }
    if (size > 0) out[at + 1 < size ? at : size - 1] = 0;
    return at;
}

static int put_num(char *out, int size, int at, int n) {
    char b[8];
    int k = 0;
    do { b[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 7);
    char r[8];
    for (int i = 0; i < k; ++i) r[i] = b[k - 1 - i];
    r[k] = 0;
    return put(out, size, at, r);
}

int acr_gen1_ai_write(const acr_gen1_ai_profile *p, char *out, int size) {
    int at = put(out, size, 0, "# Armored-CoreR AI profile (Gen 1). Sliders 0..100 as in MoA's AI editor.\n");
    at = put(out, size, at, "preset = \"");
    char name[16];
    const char *n = acr_gen1_ai_preset_name(p->preset);
    int i = 0;
    for (; n[i] && i < 15; ++i) name[i] = (char)lower(n[i]);
    name[i] = 0;
    at = put(out, size, at, name);
    at = put(out, size, at, "\"\n");
    for (int k = 0; k < ACR_GEN1_AI_SLIDERS; ++k) {
        at = put(out, size, at, k_slider_key[k]);
        at = put(out, size, at, " = ");
        at = put_num(out, size, at, p->slider[k]);
        at = put(out, size, at, "\n");
    }
    at = put(out, size, at, "rebel = ");
    at = put_num(out, size, at, p->rebel);
    at = put(out, size, at, "\nskill = ");
    at = put_num(out, size, at, p->skill);
    return put(out, size, at, "\n");
}

void acr_gen1_ai_record(const acr_gen1_ai_profile *p, uint8_t a[ACR_GEN1_AI_RECORD_SIZE]) {
    static const uint8_t k_default[ACR_GEN1_AI_RECORD_SIZE] = {
        0, 50, 50, 50, 0, 0, 0, 0, 80, 50, 55, 51, 12, 51, 50, 0,
        0xE8, 0x03, 0x88, 0x13,                          /* 1000, 5000 */
        50, 25, 50, 25, 50, 25, 50, 25, 50, 25,
        50, 50, 50, 50, 50, 50,
    };
    int s[ACR_GEN1_AI_SLIDERS];
    for (int i = 0; i < ACR_GEN1_AI_SLIDERS; ++i) s[i] = p->slider[i] > 100 ? 100 : p->slider[i];
    memcpy(a, k_default, ACR_GEN1_AI_RECORD_SIZE);
    const int shoot = s[ACR_GEN1_AI_SHOOT], blade = s[ACR_GEN1_AI_BLADE], lock = s[ACR_GEN1_AI_LOCK];
    const int avoid = s[ACR_GEN1_AI_AVOID], fly = s[ACR_GEN1_AI_FLY], range = s[ACR_GEN1_AI_RANGE];
    a[13] = (uint8_t)(shoot + 1);
    a[9] = (uint8_t)blade;
    a[10] = (uint8_t)(105 - blade);
    a[11] = (uint8_t)(lock == 100 ? 0 : lock + 1);
    a[12] = (uint8_t)((100 - lock) / 4);
    a[14] = (uint8_t)avoid;
    a[1] = (uint8_t)(100 - fly);
    a[2] = (uint8_t)(100 - fly);
    a[3] = (uint8_t)fly;
    a[20] = (uint8_t)((range + 100 - fly) / 2);
    a[22] = (uint8_t)((fly + 100 - range) / 2);
    a[24] = (uint8_t)range;
    a[28] = (uint8_t)(100 - range);
    a[0] = p->rebel;
}

/* ---- maths (the movement's sine table, so every machine agrees) --------- */

static uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

static int32_t wrap(int32_t a) {
    a &= 4095;
    return a >= 2048 ? a - 4096 : a;
}

static uint16_t angle_of(int64_t s, int64_t c) {
    if (!s && !c) return 0;
    const int64_t as = s < 0 ? -s : s, ac = c < 0 ? -c : c;
    int lo = 0, hi = 1024;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if ((int64_t)acr_gen1_sin((uint16_t)mid) * ac >= (int64_t)acr_gen1_cos((uint16_t)mid) * as) hi = mid;
        else lo = mid + 1;
    }
    if (c >= 0 && s >= 0) return (uint16_t)lo;
    if (c < 0 && s >= 0) return (uint16_t)(2048 - lo);
    if (c < 0) return (uint16_t)(2048 + lo);
    return (uint16_t)((4096 - lo) & 4095);
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t lerp100(int32_t a, int32_t b, int t) { return a + (b - a) * t / 100; }

/* ---- what the pilot sees ------------------------------------------------ */

void acr_gen1_ai_look(const acr_gen1_battle *b, int me, int32_t arena_half, acr_gen1_ai_view *v) {
    /* The target the battle picked for us (enemies only); with none, our own
     * AC stands in and target_alive stays 0. */
    const int ti = acr_gen1_battle_target(b, me);
    const acr_gen1_fighter *f = &b->f[me], *t = &b->f[ti < 0 ? me : ti];
    const acr_gen1_ac *a = &f->ac, *o = &t->ac;
    memset(v, 0, sizeof *v);
    v->x = a->x / 64; v->y = a->y / 64; v->z = a->z / 64;
    v->yaw = a->yaw;
    v->pitch = a->pitch;
    v->turn_rate = a->turn_rate;
    v->turn_accel = f->def ? f->def->motion.turn_accel : 1;
    v->state = a->state;
    v->speed = (int32_t)isqrt64((uint64_t)((int64_t)a->vx * a->vx + (int64_t)a->vz * a->vz));
    v->energy = a->energy;
    v->energy_max = f->def ? f->def->stats.energy_capacity : 0;
    v->drained = a->drained;
    v->ap = f->ap; v->ap_max = f->ap_max;
    v->legs = f->legs; v->human_plus = f->human_plus; v->kneeling = f->kneeling;
    v->weapons = f->weapons; v->current = f->current;
    v->blade = -1;
    for (int k = 0; k < f->weapons && k < ACR_GEN1_MAX_WEAPONS; ++k) {
        const acr_gen1_weapon *w = &f->weapon[k];
        v->kind[k] = w->kind;
        v->ammo[k] = w->ammo ? f->ammo[k] : 0xFFFF;
        v->range[k] = w->range;
        v->stance[k] = w->stance;
        v->max_locks[k] = w->max_locks ? w->max_locks : 1;
        if (w->kind == ACR_GEN1_BLADE) { v->blade = (int8_t)k; v->blade_drain = w->charge_drain; }
    }
    if (f->weapons && f->current < f->weapons) {
        const acr_gen1_weapon *w = &f->weapon[f->current];
        v->in_box = (uint8_t)acr_gen1_in_lock_box(b, me, w);
        v->ready = (uint8_t)(!f->cooldown && !f->hold_left && (!w->ammo || f->ammo[f->current]) &&
                             acr_gen1_stance_ok(f));
    }
    v->locked = f->locked; v->locks = f->locks;
    v->volley = (uint8_t)(f->volley_ticks != 0);
    v->swinging = (uint8_t)(f->swing != 0);

    v->target_alive = (uint8_t)(ti >= 0 && t->ap > 0);
    const int64_t dx = (o->x - a->x) / 64, dy = (o->y - a->y) / 64, dz = (o->z - a->z) / 64;
    v->dist = (int32_t)isqrt64((uint64_t)(dx * dx + dz * dz));
    v->bearing = (int16_t)wrap((int32_t)angle_of(dx, dz) - a->yaw);
    v->elevation = (int16_t)wrap((int32_t)angle_of(-dy, v->dist));
    v->target_height = -o->y / 64;
    /* Threats: any enemy with a lock on us or swinging at us, and missiles
     * flying at us. */
    const int n = acr_gen1_battle_fighters(b);
    const int my_team = acr_gen1_battle_team(b, me);
    for (int j = 0; j < n; ++j) {
        const acr_gen1_fighter *e = &b->f[j];
        if (j == me || acr_gen1_battle_team(b, j) == my_team || e->ap <= 0 || acr_gen1_battle_target(b, j) != me) continue;
        if (e->locked || e->locks) v->target_locked = 1;
        if (e->swing) v->target_swinging = 1;
    }
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        if (s->active && s->missile && s->owner != me && acr_gen1_battle_team(b, s->owner) != my_team &&
            (!b->count || s->target == me) && v->missiles_in < 255) ++v->missiles_in;
    }
    v->arena_half = arena_half;
    if (arena_half) {
        const int32_t fx = acr_gen1_sin(a->yaw), fz = acr_gen1_cos(a->yaw);
        const int64_t cx = -(int64_t)v->x, cz = -(int64_t)v->z;
        v->centre_ahead = (int32_t)((cx * fx + cz * fz) / 4096);
        v->centre_side = (int32_t)((cx * fz - cz * fx) / 4096);
    }
}

/* ---- the brain ----------------------------------------------------------- */

static int rnd(acr_gen1_ai *ai, int below) {            /* 0 .. below - 1 */
    ai->rng = ai->rng * 1103515245u + 12345u;
    return below > 0 ? (int)(((ai->rng >> 16) & 0x7FFF) % (uint32_t)below) : 0;
}
static int chance(acr_gen1_ai *ai, int percent) { return rnd(ai, 100) < percent; }

void acr_gen1_ai_init(acr_gen1_ai *ai, const acr_gen1_ai_profile *p, uint32_t seed) {
    memset(ai, 0, sizeof *ai);
    ai->prof = *p;
    ai->rng = seed ? seed : 0x5EED1234u;
    ai->want_weapon = -1;
    ai->last_ap = -1;
    ai->strafe = (int8_t)(rnd(ai, 2) ? 1 : -1);
}

/* How far the pilot likes to fight, world units: the RANGE slider from
 * blade reach (an AC with a blade) to long range, held inside the weapon's
 * reach. */
static int32_t wanted_distance(const acr_gen1_ai *ai, const acr_gen1_ai_view *v, int wi) {
    const int range = ai->prof.slider[ACR_GEN1_AI_RANGE];
    const int close = v->blade >= 0 && ai->prof.slider[ACR_GEN1_AI_BLADE] >= 50 ? 450 : 900;
    int32_t d = range <= 50 ? lerp100(close, 2400, range * 2) : lerp100(2400, 5200, (range - 50) * 2);
    if (wi >= 0 && v->kind[wi] != ACR_GEN1_BLADE && v->range[wi] > 0) {
        const int32_t reach = v->range[wi] * 4 / 5;
        if (d > reach) d = reach;
    }
    return d < 300 ? 300 : d;
}

static int has_ammo(const acr_gen1_ai_view *v, int k) { return v->ammo[k] != 0; }

/* Which gun or missile suits this distance: a score per weapon, the
 * sliders tilting it. */
static int choose_weapon(acr_gen1_ai *ai, const acr_gen1_ai_view *v) {
    int best = -1, best_score = -1;
    for (int k = 0; k < v->weapons; ++k) {
        if (v->kind[k] == ACR_GEN1_BLADE || !has_ammo(v, k)) continue;
        int score = 50;
        if (v->range[k] && v->dist > v->range[k]) score -= 40;
        if (v->kind[k] == ACR_GEN1_MISSILE) {
            score += v->dist > 1500 ? 20 : -20;
            score += (ai->prof.slider[ACR_GEN1_AI_LOCK] - 50) / 3;
            if (v->target_height > 300) score += 10;      /* missiles chase a flyer */
        } else {
            score += (ai->prof.slider[ACR_GEN1_AI_SHOOT] - 50) / 3;
        }
        if (v->stance[k] && !v->human_plus && v->legs != ACR_GEN1_LEGS_TRACKS) {
            /* A cannon pins the AC down: worth it far away and when not
             * pressed. */
            score += v->dist > 2500 ? 15 : -30;
            score -= ai->prof.slider[ACR_GEN1_AI_AVOID] / 4;
            if (ai->hurt_ms < 1500) score -= 25;
        }
        if (k == v->current) score += 8;                  /* don't flit between weapons */
        if (score > best_score) { best_score = score; best = k; }
    }
    return best;
}

static void decide(acr_gen1_ai *ai, const acr_gen1_ai_view *v) {
    const acr_gen1_ai_profile *p = &ai->prof;
    const int avoid = p->slider[ACR_GEN1_AI_AVOID], fly = p->slider[ACR_GEN1_AI_FLY];
    const int shoot = p->slider[ACR_GEN1_AI_SHOOT], lock = p->slider[ACR_GEN1_AI_LOCK];
    const int skill = p->skill > 100 ? 100 : p->skill;

    /* Reaction time 350 ms (skill 0) to 90 ms (100), with some spread. */
    ai->think_ms = 350 - skill * 26 / 10 + rnd(ai, 80);
    /* Aim: off by up to 120 facing units (about 10 degrees) at skill 0. */
    const int err = (100 - skill) * 12 / 10;
    ai->aim_error = (int16_t)(err ? rnd(ai, err * 2 + 1) - err : 0);

    /* Weapon */
    ai->want_weapon = (int8_t)choose_weapon(ai, v);
    const int wi = ai->want_weapon >= 0 ? ai->want_weapon : v->current;
    const int planted = v->stance[wi] && !v->human_plus && v->legs == ACR_GEN1_LEGS_TWO;

    /* Distance */
    const int32_t want = wanted_distance(ai, v, wi);
    const int32_t band = 250 + avoid * 4;
    ai->move = (int8_t)(v->dist > want + band ? 1 : v->dist < want - band ? -1 : 0);
    if (!ai->move && chance(ai, 25)) ai->move = (int8_t)(rnd(ai, 2) ? 1 : -1);   /* keep it lively */

    /* Side to side: more often and quicker the more it likes to dodge. */
    ai->strafe_ms -= ai->think_ms;
    if (ai->strafe_ms <= 0) {
        if (chance(ai, 35 + avoid / 3)) ai->strafe = (int8_t)-ai->strafe;
        ai->strafe_ms = 2600 - avoid * 18 + rnd(ai, 700);
    }
    const int threatened = v->target_locked || v->missiles_in || v->target_swinging || ai->hurt_ms < 800;

    /* Booster: a dodge burst when threatened, a dash to close a long gap,
     * a jump to get above (FLY), all only with energy to spare. */
    const int32_t reserve = v->energy_max * (15 + (100 - fly) / 4) / 100 +
                            (v->blade >= 0 && p->slider[ACR_GEN1_AI_BLADE] >= 40 ? v->blade_drain : 0);
    if (!ai->boost && !v->drained && v->energy > reserve + v->energy_max / 10) {
        int start = 0, hop = 0, ms = 0;
        if (threatened && chance(ai, avoid * 7 / 10)) { start = 1; ms = 300 + rnd(ai, 400); hop = chance(ai, fly / 2); }
        else if (v->dist > want + 1500 && chance(ai, 30 + fly / 3)) { start = 1; ms = 500 + rnd(ai, 600); }
        else if (chance(ai, fly / 12)) { start = 1; hop = 1; ms = 300 + fly * 8 + rnd(ai, 300); }
        else if (v->target_height > 400 && chance(ai, fly / 4)) { start = 1; hop = 1; ms = 600 + rnd(ai, 400); }
        if (start && !planted) { ai->boost = 1; ai->boost_ms = ms; ai->hop = (uint8_t)hop; }
    }

    /* Trigger: SHOOT is how keen it is; LOCK how long it waits for a lock. */
    ai->fire = 0;
    ai->want_locks = 1;
    if (v->kind[wi] == ACR_GEN1_MISSILE) {
        ai->want_locks = (uint8_t)(1 + (v->max_locks[wi] - 1) * lock / 100);
        if (v->ammo[wi] != 0xFFFF && ai->want_locks > v->ammo[wi]) ai->want_locks = (uint8_t)v->ammo[wi];
        ai->fire = (uint8_t)(v->locks >= ai->want_locks && chance(ai, 40 + shoot * 6 / 10));
    } else if (v->in_box) {
        const int waits = chance(ai, lock);
        ai->fire = (uint8_t)((v->locked || !waits) && chance(ai, 30 + shoot * 7 / 10));
    }
    if (planted) {
        /* Plant for a few seconds, then move again. */
        if (ai->plant_ms <= 0 && v->in_box && v->state == ACR_GEN1_GROUND && !threatened) ai->plant_ms = 1500 + rnd(ai, 1500);
    }
}

void acr_gen1_ai_think(acr_gen1_ai *ai, const acr_gen1_ai_view *v, int step_ms,
                       acr_gen1_input *in, acr_gen1_actions *act) {
    memset(in, 0, sizeof *in);
    memset(act, 0, sizeof *act);
    if (step_ms <= 0) step_ms = 45;
    if (v->ap <= 0) { ai->prev_boost = 0; return; }

    if (ai->last_ap >= 0 && v->ap < ai->last_ap) ai->hurt_ms = 0;
    else if (ai->hurt_ms < 100000) ai->hurt_ms += step_ms;
    ai->last_ap = v->ap;
    if (ai->switch_ms > 0) ai->switch_ms -= step_ms;
    if (ai->blade_ms > 0) ai->blade_ms -= step_ms;
    if (ai->plant_ms > 0) ai->plant_ms -= step_ms;

    if (!v->target_alive) {           /* won: stand and wait */
        ai->boost = 0;
        ai->prev_boost = 0;
        return;
    }

    ai->think_ms -= step_ms;
    if (ai->think_ms <= 0) decide(ai, v);

    const acr_gen1_ai_profile *p = &ai->prof;
    const int wi = ai->want_weapon >= 0 ? ai->want_weapon : v->current;
    const int planted = ai->plant_ms > 0 && v->stance[wi] && !v->human_plus && v->legs == ACR_GEN1_LEGS_TWO;

    /* Face the target. Turning stops at once on release, so let go when
     * the next tick would carry past it. */
    const int32_t off = v->bearing + ai->aim_error;
    const int32_t carry = iabs(v->turn_rate) + (v->turn_accel ? v->turn_accel : 1);
    if (iabs(off) > carry / 2 + 6) in->turn = (int8_t)(off > 0 ? 1 : -1);
    /* Tilt the view with the target's height (L2 / R2). */
    const int32_t tilt = v->elevation - v->pitch;
    if (tilt > ACR_GEN1_PITCH_RATE) in->look = 1;
    else if (tilt < -ACR_GEN1_PITCH_RATE) in->look = -1;

    if (!planted && !v->swinging) {
        in->move = ai->move;
        /* Strafe unless it's lining up a turn it can't make yet. */
        if (iabs(off) < 900) in->strafe = ai->strafe;
        /* Walls: never back or slide into one. */
        if (v->arena_half) {
            const int32_t margin = 700;
            const int32_t from_centre = iabs(v->x) > iabs(v->z) ? iabs(v->x) : iabs(v->z);
            if (from_centre > v->arena_half - margin) {
                if (v->centre_ahead > 0 && in->move < 0) in->move = 0;
                if (v->centre_ahead < -margin) in->move = -1;
                if ((v->centre_side > 0) != (in->strafe > 0) && iabs(v->centre_side) > margin / 2) {
                    in->strafe = (int8_t)(v->centre_side > 0 ? 1 : -1);
                    ai->strafe = in->strafe;
                }
            }
        }
        /* Booster. A jump from a stand; a ground boost needs the AC moving
         * first, so wait for speed. Let go when the energy runs low. */
        if (ai->boost) {
            ai->boost_ms -= step_ms;
            const int32_t floor = v->energy_max * (10 + (100 - p->slider[ACR_GEN1_AI_FLY]) / 5) / 100;
            if (ai->boost_ms <= 0 || v->drained || v->energy < floor) ai->boost = 0;
            else if (ai->hop || v->speed > 0 || v->state != ACR_GEN1_GROUND) in->boost = 1;
            if (in->boost && !ai->hop && !in->move && !in->strafe) in->strafe = ai->strafe;
            /* A jump: the booster alone rises; steer once off the ground. */
            if (in->boost && ai->hop && v->state == ACR_GEN1_GROUND) in->move = in->strafe = 0;
        }
    }

    /* Weapon change: one press at a time, then wait to see it land. */
    if (ai->want_weapon >= 0 && ai->want_weapon != v->current && !v->volley && ai->switch_ms <= 0) {
        act->next_weapon = 1;
        ai->switch_ms = 150;
    }
    /* Trigger: held while the plan says so and the target stays in sight. */
    if (ai->fire && v->current == wi) {
        if (v->kind[wi] == ACR_GEN1_MISSILE) act->fire = (uint8_t)(v->locks >= ai->want_locks || v->volley);
        else act->fire = (uint8_t)(v->in_box || (planted && v->kneeling));
    }
    /* Cannon stance: a press of the trigger kneels; once down, fire only
     * with the target in the box. */
    if (planted) act->fire = (uint8_t)(v->kneeling ? v->in_box : v->state == ACR_GEN1_GROUND);

    /* Blade: close, ahead and with the energy for it. */
    if (v->blade >= 0 && !v->swinging && ai->blade_ms <= 0 && !v->drained &&
        v->energy > (int32_t)v->blade_drain + v->energy_max / 20 &&
        v->dist < 760 && iabs(v->bearing) < 400) {
        if (chance(ai, p->slider[ACR_GEN1_AI_BLADE] / 3 + 5)) act->blade = 1;
        ai->blade_ms = 250 + (100 - (p->skill > 100 ? 100 : p->skill)) * 3;
    }

    in->boost_tapped = (uint8_t)(in->boost && !ai->prev_boost);
    ai->prev_boost = in->boost;
}

void acr_gen1_ai_step(acr_gen1_ai *ai, const acr_gen1_battle *b, int me, int32_t arena_half,
                      acr_gen1_input *in, acr_gen1_actions *act) {
    acr_gen1_ai_view v;
    acr_gen1_ai_look(b, me, arena_half, &v);
    acr_gen1_ai_think(ai, &v, b->step_ms, in, act);
}
