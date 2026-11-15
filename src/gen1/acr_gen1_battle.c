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

/* AC sizes in world units, a quarter of the model's (see ACR_GEN1_MODEL_SCALE). */
#define CENTRE_UP (100 * 64)        /* an AC's middle, above its feet */
#define HIT_RADIUS 65               /* stand-in */
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

int acr_gen1_in_lock_box(const acr_gen1_battle *b, int i, const acr_gen1_weapon *w) {
    const acr_gen1_ac *me = &b->f[i].ac, *it = &b->f[1 - i].ac;
    if (b->f[1 - i].ap <= 0) return 0;
    const int64_t dx = (it->x - me->x) / 64, dy = (it->y - me->y) / 64, dz = (it->z - me->z) / 64;
    const uint64_t dist = isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
    if (dist > (uint64_t)w->range) return 0;
    int32_t fx, fz;
    fwd_vec(me->yaw, &fx, &fz);
    const int64_t ahead = (dx * fx + dz * fz) / 4096;         /* along the facing */
    const int64_t side = (dx * fz - dz * fx) / 4096;
    if (ahead <= 0) return 0;
    /* The box is in the original's lock units (see ACR_GEN1_BOX_UNITS). */
    const int64_t s = side < 0 ? -side : side, h = dy < 0 ? -dy : dy;
    return s * ACR_GEN1_BOX_UNITS <= ahead * (w->box_w / 2) && h * ACR_GEN1_BOX_UNITS <= ahead * (w->box_h / 2);
}

/* A Gen 1 per-step speed at this battle's step rate, in 1/64 units. */
static int32_t per_step(const acr_gen1_battle *b, int32_t gen1_value) {
    if (b->step_rate && b->gen1_rate) return (int32_t)((int64_t)gen1_value * 64 * b->gen1_rate / b->step_rate);
    return gen1_value * 64;
}

static void spawn_shot(acr_gen1_battle *b, int i, int wi, int32_t dirx, int32_t diry, int32_t dirz) {
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        acr_gen1_shot *s = &b->shot[k];
        if (s->active) continue;
        const acr_gen1_ac *me = &b->f[i].ac;
        const int32_t sp = per_step(b, b->f[i].weapon[wi].speed);
        s->active = 1;
        s->owner = (uint8_t)i;
        s->weapon = (uint8_t)wi;
        s->x = me->x;
        s->y = me->y - MUZZLE_UP;
        s->z = me->z;
        s->vx = (int32_t)((int64_t)dirx * sp / 4096);
        s->vy = (int32_t)((int64_t)diry * sp / 4096);
        s->vz = (int32_t)((int64_t)dirz * sp / 4096);
        s->travelled = 0;
        return;
    }
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

static void damage(acr_gen1_battle *b, int target, const acr_gen1_weapon *w) {
    acr_gen1_fighter *t = &b->f[target];
    if (t->ap <= 0) return;
    t->ap -= acr_gen1_hit_ap(w->attack, w->energy ? t->energy_factor : t->shell_factor);
    if (t->ap < 0) t->ap = 0;
}

/* Blade lunge distance per Gen 1 step (measured: 516 in all). */
const int16_t acr_gen1_blade_lunge[ACR_GEN1_BLADE_STEPS] = { 125, 125, 125, 50, 44, 24, 23 };

/* Target offset seen from facing `yaw`: how far ahead and to the side. */
static void offset(uint16_t yaw, int64_t dx, int64_t dz, int64_t *ahead, int64_t *side) {
    int32_t fx, fz;
    fwd_vec(yaw, &fx, &fz);
    *ahead = (dx * fx + dz * fz) / 4096;
    *side = (dx * fz - dz * fx) / 4096;
}

/* Turn toward the target by up to ACR_GEN1_BLADE_TURN if it is in the cone. */
static void blade_steer(acr_gen1_ac *me, int64_t dx, int64_t dz) {
    int64_t ahead, side;
    offset(me->yaw, dx, dz, &ahead, &side);
    const uint32_t dist = isqrt64((uint64_t)(dx * dx + dz * dz));
    if (!dist || ahead * 4096 < (int64_t)dist * 2349) return;      /* cos 55 degrees */
    /* Turning toward +side raises yaw (facing is (sin, cos) of yaw). */
    const int dir = side > 0 ? 1 : -1;
    int lo = 0, hi = ACR_GEN1_BLADE_TURN;
    offset((uint16_t)((me->yaw + dir * hi) & 4095), dx, dz, &ahead, &side);
    if ((side > 0 ? 1 : -1) == dir) lo = hi;                        /* not past it even at full turn */
    while (hi - lo > 1) {
        const int mid = (lo + hi) / 2;
        offset((uint16_t)((me->yaw + dir * mid) & 4095), dx, dz, &ahead, &side);
        if ((side > 0 ? 1 : -1) == dir) lo = mid; else hi = mid;
    }
    me->yaw = (uint16_t)((me->yaw + dir * lo) & 4095);
    me->yaw_fine = (int32_t)me->yaw << 8;
}

/* Blade: a press starts a swing (timings in acr_gen1_battle.h). */
static void blade_step(acr_gen1_battle *b, int i, int blade, int pressed) {
    acr_gen1_fighter *f = &b->f[i];
    const int step_ms = b->step_ms ? b->step_ms : 45;
    if (blade < 0) { f->swing = 0; return; }
    const acr_gen1_weapon *w = &f->weapon[blade];
    if (pressed && !f->swing && f->blade_ms <= 0) {
        f->swing = 1;
        f->swing_hit = 0;
        f->blade_ms = w->interval_ms;
        f->lunge_x = f->lunge_z = 0;
    }
    if (!f->swing) return;
    const int target = 1 - i;
    acr_gen1_ac *me = &f->ac;
    const acr_gen1_ac *it = &b->f[target].ac;
    const int64_t dx = (it->x - me->x) / 64, dz = (it->z - me->z) / 64;
    const int t0 = f->swing - 1, t1 = t0 + step_ms;                 /* this step's ms window */
    if (t0 < ACR_GEN1_BLADE_STEER_MS && t1 >= ACR_GEN1_BLADE_STEER_MS && b->f[target].ap > 0)
        blade_steer(me, dx, dz);
    const int lunge_ms = ACR_GEN1_BLADE_STEPS * 1000 / 22;
    const int in_lunge = t1 > ACR_GEN1_BLADE_LUNGE_MS && t0 < ACR_GEN1_BLADE_LUNGE_MS + lunge_ms;
    if (in_lunge && !f->swing_hit) {
        if (!f->lunge_x && !f->lunge_z) fwd_vec(me->yaw, &f->lunge_x, &f->lunge_z);
        /* Distance covered over this step's slice of the Gen 1 profile. */
        int64_t d = 0;
        for (int ms = t0 < ACR_GEN1_BLADE_LUNGE_MS ? ACR_GEN1_BLADE_LUNGE_MS : t0; ms < t1; ++ms) {
            const int k = (ms - ACR_GEN1_BLADE_LUNGE_MS) * 22 / 1000;
            if (k >= ACR_GEN1_BLADE_STEPS) break;
            d += acr_gen1_blade_lunge[k];
        }
        d = d * 64 * 22 / 1000;                                     /* 1/64 units */
        me->x += (int32_t)(f->lunge_x * d / 4096);
        me->z += (int32_t)(f->lunge_z * d / 4096);
        me->vx = me->vz = 0;
        int64_t ahead, side;
        offset(me->yaw, (it->x - me->x) / 64, (it->z - me->z) / 64, &ahead, &side);
        const uint32_t dist = isqrt64((uint64_t)(ahead * ahead + side * side));
        if (b->f[target].ap > 0 && ahead > 0 && dist <= (uint32_t)w->range) {
            const int k = (t1 - ACR_GEN1_BLADE_LUNGE_MS) * 22 / 1000;
            const int32_t before = b->f[target].ap;
            damage(b, target, w);
            if (k >= ACR_GEN1_BLADE_FULL_STEPS) b->f[target].ap += (before - b->f[target].ap) / 2;
            f->swing_hit = 1;
        }
    }
    f->swing = (uint16_t)(t1 + 1);
    if (t1 >= ACR_GEN1_BLADE_LUNGE_MS + lunge_ms) f->swing = 0;
}

static void fighter_step(acr_gen1_battle *b, int i, acr_gen1_actions a) {
    acr_gen1_fighter *f = &b->f[i];
    if (f->ap <= 0 || !f->weapons) return;
    /* The blade is not in the cycle: guns and missiles only. */
    int blade = -1, cycled = 0;
    for (int k = 0; k < f->weapons; ++k) {
        if (f->weapon[k].kind == ACR_GEN1_BLADE) blade = k;
        else ++cycled;
    }
    if (f->weapon[f->current].kind == ACR_GEN1_BLADE && cycled) a.next_weapon = 1;
    if (a.next_weapon && cycled) {
        do f->current = (uint8_t)((f->current + 1) % f->weapons);
        while (f->weapon[f->current].kind == ACR_GEN1_BLADE);
        f->lock_ms = 0;
        f->locked = 0;
    }
    if (f->cooldown_ms > 0) f->cooldown_ms -= b->step_ms;
    if (f->blade_ms > 0) f->blade_ms -= b->step_ms;
    blade_step(b, i, blade, a.blade);
    if (!cycled) return;
    const acr_gen1_weapon *w = &f->weapon[f->current];

    /* Lock-on: the target has to stay in the FCS box for the lock time. */
    if (w->lock_ms && acr_gen1_in_lock_box(b, i, w)) {
        f->lock_ms += b->step_ms;
        if (f->lock_ms >= w->lock_ms) f->locked = 1;
    } else {
        f->lock_ms = 0;
        f->locked = 0;
    }

    if (!a.fire || f->cooldown_ms > 0) return;
    if (w->ammo && !f->ammo[f->current]) return;
    if (!acr_gen1_stance_ok(f)) return;
    if (w->kind == ACR_GEN1_MISSILE && !f->locked) return;   /* missiles wait for a full lock */
    const int target = 1 - i;
    {
        int32_t ax, ay, az;
        if (f->locked) {
            aim_at(b, target, f->ac.x, f->ac.y - MUZZLE_UP, f->ac.z, &ax, &ay, &az);
        } else {
            fwd_vec(f->ac.yaw, &ax, &az);
            ay = 0;
        }
        spawn_shot(b, i, f->current, ax, ay, az);
        if (w->ammo) --f->ammo[f->current];
    }
    f->cooldown_ms = w->interval_ms;
}

/* Closest approach of the segment p0 -> p1 to c, within r? (world units) */
static int segment_hits(int64_t x0, int64_t y0, int64_t z0, int64_t x1, int64_t y1, int64_t z1,
                        int64_t cx, int64_t cy, int64_t cz, int64_t r) {
    const int64_t dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    const int64_t len2 = dx * dx + dy * dy + dz * dz;
    int64_t t = 0;
    if (len2 > 0) {
        t = ((cx - x0) * dx + (cy - y0) * dy + (cz - z0) * dz) * 1024 / len2;
        if (t < 0) t = 0;
        if (t > 1024) t = 1024;
    }
    const int64_t px = x0 + dx * t / 1024 - cx, py = y0 + dy * t / 1024 - cy, pz = z0 + dz * t / 1024 - cz;
    return px * px + py * py + pz * pz <= r * r;
}

static void shots_step(acr_gen1_battle *b) {
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        acr_gen1_shot *s = &b->shot[k];
        if (!s->active) continue;
        const acr_gen1_fighter *owner = &b->f[s->owner];
        const acr_gen1_weapon *w = &owner->weapon[s->weapon];
        const int target = 1 - s->owner;
        if (w->kind == ACR_GEN1_MISSILE && w->homing && b->f[target].ap > 0) {
            /* As the game does: push the velocity toward the target by
             * `homing`, then hold it to the top speed. Acceleration scales
             * with the square of the step-rate ratio so turns stay the same
             * per second. */
            int32_t ax, ay, az;
            aim_at(b, target, s->x, s->y, s->z, &ax, &ay, &az);
            int64_t push = (int64_t)w->homing * 64;
            if (b->step_rate && b->gen1_rate)
                push = push * b->gen1_rate * b->gen1_rate / ((int64_t)b->step_rate * b->step_rate);
            int64_t vx = s->vx + ax * push / 4096, vy = s->vy + ay * push / 4096, vz = s->vz + az * push / 4096;
            const int64_t top = per_step(b, w->speed);
            const uint32_t len = isqrt64((uint64_t)(vx * vx + vy * vy + vz * vz));
            if (len > top) { vx = vx * top / len; vy = vy * top / len; vz = vz * top / len; }
            s->vx = (int32_t)vx; s->vy = (int32_t)vy; s->vz = (int32_t)vz;
        }
        const int32_t x0 = s->x, y0 = s->y, z0 = s->z;
        s->x += s->vx;
        s->y += s->vy;
        s->z += s->vz;
        s->travelled += per_step(b, w->speed) / 64;
        const acr_gen1_ac *t = &b->f[target].ac;
        if (b->f[target].ap > 0 &&
            segment_hits(x0 / 64, y0 / 64, z0 / 64, s->x / 64, s->y / 64, s->z / 64,
                         t->x / 64, (t->y - CENTRE_UP) / 64, t->z / 64, HIT_RADIUS)) {
            damage(b, target, w);
            s->active = 0;
        } else if (s->travelled > (w->kind == ACR_GEN1_MISSILE ? w->range * 3 / 2 : w->range) || s->y > 0) {
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

/* Two-legged ACs kneel when told to fire a cannon on the ground; while
 * kneeling they turn but do not move, and stand again on any move. */
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
        f->ac.vx = f->ac.vz = 0;
        in.move = in.strafe = 0;
        in.boost = in.boost_tapped = 0;
    }
    return in;
}

void acr_gen1_battle_step(acr_gen1_battle *b, const acr_gen1_input in[2], const acr_gen1_actions act[2]) {
    for (int i = 0; i < 2; ++i) {
        acr_gen1_fighter *f = &b->f[i];
        if (f->ap <= 0) continue;
        acr_gen1_input use = kneel_input(b, f, in[i], act[i]);
        if (f->swing) use.move = use.strafe = use.boost = use.boost_tapped = 0;   /* the swing holds the AC */
        acr_gen1_ac_step(&f->ac, f->def, use);
    }
    for (int i = 0; i < 2; ++i) fighter_step(b, i, act[i]);
    shots_step(b);
}
