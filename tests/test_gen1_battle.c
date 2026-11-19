/* Gen 1 damage (checked against the game) and the fight stepping, against the
 * VS engine as mapped in the AC1Recomp fork (docs/mapping/battle-system.md). */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "acr_gen1_battle.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_ac_def def;
static const acr_gen1_input none[2];

static acr_gen1_weapon rifle(void) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_GUN;
    strcpy(w.name, "RIFLE");
    w.attack = 218; w.ammo = 200; w.range = 6100;
    w.lock_ms = 1; w.box_w = 240; w.box_h = 240; w.inner_w = 200; w.inner_h = 200; w.lock_value = 60;
    w.interval = 5; w.hold = 13; w.speed = 700; w.shots = 1;
    return w;
}

static acr_gen1_weapon small_missile(void) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_MISSILE;
    strcpy(w.name, "MISSILE");
    w.attack = 830; w.ammo = 40; w.range = 8000;
    w.box_w = 200; w.box_h = 200; w.inner_w = 160; w.inner_h = 160;
    w.interval = 10; w.lock_value = 128; w.lock_ticks = (256 - 128) / 8; w.max_locks = 4;
    w.volley_a = 1; w.ignition = 1;
    w.flight = (acr_gen1_flight){ 50, 60, 80, 350, 110, 1024, 0 };
    return w;
}

static acr_gen1_weapon blade(void) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_BLADE;
    strcpy(w.name, "LS-2001");
    w.attack = 738; w.energy = 1; w.range = ACR_GEN1_BLADE_REACH; w.charge_drain = 2050;
    return w;
}

static void setup(acr_gen1_battle *b, int32_t dist) {
    memset(b, 0, sizeof *b);
    b->step_ms = 45;
    for (int i = 0; i < 2; ++i) {
        acr_gen1_fighter *fi = &b->f[i];
        fi->def = &def;
        fi->intercept_shot = -1;
        acr_gen1_ac_spawn(&fi->ac, &def, 0, i ? dist * 64 : 0, i ? 2048 : 0);
        fi->ap = fi->ap_max = 8384;
        fi->shell_factor = 27;
        fi->energy_factor = 32;
    }
}

static int shots_alive(const acr_gen1_battle *b) {
    int n = 0;
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) n += b->shot[k].active;
    return n;
}

int main(void) {
    /* Factor from defense: the default AC's shell defense 1,604 -> 27. */
    EXPECT(acr_gen1_damage_factor(1604, 0, 0) == 27);
    EXPECT(acr_gen1_damage_factor(100000, 0, 0) >= 1);
    /* Measured in the game: rifle (attack 218) against factors 1..96. */
    static const int f[] = { 1, 10, 20, 26, 27, 28, 30, 45, 64, 96 };
    static const int ap[] = { 2, 22, 45, 58, 60, 63, 68, 102, 145, 218 };
    for (int i = 0; i < 10; ++i) EXPECT(acr_gen1_hit_ap(218, (uint8_t)f[i]) == ap[i]);
    EXPECT(acr_gen1_hit_ap(830, 27) == 233);        /* missile, measured */
    EXPECT(acr_gen1_hit_ap(738, 32) == 246);        /* LS-2001 blade on the default AC, measured */
    /* Knockback level: the blade (impact 11) on stability 38 tops out at 5. */
    EXPECT(acr_gen1_knockback_level(11, 38) == 5);
    EXPECT(acr_gen1_knockback_level(3, 38) == 3);
    EXPECT(acr_gen1_knockback_level(0, 38) == 0);

    memset(&def, 0, sizeof def);
    def.stats.energy_capacity = 28000;
    def.stats.thrust = 32; def.stats.thrust_80 = 25; def.stats.thrust_120 = 38;
    def.stats.turn_accel = 22; def.stats.turn_speed = 41;
    acr_gen1_motion_from_stats(&def.stats, &def.motion);
    const acr_gen1_actions fire[2] = { { 1, 0, 0 }, { 0, 0, 0 } };
    const acr_gen1_actions idle[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
    const acr_gen1_actions swing[2] = { { 0, 0, 1 }, { 0, 0, 0 } };

    /* Two ACs 3,000 apart, facing each other: the rifle locks, then hits. */
    static acr_gen1_battle b;
    setup(&b, 3000);
    for (int i = 0; i < 2; ++i) { b.f[i].weapons = 1; b.f[i].weapon[0] = rifle(); b.f[i].ammo[0] = 200; }
    EXPECT(acr_gen1_in_lock_box(&b, 0, &b.f[0].weapon[0]));
    /* View tilt (L2/R2): a target high above locks only when looking up. */
    {
        const int32_t y = b.f[1].ac.y;
        b.f[1].ac.y = -2000 * 64;                       /* 2,000 up, 3,000 ahead */
        EXPECT(!acr_gen1_in_lock_box(&b, 0, &b.f[0].weapon[0]));
        b.f[0].ac.pitch = 700;                          /* ~62 degrees up: 28 past it, outside 21 */
        EXPECT(!acr_gen1_in_lock_box(&b, 0, &b.f[0].weapon[0]));
        b.f[0].ac.pitch = 384;                          /* ~34 degrees: on it */
        EXPECT(acr_gen1_in_lock_box(&b, 0, &b.f[0].weapon[0]));
        /* Aiming (presentation): in the box, x = the angle up to it (- = up),
         * the hand weapon's left / right on z; out of it, the view tilt. */
        int16_t aim[3];
        b.f[0].weapon[0].aim_table = 0;
        EXPECT(acr_gen1_aim(&b, 0, aim) == 0);
        EXPECT(aim[0] < -300 && aim[0] > -400 && aim[1] == 0 && aim[2] == 0);   /* ~34 degrees up */
        b.f[0].ac.pitch = 700;
        EXPECT(acr_gen1_aim(&b, 0, aim) == 0 && aim[0] == -700);
        b.f[0].weapon[0].aim_table = 255;
        EXPECT(acr_gen1_aim(&b, 0, aim) == -1);
        b.f[0].weapon[0].aim_table = 0;
        b.f[0].ac.pitch = 0;
        b.f[1].ac.y = y;
    }
    {
        /* Off to the right: + on z (hand weapons), on y with aim_y. */
        int16_t aim[3];
        const int32_t x = b.f[1].ac.x;
        b.f[1].ac.x += (b.f[1].ac.z > b.f[0].ac.z ? 1 : -1) * 600 * 64;
        const int t = acr_gen1_aim(&b, 0, aim);
        EXPECT(t == 0 && aim[0] == 0 && aim[1] == 0 && aim[2] != 0);
        const int16_t side = aim[2];
        b.f[0].weapon[0].aim_y = 1;
        acr_gen1_aim(&b, 0, aim);
        EXPECT(aim[1] == side && aim[2] == 0);
        b.f[0].weapon[0].aim_y = 0;
        b.f[1].ac.x = x;
    }
    int steps = 0;
    while (b.f[1].ap == 8384 && steps < 200) { acr_gen1_battle_step(&b, none, fire); ++steps; }
    EXPECT(b.f[1].ap == 8384 - 60);
    EXPECT(b.f[0].hit_flash > 0);
    /* Cadence: held fire, one shot every 13 ticks (the WG-RF35's hold;
     * its refire wait of 5 ends first), as measured. */
    {
        int fired_at[3] = { 0, 0, 0 }, n = 0;
        uint16_t ammo = b.f[0].ammo[0];
        for (int t = 1; t <= 40 && n < 3; ++t) {
            acr_gen1_battle_step(&b, none, fire);
            if (b.f[0].ammo[0] != ammo) { fired_at[n++] = t; ammo = b.f[0].ammo[0]; }
        }
        EXPECT(n == 3 && fired_at[1] - fired_at[0] == 13 && fired_at[2] - fired_at[1] == 13);
    }

    /* Gun lock (VS 0x800800E4): a target standing still locks the tick it is
     * in the inner box; a moving one takes longer, the shots go ahead of it
     * and hit; leaving the inner box drops the lock at once, though the
     * sight (outer box) still sees it. */
    {
        static acr_gen1_battle g;
        setup(&g, 4000);
        for (int i = 0; i < 2; ++i) { g.f[i].weapons = 1; g.f[i].weapon[0] = rifle(); g.f[i].ammo[0] = 200; }
        acr_gen1_battle_step(&g, none, idle);
        EXPECT(g.f[0].locked);
        /* Sliding sideways at about 75 a tick. */
        setup(&g, 4000);
        for (int i = 0; i < 2; ++i) { g.f[i].weapons = 1; g.f[i].weapon[0] = rifle(); g.f[i].ammo[0] = 200; }
        int locked_at = 0;
        for (int t = 1; t <= 20 && !locked_at; ++t) {
            g.f[1].ac.vx = 100 * 64; g.f[1].ac.vz = 0;
            acr_gen1_battle_step(&g, none, idle);
            if (g.f[0].locked) locked_at = t;
        }
        EXPECT(locked_at >= 3 && locked_at <= 12);
        /* Held on it, the lock aims further ahead each 4 ticks: to the side
         * the target is going (+x), where it will be when the shot gets there. */
        for (int t = 0; t < 8; ++t) {
            g.f[1].ac.vx = 100 * 64; g.f[1].ac.vz = 0;
            acr_gen1_battle_step(&g, none, idle);
        }
        {
            const double bearing = atan2((double)g.f[1].ac.x / 64, 4000.0) * 4096 / 6.2831853;
            const int32_t lead = g.f[0].lead_yaw >= 2048 ? g.f[0].lead_yaw - 4096 : g.f[0].lead_yaw;
            EXPECT(g.f[0].locked && lead > bearing + 40);
        }
        /* Fire: the shot goes straight and meets the moving target. */
        const int32_t ap = g.f[1].ap;
        for (int t = 0; t < 12 && g.f[1].ap == ap; ++t) {
            g.f[1].ac.vx = 100 * 64; g.f[1].ac.vz = 0;
            acr_gen1_battle_step(&g, none, t ? idle : fire);
        }
        EXPECT(g.f[1].ap < ap);
        /* 220 off the facing: inside the outer box (240), outside the inner (200). */
        setup(&g, 4000);
        for (int i = 0; i < 2; ++i) { g.f[i].weapons = 1; g.f[i].weapon[0] = rifle(); g.f[i].ammo[0] = 200; }
        acr_gen1_battle_step(&g, none, idle);
        EXPECT(g.f[0].locked);
        g.f[0].ac.yaw = (uint16_t)(4096 - 220);
        acr_gen1_battle_step(&g, none, idle);
        EXPECT(!g.f[0].locked && acr_gen1_in_lock_box(&g, 0, &g.f[0].weapon[0]));
    }

    /* At 60 steps a second a rifle shot moves 700 x 22 / 60 = 256 units a
     * step: the same speed per second as the original. */
    memset(b.shot, 0, sizeof b.shot);
    b.step_rate = 60; b.gen1_rate = 22;
    b.f[0].cooldown = 0; b.f[0].hold_left = 0;
    b.f[1].ac.x = 50000 * 64;                         /* out of the way */
    acr_gen1_battle_step(&b, none, fire);
    int found = 0;
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k)
        if (b.shot[k].active) { found = 1; EXPECT(b.shot[k].vz / 64 == 256); }
    EXPECT(found);
    /* Two shots per trigger (fire pattern 1) take two rounds. */
    memset(b.shot, 0, sizeof b.shot);
    b.f[0].cooldown = 0; b.f[0].hold_left = 0;
    b.f[0].weapon[0].shots = 2;
    {
        const uint16_t ammo = b.f[0].ammo[0];
        acr_gen1_battle_step(&b, none, fire);
        EXPECT(shots_alive(&b) == 2 && b.f[0].ammo[0] == ammo - 2);
    }
    b.f[0].weapon[0].shots = 1;

    /* Blade: its own button, not in the weapon cycle. One hit = 246 on the
     * default AC; from 650 and closer the arc hits twice (492), at 700
     * once, from 800 not at all (measured in the recomp). */
    {
        static const int dist[] = { 300, 400, 500, 550, 600, 650, 700, 800 };
        static const int hits[] = { 2, 2, 2, 2, 2, 2, 1, 0 };
        for (int d = 0; d < 16; ++d) {
            static acr_gen1_battle bb;
            setup(&bb, dist[d & 7]);
            if (d >= 8) { bb.step_rate = 60; bb.gen1_rate = 22; bb.step_ms = 16; }   /* the same at 60 steps */
            acr_gen1_fighter *me = &bb.f[0];
            me->weapons = 2;
            me->weapon[0] = rifle(); me->ammo[0] = 200;
            me->weapon[1] = blade();
            acr_gen1_battle_step(&bb, none, swing);
            EXPECT(me->swing > 0 && me->ac.energy == 28000 - 2050);
            for (int k = 0; k < 120; ++k) acr_gen1_battle_step(&bb, none, idle);
            EXPECT(me->swing == 0);
            EXPECT(bb.f[1].ap == 8384 - 246 * hits[d & 7]);
            if (bb.f[1].ap != 8384 - 246 * hits[d & 7])
                printf("  blade at %d (%s): %d hit(s)\n", dist[d & 7], d >= 8 ? "60/s" : "22/s", (8384 - bb.f[1].ap) / 246);
        }
        static acr_gen1_battle bb;
        setup(&bb, 3000);
        acr_gen1_fighter *me = &bb.f[0];
        me->weapons = 2;
        me->weapon[0] = rifle(); me->ammo[0] = 200;
        me->weapon[1] = blade();
        const acr_gen1_actions cycle[2] = { { 0, 1, 0 }, { 0, 0, 0 } };
        acr_gen1_battle_step(&bb, none, cycle);
        EXPECT(me->current == 0);                       /* only the gun to cycle to */
        /* Nothing in reach: the full lunge, 516 straight ahead. */
        acr_gen1_battle_step(&bb, none, swing);
        for (int k = 0; k < 40; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(me->ac.z / 64 >= 510 && me->ac.z / 64 <= 522 && me->ac.x == 0);
        /* Not enough energy: no swing. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        me->ac.energy = 2050;
        acr_gen1_battle_step(&bb, none, swing);
        EXPECT(me->swing == 0);
        /* Steering: a target 30 degrees right turns the AC toward it at its
         * top turn rate (38 a tick) until the lunge. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 1000 * 64, 1732 * 64, 2048);
        acr_gen1_battle_step(&bb, none, swing);
        for (int k = 0; k < 40; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(me->ac.yaw == def.motion.turn_max * ACR_GEN1_BLADE_STEER_TICKS && me->ac.x > 0);
        /* Far to the side (70 degrees): no turn. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 2750 * 64, 1000 * 64, 2048);
        acr_gen1_battle_step(&bb, none, swing);
        for (int k = 0; k < 40; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(me->ac.yaw == 0);
        /* Behind: a swing hits nothing. */
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 0, 400 * 64, 2048);
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 2048);
        const int32_t before = bb.f[1].ap;
        acr_gen1_battle_step(&bb, none, swing);
        for (int k = 0; k < 40; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(bb.f[1].ap == before);
    }

    /* Cannon stance: a two-legged AC crouches first, four legs need the
     * ground, caterpillars and Human PLUS fire freely. */
    setup(&b, 3000);
    b.f[0].weapons = 1; b.f[0].weapon[0] = rifle(); b.f[0].ammo[0] = 200;
    b.f[0].weapon[0].stance = 1;
    b.f[0].legs = ACR_GEN1_LEGS_TWO;
    acr_gen1_battle_step(&b, none, fire);
    EXPECT(shots_alive(&b) == 0 && b.f[0].kneeling);
    for (int k = 0; k < 12; ++k) acr_gen1_battle_step(&b, none, fire);
    EXPECT(b.f[0].ammo[0] < 200);
    b.f[0].legs = ACR_GEN1_LEGS_FOUR;
    b.f[0].ac.state = ACR_GEN1_AIR;
    EXPECT(!acr_gen1_stance_ok(&b.f[0]));
    b.f[0].legs = ACR_GEN1_LEGS_TRACKS;
    EXPECT(acr_gen1_stance_ok(&b.f[0]));
    b.f[0].legs = ACR_GEN1_LEGS_FOUR;
    b.f[0].human_plus = 1;
    EXPECT(acr_gen1_stance_ok(&b.f[0]));

    /* Missiles: the cursor reaches the target, then (256 - 128) / 8 = 16
     * ticks make a lock; the volley leaves 4 ticks after fire. The game
     * launched 0.93 s (about 20 ticks) after the target entered the box. */
    {
        static acr_gen1_battle mb;
        setup(&mb, 3000);
        mb.f[0].weapons = 1; mb.f[0].weapon[0] = small_missile(); mb.f[0].ammo[0] = 40;
        int t = 0;
        while (mb.f[0].ammo[0] == 40 && t < 60) { acr_gen1_battle_step(&mb, none, fire); ++t; }
        EXPECT(t >= 18 && t <= 24);
        EXPECT(mb.f[0].ammo[0] == 39 && shots_alive(&mb) == 1);
        /* It flies to the target and hits for 233. */
        for (int k = 0; k < 40 && shots_alive(&mb); ++k) acr_gen1_battle_step(&mb, none, idle);
        EXPECT(mb.f[1].ap == 8384 - 233);
        /* Held off fire, the locks stack up to the FCS / weapon cap, and one
         * press sends a volley per lock. */
        setup(&mb, 3000);
        mb.f[0].weapons = 1; mb.f[0].weapon[0] = small_missile(); mb.f[0].ammo[0] = 40;
        for (int k = 0; k < 120; ++k) acr_gen1_battle_step(&mb, none, idle);
        EXPECT(mb.f[0].locks == 4);
        for (int k = 0; k < 20; ++k) acr_gen1_battle_step(&mb, none, k == 0 ? fire : idle);
        EXPECT(mb.f[0].ammo[0] == 36 && mb.f[0].locks == 0);
        /* A target well off to the side never starts a lock. */
        setup(&mb, 3000);
        mb.f[0].weapons = 1; mb.f[0].weapon[0] = small_missile(); mb.f[0].ammo[0] = 40;
        mb.f[1].ac.x = 2500 * 64;
        for (int k = 0; k < 60; ++k) acr_gen1_battle_step(&mb, none, fire);
        EXPECT(mb.f[0].ammo[0] == 40 && mb.f[0].locks == 0);
        /* A missile that passes its target and leaves the cone gives up. */
        setup(&mb, 3000);
        mb.f[0].weapons = 1; mb.f[0].weapon[0] = small_missile(); mb.f[0].ammo[0] = 40;
        while (mb.f[0].ammo[0] == 40) acr_gen1_battle_step(&mb, none, fire);
        mb.f[1].ac.z = -3000 * 64;                    /* the target is suddenly behind */
        for (int k = 0; k < 6; ++k) acr_gen1_battle_step(&mb, none, idle);
        int gave_up = 0;
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) if (mb.shot[k].active && (mb.shot[k].flags & 2)) gave_up = 1;
        EXPECT(gave_up);
        /* SP-JAM: locks on a jammer break (5% a tick), so they rarely stack. */
        setup(&mb, 3000);
        mb.f[0].weapons = 1; mb.f[0].weapon[0] = small_missile(); mb.f[0].ammo[0] = 40;
        mb.f[1].jammer = 1;
        int broke = 0;
        for (int k = 0; k < 400; ++k) {
            const uint8_t l = mb.f[0].locks;
            acr_gen1_battle_step(&mb, none, idle);
            if (mb.f[0].locks < l) ++broke;
        }
        EXPECT(broke > 0);
        /* The interceptor: with chance 128 / 128 it reacts every tick and
         * shoots down an incoming missile now and then. */
        int downed = 0;
        for (int run = 0; run < 20; ++run) {
            setup(&mb, 4000);
            mb.rng = (uint32_t)run + 1;
            static acr_gen1_ac_def tough;
            tough = def;
            tough.stats.intercept_chance = 128;
            tough.stats.intercept_angle = 128;
            mb.f[1].def = &tough;
            mb.f[0].weapons = 1; mb.f[0].weapon[0] = small_missile(); mb.f[0].ammo[0] = 40;
            while (mb.f[0].ammo[0] == 40) acr_gen1_battle_step(&mb, none, fire);
            for (int k = 0; k < 60 && shots_alive(&mb); ++k) acr_gen1_battle_step(&mb, none, idle);
            if (mb.f[1].ap == 8384) ++downed;
        }
        EXPECT(downed > 0 && downed < 20);
    }

    /* Round result: knock-out, double knock-out, time up on AP. */
    setup(&b, 3000);
    EXPECT(acr_gen1_battle_result(&b, 0) == 0);
    EXPECT(acr_gen1_battle_result(&b, 1) == 3);
    b.f[1].ap = 100;
    EXPECT(acr_gen1_battle_result(&b, 1) == 1);
    b.f[0].ap = 0;
    EXPECT(acr_gen1_battle_result(&b, 0) == 2);
    b.f[1].ap = 0;
    EXPECT(acr_gen1_battle_result(&b, 0) == 3);

    /* Camera swing (VS 0x8008774C): toward half the target's angle at 20 a
     * tick while it is in the sight, 3/4 at 64 while a missile flies at it,
     * back to 0 otherwise. */
    {
        static acr_gen1_battle cb;
        setup(&cb, 3000);
        cb.f[0].last_missile = cb.f[1].last_missile = -1;
        cb.f[0].weapon[0] = rifle();
        cb.f[0].weapons = 1;
        cb.f[0].ammo[0] = 200;
        acr_gen1_ac_set_yaw(&cb.f[0].ac, 60);              /* target 60 to the left (bearing -60) */
        acr_gen1_actions none_act[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
        acr_gen1_battle_step(&cb, none, none_act);
        EXPECT(cb.f[0].cam_yaw == -20 && cb.f[0].cam_yaw_prev == 0);
        for (int t = 0; t < 5; ++t) acr_gen1_battle_step(&cb, none, none_act);
        EXPECT(cb.f[0].cam_yaw == -30 && cb.f[0].cam_pitch == 0);
        cb.f[1].ap = 0;                                    /* gone: back to 0 */
        acr_gen1_battle_step(&cb, none, none_act);
        EXPECT(cb.f[0].cam_yaw == -10);
        cb.f[1].ap = 8384;
        cb.shot[5].active = 1; cb.shot[5].missile = 1; cb.shot[5].owner = 0;
        cb.shot[5].y = -100000 * 64; cb.shot[5].flight.life = 1000;
        cb.f[0].last_missile = 5;
        acr_gen1_ac_set_yaw(&cb.f[0].ac, 2048 + 600);      /* facing away: not in the sight */
        acr_gen1_battle_step(&cb, none, none_act);
        EXPECT(cb.f[0].cam_yaw == 54);                     /* toward 3/4 of 1448, 64 a tick */
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 battle: all passed (first hit after %d steps)\n", steps);
    return 0;
}
