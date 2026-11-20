#include "acr_gen1_battle.h"

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

/* The target's middle seen from the weapon (VS sight sweep 0x80078230):
 * distance, and its angles off the facing (+ right) and the view tilt (+ up). */
static uint32_t sight_angles(const acr_gen1_battle *b, int i, int32_t *dyaw, int32_t *dpitch) {
    const acr_gen1_ac *me = &b->f[i].ac, *it = &b->f[1 - i].ac;
    int32_t m[3];
    muzzle_at(b, i, m);
    const int64_t dx = (it->x - m[0]) / 64, dy = (it->y - CENTRE_UP - m[1]) / 64, dz = (it->z - m[2]) / 64;
    uint16_t yaw;
    int16_t pitch;
    dir_of(dx, dy, dz, &yaw, &pitch);
    (void)me;
    *dyaw = wrap((int32_t)yaw - view_yaw(&b->f[i]));
    *dpitch = wrap((int32_t)pitch - view_pitch(&b->f[i]));
    return isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
}

/* Inside a lock box (angles either side, 4096 a turn) and the range? */
static int in_box_pct(const acr_gen1_battle *b, int i, int32_t range, uint16_t box_w, uint16_t box_h, unsigned pct);
static int in_box(const acr_gen1_battle *b, int i, int32_t range, uint16_t box_w, uint16_t box_h) {
    return in_box_pct(b, i, range, box_w, box_h, b->f[i].modern ? MT(b, box_pct, BOX_PCT) : 100u);
}
static int in_box_pct(const acr_gen1_battle *b, int i, int32_t range, uint16_t box_w, uint16_t box_h, unsigned pct) {
    if (b->f[1 - i].ap <= 0 || !box_w) return 0;
    int32_t ay, ap;
    const uint32_t dist = sight_angles(b, i, &ay, &ap);
    if (dist > (uint32_t)range) return 0;
    if (pct != 100u) {
        box_w = (uint16_t)(box_w * pct / 100u);
        box_h = (uint16_t)(box_h * pct / 100u);
    }
    return ay >= -(int32_t)box_w && ay <= box_w && ap >= -(int32_t)box_h && ap <= box_h;
}

static int32_t sight_range(const acr_gen1_weapon *w) { return w->lock_range ? w->lock_range : w->range; }

int acr_gen1_in_lock_box(const acr_gen1_battle *b, int i, const acr_gen1_weapon *w) {
    return in_box(b, i, sight_range(w), w->box_w, w->box_h);
}

int acr_gen1_aim(const acr_gen1_battle *b, int i, int16_t aim[3]) {
    const acr_gen1_fighter *f = &b->f[i];
    aim[0] = aim[1] = aim[2] = 0;
    if (!f->weapons || f->swing || f->current >= f->weapons) return -1;
    const acr_gen1_weapon *w = &f->weapon[f->current];
    if (w->kind == ACR_GEN1_BLADE || w->aim_table == 255) return -1;
    const acr_gen1_ac *me = &f->ac, *it = &b->f[1 - i].ac;
    aim[0] = (int16_t)-me->pitch;
    if (!w->straight && acr_gen1_in_lock_box(b, i, w)) {
        uint16_t yaw;
        int16_t pitch;
        if (w->kind == ACR_GEN1_GUN && f->locked) { yaw = f->lead_yaw; pitch = f->lead_pitch; }   /* where the shots go */
        else dir_of((it->x - me->x) / 64, (it->y - me->y) / 64, (it->z - me->z) / 64, &yaw, &pitch);
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
    const int32_t sp = per_step(b, b->f[i].weapon[wi].speed);
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

static void damage(acr_gen1_battle *b, int target, int attacker, const acr_gen1_weapon *w) {
    acr_gen1_fighter *t = &b->f[target];
    if (t->ap <= 0) return;
    t->ap -= acr_gen1_hit_ap(w->attack, w->energy ? t->energy_factor : t->shell_factor);
    if (t->ap < 0) t->ap = 0;
    b->f[attacker].hit_flash = 40;          /* attacker's "HIT" blink, counts down a tick */
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

/* The blade lunge over the swing's ms [from, to): the measured per-tick
 * distances spread evenly over each tick, stopping at body contact. */
static void lunge_move(acr_gen1_battle *b, int i, int from, int to) {
    acr_gen1_fighter *f = &b->f[i];
    acr_gen1_ac *me = &f->ac;
    const acr_gen1_ac *it = &b->f[1 - i].ac;
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
    int64_t ahead, side;
    offset(me->yaw, (it->x - me->x) / 64, (it->z - me->z) / 64, &ahead, &side);
    const int64_t gap = ahead - 2 * ACR_GEN1_HIT_RADIUS;
    if ((side < 0 ? -side : side) < 2 * ACR_GEN1_HIT_RADIUS && ahead > 0 && d / 64 >= gap) {
        d = gap > 0 ? gap * 64 : 0;
        f->lunge_done = 1;
    }
    acr_gen1_ac_shift(me, (int32_t)(f->lunge_x * d / 4096), (int32_t)(f->lunge_z * d / 4096));
    me->vx = me->vz = 0;
}

/* Blade: a press starts a swing (see acr_gen1_battle.h). */
static void blade_step(acr_gen1_battle *b, int i, int blade, int pressed) {
    acr_gen1_fighter *f = &b->f[i];
    const int step_ms = b->step_ms ? b->step_ms : 45;
    if (f->recover_ms) f->recover_ms = (uint16_t)(f->recover_ms > step_ms ? f->recover_ms - step_ms : 0);
    if (blade < 0) { f->swing = 0; return; }
    const acr_gen1_weapon *w = &f->weapon[blade];
    acr_gen1_ac *me = &f->ac;
    const int target = 1 - i;
    acr_gen1_ac *it = &b->f[target].ac;
    const int32_t cost = f->modern ? (int32_t)(w->charge_drain * MT(b, blade_en_pct, BLADE_EN_PCT) / 100u)
                                   : (int32_t)w->charge_drain;
    if (pressed && !f->swing && !f->recover_ms && w->attack && !me->drained && me->energy > cost) {
        me->energy -= cost;
        f->swing = 1;
        f->swing_hits = 0;
        f->lunge_done = 0;
        f->lunge_x = f->lunge_z = 0;
        /* 0x80078230: an AC up to 3,000 ahead within +-45 degrees. */
        int64_t ahead, side;
        const int64_t dx = (it->x - me->x) / 64, dz = (it->z - me->z) / 64;
        offset(me->yaw, dx, dz, &ahead, &side);
        const uint32_t dist = isqrt64((uint64_t)(dx * dx + dz * dz));
        f->swing_target = (int8_t)(b->f[target].ap > 0 && ahead > 0 && dist <= ACR_GEN1_BLADE_SEEK &&
                                   (side < 0 ? -side : side) <= ahead ? target : -1);
    }
    if (!f->swing) return;
    const int t0 = f->swing - 1, t1 = t0 + step_ms;                 /* this step's ms window */
    /* Turn toward the target at the AC's top turn rate until the lunge. */
    if (f->swing_target >= 0 && t1 <= tick_ms(ACR_GEN1_BLADE_STEER_TICKS)) {
        const int64_t dx = (it->x - me->x) / 64, dz = (it->z - me->z) / 64;
        int32_t rate = (int32_t)((int64_t)f->def->motion.turn_max * rate_n(b) / rate_d(b));
        if (f->modern) rate = (int32_t)(rate * (int32_t)MT(b, steer_pct, STEER_PCT) / 100);
        if (rate < 1) rate = 1;
        acr_gen1_ac_set_yaw(me, (uint16_t)(turn_toward(me->yaw, angle_of(dx, dz), rate) & 4095));
    }
    /* Lunge and the two crossings, in time order within the step. */
    const int reach = w->range ? (int)w->range : ACR_GEN1_BLADE_REACH;
    const int cross[2] = { ACR_GEN1_BLADE_LUNGE_MS + tick_ms(ACR_GEN1_BLADE_HIT_TICK),
                           ACR_GEN1_BLADE_LUNGE_MS + tick_ms(ACR_GEN1_BLADE_HIT_TICK + 1) };
    int from = t0;
    for (int c = 0; c <= 2; ++c) {
        const int to = c < 2 ? cross[c] : t1;
        if (to <= t0 || to > t1) { if (c == 2) lunge_move(b, i, from, t1); continue; }
        lunge_move(b, i, from, to);
        from = to;
        if (c == 2 || b->f[target].ap <= 0) continue;
        int64_t ahead, side;
        const int64_t dx = (it->x - me->x) / 64, dz = (it->z - me->z) / 64;
        offset(me->yaw, dx, dz, &ahead, &side);
        if (ahead <= 0 || (side < 0 ? -side : side) > ahead) continue;
        const int r = c == 0 ? reach : reach - (ACR_GEN1_BLADE_REACH - ACR_GEN1_BLADE_REACH2);
        if (isqrt64((uint64_t)(dx * dx + dz * dz)) > (uint32_t)r) continue;
        damage(b, target, i, w);
        ++f->swing_hits;
        f->lunge_done = 1;
        int32_t kx = f->lunge_x, kz = f->lunge_z;
        if (!kx && !kz) fwd_vec(me->yaw, &kx, &kz);
        acr_gen1_ac_shift(it, kx * ACR_GEN1_BLADE_KNOCK * 64 / 4096, kz * ACR_GEN1_BLADE_KNOCK * 64 / 4096);
    }
    f->swing = (uint16_t)(t1 + 1);
    if (t1 >= ACR_GEN1_BLADE_SWING_MS) {
        f->swing = 0;
        if (f->modern)         /* standing still after the swing: short on a hit, long on a miss */
            f->recover_ms = (uint16_t)(f->swing_hits ? MT(b, recover_hit_ms, RECOVER_HIT_MS)
                                                     : MT(b, recover_miss_ms, RECOVER_MISS_MS));
    }
}

/* ---- missiles ---- */

static void launch_missile(acr_gen1_battle *b, int i, int wi, int32_t side) {
    acr_gen1_fighter *f = &b->f[i];
    const acr_gen1_weapon *w = &f->weapon[wi];
    acr_gen1_shot *s = free_shot(b);
    if (!s) return;
    const acr_gen1_ac *me = &f->ac, *it = &b->f[1 - i].ac;
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
    /* Start: toward the target plus a random spread, at 100 plus the launcher's own motion. */
    dir_of((it->x - s->x) / 64, (it->y - CENTRE_UP - s->y) / 64, (it->z - s->z) / 64, &s->yaw, &s->pitch);
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
    const int target = 1 - s->owner;
    const acr_gen1_ac *t = &b->f[target].ac;
    const int alive = b->f[target].ap > 0;
    ++s->age;
    if (s->age >= fl->life) { s->active = 0; return; }
    uint16_t tyaw = s->yaw;
    int16_t tpitch = s->pitch;
    if (alive) dir_of((t->x - s->x) / 64, (t->y - CENTRE_UP - s->y) / 64, (t->z - s->z) / 64, &tyaw, &tpitch);
    if (s->age < s->ignition) {                              /* coasting, unlit */
        s->vy += ACR_GEN1_GRAVITY;
        if (fl->kind == 4 && alive) {
            s->yaw = (uint16_t)(turn_toward(s->yaw, tyaw, fl->turn) & 4095);
            s->pitch = (int16_t)wrap(turn_toward(s->pitch, tpitch, fl->turn));
        }
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
    const acr_gen1_fighter *t = &b->f[1 - i];
    const int outer = in_box(b, i, sight_range(w), w->box_w, w->box_h);
    const int inner = in_box(b, i, sight_range(w), w->inner_w ? w->inner_w : w->box_w, w->inner_h ? w->inner_h : w->box_h);
    if (!outer || (!f->locks && !inner) || (t->jammer && f->locks && rnd(b) < 1639)) {
        f->locks = 0;
        f->lock_count = 0;
        f->cursor_yaw = f->cursor_pitch = 0;
        return;
    }
    unsigned cap = w->max_locks ? w->max_locks : 1;
    if (cap > ammo) cap = ammo;
    if (cap > ACR_GEN1_MAX_LOCKS) cap = ACR_GEN1_MAX_LOCKS;
    if (f->locks >= cap) return;
    uint16_t tyaw;
    int16_t tpitch;
    dir_of((t->ac.x - f->ac.x) / 64, (t->ac.y - f->ac.y) / 64, (t->ac.z - f->ac.z) / 64, &tyaw, &tpitch);
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
            for (int s = 0; s < w->volley_a + w->volley_b && f->ammo[wi]; ++s) {
                launch_missile(b, i, wi, s < w->volley_a ? -60 : 60);
                --f->ammo[wi];
            }
            if (f->locks) --f->locks;
            if (!f->volley_ticks) { f->cooldown = w->interval; f->locks = 0; }
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
        if (!s->active || !s->missile || s->owner == i) continue;
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
    const acr_gen1_fighter *t = &b->f[1 - i];
    f->cam_pitch_prev = f->cam_pitch;
    f->cam_yaw_prev = f->cam_yaw;
    int32_t to_pitch = 0, to_yaw = 0, step = 20;
    if (t->ap > 0 && f->ap > 0 && f->weapons && f->current < f->weapons) {
        const acr_gen1_weapon *w = &f->weapon[f->current];
        const acr_gen1_ac *me = &f->ac, *it = &t->ac;
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
    if (f->def) intercept_tick(b, i);
    if (f->ap <= 0 || !f->weapons) return;
    const acr_gen1_weapon *w = &f->weapon[f->current];
    /* Post-shot hold and refire wait; a cannon's wait pauses during the hold. */
    if (f->hold_left) --f->hold_left;
    if (f->cooldown && !(f->hold_left && w->stance)) --f->cooldown;
    if (w->kind == ACR_GEN1_MISSILE) missile_fire_tick(b, i, f->current, fire && acr_gen1_stance_ok(f));
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
    const acr_gen1_ac *t = &b->f[1 - i].ac;
    int32_t m[3];
    muzzle_at(b, i, m);
    /* The target's middle and its movement over the last tick. */
    const int64_t px = t->x / 64, py = (t->y - CENTRE_UP) / 64, pz = t->z / 64;
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
    if (f->weapon[f->current].kind == ACR_GEN1_BLADE && cycled) a.next_weapon = 1;
    if (a.next_weapon && cycled && !f->volley_ticks) {
        do f->current = (uint8_t)((f->current + 1) % f->weapons);
        while (f->weapon[f->current].kind == ACR_GEN1_BLADE);
        f->lock_wait = f->lock_lead = 0;
        f->locked = 0;
        f->locks = 0;
        f->lock_count = 0;
        f->cursor_yaw = f->cursor_pitch = 0;
    }
    for (int t = 0; t < ticks; ++t) fighter_tick(b, i, a.fire);
    blade_step(b, i, blade, a.blade);
    if (!cycled) return;
    const acr_gen1_weapon *w = &f->weapon[f->current];
    if (w->kind == ACR_GEN1_MISSILE) { f->locked = f->locks > 0; return; }

    for (int t = 0; t < ticks; ++t) gun_lock_tick(b, i, w);

    if (!a.fire || f->cooldown || f->hold_left) return;
    if (w->ammo && !f->ammo[f->current]) return;
    if (f->modern && w->energy && f->ac.drained) return;   /* modern: no energy weapons until the bar is full */
    if (!acr_gen1_stance_ok(f)) return;
    if (f->swing || f->recover_ms) return;
    int32_t ax, ay, az;
    if (f->locked && !w->straight) {
        vec_of(f->lead_yaw, f->lead_pitch, &ax, &ay, &az);   /* VS 0x80081150: the lock's aim */
    } else {
        /* Along the facing, tilted with the view (+y is down). */
        const uint16_t p = (uint16_t)(f->ac.pitch & 4095);
        fwd_vec(f->ac.yaw, &ax, &az);
        ax = (int32_t)((int64_t)ax * acr_gen1_cos(p) / 4096);
        az = (int32_t)((int64_t)az * acr_gen1_cos(p) / 4096);
        ay = -acr_gen1_sin(p);
    }
    const int n = w->shots ? w->shots : 1;
    for (int s = 0; s < n; ++s) {
        if (w->ammo && !f->ammo[f->current]) break;
        spawn_shot(b, i, f->current, n == 1 ? 0 : (s * 2 - (n - 1)) * 20, ax, ay, az);
        if (w->ammo) --f->ammo[f->current];
    }
    f->cooldown = w->interval;
    if (f->modern && f->hard_lock && w->straight)    /* rockets on hard lock: far slower */
        f->cooldown = (uint16_t)(w->interval * MT(b, rocket_rof_pct, ROCKET_ROF_PCT) / 100u);
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

static void shots_step(acr_gen1_battle *b, int ticks) {
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        acr_gen1_shot *s = &b->shot[k];
        if (!s->active) continue;
        const acr_gen1_fighter *owner = &b->f[s->owner];
        const acr_gen1_weapon *w = &owner->weapon[s->weapon];
        const int target = 1 - s->owner;
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
            s->travelled += per_step(b, w->speed) / 64;
        }
        const acr_gen1_ac *t = &b->f[target].ac;
        const int hits_ac = b->f[target].ap > 0 &&
            segment_hits_ac(x0 / 64, y0 / 64, z0 / 64, s->x / 64, s->y / 64, s->z / 64, t->x / 64, t->y / 64, t->z / 64);
        /* The arena stops shots (VS 0x80074FA4 tests the stage before the
         * ACs); here the nearer of the two along the way wins. */
        int32_t wall_t = 0;
        int hits_wall = 0;
        if (b->stage) {
            const int32_t a[3] = { x0 / 64, y0 / 64, z0 / 64 }, e[3] = { s->x / 64, s->y / 64, s->z / 64 };
            hits_wall = acr_gen1_stage_segment(b->stage, a, e, &wall_t);
        }
        if (hits_ac && hits_wall) {
            /* How far along the way the AC's middle is, against the wall. */
            const int64_t dx = s->x / 64 - x0 / 64, dz = s->z / 64 - z0 / 64, len2 = dx * dx + dz * dz;
            const int64_t ac_t = len2 ? ((int64_t)(t->x / 64 - x0 / 64) * dx + (int64_t)(t->z / 64 - z0 / 64) * dz) * 1024 / len2 : 0;
            if (ac_t <= wall_t) damage(b, target, s->owner, w);
            s->active = 0;
        } else if (hits_ac) {
            damage(b, target, s->owner, w);
            s->active = 0;
        } else if (hits_wall) {
            s->active = 0;                              /* into the arena */
        } else if ((!s->missile && s->travelled > w->range) || (!b->stage && s->y > 0)) {
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
    default: return f->kneeling && f->kneel_ms >= ACR_GEN1_KNEEL_MS;
    }
}

/* Two-legged ACs crouch when told to fire a cannon on the ground (AC state
 * 34); crouched they turn at the crouch rates (legs +0x3C) and slide to a
 * stop (the sim's crouch input: velocity - 1/8 a tick, zero below 512, VS
 * 0x8007E3E0); any move without fire stands them up again. */
static acr_gen1_input kneel_input(acr_gen1_battle *b, acr_gen1_fighter *f, acr_gen1_input in, acr_gen1_actions a) {
    const acr_gen1_weapon *w = f->weapons ? &f->weapon[f->current] : NULL;
    const int wants = w && w->stance && !f->human_plus && f->legs == ACR_GEN1_LEGS_TWO;
    if (!wants) { f->kneeling = 0; return in; }
    const int moving = in.move || in.strafe || in.boost;
    if (!f->kneeling && a.fire && f->ac.state == ACR_GEN1_GROUND) {
        f->kneeling = 1;
        f->kneel_ms = 0;
    } else if (f->kneeling && moving && !a.fire) {
        f->kneeling = 0;
    }
    if (f->kneeling) {
        f->kneel_ms += b->step_ms;
        in.move = in.strafe = 0;
        in.boost = in.boost_tapped = 0;
        in.crouch = 1;
    }
    return in;
}

/* Modern controls' hard lock (see ACR_GEN1_MODERN_SIGHT_LAG): a press turns
 * it on or off; while on and the target is in the current weapon's sight,
 * the AC turns / tilts toward it at its normal rates unless the pilot is
 * turning / tilting. Leaving the sight drops it. */
static acr_gen1_input hard_lock_input(acr_gen1_battle *b, int i, acr_gen1_input in, int pressed) {
    acr_gen1_fighter *f = &b->f[i];
    const int was_on = f->hard_lock;
    if (pressed) f->hard_lock = (uint8_t)!f->hard_lock;
    if (!f->hard_lock) return in;
    const acr_gen1_weapon *w = NULL;
    const int rockets = f->weapons && f->current < f->weapons && f->weapon[f->current].straight;
    if (rockets) w = &f->weapon[f->current];
    else
        for (int k = 0; k < f->weapons; ++k)        /* the current gun / missile, else any with a sight */
            if (f->weapon[k].kind != ACR_GEN1_BLADE && f->weapon[k].box_w && (!w || k == f->current)) w = &f->weapon[k];
    const int seen = w && (rockets ? in_box_pct(b, i, w->range, ACR_GEN1_MODERN_ROCKET_BOX, ACR_GEN1_MODERN_ROCKET_BOX,
                                                         MT(b, rocket_box_pct, ROCKET_BOX_PCT))
                                   : acr_gen1_in_lock_box(b, i, w));
    if (!w || b->f[1 - i].ap <= 0 || !seen) { f->hard_lock = 0; return in; }
    if (!was_on) {                                  /* starting it: close enough */
        int32_t ay, ap;
        if (sight_angles(b, i, &ay, &ap) > MT(b, hard_lock_start, HARD_LOCK_START)) { f->hard_lock = 0; return in; }
    }
    const acr_gen1_ac *me = &f->ac, *it = &b->f[1 - i].ac;
    uint16_t yaw;
    int16_t pitch;
    dir_of((it->x - me->x) / 64, (it->y - CENTRE_UP - (me->y - MUZZLE_UP)) / 64, (it->z - me->z) / 64, &yaw, &pitch);
    const int32_t ry = wrap((int32_t)yaw - me->yaw), rp = wrap((int32_t)pitch - me->pitch);
    int32_t dead = f->def ? f->def->motion.turn_max : 16;     /* within a tick's turn: stop */
    if (dead < 16) dead = 16;
    if (!in.turn && !in.crouch) in.turn = (int8_t)(ry > dead ? 1 : ry < -dead ? -1 : 0);
    if (!in.look) in.look = (int8_t)(rp > ACR_GEN1_PITCH_RATE ? 1 : rp < -ACR_GEN1_PITCH_RATE ? -1 : 0);
    return in;
}

void acr_gen1_battle_step(acr_gen1_battle *b, const acr_gen1_input in[2], const acr_gen1_actions act[2]) {
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
    for (int i = 0; i < 2; ++i) {
        acr_gen1_fighter *f = &b->f[i];
        if (f->ap <= 0) {                       /* down: stays where it is */
            const acr_gen1_input still = { 0 };
            acr_gen1_ac_advance(&f->ac, f->def, still, 0, 1, 1);
            continue;
        }
        acr_gen1_input use = kneel_input(b, f, in[i], act[i]);
        if (!f->modern) { use.cruise = use.quick = 0; f->hard_lock = 0; }
        else use = hard_lock_input(b, i, use, act[i].hard_lock);
        if (f->recover_ms) use.move = use.strafe = use.boost = use.boost_tapped = use.cruise = use.quick = 0;
        if (f->swing) use.quick = use.cruise = 0;
        if (f->swing) use.move = use.strafe = use.boost = use.boost_tapped = 0;   /* the swing holds the AC */
        acr_gen1_ac_advance(&f->ac, f->def, use, ticks, num, den);
    }
    for (int i = 0; i < 2; ++i) fighter_step(b, i, act[i], ticks);
    shots_step(b, ticks);
}

int acr_gen1_battle_out_of_area(const acr_gen1_battle *b, int i) {
    if (!b->stage || !acr_gen1_stage_has_area_rule(b->stage)) return 0;
    const acr_gen1_ac *ac = &b->f[i].ac;
    const uint8_t bits = acr_gen1_stage_area_bits(b->stage, ac->x / 64, ac->y / 64, ac->z / 64);
    if (!(bits & ACR_GEN1_AREA_INNER)) return 2;
    return (bits & ACR_GEN1_AREA_INNER) != ACR_GEN1_AREA_INNER ? 1 : 0;
}

int acr_gen1_battle_result(const acr_gen1_battle *b, int time_up) {
    /* Out of area (outside both inner boxes) loses like a knock-out. */
    const int down0 = b->f[0].ap <= 0 || acr_gen1_battle_out_of_area(b, 0) == 2,
              down1 = b->f[1].ap <= 0 || acr_gen1_battle_out_of_area(b, 1) == 2;
    if (down0 || down1) return down0 && down1 ? 3 : down0 ? 2 : 1;
    if (!time_up) return 0;
    return b->f[0].ap > b->f[1].ap ? 1 : b->f[1].ap > b->f[0].ap ? 2 : 3;
}
