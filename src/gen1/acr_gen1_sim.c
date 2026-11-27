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

static int walking(const acr_gen1_ac *ac) {
    return ac->anim != ACR_GEN1_ANIM_IDLE && ac->anim != ACR_GEN1_ANIM_JUMP;
}

float acr_gen1_tick_fraction(const acr_gen1_ac *ac) {
    if (!ac->show_den || ac->show_num >= ac->show_den) return 1.0f;
    return (float)ac->show_num / (float)ac->show_den;
}

/* Walking, strafing, walking back: the legs' animation moves the AC, one
 * animation frame a tick. */
static void walk(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in,
                 int32_t *mx, int32_t *mz) {
    uint8_t start = ACR_GEN1_ANIM_IDLE, loop = ACR_GEN1_ANIM_IDLE;
    if (in.move > 0)        { start = ACR_GEN1_ANIM_WALK_START;     loop = ACR_GEN1_ANIM_WALK; }
    else if (in.move < 0)   { start = ACR_GEN1_ANIM_BACK_START;     loop = ACR_GEN1_ANIM_BACK; }
    else if (in.strafe < 0) { start = ACR_GEN1_ANIM_STRAFE_L_START; loop = ACR_GEN1_ANIM_STRAFE_L; }
    else if (in.strafe > 0) { start = ACR_GEN1_ANIM_STRAFE_R_START; loop = ACR_GEN1_ANIM_STRAFE_R; }
    /* Diagonals: the set's own diagonal walk (the game has eight walking
     * directions); a set without it walks the straight track turned 45. */
    int diag_track = 0;
    if (in.move && in.strafe) {
        uint8_t ds, dl;
        if (in.move > 0) { ds = in.strafe < 0 ? ACR_GEN1_ANIM_FWD_L_START : ACR_GEN1_ANIM_FWD_R_START; }
        else             { ds = in.strafe < 0 ? ACR_GEN1_ANIM_BACK_L_START : ACR_GEN1_ANIM_BACK_R_START; }
        dl = (uint8_t)(ds + 1);
        if (def->anim[dl] && def->anim[dl]->count) { start = ds; loop = dl; diag_track = 1; }
    }
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
        /* Walking diagonally (forward or back with a strafe): the walk
         * track turned 45 degrees toward the strafe side, only for a set
         * without its own diagonal track. */
        int turn = 0;
        if (in.move && in.strafe && !diag_track) turn = (in.move > 0 ? in.strafe : -in.strafe) * 512;
        int32_t wx, wz;
        to_world((uint16_t)((ac->yaw + turn) & 4095), d[0], d[2], &wx, &wz);
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
 * animation's first upward key (default AC: 7 ticks, 3,200). */
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

/* The cannon stance's slide (VS 0x8007E3E0): an eighth off each axis a
 * tick, nothing left below 512. */
static void crouch_slide(int32_t *v) {
    *v -= *v / 8;
    if (*v > -512 && *v < 512) *v = 0;
}

/* Height of the AC for ceilings (its hit cylinder, 212). */
#define ACR_GEN1_HIT_HEIGHT_SIM 212

/* Arena: walls push the AC out sideways, then the floor under it.
 * On the ground it steps up to ACR_GEN1_STEP_UP and follows a floor
 * dropping away by up to ACR_GEN1_STEP_DOWN; further down it falls. In
 * the air *floor_y is the floor it would land on (1/64 units). */
static void stage_move(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int32_t *floor_y, int *have_floor) {
    const acr_gen1_stage *st = def->stage;
    int32_t x = ac->x / 64, z = ac->z / 64;
    const int32_t feet = ac->y / 64;
    if (acr_gen1_stage_push(st, &x, &z, feet, ACR_GEN1_HIT_HEIGHT_SIM, 75, ACR_GEN1_STEP_UP)) {
        ac->x = x * 64 + ac->x % 64;
        ac->z = z * 64 + ac->z % 64;
    }
    int32_t f;
    const int grounded = ac->state != ACR_GEN1_AIR;
    const int32_t from = grounded ? feet : ac->py / 64;
    if (!acr_gen1_stage_floor(st, ac->x / 64, ac->z / 64, from - ACR_GEN1_STEP_UP, &f)) {
        *have_floor = 0;                         /* nothing below: keeps falling */
        if (grounded) { ac->state = ACR_GEN1_AIR; ac->vy = 0; }
        return;
    }
    *floor_y = f * 64;
    if (grounded) {
        if (f - feet > ACR_GEN1_STEP_DOWN) {    /* walked off an edge */
            ac->state = ACR_GEN1_AIR;
            ac->vy = 0;
        } else {
            ac->y = f * 64;
        }
    }
}

/* Touchdown (VS 0x8007BA68): the fall speed picks the soft landing
 * (state 0x60, animation 23, cut short by any direction or turn) or the
 * hard one (0x61, animation 36, played to the end), see
 * ACR_GEN1_LAND_HARD_VY. Either way the AC stops dead: the landing states
 * skip the movement handlers, so nothing slides. y is already at the
 * floor. */
static void touch_down(acr_gen1_ac *ac, const acr_gen1_ac_def *def) {
    ac->vy_at_touchdown = ac->vy;
    ac->vy = 0;
    ac->vx = 0;
    ac->vz = 0;
    ac->coast = 0;
    ac->state = ACR_GEN1_LANDING;
    set_anim(ac, ACR_GEN1_ANIM_IDLE);
    const int32_t fall = ac->vy_at_touchdown;
    uint32_t ticks;
    if (def->landing_guess) {                /* test: the old stand-in curve */
        ticks = 0;
        if (fall >= 5000) ticks = def->landing_steps;
        else if (fall > 2000) ticks = (uint32_t)def->landing_steps * (uint32_t)(fall - 2000) / 3000u;
        ac->landing_soft = 0;
    } else {
        ac->landing_soft = fall < ACR_GEN1_LAND_HARD_VY;
        const acr_gen1_root_track *t = def->anim[ac->landing_soft ? ACR_GEN1_ANIM_LAND_SOFT : ACR_GEN1_ANIM_LAND];
        ticks = t && t->length ? t->length : ac->landing_soft ? ACR_GEN1_LAND_SOFT_TICKS : ACR_GEN1_LAND_HARD_TICKS;
    }
    ac->timer = (uint16_t)ticks;
    ac->landing_pause = (uint16_t)ticks;
    if (!ac->timer) ac->state = ACR_GEN1_GROUND;
}

/* Floor division by 64 (positions are in 1/64 units). */
static int32_t whole(int32_t v) { return v >= 0 ? v / 64 : -((-v + 63) / 64); }

/* The game's own stage rules (VS engine, gen1-stages.md, "Walls and
 * floors"), for an import with the game's collision:
 * - the move goes through the stage as one box (100 either side, 212
 *   tall), up to six tries, each pushed out of the nearest polygon
 *   (0x80078C4C); on the ground the step also leans down by a quarter of
 *   its length, so the AC follows slopes and steps down (0x8007B60C), but
 *   not where the floor already holds it (the first try comes back level);
 * - where the stage cut a sideways step by 7 or more the speed becomes the
 *   step taken; the fall stops at a floor (touchdown) or a ceiling;
 * - on the ground: once the floor under the AC (0x8007C964) is 50 or more
 *   below the feet and the AC could drop 50, it falls from 256 a tick
 *   (0x80083978), its sideways speed gone unless it was ground boosting. */
static void game_stage_tick(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int32_t mx, int32_t mz, int boosting) {
    const acr_gen1_stage *st = def->stage;
    const int air = ac->state == ACR_GEN1_AIR;
    const int32_t my = air ? ac->vy : 0;
    const int32_t pos[3] = { whole(ac->x), whole(ac->y), whole(ac->z) };
    int32_t step[3] = { whole(ac->x + mx) - pos[0], whole(ac->y + my) - pos[1], whole(ac->z + mz) - pos[2] };
    if (!air) {
        step[1] = (int32_t)(isqrt64((uint64_t)((int64_t)step[0] * step[0] + (int64_t)step[2] * step[2])) / 4);
        int32_t t[3] = { step[0], step[1], step[2] };
        if (acr_gen1_stage_try(st, pos, t, ACR_GEN1_STAGE_BOX_HALF, ACR_GEN1_STAGE_BOX_HEIGHT, ACR_GEN1_STAGE_AC_MASK) &&
            t[1] <= 0)
            step[1] = 0;
    }
    const int32_t want[3] = { step[0], step[1], step[2] };
    if (!acr_gen1_stage_move(st, pos, step, ACR_GEN1_STAGE_BOX_HALF, ACR_GEN1_STAGE_BOX_HEIGHT, ACR_GEN1_STAGE_AC_MASK)) {
        ac->x += mx;
        ac->z += mz;
        if (air) ac->y += ac->vy;
        else ac->y += step[1] * 64;
    } else {
        ac->x = (pos[0] + step[0]) * 64 + (ac->x - pos[0] * 64);
        ac->y = (pos[1] + step[1]) * 64 + (ac->y - pos[1] * 64);
        ac->z = (pos[2] + step[2]) * 64 + (ac->z - pos[2] * 64);
        /* Sideways speed (sliding, boosting, the air): what the stage left. */
        if ((want[0] - step[0] >= 7 || step[0] - want[0] >= 7) && ac->vx) ac->vx = step[0] * 64;
        if ((want[2] - step[2] >= 7 || step[2] - want[2] >= 7) && ac->vz) ac->vz = step[2] * 64;
        if (air && step[1] != want[1]) {
            const int32_t fall = ac->vy;
            ac->vy = step[1] * 64;
            if (fall >= 0 && step[1] < want[1]) {          /* stopped by a floor */
                ac->vy = fall;
                ac->y = (pos[1] + step[1]) * 64;
                touch_down(ac, def);
                return;
            }
        }
    }
    if (ac->state == ACR_GEN1_AIR) return;
    /* Still on the ground? */
    const int32_t now[3] = { whole(ac->x), whole(ac->y), whole(ac->z) };
    int32_t f;
    if (acr_gen1_stage_floor_probe(st, now, ACR_GEN1_STAGE_PROBE_HALF, ACR_GEN1_STAGE_BOX_HEIGHT, ACR_GEN1_STAGE_AC_MASK,
                                   &f) && f - now[1] < ACR_GEN1_STAGE_GROUNDED)
        return;
    const int32_t lower[3] = { now[0], now[1] + ACR_GEN1_STAGE_GROUNDED, now[2] };
    int32_t none[3] = { 0, 0, 0 };
    if (acr_gen1_stage_try(st, lower, none, ACR_GEN1_STAGE_BOX_HALF, ACR_GEN1_STAGE_BOX_HEIGHT, ACR_GEN1_STAGE_AC_MASK))
        return;                                         /* something holds it up */
    ac->state = ACR_GEN1_AIR;
    ac->vy = 256;
    ac->timer = 0;
    if (!boosting) { ac->vx = 0; ac->vz = 0; }
}

int32_t acr_gen1_walk_pace(const acr_gen1_ac_def *def) {
    const acr_gen1_root_track *t = def->anim[ACR_GEN1_ANIM_WALK];
    if (t && t->count && t->keys[t->count - 1].frame) {
        const acr_gen1_root_key *k = &t->keys[t->count - 1];
        const int32_t z = k->z < 0 ? -k->z : k->z;          /* model units: a quarter of world units */
        return (int32_t)((int64_t)z * 64 / 4 / k->frame);
    }
    return ACR_GEN1_WALK_PACE * 64;
}

int32_t acr_gen1_cruise_top(const acr_gen1_ac_def *def) {
    /* Four legs and caterpillars already walk at the ground boost's top
     * speed (drive_walk), so the toggle can't go faster than walking. */
    if (def->drive_walk) return def->motion.boost_max;
    int32_t top = acr_gen1_walk_pace(def) * (def->cruise_pct ? def->cruise_pct : ACR_GEN1_CRUISE_PCT) / 100;
    if (top > (int32_t)def->motion.boost_max) top = def->motion.boost_max;
    return top;
}

int32_t acr_gen1_quick_cost(const acr_gen1_ac_def *def, int air) {
    const acr_gen1_motion *m = &def->motion;
    const int32_t accel = air ? m->air_accel : m->boost_accel, top = air ? m->air_max : m->boost_max;
    const int64_t dist = (int64_t)top * ACR_GEN1_QUICK_TICKS;
    int64_t covered = 0, v = 0;
    int32_t ticks = 0;
    if (accel <= 0 || top <= 0) ticks = ACR_GEN1_QUICK_TICKS;
    while (covered < dist && ticks < 1000) {    /* a normal boost from a stand */
        v += accel;
        if (v > top) v = top;
        covered += v;
        ++ticks;
    }
    return ticks * (int32_t)(def->stats.rise_drain >> 1);
}

void acr_gen1_ac_spawn(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int32_t x, int32_t z, uint16_t yaw) {
    *ac = (acr_gen1_ac){0};
    ac->x = ac->px = ac->sx = x;
    ac->z = ac->pz = ac->sz = z;
    ac->yaw = ac->pyaw = ac->syaw = yaw;
    ac->energy = def->stats.energy_capacity;
    ac->state = ACR_GEN1_GROUND;
    ac->show_num = ac->show_den = 1;
    /* The first step runs a tick. */
    if (def->step_rate && def->gen1_rate && def->gen1_rate < def->step_rate)
        ac->phase = (uint16_t)(def->step_rate - def->gen1_rate);
}

void acr_gen1_ac_tick(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in) {
    const acr_gen1_motion *m = &def->motion;
    const acr_gen1_stats *s = &def->stats;
    ac->px = ac->x; ac->py = ac->y; ac->pz = ac->z;
    ac->pyaw = ac->yaw;

    /* A soft landing ends as soon as the pad asks for a walk, strafe or
     * turn (0x80084834 starts that animation the same tick). */
    if (ac->state == ACR_GEN1_LANDING && ac->landing_soft && (in.move || in.strafe || in.turn)) {
        ac->state = ACR_GEN1_GROUND;
        ac->timer = 0;
        ac->landing_soft = 0;
    }

    /* Turning: speeds up each tick to the top rate, stops at once on
     * release (VS 0x8007E1DC). Crouched in the cannon stance it uses the
     * weapon's crouch rates. */
    if (in.turn && ac->state != ACR_GEN1_LANDING) {
        const int crouched = in.crouch && m->crouch_turn_max;
        const int32_t ta = crouched ? m->crouch_turn_accel : m->turn_accel;
        const int32_t tm = crouched ? m->crouch_turn_max : m->turn_max;
        int32_t r = ac->turn_rate + in.turn * (ta ? ta : 1);
        if (r > tm) r = tm;
        if (r < -tm) r = -tm;
        ac->turn_rate = (int16_t)r;
    } else {
        ac->turn_rate = 0;
    }
    ac->yaw = (uint16_t)((ac->yaw + ac->turn_rate) & 4095);

    /* The game reads the pad once a tick: a press is fresh when the button
     * was up at the last tick or was let go of since. */
    const int can_boost = in.boost && !ac->drained && ac->energy > 0;
    /* SP-MODERN: the booster rises only with the toggle on (caterpillars
     * always), never boosts along the ground, and jumps whatever the bar. */
    const int tracks = def->no_boost_hop;
    const int can_rise = can_boost && (!in.modern || in.cruise || tracks);
    const int ground_boost = can_boost && !in.modern;
    const int can_jump = in.modern && !tracks ? in.boost : can_rise;
    const int fresh_press = in.boost && (!ac->boost_was_down || ac->boost_released || in.boost_tapped);
    ac->boost_was_down = in.boost;
    ac->boost_released = 0;
    int32_t drain = 0, mx = 0, mz = 0, dx = 0, dz = 0;
    const int dir = stick_dir(ac->yaw, in, &dx, &dz);

    /* Modern controls: the quick boost (see ACR_GEN1_QUICK_TICKS). */
    if (ac->quick_cool) --ac->quick_cool;
    if (!ac->quick_ticks && ac->quick_chain && ++ac->quick_gap > ACR_GEN1_QUICK_CHAIN_GAP) {
        ac->quick_chain = 0;                    /* too long: the chain starts over */
        ac->quick_gap = 0;
    }
    const int quick_press = ac->quick_pending;
    ac->quick_pending = (uint8_t)(quick_press && ac->quick_ticks);   /* pressed mid-burst: waits for its end */
    if (quick_press && !ac->quick_ticks && dir && !ac->quick_cool && !ac->drained && ac->energy > 0 && !in.crouch &&
        (ac->state == ACR_GEN1_GROUND || ac->state == ACR_GEN1_AIR)) {
        const int air = ac->state == ACR_GEN1_AIR;
        const int32_t top = air ? m->air_max : m->boost_max;
        const int32_t cost = acr_gen1_quick_cost(def, air);
        if (ac->energy > cost) ac->energy -= cost;
        else { ac->energy = 0; ac->drained = 1; }
        ac->vx = (int32_t)((int64_t)dx * top / 4096);
        ac->vz = (int32_t)((int64_t)dz * top / 4096);
        ac->quick_ticks = ACR_GEN1_QUICK_TICKS;
        ac->quick_gap = 0;
        if (++ac->quick_chain >= ACR_GEN1_QUICK_CHAIN) { ac->quick_chain = 0; ac->quick_cool = ACR_GEN1_QUICK_COOL; }
        if (!air) set_anim(ac, ACR_GEN1_ANIM_IDLE);
    }
    /* The boost toggle on in the air slows the fall (not while rising). */
    const int toggle_on = in.modern && in.cruise && !ac->drained && ac->energy > 0;
    const int32_t gravity = toggle_on ? ACR_GEN1_GRAVITY * ACR_GEN1_CRUISE_AIR_FALL_PCT / 100 : ACR_GEN1_GRAVITY;
    const int32_t fall_max = toggle_on ? ACR_GEN1_FALL_MAX * ACR_GEN1_CRUISE_AIR_FALL_PCT / 100 : ACR_GEN1_FALL_MAX;
    if (ac->quick_ticks && (ac->state == ACR_GEN1_GROUND || ac->state == ACR_GEN1_AIR)) {
        --ac->quick_ticks;
        drain = 4;                              /* paid at the press: no cost, no refill */
        if (ac->state == ACR_GEN1_GROUND) {
            ac->coast = ACR_GEN1_BOOST_COAST_TICKS;
            mx = ac->vx;
            mz = ac->vz;
            goto moved;
        }
    } else {
        ac->quick_ticks = 0;
    }

    if (in.stun && ac->state == ACR_GEN1_GROUND && !in.swing) {
        ac->vy = 0;
        mx = ac->vx;
        mz = ac->vz;
        goto moved;
    }
    if (in.swing && (ac->state == ACR_GEN1_GROUND || ac->state == ACR_GEN1_AIR)) {
        const int k = in.swing - 1;
        if (k >= 12) {
            ac->vx = ac->vy = ac->vz = 0;
        } else if (k >= 7 && !in.swing_fwd) {
            limit_length(&ac->vx, &ac->vz, ACR_GEN1_SWING_CAP);
            slow_down(&ac->vx, &ac->vz, ACR_GEN1_SWING_SLOW);
        }
        if (ac->state == ACR_GEN1_GROUND) ac->vy = 0;
        mx = ac->vx;
        mz = ac->vz;
        goto moved;
    }

    switch (ac->state) {
    case ACR_GEN1_GROUND: {
        if (in.crouch) {
            crouch_slide(&ac->vx);
            crouch_slide(&ac->vz);
            if (walking(ac)) set_anim(ac, ACR_GEN1_ANIM_IDLE);
            mx = ac->vx;
            mz = ac->vz;
            break;
        }
        /* Already moving (walking, or sliding after a boost): a direction
         * and the booster boost along the ground. From a stand the booster
         * jumps, with or without a direction (checked in the recomp: the
         * first step's press still jumps). */
        const int moving = walking(ac) || ac->vx || ac->vz;
        /* Boost hop: let go of the booster during a ground boost and press it
         * again while still sliding, and the AC jumps straight into the air,
         * keeping its speed (humanoid, reverse-joint and four-legged legs;
         * as described by a player, not yet measured). */
        /* Caterpillars (user, 2026-11-19): no ground boost and no jump. The
         * booster lifts them straight into the rising boost, from a stand
         * or on the move, without the take-off kick the other legs start
         * their arc with; they keep their speed across the ground. With
         * SP-MODERN the same, at the modern rise's lift (and any other
         * no_boost_hop legs lift off this way while moving). */
        if ((def->no_jump && can_rise) || (in.modern && tracks && can_jump && moving)) {
            if (!ac->vx && !ac->vz && dir && in.modern) {   /* from a walk: walking pace */
                const int32_t pace = acr_gen1_walk_pace(def);
                ac->vx = (int32_t)((int64_t)dx * pace / 4096);
                ac->vz = (int32_t)((int64_t)dz * pace / 4096);
            }
            ac->vy = -(int32_t)m->rise_accel * (in.modern ? ACR_GEN1_MODERN_RISE_PCT : 100) / 100;   /* the rise's first tick */
            if (ac->vy >= 0) ac->vy = -1;
            drain = 1;                                 /* marker: rising */
            ac->state = ACR_GEN1_AIR;
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
            mx = ac->vx;
            mz = ac->vz;
            break;
        }
        if (can_jump && fresh_press && (ac->vx || ac->vz) && !def->no_boost_hop && !(def->drive_walk && dir && !in.modern)) {
            int32_t vy;
            uint16_t crouch;
            takeoff(def, &crouch, &vy);
            if (in.modern) vy = vy * ACR_GEN1_MODERN_JUMP_PCT / 100;
            ac->vy = vy;
            ac->state = ACR_GEN1_AIR;
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
            mx = ac->vx;
            mz = ac->vz;
            break;
        }
        /* Four-legged and caterpillar ACs (the game's second movement
         * handler, VS 0x80085704): their walking animations carry no root
         * motion. A direction pushes the AC that way by the ground boost's
         * acceleration (player block +0x208) up to its top speed (+0x20A),
         * along the facing turned by the direction (0, +-45, +-90, 180,
         * +-135 degrees); with none it is cut to the top speed and slows by
         * a quarter of the acceleration (0x80014790, as in the air). The
         * booster from a stand still jumps; boosting while moving is the
         * ground boost, which uses the same two numbers. */
        /* Modern controls: the boost toggle, a slow ground boost. */
        const int cruising = in.cruise && !in.boost && !ac->drained && ac->energy > 0 && dir && moving;
        /* SP-MODERN on these legs: they walk the same way unless the toggle
         * is cruising or cross jumps from a stand. */
        if (def->drive_walk && !ground_boost && !cruising && !(in.modern && can_jump && !ac->vx && !ac->vz)) {
            uint8_t loop = ACR_GEN1_ANIM_IDLE;
            if (dir) {
                ac->vx += dx * (int32_t)m->boost_accel / 4096;
                ac->vz += dz * (int32_t)m->boost_accel / 4096;
                limit_length(&ac->vx, &ac->vz, m->boost_max);
                loop = in.move > 0 ? ACR_GEN1_ANIM_WALK : in.move < 0 ? ACR_GEN1_ANIM_BACK
                     : in.strafe < 0 ? ACR_GEN1_ANIM_STRAFE_L : ACR_GEN1_ANIM_STRAFE_R;
            } else {
                limit_length(&ac->vx, &ac->vz, m->boost_max);
                slow_down(&ac->vx, &ac->vz, m->boost_accel / 4 ? m->boost_accel / 4 : 1);
            }
            /* The legs' walking loop shows while it moves (pose only). */
            if (loop == ACR_GEN1_ANIM_IDLE && !ac->vx && !ac->vz) { if (walking(ac)) set_anim(ac, ACR_GEN1_ANIM_IDLE); }
            else if (loop != ACR_GEN1_ANIM_IDLE) { if (ac->anim != loop) set_anim(ac, loop); else ac->anim_frame++; }
            mx = ac->vx;
            mz = ac->vz;
            break;
        }
        if (cruising) {
            const int32_t top = acr_gen1_cruise_top(def);
            if (!ac->vx && !ac->vz) {               /* from a walk: starts at walking pace */
                const int32_t pace = acr_gen1_walk_pace(def);
                ac->vx = (int32_t)((int64_t)dx * pace / 4096);
                ac->vz = (int32_t)((int64_t)dz * pace / 4096);
            }
            ac->vx += dx * (int32_t)m->boost_accel / 4096;
            ac->vz += dz * (int32_t)m->boost_accel / 4096;
            limit_length(&ac->vx, &ac->vz, top);
            ac->coast = ACR_GEN1_BOOST_COAST_TICKS;
            drain = 3;                          /* marker: cruise (a ground boost at CRUISE_PCT) */
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
        } else if (ground_boost && dir && moving) {
            ac->vx += dx * (int32_t)m->boost_accel / 4096;
            ac->vz += dz * (int32_t)m->boost_accel / 4096;
            limit_length(&ac->vx, &ac->vz, m->boost_max);
            ac->coast = ACR_GEN1_BOOST_COAST_TICKS;   /* measured: 2-3 ticks at full speed */
            drain = 2;                          /* marker: ground boost (half cost) */
            set_anim(ac, ACR_GEN1_ANIM_IDLE);
        } else if (can_jump && ac->vx == 0 && ac->vz == 0) {
            takeoff(def, &ac->timer, &ac->vy);
            if (in.modern) ac->vy = ac->vy * ACR_GEN1_MODERN_JUMP_PCT / 100;
            ac->state = ACR_GEN1_CROUCH;
            set_anim(ac, ACR_GEN1_ANIM_JUMP);
        } else if (ac->vx || ac->vz) {
            /* Slide: keep full speed a moment, then slow by the boost
             * acceleration a tick. */
            if (ac->coast) --ac->coast;
            else slow_down(&ac->vx, &ac->vz, m->boost_accel);
        } else {
            walk(ac, def, in, &mx, &mz);
        }
        mx += ac->vx;
        mz += ac->vz;
        break;
    }
    case ACR_GEN1_CROUCH:
        if (ac->timer) --ac->timer;
        if (!ac->timer) ac->state = ACR_GEN1_AIR;
        break;
    case ACR_GEN1_AIR:
        if (ac->quick_ticks) {                  /* quick boost: keeps its line, no rise */
            ac->vy += gravity;
            if (ac->vy > fall_max) ac->vy = fall_max;
            mx = ac->vx;
            mz = ac->vz;
            break;
        }
        if (can_rise) {
            const int32_t pct = in.modern ? ACR_GEN1_MODERN_RISE_PCT : 100;
            ac->vy -= (int32_t)m->rise_accel * pct / 100;
            if (ac->vy < -(int32_t)m->rise_max * pct / 100) ac->vy = -(int32_t)m->rise_max * pct / 100;
            drain = 1;                          /* marker: rising / air boost */
        } else {
            ac->vy += gravity;
            if (ac->vy > fall_max) ac->vy = fall_max;
        }
        /* Steering in the air (VS air handler 0x8007FBAC, direction states
         * 0x41-0x48 from the pad, 0x80085400): any direction held, booster
         * or not, pushes the AC that way by the air acceleration and the
         * speed is then cut to the air top speed. The booster only decides
         * rising or falling. With no direction (or only turning) the speed
         * is first cut to the air top speed, then slows by a quarter of the
         * air acceleration (0x80014790). */
        if (dir) {
            ac->vx += dx * (int32_t)m->air_accel / 4096;
            ac->vz += dz * (int32_t)m->air_accel / 4096;
            limit_length(&ac->vx, &ac->vz, m->air_max);
        } else {
            limit_length(&ac->vx, &ac->vz, m->air_max);
            slow_down(&ac->vx, &ac->vz, m->air_accel / 4);
        }
        mx = ac->vx;
        mz = ac->vz;
        break;
    case ACR_GEN1_LANDING:
        if (ac->timer) --ac->timer;
        if (!ac->timer) { ac->state = ACR_GEN1_GROUND; ac->landing_soft = 0; }
        slow_down(&ac->vx, &ac->vz, m->boost_accel);   /* only the stand-in keeps speed */
        mx = ac->vx;
        mz = ac->vz;
        break;
    }
    /* SP-MODERN: the boost toggle left on always drains, never refills:
     * on the ground (standing, crouching for a jump, landing) at its own
     * rate, in the air (falling or drifting, not rising) at 110 % of it. */
    if (!drain && toggle_on)
        drain = ac->state == ACR_GEN1_AIR ? 6 : 5;   /* marker: the toggle's drain, not moving on it */

moved:
    /* Every move is cut to 75 a tick on each axis (three quarters of the
     * AC's width, AC object +0x28; VS 0x80054124, which all the movement
     * handlers load their step through). The air boost measured 54 a tick,
     * so the air uses the same three-quarter limit, not the half one. */
    if (m->axis_step_max) {
        const int32_t lim = (int32_t)m->axis_step_max * 64;
        if (mx > lim) mx = lim;
        if (mx < -lim) mx = -lim;
        if (mz > lim) mz = lim;
        if (mz < -lim) mz = -lim;
    }
    /* The jump's take-off animation counts on a frame a tick through the
     * crouch and into the air, as the game's animation player does; the
     * drawing plays it from this and turns to the air poses when it ends. */
    if (ac->anim == ACR_GEN1_ANIM_JUMP && ac->anim_frame < UINT16_MAX) ++ac->anim_frame;
    if (def->stage && def->stage->areas) {
        game_stage_tick(ac, def, mx, mz, drain == 2 || drain == 3);
    } else {
        ac->x += mx;
        ac->z += mz;
        /* The floor under the AC: y = 0 on the open test floor; in an arena the
         * stage's (walls first, then floors; see the step stand-ins). */
        int32_t floor_y = 0;
        int have_floor = 1;
        if (def->stage) stage_move(ac, def, &floor_y, &have_floor);
        if (ac->state == ACR_GEN1_AIR) {
            ac->y += ac->vy;
            if (def->stage && ac->vy < 0) {          /* rising into a ceiling */
                int32_t c;
                const int32_t head = ac->y / 64 - ACR_GEN1_HIT_HEIGHT_SIM;
                if (acr_gen1_stage_ceiling(def->stage, ac->x / 64, ac->z / 64, ac->py / 64 - ACR_GEN1_HIT_HEIGHT_SIM, &c) &&
                    head < c) {
                    ac->y = (c + ACR_GEN1_HIT_HEIGHT_SIM) * 64;
                    ac->vy = 0;
                }
            }
            if (have_floor && ac->y >= floor_y && ac->vy >= 0) {          /* touch down */
                ac->y = floor_y;
                touch_down(ac, def);
            }
        }
    }
    /* Energy (VS engine 0x80082D90): boosting drains, otherwise the
     * generator refills it.
     * - The moving refill is for an AC moving on the ground (walking,
     *   boosting or sliding); standing, turning on the spot and the air
     *   (not boosting) get the standing refill.
     * - Below the red zone both draining and refilling run at a quarter.
     * - A ground boost costs half.
     * - The refill is added before the cap is checked, so a full bar goes
     *   over by up to one refill for a tick, then back to the capacity.
     * - Running dry locks the booster; the bar then refills at half speed
     *   (with no quarter below the red zone) and the lock lifts only when
     *   it is completely full. */
    const int on_ground = ac->state == ACR_GEN1_GROUND || ac->state == ACR_GEN1_LANDING;
    const int moving_refill = on_ground && (walking(ac) || ac->vx || ac->vz);
    int32_t cost = s->rise_drain;
    int32_t refill = moving_refill ? s->refill_moving : s->refill_standing;
    if (drain == 4) return;                     /* quick boost burst: paid at the press */
    if (ac->drained) {
        if (ac->energy >= s->energy_capacity) { ac->energy = s->energy_capacity; ac->drained = 0; }
        else ac->energy += refill >> 1;
        return;
    }
    if (ac->energy < s->red_zone) { cost >>= 2; refill >>= 2; }
    if (drain) {
        if (drain >= 2) cost >>= 1;
        else if (drain == 1 && in.modern) cost = cost * ACR_GEN1_MODERN_RISE_EN_PCT / 100;
        if (drain == 3 || drain == 5 || drain == 6) cost = cost * ACR_GEN1_CRUISE_DRAIN_PCT / 100;
        if (drain == 6) cost = cost * ACR_GEN1_CRUISE_AIR_DRAIN_PCT / 100;
        if (ac->energy > cost) ac->energy -= cost;
        else { ac->energy = 0; ac->drained = 1; }
    } else if (ac->energy >= s->energy_capacity) {
        ac->energy = s->energy_capacity;
    } else {
        ac->energy += refill;
    }
}

static int32_t lerp32(int32_t a, int32_t b, uint32_t num, uint32_t den) {
    return a + (int32_t)((int64_t)(b - a) * num / den);
}

static void show(acr_gen1_ac *ac, uint32_t num, uint32_t den) {
    if (!den || num >= den) { num = 1; den = 1; }
    ac->show_num = (uint16_t)num;
    ac->show_den = (uint16_t)den;
    ac->sx = lerp32(ac->px, ac->x, num, den);
    ac->sy = lerp32(ac->py, ac->y, num, den);
    ac->sz = lerp32(ac->pz, ac->z, num, den);
    int32_t dyaw = ((int32_t)ac->yaw - (int32_t)ac->pyaw) & 4095;
    if (dyaw >= 2048) dyaw -= 4096;
    ac->syaw = (uint16_t)((ac->pyaw + (int32_t)((int64_t)dyaw * num / den)) & 4095);
}

void acr_gen1_ac_advance(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in,
                         int ticks, uint32_t num, uint32_t den) {
    /* View tilt: L2 up, R2 down, both back to level (rates per Gen 1 tick,
     * moved every step so it stays smooth). */
    const int scaled = def->step_rate && def->gen1_rate;
    const int64_t rn = scaled ? def->gen1_rate : 1, rd = scaled ? def->step_rate : 1;
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

    if (!in.boost || in.boost_tapped) ac->boost_released = 1;
    if (in.quick) ac->quick_pending = 1;          /* a press between ticks still counts */
    for (int k = 0; k < ticks; ++k) acr_gen1_ac_tick(ac, def, in);
    show(ac, num, den);
}

int acr_gen1_ac_step(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in) {
    const int scaled = def->step_rate && def->gen1_rate;
    const uint32_t rn = scaled ? def->gen1_rate : 1, rd = scaled ? def->step_rate : 1;
    if (ac->phase >= rd) ac->phase = (uint16_t)(rn < rd ? rd - rn : 0);   /* the rate changed */
    uint32_t p = ac->phase + rn;
    const int ticks = (int)(p / rd);
    p %= rd;
    ac->phase = (uint16_t)p;
    acr_gen1_ac_advance(ac, def, in, ticks, scaled ? p : 1, scaled ? rd : 1);
    return ticks;
}

void acr_gen1_ac_shift(acr_gen1_ac *ac, int32_t dx, int32_t dz) {
    ac->x += dx; ac->px += dx; ac->sx += dx;
    ac->z += dz; ac->pz += dz; ac->sz += dz;
}

void acr_gen1_ac_set_yaw(acr_gen1_ac *ac, uint16_t yaw) {
    ac->yaw = ac->pyaw = ac->syaw = (uint16_t)(yaw & 4095);
}
