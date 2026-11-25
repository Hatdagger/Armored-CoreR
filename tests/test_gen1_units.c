/* Mission units in the Gen 1 battle: lock-on, hits and the unit damage rule
 * (mission engine 0x8007AEA4), destruction, and the MT brain read from
 * mission 6 (docs/research/gen1-stages.md, "Mission unit behaviour"). */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_battle.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_ac_def def;
static const acr_gen1_input none[2];
static const acr_gen1_actions idle[2];

static acr_gen1_weapon rifle(void) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_GUN;
    strcpy(w.name, "RIFLE");
    w.attack = 218; w.ammo = 200; w.range = 8500;
    w.lock_ms = 1; w.box_w = 240; w.box_h = 240; w.inner_w = 200; w.inner_h = 200; w.lock_value = 60;
    w.interval = 5; w.hold = 0; w.speed = 700; w.shots = 1;
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

/* The player at the origin facing +z with a rifle; the other AC far behind. */
static void setup(acr_gen1_battle *b) {
    memset(b, 0, sizeof *b);
    b->step_ms = 45;
    for (int i = 0; i < 2; ++i) {
        acr_gen1_fighter *f = &b->f[i];
        f->def = &def;
        f->intercept_shot = -1;
        f->target = -1;
        acr_gen1_ac_spawn(&f->ac, &def, 0, i ? -20000 * 64 : 0, 0);
        f->ap = f->ap_max = 8384;
        f->shell_factor = 27;
        f->energy_factor = 32;
    }
    b->f[0].weapons = 1;
    b->f[0].weapon[0] = rifle();
    b->f[0].ammo[0] = 200;
}

/* Map 6's biped cannon MT (record 7): AP 5,000, gun 200, n = 2, box 125 / 175. */
static acr_gen1_unit mt(int32_t x, int32_t z, uint16_t yaw, int brain) {
    acr_gen1_unit u;
    memset(&u, 0, sizeof u);
    u.active = 1;
    u.team = 1;
    u.brain = (uint8_t)brain;
    u.x = x * 64; u.y = 0; u.z = z * 64;
    u.yaw = yaw;
    u.ap = u.ap_max = 5000;
    u.half = 125; u.height = 175;
    u.energy_n = 2;
    u.attack = 200;
    return u;
}

int main(void) {
    memset(&def, 0, sizeof def);
    def.stats.energy_capacity = 28000;
    def.stats.thrust = 32; def.stats.thrust_80 = 25; def.stats.thrust_120 = 38;
    def.stats.turn_accel = 22; def.stats.turn_speed = 41;
    acr_gen1_motion_from_stats(&def.stats, &def.motion);

    /* The unit damage rule. */
    EXPECT(acr_gen1_unit_damage(200, 0, 2) == 200);
    EXPECT(acr_gen1_unit_damage(800, 1, 2) == 601);          /* 800 x 6 / 8 + 1 */
    EXPECT(acr_gen1_unit_damage(800, 1, -1) == 901);         /* a negative nibble: more */
    EXPECT(acr_gen1_unit_damage(0, 1, 2) == 0);

    static acr_gen1_battle b;
    acr_gen1_actions fire[2] = { { 0 } };
    fire[0].fire = 1;

    /* A unit ahead is the nearest enemy: locked, hit, damaged by the attack. */
    setup(&b);
    acr_gen1_unit u = mt(0, 3000, 2048, ACR_GEN1_BRAIN_NONE);
    EXPECT(acr_gen1_battle_add_unit(&b, &u) == 0);
    acr_gen1_battle_retarget(&b);
    EXPECT(acr_gen1_battle_target_any(&b, 0) == ACR_GEN1_UNIT(0));
    EXPECT(acr_gen1_battle_target(&b, 0) == -1);             /* not a fighter */
    int32_t mid[3], h = 0;
    EXPECT(acr_gen1_battle_target_at(&b, ACR_GEN1_UNIT(0), mid, &h) && mid[2] == 3000 && h == 174);
    EXPECT(acr_gen1_in_lock_box(&b, 0, &b.f[0].weapon[0]));
    int steps = 0;
    while (b.unit[0].ap == 5000 && steps < 100) { acr_gen1_battle_step(&b, none, fire); ++steps; }
    EXPECT(b.unit[0].ap == 5000 - 218);
    EXPECT(b.f[1].ap == 8384);

    /* Destroyed: the explosion's ticks, then gone; the target moves on. */
    b.unit[0].ap = 100;
    for (int t = 0; t < 60 && !b.unit[0].down; ++t) acr_gen1_battle_step(&b, none, fire);
    EXPECT(b.unit[0].down && b.unit[0].ap == 0);
    for (int t = 0; t < ACR_GEN1_UNIT_DOWN_TICKS + 2; ++t) acr_gen1_battle_step(&b, none, idle);
    EXPECT(!b.unit[0].active);
    EXPECT(acr_gen1_battle_target_any(&b, 0) == 1);

    /* A turret facing the player fires bursts of three at it: each hit is
     * the attack through the AC's shell factor, as an AC's gun. */
    setup(&b);
    u = mt(0, 3000, 2048, ACR_GEN1_BRAIN_TURRET);
    acr_gen1_battle_add_unit(&b, &u);
    acr_gen1_battle_retarget(&b);
    for (int t = 0; t < 600; ++t) acr_gen1_battle_step(&b, none, idle);
    const int32_t lost = 8384 - b.f[0].ap, per = acr_gen1_hit_ap(200, 27);
    EXPECT(lost > 0 && lost % per == 0);
    EXPECT(b.f[1].ap == 8384);                               /* its own side's AC: not aimed at */

    /* An unarmed unit never fires. */
    setup(&b);
    u = mt(0, 3000, 2048, ACR_GEN1_BRAIN_TURRET);
    u.attack = 0;
    acr_gen1_battle_add_unit(&b, &u);
    for (int t = 0; t < 600; ++t) acr_gen1_battle_step(&b, none, idle);
    EXPECT(b.f[0].ap == 8384);

    /* The MT: far (measure 2 x 4,000 > 2,500) it walks in; close it backs off. */
    setup(&b);
    u = mt(0, 4000, 0, ACR_GEN1_BRAIN_MT);                   /* facing away at first */
    acr_gen1_battle_add_unit(&b, &u);
    for (int t = 0; t < 300; ++t) acr_gen1_battle_step(&b, none, idle);
    EXPECT(b.unit[0].z / 64 < 2500);
    EXPECT(b.unit[0].target == 0);
    setup(&b);
    u = mt(0, 300, 2048, ACR_GEN1_BRAIN_MT);                 /* measure 600 < 1,000 */
    acr_gen1_battle_add_unit(&b, &u);
    for (int t = 0; t < 40; ++t) acr_gen1_battle_step(&b, none, idle);
    EXPECT(b.unit[0].z / 64 > 300);

    /* Walking by the motion set (map 6's MT, chunk 12 handler 2): state 1
     * starts with animation 1 (168 / 4 forward in 8 frames), then loops
     * walk 2 (646 / 4 in 24 frames, keys as on the disc). Facing +z. */
    {
        static const acr_gen1_root_key k_start[] = { { 8, 0, 0, -168 } };
        static const acr_gen1_root_key k_walk[] = { { 3, 0, 0, -27 }, { 6, 0, 0, -111 }, { 8, 0, 0, -223 },
                                                    { 18, 0, 0, -434 }, { 20, 0, 0, -546 }, { 24, 0, 0, -646 } };
        static const acr_gen1_root_key k_side[] = { { 24, 646, 0, 0 } };
        static acr_gen1_root_track tr[ACR_GEN1_UNIT_ANIMS];
        static const acr_gen1_root_track *motion[ACR_GEN1_UNIT_ANIMS];
        tr[1] = (acr_gen1_root_track){ 8, 1, k_start };
        tr[2] = (acr_gen1_root_track){ 24, 6, k_walk };
        tr[9] = (acr_gen1_root_track){ 24, 1, k_side };
        motion[1] = &tr[1]; motion[2] = &tr[2]; motion[9] = &tr[9];
        setup(&b);
        b.f[0].ac.z = 20000 * 64;                              /* ahead of it, far: it keeps walking in */
        b.f[1].ac.z = 20000 * 64;
        u = mt(0, 0, 0, ACR_GEN1_BRAIN_MT);
        u.attack = 0;
        u.motion = motion;
        acr_gen1_battle_add_unit(&b, &u);
        b.unit[0].state = ACR_GEN1_UNIT_AHEAD;
        b.unit[0].state_ticks = 250;
        for (int t = 0; t < 8; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.unit[0].anim == 1);
        EXPECT(b.unit[0].z / 64 == 42);                        /* 168 / 4 */
        for (int t = 0; t < 24; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.unit[0].anim == 2);
        EXPECT(b.unit[0].z / 64 == 42 + 162);                  /* + 646 / 4, rounded as the game */
        EXPECT(b.unit[0].x == 0);
        for (int t = 0; t < 24; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.unit[0].z / 64 == 42 + 2 * 162);
        /* walk to walk: straight into the new one, no start (state 4, walk 9: +x is its left) */
        b.unit[0].state = ACR_GEN1_UNIT_LEFT;
        for (int t = 0; t < 24; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.unit[0].anim == 9);
        EXPECT(b.unit[0].x / 64 == -162);                      /* facing +z, its left is -x */
    }

    /* A hit turns the MT's step around (event 5). */
    setup(&b);
    u = mt(0, 3000, 2048, ACR_GEN1_BRAIN_MT);
    acr_gen1_battle_add_unit(&b, &u);
    b.unit[0].state = ACR_GEN1_UNIT_AHEAD_L;
    b.unit[0].state_ticks = 200;
    while (b.unit[0].ap == 5000 && steps < 400) { acr_gen1_battle_step(&b, none, fire); ++steps; }
    EXPECT(b.unit[0].state == ACR_GEN1_UNIT_BACK_R);

    /* The same fight twice: the same result (netplay needs it). */
    static acr_gen1_battle r1, r2;
    for (int run = 0; run < 2; ++run) {
        acr_gen1_battle *r = run ? &r2 : &r1;
        setup(r);
        for (int k = 0; k < 6; ++k) {
            u = mt(-3000 + k * 1200, 2500 + (k & 1) * 900, 2048, k & 1 ? ACR_GEN1_BRAIN_MT : ACR_GEN1_BRAIN_TURRET);
            acr_gen1_battle_add_unit(r, &u);
        }
        acr_gen1_battle_retarget(r);
        for (int t = 0; t < 900; ++t) acr_gen1_battle_step(r, none, (t / 40) & 1 ? fire : idle);
    }
    EXPECT(!memcmp(&r1, &r2, sizeof r1));
    EXPECT(r1.f[0].ap < 8384);
    int hurt = 0;
    for (int k = 0; k < r1.units; ++k) hurt += r1.unit[k].ap < r1.unit[k].ap_max;
    EXPECT(hurt > 0);

    /* Blade: a press seeks the unit ahead, lunges, stops at its body and hits
     * it (energy: 738 x 6 / 8 + 1 = 554 a hit, up to two a swing). */
    setup(&b);
    b.f[0].weapons = 2;
    b.f[0].weapon[1] = blade();
    u = mt(150, 800, 2048, ACR_GEN1_BRAIN_NONE);
    acr_gen1_battle_add_unit(&b, &u);
    acr_gen1_battle_retarget(&b);
    acr_gen1_actions slash[2] = { { 0 } };
    slash[0].blade = 1;
    acr_gen1_battle_step(&b, none, slash);
    for (int t = 0; t < 40; ++t) acr_gen1_battle_step(&b, none, idle);
    EXPECT(b.unit[0].ap == 5000 - 554 || b.unit[0].ap == 5000 - 2 * 554);
    EXPECT(b.f[0].ac.yaw > 0 && b.f[0].ac.yaw < 512);             /* turned toward it */
    {   /* the lunge stopped short of its body */
        const int64_t dx = (b.unit[0].x - b.f[0].ac.x) / 64, dz = (b.unit[0].z - b.f[0].ac.z) / 64;
        EXPECT(dx * dx + dz * dz >= (int64_t)(125 + ACR_GEN1_HIT_RADIUS - 2) * (125 + ACR_GEN1_HIT_RADIUS - 2));
    }
    /* Out of reach: the swing misses. */
    setup(&b);
    b.f[0].weapons = 2;
    b.f[0].weapon[1] = blade();
    u = mt(0, 2500, 2048, ACR_GEN1_BRAIN_NONE);
    acr_gen1_battle_add_unit(&b, &u);
    acr_gen1_battle_step(&b, none, slash);
    for (int t = 0; t < 40; ++t) acr_gen1_battle_step(&b, none, idle);
    EXPECT(b.unit[0].ap == 5000);

    /* Bodies: an AC inside a unit is pushed out of its square. */
    setup(&b);
    u = mt(0, 100, 0, ACR_GEN1_BRAIN_NONE);
    acr_gen1_battle_add_unit(&b, &u);
    acr_gen1_battle_step(&b, none, idle);
    {
        const int64_t az = b.f[0].ac.z / 64, ax = b.f[0].ac.x / 64;
        EXPECT(az <= 100 - 125 - ACR_GEN1_HIT_RADIUS + 1 && ax == 0);   /* out the near side */
    }
    /* Corner contact pushes out diagonally; clear of it, nothing moves. */
    setup(&b);
    u = mt(150 + 125, 150 + 125, 0, ACR_GEN1_BRAIN_NONE);         /* its corner 150 / 150 from the AC */
    acr_gen1_battle_add_unit(&b, &u);
    acr_gen1_battle_step(&b, none, idle);
    EXPECT(b.f[0].ac.x == 0 && b.f[0].ac.z == 0);
    setup(&b);
    u = mt(40 + 125, 40 + 125, 0, ACR_GEN1_BRAIN_NONE);
    acr_gen1_battle_add_unit(&b, &u);
    acr_gen1_battle_step(&b, none, idle);
    {
        const int64_t ax = b.f[0].ac.x / 64, az = b.f[0].ac.z / 64;
        EXPECT(ax < 0 && az < 0);
        const int64_t ex = 40 - ax, ez = 40 - az;               /* to the corner */
        EXPECT(ex * ex + ez * ez >= (int64_t)(ACR_GEN1_HIT_RADIUS - 2) * (ACR_GEN1_HIT_RADIUS - 2));
    }
    /* A unit above the AC's head (or below its feet) doesn't touch it. */
    setup(&b);
    u = mt(0, 100, 0, ACR_GEN1_BRAIN_NONE);
    u.y = -400 * 64;
    acr_gen1_battle_add_unit(&b, &u);
    acr_gen1_battle_step(&b, none, idle);
    EXPECT(b.f[0].ac.z == 0);

    /* Missions: units keep their side standing; the win comes when the last
     * enemy target (AC or unit) is down. */
    setup(&b);
    b.count = 2;
    b.f[1].team = 1;
    u = mt(0, 3000, 2048, ACR_GEN1_BRAIN_NONE);
    acr_gen1_battle_add_unit(&b, &u);
    u = mt(0, 4000, 2048, ACR_GEN1_BRAIN_NONE);
    u.active = 0;                                               /* dormant: waits for its script */
    acr_gen1_battle_add_unit(&b, &u);
    EXPECT(acr_gen1_battle_result(&b, 0) == 0);
    b.f[1].ap = 0;
    EXPECT(acr_gen1_battle_result(&b, 0) == 0);                 /* the unit still stands */
    b.unit[0].ap = 0; b.unit[0].down = 1;
    EXPECT(acr_gen1_battle_result(&b, 0) == 1);
    b.f[0].ap = 0;
    EXPECT(acr_gen1_battle_result(&b, 0) == 3);
    /* A side of units only (no AC on it) still counts. */
    setup(&b);
    b.count = 1;
    u = mt(0, 3000, 2048, ACR_GEN1_BRAIN_NONE);
    acr_gen1_battle_add_unit(&b, &u);
    EXPECT(acr_gen1_battle_result(&b, 0) == 0);
    b.unit[0].ap = 0; b.unit[0].down = 1;
    EXPECT(acr_gen1_battle_result(&b, 0) == 1);

    /* Map 6's script: the five MTs (records 7-11) down open the deck; the
     * tank AC (4) joins and the big MT (6) climbs to the deck by cutscene
     * 3's points and joins;
     * the big MT down wins it, 100 ticks later with the tank AC down first
     * (bonus), 160 with it still up. Nothing else wins it. */
    for (int tank_first = 0; tank_first < 2; ++tank_first) {
        setup(&b);
        b.count = 2;
        b.f[1].team = 1;
        for (int r = 7; r <= 11; ++r) {
            u = mt(-4000 + 1000 * (r - 7), 6000, 2048, ACR_GEN1_BRAIN_NONE);
            u.record = (uint8_t)r;
            acr_gen1_battle_add_unit(&b, &u);
        }
        u = mt(1710, 340, 0, ACR_GEN1_BRAIN_NONE);
        u.record = 4; u.wait = (uint8_t)acr_gen1_mission_wait(6, 4); u.active = 0;
        acr_gen1_battle_add_unit(&b, &u);
        u = mt(300, 10000, 0, ACR_GEN1_BRAIN_NONE);
        u.record = 6; u.wait = (uint8_t)acr_gen1_mission_wait(6, 6); u.active = 0;
        u.y = 500 * 64; u.ap = u.ap_max = 18000;
        acr_gen1_battle_add_unit(&b, &u);
        acr_gen1_mission_start(&b, 6);
        EXPECT(b.unit[5].wait == ACR_GEN1_WAIT_HIDDEN && b.unit[6].wait == ACR_GEN1_WAIT_SHOWN);
        b.f[1].ap = 0;                                          /* the target AC doesn't decide it */
        for (int t = 0; t < 4; ++t) b.unit[t].down = 1, b.unit[t].ap = 0;
        for (int t = 0; t < 200; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(acr_gen1_battle_result(&b, 0) == 0);
        EXPECT(!b.unit[5].active && !b.unit[6].active);         /* one MT still stands */
        b.unit[4].down = 1; b.unit[4].ap = 0;
        for (int t = 0; t < 20; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.mission.phase == 1 && !b.unit[5].active);      /* the deck opening */
        for (int t = 0; t < 100 && b.mission.phase != -1; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.unit[5].active && !b.unit[5].wait);            /* the tank AC is in */
        /* cutscene 3's path: point 10 -> 11 (60 ticks from tick 50), -> 12 (34), put at 13 */
        for (int t = 0; t < 110; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(!b.unit[6].active && b.unit[6].x == -300 * 64 && b.unit[6].y == 500 * 64 && b.unit[6].z == 8000 * 64);
        for (int t = 0; t < 34; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.unit[6].y == -1200 * 64 && b.unit[6].z == 6500 * 64);
        for (int t = 0; t < 126; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(!b.unit[6].active && b.unit[6].z == 3500 * 64);  /* on the deck, the cutscene's last tick */
        acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.unit[6].active && b.unit[6].y == -500 * 64 && b.unit[6].yaw == 2048);   /* the big MT joins */
        EXPECT(acr_gen1_battle_result(&b, 0) == 0);
        if (tank_first) {
            b.unit[5].down = 1; b.unit[5].ap = 0;
            acr_gen1_battle_step(&b, none, idle);
            acr_gen1_battle_step(&b, none, idle);
            EXPECT(b.mission.flags & ACR_GEN1_MISSION_BONUS);
        }
        b.unit[6].down = 1; b.unit[6].ap = 0;
        int ticks = 0;
        while (acr_gen1_battle_result(&b, 0) == 0 && ticks < 400) { acr_gen1_battle_step(&b, none, idle); ++ticks; }
        EXPECT(acr_gen1_battle_result(&b, 0) == 1);
        EXPECT(tank_first ? ticks >= 95 && ticks <= 110 : ticks >= 155 && ticks <= 170);
        EXPECT(!!(b.mission.flags & ACR_GEN1_MISSION_BONUS) == tank_first);
        b.f[0].ap = 0;
        EXPECT(acr_gen1_battle_result(&b, 0) == 2);             /* the player down loses */
    }
    EXPECT(!acr_gen1_mission_has_script(5) && acr_gen1_mission_wait(5, 4) == ACR_GEN1_WAIT_NONE);

    /* Maps 2, 15 and 13: everything from record 4 down wins, the request
     * 29 ticks later (2, 13) or at once (15), the ending 100 after it; map 13 makes records 11-14 when fewer
     * than 8 are left. */
    for (int map = 0; map < 3; ++map) {
        const int id = map == 0 ? 2 : map == 1 ? 15 : 13;
        setup(&b);
        b.count = 2;
        b.f[1].team = 1;
        b.f[1].ap = 0;
        const int first = 4, last = id == 13 ? 14 : 9;
        for (int r = first; r <= last; ++r) {
            u = mt(-4000 + 500 * (r - first), 6000, 2048, ACR_GEN1_BRAIN_NONE);
            u.record = (uint8_t)r;
            u.wait = (uint8_t)acr_gen1_mission_wait(id, r);
            u.active = !u.wait;
            acr_gen1_battle_add_unit(&b, &u);
        }
        acr_gen1_mission_start(&b, id);
        EXPECT(b.mission.map == id + 1);
        if (id == 13) {
            EXPECT(b.unit[7].wait == ACR_GEN1_WAIT_HIDDEN && b.unit[10].wait == ACR_GEN1_WAIT_HIDDEN);
            for (int k = 0; k < 3; ++k) b.unit[k].down = 1, b.unit[k].ap = 0;      /* 8 left (4 waiting) */
            acr_gen1_battle_step(&b, none, idle);
            EXPECT(!b.unit[7].active);
            b.unit[3].down = 1; b.unit[3].ap = 0;                                  /* 7 left */
            acr_gen1_battle_step(&b, none, idle);
            EXPECT(b.unit[7].active && b.unit[10].active && !b.unit[7].wait);
        }
        for (int k = 0; k < b.units; ++k) b.unit[k].down = 1, b.unit[k].ap = 0;
        int ticks = 0;
        while (acr_gen1_battle_result(&b, 0) == 0 && ticks < 400) { acr_gen1_battle_step(&b, none, idle); ++ticks; }
        EXPECT(acr_gen1_battle_result(&b, 0) == 1);
        EXPECT(ticks == (id == 15 ? 101 : 130));      /* counted from the tick all are down */
    }

    /* Map 12: records 4-9 the targets, 10-12 the friendly MTs (the player's
     * side). The targets go for the friendlies too, the friendlies for the
     * targets; the friendlies don't keep the player's side up. Every
     * target down wins (the ending 100 ticks later); every friendly down
     * fails: 20 ticks, radio 59 (stand-in length), the ending 100 later. */
    EXPECT(acr_gen1_mission_team(12, 10) == 0 && acr_gen1_mission_team(12, 12) == 0);
    EXPECT(acr_gen1_mission_team(12, 9) == 1 && acr_gen1_mission_team(6, 10) == 1);
    for (int fail = 0; fail < 2; ++fail) {
        setup(&b);
        b.count = 2;
        b.f[1].team = 1;
        b.f[1].ap = 0;
        for (int r = 4; r <= 12; ++r) {
            const int friend_ = r >= 10;
            u = mt(friend_ ? -3000 + 3000 * (r - 10) : -4000 + 1000 * (r - 4), friend_ ? 3000 : 9000,
                   friend_ ? 0 : 2048, ACR_GEN1_BRAIN_MT);
            u.record = (uint8_t)r;
            u.team = (uint8_t)acr_gen1_mission_team(12, r);
            acr_gen1_battle_add_unit(&b, &u);
        }
        b.f[0].ac.z = -20000 * 64;                               /* the player far behind */
        acr_gen1_mission_start(&b, 12);
        EXPECT(b.mission.map == 13);
        acr_gen1_battle_step(&b, none, idle);
        EXPECT(b.unit[0].target >= ACR_GEN1_UNIT(6) && b.unit[0].target <= ACR_GEN1_UNIT(8));   /* a friendly */
        EXPECT(b.unit[6].target >= ACR_GEN1_UNIT(0) && b.unit[6].target <= ACR_GEN1_UNIT(5));   /* a target */
        EXPECT(acr_gen1_battle_result(&b, 0) == 0);
        /* some down first (two friendlies, or three targets): nothing yet */
        for (int k = 0; k < (fail ? 2 : 3); ++k) b.unit[fail ? 6 + k : k].down = 1, b.unit[fail ? 6 + k : k].ap = 0;
        for (int t = 0; t < 30; ++t) acr_gen1_battle_step(&b, none, idle);
        EXPECT(acr_gen1_battle_result(&b, 0) == 0 && !b.mission.end_ticks && b.mission.phase == 0);
        for (int k = fail ? 8 : 0; k <= (fail ? 8 : 5); ++k) b.unit[k].down = 1, b.unit[k].ap = 0;
        int ticks = 0;
        while (acr_gen1_battle_result(&b, 0) == 0 && ticks < 400) { acr_gen1_battle_step(&b, none, idle); ++ticks; }
        EXPECT(acr_gen1_battle_result(&b, 0) == (fail ? 2 : 1));
        EXPECT(b.mission.result == (fail ? ACR_GEN1_MISSION_FAILED : ACR_GEN1_MISSION_DONE));
        EXPECT(ticks == (fail ? 19 + 66 + 101 : 101));
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 units: all passed (first hit after %d steps)\n", steps);
    return 0;
}
