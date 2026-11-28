#include "acr_gen2_move.h"

#include "acr_gen2_fp.h"

/* Every float operation goes through these: the PS2's rounding, and the same
 * bits on every machine (acr_gen2_fp.h). */
#define ADD(a, b) g2_add((a), (b))
#define SUB(a, b) g2_sub((a), (b))
#define MUL(a, b) g2_mul((a), (b))
#define DIV(a, b) g2_div((a), (b))

static float g2_sqrt(float v) { return g2_chop(sqrt((double)v)); }
static float g2_len(float x, float z) { return g2_sqrt(ADD(MUL(x, x), MUL(z, z))); }

#define G2_PI      3.14159274f
#define G2_TWO_PI  6.28318548f
#define G2_HALF_PI 1.57079637f
#define G2_EPS     0.001f       /* the game's own "not moving" margin */

/* Movement directions, in the AC's own frame: x, z (0x3c7f90). */
static const float DIR_X[8] = { 0.0f, 0.707107f, -0.707107f, 1.0f, -1.0f, 0.0f, 0.707107f, -0.707107f };
static const float DIR_Z[8] = { -1.0f, -0.707107f, -0.707107f, 0.0f, 0.0f, 1.0f, 0.707107f, 0.707107f };
/* Overboost goes forward, or sideways with a strafe held (0x3c7ee8). */
static const uint8_t OB_DIR[9] = { 0, 3, 4, 3, 4, 0, 3, 4, 0 };

/* Sine and cosine by series, from the same arithmetic, so every machine
 * agrees (the C library's differ in the last bit). Good to 4e-6. */
static void g2_sincos(float a, float *s, float *c)
{
    static const float SK[4] = { 2.75573192e-6f, -1.98412698e-4f, 8.33333333e-3f, -0.166666667f };
    static const float CK[5] = { -2.75573192e-7f, 2.48015873e-5f, -1.38888889e-3f, 4.16666667e-2f, -0.5f };
    float x, x2, p, sign = 1.0f;
    int i;
    while (a > G2_PI) a = SUB(a, G2_TWO_PI);
    while (a < -G2_PI) a = ADD(a, G2_TWO_PI);
    x = a;
    if (x > G2_HALF_PI) { x = SUB(G2_PI, x); sign = -1.0f; }
    else if (x < -G2_HALF_PI) { x = SUB(-G2_PI, x); sign = -1.0f; }
    x2 = MUL(x, x);
    for (p = SK[0], i = 1; i < 4; i++) p = ADD(MUL(p, x2), SK[i]);
    *s = MUL(x, ADD(MUL(p, x2), 1.0f));
    for (p = CK[0], i = 1; i < 5; i++) p = ADD(MUL(p, x2), CK[i]);
    *c = MUL(sign, ADD(MUL(p, x2), 1.0f));
}

int acr_gen2_dir(const acr_gen2_input *in)
{
    if (in->forward) {
        if (in->left) return ACR_GEN2_DIR_FWD_L;
        if (in->right) return ACR_GEN2_DIR_FWD_R;
    }
    if (in->back) {
        if (in->left) return ACR_GEN2_DIR_BACK_L;
        if (in->right) return ACR_GEN2_DIR_BACK_R;
    }
    if (in->forward) return ACR_GEN2_DIR_FWD;
    if (in->back) return ACR_GEN2_DIR_BACK;
    if (in->left) return ACR_GEN2_DIR_L;
    return in->right ? ACR_GEN2_DIR_R : ACR_GEN2_DIR_NONE;
}

void acr_gen2_move_init(acr_gen2_ac *ac, const acr_gen2_stats *s)
{
    acr_gen2_ac zero = { 0 };
    *ac = zero;
    ac->energy = s->boost_max;
    ac->move_dir = ACR_GEN2_DIR_FWD;
}

/* 0x13f960 */
static void set_state(acr_gen2_ac *ac, int state)
{
    ac->state = (uint8_t)state;
    ac->substate = 0;
    ac->pending = 0;
    ac->state_frame = 0;
    ac->changed = 1;
}

/* 0x140af8: turning speeds up by accel a frame while a turn button is held
 * and slows by the same when it is not. */
static void turn(acr_gen2_ac *ac, const acr_gen2_input *in, float accel, float max)
{
    float v = ac->yaw_speed;
    if (in->turn > 0) v = ADD(v, accel);
    else if (in->turn < 0) v = SUB(v, accel);
    else if (0.0f < v) v = accel < v ? SUB(v, accel) : 0.0f;
    else v = v < -accel ? ADD(v, accel) : 0.0f;
    if (max < v) v = max;
    if (v < -max) v = -max;
    ac->yaw_speed = v;
    ac->yaw = ADD(ac->yaw, v);
    if (ac->yaw > G2_PI) ac->yaw = SUB(ac->yaw, G2_TWO_PI);
    else if (ac->yaw < -G2_PI) ac->yaw = ADD(ac->yaw, G2_TWO_PI);
}

/* 0x140c58: slow the ground speed by dec; returns what is left. */
static float brake(acr_gen2_ac *ac, float dec)
{
    float s = g2_len(ac->vx, ac->vz);
    float n = SUB(s, dec);
    if (n <= 0.0f) {
        ac->vx = 0.0f;
        ac->vz = 0.0f;
        return 0.0f;
    }
    ac->vx = DIV(MUL(ac->vx, n), s);
    ac->vz = DIV(MUL(ac->vz, n), s);
    return n;
}

/* 0x140d18: add a push to the ground velocity and hold the speed to max. An
 * AC already faster than max (out of an overboost) keeps its speed and
 * sheds the air acceleration a frame. */
static void push(acr_gen2_ac *ac, const acr_gen2_stats *s, float ax, float az, float max)
{
    float old = g2_len(ac->vx, ac->vz), now, to, k;
    ac->vx = ADD(ac->vx, ax);
    ac->vz = ADD(ac->vz, az);
    now = g2_len(ac->vx, ac->vz);
    if (ADD(max, G2_EPS) < old) {
        to = SUB(old, s->flymv_accel);
        if (to < max) to = max;
    } else {
        to = now;
        if (max < to) to = max;
    }
    if (0.0f < now) {
        k = DIV(to, now);
        ac->vz = MUL(ac->vz, k);
        ac->vx = MUL(ac->vx, k);
    }
}

/* 0x140bb8: accelerate along a direction of the AC's own frame. */
static void move(acr_gen2_ac *ac, const acr_gen2_stats *s, int dir, float accel, float max)
{
    float sn, cs, lx = MUL(accel, DIR_X[dir]), lz = MUL(accel, DIR_Z[dir]);
    g2_sincos(ac->yaw, &sn, &cs);
    push(ac, s, ADD(MUL(lx, cs), MUL(lz, sn)), SUB(MUL(lz, cs), MUL(lx, sn)), max);
}

/* 0x140e58: fall. */
static void fall(acr_gen2_ac *ac, float gravity, float max)
{
    float v;
    float sum = ADD(ac->vy, gravity);
    if (ADD(max, G2_EPS) < ac->vy) {
        v = SUB(ac->vy, gravity);
        if (v < max) v = max;
    } else {
        v = sum;
        if (max < v) v = max;
    }
    /* The game scales the sum to the result rather than storing it (the last
     * bit can differ); a zero sum gives 0 on the PS2. */
    ac->vy = sum != 0.0f ? MUL(sum, DIV(v, sum)) : 0.0f;
}

/* 0x143858 with the rise's values: up is negative. */
static void rise(acr_gen2_ac *ac, const acr_gen2_stats *s)
{
    float v = SUB(ac->vy, s->flyup_accel);
    if (v < -s->flyup_maxspd) v = -s->flyup_maxspd;
    ac->vy = v;
}

/* 0x13ec60 */
static int spend(acr_gen2_ac *ac, int32_t cost)
{
    if (ac->charging || ac->energy < cost) return 0;
    ac->energy -= cost;
    return 1;
}

static int can_crouch(const acr_gen2_stats *s)
{
    return s->leg_kind == ACR_GEN2_LEGS_TWO || s->leg_kind == ACR_GEN2_LEGS_FOUR;
}

static void lift_off(acr_gen2_ac *ac, const acr_gen2_stats *s)
{
    ac->vy = -s->flyup_accel;
    set_state(ac, ACR_GEN2_ST_AIR);
}

/* 0x13daf8: the booster with no direction held, on legs that can: jump. */
static int try_crouch(acr_gen2_ac *ac, const acr_gen2_stats *s, int dir)
{
    if (dir != ACR_GEN2_DIR_NONE || !can_crouch(s)) return 0;
    set_state(ac, ACR_GEN2_ST_CROUCH);
    return 1;
}

/* Two-legged walking: the walk clip's own forward movement (0x144a40). */
static void walk(acr_gen2_ac *ac, const acr_gen2_stats *s, int dir, float speed)
{
    if (speed > 0.0f) move(ac, s, dir, speed, speed);
}

/* ---- per-frame state handlers ---- */

static void st_stand(acr_gen2_ac *ac, const acr_gen2_stats *s)
{
    brake(ac, s->leg_kind == ACR_GEN2_LEGS_HOVER ? MUL(s->flymv_accel, 0.25f) : s->dash_brake);
}

static void st_turn(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_input *in)
{
    if (ac->substate == 0) {
        ac->yaw_speed = 0.0f;
        ac->substate = 1;
        ac->changed = 1;
    }
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    brake(ac, s->leg_kind == ACR_GEN2_LEGS_HOVER ? MUL(s->flymv_accel, 0.25f) : s->dash_brake);
}

/* 0x1445c0: the first steps. No turning, and no change of mind for 7 frames. */
static void st_walk_start(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_gait *g, int dir)
{
    int d, frames;
    if (ac->substate == 0) {
        ac->move_dir = (uint8_t)(dir == ACR_GEN2_DIR_NONE ? ACR_GEN2_DIR_FWD : dir);
        ac->substate = 1;
        ac->changed = 1;
        ac->events = 0;
        return;
    }
    d = ac->move_dir;
    frames = g->walk_start_frames[d];
    ac->events = ac->state_frame >= 7;
    if (ac->state_frame >= frames - 1) {
        set_state(ac, ac->pending ? ac->pending : ACR_GEN2_ST_WALK);
        return;
    }
    if (frames > 0) walk(ac, s, d, DIV(g->walk_start_dist[d], (float)frames));
}

static void st_walk(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_gait *g,
                    const acr_gen2_input *in, int dir)
{
    if (dir != ACR_GEN2_DIR_NONE) ac->move_dir = (uint8_t)dir;
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    walk(ac, s, ac->move_dir, g->walk_speed[ac->move_dir]);
}

/* 0x143a70 (caterpillar, four legs) and 0x144de8 (hover): glide. */
static void st_glide(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_input *in, int dir)
{
    int hover = s->leg_kind == ACR_GEN2_LEGS_HOVER;
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    if (dir == ACR_GEN2_DIR_NONE) {
        brake(ac, hover ? MUL(s->flymv_accel, 0.25f) : s->dash_brake);
        return;
    }
    ac->move_dir = (uint8_t)dir;
    if (hover) move(ac, s, dir, s->flymv_accel, s->flymv_maxspd);
    else move(ac, s, dir, s->dash_accel, s->dash_maxspd);
}

/* 0x13ba80: ground boost. While the button is held (pending 5) the stick
 * steers it; with no direction it keeps its last one. Once let go (pending
 * 6, set by the events below) it holds its direction, and after frame 16
 * coasts dash_mintime more frames before the slide. */
static void st_dash(acr_gen2_ac *ac, const acr_gen2_tuning *t, const acr_gen2_stats *s,
                    const acr_gen2_input *in, int dir)
{
    int held;
    if (ac->state_frame == 0) {
        ac->pending = ACR_GEN2_ST_DASH;
        ac->dash_idle = 0;
        ac->dash_coast = 0;
    }
    held = ac->pending == ACR_GEN2_ST_DASH;
    if (held && (float)ac->dash_idle < t->dash_mintime) {
        if (dir != ACR_GEN2_DIR_NONE) ac->dash_idle = 0;
        else ac->dash_idle++;
    }
    if (dir != ACR_GEN2_DIR_NONE && held && (float)ac->dash_idle < t->dash_mintime)
        ac->move_dir = (uint8_t)dir;
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    move(ac, s, ac->move_dir, s->dash_accel, s->dash_maxspd);
    if (held && !ac->charging && (float)ac->dash_idle < t->dash_mintime) return;
    if (ac->state_frame < 16) return;
    ac->dash_coast++;
    if (t->dash_mintime < (float)ac->dash_coast) {
        set_state(ac, ACR_GEN2_ST_BRAKE);
        ac->events = 0;
    }
}

/* 0x13bce0: the slide. At least 21 frames, then 17 more once stopped. */
static void st_brake(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_input *in)
{
    float left;
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    left = brake(ac, s->dash_brake);
    if (ac->substate == 0) {
        if (ac->state_frame > 20 && left <= G2_EPS) {
            ac->substate = 1;
            ac->state_frame = 0;
            ac->changed = 1;
        }
    } else if (ac->state_frame > 16) {
        set_state(ac, ACR_GEN2_ST_STAND);
    }
}

/* 0x13bed8 (0x144ec8 for hovers, which lift at once): the squat before a jump. */
static void st_crouch(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_gait *g,
                      const acr_gen2_input *in)
{
    int frames = g->crouch_frames > 20 ? g->crouch_frames : 20;   /* a 20-frame blend, then the clip's end */
    if (s->leg_kind == ACR_GEN2_LEGS_HOVER) {
        brake(ac, MUL(s->flymv_accel, 0.25f));
        lift_off(ac, s);
        return;
    }
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    if (ac->state_frame < frames) return;
    ac->vx = 0.0f;
    ac->vz = 0.0f;
    ac->vy = -s->jumpspd;
    set_state(ac, ACR_GEN2_ST_AIR);
    ac->events = 0;     /* the first frame up is the full jump speed: no gravity, no booster yet */
}

/* 0x13c0d0 */
static void st_air(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_input *in, int dir)
{
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    if (dir != ACR_GEN2_DIR_NONE) move(ac, s, dir, s->flymv_accel, s->flymv_maxspd);
    else brake(ac, MUL(s->flymv_accel, 0.25f));
}

/* 0x13d2e8 */
static void st_land(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_gait *g,
                    const acr_gen2_input *in)
{
    brake(ac, s->dash_brake);
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    if (ac->state_frame >= g->land_frames) set_state(ac, ACR_GEN2_ST_STAND);
}

/* 0x13d3a0: no control until the clip ends. */
static void st_hard_land(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_gait *g)
{
    brake(ac, s->dash_brake);
    ac->events = 0;
    if (ac->state_frame >= g->hard_land_frames) set_state(ac, ACR_GEN2_ST_STAND);
}

/* 0x13d650 */
static void st_overboost(acr_gen2_ac *ac, const acr_gen2_tuning *t, const acr_gen2_stats *s,
                         const acr_gen2_input *in, int dir)
{
    float accel = s->oboost_acc;
    if (ac->substate == 0) {
        accel = s->oboost_spd0;
        ac->substate = 1;
    }
    turn(ac, in, s->turn_accel, s->turn_maxspd);
    move(ac, s, OB_DIR[dir], accel, s->oboost_spd0);
    if (in->boost) rise(ac, s);
    else fall(ac, t->gravity, t->fall_maxspd);
}

/* ---- events: what the held buttons do in each state (0x13fc08) ---- */

enum { EV_BOOST, EV_BOOST_ONLY, EV_DIR, EV_TURN, EV_NONE };

static int pick_event(const acr_gen2_ac *ac, const acr_gen2_input *in, int dir, int has_boost, int has_boost_only)
{
    if (has_boost && in->boost && !ac->charging) return EV_BOOST;
    if (dir != ACR_GEN2_DIR_NONE) return EV_DIR;
    if (has_boost_only && in->boost) return EV_BOOST_ONLY;
    return in->turn ? EV_TURN : EV_NONE;
}

/* The booster on the ground. Two legs: jump with no direction, boost with one
 * (0x144760). Caterpillar and four legs: jump if they can, else lift off
 * (0x143bb0). Hover: boost with a direction, else lift off (0x144f20). */
static void ev_ground_boost(acr_gen2_ac *ac, const acr_gen2_stats *s, int dir)
{
    switch (s->leg_kind) {
    case ACR_GEN2_LEGS_TWO:
        if (try_crouch(ac, s, dir)) return;
        if (dir != ACR_GEN2_DIR_NONE) {
            ac->move_dir = (uint8_t)dir;
            set_state(ac, ACR_GEN2_ST_DASH);
        }
        return;
    case ACR_GEN2_LEGS_HOVER:
        if (dir != ACR_GEN2_DIR_NONE) {
            ac->move_dir = (uint8_t)dir;
            set_state(ac, ACR_GEN2_ST_DASH);
        } else {
            lift_off(ac, s);
        }
        return;
    default:
        if (!try_crouch(ac, s, dir)) lift_off(ac, s);
        return;
    }
}

static void events(acr_gen2_ac *ac, const acr_gen2_tuning *t, const acr_gen2_stats *s,
                   const acr_gen2_input *in, int dir)
{
    int two = s->leg_kind == ACR_GEN2_LEGS_TWO, hover = s->leg_kind == ACR_GEN2_LEGS_HOVER;
    int walk_to = two ? ACR_GEN2_ST_WALK_START : ACR_GEN2_ST_WALK;
    int ev;
    switch (ac->state) {
    case ACR_GEN2_ST_STAND:
        ev = pick_event(ac, in, dir, 1, !hover);
        if (ev == EV_BOOST) ev_ground_boost(ac, s, dir);
        else if (ev == EV_BOOST_ONLY) try_crouch(ac, s, dir);   /* run dry: a plain jump still works */
        else if (ev == EV_DIR) set_state(ac, walk_to);
        else if (ev == EV_TURN) set_state(ac, ACR_GEN2_ST_TURN);
        break;
    case ACR_GEN2_ST_WALK_START:
        ev = pick_event(ac, in, dir, 1, 0);
        if (ev == EV_BOOST) {
            if (two) ac->pending = ACR_GEN2_ST_DASH;            /* 0x144730: boost when the steps end */
            else ev_ground_boost(ac, s, dir);
        } else if (ev == EV_TURN) set_state(ac, ACR_GEN2_ST_TURN);
        else if (ev == EV_NONE) set_state(ac, ac->pending ? ac->pending : ACR_GEN2_ST_STAND);
        break;
    case ACR_GEN2_ST_WALK:
        ev = pick_event(ac, in, dir, 1, 0);
        if (ev == EV_BOOST) {
            if (two) set_state(ac, ACR_GEN2_ST_DASH);           /* 0x13db68 */
            else ev_ground_boost(ac, s, dir);
        } else if (ev == EV_TURN || ev == EV_NONE) set_state(ac, ACR_GEN2_ST_STAND);
        break;
    case ACR_GEN2_ST_TURN:
        ev = pick_event(ac, in, dir, hover, 0);                 /* two legs: the booster waits for the turn to end */
        if (ev == EV_BOOST) ev_ground_boost(ac, s, dir);
        else if (ev == EV_DIR) set_state(ac, walk_to);
        else if (ev == EV_NONE) set_state(ac, ACR_GEN2_ST_STAND);
        break;
    case ACR_GEN2_ST_DASH:
        ev = pick_event(ac, in, dir, 1, 0);
        if (ev != EV_BOOST) {
            ac->pending = ACR_GEN2_ST_BRAKE;                    /* 0x13bc80: let go */
        } else if (ac->pending == ACR_GEN2_ST_BRAKE) {          /* 0x13bc90: pressed again: hop into the air */
            ac->vy = hover ? -s->flyup_accel : -t->dashjumpspd;
            set_state(ac, ACR_GEN2_ST_AIR);
        }
        break;
    case ACR_GEN2_ST_BRAKE:
        if (hover && pick_event(ac, in, dir, 1, 0) == EV_BOOST) ev_ground_boost(ac, s, dir);
        break;
    case ACR_GEN2_ST_AIR:
        /* The booster lifts; without it (or run dry) gravity pulls. Never both. */
        if (pick_event(ac, in, dir, 1, 0) == EV_BOOST) rise(ac, s);
        else fall(ac, t->gravity, t->fall_maxspd);
        break;
    case ACR_GEN2_ST_LAND:
        ev = pick_event(ac, in, dir, !two, !two && !hover);
        if (ev == EV_BOOST) ev_ground_boost(ac, s, dir);
        else if (ev == EV_BOOST_ONLY) try_crouch(ac, s, dir);
        else if (ev == EV_DIR) set_state(ac, ACR_GEN2_ST_WALK); /* 0x13d998: straight into the walk loop */
        break;
    default:
        break;
    }
}

/* ---- energy (0x139c50) ---- */

static int32_t half(int32_t v) { return (int32_t)MUL((float)v, 0.5f); }

static int32_t drain_now(const acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_input *in)
{
    if (ac->state == ACR_GEN2_ST_OVERBOOST)
        return in->boost ? (int32_t)ADD((float)s->oboost_sub, MUL((float)s->boost_sub, 0.5f)) : s->oboost_sub;
    if (!in->boost) return 0;
    if (ac->state == ACR_GEN2_ST_AIR) return s->boost_sub;
    if (ac->state == ACR_GEN2_ST_DASH) return half(s->boost_sub);   /* the ground boost costs half */
    return 0;
}

static int32_t refill_now(const acr_gen2_ac *ac, const acr_gen2_stats *s)
{
    int moving;
    if (ac->state == ACR_GEN2_ST_STAND) return s->boost_add;
    if (s->leg_kind == ACR_GEN2_LEGS_TWO)
        return (ac->state == ACR_GEN2_ST_WALK || ac->state == ACR_GEN2_ST_DASH) ? s->move_boost_add : s->boost_add;
    if (ac->state != ACR_GEN2_ST_DASH) return s->boost_add;    /* gliding refills as standing does */
    moving = fabsf(ac->vx) >= G2_EPS || fabsf(ac->vz) >= G2_EPS;
    return moving ? s->move_boost_add : s->boost_add;
}

static void energy(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_input *in)
{
    int32_t drain = drain_now(ac, s, in);
    int32_t refill = drain ? 0 : refill_now(ac, s);
    if (ac->charging) {
        refill = half(refill);
        drain = 0;
    } else if (ac->energy < s->boost_redarea) {     /* red zone: a quarter of both */
        refill = (int32_t)MUL((float)refill, 0.25f);
        drain = (int32_t)MUL((float)drain, 0.25f);
    }
    ac->energy += refill;
    if (ac->energy >= s->boost_max) {
        ac->energy = s->boost_max;
        ac->charging = 0;
    }
    if (!spend(ac, drain) && !ac->charging) {
        ac->charging = 1;
        ac->energy = 0;
    }
}

/* ---- overboost switch (0x141df8) ---- */

static void end_overboost(acr_gen2_ac *ac, float ground_y)
{
    set_state(ac, ac->y < ground_y ? ACR_GEN2_ST_AIR : ACR_GEN2_ST_BRAKE);
}

static void overboost_switch(acr_gen2_ac *ac, const acr_gen2_stats *s, const acr_gen2_input *in, float ground_y)
{
    if (in->overboost) {
        if (ac->state == ACR_GEN2_ST_OVERBOOST) end_overboost(ac, ground_y);
        else ac->ob_charge = ac->ob_charge ? 0 : 1;
    }
    if (!ac->ob_charge) return;
    if (ac->charging || ac->energy < s->oboost_1stsub || ac->state == ACR_GEN2_ST_HARD_LAND) {
        ac->ob_charge = 0;
        return;
    }
    if (++ac->ob_charge > ACR_GEN2_OB_CHARGE_FRAMES) {
        ac->ob_charge = 0;
        spend(ac, s->oboost_1stsub);
        set_state(ac, ACR_GEN2_ST_OVERBOOST);
    }
}

/* ---- touching down (0x13f494) ---- */

static void touch_down(acr_gen2_ac *ac, const acr_gen2_tuning *t, const acr_gen2_stats *s)
{
    if (s->leg_kind == ACR_GEN2_LEGS_HOVER) {
        set_state(ac, ACR_GEN2_ST_STAND);
    } else if (s->leg_kind != ACR_GEN2_LEGS_CATERPILLAR && t->land_dmgspd < ac->vy) {
        ac->vx = MUL(ac->vx, 0.3f);         /* 0x13da80 */
        ac->vz = MUL(ac->vz, 0.3f);
        set_state(ac, ACR_GEN2_ST_HARD_LAND);
    } else {
        if (DIV(t->fall_maxspd, 3.0f) < ac->vy) {   /* 0x13d9a8 */
            ac->vx = MUL(ac->vx, 0.7f);
            ac->vz = MUL(ac->vz, 0.7f);
        }
        set_state(ac, ACR_GEN2_ST_LAND);
    }
}

void acr_gen2_move_step(acr_gen2_ac *ac, const acr_gen2_tuning *t, const acr_gen2_stats *s,
                        const acr_gen2_gait *g, const acr_gen2_input *in, float ground_y)
{
    int dir = acr_gen2_dir(in);
    int two = s->leg_kind == ACR_GEN2_LEGS_TWO;
    float px = ac->x, pz = ac->z, moved, speed;

    overboost_switch(ac, s, in, ground_y);

    /* The state's own frame, then what the buttons ask of it (0x13f8d8). */
    ac->events = 1;
    ac->changed = 0;
    switch (ac->state) {
    case ACR_GEN2_ST_STAND:      st_stand(ac, s); break;
    case ACR_GEN2_ST_WALK_START: if (two) st_walk_start(ac, s, g, dir); else st_glide(ac, s, in, dir); break;
    case ACR_GEN2_ST_WALK:       if (two) st_walk(ac, s, g, in, dir); else st_glide(ac, s, in, dir); break;
    case ACR_GEN2_ST_TURN:       st_turn(ac, s, in); break;
    case ACR_GEN2_ST_DASH:       st_dash(ac, t, s, in, dir); break;
    case ACR_GEN2_ST_BRAKE:      st_brake(ac, s, in); break;
    case ACR_GEN2_ST_CROUCH:     st_crouch(ac, s, g, in); break;
    case ACR_GEN2_ST_AIR:        st_air(ac, s, in, dir); break;
    case ACR_GEN2_ST_LAND:       st_land(ac, s, g, in); break;
    case ACR_GEN2_ST_HARD_LAND:  st_hard_land(ac, s, g); break;
    case ACR_GEN2_ST_OVERBOOST:  st_overboost(ac, t, s, in, dir); break;
    default:                     set_state(ac, ACR_GEN2_ST_STAND); break;
    }
    if (ac->events) events(ac, t, s, in, dir);
    if (!ac->changed) ac->state_frame++;

    /* Move, and meet the ground. */
    ac->x = ADD(ac->x, ac->vx);
    ac->y = ADD(ac->y, ac->vy);
    ac->z = ADD(ac->z, ac->vz);
    if (ac->y >= ground_y) {
        ac->y = ground_y;
        if (ac->state == ACR_GEN2_ST_AIR && ac->vy >= 0.0f) touch_down(ac, t, s);
        if (ac->vy > 0.0f) ac->vy = 0.0f;
    } else if (ac->state != ACR_GEN2_ST_AIR && ac->state != ACR_GEN2_ST_OVERBOOST
               && ac->state != ACR_GEN2_ST_CROUCH && ac->y < SUB(ground_y, G2_EPS)) {
        set_state(ac, ACR_GEN2_ST_AIR);     /* the ground fell away */
    }

    /* Gravity for every state that does not handle its own (0x1391b0). */
    if (ac->state != ACR_GEN2_ST_AIR && ac->state != ACR_GEN2_ST_OVERBOOST && ac->state != ACR_GEN2_ST_CROUCH)
        fall(ac, t->gravity, t->fall_maxspd);

    energy(ac, s, in);
    if (ac->state == ACR_GEN2_ST_OVERBOOST && ac->charging) end_overboost(ac, ground_y);

    /* The HUD's speed: the smaller of distance covered and ground speed, eased in (0x138f08). */
    moved = g2_len(SUB(ac->x, px), SUB(ac->z, pz));
    speed = g2_len(ac->vx, ac->vz);
    if (moved < speed) speed = moved;
    ac->speed_shown = ADD(ac->speed_shown, MUL(SUB(speed, ac->speed_shown), 0.1f));
}
