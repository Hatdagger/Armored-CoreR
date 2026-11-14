#include "acr_gen1_sim.h"

#include <stddef.h>

#include "acr_gen1_sin.inc"

int32_t acr_gen1_sin(uint16_t a) {
    a &= 4095;
    if (a <= 1024) return k_quarter_sin[a];
    if (a <= 2048) return k_quarter_sin[2048 - a];
    if (a <= 3072) return -k_quarter_sin[a - 2048];
    return -k_quarter_sin[4096 - a];
}
int32_t acr_gen1_cos(uint16_t a) { return acr_gen1_sin((uint16_t)(a + 1024)); }

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

/* a / b rounded to nearest, halves away from zero. */
static int32_t div_round(int32_t a, int32_t b) {
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

/* Local (x right-ish as the tracks use it, z negative forward) to world. */
static void to_world(uint16_t yaw, int32_t lx, int32_t lz, int32_t *wx, int32_t *wz) {
    const int32_t s = acr_gen1_sin(yaw), c = acr_gen1_cos(yaw);
    const int32_t fwd = -lz, left = lx;      /* tracks: +x is to the AC's left */
    /* forward = (sin, cos), right = (cos, -sin) */
    *wx = (int32_t)(((int64_t)fwd * s - (int64_t)left * c) / 4096);
    *wz = (int32_t)(((int64_t)fwd * c + (int64_t)left * s) / 4096);
}

static void limit_length(int32_t *x, int32_t *z, uint32_t max) {
    const uint64_t len2 = (uint64_t)((int64_t)*x * *x) + (uint64_t)((int64_t)*z * *z);
    if (len2 <= (uint64_t)max * max) return;
    const uint32_t len = isqrt64(len2);
    if (len == 0) return;
    *x = (int32_t)((int64_t)*x * max / len);
    *z = (int32_t)((int64_t)*z * max / len);
}

static void slow_down(int32_t *x, int32_t *z, uint32_t by) {
    const uint64_t len2 = (uint64_t)((int64_t)*x * *x) + (uint64_t)((int64_t)*z * *z);
    const uint32_t len = isqrt64(len2);
    if (len <= by) { *x = 0; *z = 0; return; }
    *x = (int32_t)((int64_t)*x * (len - by) / len);
    *z = (int32_t)((int64_t)*z * (len - by) / len);
}

/* Stick direction in world space, length 4096 (diagonals normalised). */
static int stick_dir(uint16_t yaw, acr_gen1_input in, int32_t *dx, int32_t *dz) {
    if (!in.move && !in.strafe) return 0;
    int32_t lz = -in.move * 4096, lx = -in.strafe * 4096;   /* -x = right */
    if (in.move && in.strafe) { lz = lz * 2896 / 4096; lx = lx * 2896 / 4096; }
    to_world(yaw, lx, lz, dx, dz);
    return 1;
}

static void set_anim(acr_gen1_ac *ac, uint8_t anim) {
    ac->anim = anim;
    ac->anim_frame = 0;
    ac->span_left = 0;
}

/* Walking, strafing, walking back: the legs' animation moves the AC. */
static void walk(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in,
                 int32_t *mx, int32_t *mz) {
    uint8_t start = ACR_GEN1_ANIM_IDLE, loop = ACR_GEN1_ANIM_IDLE;
    if (in.move > 0)        { start = ACR_GEN1_ANIM_WALK_START;     loop = ACR_GEN1_ANIM_WALK; }
    else if (in.move < 0)   { start = ACR_GEN1_ANIM_BACK_START;     loop = ACR_GEN1_ANIM_BACK; }
    else if (in.strafe < 0) { start = ACR_GEN1_ANIM_STRAFE_L_START; loop = ACR_GEN1_ANIM_STRAFE_L; }
    else if (in.strafe > 0) { start = ACR_GEN1_ANIM_STRAFE_R_START; loop = ACR_GEN1_ANIM_STRAFE_R; }
    if (ac->anim != start && ac->anim != loop)
        set_anim(ac, def->anim[start] ? start : loop);
    const acr_gen1_root_track *t = def->anim[ac->anim];
    if (!t || ac->anim == ACR_GEN1_ANIM_IDLE) return;
    if (ac->span_left == 0) {
        if (ac->anim_frame >= t->length) {
            if (ac->anim == start && def->anim[loop]) { set_anim(ac, loop); t = def->anim[loop]; }
            else ac->anim_frame = 0;
        }
        int32_t d[3];
        unsigned frames;
        if (!acr_gen1_root_span(t, ac->anim_frame, d, &frames)) { ac->anim_frame = t->length; return; }
        int32_t wx, wz;
        to_world(ac->yaw, d[0], d[2], &wx, &wz);
        acr_gen1_stepper_init(&ac->step[0], (int)frames, div_round(wx, 4));
        acr_gen1_stepper_init(&ac->step[1], (int)frames, div_round(d[1], 4));
        acr_gen1_stepper_init(&ac->step[2], (int)frames, div_round(wz, 4));
        ac->span_left = (uint16_t)frames;
    }
    *mx += acr_gen1_stepper_next(&ac->step[0]) * 64;
    (void)acr_gen1_stepper_next(&ac->step[1]);
    *mz += acr_gen1_stepper_next(&ac->step[2]) * 64;
    ac->anim_frame++;
    ac->span_left--;
}

/* Jump: still frames before lift-off and the take-off speed, from the jump
 * animation's first upward key (default AC: 7 steps, 3,200). */
static void takeoff(const acr_gen1_ac_def *def, uint16_t *crouch, int32_t *vy) {
    *crouch = 7;
    *vy = -3200;
    const acr_gen1_root_track *t = def->anim[ACR_GEN1_ANIM_JUMP];
    if (!t) return;
    uint16_t prev_frame = 0;
    int32_t prev_y = 0;
    for (uint16_t i = 0; i < t->count; ++i) {
        if (t->keys[i].y != prev_y) {
            const int32_t n = t->keys[i].frame - prev_frame;
            *crouch = prev_frame;
            *vy = div_round(t->keys[i].y - prev_y, 4) * 64 / (n > 0 ? n : 1);
            return;
        }
        prev_frame = t->keys[i].frame;
        prev_y = t->keys[i].y;
    }
}

void acr_gen1_ac_spawn(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int32_t x, int32_t z, uint16_t yaw) {
    *ac = (acr_gen1_ac){0};
    ac->x = x;
    ac->z = z;
    ac->yaw = yaw;
    ac->energy = def->stats.energy_capacity;
    ac->state = ACR_GEN1_GROUND;
}

void acr_gen1_ac_step(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in) {
    const acr_gen1_motion *m = &def->motion;
    const acr_gen1_stats *s = &def->stats;

    /* Turning: speeds up each step to the top rate, stops at once on release. */
    if (in.turn && ac->state != ACR_GEN1_LANDING) {
        ac->turn_rate = (int16_t)(ac->turn_rate + in.turn * (int)m->turn_accel);
        if (ac->turn_rate > (int)m->turn_max) ac->turn_rate = (int16_t)m->turn_max;
        if (ac->turn_rate < -(int)m->turn_max) ac->turn_rate = (int16_t)-(int)m->turn_max;
    } else {
        ac->turn_rate = 0;
    }
    ac->yaw = (uint16_t)((ac->yaw + ac->turn_rate) & 4095);

    const int can_boost = in.boost && !ac->drained && ac->energy > 0;
    const int fresh_press = in.boost && (!ac->boost_was_down || in.boost_tapped);
    ac->boost_was_down = in.boost;
    int32_t drain = 0, mx = 0, mz = 0, dx = 0, dz = 0;
    const int dir = stick_dir(ac->yaw, in, &dx, &dz);
    int moving = dir;

    switch (ac->state) {
    case ACR_GEN1_GROUND:
        /* Boost hop: let go of the booster during a ground boost and press it
         * again while still sliding, and the AC jumps straight into the air,
         * keeping its speed (humanoid, reverse-joint and four-legged legs;
         * as described by a player, not yet measured). */
        if (can_boost && fresh_press && (ac->vx || ac->vz) && !def->no_boost_hop) {
            int32_t vy;
            uint16_t crouch;
            takeoff(def, &crouch, &vy);
            ac->vy = vy;
            ac->state = ACR_GEN1_AIR;
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
            mx = ac->vx;
            mz = ac->vz;
            moving = 1;
            break;
        }
        if (can_boost && dir) {
            ac->vx += dx * (int32_t)m->boost_accel / 4096;
            ac->vz += dz * (int32_t)m->boost_accel / 4096;
            limit_length(&ac->vx, &ac->vz, m->boost_max);
            drain = 2;                          /* marker: ground boost (half cost) */
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
        } else if (can_boost && ac->vx == 0 && ac->vz == 0) {
            takeoff(def, &ac->timer, &ac->vy);
            ac->state = ACR_GEN1_CROUCH;
            set_anim(ac, ACR_GEN1_ANIM_JUMP);
        } else if (ac->vx || ac->vz) {
            slow_down(&ac->vx, &ac->vz, m->boost_accel);
        } else {
            walk(ac, def, in, &mx, &mz);
        }
        mx += ac->vx;
        mz += ac->vz;
        /* Ground movement is limited on each axis (measured 75 units a step). */
        if (m->axis_step_max) {
            const int32_t lim = (int32_t)m->axis_step_max * 64;
            if (mx > lim) mx = lim;
            if (mx < -lim) mx = -lim;
            if (mz > lim) mz = lim;
            if (mz < -lim) mz = -lim;
        }
        break;
    case ACR_GEN1_CROUCH:
        if (ac->timer) --ac->timer;
        if (!ac->timer) ac->state = ACR_GEN1_AIR;
        break;
    case ACR_GEN1_AIR:
        if (can_boost) {
            ac->vy -= m->rise_accel;
            if (ac->vy < -(int32_t)m->rise_max) ac->vy = -(int32_t)m->rise_max;
            drain = 1;                          /* marker: rising / air boost */
        } else {
            ac->vy += ACR_GEN1_GRAVITY;
            if (ac->vy > ACR_GEN1_FALL_MAX) ac->vy = ACR_GEN1_FALL_MAX;
        }
        if (can_boost && dir) {
            /* Faster than the air boost allows (e.g. carried over from a
             * ground boost): ease down to it instead of adding more. */
            const uint64_t len2 = (uint64_t)((int64_t)ac->vx * ac->vx) + (uint64_t)((int64_t)ac->vz * ac->vz);
            if (len2 > (uint64_t)m->air_max * m->air_max) {
                slow_down(&ac->vx, &ac->vz, m->air_accel / 4);
            } else {
                ac->vx += dx * (int32_t)m->air_accel / 4096;
                ac->vz += dz * (int32_t)m->air_accel / 4096;
                limit_length(&ac->vx, &ac->vz, m->air_max);
            }
        } else {
            slow_down(&ac->vx, &ac->vz, m->air_accel / 4);
        }
        mx = ac->vx;
        mz = ac->vz;
        moving = 1;
        break;
    case ACR_GEN1_LANDING:
        if (ac->timer) --ac->timer;
        if (!ac->timer) ac->state = ACR_GEN1_GROUND;
        break;
    }

    ac->x += mx;
    ac->z += mz;
    if (ac->state == ACR_GEN1_AIR) {
        ac->y += ac->vy;
        if (ac->y >= 0 && ac->vy >= 0) {          /* touch down: stop dead */
            ac->y = 0;
            ac->vx = ac->vy = ac->vz = 0;
            ac->state = ACR_GEN1_LANDING;
            ac->timer = def->landing_steps;
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
        }
    }

    /* Energy (VS engine 0x80082D90): boosting drains, otherwise the
     * generator refills it.
     * - Below the red zone both draining and refilling run at a quarter.
     * - A ground boost costs half.
     * - Running dry locks the booster; the bar then refills at half speed
     *   and the lock lifts only when it is completely full. */
    int32_t cost = s->rise_drain;
    int32_t refill = (moving || ac->vx || ac->vz) ? s->refill_moving : s->refill_standing;
    if (ac->drained) {
        ac->energy += refill >> 1;
        if (ac->energy >= s->energy_capacity) { ac->energy = s->energy_capacity; ac->drained = 0; }
        return;
    }
    if (ac->energy < s->red_zone) { cost >>= 2; refill >>= 2; }
    if (drain) {
        if (drain == 2) cost >>= 1;
        if (ac->energy > cost) ac->energy -= cost;
        else { ac->energy = 0; ac->drained = 1; }
    } else {
        ac->energy += refill;
        if (ac->energy > s->energy_capacity) ac->energy = s->energy_capacity;
    }
}
