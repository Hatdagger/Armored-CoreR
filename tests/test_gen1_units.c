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

    /* A hit turns the MT's step around (event 5). */
    setup(&b);
    u = mt(0, 3000, 2048, ACR_GEN1_BRAIN_MT);
    acr_gen1_battle_add_unit(&b, &u);
    b.unit[0].state = ACR_GEN1_UNIT_AHEAD_L;
    b.unit[0].state_ticks = 200;
    while (b.unit[0].ap == 5000 && steps < 400) { acr_gen1_battle_step(&b, none, fire); ++steps; }
    EXPECT(b.unit[0].state == ACR_GEN1_UNIT_BACK_L);

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

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 units: all passed (first hit after %d steps)\n", steps);
    return 0;
}
