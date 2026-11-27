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
    w.interval = 5; w.hold = 7; w.speed = 700; w.shots = 1;   /* 7 + 5 + 1 = 13 ticks */
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
    const acr_gen1_actions fire[2] = { { 1, 0, 0, 0 }, { 0, 0, 0, 0 } };
    const acr_gen1_actions idle[2] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
    const acr_gen1_actions swing[2] = { { 0, 0, 1, 0 }, { 0, 0, 0, 0 } };

    /* Two ACs 3,000 apart, facing each other: the rifle locks, then hits. */
    static acr_gen1_battle b;
    setup(&b, 3000);
    for (int i = 0; i < 2; ++i) { b.f[i].weapons = 1; b.f[i].weapon[0] = rifle(); b.f[i].ammo[0] = 200; }
    EXPECT(acr_gen1_in_lock_box(&b, 0, &b.f[0].weapon[0]));
    {   /* Lock feel pass (docs/feel/lockon.md). Box axes: w bounds the yaw,
         * h the pitch. A target ~15 degrees to the side (~170 units). */
        acr_gen1_weapon *w = &b.f[0].weapon[0];
        const int32_t x = b.f[1].ac.x;
        b.f[1].ac.x += 800 * 64;
        w->box_w = 100; w->box_h = 300;
        EXPECT(!acr_gen1_in_lock_box(&b, 0, w));
        w->box_w = 300; w->box_h = 100;
        EXPECT(acr_gen1_in_lock_box(&b, 0, w));
        b.f[1].ac.x = x;
        *w = rifle();
        /* Range: the sweep's flat distance against lock_range. */
        const int32_t z = b.f[1].ac.z, z0 = b.f[0].ac.z;
        w->lock_range = 6100;
        b.f[1].ac.z = z0 + (z > z0 ? 1 : -1) * 6000 * 64;
        EXPECT(acr_gen1_in_lock_box(&b, 0, w));
        b.f[1].ac.z = z0 + (z > z0 ? 1 : -1) * 6200 * 64;
        EXPECT(!acr_gen1_in_lock_box(&b, 0, w));
        b.f[1].ac.z = z;
        *w = rifle();
    }
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
        const acr_gen1_actions cycle[2] = { { 0, 1, 0, 0 }, { 0, 0, 0, 0 } };
        acr_gen1_battle_step(&bb, none, cycle);
        EXPECT(me->current == 0);                       /* only the gun to cycle to */
        /* Empty weapons: skipped by the switch; one that runs dry stays
         * selected until the pilot switches; with nothing left it stays. */
        {
            static acr_gen1_battle eb;
            setup(&eb, 3000);
            acr_gen1_fighter *ef = &eb.f[0];
            ef->weapons = 3;
            ef->weapon[0] = rifle(); ef->ammo[0] = 200;
            ef->weapon[1] = small_missile(); ef->ammo[1] = 0;
            ef->weapon[2] = rifle(); ef->ammo[2] = 5;
            acr_gen1_actions sw[2] = { { 0 }, { 0 } }, still[2] = { { 0 }, { 0 } };
            sw[0].next_weapon = 1;
            acr_gen1_battle_step(&eb, none, sw);
            EXPECT(ef->current == 2);                   /* the empty missile is skipped */
            ef->ammo[2] = 0;
            acr_gen1_battle_step(&eb, none, still);
            EXPECT(ef->current == 2);                   /* ran dry: no automatic swap */
            acr_gen1_battle_step(&eb, none, sw);
            EXPECT(ef->current == 0);                   /* switched away to the rifle */
            acr_gen1_battle_step(&eb, none, sw);
            EXPECT(ef->current == 0);                   /* the empty ones can't be picked again */
            ef->ammo[0] = 0;
            acr_gen1_battle_step(&eb, none, sw);
            EXPECT(ef->current == 0);                   /* nothing left: it stays */
        }
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
        /* Air slash (VS 0x8007F264): started in the air it flies at the
         * target and hits with the blade's attack x 3. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 0, 600 * 64, 2048);
        bb.f[1].ap = 8384;
        me->ac.state = ACR_GEN1_AIR;
        me->ac.y = -200 * 64;
        me->ac.energy = 28000;
        acr_gen1_battle_step(&bb, none, swing);
        EXPECT(me->swing > 0 && me->air_swing);
        for (int k = 0; k < 60; ++k) acr_gen1_battle_step(&bb, none, idle);
        {
            const int32_t one = acr_gen1_hit_ap(738 * ACR_GEN1_AIR_SLASH_ATTACK_X, 32);
            EXPECT(bb.f[1].ap < 8384 && (8384 - bb.f[1].ap) % one == 0);
            if (bb.f[1].ap >= 8384 || (8384 - bb.f[1].ap) % one) printf("  air slash: took %d (one hit %d)\n", 8384 - bb.f[1].ap, one);
        }
        EXPECT(!me->air_swing && me->swing == 0);
        /* A stunning hit ends the victim's own swing (its hit state
         * replaces the swing state): two ACs swinging face to face, the
         * first to cut wins. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 0, 400 * 64, 2048);
        me->ac.energy = bb.f[1].ac.energy = 28000;
        me->ap = bb.f[1].ap = 8384;
        bb.f[1].weapons = 1; bb.f[1].weapon[0] = blade(); bb.f[1].current = 0;
        bb.f[1].weapon[0].impact = 11; me->weapon[1].impact = 11;   /* LS-2001's impact: a stunning hit */
        static acr_gen1_ac_def steady_def;
        steady_def = def;
        steady_def.stats.stability = 38;
        me->def = bb.f[1].def = &steady_def;
        const acr_gen1_actions both[2] = { { 0, 0, 1, 0 }, { 0, 0, 1, 0 } };
        acr_gen1_battle_step(&bb, none, both);
        EXPECT(me->swing > 0 && bb.f[1].swing > 0);
        for (int k = 0; k < 60; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(bb.f[1].ap < 8384 && me->ap == 8384);
        if (!(bb.f[1].ap < 8384 && me->ap == 8384)) printf("  face-off: me %d them %d stag %d/%d\n", me->ap, bb.f[1].ap, me->stagger, bb.f[1].stagger);
        bb.f[1].weapons = 0;
        me->def = bb.f[1].def = &def;
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
        {   /* the locking beep (sound 47) ran while locking, and stops at the cap */
            const uint8_t tones = mb.f[0].tones;
            EXPECT(tones > 4);
            for (int k = 0; k < 10; ++k) acr_gen1_battle_step(&mb, none, idle);
            EXPECT(mb.f[0].tones == tones);
        }
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
        acr_gen1_actions none_act[2] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
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

    /* Four ACs in two teams (co-op): targets are enemies only, shots and
     * blades hit teammates too, the round goes by team. */
    {
        static const acr_gen1_input none4[ACR_GEN1_MAX_FIGHTERS];
        static const acr_gen1_actions none_act4[ACR_GEN1_MAX_FIGHTERS];
        static acr_gen1_battle q;
        /* 0 and 1 (team 0) face +z; enemies 2 and 3 (team 1) face them. */
        const int32_t pos[4][2] = { { 0, 0 }, { 0, 1200 }, { 0, 3000 }, { 3000, 3000 } };
        #define SETUP4() do { \
            memset(&q, 0, sizeof q); q.step_ms = 45; q.count = 4; \
            for (int i = 0; i < 4; ++i) { \
                acr_gen1_fighter *fi = &q.f[i]; \
                fi->def = &def; fi->intercept_shot = -1; fi->swing_target = -1; fi->last_missile = -1; \
                acr_gen1_ac_spawn(&fi->ac, &def, pos[i][0] * 64, pos[i][1] * 64, i < 2 ? 0 : 2048); \
                fi->ap = fi->ap_max = 8384; fi->shell_factor = 27; fi->energy_factor = 32; \
                fi->team = (uint8_t)(i >= 2); \
                fi->weapon[0] = rifle(); fi->weapon[1] = small_missile(); fi->weapon[2] = blade(); \
                fi->weapons = 3; for (int k = 0; k < 3; ++k) fi->ammo[k] = fi->weapon[k].ammo; \
            } \
            acr_gen1_battle_retarget(&q); \
        } while (0)
        SETUP4();
        EXPECT(acr_gen1_battle_fighters(&q) == 4);
        EXPECT(acr_gen1_battle_team(&q, 1) == 0 && acr_gen1_battle_team(&q, 3) == 1);
        /* 0 looks past its teammate (in the box) at enemy 2. */
        EXPECT(acr_gen1_battle_target(&q, 0) == 2);
        EXPECT(acr_gen1_battle_target(&q, 1) == 2);
        EXPECT(acr_gen1_battle_target(&q, 2) == 1);           /* the nearest in its box */
        for (int i = 0; i < 4; ++i) EXPECT(acr_gen1_battle_team(&q, acr_gen1_battle_target(&q, i)) != acr_gen1_battle_team(&q, i));
        /* The user's lock rules. Starting a lock with two enemies side by
         * side: the closer. */
        SETUP4();
        acr_gen1_ac_spawn(&q.f[1].ac, &def, -3000 * 64, 0, 0);
        acr_gen1_ac_spawn(&q.f[2].ac, &def, 150 * 64, 3000 * 64, 2048);
        acr_gen1_ac_spawn(&q.f[3].ac, &def, -150 * 64, 2400 * 64, 2048);
        q.targeted = 0;
        acr_gen1_battle_retarget(&q);
        EXPECT(acr_gen1_battle_target(&q, 0) == 3);
        /* Switch target: the next one in the sight, then round again. */
        acr_gen1_actions sw4[ACR_GEN1_MAX_FIGHTERS] = { { 0 } };
        sw4[0].switch_target = 1;
        acr_gen1_battle_retarget_acts(&q, sw4);
        EXPECT(acr_gen1_battle_target(&q, 0) == 2);
        acr_gen1_battle_retarget_acts(&q, sw4);
        EXPECT(acr_gen1_battle_target(&q, 0) == 3);
        acr_gen1_battle_retarget_acts(&q, none_act4);
        EXPECT(acr_gen1_battle_target(&q, 0) == 3);
        /* One gun only: weapon switch swaps targets too (not with two weapons). */
        acr_gen1_actions nx4[ACR_GEN1_MAX_FIGHTERS] = { { 0 } };
        nx4[0].next_weapon = 1;
        acr_gen1_battle_retarget_acts(&q, nx4);
        EXPECT(acr_gen1_battle_target(&q, 0) == 3);
        q.f[0].weapon[1] = blade();
        acr_gen1_battle_retarget_acts(&q, nx4);
        EXPECT(acr_gen1_battle_target(&q, 0) == 2);
        q.f[0].weapon[1] = small_missile();
        acr_gen1_battle_retarget_acts(&q, sw4);
        EXPECT(acr_gen1_battle_target(&q, 0) == 3);
        /* Crossing: locked on 2, enemy 3 passes in front of it; the lock stays on 2. */
        acr_gen1_battle_retarget_acts(&q, sw4);
        EXPECT(acr_gen1_battle_target(&q, 0) == 2);
        for (int x = 1500; x >= -1500; x -= 250) {
            acr_gen1_ac_spawn(&q.f[3].ac, &def, x * 64, 1800 * 64, 2048);
            acr_gen1_battle_retarget(&q);
            EXPECT(acr_gen1_battle_target(&q, 0) == 2);
        }
        SETUP4();
        /* Friendly fire: 0's rifle shots run into teammate 1, standing in the way. */
        acr_gen1_actions fire4[ACR_GEN1_MAX_FIGHTERS] = { { 0 } };
        fire4[0].fire = 1;
        for (int t = 0; t < 40; ++t) acr_gen1_battle_step(&q, none4, fire4);
        EXPECT(q.f[1].ap < q.f[1].ap_max);
        EXPECT(q.f[2].ap == q.f[2].ap_max);
        EXPECT(q.f[0].hit_flash == 0);                       /* no "HIT" for hitting a friend */
        /* Teammate out of the way: the shots reach the enemy. */
        SETUP4();
        acr_gen1_ac_spawn(&q.f[1].ac, &def, -3000 * 64, 0, 0);
        for (int t = 0; t < 40; ++t) acr_gen1_battle_step(&q, none4, fire4);
        EXPECT(q.f[2].ap < q.f[2].ap_max);
        EXPECT(acr_gen1_battle_target(&q, 0) == 2);
        /* Missiles: launched at the target, they keep it. */
        SETUP4();
        acr_gen1_ac_spawn(&q.f[1].ac, &def, -3000 * 64, 0, 0);
        acr_gen1_actions next4[ACR_GEN1_MAX_FIGHTERS] = { { 0 } };
        next4[0].next_weapon = 1;
        acr_gen1_battle_step(&q, none4, next4);
        EXPECT(q.f[0].current == 1);
        int launched = 0, ok_target = 1;
        for (int t = 0; t < 200; ++t) {
            acr_gen1_battle_step(&q, none4, fire4);
            for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k)
                if (q.shot[k].active && q.shot[k].missile && q.shot[k].owner == 0) { ++launched; if (q.shot[k].target != 2) ok_target = 0; }
        }
        EXPECT(launched > 0 && ok_target);
        /* The blade seeks the enemy, not the teammate just ahead, but hits the teammate in its arc. */
        SETUP4();
        acr_gen1_ac_spawn(&q.f[1].ac, &def, 0, 300 * 64, 0);
        acr_gen1_ac_spawn(&q.f[2].ac, &def, 0, 2000 * 64, 2048);
        acr_gen1_actions slash4[ACR_GEN1_MAX_FIGHTERS] = { { 0 } };
        slash4[0].blade = 1;
        acr_gen1_battle_step(&q, none4, slash4);
        EXPECT(q.f[0].swing_target == 2);
        for (int t = 0; t < 40; ++t) acr_gen1_battle_step(&q, none4, none_act4);
        EXPECT(q.f[1].ap < q.f[1].ap_max);
        /* Teams: the round goes on while one of a team stands. */
        SETUP4();
        EXPECT(acr_gen1_battle_result(&q, 0) == 0);
        q.f[2].ap = 0;
        EXPECT(acr_gen1_battle_result(&q, 0) == 0);
        acr_gen1_battle_retarget(&q);
        EXPECT(acr_gen1_battle_target(&q, 0) == 3 && acr_gen1_battle_target(&q, 1) == 3);
        q.f[3].ap = 0;
        EXPECT(acr_gen1_battle_result(&q, 0) == 1);
        SETUP4();
        q.f[0].ap = 0;
        EXPECT(acr_gen1_battle_result(&q, 0) == 0);
        q.f[2].ap = 100;                                      /* time up: 8384 vs 8484 */
        EXPECT(acr_gen1_battle_result(&q, 1) == 2);
        /* Three ACs: two against one. */
        SETUP4();
        q.count = 3;
        acr_gen1_battle_retarget(&q);
        EXPECT(acr_gen1_battle_target(&q, 0) == 2 && acr_gen1_battle_target(&q, 2) == 1);
        q.f[2].ap = 0;
        EXPECT(acr_gen1_battle_result(&q, 0) == 1);
        /* SP-MODERN with teams: hard lock pins the picked enemy and holds it
         * when it leaves the sight, even with another enemy nearer. */
        SETUP4();
        acr_gen1_ac_spawn(&q.f[1].ac, &def, -3000 * 64, 0, 0);
        q.f[0].modern = 1;
        acr_gen1_actions hl4[ACR_GEN1_MAX_FIGHTERS] = { { 0 } };
        hl4[0].hard_lock = 1;
        acr_gen1_battle_step(&q, none4, hl4);
        EXPECT(q.f[0].hard_lock && acr_gen1_battle_target(&q, 0) == 2);
        acr_gen1_ac_spawn(&q.f[2].ac, &def, 3000 * 64, 0, 2048);            /* off to the side */
        acr_gen1_ac_spawn(&q.f[3].ac, &def, 0, -1500 * 64, 2048);           /* nearer, behind */
        for (int t = 0; t < 5; ++t) acr_gen1_battle_step(&q, none4, none_act4);
        EXPECT(q.f[0].hard_lock && acr_gen1_battle_target(&q, 0) == 2);
        EXPECT(q.f[0].look_yaw > 1024 - 16 && q.f[0].look_yaw < 1024 + 16);  /* the camera on 2 */
        q.f[2].ap = 0;                                                        /* down: the lock drops */
        for (int t = 0; t < 2; ++t) acr_gen1_battle_step(&q, none4, none_act4);
        EXPECT(!q.f[0].hard_lock && acr_gen1_battle_target(&q, 0) == 3);
        #undef SETUP4
    }
    /* Modern controls: a smaller sight that lags the facing, part-way lead,
     * slower missile locks, a dearer blade with a recovery. */
    {
        static acr_gen1_battle mb;
        acr_gen1_actions na[2] = { { 0 }, { 0 } };
        setup(&mb, 3000);
        mb.f[0].last_missile = mb.f[1].last_missile = -1;
        mb.f[0].weapons = 1; mb.f[0].weapon[0] = rifle(); mb.f[0].ammo[0] = 200;
        /* Target ~19 degrees off: inside the rifle's 21-degree box, outside 30 % of it. */
        acr_gen1_ac_set_yaw(&mb.f[0].ac, 216);
        EXPECT(acr_gen1_in_lock_box(&mb, 0, &mb.f[0].weapon[0]));
        mb.f[0].modern = 1;
        acr_gen1_battle_step(&mb, none, na);
        EXPECT(!acr_gen1_in_lock_box(&mb, 0, &mb.f[0].weapon[0]));
        /* Facing it: in the sight. A sudden turn away leaves the sight behind for a moment. */
        acr_gen1_ac_set_yaw(&mb.f[0].ac, 0); mb.f[0].look_yaw = 0;
        for (int t = 0; t < 10; ++t) acr_gen1_battle_step(&mb, none, na);
        EXPECT(mb.f[0].sight_yaw == 0 && !acr_gen1_in_lock_box(&mb, 0, &mb.f[0].weapon[0]));   /* no FCS without hard lock */
        mb.f[0].hard_lock = 1;
        EXPECT(acr_gen1_in_lock_box(&mb, 0, &mb.f[0].weapon[0]));
        mb.f[0].hard_lock = 0;
        acr_gen1_ac_set_yaw(&mb.f[0].ac, 600); mb.f[0].look_yaw = 600;
        acr_gen1_battle_step(&mb, none, na);
        /* The sight follows at the legs' turn rate, no faster. */
        EXPECT(mb.f[0].sight_yaw > 0 && mb.f[0].sight_yaw <= mb.f[0].def->motion.turn_max &&
               !acr_gen1_in_lock_box(&mb, 0, &mb.f[0].weapon[0]));   /* no FCS without hard lock */
        for (int t = 0; t < 40; ++t) acr_gen1_battle_step(&mb, none, na);
        EXPECT(mb.f[0].sight_yaw == 600);
        /* The free camera: the right stick spins it; the AC follows at its own turn rate. */
        {
            acr_gen1_input spin[2] = { { 0 }, { 0 } };
            spin[0].turn = 1;
            const uint16_t l0 = mb.f[0].look_yaw, y0 = mb.f[0].ac.yaw;
            acr_gen1_battle_step(&mb, spin, na);
            EXPECT(mb.f[0].look_yaw == ((l0 + ACR_GEN1_MODERN_CAM_RATE) & 4095));
            for (int t = 0; t < 4; ++t) acr_gen1_battle_step(&mb, spin, na);
            const int32_t cam_moved = ((int32_t)mb.f[0].look_yaw - l0) & 4095, ac_moved = ((int32_t)mb.f[0].ac.yaw - y0) & 4095;
            EXPECT(cam_moved == 5 * ACR_GEN1_MODERN_CAM_RATE && ac_moved > 0 && ac_moved < cam_moved);
            /* Forward on the stick with the camera a quarter turn right of the AC: the AC strafes right. */
            acr_gen1_ac_set_yaw(&mb.f[0].ac, 0);
            mb.f[0].look_yaw = 1024;
            acr_gen1_input fwd[2] = { { 0 }, { 0 } };
            fwd[0].move = 1;
            acr_gen1_battle_step(&mb, fwd, na);
            EXPECT(mb.f[0].used.move == 0 && mb.f[0].used.strafe == 1 && mb.f[0].used.turn == 1);
            acr_gen1_ac_set_yaw(&mb.f[0].ac, 0); mb.f[0].look_yaw = 0;
            for (int t = 0; t < 10; ++t) acr_gen1_battle_step(&mb, none, na);
        }
        /* Hard lock: the camera stays on the target wherever it is (here
         * right behind). The AC turns freely until it fires, then turns
         * round toward the target at its own rate. */
        acr_gen1_ac_set_yaw(&mb.f[0].ac, 2048); mb.f[0].look_yaw = 2048;
        acr_gen1_battle_step(&mb, none, na);
        na[0].hard_lock = 1;
        acr_gen1_battle_step(&mb, none, na);
        na[0].hard_lock = 0;
        EXPECT(mb.f[0].hard_lock && mb.f[0].look_yaw == 0);       /* the camera on the target at once */
        for (int t = 0; t < 20; ++t) acr_gen1_battle_step(&mb, none, na);
        EXPECT(mb.f[0].look_yaw == 0 && mb.f[0].hard_lock && mb.f[0].ac.yaw == 2048);   /* not firing: no aim */
        {
            acr_gen1_input spin[2] = { { 0 }, { 0 } };
            spin[0].turn = 1;
            for (int t = 0; t < 3; ++t) acr_gen1_battle_step(&mb, spin, na);
            EXPECT(mb.f[0].ac.yaw > 2048 && mb.f[0].look_yaw == 0);  /* the right stick turns the AC */
            acr_gen1_ac_set_yaw(&mb.f[0].ac, 2048);
        }
        {
            /* Firing: the AC snaps to the target; the FCS sight swings round
             * from the old facing at the legs' rate, and only then locks. */
            acr_gen1_actions hold[2] = { { 1 }, { 0 } };
            for (int k = 0; k < 100; ++k) acr_gen1_battle_step(&mb, none, na);   /* the sight settles behind */
            EXPECT(mb.f[0].sight_yaw == 2048 && mb.f[0].ac.yaw == 2048);
            acr_gen1_battle_step(&mb, none, hold);
            const int32_t tm = mb.f[0].def->motion.turn_max;
            const int32_t s1 = mb.f[0].sight_yaw >= 2048 ? (int32_t)mb.f[0].sight_yaw - 4096 : mb.f[0].sight_yaw;
            EXPECT(mb.f[0].ac.yaw == 0);                                  /* snapped */
            EXPECT(s1 != -2048 && (s1 > 0 ? 2048 - s1 : 2048 + s1) <= tm && !acr_gen1_in_lock_box(&mb, 0, &mb.f[0].weapon[0]));
            int t = 0;
            while (!acr_gen1_in_lock_box(&mb, 0, &mb.f[0].weapon[0]) && t < 200) { acr_gen1_battle_step(&mb, none, hold); ++t; }
            EXPECT(t > 2 && t < 200 && mb.f[0].hard_lock);              /* caught up after a while */
            /* Already facing it: the sight is there at once. */
            acr_gen1_battle_step(&mb, none, hold);
            EXPECT(acr_gen1_in_lock_box(&mb, 0, &mb.f[0].weapon[0]));
            /* Dodging (not firing): the AC turns to face its way at the legs' rate. */
            acr_gen1_input side[2] = { { 0 }, { 0 } };
            side[0].strafe = 1;
            acr_gen1_battle_step(&mb, side, na);
            const int32_t y = mb.f[0].ac.yaw >= 2048 ? (int32_t)mb.f[0].ac.yaw - 4096 : mb.f[0].ac.yaw;
            EXPECT(y > 0 && y <= tm);
            for (int k = 0; k < 100; ++k) acr_gen1_battle_step(&mb, side, na);
            EXPECT(mb.f[0].ac.yaw > 1024 - 64 && mb.f[0].ac.yaw < 1024 + 64 && mb.f[0].look_yaw == 0);
            /* Firing while strafing: faces the target, still strafing. */
            acr_gen1_battle_step(&mb, side, hold);
            EXPECT(mb.f[0].ac.yaw == 0 && mb.f[0].used.strafe == 1 && mb.f[0].used.move == 0);
            acr_gen1_ac_set_yaw(&mb.f[0].ac, 0);
        }
        /* Modern rockets: a third of the fire rate and slower flight, hard lock or not. */
        mb.f[0].weapon[0].straight = 1; mb.f[0].weapon[0].box_w = mb.f[0].weapon[0].box_h = 0;
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) mb.shot[k].active = 0;
        mb.f[0].cooldown = mb.f[0].hold_left = 0;
        acr_gen1_actions shoot[2] = { { 1 }, { 0 } };
        acr_gen1_battle_step(&mb, none, shoot);
        EXPECT(mb.f[0].hard_lock && mb.f[0].cooldown == 5 * ACR_GEN1_MODERN_ROCKET_ROF_PCT / 100 + 1);
        {
            int64_t v2 = 0;
            for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k)
                if (mb.shot[k].active && mb.shot[k].owner == 0)
                    v2 = (int64_t)mb.shot[k].vx * mb.shot[k].vx + (int64_t)mb.shot[k].vz * mb.shot[k].vz + (int64_t)mb.shot[k].vy * mb.shot[k].vy;
            const int64_t full = (int64_t)mb.f[0].weapon[0].speed * 64;
            const int64_t want = full * ACR_GEN1_MODERN_ROCKET_SPEED_PCT / 100;
            EXPECT(v2 > (want - want / 50) * (want - want / 50) && v2 < (want + want / 50) * (want + want / 50));
        }
        mb.f[0].hard_lock = 0;
        mb.f[0].cooldown = mb.f[0].hold_left = 0;
        acr_gen1_battle_step(&mb, none, shoot);
        EXPECT(mb.f[0].cooldown == 5 * ACR_GEN1_MODERN_ROCKET_ROF_PCT / 100 + 1);   /* without hard lock too */
        /* Spread and bloom: rapid fire spreads the shots, then settles; the
         * original controls get none. */
        {
            mb.f[0].weapon[0] = rifle(); mb.f[0].weapon[0].interval = 1; mb.f[0].weapon[0].hold = 0; mb.f[0].ammo[0] = 200;
            mb.f[0].hard_lock = 0; mb.f[0].bloom = 0; mb.f[0].cooldown = mb.f[0].hold_left = 0;
            acr_gen1_ac_set_yaw(&mb.f[0].ac, 0); mb.f[0].look_yaw = 0;
            acr_gen1_actions mg[2] = { { 1 }, { 0 } };
            int32_t lo = 4096, hi = -4096;
            for (int k = 0; k < 40; ++k) {
                for (int q = 0; q < ACR_GEN1_MAX_SHOTS; ++q) mb.shot[q].active = 0;
                acr_gen1_battle_step(&mb, none, mg);
                for (int q = 0; q < ACR_GEN1_MAX_SHOTS; ++q)
                    if (mb.shot[q].active && mb.shot[q].owner == 0) {
                        const int32_t sx = mb.shot[q].vx;
                        if (sx < lo) lo = sx;
                        if (sx > hi) hi = sx;
                    }
            }
            EXPECT(mb.f[0].bloom + 2 * ACR_GEN1_MODERN_BLOOM_DECAY >= ACR_GEN1_MODERN_BLOOM_MAX);   /* fires every other tick */
            EXPECT(hi > 0 && lo < 0);                                   /* shots go both ways off the line */
            for (int k = 0; k < 60; ++k) acr_gen1_battle_step(&mb, none, na);
            EXPECT(mb.f[0].bloom == 0);
            mb.f[0].modern = 0;
            for (int k = 0; k < 10; ++k) acr_gen1_battle_step(&mb, none, mg);
            EXPECT(mb.f[0].bloom == 0);
            mb.f[0].modern = 1;
        }
        mb.f[0].weapon[0] = rifle();
        /* Any distance, and it holds when the target moves far off. */
        mb.f[1].ac.z = mb.f[1].ac.pz = mb.f[1].ac.sz = 20000 * 64;
        na[0].hard_lock = 1;
        acr_gen1_battle_step(&mb, none, na);
        na[0].hard_lock = 0;
        EXPECT(mb.f[0].hard_lock);
        mb.f[1].ac.x = mb.f[1].ac.px = mb.f[1].ac.sx = 6000 * 64;
        acr_gen1_battle_step(&mb, none, na);
        EXPECT(mb.f[0].hard_lock);
        na[0].hard_lock = 1;                                      /* R3 again: off */
        acr_gen1_battle_step(&mb, none, na);
        na[0].hard_lock = 0;
        EXPECT(!mb.f[0].hard_lock);
        mb.f[1].ac.x = mb.f[1].ac.px = mb.f[1].ac.sx = 0;
        mb.f[1].ac.z = mb.f[1].ac.pz = mb.f[1].ac.sz = 3000 * 64;

        /* Missile locks take 40 % longer. */
        static acr_gen1_battle ml[2];
        int took[2] = { 0, 0 };
        for (int k = 0; k < 2; ++k) {
            setup(&ml[k], 3000);
            ml[k].f[0].last_missile = ml[k].f[1].last_missile = -1;
            ml[k].f[0].weapons = 1; ml[k].f[0].weapon[0] = small_missile(); ml[k].f[0].ammo[0] = 40;
            ml[k].f[0].modern = (uint8_t)k;
            ml[k].f[0].hard_lock = (uint8_t)k;                  /* modern: the FCS only with hard lock */
            for (int t = 0; t < 200 && ml[k].f[0].locks < 2; ++t) { acr_gen1_battle_step(&ml[k], none, na); ++took[k]; }
        }
        EXPECT(ml[0].f[0].locks == 2 && ml[1].f[0].locks == 2 && took[1] > took[0] * 13 / 10);

        /* Blade: 25 % more energy, and a miss leaves the AC standing longer than a hit. */
        static acr_gen1_battle bb;
        setup(&bb, 5000);                                           /* out of reach: a miss */
        bb.f[0].weapons = 1; bb.f[0].weapon[0] = blade();
        bb.f[0].modern = 1;
        const int32_t e0 = bb.f[0].ac.energy;
        acr_gen1_actions sw[2] = { { 0 }, { 0 } };
        sw[0].blade = 1;
        acr_gen1_battle_step(&bb, none, sw);
        EXPECT(e0 - bb.f[0].ac.energy >= 2050 * 125 / 100 - 600 && bb.f[0].swing);
        int steps_left = 0;
        while (bb.f[0].swing && steps_left < 100) { acr_gen1_battle_step(&bb, none, na); ++steps_left; }
        EXPECT(!bb.f[0].swing && bb.f[0].recover_ms > 200);
        const acr_gen1_input fwd_in[2] = { { 1, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0 } };
        const int32_t z0 = bb.f[0].ac.z;
        acr_gen1_battle_step(&bb, fwd_in, sw);                       /* held still, no new swing */
        EXPECT(bb.f[0].ac.z == z0 && !bb.f[0].swing);
        for (int t = 0; t < 10; ++t) acr_gen1_battle_step(&bb, none, na);
        EXPECT(bb.f[0].recover_ms == 0);
        setup(&bb, 400);                                            /* close: a hit */
        bb.f[0].weapons = 1; bb.f[0].weapon[0] = blade();
        bb.f[0].modern = 1;
        acr_gen1_battle_step(&bb, none, sw);
        while (bb.f[0].swing) acr_gen1_battle_step(&bb, none, na);
        EXPECT(bb.f[1].ap < bb.f[1].ap_max && bb.f[0].recover_ms > 0 && bb.f[0].recover_ms <= ACR_GEN1_MODERN_RECOVER_HIT_MS);
    }

    /* Blade waves (VS 0x8007EB48): LS-2001 sends one only with Human PLUS
     * and boost held; LS-1000W always sends one and has no melee hit; the
     * swing drops every lock when it ends. */
    {
        static acr_gen1_battle wb;
        acr_gen1_actions sw[2] = { { 0 }, { 0 } }, na2[2] = { { 0 }, { 0 } };
        sw[0].blade = 1;
        acr_gen1_input boost_in[2];
        memset(boost_in, 0, sizeof boost_in);
        boost_in[0].boost = 1;
        int waves;
        /* No Human PLUS: no wave. */
        setup(&wb, 3000);
        wb.f[0].weapons = 1; wb.f[0].weapon[0] = blade();
        acr_gen1_battle_step(&wb, boost_in, sw);
        waves = 0;
        for (int t = 0; t < 40 && wb.f[0].swing; ++t) {
            acr_gen1_battle_step(&wb, boost_in, na2);
            for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) waves += wb.shot[k].active && wb.shot[k].wave;
        }
        EXPECT(waves == 0);
        /* Human PLUS and boost: one wave, and it reaches a target 3,000 away. */
        setup(&wb, 3000);
        wb.f[0].weapons = 1; wb.f[0].weapon[0] = blade(); wb.f[0].human_plus = 1;
        acr_gen1_battle_step(&wb, boost_in, sw);
        int fired = 0;
        for (int t = 0; t < 60; ++t) {
            int now = 0;
            for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) now += wb.shot[k].active && wb.shot[k].wave;
            if (now > fired) fired = now;
            acr_gen1_battle_step(&wb, boost_in, na2);
        }
        EXPECT(fired == 1 && wb.f[1].ap < wb.f[1].ap_max);
        /* LS-1000W: a wave without Human PLUS or boost (reaching 3,000), and
         * up close only the wave hits (246), no melee hits on top. */
        for (int d = 0; d < 2; ++d) {
            setup(&wb, d ? 400 : 3000);
            wb.f[0].weapons = 1; wb.f[0].weapon[0] = blade(); strcpy(wb.f[0].weapon[0].name, "LS-1000W");
            acr_gen1_battle_step(&wb, none, sw);
            for (int t = 0; t < 60; ++t) acr_gen1_battle_step(&wb, none, na2);
            EXPECT(wb.f[1].ap_max - wb.f[1].ap == 246);
        }
        /* Locks are dropped when the swing ends. */
        setup(&wb, 5000);
        wb.f[0].weapons = 1; wb.f[0].weapon[0] = blade();
        acr_gen1_battle_step(&wb, none, sw);
        wb.f[0].locked = 1; wb.f[0].locks = 2;
        while (wb.f[0].swing) acr_gen1_battle_step(&wb, none, na2);
        EXPECT(!wb.f[0].locked && !wb.f[0].locks);
    }

    /* Refire (VS 0x80081034): a wait of n fires every n + 1 ticks, so the
     * WG-MGA1 (wait 1, no hold) fires every other tick, not every tick. */
    {
        static acr_gen1_battle rb;
        setup(&rb, 3000);
        rb.f[0].weapons = 1; rb.f[0].weapon[0] = rifle(); rb.f[0].ammo[0] = 200;
        rb.f[0].weapon[0].interval = 1; rb.f[0].weapon[0].hold = 0;
        for (int t = 0; t < 20; ++t) acr_gen1_battle_step(&rb, none, fire);
        EXPECT(rb.f[0].ammo[0] == 190);
    }
    /* Hit stagger: a rifle hit (impact 3) on stability 38 is level 3: the
     * target loses its controls for a while and is pushed back. */
    {
        static acr_gen1_ac_def steady;
        steady = def;
        steady.stats.stability = 38;
        static acr_gen1_battle sb;
        setup(&sb, 1500);
        sb.f[1].def = &steady;
        sb.f[0].weapons = 1; sb.f[0].weapon[0] = rifle(); sb.f[0].weapon[0].impact = 3; sb.f[0].ammo[0] = 200;
        const int32_t z0 = sb.f[1].ac.z;
        int k = 0;
        while (sb.f[1].ap == sb.f[1].ap_max && k++ < 200) acr_gen1_battle_step(&sb, none, fire);
        EXPECT(sb.f[1].ap < sb.f[1].ap_max);
        EXPECT(sb.f[1].stagger > 0 && sb.f[1].stagger <= ACR_GEN1_STAGGER_BASE + 3 * ACR_GEN1_STAGGER_PER_LEVEL);
        acr_gen1_battle_step(&sb, none, idle);
        EXPECT(sb.f[1].ac.z > z0);                              /* pushed away from the shooter */
        for (int t = 0; t < 30; ++t) acr_gen1_battle_step(&sb, none, idle);
        EXPECT(sb.f[1].stagger == 0);
        /* no impact: no stagger */
        setup(&sb, 1500);
        sb.f[1].def = &steady;
        sb.f[0].weapons = 1; sb.f[0].weapon[0] = rifle(); sb.f[0].ammo[0] = 200;
        k = 0;
        while (sb.f[1].ap == sb.f[1].ap_max && k++ < 200) acr_gen1_battle_step(&sb, none, fire);
        EXPECT(sb.f[1].ap < sb.f[1].ap_max && sb.f[1].stagger == 0);
    }
    /* Round over: shots still fly but do no damage; a destroyed AC in the
     * air falls instead of hanging there. */
    {
        static acr_gen1_battle ob;
        setup(&ob, 1500);
        ob.f[0].weapons = 1; ob.f[0].weapon[0] = rifle(); ob.f[0].ammo[0] = 200;
        acr_gen1_battle_step(&ob, none, fire);
        ob.over = 1;
        for (int t = 0; t < 60; ++t) acr_gen1_battle_step(&ob, none, idle);
        EXPECT(ob.f[1].ap == ob.f[1].ap_max);
        setup(&ob, 1500);
        ob.f[1].ac.y = -1500 * 64;
        ob.f[1].ac.state = ACR_GEN1_AIR;
        ob.f[1].ap = 0;
        for (int t = 0; t < 200; ++t) acr_gen1_battle_step(&ob, none, idle);
        EXPECT(ob.f[1].ac.y > -100 * 64);                       /* back on the floor (y 0) */
    }
    /* The vertical missile (flight kind 4): leaves upward and doesn't drop
     * before its motor lights. */
    {
        static acr_gen1_battle vb;
        setup(&vb, 4000);
        acr_gen1_weapon vm = small_missile();
        vm.flight = (acr_gen1_flight){ 120, 160, 60, 300, 90, 1024, 4 };
        vm.ignition = 20;
        vb.f[0].weapons = 1; vb.f[0].weapon[0] = vm; vb.f[0].ammo[0] = 40;
        int k = 0;
        while (!shots_alive(&vb) && k++ < 200) acr_gen1_battle_step(&vb, none, fire);
        EXPECT(shots_alive(&vb));
        int si = -1;
        for (int q = 0; q < ACR_GEN1_MAX_SHOTS; ++q) if (vb.shot[q].active) si = q;
        const int32_t y_launch = vb.shot[si].y;
        acr_gen1_battle_step(&vb, none, idle);
        EXPECT(vb.shot[si].vy < 0 && vb.shot[si].y < y_launch);  /* going up */
        int32_t top_y = vb.shot[si].y;
        for (int t = 0; t < 10; ++t) { acr_gen1_battle_step(&vb, none, idle); if (vb.shot[si].y < top_y) top_y = vb.shot[si].y; }
        EXPECT(vb.shot[si].active && top_y < y_launch - 300 * 64);
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 battle: all passed (first hit after %d steps)\n", steps);
    return 0;
}
