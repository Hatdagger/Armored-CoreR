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

/* Inside a lock box of half-sizes w / 2, h / 2 (ACR_GEN1_BOX_UNITS) and the range? */
static int in_box(const acr_gen1_battle *b, int i, int32_t range, uint16_t box_w, uint16_t box_h) {
    const acr_gen1_ac *me = &b->f[i].ac, *it = &b->f[1 - i].ac;
    if (b->f[1 - i].ap <= 0 || !box_w) return 0;
    const int64_t dx = (it->x - me->x) / 64, dy = (it->y - CENTRE_UP - (me->y - CENTRE_UP)) / 64,
                  dz = (it->z - me->z) / 64;
    const uint64_t dist = isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
    if (dist > (uint64_t)range) return 0;
    int32_t fx, fz;
    fwd_vec(me->yaw, &fx, &fz);
    const int64_t flat = (dx * fx + dz * fz) / 4096;          /* along the facing */
    const int64_t side = (dx * fz - dz * fx) / 4096;
    /* Tilted with the view (L2/R2): +y is down, pitch + is up. */
    const uint16_t p = (uint16_t)(me->pitch & 4095);
    const int64_t cp = acr_gen1_cos(p), sp = acr_gen1_sin(p);
    const int64_t ahead = (flat * cp - dy * sp) / 4096, up = (-dy * cp - flat * sp) / 4096;
    if (ahead <= 0) return 0;
    const int64_t s = side < 0 ? -side : side, h = up < 0 ? -up : up;
    return s * ACR_GEN1_BOX_UNITS <= ahead * (box_w / 2) && h * ACR_GEN1_BOX_UNITS <= ahead * (box_h / 2);
}

int acr_gen1_in_lock_box(const acr_gen1_battle *b, int i, const acr_gen1_weapon *w) {
    return in_box(b, i, w->range, w->box_w, w->box_h);
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
        dir_of((it->x - me->x) / 64, (it->y - me->y) / 64, (it->z - me->z) / 64, &yaw, &pitch);
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
    const acr_gen1_ac *me = &b->f[i].ac;
    const int32_t sp = per_step(b, b->f[i].weapon[wi].speed);
    s->active = 1;
    s->owner = (uint8_t)i;
    s->weapon = (uint8_t)wi;
    s->x = me->x + side * dirz / 4096 * 64;
    s->y = me->y - MUZZLE_UP;
    s->z = me->z - side * dirx / 4096 * 64;
    s->vx = (int32_t)((int64_t)dirx * sp / 4096);
    s->vy = (int32_t)((int64_t)diry * sp / 4096);
    s->vz = (int32_t)((int64_t)dirz * sp / 4096);
}

/* Unit vector (x 4096) from a point to the other AC's middle. */
static void aim_at(const acr_gen1_battle *b, int target, int32_t x, int32_t y, int32_t z,
                   int32_t *ax, int32_t *ay, int32_t *az) {
    const acr_gen1_ac *t = &b->f[target].ac;
    const int64_t dx = (t->x - x) / 64, dy = (t->y - CENTRE_UP - y) / 64, dz = (t->z - z) / 64;
    const uint32_t len = isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
    if (!len) { *ax = 0; *ay = 0; *az = 4096; return; }
    *ax = (int32_t)(dx * 4096 / len);
    *ay = (int32_t)(dy * 4096 / len);
    *az = (int32_t)(dz * 4096 / len);
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
    if (blade < 0) { f->swing = 0; return; }
    const acr_gen1_weapon *w = &f->weapon[blade];
    acr_gen1_ac *me = &f->ac;
    const int target = 1 - i;
    acr_gen1_ac *it = &b->f[target].ac;
    if (pressed && !f->swing && w->attack && !me->drained && me->energy > (int32_t)w->charge_drain) {
        me->energy -= w->charge_drain;
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
    if (t1 >= ACR_GEN1_BLADE_SWING_MS) f->swing = 0;
}

/* ---- missiles ---- */

static void launch_missile(acr_gen1_battle *b, int i, int wi, int32_t side) {
    acr_gen1_fighter *f = &b->f[i];
    const acr_gen1_weapon *w = &f->weapon[wi];
    acr_gen1_shot *s = free_shot(b);
    if (!s) return;
    const acr_gen1_ac *me = &f->ac, *it = &b->f[1 - i].ac;
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
    const int outer = in_box(b, i, w->range, w->box_w, w->box_h);
    const int inner = in_box(b, i, w->range, w->inner_w ? w->inner_w : w->box_w, w->inner_h ? w->inner_h : w->box_h);
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
    const int32_t ry = wrap(tyaw - f->ac.yaw), rp = wrap(tpitch - f->ac.pitch);
    const int32_t sp = w->lock_value * 3 / 16 > 0 ? w->lock_value * 3 / 16 : 1;
    f->cursor_yaw = (int16_t)turn_toward(f->cursor_yaw, ry, sp);
    f->cursor_pitch = (int16_t)turn_toward(f->cursor_pitch, rp, sp);
    if (f->cursor_yaw != ry || f->cursor_pitch != rp) return;
    if (++f->lock_count > w->lock_ticks) {
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

static void fighter_tick(acr_gen1_battle *b, int i, int fire) {
    acr_gen1_fighter *f = &b->f[i];
    if (f->hit_flash) --f->hit_flash;
    if (f->def) intercept_tick(b, i);
    if (f->ap <= 0 || !f->weapons) return;
    const acr_gen1_weapon *w = &f->weapon[f->current];
    /* Post-shot hold and refire wait; a cannon's wait pauses during the hold. */
    if (f->hold_left) --f->hold_left;
    if (f->cooldown && !(f->hold_left && w->stance)) --f->cooldown;
    if (w->kind == ACR_GEN1_MISSILE) missile_fire_tick(b, i, f->current, fire && acr_gen1_stance_ok(f));
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
        f->lock_ms = 0;
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

    /* Guns: the target has to stay in the FCS box for the lock time. */
    if (w->lock_ms && !w->straight && acr_gen1_in_lock_box(b, i, w)) {
        f->lock_ms += b->step_ms;
        if (f->lock_ms >= w->lock_ms) f->locked = 1;
    } else {
        f->lock_ms = 0;
        f->locked = 0;
    }

    if (!a.fire || f->cooldown || f->hold_left) return;
    if (w->ammo && !f->ammo[f->current]) return;
    if (!acr_gen1_stance_ok(f)) return;
    if (f->swing) return;
    const int target = 1 - i;
    int32_t ax, ay, az;
    if (f->locked && !w->straight) {
        aim_at(b, target, f->ac.x, f->ac.y - MUZZLE_UP, f->ac.z, &ax, &ay, &az);
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
        if (b->f[target].ap > 0 &&
            segment_hits_ac(x0 / 64, y0 / 64, z0 / 64, s->x / 64, s->y / 64, s->z / 64, t->x / 64, t->y / 64, t->z / 64)) {
            damage(b, target, s->owner, w);
            s->active = 0;
        } else if ((!s->missile && s->travelled > w->range) || s->y > 0) {
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
            const acr_gen1_input still = { 0, 0, 0, 0, 0, 0, 0 };
            acr_gen1_ac_advance(&f->ac, f->def, still, 0, 1, 1);
            continue;
        }
        acr_gen1_input use = kneel_input(b, f, in[i], act[i]);
        if (f->swing) use.move = use.strafe = use.boost = use.boost_tapped = 0;   /* the swing holds the AC */
        acr_gen1_ac_advance(&f->ac, f->def, use, ticks, num, den);
    }
    for (int i = 0; i < 2; ++i) fighter_step(b, i, act[i], ticks);
    shots_step(b, ticks);
}

int acr_gen1_battle_result(const acr_gen1_battle *b, int time_up) {
    const int down0 = b->f[0].ap <= 0, down1 = b->f[1].ap <= 0;
    if (down0 || down1) return down0 && down1 ? 3 : down0 ? 2 : 1;
    if (!time_up) return 0;
    return b->f[0].ap > b->f[1].ap ? 1 : b->f[1].ap > b->f[0].ap ? 2 : 3;
}
