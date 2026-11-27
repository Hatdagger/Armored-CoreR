#include "acr_gen1_battle.h"
#include "acr_gen1_pose.h"

#include <stddef.h>
#include <string.h>

/* 0x800A83D8: (184320 - (x * 4096 - C) / 50) >> 12, limited to 1..45; the
 * screen options raise x by 15 %. Unsigned maths as in the game. */
uint8_t acr_gen1_damage_factor(uint32_t defense, int energy, int screen) {
    const uint32_t c = energy ? 3584000u : 3051520u;
    uint32_t x = defense << 12;
    if (screen) x = (uint32_t)(((uint64_t)defense * 115u << 12) / 100u);
    const uint32_t q = (uint32_t)(((uint64_t)(uint32_t)(x - c) * 0x51EB851Fu) >> 32) >> 4;
    uint32_t v = (uint32_t)(0x2D000u - q) >> 12;
    if (v < 1) v = 1;
    if (v > 45) v = 45;
    return (uint8_t)v;
}

/* 0x80079500: ((attack * factor) >> 6) * 2 / 3. */
int32_t acr_gen1_hit_ap(uint16_t attack, uint8_t factor) {
    const int32_t x = ((int32_t)attack * factor) >> 6;
    return x * 2 / 3;
}

/* 0x80079AA4: impact x 256 / AC +0x16C (stability x 25 / 4), 0..5. */
int acr_gen1_knockback_level(uint16_t impact, uint8_t stability) {
    const uint32_t s = (uint32_t)stability * 25u / 4u;
    if (!impact || !s) return 0;
    const uint32_t v = (uint32_t)impact * 256u / s;
    return v > 5 ? 5 : (int)v;
}

/* Cannon kick (see the header). The slot recoil is the compile's slot +9
 * for cannons (menu module; AC1Recomp stat-block.md "Options in the slot
 * code"); player block +0x214 = recoil x 75 / 32 + 1 (VS 0x80082894). */
uint8_t acr_gen1_cannon_kick(uint8_t base_recoil, uint16_t table_kick, uint32_t stability, uint32_t carried_weight) {
    if (!base_recoil || !table_kick || !stability || !carried_weight) return 0;
    const uint64_t r = ((uint64_t)base_recoil * (4169728u / stability) * (32768000u / carried_weight)) >> 24;
    const uint32_t recoil = r < 1 ? 1u : r > 64 ? 64u : (uint32_t)r;
    const uint32_t k = ((recoil * 75u / 32u + 1u) * table_kick) >> 8;
    return (uint8_t)(k > 255 ? 255 : k);
}

/* Heights on an AC, above its feet, in 1/64 units. */
#define CENTRE_UP (ACR_GEN1_HIT_HEIGHT / 2 * 64)   /* missiles aim here (target y - 106) */
#define MUZZLE_UP (120 * 64)

static void fwd_vec(uint16_t yaw, int32_t *x, int32_t *z) {
    *x = acr_gen1_sin(yaw);
    *z = acr_gen1_cos(yaw);
}

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

/* The angle whose sine and cosine go as s and c (4096 a turn), from the
 * same table as the movement, so it is the same on every machine. */
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

/* Facing (yaw) and climb (pitch, + up) of a world offset (y + is down). */
static void dir_of(int64_t dx, int64_t dy, int64_t dz, uint16_t *yaw, int16_t *pitch) {
    *yaw = angle_of(dx, dz);
    *pitch = (int16_t)wrap(angle_of(-dy, isqrt64((uint64_t)(dx * dx + dz * dz))));
}

/* Unit vector (x 4096) of a heading. */
static void vec_of(uint16_t yaw, int16_t pitch, int32_t *x, int32_t *y, int32_t *z) {
    const int32_t cp = acr_gen1_cos((uint16_t)(pitch & 4095)), sp = acr_gen1_sin((uint16_t)(pitch & 4095));
    *x = (int32_t)((int64_t)acr_gen1_sin(yaw) * cp / 4096);
    *z = (int32_t)((int64_t)acr_gen1_cos(yaw) * cp / 4096);
    *y = -sp;
}

/* Turns `cur` toward `to` by at most `step` (VS 0x800136E0). */
static int32_t turn_toward(int32_t cur, int32_t to, int32_t step) {
    const int32_t d = wrap(to - cur);
    if (d > step) return cur + step;
    if (d < -step) return cur - step;
    return to;
}

/* The battle's own random numbers (0..32767), seeded once. */
static int32_t rnd(acr_gen1_battle *b) {
    if (!b->rng) b->rng = 0x2545F491u;
    b->rng = b->rng * 1103515245u + 12345u;
    return (int32_t)((b->rng >> 16) & 0x7FFF);
}

/* Rate scaling: a Gen 1 per-tick amount, per step, in 1/64 units. */
static int64_t rate_n(const acr_gen1_battle *b) { return b->step_rate && b->gen1_rate ? b->gen1_rate : 1; }
static int64_t rate_d(const acr_gen1_battle *b) { return b->step_rate && b->gen1_rate ? b->step_rate : 1; }
static int32_t per_step(const acr_gen1_battle *b, int32_t gen1_value) {
    return (int32_t)((int64_t)gen1_value * 64 * rate_n(b) / rate_d(b));
}

/* Modern controls' tuning: the battle's value, or the default. */
static unsigned tune(unsigned v, unsigned dflt) { return v ? v : dflt; }
#define MT(b, field, DFLT) tune((b)->modern.field, ACR_GEN1_MODERN_##DFLT)

/* Where the FCS sight points: the facing and view tilt, or with modern
 * controls the lagging sight (see ACR_GEN1_MODERN_SIGHT_LAG). */
static uint16_t view_yaw(const acr_gen1_fighter *f) { return f->modern && f->hist_n ? f->sight_yaw : f->ac.yaw; }
static int16_t view_pitch(const acr_gen1_fighter *f) { return f->modern && f->hist_n ? f->sight_pitch : f->ac.pitch; }

/* Where fighter i's current weapon fires from (1/64 world units). */
static void muzzle_at(const acr_gen1_battle *b, int i, int32_t m[3]) {
    const acr_gen1_fighter *f = &b->f[i];
    const acr_gen1_ac *me = &f->ac;
    const int32_t r = f->muzzle_set ? f->muzzle[0] : 0, u = f->muzzle_set ? f->muzzle[1] : 120,
                  a = f->muzzle_set ? f->muzzle[2] : 0;
    int32_t fx, fz;
    fwd_vec(me->yaw, &fx, &fz);
    /* right = (fz, -fx), as the shots' side offsets */
    m[0] = me->x + (int32_t)(((int64_t)r * fz + (int64_t)a * fx) * 64 / 4096);
    m[1] = me->y - u * 64;
    m[2] = me->z + (int32_t)(((int64_t)a * fz - (int64_t)r * fx) * 64 / 4096);
}

/* ---- fighters, teams, targets ---- */

int acr_gen1_battle_fighters(const acr_gen1_battle *b) {
    /* count 1: a mission map flown alone (the player against the map's
     * units; since 2026-11-27), 0: the old two-AC fight. */
    const int n = b->count ? b->count : 2;
    return n > ACR_GEN1_MAX_FIGHTERS ? ACR_GEN1_MAX_FIGHTERS : n;
}

int acr_gen1_battle_team(const acr_gen1_battle *b, int i) { return b->count ? (b->f[i].team & 1) : i; }

/* ---- targets: fighters 0..3, units ACR_GEN1_UNIT(u) ---- */

static int is_unit(int t) { return t >= ACR_GEN1_MAX_FIGHTERS; }
static int unit_ok(const acr_gen1_battle *b, int t) {
    return is_unit(t) && t - ACR_GEN1_MAX_FIGHTERS < b->units && b->unit[t - ACR_GEN1_MAX_FIGHTERS].active;
}
static int team_of(const acr_gen1_battle *b, int t) {
    return is_unit(t) ? b->unit[t - ACR_GEN1_MAX_FIGHTERS].team & 1 : acr_gen1_battle_team(b, t);
}
static int enemies(const acr_gen1_battle *b, int i, int j) {
    return i != j && team_of(b, i) != team_of(b, j);
}

/* A target as the sights see it: feet and the tick before (1/64 units), how
 * far up its middle is (1/64), alive. */
typedef struct tview { int32_t x, y, z, px, py, pz, up; int alive; uint8_t jammer; } tview;
static int view_of(const acr_gen1_battle *b, int t, tview *v) {
    memset(v, 0, sizeof *v);
    if (t < 0) return 0;
    if (is_unit(t)) {
        if (!unit_ok(b, t)) return 0;
        const acr_gen1_unit *u = &b->unit[t - ACR_GEN1_MAX_FIGHTERS];
        v->x = u->x; v->y = u->y; v->z = u->z;
        v->px = u->px; v->py = u->py; v->pz = u->pz;
        v->up = u->height / 2 * 64;
        v->alive = u->ap > 0 && !u->down;
        return 1;
    }
    if (t >= acr_gen1_battle_fighters(b)) return 0;
    const acr_gen1_fighter *f = &b->f[t];
    v->x = f->ac.x; v->y = f->ac.y; v->z = f->ac.z;
    v->px = f->ac.px; v->py = f->ac.py; v->pz = f->ac.pz;
    v->up = CENTRE_UP;
    v->alive = f->ap > 0;
    v->jammer = f->jammer;
    return 1;
}

int acr_gen1_battle_target_any(const acr_gen1_battle *b, int i) {
    if (!b->count && !b->units) return 1 - i;
    const int t = b->f[i].target;
    if (t < 0) return -1;
    if (is_unit(t)) return unit_ok(b, t) && enemies(b, i, t) ? t : -1;
    return t < acr_gen1_battle_fighters(b) && enemies(b, i, t) ? t : -1;
}

int acr_gen1_battle_target(const acr_gen1_battle *b, int i) {
    const int t = acr_gen1_battle_target_any(b, i);
    return is_unit(t) ? -1 : t;
}

int acr_gen1_battle_target_at(const acr_gen1_battle *b, int t, int32_t mid[3], int32_t *height) {
    tview v;
    if (!view_of(b, t, &v)) return 0;
    mid[0] = v.x / 64; mid[1] = (v.y - v.up) / 64; mid[2] = v.z / 64;
    if (height) *height = v.up * 2 / 64;
    return 1;
}

/* A target's middle seen from fighter i's weapon (VS sight sweep 0x80078230):
 * distance, and its angles off the facing (+ right) and the view tilt (+ up). */
static uint32_t sight_angles_to(const acr_gen1_battle *b, int i, int t, int32_t *dyaw, int32_t *dpitch) {
    tview it;
    view_of(b, t, &it);
    int32_t m[3];
    muzzle_at(b, i, m);
    const int64_t dx = (it.x - m[0]) / 64, dy = (it.y - it.up - m[1]) / 64, dz = (it.z - m[2]) / 64;
    uint16_t yaw;
    int16_t pitch;
    dir_of(dx, dy, dz, &yaw, &pitch);
    *dyaw = wrap((int32_t)yaw - view_yaw(&b->f[i]));
    *dpitch = wrap((int32_t)pitch - view_pitch(&b->f[i]));
    return isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
}

/* Is target t inside fighter i's lock box (angles either side, 4096 a turn) and
 * the range? Modern controls: no FCS without hard lock, and a smaller box. */
static int in_box_of(const acr_gen1_battle *b, int i, int t, int32_t range, uint16_t box_w, uint16_t box_h) {
    tview tv;
    if (!view_of(b, t, &tv) || !tv.alive || !box_w) return 0;
    if (b->f[i].modern && !b->f[i].hard_lock) return 0;
    const unsigned pct = b->f[i].modern ? MT(b, box_pct, BOX_PCT) : 100u;
    int32_t ay, ap;
    (void)sight_angles_to(b, i, t, &ay, &ap);
    {   /* The sweep's range test (VS 0x80078360-0x800783B8): each axis within
         * the range, then the flat distance (x and z only): a lock reaches as
         * far up or down as it does across. */
        int32_t m[3];
        muzzle_at(b, i, m);
        const int64_t dx = (tv.x - m[0]) / 64, dy = (tv.y - tv.up - m[1]) / 64, dz = (tv.z - m[2]) / 64;
        if (dx > range || dx < -range || dy > range || dy < -range || dz > range || dz < -range ||
            dx * dx + dz * dz > (int64_t)range * range) return 0;
    }
    if (pct != 100u) {
        box_w = (uint16_t)(box_w * pct / 100u);
        box_h = (uint16_t)(box_h * pct / 100u);
    }
    return ay >= -(int32_t)box_w && ay <= box_w && ap >= -(int32_t)box_h && ap <= box_h;
}

static int in_box(const acr_gen1_battle *b, int i, int32_t range, uint16_t box_w, uint16_t box_h) {
    return in_box_of(b, i, acr_gen1_battle_target_any(b, i), range, box_w, box_h);
}

static int32_t sight_range(const acr_gen1_weapon *w) { return w->lock_range ? w->lock_range : w->range; }

static void drop_locks(acr_gen1_fighter *f) {
    f->lock_wait = f->lock_lead = 0;
    f->locked = 0;
    f->locks = 0;
    f->lock_count = 0;
    f->cursor_yaw = f->cursor_pitch = 0;
}

static int64_t dist2(const acr_gen1_battle *b, const acr_gen1_ac *a, int t) {
    tview o;
    view_of(b, t, &o);
    const int64_t dx = (o.x - a->x) / 64, dy = (o.y - a->y) / 64, dz = (o.z - a->z) / 64;
    return dx * dx + dy * dy + dz * dz;
}

/* See "Teams and targets" in the header. */
void acr_gen1_battle_retarget_acts(acr_gen1_battle *b, const acr_gen1_actions act[]) {
    if (!b->count && !b->units) return;
    const int n = acr_gen1_battle_fighters(b);
    /* Everything that can be aimed at: the fighters, then the units. */
    int all[ACR_GEN1_MAX_FIGHTERS + ACR_GEN1_MAX_UNITS], na = 0;
    for (int j = 0; j < n; ++j) all[na++] = j;
    for (int u = 0; u < b->units; ++u) if (b->unit[u].active) all[na++] = ACR_GEN1_UNIT(u);
    for (int i = 0; i < n; ++i) {
        acr_gen1_fighter *f = &b->f[i];
        const int old = b->targeted ? acr_gen1_battle_target_any(b, i) : -1;   /* first pick: nothing held */
        if (f->volley_ticks && old >= 0) continue;          /* a volley keeps its target */
        const acr_gen1_weapon *w = f->weapons && f->current < f->weapons ? &f->weapon[f->current] : NULL;
        const int boxed = w && w->kind != ACR_GEN1_BLADE && w->box_w;
        /* Enemies inside the lock box, near to far (equal: lower number first). */
        int in[ACR_GEN1_MAX_FIGHTERS + ACR_GEN1_MAX_UNITS], k = 0;
        int64_t d[ACR_GEN1_MAX_FIGHTERS + ACR_GEN1_MAX_UNITS];
        for (int c = 0; boxed && c < na; ++c) {
            const int j = all[c];
            if (!enemies(b, i, j) || !in_box_of(b, i, j, sight_range(w), w->box_w, w->box_h)) continue;
            const int64_t dj = dist2(b, &f->ac, j);
            int at = k++;
            while (at > 0 && d[at - 1] > dj) { in[at] = in[at - 1]; d[at] = d[at - 1]; --at; }
            in[at] = j;
            d[at] = dj;
        }
        int held = -1;
        for (int c = 0; c < k; ++c) if (in[c] == old) held = c;
        /* The switch: its own button, or weapon switch when there is only one
         * gun / missile to switch to (the blade is not in the cycle). */
        int cycled = 0;
        for (int c = 0; c < f->weapons; ++c) cycled += f->weapon[c].kind != ACR_GEN1_BLADE;
        const int sw = act && (act[i].switch_target || (act[i].next_weapon && cycled <= 1));
        int pick = -1;
        if (held >= 0) pick = sw ? in[(held + 1) % k] : old;
        else if (f->modern && f->hard_lock && old >= 0 && !is_unit(old) && b->f[old].ap > 0) pick = sw && k ? in[0] : old;   /* hard lock holds on */
        else if (k) pick = in[0];
        if (pick < 0) {
            int64_t best = -1;
            for (int c = 0; c < na; ++c) {
                const int j = all[c];
                tview tj;
                if (!enemies(b, i, j) || !view_of(b, j, &tj) || !tj.alive) continue;
                const int64_t dj = dist2(b, &f->ac, j);
                if (best < 0 || dj < best) { best = dj; pick = j; }
            }
        }
        if (pick < 0 && old >= 0) pick = old;                 /* every enemy down: keep looking at one */
        if (pick < 0)
            for (int j = 0; j < n && pick < 0; ++j) if (enemies(b, i, j)) pick = j;
        if (pick < 0 && !b->count) pick = 1 - i;             /* the old two-AC test: the other AC */
        if (pick != old) drop_locks(f);
        f->target = (int8_t)pick;
    }
    b->targeted = 1;
}

void acr_gen1_battle_retarget(acr_gen1_battle *b) { acr_gen1_battle_retarget_acts(b, NULL); }

int acr_gen1_in_lock_box(const acr_gen1_battle *b, int i, const acr_gen1_weapon *w) {
    return in_box(b, i, sight_range(w), w->box_w, w->box_h);
}

int acr_gen1_aim(const acr_gen1_battle *b, int i, int16_t aim[3]) {
    const acr_gen1_fighter *f = &b->f[i];
    aim[0] = aim[1] = aim[2] = 0;
    if (!f->weapons || f->swing || f->current >= f->weapons) return -1;
    const acr_gen1_weapon *w = &f->weapon[f->current];
    if (w->kind == ACR_GEN1_BLADE || w->aim_table == 255) return -1;
    const acr_gen1_ac *me = &f->ac;
    aim[0] = (int16_t)-me->pitch;
    const int t = acr_gen1_battle_target_any(b, i);
    if (t >= 0 && !w->straight && acr_gen1_in_lock_box(b, i, w)) {
        tview it;
        view_of(b, t, &it);
        uint16_t yaw;
        int16_t pitch;
        if (w->kind == ACR_GEN1_GUN && f->locked) { yaw = f->lead_yaw; pitch = f->lead_pitch; }   /* where the shots go */
        else dir_of((it.x - me->x) / 64, (it.y - me->y) / 64, (it.z - me->z) / 64, &yaw, &pitch);
        aim[0] = (int16_t)-pitch;
        aim[w->aim_y ? 1 : 2] = (int16_t)wrap((int32_t)yaw - me->yaw);
    }
    return w->aim_table;
}

static acr_gen1_shot *free_shot(acr_gen1_battle *b) {
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k)
        if (!b->shot[k].active) { memset(&b->shot[k], 0, sizeof b->shot[k]); return &b->shot[k]; }
    return NULL;
}

static void spawn_shot(acr_gen1_battle *b, int i, int wi, int32_t side, int32_t dirx, int32_t diry, int32_t dirz) {
    acr_gen1_shot *s = free_shot(b);
    if (!s) return;
    int32_t sp = per_step(b, b->f[i].weapon[wi].speed);
    if (b->f[i].modern && b->f[i].weapon[wi].straight)        /* modern rockets: slower */
        sp = (int32_t)((int64_t)sp * MT(b, rocket_speed_pct, ROCKET_SPEED_PCT) / 100);
    int32_t m[3];
    muzzle_at(b, i, m);
    s->active = 1;
    s->owner = (uint8_t)i;
    s->weapon = (uint8_t)wi;
    s->x = m[0] + side * dirz / 4096 * 64;
    s->y = m[1];
    s->z = m[2] - side * dirx / 4096 * 64;
    s->vx = (int32_t)((int64_t)dirx * sp / 4096);
    s->vy = (int32_t)((int64_t)diry * sp / 4096);
    s->vz = (int32_t)((int64_t)dirz * sp / 4096);
}

/* 0x8007AEA4: a unit's loss from a hit. Energy: attack x (8 - n) / 8 + 1
 * (n the signed nibble of record +16 bits 8-11); solid: the attack. */
int32_t acr_gen1_unit_damage(uint16_t attack, int energy, int8_t n) {
    if (!energy || !attack) return attack;
    return (int32_t)attack * (8 - n) / 8 + 1;
}

static void unit_hit_turn(acr_gen1_unit *u);

static void damage_by(acr_gen1_battle *b, int target, int attacker, uint16_t attack, int energy) {
    if (b->over) return;                                /* the round is decided: hits no longer count */
    if (is_unit(target)) {
        acr_gen1_unit *u = &b->unit[target - ACR_GEN1_MAX_FIGHTERS];
        if (u->ap <= 0 || u->down) return;
        u->ap -= acr_gen1_unit_damage(attack, energy, u->energy_n);
        u->hit_flash = 8;
        if (u->ap <= 0) { u->ap = 0; u->down = 1; }
        else unit_hit_turn(u);                          /* event 5: the MT turns its step around */
    } else {
        acr_gen1_fighter *t = &b->f[target];
        if (t->ap <= 0) return;
        t->ap -= acr_gen1_hit_ap(attack, energy ? t->energy_factor : t->shell_factor);
        if (t->ap < 0) t->ap = 0;
    }
    if (!is_unit(attacker) && enemies(b, attacker, target))
        b->f[attacker].hit_flash = 40;                  /* attacker's "HIT" blink, counts down a tick */
}

/* Hit stagger (VS 0x80082500; ACR_GEN1_STAGGER_* in the header): a hit of
 * `impact` on fighter `target`, travelling along (dx, dz). */
static void stagger_hit(acr_gen1_battle *b, int target, uint8_t impact, int64_t dx, int64_t dz) {
    if (b->over || !impact || is_unit(target)) return;
    acr_gen1_fighter *t = &b->f[target];
    if (t->ap <= 0 || !t->def) return;
    const int level = acr_gen1_knockback_level(impact, t->def->stats.stability);
    if (!level) return;
    t->hold_left = 0;                                   /* the firing animation is cut short */
    /* Crouched (state 34): pushed in the stance (0x8007B454), no stun.
     * Otherwise the hit state replaces the AC's own (a swing ends) and
     * restarts the stun at this hit's level, running or not (0x8007BC1C). */
    if (!t->kneeling) {
        t->stagger = (uint8_t)(ACR_GEN1_STAGGER_BASE + ACR_GEN1_STAGGER_PER_LEVEL * level);
        if (t->swing) { t->swing = 0; t->wave_armed = 0; t->air_swing = 0; }
        t->hit_side = (uint8_t)acr_gen1_hit_side(t->ac.yaw, dx, dz);
        t->hit_air = t->ac.state == ACR_GEN1_AIR;
        ++t->hit_seq;
    }
    const int64_t len = isqrt64((uint64_t)(dx * dx + dz * dz));
    if (!len) { t->knock_x = t->knock_z = 0; return; }
    int64_t k = (int64_t)impact * ACR_GEN1_KNOCK_PER_IMPACT;
    if (k > ACR_GEN1_KNOCK_MAX) k = ACR_GEN1_KNOCK_MAX;
    /* The push is added to the AC's velocity (0x800826DC / 0x800827AC).
     * On the ground (stun state 0x80) 0x8007BC6C then zeroes the vertical
     * speed and takes 128 off the horizontal speed once; the stun handler
     * (0x8007FECC) leaves the velocity alone while the stun counts down,
     * then the ground rules take over. In the air (0x81) the air rules go
     * on with the push added. */
    acr_gen1_ac *ac = &t->ac;
    ac->vx += (int32_t)(dx * k / len);
    ac->vz += (int32_t)(dz * k / len);
    if (ac->state != ACR_GEN1_AIR) {
        ac->vy = 0;
        const int64_t sp = isqrt64((uint64_t)((int64_t)ac->vx * ac->vx + (int64_t)ac->vz * ac->vz));
        if (sp <= 128) ac->vx = ac->vz = 0;
        else {
            ac->vx = (int32_t)(ac->vx * (sp - 128) / sp);
            ac->vz = (int32_t)(ac->vz * (sp - 128) / sp);
        }
    }
    t->knock_x = t->knock_z = 0;
}

/* Blade lunge distance per Gen 1 tick from ACR_GEN1_BLADE_LUNGE_MS (measured: 516 in all). */
const int16_t acr_gen1_blade_lunge[ACR_GEN1_BLADE_STEPS] = { 125, 125, 125, 50, 44, 24, 23 };

/* Target offset seen from facing `yaw`: how far ahead and to the side. */
static void offset(uint16_t yaw, int64_t dx, int64_t dz, int64_t *ahead, int64_t *side) {
    int32_t fx, fz;
    fwd_vec(yaw, &fx, &fz);
    *ahead = (dx * fx + dz * fz) / 4096;
    *side = (dx * fz - dz * fx) / 4096;
}

static int tick_ms(int k) { return k * 1000 / 22; }
static int kneel_need_ms(const acr_gen1_fighter *f);

/* The blade lunge over the swing's ms [from, to): the measured per-tick
 * distances spread evenly over each tick, stopping at body contact. */
static void lunge_move(acr_gen1_battle *b, int i, int from, int to) {
    acr_gen1_fighter *f = &b->f[i];
    acr_gen1_ac *me = &f->ac;
    const int lunge_end = ACR_GEN1_BLADE_LUNGE_MS + tick_ms(ACR_GEN1_BLADE_STEPS);
    if (f->lunge_done || to <= ACR_GEN1_BLADE_LUNGE_MS || from >= lunge_end) return;
    if (!f->lunge_x && !f->lunge_z) fwd_vec(me->yaw, &f->lunge_x, &f->lunge_z);
    int64_t d = 0;
    for (int ms = from < ACR_GEN1_BLADE_LUNGE_MS ? ACR_GEN1_BLADE_LUNGE_MS : from; ms < to; ++ms) {
        const int k = (ms - ACR_GEN1_BLADE_LUNGE_MS) * 22 / 1000;
        if (k >= ACR_GEN1_BLADE_STEPS) break;
        d += acr_gen1_blade_lunge[k];
    }
    d = d * 64 * 22 / 1000;                                         /* 1/64 units */
    const int n = acr_gen1_battle_fighters(b);
    for (int j = 0; j < n; ++j) {                                   /* bodies: any AC in the way */
        if (j == i) continue;
        const acr_gen1_ac *it = &b->f[j].ac;
        int64_t ahead, side;
        offset(me->yaw, (it->x - me->x) / 64, (it->z - me->z) / 64, &ahead, &side);
        const int64_t gap = ahead - 2 * ACR_GEN1_HIT_RADIUS;
        if ((side < 0 ? -side : side) < 2 * ACR_GEN1_HIT_RADIUS && ahead > 0 && d / 64 >= gap) {
            d = gap > 0 ? gap * 64 : 0;
            f->lunge_done = 1;
        }
    }
    for (int u = 0; u < b->units; ++u) {                            /* and any unit standing there */
        const acr_gen1_unit *un = &b->unit[u];
        if (!un->active || un->down) continue;
        int64_t ahead, side;
        offset(me->yaw, (un->x - me->x) / 64, (un->z - me->z) / 64, &ahead, &side);
        const int64_t reach = (int64_t)un->half + ACR_GEN1_HIT_RADIUS, gap = ahead - reach;
        if ((side < 0 ? -side : side) < reach && ahead > 0 && d / 64 >= gap) {
            d = gap > 0 ? gap * 64 : 0;
            f->lunge_done = 1;
        }
    }
    /* On top of the AC's own velocity, which the swing handler keeps or
     * slows (in.swing): an air slash carries on. */
    acr_gen1_ac_shift(me, (int32_t)(f->lunge_x * d / 4096), (int32_t)(f->lunge_z * d / 4096));
}

/* How far the blade must reach to touch target t from `me` (flat): its
 * middle for an AC, its near side for a unit (an AC-sized unit, half 75,
 * reads as an AC). Fills ahead / side; 0 when t is gone. */
static int blade_gap(const acr_gen1_battle *b, const acr_gen1_ac *me, int t, int64_t *ahead, int64_t *side, int64_t *dist) {
    tview v;
    if (!view_of(b, t, &v) || !v.alive) return 0;
    const int64_t dx = (v.x - me->x) / 64, dz = (v.z - me->z) / 64;
    offset(me->yaw, dx, dz, ahead, side);
    *dist = (int64_t)isqrt64((uint64_t)(dx * dx + dz * dz));
    if (is_unit(t)) {
        const int64_t extra = (int64_t)b->unit[t - ACR_GEN1_MAX_FIGHTERS].half - ACR_GEN1_HIT_RADIUS;
        if (extra > 0) *dist = *dist > extra ? *dist - extra : 0;
    }
    return 1;
}

/* The blade's wave type (its MoA blade id), -1 for a blade the disc
 * doesn't list. */
static int wave_type(const acr_gen1_weapon *w) {
    static const char *const ids[5] = { "LS-2001", "LS-200G", "LS-3303", "LS-99-MOONLIGHT", "LS-1000W" };
    for (int k = 0; k < 5; ++k) if (!strcmp(w->name, ids[k])) return k;
    return -1;
}

/* One blade wave shot along yaw `dir` (see ACR_GEN1_WAVE_TICK). */
static void spawn_wave(acr_gen1_battle *b, int i, int blade, uint16_t dir, uint16_t attack, uint8_t impact,
                       int energy, int32_t speed, int32_t range) {
    acr_gen1_shot *s = free_shot(b);
    if (!s) return;
    int32_t m[3], fx, fz;
    muzzle_at(b, i, m);
    fwd_vec(dir, &fx, &fz);
    const int32_t sp = per_step(b, speed);
    s->active = 1;
    s->owner = (uint8_t)i;
    s->weapon = (uint8_t)blade;
    s->x = m[0]; s->y = m[1]; s->z = m[2];
    s->vx = (int32_t)((int64_t)fx * sp / 4096);
    s->vz = (int32_t)((int64_t)fz * sp / 4096);
    s->wave = 1;
    s->attack = attack;
    s->wave_impact = impact;
    s->wave_energy = (uint8_t)energy;
    s->wave_speed = speed;
    s->wave_range = range;
}

static void blade_wave(acr_gen1_battle *b, int i, int blade) {
    acr_gen1_fighter *f = &b->f[i];
    const acr_gen1_weapon *w = &f->weapon[blade];
    const int type = wave_type(w);
    if (type < 0 || !f->wave_armed) return;
    if (type != 4 && (!f->human_plus || !f->boost_held)) return;
    f->wave_armed = 0;
    const uint16_t yaw = f->ac.yaw;
    switch (type) {
    case 0: spawn_wave(b, i, blade, yaw, 1024, 16, 0, 360, 3600); break;
    case 1: spawn_wave(b, i, blade, yaw, 1792, 16, 0, 360, 3600); break;
    case 2:
        for (int k = -1; k <= 1; ++k) spawn_wave(b, i, blade, (uint16_t)((yaw + k * 128) & 4095), 1024, 16, 0, 360, 3600);
        break;
    case 3: spawn_wave(b, i, blade, yaw, 5120, 32, 0, 600, 10000); break;
    default: spawn_wave(b, i, blade, yaw, w->attack, w->impact, w->energy, 360, 3600); break;
    }
}

/* One Gen 1 tick of an air slash's flight (ACR_GEN1_AIR_SLASH_* in the
 * header; VS 0x8007F264). Sets the swinging AC's facing and velocity; the
 * sim then moves it with no gravity (in.swing_air). */
static void air_slash_tick(acr_gen1_battle *b, int i) {
    acr_gen1_fighter *f = &b->f[i];
    acr_gen1_ac *me = &f->ac;
    const acr_gen1_motion *m = &f->def->motion;
    tview v, mine;
    view_of(b, i, &mine);
    if (f->swing_target >= 0 && view_of(b, f->swing_target, &v) && v.alive) {
        int64_t tvx = 0, tvy = 0, tvz = 0;
        if (!is_unit(f->swing_target)) {
            const acr_gen1_ac *t = &b->f[f->swing_target].ac;
            tvx = t->vx; tvy = t->vy; tvz = t->vz;
        }
        const int64_t mx = me->x, my = (int64_t)me->y - mine.up, mz = me->z;
        const int64_t sp = m->air_max ? m->air_max : 64;
        int64_t px = v.x, py = (int64_t)v.y - v.up, pz = v.z;
        for (int k = 0; k < 5; ++k) {                    /* 0x800155DC: 5 passes */
            const int64_t dx = px - mx, dy = py - my, dz = pz - mz;
            const int64_t ticks = (int64_t)isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz)) / sp;
            px = v.x + tvx * ticks; py = (int64_t)v.y - v.up + tvy * ticks; pz = v.z + tvz * ticks;
        }
        const int64_t dx = px - mx, dz = pz - mz;
        int32_t rate = (int32_t)m->turn_max * ACR_GEN1_AIR_SLASH_TURN_X;
        if (f->modern) rate = (int32_t)(rate * (int32_t)MT(b, steer_pct, STEER_PCT) / 100);
        if (rate < 1) rate = 1;
        acr_gen1_ac_set_yaw(me, (uint16_t)(turn_toward(me->yaw, angle_of(dx / 64, dz / 64), rate) & 4095));
        /* Aim short of the point by both hit radii x 1.25. */
        const int64_t len = isqrt64((uint64_t)(dx * dx + dz * dz));
        const int64_t off = (int64_t)2 * ACR_GEN1_HIT_RADIUS * 64 * 5 / 4;
        int64_t gx = dx, gz = dz;
        if (len) { gx = dx - dx * off / len; gz = dz - dz * off / len; }
        const int64_t top = m->air_max;
        me->vx = (int32_t)(gx > top ? top : gx < -top ? -top : gx);
        me->vz = (int32_t)(gz > top ? top : gz < -top ? -top : gz);
        const int64_t gy = py - my;                      /* y is down */
        const int32_t climb = 4 * (int32_t)m->rise_accel;
        if (gy <= 256) me->vy = gy < -climb ? me->vy - climb : (int32_t)gy;
        else { me->vy += ACR_GEN1_AIR_SLASH_FALL; if (me->vy > ACR_GEN1_AIR_SLASH_FALL_MAX) me->vy = ACR_GEN1_AIR_SLASH_FALL_MAX; }
        return;
    }
    if (f->boost_held && !me->drained && me->energy > 0) {
        me->vy -= (int32_t)m->rise_accel;                /* the rise's energy cost: not modelled yet */
        return;
    }
    me->vy += ACR_GEN1_AIR_SLASH_FALL;
    if (me->vy > ACR_GEN1_AIR_SLASH_FALL_MAX) me->vy = ACR_GEN1_AIR_SLASH_FALL_MAX;
    if (me->vy < -(int32_t)m->rise_max) me->vy = -(int32_t)m->rise_max;
}

/* Blade: a press starts a swing (see acr_gen1_battle.h). */
static void blade_step(acr_gen1_battle *b, int i, int blade, int pressed) {
    acr_gen1_fighter *f = &b->f[i];
    const int step_ms = b->step_ms ? b->step_ms : 45;
    if (f->recover_ms) f->recover_ms = (uint16_t)(f->recover_ms > step_ms ? f->recover_ms - step_ms : 0);
    if (blade < 0) { f->swing = 0; return; }
    const acr_gen1_weapon *w = &f->weapon[blade];
    acr_gen1_ac *me = &f->ac;
    const int n = acr_gen1_battle_fighters(b);
    const int32_t cost = f->modern ? (int32_t)(w->charge_drain * MT(b, blade_en_pct, BLADE_EN_PCT) / 100u)
                                   : (int32_t)w->charge_drain;
    if (pressed && !f->swing && !f->recover_ms && !f->stagger && w->attack && !me->drained && me->energy > cost) {
        me->energy -= cost;
        f->swing = 1;
        f->wave_armed = 1;
        f->swing_hits = 0;
        f->lunge_done = 0;
        f->lunge_x = f->lunge_z = 0;
        f->air_swing = (uint8_t)(me->state == ACR_GEN1_AIR);   /* +0x46 = 0xC0: state 0x21 */
        /* 0x80078230: an AC up to 3,000 ahead within +-45 degrees (enemies
         * only; the nearest). */
        f->swing_target = -1;
        int64_t best = INT64_MAX;
        for (int j = 0; j < ACR_GEN1_UNIT(b->units); ++j) {      /* ACs, then the units */
            if (j >= n && j < ACR_GEN1_MAX_FIGHTERS) continue;
            if (!enemies(b, i, j)) continue;
            int64_t ahead, side, dist;
            if (!blade_gap(b, me, j, &ahead, &side, &dist)) continue;
            if (ahead > 0 && dist <= ACR_GEN1_BLADE_SEEK && (side < 0 ? -side : side) <= ahead && dist < best) {
                best = dist;
                f->swing_target = (int8_t)j;
            }
        }
    }
    if (!f->swing) return;
    const int t0 = f->swing - 1, t1 = t0 + step_ms;                 /* this step's ms window */
    /* Turn toward the target at the AC's top turn rate until the lunge. */
    tview sv;
    if (!f->air_swing && f->swing_target >= 0 && view_of(b, f->swing_target, &sv) && sv.alive &&
        t1 <= tick_ms(ACR_GEN1_BLADE_STEER_TICKS)) {
        const int64_t dx = (sv.x - me->x) / 64, dz = (sv.z - me->z) / 64;
        int32_t rate = (int32_t)((int64_t)f->def->motion.turn_max * rate_n(b) / rate_d(b));
        if (f->modern) rate = (int32_t)(rate * (int32_t)MT(b, steer_pct, STEER_PCT) / 100);
        if (rate < 1) rate = 1;
        acr_gen1_ac_set_yaw(me, (uint16_t)(turn_toward(me->yaw, angle_of(dx, dz), rate) & 4095));
    }
    if (t0 < tick_ms(ACR_GEN1_WAVE_TICK + 5) && t1 >= tick_ms(ACR_GEN1_WAVE_TICK)) blade_wave(b, i, blade);
    const int melee = wave_type(w) != 4;          /* LS-1000W: the wave is the whole attack */
    /* Lunge and the two crossings, in time order within the step. */
    const int reach = w->range ? (int)w->range : ACR_GEN1_BLADE_REACH;
    const int air = f->air_swing;
    const int cross[2] = { air ? tick_ms(ACR_GEN1_AIR_SLASH_HIT_TICK) : ACR_GEN1_BLADE_LUNGE_MS + tick_ms(ACR_GEN1_BLADE_HIT_TICK),
                           air ? tick_ms(ACR_GEN1_AIR_SLASH_HIT_TICK + 1) : ACR_GEN1_BLADE_LUNGE_MS + tick_ms(ACR_GEN1_BLADE_HIT_TICK + 1) };
    /* Air slash: the flight toward the target is the lunge (the battle step
     * sets the velocity) and each hit is the blade's attack x 3. */
    const uint32_t a3 = (uint32_t)w->attack * ACR_GEN1_AIR_SLASH_ATTACK_X;
    const uint16_t attack = air ? (uint16_t)(a3 > 65535u ? 65535u : a3) : w->attack;
    int from = t0;
    for (int c = 0; c <= 2; ++c) {
        const int to = c < 2 ? cross[c] : t1;
        if (to <= t0 || to > t1) { if (c == 2 && !air) lunge_move(b, i, from, t1); continue; }
        if (!air) lunge_move(b, i, from, to);
        from = to;
        if (c == 2 || !melee) continue;
        /* The blade's arc hits every AC in it, teammates too. */
        const int r = c == 0 || air ? reach : reach - (ACR_GEN1_BLADE_REACH - ACR_GEN1_BLADE_REACH2);
        for (int j = 0; j < n; ++j) {
            if (j == i || b->f[j].ap <= 0) continue;
            acr_gen1_ac *it = &b->f[j].ac;
            int64_t ahead, side;
            const int64_t dx = (it->x - me->x) / 64, dz = (it->z - me->z) / 64;
            offset(me->yaw, dx, dz, &ahead, &side);
            if (ahead <= 0 || (side < 0 ? -side : side) > ahead) continue;
            if (isqrt64((uint64_t)(dx * dx + dz * dz)) > (uint32_t)r) continue;
            damage_by(b, j, i, attack, w->energy);
            ++f->swing_hits;
            ++b->f[j].blade_hit_seq;
            f->lunge_done = 1;
            int32_t kx = f->lunge_x, kz = f->lunge_z;
            if (!kx && !kz) fwd_vec(me->yaw, &kx, &kz);
            stagger_hit(b, j, w->impact, kx, kz);
            acr_gen1_ac_shift(it, kx * ACR_GEN1_BLADE_KNOCK * 64 / 4096, kz * ACR_GEN1_BLADE_KNOCK * 64 / 4096);
        }
        for (int u = 0; u < b->units; ++u) {         /* units in the arc: hit, not knocked back */
            const int t = ACR_GEN1_UNIT(u);
            int64_t ahead, side, dist;
            if (!blade_gap(b, me, t, &ahead, &side, &dist)) continue;
            if (ahead <= 0 || (side < 0 ? -side : side) > ahead + b->unit[u].half || dist > r) continue;
            damage_by(b, t, i, attack, w->energy);
            ++f->swing_hits;
            f->lunge_done = 1;
        }
    }
    f->swing = (uint16_t)(t1 + 1);
    /* The swing ends once its animation (56) has played out, frame 12 at
     * the earliest (VS 0x8007EFB8: AC +0x76 >= 12 and the animation done);
     * 26 ticks (1.18 s) on most bodies. Without the track: the measured
     * swing-to-swing time. */
    const acr_gen1_root_track *sw = f->def ? f->def->anim[air ? ACR_GEN1_SHOW_AIR_SWING : ACR_GEN1_SHOW_SWING] : NULL;
    const int swing_len = sw && sw->length >= 12 && sw->length <= 64 ? tick_ms(sw->length) : ACR_GEN1_BLADE_SWING_MS;
    if (t1 >= swing_len) {
        f->swing = 0;
        f->air_swing = 0;
        /* Master of Arena drops every lock when the swing ends and the
         * busy bit clears (VS 0x8007EFDC -> 0x80080084); Project Phantasma
         * keeps them (ACG1PLUS feel-diff.md 4.1 row 7). */
        drop_locks(f);
        if (f->modern)         /* standing still after the swing: short on a hit, long on a miss */
            f->recover_ms = (uint16_t)(f->swing_hits ? MT(b, recover_hit_ms, RECOVER_HIT_MS)
                                                     : MT(b, recover_miss_ms, RECOVER_MISS_MS));
    }
}

/* ---- missiles ---- */

static void launch_missile(acr_gen1_battle *b, int i, int wi, int32_t side, int32_t fan) {
    acr_gen1_fighter *f = &b->f[i];
    const acr_gen1_weapon *w = &f->weapon[wi];
    const int t = acr_gen1_battle_target_any(b, i);
    tview it;
    if (!view_of(b, t, &it)) return;
    acr_gen1_shot *s = free_shot(b);
    if (!s) return;
    const acr_gen1_ac *me = &f->ac;
    s->target = (int8_t)t;
    f->last_missile = (int16_t)(s - b->shot);
    s->active = 1;
    s->missile = 1;
    s->owner = (uint8_t)i;
    s->weapon = (uint8_t)wi;
    int32_t fx, fz;
    fwd_vec(me->yaw, &fx, &fz);
    s->x = me->x + side * fz / 4096 * 64;
    s->y = me->y - MUZZLE_UP;
    s->z = me->z - side * fx / 4096 * 64;
    /* Start: toward the target plus a random spread, at 100 plus the launcher's own motion.
     * The game (0x800815A4) actually points it along the launcher's own
     * joints (0x80078888); for the vertical launcher (flight kind 4,
     * WM-SMSS24) that is straight up. Until the joints are read, kind 4
     * leaves straight up over the AC's facing and the rest at the target. */
    dir_of((it.x - s->x) / 64, (it.y - it.up - s->y) / 64, (it.z - s->z) / 64, &s->yaw, &s->pitch);
    if (w->flight.kind == 4) { s->yaw = me->yaw; s->pitch = 1024 - 16; }
    /* Kind 3 (WM-X201, XCS-9900, the split missiles): the game's start is
     * the tube's own heading plus a quarter of the turn to the target
     * (0x8008171C: lock offset >> 2), so it climbs away up front and the
     * four it breaks into dive onto the target. Until the tube vertices are
     * read the tube is taken as the facing, tilted 30 degrees up. */
    if (w->flight.kind == 3) {
        s->yaw = (uint16_t)((me->yaw + wrap(s->yaw - me->yaw) / 4) & 4095);
        s->pitch = (int16_t)wrap(0x155 + s->pitch / 4);
    }
    s->yaw = (uint16_t)((s->yaw + fan) & 4095);
    if (w->spread_yaw) s->yaw = (uint16_t)((s->yaw + rnd(b) % (w->spread_yaw + 1) - w->spread_yaw / 2) & 4095);
    if (w->spread_pitch) s->pitch = (int16_t)wrap(s->pitch + rnd(b) % (w->spread_pitch + 1) - w->spread_pitch / 2);
    int32_t ux, uy, uz;
    vec_of(s->yaw, s->pitch, &ux, &uy, &uz);
    s->vx = ux * 100 * 64 / 4096 + me->vx;
    s->vy = uy * 100 * 64 / 4096 + me->vy;
    s->vz = uz * 100 * 64 / 4096 + me->vz;
    s->flight = w->flight;
    s->ignition = w->ignition;
    s->split = w->flight.kind == 3 ? 6 : 0;
}

/* One Gen 1 tick of a missile's guidance (VS update 0x8008907C). */
static void missile_tick(acr_gen1_battle *b, int k) {
    acr_gen1_shot *s = &b->shot[k];
    const acr_gen1_flight *fl = &s->flight;
    tview t;
    const int alive = s->target != s->owner && view_of(b, s->target, &t) && t.alive;
    ++s->age;
    if (s->age >= fl->life) { s->active = 0; return; }
    uint16_t tyaw = s->yaw;
    int16_t tpitch = s->pitch;
    if (alive) dir_of((t.x - s->x) / 64, (t.y - t.up - s->y) / 64, (t.z - s->z) / 64, &tyaw, &tpitch);
    if (s->age < s->ignition) {
        if (fl->kind == 4) {
            /* The vertical missile (0x80089168 -> 0x8008951C): before
             * ignition it doesn't fall. It flies along its heading, 8 a
             * tick faster up to its top speed, the heading turning toward
             * the target (with lead, 0x80088F60) at the flight's turn rate:
             * up out of the launcher, then over onto the target. */
            if (alive) {
                s->yaw = (uint16_t)(turn_toward(s->yaw, tyaw, fl->turn) & 4095);
                s->pitch = (int16_t)wrap(turn_toward(s->pitch, tpitch, fl->turn));
            }
            const int64_t top = (int64_t)fl->top * 64;
            int64_t sp = isqrt64((uint64_t)((int64_t)s->vx * s->vx + (int64_t)s->vy * s->vy + (int64_t)s->vz * s->vz)) + 8 * 64;
            if (sp > top) sp = top;
            int32_t ux, uy, uz;
            vec_of(s->yaw, s->pitch, &ux, &uy, &uz);
            s->vx = (int32_t)(ux * sp / 4096); s->vy = (int32_t)(uy * sp / 4096); s->vz = (int32_t)(uz * sp / 4096);
            return;
        }
        s->vy += ACR_GEN1_GRAVITY;                           /* coasting, unlit */
        return;
    }
    if (s->split && s->age == s->ignition + s->split) {    /* kind 3: four kind-0 missiles */
        const acr_gen1_weapon *w = &b->f[s->owner].weapon[s->weapon];
        const acr_gen1_shot parent = *s;
        s->active = 0;
        for (int c = 0; c < 4; ++c) {
            acr_gen1_shot *n = free_shot(b);
            if (!n) break;
            *n = parent;
            n->active = 1;
            n->split = 0;
            n->age = 0;
            n->ignition = 0;
            n->flags = 0;
            n->flight = w->split;
            n->yaw = (uint16_t)((parent.yaw + ((c & 1) ? 0x1C0 : -0x1C0)) & 4095);
            n->pitch = (int16_t)wrap(parent.pitch - ((c & 2) ? 0x200 : 0));
        }
        return;
    }
    if (s->age >= fl->fuel) {                                /* burnt out: the nose drops */
        s->pitch = (int16_t)turn_toward(s->pitch, -1024, 16);
        s->vy += 8 * 64;
        return;
    }
    if (fl->kind == 5 || fl->kind == 11) s->flags &= (uint8_t)~2u;
    if (alive && !(s->flags & 2)) {
        const int32_t dy = wrap(tyaw - s->yaw), dp = wrap(tpitch - s->pitch);
        const int inside = (dy < 0 ? -dy : dy) <= fl->cone && (dp < 0 ? -dp : dp) <= fl->cone;
        if (inside) s->flags |= 1;
        else if (s->flags & 1) s->flags |= 2;                /* left the cone: gives up for good */
    }
    if (alive && !(s->flags & 2)) {
        s->yaw = (uint16_t)(turn_toward(s->yaw, tyaw, fl->turn) & 4095);
        s->pitch = (int16_t)wrap(turn_toward(s->pitch, tpitch, fl->turn));
    } else if (rnd(b) < 3277) {                              /* 10 %: a random wobble */
        s->yaw = (uint16_t)((s->yaw + (rnd(b) & 64) - 32) & 4095);
    }
    int32_t ux, uy, uz;
    vec_of(s->yaw, s->pitch, &ux, &uy, &uz);
    int64_t vx = s->vx + (int64_t)ux * fl->thrust * 64 / 4096, vy = s->vy + (int64_t)uy * fl->thrust * 64 / 4096,
            vz = s->vz + (int64_t)uz * fl->thrust * 64 / 4096;
    const int64_t top = (int64_t)fl->top * 64;
    const uint32_t len = isqrt64((uint64_t)(vx * vx + vy * vy + vz * vz));
    if (len > top) { vx = vx * top / len; vy = vy * top / len; vz = vz * top / len; }
    s->vx = (int32_t)vx; s->vy = (int32_t)vy; s->vz = (int32_t)vz;
}

/* Missile lock (VS 0x80080638), one tick: the cursor turns toward the
 * target by lock value x 3 / 16; once it sits on it a counter runs, and
 * passing (256 - FCS value) / 8 adds a lock and sends the cursor back to
 * the centre. A new lock needs the inner box; the locks hold while the
 * target stays in the outer one. */
static void missile_lock_tick(acr_gen1_battle *b, int i, const acr_gen1_weapon *w, uint16_t ammo) {
    acr_gen1_fighter *f = &b->f[i];
    tview tv;
    view_of(b, acr_gen1_battle_target_any(b, i), &tv);
    const tview *t = &tv;
    const int outer = in_box(b, i, sight_range(w), w->box_w, w->box_h);
    const int inner = in_box(b, i, sight_range(w), w->inner_w ? w->inner_w : w->box_w, w->inner_h ? w->inner_h : w->box_h);
    if (!outer || (!f->locks && !inner) || (t->jammer && f->locks && rnd(b) < 1639)) {
        f->locks = 0;
        f->lock_count = 0;
        f->cursor_yaw = f->cursor_pitch = 0;
        f->tone_tick = 0;
        return;
    }
    unsigned cap = w->max_locks ? w->max_locks : 1;
    if (cap > ammo) cap = ammo;
    if (cap > ACR_GEN1_MAX_LOCKS) cap = ACR_GEN1_MAX_LOCKS;
    if (f->locks >= cap) { f->tone_tick = 0; return; }
    if (!(f->tone_tick++ & 3)) ++f->tones;                 /* the locking beep, every 4th tick */
    uint16_t tyaw;
    int16_t tpitch;
    dir_of((t->x - f->ac.x) / 64, (t->y - f->ac.y) / 64, (t->z - f->ac.z) / 64, &tyaw, &tpitch);
    const int32_t ry = wrap(tyaw - view_yaw(f)), rp = wrap(tpitch - view_pitch(f));
    const int32_t sp = w->lock_value * 3 / 16 > 0 ? w->lock_value * 3 / 16 : 1;
    f->cursor_yaw = (int16_t)turn_toward(f->cursor_yaw, ry, sp);
    f->cursor_pitch = (int16_t)turn_toward(f->cursor_pitch, rp, sp);
    if (f->cursor_yaw != ry || f->cursor_pitch != rp) return;
    unsigned need = w->lock_ticks;
    if (f->modern) { need = need * MT(b, missile_pct, MISSILE_PCT) / 100u; if (need > 254) need = 254; }
    if (++f->lock_count > need) {
        ++f->locks;
        f->lock_count = 0;
        f->cursor_yaw = f->cursor_pitch = 0;
    }
}

/* Missile firing (VS 0x80081888), one tick: with a lock and fire held (or
 * SP-M/AUTO with every lock taken) a volley run starts: locks x 4 ticks,
 * a volley from both sides every fourth, one lock each. */
static void missile_fire_tick(acr_gen1_battle *b, int i, int wi, int fire) {
    acr_gen1_fighter *f = &b->f[i];
    const acr_gen1_weapon *w = &f->weapon[wi];
    if (f->volley_ticks) {
        if (--f->volley_ticks % 4 == 0) {
            /* Side A, then side B (VS 0x80081A40 / 0x80081A68: one launch
             * call per side, each with that side's joints). A one-slot unit
             * fires from its own shoulder; a two-slot unit's A is the left.
             * The game points each missile along its own launcher tube
             * (vertex pairs of the launcher model, 0x80078888); until those
             * are read, several from one side leave fanned out across the
             * facing, 0x100 apart, from tubes 20 apart. */
            const int32_t a_sign = w->back_side == 1 ? 1 : -1;
            for (int s = 0; s < w->volley_a + w->volley_b && f->ammo[wi]; ++s) {
                const int on_b = s >= w->volley_a;
                const int n = on_b ? s - w->volley_a : s, cnt = on_b ? w->volley_b : w->volley_a;
                const int32_t sign = on_b ? -a_sign : a_sign;
                const int32_t off2 = 2 * n - (cnt - 1);              /* tube offset x 2 from the middle */
                launch_missile(b, i, wi, sign * 60 + off2 * 10, off2 * 0x80 * sign);
                --f->ammo[wi];
            }
            if (f->locks) --f->locks;
            if (!f->volley_ticks) { f->cooldown = (uint16_t)(w->interval + 1); f->locks = 0; }   /* + 1: as guns (VS 0x800818F0) */
        }
        return;
    }
    missile_lock_tick(b, i, w, f->ammo[wi]);
    unsigned cap = w->max_locks ? w->max_locks : 1;
    if (cap > f->ammo[wi]) cap = f->ammo[wi];
    if (!f->cooldown && f->locks && (fire || (w->auto_launch && f->locks >= cap)))
        f->volley_ticks = (uint8_t)(f->locks * 4);
}

/* Missile interceptor (VS 0x800829E4), one tick for fighter i. */
static void intercept_tick(acr_gen1_battle *b, int i) {
    acr_gen1_fighter *f = &b->f[i];
    const acr_gen1_stats *st = &f->def->stats;
    const int bursting = f->intercept_burst > 0;
    if (bursting) {
        if (!(--f->intercept_burst & 1)) return;            /* a burst shoots on odd ticks */
    } else if ((rnd(b) & 127) >= st->intercept_chance) {
        return;
    }
    const int32_t cone = st->intercept_angle * 4;
    int best = -1;
    uint32_t best_d = ACR_GEN1_INTERCEPT_RANGE + 1;
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        if (!s->active || !s->missile || s->owner == i ||
            acr_gen1_battle_team(b, s->owner) == acr_gen1_battle_team(b, i)) continue;
        const int64_t dx = (s->x - f->ac.x) / 64, dy = (s->y - (f->ac.y - MUZZLE_UP)) / 64, dz = (s->z - f->ac.z) / 64;
        if (dx > ACR_GEN1_INTERCEPT_RANGE || dx < -ACR_GEN1_INTERCEPT_RANGE || dy > ACR_GEN1_INTERCEPT_RANGE ||
            dy < -ACR_GEN1_INTERCEPT_RANGE || dz > ACR_GEN1_INTERCEPT_RANGE || dz < -ACR_GEN1_INTERCEPT_RANGE) continue;
        const uint32_t d = isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
        if (d > ACR_GEN1_INTERCEPT_RANGE || d >= best_d) continue;
        uint16_t yaw;
        int16_t pitch;
        dir_of(dx, dy, dz, &yaw, &pitch);
        const int32_t ry = wrap(yaw - f->ac.yaw);
        if ((ry < 0 ? -ry : ry) > cone || (pitch < 0 ? -pitch : pitch) > cone) continue;
        best = k;
        best_d = d;
    }
    if (best < 0) return;
    if (!bursting) { f->intercept_burst = ACR_GEN1_INTERCEPT_BURST; f->intercept_shot = (int16_t)best; return; }
    f->intercept_shot = (int16_t)best;
    if (rnd(b) < 1024) b->shot[best].active = 0;            /* 1 in 32 shots brings it down */
}

/* ---- per fighter ---- */

/* The chase camera's swing, one tick (VS 0x8008774C; see the header). */
static void camera_tick(acr_gen1_battle *b, int i) {
    acr_gen1_fighter *f = &b->f[i];
    const int ti = acr_gen1_battle_target_any(b, i);
    tview tv;
    view_of(b, ti, &tv);
    const tview *it = &tv;
    f->cam_pitch_prev = f->cam_pitch;
    f->cam_yaw_prev = f->cam_yaw;
    int32_t to_pitch = 0, to_yaw = 0, step = 20;
    if (ti >= 0 && tv.alive && f->ap > 0 && f->weapons && f->current < f->weapons) {
        const acr_gen1_weapon *w = &f->weapon[f->current];
        const acr_gen1_ac *me = &f->ac;
        const int16_t lm = f->last_missile;
        const int missile_out = lm >= 0 && lm < ACR_GEN1_MAX_SHOTS && b->shot[lm].active &&
                                b->shot[lm].missile && b->shot[lm].owner == i;
        const int seen = w->kind != ACR_GEN1_BLADE && w->box_w && acr_gen1_in_lock_box(b, i, w);
        if (missile_out || seen) {
            uint16_t yaw;
            int16_t pitch;
            dir_of((it->x - me->x) / 64, (it->y - me->y) / 64, (it->z - me->z) / 64, &yaw, &pitch);
            const int32_t ry = wrap((int32_t)yaw - me->yaw), rp = wrap((int32_t)pitch - me->pitch);
            if (missile_out) {           /* v - (v >> 2), as the game rounds */
                to_pitch = rp - (rp >> 2);
                to_yaw = ry - (ry >> 2);
                step = 64;
            } else {
                to_pitch = rp >> 1;
                to_yaw = ry >> 1;
            }
        }
    }
    if (f->modern && f->look_init) {
        /* The free camera: its own heading; with hard lock also the target's height. */
        to_yaw = wrap((int32_t)f->look_yaw - f->ac.yaw);
        to_pitch = 0;
        if (f->hard_lock && tv.alive) {
            uint16_t yaw;
            int16_t pitch;
            dir_of((tv.x - f->ac.x) / 64, (tv.y - f->ac.y) / 64, (tv.z - f->ac.z) / 64, &yaw, &pitch);
            to_pitch = wrap((int32_t)pitch - f->ac.pitch);
        }
        f->cam_yaw = (int16_t)to_yaw;
        /* Hard lock pins the target to the middle of the screen. */
        f->cam_pitch = (int16_t)(f->hard_lock && tv.alive ? to_pitch : turn_toward(f->cam_pitch, to_pitch, 64));
        return;
    }
    f->cam_pitch = (int16_t)turn_toward(f->cam_pitch, to_pitch, step);
    f->cam_yaw = (int16_t)turn_toward(f->cam_yaw, to_yaw, step);
}

/* Modern controls: the FCS sight follows where the facing was a moment
 * ago, no faster than the AC turns (ACR_GEN1_MODERN_SIGHT_LAG), one tick. */
static void sight_tick(acr_gen1_battle *b, int i) {
    acr_gen1_fighter *f = &b->f[i];
    const acr_gen1_ac *me = &f->ac;
    if (!f->modern) return;
    for (int k = 3; k > 0; --k) { f->hist_yaw[k] = f->hist_yaw[k - 1]; f->hist_pitch[k] = f->hist_pitch[k - 1]; }
    f->hist_yaw[0] = me->yaw;
    f->hist_pitch[0] = me->pitch;
    if (!f->hist_n) { f->sight_yaw = me->yaw; f->sight_pitch = me->pitch; }
    if (f->hist_n < 4) ++f->hist_n;
    unsigned lag = MT(b, sight_lag, SIGHT_LAG);
    if (lag > 3) lag = 3;
    if (lag > (unsigned)f->hist_n - 1) lag = (unsigned)f->hist_n - 1;
    int32_t rate = f->def ? f->def->motion.turn_max : 32;
    if (rate < 1) rate = 1;
    f->sight_yaw = (uint16_t)(turn_toward(f->sight_yaw, f->hist_yaw[lag], rate) & 4095);
    f->sight_pitch = (int16_t)turn_toward(f->sight_pitch, f->hist_pitch[lag], ACR_GEN1_PITCH_RATE);
}

static void fighter_tick(acr_gen1_battle *b, int i, int fire) {
    acr_gen1_fighter *f = &b->f[i];
    sight_tick(b, i);
    camera_tick(b, i);
    if (f->hit_flash) --f->hit_flash;
    if (f->bloom) {                                     /* modern: the bloom settles */
        const unsigned d = MT(b, bloom_decay, BLOOM_DECAY);
        f->bloom = (uint16_t)(f->bloom > d ? f->bloom - d : 0);
    }
    if (f->def) intercept_tick(b, i);
    if (f->ap <= 0 || !f->weapons) return;
    const acr_gen1_weapon *w = &f->weapon[f->current];
    /* Post-shot hold and refire wait. The VS gun and cannon routines
     * (0x80081020, 0x800823EC) only count the wait down while no firing
     * animation is playing (AC +0x6D = 255), so the wait pauses during the
     * hold for every gun, not just cannons. */
    if (f->hold_left) --f->hold_left;
    else if (f->cooldown) --f->cooldown;
    if (w->kind == ACR_GEN1_MISSILE) missile_fire_tick(b, i, f->current, fire && !f->stagger && acr_gen1_stance_ok(f));
}

/* Gun lock, one tick (VS 0x800800E4; see ACR_GEN1_LEAD_MAX in the header). */
static void gun_lock_tick(acr_gen1_battle *b, int i, const acr_gen1_weapon *w) {
    acr_gen1_fighter *f = &b->f[i];
    f->locked = 0;
    if (!w->lock_ms || w->straight ||
        !in_box(b, i, sight_range(w), w->inner_w ? w->inner_w : w->box_w, w->inner_h ? w->inner_h : w->box_h)) {
        f->lock_wait = f->lock_lead = 0;
        return;
    }
    tview tv;                                               /* in the box: a target */
    view_of(b, acr_gen1_battle_target_any(b, i), &tv);
    const tview *t = &tv;
    int32_t m[3];
    muzzle_at(b, i, m);
    /* The target's middle and its movement over the last tick. */
    const int64_t px = t->x / 64, py = (t->y - t->up) / 64, pz = t->z / 64;
    const int64_t vx = (t->x - t->px) / 64, vy = (t->y - t->py) / 64, vz = (t->z - t->pz) / 64;
    const int64_t mx = m[0] / 64, my = m[1] / 64, mz = m[2] / 64;
    const uint32_t speed = isqrt64((uint64_t)(vx * vx + vy * vy + vz * vz));
    const uint32_t dist = isqrt64((uint64_t)((px - mx) * (px - mx) + (py - my) * (py - my) + (pz - mz) * (pz - mz)));
    const int32_t need = (int32_t)(((int64_t)(speed * w->lock_value >> 8) * dist) >> 15);
    if ((int16_t)(need - f->lock_wait) > 0) {       /* still waiting */
        ++f->lock_wait;
        f->lock_lead = 0;
        return;
    }
    /* Aim ahead: where the target will be when a shot gets there (0x800155DC). */
    int n = f->lock_lead >> 2;
    if (n > ACR_GEN1_LEAD_MAX) n = ACR_GEN1_LEAD_MAX;
    const int64_t shot = w->speed > 0 ? w->speed : 700;
    int64_t ax = px, ay = py, az = pz;
    for (;;) {
        const int64_t d = isqrt64((uint64_t)((ax - mx) * (ax - mx) + (ay - my) * (ay - my) + (az - mz) * (az - mz)));
        if (--n < 0) break;
        const int64_t k = d / shot;
        ax = px + vx * k; ay = py + vy * k; az = pz + vz * k;
    }
    if (f->modern) {          /* the weaker FCS leads only part of the way */
        const int64_t pct = MT(b, lead_pct, LEAD_PCT);
        ax = px + (ax - px) * pct / 100; ay = py + (ay - py) * pct / 100; az = pz + (az - pz) * pct / 100;
    }
    dir_of(ax - mx, ay - my, az - mz, &f->lead_yaw, &f->lead_pitch);
    f->lock_wait = (uint16_t)(need + 0x20);
    if (f->lock_lead < 0x4000) ++f->lock_lead;
    f->locked = f->lock_lead >= need;
}

static void fighter_step(acr_gen1_battle *b, int i, acr_gen1_actions a, int ticks) {
    acr_gen1_fighter *f = &b->f[i];
    if (f->ap <= 0 || !f->weapons) {
        for (int t = 0; t < ticks; ++t) fighter_tick(b, i, 0);
        return;
    }
    /* The blade is not in the cycle: guns and missiles only. */
    int blade = -1, cycled = 0;
    for (int k = 0; k < f->weapons; ++k) {
        if (f->weapon[k].kind == ACR_GEN1_BLADE) blade = k;
        else ++cycled;
    }
    const int blade_current = f->weapon[f->current].kind == ACR_GEN1_BLADE && cycled;
    if (blade_current) a.next_weapon = 1;
    if (a.next_weapon && cycled) {
        /* The next weapon that still has ammo (user, 2026-11-19; the game's
         * pick skips slots with no rounds left, VS 0x80082FB0): an empty
         * one stays selected until the pilot switches away (no automatic
         * swap), and can't be picked again; with nothing else left the
         * switch does nothing. A press counts on from a switch still
         * waiting (+0x11), cuts a missile volley short (+0x47 = 0) and
         * drops the locks (0x80083038). */
        const uint8_t from = f->switch_pending ? f->switch_to : f->current;
        uint8_t next = from;
        for (int k = 1; k <= f->weapons; ++k) {
            const uint8_t c = (uint8_t)((from + k) % f->weapons);
            const acr_gen1_weapon *wc = &f->weapon[c];
            if (wc->kind == ACR_GEN1_BLADE || (wc->ammo && !f->ammo[c])) continue;
            next = c;
            break;
        }
        if (f->weapon[next].kind == ACR_GEN1_BLADE)       /* only the blade was current */
            for (int k = 0; k < f->weapons; ++k) if (f->weapon[k].kind != ACR_GEN1_BLADE) { next = (uint8_t)k; break; }
        f->switch_to = next;
        f->switch_pending = next != f->current;
        f->volley_ticks = 0;
        drop_locks(f);
    }
    /* The pick becomes the weapon once the refire wait is 0 (VS 0x80083044:
     * +0x48 = 0), so switching waits out the reload (and the post-shot
     * hold, during which the wait doesn't count). A kneeling AC stands up
     * (0x800830A8). Leaving the blade (not in the game's cycle) is at once. */
    if (f->switch_pending && (!f->cooldown || blade_current)) {
        f->current = f->switch_to;
        f->switch_pending = 0;
        drop_locks(f);
        if (f->kneeling) acr_gen1_stand_up(b, f);
    }
    for (int t = 0; t < ticks; ++t) fighter_tick(b, i, a.fire);
    blade_step(b, i, blade, a.blade);
    if (!cycled) return;
    const acr_gen1_weapon *w = &f->weapon[f->current];
    if (w->kind == ACR_GEN1_MISSILE) { f->locked = f->locks > 0; return; }

    for (int t = 0; t < ticks; ++t) {
        gun_lock_tick(b, i, w);
        /* The build-up beep: in the sight, not locked yet (VS 0x800804A4). */
        if (!f->locked && w->lock_ms && !w->straight && acr_gen1_in_lock_box(b, i, w)) {
            if (!(f->tone_tick++ & 1)) ++f->tones;
        } else f->tone_tick = 0;
    }

    if (!a.fire || f->cooldown || f->hold_left) return;
    if (w->ammo && !f->ammo[f->current]) return;
    if (f->modern && w->energy && f->ac.drained) return;   /* modern: no energy weapons until the bar is full */
    if (!acr_gen1_stance_ok(f)) return;
    if (f->swing || f->recover_ms || f->stagger) return;
    int32_t ax, ay, az;
    if (f->locked && !w->straight) {
        vec_of(f->lead_yaw, f->lead_pitch, &ax, &ay, &az);   /* VS 0x80081150: the lock's aim */
    } else {
        /* Along the facing, tilted with the view (+y is down). Modern: along
         * the FCS sight, which may still be swinging round after a snap. */
        const uint16_t p = (uint16_t)(view_pitch(f) & 4095);
        fwd_vec(view_yaw(f), &ax, &az);
        ax = (int32_t)((int64_t)ax * acr_gen1_cos(p) / 4096);
        az = (int32_t)((int64_t)az * acr_gen1_cos(p) / 4096);
        ay = -acr_gen1_sin(p);
    }
    const int n = w->shots ? w->shots : 1;
    /* Modern: spread and bloom (ACR_GEN1_MODERN_SPREAD). */
    uint16_t aim_yaw = 0;
    int16_t aim_pitch = 0;
    const int32_t cone = f->modern ? (int32_t)MT(b, spread, SPREAD) + f->bloom : 0;
    if (cone) dir_of(ax, ay, az, &aim_yaw, &aim_pitch);
    for (int s = 0; s < n; ++s) {
        if (w->ammo && !f->ammo[f->current]) break;
        if (cone) {
            const int32_t oy = rnd(b) % (2 * cone + 1) - cone, op = (rnd(b) % (cone + 1)) - cone / 2;
            vec_of((uint16_t)((aim_yaw + oy) & 4095), (int16_t)(aim_pitch + op), &ax, &ay, &az);
        }
        spawn_shot(b, i, f->current, n == 1 ? 0 : (s * 2 - (n - 1)) * 20, ax, ay, az);
        if (w->ammo) --f->ammo[f->current];
    }
    /* Cannon kick (VS 0x80081E80, once a trigger): the AC's motion loses
     * kick world units a tick along the shot; Human PLUS standing on the
     * ground (body frame 3, class 0x10) none. */
    if (w->kick && w->stance && !(f->human_plus && f->ac.state != ACR_GEN1_AIR)) {
        int32_t kx = ax, ky = ay, kz = az;
        const int64_t len = isqrt64((uint64_t)((int64_t)kx * kx + (int64_t)ky * ky + (int64_t)kz * kz));
        if (len > 0) {
            f->ac.vx -= (int32_t)((int64_t)kx * w->kick * 64 / len);
            f->ac.vz -= (int32_t)((int64_t)kz * w->kick * 64 / len);
            if (f->ac.state == ACR_GEN1_AIR) f->ac.vy -= (int32_t)((int64_t)ky * w->kick * 64 / len);
        }
    }
    if (f->modern) {
        const unsigned mx = MT(b, bloom_max, BLOOM_MAX);
        f->bloom = (uint16_t)(f->bloom + MT(b, bloom_shot, BLOOM_SHOT) > mx ? mx : f->bloom + MT(b, bloom_shot, BLOOM_SHOT));
    }
    /* The refire wait (VS 0x80081034): a tick with the wait above 0 counts
     * it down and does nothing else; the gun fires on a tick that starts at
     * 0. So a wait of n fires every n + 1 ticks (WG-MGA1, n = 1: every
     * other tick, 11 a second). Ours counts down before the fire test,
     * hence the + 1. */
    f->cooldown = (uint16_t)(w->interval + 1);
    if (f->modern && w->straight)                    /* modern rockets: far slower to refire */
        f->cooldown = (uint16_t)(w->interval * MT(b, rocket_rof_pct, ROCKET_ROF_PCT) / 100u + 1);
    f->hold_left = w->hold;
}

/* Does the segment p0 -> p1 pass through the AC's cylinder (feet at fy)?
 * World units; y + is down. */
static int segment_hits_ac(int64_t x0, int64_t y0, int64_t z0, int64_t x1, int64_t y1, int64_t z1,
                           int64_t fx, int64_t fy, int64_t fz) {
    const int64_t dx = x1 - x0, dz = z1 - z0, dy = y1 - y0;
    const int64_t len2 = dx * dx + dz * dz;
    int64_t t = 1024;
    if (len2 > 0) {
        t = ((fx - x0) * dx + (fz - z0) * dz) * 1024 / len2;
        if (t < 0) t = 0;
        if (t > 1024) t = 1024;
    }
    const int64_t px = x0 + dx * t / 1024 - fx, pz = z0 + dz * t / 1024 - fz, py = y0 + dy * t / 1024;
    if (px * px + pz * pz > (int64_t)ACR_GEN1_HIT_RADIUS * ACR_GEN1_HIT_RADIUS) return 0;
    return py <= fy && py >= fy - ACR_GEN1_HIT_HEIGHT;
}

/* The same for a unit's box: `half` either side of (fx, fz) on the world
 * axes (as the game's object boxes), `height` up from the feet. The test is
 * at the segment's closest approach to the box's middle line. */
static int segment_hits_box(int64_t x0, int64_t y0, int64_t z0, int64_t x1, int64_t y1, int64_t z1,
                            int64_t fx, int64_t fy, int64_t fz, int64_t half, int64_t height) {
    const int64_t dx = x1 - x0, dz = z1 - z0, dy = y1 - y0;
    const int64_t len2 = dx * dx + dz * dz;
    int64_t t = 1024;
    if (len2 > 0) {
        t = ((fx - x0) * dx + (fz - z0) * dz) * 1024 / len2;
        if (t < 0) t = 0;
        if (t > 1024) t = 1024;
    }
    const int64_t px = x0 + dx * t / 1024 - fx, pz = z0 + dz * t / 1024 - fz, py = y0 + dy * t / 1024;
    if (px < -half || px > half || pz < -half || pz > half) return 0;
    return py <= fy && py >= fy - height;
}

static void units_step(acr_gen1_battle *b, int ticks);
static void bodies_step(acr_gen1_battle *b);

static void shots_step(acr_gen1_battle *b, int ticks) {
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        acr_gen1_shot *s = &b->shot[k];
        if (!s->active) continue;
        const int from_unit = is_unit(s->owner);
        const acr_gen1_fighter *owner = &b->f[from_unit ? 0 : s->owner];
        const acr_gen1_weapon *w = from_unit || s->wave ? NULL : &owner->weapon[s->weapon];
        const int32_t x0 = s->x, y0 = s->y, z0 = s->z;
        if (s->missile) {
            for (int t = 0; t < ticks && s->active; ++t) missile_tick(b, k);
            if (!s->active) continue;
            s->x += (int32_t)((int64_t)s->vx * rate_n(b) / rate_d(b));
            s->y += (int32_t)((int64_t)s->vy * rate_n(b) / rate_d(b));
            s->z += (int32_t)((int64_t)s->vz * rate_n(b) / rate_d(b));
        } else {
            s->x += s->vx;
            s->y += s->vy;
            s->z += s->vz;
            int32_t sp = per_step(b, w ? w->speed : s->wave ? s->wave_speed : ACR_GEN1_UNIT_SHOT);
            if (w && owner->modern && w->straight) sp = (int32_t)((int64_t)sp * MT(b, rocket_speed_pct, ROCKET_SPEED_PCT) / 100);
            s->travelled += sp / 64;                    /* same range at the slower speed */
        }
        /* Any AC but the shooter (friendly fire); the nearest along the way. */
        const int n = acr_gen1_battle_fighters(b);
        int target = -1;
        int64_t target_t = 0;
        const int64_t sdx = s->x / 64 - x0 / 64, sdz = s->z / 64 - z0 / 64, slen2 = sdx * sdx + sdz * sdz;
        for (int j = 0; j < n; ++j) {
            if (j == s->owner || b->f[j].ap <= 0) continue;
            const acr_gen1_ac *t = &b->f[j].ac;
            if (!segment_hits_ac(x0 / 64, y0 / 64, z0 / 64, s->x / 64, s->y / 64, s->z / 64, t->x / 64, t->y / 64, t->z / 64))
                continue;
            const int64_t tt = slen2 ? ((int64_t)(t->x / 64 - x0 / 64) * sdx + (int64_t)(t->z / 64 - z0 / 64) * sdz) * 1024 / slen2 : 0;
            if (target < 0 || tt < target_t) { target = j; target_t = tt; }
        }
        /* Units: their box (a unit's shots pass its own team's units, as the
         * game's shots only test the other side's objects). */
        for (int u = 0; u < b->units; ++u) {
            const acr_gen1_unit *un = &b->unit[u];
            if (!un->active || un->down || ACR_GEN1_UNIT(u) == s->owner) continue;
            if (from_unit && (b->unit[s->owner - ACR_GEN1_MAX_FIGHTERS].team & 1) == (un->team & 1)) continue;
            if (!segment_hits_box(x0 / 64, y0 / 64, z0 / 64, s->x / 64, s->y / 64, s->z / 64,
                                  un->x / 64, un->y / 64, un->z / 64, un->half, un->height))
                continue;
            const int64_t tt = slen2 ? ((int64_t)(un->x / 64 - x0 / 64) * sdx + (int64_t)(un->z / 64 - z0 / 64) * sdz) * 1024 / slen2 : 0;
            if (target < 0 || tt < target_t) { target = ACR_GEN1_UNIT(u); target_t = tt; }
        }
        const int hits_ac = target >= 0;
        /* The arena stops shots (VS 0x80074FA4 tests the stage before the
         * ACs); here the nearer of the two along the way wins. */
        int32_t wall_t = 0;
        int hits_wall = 0;
        if (b->stage) {
            const int32_t a[3] = { x0 / 64, y0 / 64, z0 / 64 }, e[3] = { s->x / 64, s->y / 64, s->z / 64 };
            hits_wall = acr_gen1_stage_segment(b->stage, a, e, &wall_t);
        }
        const uint16_t attack = w ? w->attack : s->attack;
        const int energy = w ? w->energy : s->wave_energy;
        const uint8_t impact = w ? w->impact : s->wave_impact;
        if (hits_ac && hits_wall) {
            /* How far along the way the AC's middle is, against the wall. */
            if (target_t <= wall_t) {
                damage_by(b, target, s->owner, attack, energy);
                stagger_hit(b, target, impact, s->vx, s->vz);
            }
            s->active = 0;
        } else if (hits_ac) {
            damage_by(b, target, s->owner, attack, energy);
            stagger_hit(b, target, impact, s->vx, s->vz);
            s->active = 0;
        } else if (hits_wall) {
            s->active = 0;                              /* into the arena */
        } else if ((!s->missile && s->travelled > (w ? w->range : s->wave ? s->wave_range : ACR_GEN1_UNIT_RANGE)) || (!b->stage && s->y > 0)) {
            s->active = 0;                              /* out of range or into the floor */
        }
    }
}

int acr_gen1_stance_ok(const acr_gen1_fighter *f) {
    if (!f->weapons) return 1;
    const acr_gen1_weapon *w = &f->weapon[f->current];
    if (!w->stance || f->human_plus) return 1;
    switch (f->legs) {
    case ACR_GEN1_LEGS_TRACKS: return 1;
    case ACR_GEN1_LEGS_FOUR: return f->ac.state == ACR_GEN1_GROUND;
    default:
        return f->kneeling && f->kneel_ms >= kneel_need_ms(f);
    }
}

/* Two-legged ACs crouch when told to fire a cannon on the ground (AC state
 * 34; VS 0x80082340: fire held, not in the air or ground boosting); crouched
 * they turn at the crouch rates (legs +0x3C) and slide to a stop (the sim's
 * crouch input: velocity - 1/8 a tick, zero below 512, VS 0x8007E3E0).
 * The kneel plays out in full; once down, moving with the refire wait at 0
 * and not firing stands them up, which takes as long as the kneel took
 * (state 0x23; see ACR_GEN1_KNEEL_MS). */
static int kneel_need_ms(const acr_gen1_fighter *f) {
    const acr_gen1_root_track *k = f->def ? f->def->anim[ACR_GEN1_ANIM_KNEEL] : NULL;
    return k && k->length > 0 && k->length <= 64 ? tick_ms(k->length) : ACR_GEN1_KNEEL_MS;
}

void acr_gen1_stand_up(acr_gen1_battle *b, acr_gen1_fighter *f) {
    (void)b;
    if (!f->kneeling) return;
    const int need = kneel_need_ms(f);
    f->stand_ms = f->kneel_ms < need ? f->kneel_ms : need;
    f->kneeling = 0;
    f->kneel_ms = 0;
}

static acr_gen1_input kneel_input(acr_gen1_battle *b, acr_gen1_fighter *f, acr_gen1_input in, acr_gen1_actions a) {
    if (f->stand_ms > 0) {                          /* standing up: the controls are held */
        f->stand_ms -= b->step_ms;
        if (f->stand_ms < 0) f->stand_ms = 0;
        in.move = in.strafe = 0;
        in.boost = in.boost_tapped = 0;
        in.crouch = 1;
        return in;
    }
    const acr_gen1_weapon *w = f->weapons ? &f->weapon[f->current] : NULL;
    const int wants = w && w->stance && !f->human_plus && f->legs == ACR_GEN1_LEGS_TWO;
    if (!wants) { f->kneeling = 0; return in; }
    const int moving = in.move || in.strafe || in.boost;
    const int firing = a.fire && (!w->ammo || f->ammo[f->current]);
    if (!f->kneeling && a.fire && !in.boost && f->ac.state == ACR_GEN1_GROUND) {
        f->kneeling = 1;
        f->kneel_ms = 0;
    } else if (f->kneeling && f->kneel_ms >= kneel_need_ms(f) && moving && !firing && !f->cooldown) {
        acr_gen1_stand_up(b, f);
        in.move = in.strafe = 0;
        in.boost = in.boost_tapped = 0;
        in.crouch = 1;
        return in;
    }
    if (f->kneeling) {
        f->kneel_ms += b->step_ms;
        in.move = in.strafe = 0;
        in.boost = in.boost_tapped = 0;
        in.crouch = 1;
    }
    return in;
}

/* Modern controls' free camera and hard lock (see ACR_GEN1_MODERN_CAM_RATE).
 * Without hard lock the right stick (in.turn) spins the camera and the AC
 * turns toward the camera's heading at its turn rate. R3 hard lock pins the
 * camera on the target at any distance (it drops when the target is down);
 * the AC then turns freely on the right stick and tilts with in.look, and
 * while the pilot fires or swings the blade it turns and tilts toward the
 * target at its legs' turn rate and the normal tilt rate: that is the aim,
 * and the FCS (which follows the facing) still has to bring the target into
 * its box and lock as normal. The left stick is always taken relative to
 * the camera and turned into the AC's own eight directions. */
static acr_gen1_input modern_input(acr_gen1_battle *b, int i, acr_gen1_input in, acr_gen1_actions act, int ticks) {
    acr_gen1_fighter *f = &b->f[i];
    const int t = acr_gen1_battle_target(b, i);       /* co-op / Rogue Arena: the picked enemy */
    const acr_gen1_ac *me = &f->ac, *it = &b->f[t >= 0 ? t : i].ac;
    if (!f->look_init) { f->look_yaw = me->yaw; f->look_init = 1; }
    if (act.hard_lock) {
        f->hard_lock = (uint8_t)!f->hard_lock;
        if (f->hard_lock) {                             /* starting it: close enough (0 = anywhere) */
            const uint32_t start = b->modern.hard_lock_start ? b->modern.hard_lock_start : ACR_GEN1_MODERN_HARD_LOCK_START;
            const int64_t dx = (it->x - me->x) / 64, dz = (it->z - me->z) / 64;
            if (start && isqrt64((uint64_t)(dx * dx + dz * dz)) > start) f->hard_lock = 0;
        }
    }
    if (t < 0 || b->f[t].ap <= 0) f->hard_lock = 0;
    int32_t dead = f->def ? f->def->motion.turn_max : 16;     /* within a tick's turn: stop */
    if (dead < 16) dead = 16;
    const int8_t stick_turn = in.turn;
    if (f->hard_lock) {
        uint16_t yaw;
        int16_t pitch;
        dir_of((it->x - me->x) / 64, (it->y - me->y) / 64, (it->z - me->z) / 64, &yaw, &pitch);
        f->look_yaw = yaw;                              /* the target stays in the middle */
    } else if (in.turn) {
        f->look_yaw = (uint16_t)((f->look_yaw + in.turn * ACR_GEN1_MODERN_CAM_RATE * ticks) & 4095);
    }
    /* The stick, relative to the camera, as one of the AC's eight directions. */
    const int moving = in.move || in.strafe;
    int32_t stick_yaw = f->look_yaw;                    /* the stick's way in the world */
    if (moving) {
        static const int8_t k_move[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, k_strafe[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        int h = 0;
        for (int k = 0; k < 8; ++k) if (k_move[k] == in.move && k_strafe[k] == in.strafe) h = k;
        stick_yaw = (f->look_yaw + h * 512) & 4095;
        const int32_t rel = (int32_t)((f->look_yaw + h * 512 - me->yaw + 256) & 4095) / 512;
        in.move = k_move[rel];
        in.strafe = k_strafe[rel];
    }
    in.turn = 0;
    if (in.crouch) return in;
    if (f->hard_lock) {
        if (act.fire || act.blade) {
            /* Firing / attacking: the AC snaps to face the target. The FCS
             * sight does not: it swings round from where the AC faced at the
             * legs' turn rate (sight_tick), so the lock has to catch up. */
            uint16_t yaw;
            int16_t pitch;
            dir_of((it->x - me->x) / 64, (it->y - CENTRE_UP - (me->y - MUZZLE_UP)) / 64, (it->z - me->z) / 64, &yaw, &pitch);
            int32_t pf = (int32_t)pitch * 256;
            if (pf > ACR_GEN1_PITCH_MAX * 256) pf = ACR_GEN1_PITCH_MAX * 256;
            if (pf < -ACR_GEN1_PITCH_MAX * 256) pf = -ACR_GEN1_PITCH_MAX * 256;
            acr_gen1_ac_set_yaw(&f->ac, yaw);
            f->ac.pitch_fine = pf;
            f->ac.pitch = (int16_t)(pf / 256);
            in.look = 0;
            if (in.move || in.strafe) {                 /* strafing: re-take the stick for the new facing */
                static const int8_t k_move[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, k_strafe[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
                const int32_t rel = (int32_t)((stick_yaw - f->ac.yaw + 256) & 4095) / 512;
                in.move = k_move[rel];
                in.strafe = k_strafe[rel];
            }
        } else if (moving) {                            /* dodging: the AC turns to face its way */
            const int32_t d = wrap(stick_yaw - me->yaw);
            in.turn = (int8_t)(d > dead ? 1 : d < -dead ? -1 : 0);
        } else {
            in.turn = stick_turn;                       /* standing: the right stick turns the AC */
        }
        return in;
    }
    /* No hard lock: the AC turns toward the camera's heading at its own rate. */
    const int32_t d = wrap((int32_t)f->look_yaw - me->yaw);
    in.turn = (int8_t)(d > dead ? 1 : d < -dead ? -1 : 0);
    return in;
}

void acr_gen1_battle_step(acr_gen1_battle *b, const acr_gen1_input in[], const acr_gen1_actions act[]) {
    const int n = acr_gen1_battle_fighters(b);
    /* Gen 1 ticks that fall in this step, and how far the shown ACs are
     * from the tick before to the latest. */
    int ticks = 1;
    uint32_t num = 1, den = 1;
    if (b->step_rate && b->gen1_rate) {
        b->tick_phase += b->gen1_rate;
        ticks = (int)(b->tick_phase / b->step_rate);
        b->tick_phase %= b->step_rate;
        num = b->tick_phase;
        den = b->step_rate;
    }
    for (int i = 0; i < n; ++i) {
        acr_gen1_fighter *f = &b->f[i];
        if (f->ap <= 0) {
            /* Down: no pilot, but the game goes on, so it falls to the ground
             * where it is (it used to hang in the air). */
            const acr_gen1_input still = { 0 };
            f->stagger = 0; f->knock_x = f->knock_z = 0;
            acr_gen1_ac_advance(&f->ac, f->def, still, ticks, num, den);
            continue;
        }
        f->boost_held = (uint8_t)(in[i].boost != 0);
        acr_gen1_input use = kneel_input(b, f, in[i], act[i]);
        if (!f->modern) { use.cruise = use.quick = use.modern = 0; f->hard_lock = 0; }
        else { use = modern_input(b, i, use, act[i], ticks); use.modern = 1; }
        if (f->recover_ms) use.move = use.strafe = use.boost = use.boost_tapped = use.cruise = use.quick = 0;
        if (f->swing) use.quick = use.cruise = 0;
        if (f->swing) {                        /* the swing handler moves the AC (sim: in.swing) */
            const int k = (f->swing - 1) * 22 / 1000;
            use.swing = (uint8_t)(k > 250 ? 251 : k + 1);
            use.swing_fwd = (uint8_t)(use.move > 0);
            use.swing_air = f->air_swing;
            if (f->air_swing && f->ac.state == ACR_GEN1_AIR)
                for (int t = 0; t < ticks; ++t) air_slash_tick(b, i);
            use.move = use.strafe = use.boost = use.boost_tapped = 0;
        }
        if (f->stagger) {                      /* hit stagger: the pilot has no say */
            use.stun = 1;
            use.move = use.strafe = use.turn = 0;
            use.boost = use.boost_tapped = use.cruise = use.quick = use.crouch = 0;
        }
        f->used = use;
        acr_gen1_ac_advance(&f->ac, f->def, use, ticks, num, den);
        for (int t = 0; t < ticks; ++t)        /* the push is in the velocity (stagger_hit) */
            if (f->stagger) --f->stagger;
    }
    acr_gen1_battle_retarget_acts(b, act);
    for (int i = 0; i < n; ++i) fighter_step(b, i, act[i], ticks);
    units_step(b, ticks);
    bodies_step(b);
    shots_step(b, ticks);
}

int acr_gen1_battle_out_of_area(const acr_gen1_battle *b, int i) {
    if (!b->stage || !acr_gen1_stage_has_area_rule(b->stage)) return 0;
    const acr_gen1_ac *ac = &b->f[i].ac;
    const uint8_t bits = acr_gen1_stage_area_bits(b->stage, ac->x / 64, ac->y / 64, ac->z / 64);
    /* The arena engine's end check (0x800528E4): bits 0-2 exactly 1, i.e.
     * inside box 0 (the whole level) and outside both inner boxes. Outside
     * box 0 too (below the sea, say) is not out of area. */
    if ((bits & 7) == 1) return 2;
    return (bits & 1) && (bits & ACR_GEN1_AREA_INNER) != ACR_GEN1_AREA_INNER ? 1 : 0;
}

int acr_gen1_battle_result(const acr_gen1_battle *b, int time_up) {
    /* Out of area (outside both inner boxes) loses like a knock-out; a team
     * is down when every AC in it is. */
    const int n = acr_gen1_battle_fighters(b);
    /* Armored-CoreR's own missions (acr_gen1_mission.h): the player against
     * fighter 1 only; units (P4's drones) never keep a side standing. */
    if (b->mission.own) {
        /* Co-op on one PC: both players are team 0, the opponent is mission.foe. */
        const int fo = acr_gen1_mission_foe(b);
        int p_down = 1;
        int64_t p_ap = 0;
        for (int i = 0; i < n; ++i)
            if (i != fo && acr_gen1_battle_team(b, i) == 0 && b->f[i].ap > 0 && acr_gen1_battle_out_of_area(b, i) != 2) {
                p_down = 0;
                p_ap += b->f[i].ap;
            }
        const int f_down = b->f[fo].ap <= 0 || acr_gen1_battle_out_of_area(b, fo) == 2;
        if (b->mission.own == ACR_GEN1_OWN_P4_ALTERED && b->mission.phase < 2) {
            if (p_down) return 2;
            if (b->mission.phase || !time_up) return 0;    /* the challenger comes in instead */
        } else {
            if (p_down || f_down) return p_down && f_down ? 3 : p_down ? 2 : 1;
            if (!time_up) return 0;
        }
        return p_ap > b->f[fo].ap ? 1 : b->f[fo].ap > p_ap ? 2 : 3;
    }
    int standing[2] = { 0, 0 }, members[2] = { 0, 0 };
    int64_t ap[2] = { 0, 0 };
    for (int i = 0; i < n; ++i) {
        const int t = acr_gen1_battle_team(b, i);
        ++members[t];
        if (b->f[i].ap > 0 && acr_gen1_battle_out_of_area(b, i) != 2) { ++standing[t]; ap[t] += b->f[i].ap; }
    }
    /* Missions: a unit in the fight keeps its side standing, so the
     * mission is won when every enemy target is down (ACs and units), and
     * a side of units alone still counts (no AC on it). Dormant units
     * (not active) wait for the script and don't count; a map with a script
 * (acr_gen1_mission.h) is won by its objective instead. Time-up AP stays
     * the ACs' (units keep theirs in other units). */
    for (int u = 0; u < b->units; ++u) {
        const acr_gen1_unit *un = &b->unit[u];
        if (!un->active) continue;
        const int t = un->team & 1;
        if (!t) continue;               /* units on the player's side (map 12's) fight beside it, don't keep it up */
        if (!members[t]) ++members[t];
        if (un->ap > 0 && !un->down) ++standing[t];
    }
    const int down0 = members[0] && !standing[0], down1 = members[1] && !standing[1];
    /* A map with a script: its objective wins it (map 6: the big MT, the
     * tank AC is a bonus), whatever else stands; the player's side down
     * still loses, and so does the script's "failed" (map 12: the friendly
     * units all lost). */
    if (b->mission.map) {
        if (down0) return 2;
        return b->mission.result == ACR_GEN1_MISSION_DONE ? 1 : b->mission.result == ACR_GEN1_MISSION_FAILED ? 2 : 0;
    }
    if (down0 || down1) return down0 && down1 ? 3 : down0 ? 2 : 1;
    if (!time_up) return 0;
    return ap[0] > ap[1] ? 1 : ap[1] > ap[0] ? 2 : 3;
}

/* ---- Units ----
 * Read from mission 6's MT (handler 0x800A3E48; docs/research/gen1-stages.md,
 * "Mission unit behaviour"); the same decision code sits in its big MT's
 * handler, so it is taken as the missions' common MT. Each tick:
 * - Target (0x8007931C): the nearest enemy, measured as 2 x the flat
 *   distance (it halves x and z before the square root and returns x 4).
 * - Decision (event routine 0x800A3A9C), when a step ends: measure > 2,500
 *   -> AHEAD; < 1,000 -> BACK; else a random number below 80 picks AHEAD
 *   (6 in 80), the six diagonals and side steps (2 each), TURN_L / TURN_R
 *   (4 each) or nothing (44): it keeps what it does.
 * - A hit (event 5) turns the step around: 1-6, 2-8, 3-7, 4-5.
 * - Turning (states 9 / 10, 0x800A30F4): the turn speed grows by 8 a tick
 *   up to 24 and is added to the facing.
 * - Gun (0x800A38B4 / 0x800A3714): it aims while the target is within
 *   7,000, +-1,024 of the facing and +-256 up or down; the gun turns 16 a
 *   tick (0x800136E0) and is on aim once a tick's turn reaches it. On aim
 *   with no burst running, 2,622 in 32,768 a tick a burst starts (328 of
 *   those skip the line-of-sight test 0x8007DEA4); a burst is 6 ticks, a
 *   shot on each odd one (3 shots), the muzzles taking turns.
 * - Walking (0x800A3430 -> 0x800A32AC): each step state plays a start
 *   animation, then loops its walk; from another state's walk it goes
 *   straight into the new walk; after one of two linking animations it
 *   waits for that to end. The root track moves it (0x80055138, one frame
 *   a tick, the AC's stepper; the direction fixed at each key span):
 *     state   1   2   3   4   5   6   7   8
 *     walk    2   4   6   9  11  13  15  17
 *     start   1   3   5   8  10  12  14  16
 *   Map 6's MT: walk 2 is 646 / 4 forward in 24 frames; the big MT's 2,075.
 * Stand-ins: a decision every UNIT_STEP_TICKS (who sends events 0-4 is not
 * read); 0x800A3194 (may it start the step?) taken as yes; it faces its
 * target at UNIT_FACE a tick while walking; a unit without a motion set
 * walks UNIT_WALK a tick (UNIT_SIDE sideways or back); a step that would
 * leave the floor or meet a wall is turned around as a hit is. */
#define UNIT_STEP_TICKS 24
#define UNIT_WALK       16
#define UNIT_SIDE       11
#define UNIT_FACE       24
#define UNIT_GUN_TURN   16

static const uint8_t k_unit_swap[11] = { 0, 6, 8, 7, 5, 4, 1, 3, 2, 9, 10 };

static const uint8_t k_unit_walk[9]  = { 0, 2, 4, 6, 9, 11, 13, 15, 17 };
static const uint8_t k_unit_start[9] = { 0, 1, 3, 5, 8, 10, 12, 14, 16 };
static const uint8_t k_unit_link[9][2] = { { 0, 0 }, { 3, 5 }, { 1, 5 }, { 1, 3 }, { 255, 255 },
                                           { 255, 255 }, { 14, 16 }, { 12, 16 }, { 12, 14 } };

static void unit_hit_turn(acr_gen1_unit *u) {
    if (u->brain == ACR_GEN1_BRAIN_MT && u->state < 11) u->state = k_unit_swap[u->state];
}

int acr_gen1_battle_add_unit(acr_gen1_battle *b, const acr_gen1_unit *u) {
    if (b->units >= ACR_GEN1_MAX_UNITS) return -1;
    acr_gen1_unit *n = &b->unit[b->units];
    *n = *u;
    n->px = n->x; n->py = n->y; n->pz = n->z;
    n->state = ACR_GEN1_UNIT_IDLE;
    n->state_ticks = (uint8_t)(1 + (b->units * 7) % UNIT_STEP_TICKS);   /* +76: not all at once */
    n->burst = n->cooldown = 0;
    n->gun_yaw = n->gun_pitch = 0;
    n->down = 0;
    n->target = -1;
    n->turn = 0;
    n->anim = 0;
    n->anim_frame = n->span_left = 0;
    if (!n->ap_max) n->ap_max = n->ap;
    return b->units++;
}

static void unit_decide(acr_gen1_battle *b, acr_gen1_unit *u, int64_t measure) {
    static const uint8_t k_pick[36] = { 1, 1, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
                                        0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 10, 10, 10, 10 };
    int next = -1;
    if (measure > ACR_GEN1_UNIT_FAR) next = ACR_GEN1_UNIT_AHEAD;
    else if (measure < ACR_GEN1_UNIT_NEAR) next = ACR_GEN1_UNIT_BACK;
    else {
        const int r = rnd(b) * 80 / 32768;
        if (r < 36 && k_pick[r]) next = k_pick[r];
    }
    if (next >= 0) u->state = (uint8_t)next;
    if (u->state != ACR_GEN1_UNIT_TURN_L && u->state != ACR_GEN1_UNIT_TURN_R) u->turn = 0;
    u->state_ticks = UNIT_STEP_TICKS;
}

/* Moves it (dx, dz) world units: the stage stops it at walls (the game's
 * box move) and it won't step off its floor; either turns the step around. */
static void unit_move(acr_gen1_battle *b, acr_gen1_unit *u, int32_t dx, int32_t dz) {
    int32_t step[3] = { dx, 0, dz };
    if (!step[0] && !step[2]) return;
    const int32_t pos[3] = { u->x / 64, u->y / 64, u->z / 64 };
    if (b->stage) {
        const int32_t want[3] = { step[0], step[1], step[2] };
        acr_gen1_stage_move(b->stage, pos, step, u->half, u->height, ACR_GEN1_STAGE_AC_MASK);
        const int32_t to[3] = { pos[0] + step[0], pos[1], pos[2] + step[2] };
        int32_t floor_y = 0;
        const int blocked = step[0] != want[0] || step[2] != want[2];
        const int grounded = acr_gen1_stage_floor_probe(b->stage, to, ACR_GEN1_STAGE_PROBE_HALF, u->height,
                                                        ACR_GEN1_STAGE_AC_MASK, &floor_y) &&
                             floor_y - to[1] < ACR_GEN1_STAGE_GROUNDED && floor_y >= to[1] - 1;
        if (!grounded || (blocked && !step[0] && !step[2])) { unit_hit_turn(u); return; }
        if (blocked) unit_hit_turn(u);
    }
    u->x += step[0] * 64;
    u->z += step[2] * 64;
}

static void unit_walk(acr_gen1_battle *b, acr_gen1_unit *u, uint16_t way, int32_t speed) {
    unit_move(b, u, acr_gen1_sin(way) * speed / 4096, acr_gen1_cos(way) * speed / 4096);
}

static const acr_gen1_root_track *unit_track(const acr_gen1_unit *u, unsigned a) {
    return u->motion && a < ACR_GEN1_UNIT_ANIMS ? u->motion[a] : NULL;
}

static void unit_set_anim(acr_gen1_unit *u, uint8_t a) {
    u->anim = a;
    u->anim_frame = u->span_left = 0;
}

static int32_t unit_div_round(int32_t a, int32_t b) {
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

/* One tick of a step state by the animations (0x800A32AC, then the root
 * motion of 0x80055138). Returns 0 when the state's walk has no track. */
static int unit_anim_walk(acr_gen1_battle *b, acr_gen1_unit *u) {
    const int st = u->state;
    const uint8_t walk = k_unit_walk[st], start = k_unit_start[st];
    if (!unit_track(u, walk)) return 0;
    const acr_gen1_root_track *cur = unit_track(u, u->anim);
    int cur_is_walk = 0;
    for (int k = 1; k <= 8; ++k) cur_is_walk |= u->anim == k_unit_walk[k];
    if (cur_is_walk) {
        if (u->anim != walk) unit_set_anim(u, walk);            /* walk to walk: straight in */
    } else if (u->anim && (u->anim == start || u->anim == k_unit_link[st][0] || u->anim == k_unit_link[st][1])) {
        if (!cur || u->anim_frame >= cur->length) unit_set_anim(u, walk);
    } else {
        unit_set_anim(u, unit_track(u, start) ? start : walk);
    }
    const acr_gen1_root_track *t = unit_track(u, u->anim);
    if (!t) return 1;
    if (!u->span_left) {
        if (u->anim_frame >= t->length) {
            if (u->anim != walk) return 1;                      /* a start that ran out: walk next tick */
            u->anim_frame = 0;
        }
        int32_t d[3];
        unsigned frames;
        if (!acr_gen1_root_span(t, u->anim_frame, d, &frames)) { ++u->anim_frame; return 1; }
        /* tracks: z negative forward, +x to its left; forward = (sin, cos), right = (cos, -sin) */
        const int32_t s = acr_gen1_sin(u->yaw), c = acr_gen1_cos(u->yaw);
        const int32_t fwd = -d[2], left = d[0];
        const int32_t wx = (int32_t)(((int64_t)fwd * s - (int64_t)left * c) / 4096);
        const int32_t wz = (int32_t)(((int64_t)fwd * c + (int64_t)left * s) / 4096);
        acr_gen1_stepper_init(&u->step[0], (int)frames, unit_div_round(wx, 4));
        acr_gen1_stepper_init(&u->step[1], (int)frames, unit_div_round(wz, 4));
        u->span_left = (uint16_t)frames;
    }
    const int32_t dx = acr_gen1_stepper_next(&u->step[0]), dz = acr_gen1_stepper_next(&u->step[1]);
    ++u->anim_frame;
    --u->span_left;
    unit_move(b, u, dx, dz);
    return 1;
}

static void unit_fire(acr_gen1_battle *b, int ui) {
    acr_gen1_unit *u = &b->unit[ui];
    acr_gen1_shot *s = free_shot(b);
    if (!s) return;
    const uint16_t yaw = (uint16_t)((u->yaw + u->gun_yaw) & 4095);
    int32_t dx, dy, dz;
    vec_of(yaw, u->gun_pitch, &dx, &dy, &dz);
    const int32_t side = (u->side & 2 ? 1 : -1) * (int32_t)u->half / 2;
    int32_t fx, fz;
    fwd_vec(u->yaw, &fx, &fz);
    const int32_t sp = per_step(b, ACR_GEN1_UNIT_SHOT);
    s->active = 1;
    s->owner = (uint8_t)ACR_GEN1_UNIT(ui);
    s->attack = u->attack;
    s->x = u->x + side * fz / 4096 * 64 + fx * u->half / 4096 * 64;
    s->y = u->y - (int32_t)u->height * 2 / 3 * 64;
    s->z = u->z - side * fx / 4096 * 64 + fz * u->half / 4096 * 64;
    s->vx = (int32_t)((int64_t)dx * sp / 4096);
    s->vy = (int32_t)((int64_t)dy * sp / 4096);
    s->vz = (int32_t)((int64_t)dz * sp / 4096);
    u->side ^= 2;
}

/* The gun, one tick (0x800A38B4): aim, start a burst, fire its odd ticks. */
static void unit_gun_tick(acr_gen1_battle *b, int ui, const tview *t) {
    acr_gen1_unit *u = &b->unit[ui];
    if (u->cooldown) { --u->cooldown; return; }
    int32_t to_yaw = 0, to_pitch = 0, in_cone = 0;
    const int32_t mx = u->x / 64, my = u->y / 64 - u->height * 2 / 3, mz = u->z / 64;
    int32_t tx = 0, ty = 0, tz = 0;
    if (t) {
        tx = t->x / 64; ty = (t->y - t->up) / 64; tz = t->z / 64;
        const int64_t dx = tx - mx, dy = ty - my, dz = tz - mz;
        uint16_t yaw;
        int16_t pitch;
        dir_of(dx, dy, dz, &yaw, &pitch);
        to_yaw = wrap((int32_t)yaw - u->yaw);
        to_pitch = pitch;
        in_cone = isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz)) <= ACR_GEN1_UNIT_RANGE &&
                  to_yaw >= -1024 && to_yaw <= 1024 && to_pitch >= -256 && to_pitch <= 256;
    }
    if (!in_cone) {                                      /* back to the middle */
        u->gun_yaw = (int16_t)turn_toward(u->gun_yaw, 0, UNIT_GUN_TURN);
        u->gun_pitch = (int16_t)turn_toward(u->gun_pitch, 0, UNIT_GUN_TURN);
        return;
    }
    const int32_t oy = u->gun_yaw, op = u->gun_pitch;
    u->gun_yaw = (int16_t)turn_toward(u->gun_yaw, to_yaw, UNIT_GUN_TURN);
    u->gun_pitch = (int16_t)turn_toward(u->gun_pitch, to_pitch, UNIT_GUN_TURN);
    const int32_t sy = u->gun_yaw - oy, spt = u->gun_pitch - op;
    if (sy >= UNIT_GUN_TURN || sy <= -UNIT_GUN_TURN || spt >= UNIT_GUN_TURN || spt <= -UNIT_GUN_TURN) return;   /* still turning */
    if (!u->attack) return;
    if (!u->burst) {
        if (rnd(b) >= 2622) return;
        if (rnd(b) >= 328 && b->stage) {                 /* line of sight */
            const int32_t a[3] = { mx, my, mz }, e[3] = { tx, ty, tz };
            int32_t hit_t;
            if (acr_gen1_stage_segment(b->stage, a, e, &hit_t)) return;
        }
        u->burst = 6;
        return;
    }
    if (u->burst & 1) unit_fire(b, ui);
    --u->burst;
}

static void unit_tick(acr_gen1_battle *b, int ui) {
    acr_gen1_unit *u = &b->unit[ui];
    u->px = u->x; u->py = u->y; u->pz = u->z;
    if (u->hit_flash) --u->hit_flash;
    if (u->down) {                                       /* destroyed: the explosion, then gone */
        if (++u->down >= ACR_GEN1_UNIT_DOWN_TICKS) u->active = 0;
        return;
    }
    if (u->brain == ACR_GEN1_BRAIN_NONE) return;
    /* Target: the nearest enemy (0x8007931C: any object of another team),
     * ACs and units alike, so map 12's MTs go for its friendly units too. */
    const int n = acr_gen1_battle_fighters(b);
    int64_t best = -1;
    u->target = -1;
    for (int j = 0; j < n + b->units; ++j) {
        const int id = j < n ? j : ACR_GEN1_UNIT(j - n);
        if (id == ACR_GEN1_UNIT(ui) || team_of(b, id) == (u->team & 1)) continue;
        if (j < n && !b->f[j].def) continue;
        tview v;
        if (!view_of(b, id, &v) || !v.alive) continue;
        const int64_t dx = (v.x - u->x) / 64 / 2, dz = (v.z - u->z) / 64 / 2;
        const int64_t d = (int64_t)isqrt64((uint64_t)(dx * dx + dz * dz)) * 4;
        if (best < 0 || d < best) { best = d; u->target = (int8_t)id; }
    }
    tview tv, *t = NULL;
    if (u->target >= 0 && view_of(b, u->target, &tv)) t = &tv;
    if (u->brain == ACR_GEN1_BRAIN_MT) {
        if (!u->state_ticks || !--u->state_ticks) {
            if (t) unit_decide(b, u, best);
            else { u->state = ACR_GEN1_UNIT_IDLE; u->state_ticks = UNIT_STEP_TICKS; }
        }
        static const int16_t k_way[9] = { 0, 0, -512, 512, -1024, 1024, 2048, 2048 + 512, 2048 - 512 };
        if (u->state == ACR_GEN1_UNIT_TURN_L || u->state == ACR_GEN1_UNIT_TURN_R) {
            const int32_t d = u->state == ACR_GEN1_UNIT_TURN_R ? 8 : -8;
            int32_t v = u->turn + d;
            if (v > 24) v = 24;
            if (v < -24) v = -24;
            u->turn = (int16_t)v;
            u->yaw = (uint16_t)((u->yaw + u->turn) & 4095);
            if (u->anim) unit_set_anim(u, 0);
        } else if (u->state != ACR_GEN1_UNIT_IDLE) {
            if (t) {
                uint16_t yaw;
                int16_t pitch;
                dir_of((t->x - u->x) / 64, 0, (t->z - u->z) / 64, &yaw, &pitch);
                u->yaw = (uint16_t)(turn_toward(u->yaw, yaw, UNIT_FACE) & 4095);
            }
            if (!unit_anim_walk(b, u))
                unit_walk(b, u, (uint16_t)((u->yaw + k_way[u->state]) & 4095),
                          u->state == ACR_GEN1_UNIT_AHEAD ? UNIT_WALK : UNIT_SIDE);
        } else if (u->anim) {
            unit_set_anim(u, 0);
        }
    } else if (u->brain == ACR_GEN1_BRAIN_TURRET && t) {
        uint16_t yaw;
        int16_t pitch;
        dir_of((t->x - u->x) / 64, 0, (t->z - u->z) / 64, &yaw, &pitch);
        if (best <= 2 * ACR_GEN1_UNIT_RANGE) u->yaw = (uint16_t)(turn_toward(u->yaw, yaw, UNIT_GUN_TURN) & 4095);
    }
    unit_gun_tick(b, ui, t);
}

static void units_step(acr_gen1_battle *b, int ticks) {
    for (int k = 0; k < ticks; ++k) {
        for (int u = 0; u < b->units; ++u)
            if (b->unit[u].active) unit_tick(b, u);
        acr_gen1_mission_tick(b);
    }
}

/* Bodies: an AC can't stand inside a unit. The unit's body is a square,
 * half a side either way of its middle and turned with it, height tall;
 * the AC is its hit cylinder. An overlap pushes the AC out (units are the
 * heavier: an MT walking into an AC shoves it). Ours: the game's own body
 * test (object +0x72 / +0x74 against chunk 12) is still to be read. */
static void bodies_step(acr_gen1_battle *b) {
    const int n = acr_gen1_battle_fighters(b);
    for (int u = 0; u < b->units; ++u) {
        const acr_gen1_unit *un = &b->unit[u];
        if (!un->active || un->down || !un->half) continue;
        for (int i = 0; i < n; ++i) {
            acr_gen1_ac *ac = &b->f[i].ac;
            if (!b->f[i].def) continue;
            /* y is down: the AC spans [y - 212, y], the unit [uy - height, uy] */
            if (ac->y / 64 - ACR_GEN1_HIT_HEIGHT >= un->y / 64 || un->y / 64 - un->height >= ac->y / 64) continue;
            int64_t ahead, side;
            offset(un->yaw, (ac->x - un->x) / 64, (ac->z - un->z) / 64, &ahead, &side);
            const int64_t h = un->half, r = ACR_GEN1_HIT_RADIUS;
            const int64_t ca = ahead < -h ? -h : ahead > h ? h : ahead;   /* nearest point of the square */
            const int64_t cs = side < -h ? -h : side > h ? h : side;
            int64_t pa = 0, ps = 0;                                      /* push, square's frame */
            if (ca == ahead && cs == side) {                             /* middle inside: shortest way out */
                const int64_t oa = h - (ahead < 0 ? -ahead : ahead), os = h - (side < 0 ? -side : side);
                if (oa < os) pa = (ahead < 0 ? -1 : 1) * (oa + r);
                else ps = (side < 0 ? -1 : 1) * (os + r);
            } else {
                const int64_t ea = ahead - ca, es = side - cs, d2 = ea * ea + es * es;
                if (d2 >= r * r) continue;
                const int64_t d = (int64_t)isqrt64((uint64_t)d2);
                if (!d) continue;
                pa = ea * (r - d) / d + (ea > 0 ? 1 : ea < 0 ? -1 : 0);
                ps = es * (r - d) / d + (es > 0 ? 1 : es < 0 ? -1 : 0);
            }
            int32_t fx, fz;
            fwd_vec(un->yaw, &fx, &fz);
            /* back to world: ahead along (fx, fz), side along (fz, -fx) (see offset) */
            const int64_t wx = (pa * fx + ps * fz) / 4096, wz = (pa * fz - ps * fx) / 4096;
            acr_gen1_ac_shift(ac, (int32_t)(wx * 64), (int32_t)(wz * 64));
        }
    }
}
