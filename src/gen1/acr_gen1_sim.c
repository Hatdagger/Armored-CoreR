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
    ac->walk_phase = 0xFFFF;
    ac->tick_mx = ac->tick_mz = ac->rem_mx = ac->rem_mz = 0;
}

float acr_gen1_walk_fraction(const acr_gen1_ac *ac, const acr_gen1_ac_def *def) {
    if (!def->step_rate || !def->gen1_rate || ac->walk_phase == 0xFFFF) return 0.0f;
    return (float)ac->walk_phase / (float)def->step_rate;
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
    const int scaled = def->step_rate && def->gen1_rate;
    const int32_t rn = scaled ? def->gen1_rate : 1, rd = scaled ? def->step_rate : 1;
    int tick = 0;
    if (ac->walk_phase == 0xFFFF) { ac->walk_phase = 0; tick = 1; }
    else {
        ac->walk_phase = (uint16_t)(ac->walk_phase + rn);
        if (ac->walk_phase >= rd) { ac->walk_phase = (uint16_t)(ac->walk_phase - rd); tick = 1; }
    }
    if (tick) {
        if (ac->span_left == 0) {
            if (ac->anim_frame >= t->length) {
                if (ac->anim == start && def->anim[loop]) { set_anim(ac, loop); ac->walk_phase = 0; t = def->anim[loop]; }
                else ac->anim_frame = 0;
            }
            int32_t d[3];
            unsigned frames;
            if (!acr_gen1_root_span(t, ac->anim_frame, d, &frames)) {
                ac->anim_frame = t->length; ac->tick_mx = ac->tick_mz = 0; return;
            }
            /* Walking diagonally (forward or back with a strafe): the walk
             * track turned 45 degrees toward the strafe side. The game's own
             * diagonal walking is not measured yet. */
            int turn = 0;
            if (in.move && in.strafe) turn = (in.move > 0 ? in.strafe : -in.strafe) * 512;
            int32_t wx, wz;
            to_world((uint16_t)((ac->yaw + turn) & 4095), d[0], d[2], &wx, &wz);
            acr_gen1_stepper_init(&ac->step[0], (int)frames, div_round(wx, 4));
            acr_gen1_stepper_init(&ac->step[1], (int)frames, div_round(d[1], 4));
            acr_gen1_stepper_init(&ac->step[2], (int)frames, div_round(wz, 4));
            ac->span_left = (uint16_t)frames;
        }
        ac->tick_mx = acr_gen1_stepper_next(&ac->step[0]) * 64;
        (void)acr_gen1_stepper_next(&ac->step[1]);
        ac->tick_mz = acr_gen1_stepper_next(&ac->step[2]) * 64;
        ac->anim_frame++;
        ac->span_left--;
    }
    /* This step's share of the tick (all of it at Gen 1's pace). */
    const int32_t sx = ac->tick_mx * rn + ac->rem_mx, sz = ac->tick_mz * rn + ac->rem_mz;
    *mx += sx / rd; ac->rem_mx = sx % rd;
    *mz += sz / rd; ac->rem_mz = sz % rd;
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
    ac->yaw_fine = (int32_t)yaw << 8;
    ac->energy = def->stats.energy_capacity;
    ac->state = ACR_GEN1_GROUND;
}

void acr_gen1_ac_step(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in) {
    const acr_gen1_motion *m = &def->motion;
    const acr_gen1_stats *s = &def->stats;

    /* Turning: speeds up each step to the top rate, stops at once on release.
     * Kept per second at faster step rates (top speed x r, acceleration x r^2,
     * r = gen1_rate / step_rate), in 1/256 units so small steps add up. */
    const int scaled = def->step_rate && def->gen1_rate;
    const int64_t rn = scaled ? def->gen1_rate : 1, rd = scaled ? def->step_rate : 1;
    if (in.turn && ac->state != ACR_GEN1_LANDING) {
        const uint16_t ta = in.crouch && m->crouch_turn_max ? m->crouch_turn_accel : m->turn_accel;
        const uint16_t tm = in.crouch && m->crouch_turn_max ? m->crouch_turn_max : m->turn_max;
        const int32_t accel = (int32_t)((int64_t)ta * 256 * rn * rn / (rd * rd));
        const int32_t top = (int32_t)((int64_t)tm * 256 * rn / rd);
        ac->turn_fine += in.turn * (accel ? accel : 1);
        if (ac->turn_fine > top) ac->turn_fine = top;
        if (ac->turn_fine < -top) ac->turn_fine = -top;
    } else {
        ac->turn_fine = 0;
    }
    ac->turn_rate = (int16_t)(ac->turn_fine * rd / rn / 256);
    ac->yaw_fine = (ac->yaw_fine + ac->turn_fine) & (4096 * 256 - 1);
    ac->yaw = (uint16_t)(ac->yaw_fine >> 8);

    /* View tilt: L2 up, R2 down, both back to level (rates per Gen 1 step). */
    if (in.look == 2) {
        const int32_t r = (int32_t)((int64_t)ACR_GEN1_PITCH_LEVEL_RATE * 256 * rn / rd);
        if (ac->pitch_fine > r) ac->pitch_fine -= r;
        else if (ac->pitch_fine < -r) ac->pitch_fine += r;
        else ac->pitch_fine = 0;
    } else if (in.look) {
        const int32_t lim = ACR_GEN1_PITCH_MAX * 256;
        ac->pitch_fine += in.look * (int32_t)((int64_t)ACR_GEN1_PITCH_RATE * 256 * rn / rd);
        if (ac->pitch_fine > lim) ac->pitch_fine = lim;
        if (ac->pitch_fine < -lim) ac->pitch_fine = -lim;
    }
    ac->pitch = (int16_t)(ac->pitch_fine / 256);

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
            ac->coast = (uint16_t)(3 * rd / rn);   /* measured: 2-3 steps at full speed */
            drain = 2;                          /* marker: ground boost (half cost) */
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
        } else if (can_boost && ac->vx == 0 && ac->vz == 0) {
            takeoff(def, &ac->timer, &ac->vy);
            ac->state = ACR_GEN1_CROUCH;
            set_anim(ac, ACR_GEN1_ANIM_JUMP);
        } else if (ac->vx || ac->vz) {
            /* Slide: keep full speed a moment, then slow by the boost
             * acceleration a step (per second at faster step rates). */
            if (ac->coast) --ac->coast;
            else slow_down(&ac->vx, &ac->vz, (uint32_t)(m->boost_accel * rn / rd));
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
        slow_down(&ac->vx, &ac->vz, (uint32_t)(m->boost_accel * rn / rd));
        mx = ac->vx;
        mz = ac->vz;
        break;
    }

    ac->x += mx;
    ac->z += mz;
    if (ac->state == ACR_GEN1_AIR) {
        ac->y += ac->vy;
        if (ac->y >= 0 && ac->vy >= 0) {          /* touch down */
            ac->vy_at_touchdown = ac->vy;
            ac->y = 0;
            ac->vy = 0;
            /* Sideways speed carries into a ground slide (the game was only
             * measured landing with none left). */
            ac->coast = 0;
            ac->state = ACR_GEN1_LANDING;
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
            /* Recovery grows with the speed of the fall: none below 2,000, the
             * full measured stagger at 5,000 and above (a hop's touchdown), so
             * feathering the booster before landing softens it. Counted in
             * steps, so it shortens at faster step rates like movement. The
             * curve below 5,000 is a stand-in until measured. */
            const int32_t fall = ac->vy_at_touchdown;
            uint32_t steps = 0;
            if (fall >= 5000) steps = def->landing_steps;
            else if (fall > 2000) steps = (uint32_t)def->landing_steps * (uint32_t)(fall - 2000) / 3000u;
            ac->timer = (uint16_t)steps;
            if (!ac->timer) ac->state = ACR_GEN1_GROUND;
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
    if (def->step_rate && def->gen1_rate) {      /* same energy per second at any step rate */
        cost = cost * def->gen1_rate / def->step_rate;
        refill = refill * def->gen1_rate / def->step_rate;
        if (!cost) cost = 1;
        if (!refill) refill = 1;
    }
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
