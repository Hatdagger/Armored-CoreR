/* Gen 1 movement, with made-up stats and tracks. One acr_gen1_ac_step is
 * one Gen 1 tick unless the def has a faster step rate. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_sim.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const acr_gen1_root_key k_walk[] = { { 2, 0, 0, -248 }, { 4, 0, 0, -548 } };
static const acr_gen1_root_key k_jump[] = { { 7, 0, 0, 0 }, { 9, 0, -400, 0 } };

static acr_gen1_ac_def make_def(void) {
    acr_gen1_ac_def d;
    memset(&d, 0, sizeof d);
    d.stats.energy_capacity = 28000;
    d.stats.rise_drain = 400;
    d.stats.refill_standing = 200;
    d.stats.refill_moving = 100;
    d.stats.red_zone = 8000;
    d.stats.thrust = 32; d.stats.thrust_80 = 25; d.stats.thrust_120 = 38;
    d.stats.turn_accel = 22; d.stats.turn_speed = 41; d.stats.friction = 43;
    acr_gen1_motion_from_stats(&d.stats, &d.motion);
    static acr_gen1_root_track walk = { 4, 2, k_walk }, jump = { 9, 2, k_jump };
    d.anim[ACR_GEN1_ANIM_WALK] = &walk;
    d.anim[ACR_GEN1_ANIM_JUMP] = &jump;
    d.landing_steps = 20;
    return d;
}

static const acr_gen1_input k_none = { 0, 0, 0, 0, 0, 0, 0 };
static const acr_gen1_input k_fwd = { 1, 0, 0, 0, 0, 0, 0 };
static const acr_gen1_input k_boost = { 1, 0, 0, 1, 0, 0, 0 };
static const acr_gen1_input k_up = { 0, 0, 0, 1, 0, 0, 0 };

/* A script of inputs, one per Gen 1 tick: walk, ground boost, slide, hop,
 * air boost with a turn, fall, land, walk back, jump and rise. */
static acr_gen1_input script(int tick) {
    acr_gen1_input in = k_none;
    if (tick < 6) in = k_fwd;
    else if (tick < 26) in = k_boost;
    else if (tick < 28) in = k_fwd;                  /* let go: slide */
    else if (tick < 40) { in = k_boost; in.turn = 1; }   /* hop, then air boost turning */
    else if (tick < 90) in = k_none;                 /* fall and land */
    else if (tick < 100) { in.move = -1; in.strafe = 1; }
    else if (tick < 130) in = k_up;                  /* jump and rise */
    return in;
}

static int same(const acr_gen1_ac *a, const acr_gen1_ac *b) {
    return a->x == b->x && a->y == b->y && a->z == b->z && a->vx == b->vx && a->vy == b->vy &&
           a->vz == b->vz && a->yaw == b->yaw && a->energy == b->energy && a->state == b->state &&
           a->drained == b->drained && a->anim == b->anim && a->anim_frame == b->anim_frame;
}

int main(void) {
    for (int a = 0; a < 4096; a += 64) {
        const long s = acr_gen1_sin((uint16_t)a), c = acr_gen1_cos((uint16_t)a);
        EXPECT(s * s + c * c > 4090L * 4090L && s * s + c * c < 4102L * 4102L);
    }
    const acr_gen1_ac_def d = make_def();
    acr_gen1_ac ac;

    /* Walking forward at facing 0 moves +z by the track / 4: 31, 31, 37, 38. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    int32_t last = ac.z, got[4];
    for (int i = 0; i < 4; ++i) { acr_gen1_ac_step(&ac, &d, k_fwd); got[i] = (ac.z - last) / 64; last = ac.z; }
    EXPECT(got[0] + got[1] == 62 && got[2] + got[3] == 75);
    EXPECT(ac.sz == ac.z);                       /* a tick every step: shown = the game's */

    /* Walking forward and right at facing 0 moves along the diagonal. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    const acr_gen1_input diag = { 1, 0, 1, 0, 0, 0, 0 };
    for (int i = 0; i < 4; ++i) acr_gen1_ac_step(&ac, &d, diag);
    EXPECT(ac.x > 0 && ac.z > 0 && ac.x - ac.z < 4 * 64 && ac.z - ac.x < 4 * 64);

    /* Turning: +6 a tick up to 38. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    const acr_gen1_input turn = { 0, 1, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 10; ++i) acr_gen1_ac_step(&ac, &d, turn);
    EXPECT(ac.turn_rate == 38);
    EXPECT(ac.yaw == 6 + 12 + 18 + 24 + 30 + 36 + 38 * 4);
    acr_gen1_ac_step(&ac, &d, k_none);
    EXPECT(ac.turn_rate == 0);

    /* From a stand, a direction and the booster jump (the game boosts along
     * the ground only once the AC is already moving). */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    acr_gen1_ac_step(&ac, &d, k_boost);
    EXPECT(ac.state == ACR_GEN1_CROUCH && ac.vz == 0);

    /* Walking first, the same press boosts: limited to 75 a tick on an axis. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    acr_gen1_ac_step(&ac, &d, k_fwd);
    EXPECT(ac.energy == 28000);
    for (int i = 0; i < 20; ++i) { last = ac.z; acr_gen1_ac_step(&ac, &d, k_boost); }
    EXPECT(ac.state == ACR_GEN1_GROUND && ac.z - last == 75 * 64);
    EXPECT(ac.energy == 28000 - 20 * 200);

    /* Rise: the press, 7 still ticks, lift-off at 3,200, then rise acceleration. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    for (int i = 0; i < 9; ++i) acr_gen1_ac_step(&ac, &d, k_up);
    EXPECT(ac.state == ACR_GEN1_AIR && ac.vy == -3200 - 64);
    /* Let go: falls back and lands, then waits. */
    int steps = 0;
    while (ac.state == ACR_GEN1_AIR && steps < 200) { acr_gen1_ac_step(&ac, &d, k_none); ++steps; }
    EXPECT(ac.state == ACR_GEN1_LANDING && ac.y == 0);
    for (int i = 0; i < 20; ++i) acr_gen1_ac_step(&ac, &d, k_none);
    EXPECT(ac.state == ACR_GEN1_GROUND);

    /* Landing check: the pause and the touchdown speed are kept; with the
     * test switch on, a landing lasts the landing animation (here 13 ticks)
     * whatever the fall. */
    {
        static const acr_gen1_root_key k_land[] = { { 13, 0, 0, 0 } };
        static acr_gen1_root_track land = { 13, 1, k_land };
        acr_gen1_ac_def dl = make_def();
        dl.anim[ACR_GEN1_ANIM_LAND] = &land;
        for (int use_anim = 0; use_anim < 2; ++use_anim) {
            dl.landing_from_anim = (uint8_t)use_anim;
            acr_gen1_ac_spawn(&ac, &dl, 0, 0, 0);
            ac.state = ACR_GEN1_AIR; ac.y = -64; ac.vy = 6000;
            acr_gen1_ac_step(&ac, &dl, k_none);
            EXPECT(ac.state == ACR_GEN1_LANDING && ac.vy_at_touchdown == 6256);
            EXPECT(ac.landing_pause == (use_anim ? 13 : 20));
            int n = 0;
            while (ac.state == ACR_GEN1_LANDING && n < 100) { acr_gen1_ac_step(&ac, &dl, k_none); ++n; }
            EXPECT(n == (use_anim ? 13 : 20));
        }
        /* A soft touchdown: no pause from the guess, the full animation when switched. */
        dl.landing_from_anim = 1;
        acr_gen1_ac_spawn(&ac, &dl, 0, 0, 0);
        ac.state = ACR_GEN1_AIR; ac.y = -64; ac.vy = 1000;
        acr_gen1_ac_step(&ac, &dl, k_none);
        EXPECT(ac.landing_pause == 13);
        dl.landing_from_anim = 0;
        acr_gen1_ac_spawn(&ac, &dl, 0, 0, 0);
        ac.state = ACR_GEN1_AIR; ac.y = -64; ac.vy = 1000;
        acr_gen1_ac_step(&ac, &dl, k_none);
        EXPECT(ac.landing_pause == 0 && ac.state == ACR_GEN1_GROUND);
    }

    /* Boost hop: boost forward, let go one tick, press again -> airborne at speed. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    acr_gen1_ac_step(&ac, &d, k_fwd);
    for (int i = 0; i < 12; ++i) acr_gen1_ac_step(&ac, &d, k_boost);
    acr_gen1_ac_step(&ac, &d, k_fwd);
    EXPECT(ac.state == ACR_GEN1_GROUND && ac.vz > 0);
    acr_gen1_ac_step(&ac, &d, k_boost);
    EXPECT(ac.state == ACR_GEN1_AIR && ac.vy < 0 && ac.vz > 0);

    /* In the air too, a move is cut to 75 a tick on an axis (an AC whose
     * air top speed is over 75 a tick: strong booster, light AC). */
    {
        acr_gen1_ac_def fast = d;
        fast.motion.air_max = 9000;
        acr_gen1_ac_spawn(&ac, &fast, 0, 0, 0);
        ac.state = ACR_GEN1_AIR; ac.y = -100000; ac.vz = 9000;
        last = ac.z;
        acr_gen1_ac_step(&ac, &fast, k_none);
        EXPECT(ac.z - last == 75 * 64);
    }

    /* Steering in the air without the booster (VS 0x8007FBAC): a direction
     * pushes by the air acceleration while falling; the speed is cut to the
     * air top speed; with no direction it is cut, then slows by a quarter. */
    {
        acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
        ac.state = ACR_GEN1_AIR; ac.y = -100000 * 64;
        acr_gen1_ac_step(&ac, &d, k_fwd);
        EXPECT(ac.state == ACR_GEN1_AIR && ac.vz == (int32_t)d.motion.air_accel && ac.vy == ACR_GEN1_GRAVITY);
        for (int i = 0; i < 30; ++i) acr_gen1_ac_step(&ac, &d, k_fwd);
        EXPECT(ac.vz == (int32_t)d.motion.air_max);
        ac.vz = 9000;
        acr_gen1_ac_step(&ac, &d, k_fwd);
        EXPECT(ac.vz == (int32_t)d.motion.air_max);
        ac.vz = 9000;
        acr_gen1_ac_step(&ac, &d, k_none);
        EXPECT(ac.vz == (int32_t)d.motion.air_max - (int32_t)d.motion.air_accel / 4);
    }

    /* Energy: a quarter of the cost below the red zone; running dry locks the
     * booster until the bar is full, refilling at half speed meanwhile. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    ac.energy = 9000;                            /* above the red zone (8000) */
    ac.state = ACR_GEN1_AIR; ac.y = -100000;
    acr_gen1_ac_step(&ac, &d, k_up);
    EXPECT(ac.energy == 9000 - 400);
    ac.energy = 7000;
    acr_gen1_ac_step(&ac, &d, k_up);
    EXPECT(ac.energy == 7000 - 100);
    ac.energy = 50;
    acr_gen1_ac_step(&ac, &d, k_up);
    EXPECT(ac.energy == 0 && ac.drained);
    ac.state = ACR_GEN1_GROUND; ac.y = 0; ac.vx = ac.vy = ac.vz = 0;
    acr_gen1_ac_step(&ac, &d, k_none);
    EXPECT(ac.energy == 100);                    /* standing refill 200, halved */
    ac.energy = 27950;
    acr_gen1_ac_step(&ac, &d, k_up);             /* still locked: no rise; goes over */
    EXPECT(ac.state == ACR_GEN1_GROUND && ac.drained && ac.energy == 28050);
    acr_gen1_ac_step(&ac, &d, k_none);           /* full: back to the capacity, unlocked */
    EXPECT(ac.drained == 0 && ac.energy == 28000);

    /* The refill goes over the capacity for a tick, then comes back. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    ac.energy = 27950;
    acr_gen1_ac_step(&ac, &d, k_none);
    EXPECT(ac.energy == 28150);
    acr_gen1_ac_step(&ac, &d, k_none);
    EXPECT(ac.energy == 28000);

    /* Falling (not boosting) refills at the standing rate; walking at the moving rate. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    ac.energy = 10000; ac.state = ACR_GEN1_AIR; ac.y = -100000;
    acr_gen1_ac_step(&ac, &d, k_none);
    EXPECT(ac.energy == 10200);
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    ac.energy = 10000;
    acr_gen1_ac_step(&ac, &d, k_fwd);
    EXPECT(ac.energy == 10100);

    /* A tap shorter than a step (released and pressed again between steps). */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    acr_gen1_ac_step(&ac, &d, k_fwd);
    for (int i = 0; i < 12; ++i) acr_gen1_ac_step(&ac, &d, k_boost);
    const acr_gen1_input tap = { 1, 0, 0, 1, 1, 0, 0 };
    acr_gen1_ac_step(&ac, &d, tap);
    EXPECT(ac.state == ACR_GEN1_AIR);

    /* Crouched in the cannon stance: no walking, the crouch turn rates, and
     * the slide loses an eighth a tick, stopping below 512. */
    {
        acr_gen1_ac_def dc = make_def();
        dc.motion.crouch_turn_accel = 3; dc.motion.crouch_turn_max = 28;
        acr_gen1_ac_spawn(&ac, &dc, 0, 0, 0);
        ac.vz = 4000;
        const acr_gen1_input kneel = { 1, 1, 0, 0, 0, 0, 1 };
        acr_gen1_ac_step(&ac, &dc, kneel);
        EXPECT(ac.vz == 3500 && ac.z == 3500 && ac.turn_rate == 3);
        for (int i = 0; i < 30; ++i) acr_gen1_ac_step(&ac, &dc, kneel);
        EXPECT(ac.vz == 0 && ac.turn_rate == 28 && ac.anim == ACR_GEN1_ANIM_IDLE);
    }

    /* 60 steps a second runs exactly the game's ticks, 22 a second: the same
     * script gives the same state after every tick, and the shown position
     * always lies between the tick before and the latest. */
    {
        acr_gen1_ac_def d60 = make_def();
        d60.step_rate = 60; d60.gen1_rate = 22;
        acr_gen1_ac a22, a60;
        acr_gen1_ac_spawn(&a22, &d, 0, 0, 0);
        acr_gen1_ac_spawn(&a60, &d60, 0, 0, 0);
        int tick = 0, step = 0, match = 1, between = 1, ticks_in_second = 0;
        while (tick < 140 && step < 1000) {
            /* Steps before a tick already hold that tick's input, as a
             * player's would. */
            const int n = acr_gen1_ac_step(&a60, &d60, script(tick));
            ++step;
            if (step == 60) ticks_in_second = tick + n;
            for (int k = 0; k < n; ++k) {
                acr_gen1_ac_step(&a22, &d, script(tick));
                ++tick;
            }
            if (n && !same(&a22, &a60)) { if (match) printf("differs at tick %d\n", tick); match = 0; }
            const int32_t lo = a60.px < a60.x ? a60.px : a60.x, hi = a60.px < a60.x ? a60.x : a60.px;
            if (a60.sx < lo || a60.sx > hi) between = 0;
        }
        EXPECT(match);
        EXPECT(between);
        EXPECT(ticks_in_second == 22);
    }

    /* At 60 steps a second a steady ground boost is shown moving evenly:
     * 75 a tick is 27-28 a step. */
    {
        acr_gen1_ac_def d60 = make_def();
        d60.step_rate = 60; d60.gen1_rate = 22;
        acr_gen1_ac_spawn(&ac, &d60, 0, 0, 0);
        for (int i = 0; i < 3; ++i) acr_gen1_ac_step(&ac, &d60, k_fwd);
        for (int i = 0; i < 60; ++i) acr_gen1_ac_step(&ac, &d60, k_boost);
        int even = 1;
        for (int i = 0; i < 30; ++i) {
            const int32_t before = ac.sz;
            acr_gen1_ac_step(&ac, &d60, k_boost);
            const int32_t moved = (ac.sz - before) / 64;
            if (moved < 27 || moved > 28) even = 0;
        }
        EXPECT(even);
    }

    /* Moved outside the rules (a blade lunge): the shown position moves too. */
    acr_gen1_ac_spawn(&ac, &d, 0, 0, 0);
    acr_gen1_ac_shift(&ac, 640, -64);
    EXPECT(ac.x == 640 && ac.px == 640 && ac.sx == 640 && ac.z == -64 && ac.sz == -64);
    acr_gen1_ac_set_yaw(&ac, 4100);
    EXPECT(ac.yaw == 4 && ac.pyaw == 4 && ac.syaw == 4);

    /* View tilt: L2 up to the limit, held there; L2+R2 back to level. */
    {
        acr_gen1_ac t;
        acr_gen1_ac_spawn(&t, &d, 0, 0, 0);
        acr_gen1_input look = { 0, 0, 0, 0, 0, 1, 0 };
        acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == ACR_GEN1_PITCH_RATE);
        for (int k = 0; k < 40; ++k) acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == ACR_GEN1_PITCH_MAX);
        look.look = 0;
        acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == ACR_GEN1_PITCH_MAX);
        look.look = 2;
        for (int k = 0; k < 5; ++k) acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == 0);
        look.look = -1;
        for (int k = 0; k < 40; ++k) acr_gen1_ac_step(&t, &d, look);
        EXPECT(t.pitch == -ACR_GEN1_PITCH_MAX);
    }
    /* Modern controls: the boost toggle is the only ground boost, at
     * CRUISE_PCT of walking pace and CRUISE_DRAIN_PCT of the cost; the quick
     * boost is the full top speed, briefly. */
    {
        const acr_gen1_ac_def d = make_def();
        acr_gen1_ac c, g;
        acr_gen1_ac_spawn(&c, &d, 0, 0, 0);
        acr_gen1_ac_spawn(&g, &d, 0, 0, 0);
        acr_gen1_input walk = k_fwd, cruise = k_fwd, boost = k_boost;
        cruise.cruise = 1;
        cruise.modern = 1;
        for (int k = 0; k < 6; ++k) { acr_gen1_ac_step(&c, &d, walk); acr_gen1_ac_step(&g, &d, walk); }
        const int32_t e0 = c.energy;
        for (int k = 0; k < 40; ++k) { acr_gen1_ac_step(&c, &d, cruise); acr_gen1_ac_step(&g, &d, boost); }
        const int64_t vc = (int64_t)c.vx * c.vx + (int64_t)c.vz * c.vz, vg = (int64_t)g.vx * g.vx + (int64_t)g.vz * g.vz;
        const int64_t top = acr_gen1_cruise_top(&d), pace = acr_gen1_walk_pace(&d);
        EXPECT(pace == 548 * 64 / 4 / 4 && top == pace * ACR_GEN1_CRUISE_PCT / 100);
        EXPECT(c.state == ACR_GEN1_GROUND && vc <= top * top + 2 && vc >= (top - 2) * (top - 2));
        EXPECT(g.state == ACR_GEN1_GROUND && vg > vc);                   /* never beats a real ground boost */
        EXPECT(c.energy < e0 && e0 - c.energy < (28000 - g.energy));
        EXPECT(e0 - c.energy == 40 * ((d.stats.rise_drain >> 1) * ACR_GEN1_CRUISE_DRAIN_PCT / 100));
        /* Four legs and caterpillars: the heavy rate. */
        acr_gen1_ac_def h = d;
        h.cruise_pct = ACR_GEN1_CRUISE_HEAVY_PCT;
        EXPECT(acr_gen1_cruise_top(&h) == pace * ACR_GEN1_CRUISE_HEAVY_PCT / 100);
        /* Standing still the toggle never jumps, but still drains. */
        acr_gen1_ac s;
        acr_gen1_ac_spawn(&s, &d, 0, 0, 0);
        acr_gen1_input stand = k_none;
        stand.cruise = 1;
        stand.modern = 1;
        const int32_t es = s.energy;
        for (int k = 0; k < 10; ++k) acr_gen1_ac_step(&s, &d, stand);
        EXPECT(s.state == ACR_GEN1_GROUND && s.vx == 0 && s.vz == 0);
        EXPECT(es - s.energy == 10 * ((d.stats.rise_drain >> 1) * ACR_GEN1_CRUISE_DRAIN_PCT / 100));
        /* In the air (falling, not rising) the toggle drains at 110 % of its
         * ground rate, never refills, and the fall is 10 % slower. */
        {
            acr_gen1_ac fa, fo;
            acr_gen1_ac_spawn(&fa, &d, 0, 0, 0);
            acr_gen1_ac_spawn(&fo, &d, 0, 0, 0);
            fa.state = fo.state = ACR_GEN1_AIR;
            fa.y = fo.y = -5000000;
            fa.energy = fo.energy = 20000;
            acr_gen1_input on = k_none, off = k_none;
            on.modern = off.modern = 1;
            on.cruise = 1;
            const int32_t ea = fa.energy, eo = fo.energy;
            for (int k = 0; k < 10; ++k) { acr_gen1_ac_step(&fa, &d, on); acr_gen1_ac_step(&fo, &d, off); }
            EXPECT(fa.state == ACR_GEN1_AIR && fo.state == ACR_GEN1_AIR);
            EXPECT(ea - fa.energy == 10 * ((d.stats.rise_drain >> 1) * ACR_GEN1_CRUISE_DRAIN_PCT / 100
                                           * ACR_GEN1_CRUISE_AIR_DRAIN_PCT / 100));
            EXPECT(fo.energy > eo);                                  /* toggle off: the air refills */
            EXPECT(fa.vy == 10 * (ACR_GEN1_GRAVITY * ACR_GEN1_CRUISE_AIR_FALL_PCT / 100) && fo.vy == 10 * ACR_GEN1_GRAVITY);
            for (int k = 0; k < 60; ++k) { acr_gen1_ac_step(&fa, &d, on); acr_gen1_ac_step(&fo, &d, off); }
            EXPECT(fa.vy == ACR_GEN1_FALL_MAX * ACR_GEN1_CRUISE_AIR_FALL_PCT / 100 && fo.vy == ACR_GEN1_FALL_MAX);
        }
        /* Cross with SP-MODERN: never a ground boost. Toggle off: a plain
         * jump with no rise and no cost; toggle on: jump and rise. */
        {
            acr_gen1_ac w;
            acr_gen1_ac_spawn(&w, &d, 0, 0, 0);
            for (int k = 0; k < 6; ++k) acr_gen1_ac_step(&w, &d, k_fwd);
            acr_gen1_input mb = k_boost;
            mb.modern = 1;
            acr_gen1_ac_step(&w, &d, mb);
            EXPECT(w.state != ACR_GEN1_GROUND || (w.vx == 0 && w.vz == 0));   /* no ground boost */
            acr_gen1_ac j, r;
            acr_gen1_ac_spawn(&j, &d, 0, 0, 0);
            acr_gen1_ac_spawn(&r, &d, 0, 0, 0);
            acr_gen1_input jump = k_none, rise = k_none;
            jump.boost = rise.boost = 1;
            jump.modern = rise.modern = 1;
            rise.cruise = 1;
            int32_t top_j = 0, top_r = 0;
            const int32_t ej = j.energy;
            for (int k = 0; k < 60; ++k) {
                acr_gen1_ac_step(&j, &d, jump);
                acr_gen1_ac_step(&r, &d, rise);
                if (-j.y > top_j) top_j = -j.y;
                if (-r.y > top_r) top_r = -r.y;
            }
            EXPECT(top_j > 0 && top_r > top_j * 2);                 /* jumped; the toggle rises far higher */
            EXPECT(j.energy >= ej);                                  /* the plain jump costs nothing */
            acr_gen1_ac dj;                                          /* drained: still jumps */
            acr_gen1_ac_spawn(&dj, &d, 0, 0, 0);
            dj.energy = 0; dj.drained = 1;
            for (int k = 0; k < 12; ++k) acr_gen1_ac_step(&dj, &d, jump);
            EXPECT(dj.state == ACR_GEN1_AIR || dj.y < 0);
            /* Weaker than the original: half the take-off, 60 % lift, 125 % cost. */
            {
                acr_gen1_ac o, n;
                acr_gen1_ac_spawn(&o, &d, 0, 0, 0);
                acr_gen1_ac_spawn(&n, &d, 0, 0, 0);
                while (o.state != ACR_GEN1_AIR) { acr_gen1_ac_step(&o, &d, k_boost); acr_gen1_ac_step(&n, &d, rise); }
                EXPECT(n.state == ACR_GEN1_AIR);
                const int32_t eo = o.energy, en = n.energy;
                for (int k = 0; k < 10; ++k) { acr_gen1_ac_step(&o, &d, k_boost); acr_gen1_ac_step(&n, &d, rise); }
                EXPECT(en - n.energy == 10 * (d.stats.rise_drain * ACR_GEN1_MODERN_RISE_EN_PCT / 100));
                EXPECT(eo - o.energy == 10 * d.stats.rise_drain);
                EXPECT(n.y > o.y);                                   /* lower */
                acr_gen1_ac jo, jn;                                  /* the take-off */
                acr_gen1_ac_spawn(&jo, &d, 0, 0, 0);
                acr_gen1_ac_spawn(&jn, &d, 0, 0, 0);
                acr_gen1_ac_step(&jo, &d, k_boost);
                acr_gen1_ac_step(&jn, &d, jump);
                EXPECT(jn.vy == jo.vy * ACR_GEN1_MODERN_JUMP_PCT / 100);
            }
            /* Caterpillars rise with the toggle off too. */
            acr_gen1_ac_def t = d;
            t.no_boost_hop = 1;
            acr_gen1_ac tr;
            acr_gen1_ac_spawn(&tr, &t, 0, 0, 0);
            int32_t top_t = 0;
            for (int k = 0; k < 60; ++k) { acr_gen1_ac_step(&tr, &t, jump); if (-tr.y > top_t) top_t = -tr.y; }
            EXPECT(top_t > top_j * 2);
            /* Cruising caterpillars lift off at once, keeping their speed. */
            {
                acr_gen1_ac cr;
                acr_gen1_ac_spawn(&cr, &t, 0, 0, 0);
                acr_gen1_input go = k_fwd;
                go.modern = go.cruise = 1;
                for (int k = 0; k < 20; ++k) acr_gen1_ac_step(&cr, &t, go);
                const int64_t v0 = (int64_t)cr.vx * cr.vx + (int64_t)cr.vz * cr.vz;
                EXPECT(cr.state == ACR_GEN1_GROUND && v0 > 0);
                acr_gen1_input up = go;
                up.boost = 1;
                acr_gen1_ac_step(&cr, &t, up);
                const int64_t v1 = (int64_t)cr.vx * cr.vx + (int64_t)cr.vz * cr.vz;
                EXPECT(cr.state == ACR_GEN1_AIR && v1 > 0);
                for (int k = 0; k < 10; ++k) acr_gen1_ac_step(&cr, &t, up);
                EXPECT(cr.y < 0 && (cr.vx || cr.vz));                /* rising and still moving */
            }
        }
        /* Quick boost from a stand: the ground top speed at once, a few
         * ticks, then a slide. It costs, at once, what a normal boost would
         * to cover the distance; three chain, then a cool-down. */
        acr_gen1_ac q;
        acr_gen1_ac_spawn(&q, &d, 0, 0, 0);
        acr_gen1_input qb = k_fwd;
        qb.quick = 1;
        const int32_t cost = acr_gen1_quick_cost(&d, 0);
        EXPECT(cost > ACR_GEN1_QUICK_TICKS * (d.stats.rise_drain >> 1));   /* more than the burst's own ticks: the ramp-up */
        const int32_t eq = q.energy;
        acr_gen1_ac_step(&q, &d, qb);
        const int64_t v1 = (int64_t)q.vx * q.vx + (int64_t)q.vz * q.vz, bm = d.motion.boost_max;
        EXPECT(q.state == ACR_GEN1_GROUND && v1 >= (bm - 2) * (bm - 2) && v1 <= bm * bm + 2);
        EXPECT(q.energy == eq - cost && q.quick_ticks == ACR_GEN1_QUICK_TICKS - 1 && q.quick_cool == 0);
        for (int k = 0; k < ACR_GEN1_QUICK_TICKS - 1; ++k) acr_gen1_ac_step(&q, &d, k_fwd);
        EXPECT(q.quick_ticks == 0 && q.energy == eq - cost);      /* no drain or refill in the burst */
        acr_gen1_ac_step(&q, &d, qb);                              /* second */
        EXPECT(q.quick_ticks == ACR_GEN1_QUICK_TICKS - 1 && q.quick_chain == 2);
        acr_gen1_ac_step(&q, &d, qb);                              /* mid-burst: waits */
        for (int k = 0; k < ACR_GEN1_QUICK_TICKS - 2; ++k) acr_gen1_ac_step(&q, &d, k_fwd);
        acr_gen1_ac_step(&q, &d, k_fwd);                           /* the waiting press: third */
        EXPECT(q.quick_ticks == ACR_GEN1_QUICK_TICKS - 1 && q.quick_cool == ACR_GEN1_QUICK_COOL - 0 && q.quick_chain == 0);
        for (int k = 0; k < ACR_GEN1_QUICK_TICKS; ++k) acr_gen1_ac_step(&q, &d, k_fwd);
        acr_gen1_ac_step(&q, &d, qb);                              /* fourth: cooling down */
        EXPECT(q.quick_ticks == 0);
        /* Run dry: no quick boost, and the toggle cuts out. */
        acr_gen1_ac_spawn(&q, &d, 0, 0, 0);
        for (int k = 0; k < 6; ++k) acr_gen1_ac_step(&q, &d, k_fwd);
        q.energy = 0; q.drained = 1;
        acr_gen1_ac_step(&q, &d, qb);
        EXPECT(q.quick_ticks == 0);
        acr_gen1_input dry = k_fwd;
        dry.cruise = 1;
        dry.modern = 1;
        for (int k = 0; k < 30; ++k) acr_gen1_ac_step(&q, &d, dry);
        EXPECT(q.drained && q.vx == 0 && q.vz == 0 && q.energy > 0);     /* walking again, recharging */
        acr_gen1_ac_spawn(&q, &d, 0, 0, 0);
        acr_gen1_ac_step(&q, &d, (acr_gen1_input){ .quick = 1 });  /* no direction: nothing */
        EXPECT(q.vx == 0 && q.vz == 0 && q.quick_cool == 0);
    }
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 sim: all passed\n");
    return 0;
}
