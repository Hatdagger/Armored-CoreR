/* Gen 1 damage (checked against the game) and the fight stepping. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_battle.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main(void) {
    /* Factor from defense: the default AC's shell defense 1,604 -> 27. */
    EXPECT(acr_gen1_damage_factor(1604, 0, 0) == 27);
    EXPECT(acr_gen1_damage_factor(100000, 0, 0) >= 1);
    /* Measured in the game: rifle (attack 218) against factors 1..96. */
    static const int f[] = { 1, 10, 20, 26, 27, 28, 30, 45, 64, 96 };
    static const int ap[] = { 2, 22, 45, 58, 60, 63, 68, 102, 145, 218 };
    for (int i = 0; i < 10; ++i) EXPECT(acr_gen1_hit_ap(218, (uint8_t)f[i]) == ap[i]);
    EXPECT(acr_gen1_hit_ap(830, 27) == 233);        /* missile, measured */

    /* Two ACs 3,000 apart, facing each other: the rifle locks, then hits. */
    static acr_gen1_ac_def def;
    memset(&def, 0, sizeof def);
    def.stats.energy_capacity = 28000;
    def.stats.thrust = 32; def.stats.thrust_80 = 25; def.stats.thrust_120 = 38;
    def.stats.turn_accel = 22; def.stats.turn_speed = 41;
    acr_gen1_motion_from_stats(&def.stats, &def.motion);
    static acr_gen1_battle b;
    memset(&b, 0, sizeof b);
    b.step_ms = 45;
    for (int i = 0; i < 2; ++i) {
        acr_gen1_fighter *fi = &b.f[i];
        fi->def = &def;
        acr_gen1_ac_spawn(&fi->ac, &def, 0, i ? 3000 * 64 : 0, i ? 2048 : 0);
        fi->ap = fi->ap_max = 8384;
        fi->shell_factor = fi->energy_factor = 27;
        fi->weapons = 1;
        fi->weapon[0] = (acr_gen1_weapon){ ACR_GEN1_GUN, "RIFLE", 218, 200, 6100, 0, 1000, 240, 240, 600, 700, 0, 0 };
        fi->ammo[0] = 200;
    }
    const acr_gen1_input none[2] = { { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } };
    const acr_gen1_actions fire[2] = { { 1, 0, 0 }, { 0, 0, 0 } };
    EXPECT(acr_gen1_in_lock_box(&b, 0, &b.f[0].weapon[0]));
    int steps = 0;
    while (b.f[1].ap == 8384 && steps < 200) { acr_gen1_battle_step(&b, none, fire); ++steps; }
    EXPECT(b.f[1].ap == 8384 - 60);
    EXPECT(b.f[0].locked == 0 || b.f[0].lock_ms >= 1000);

    /* At 60 steps a second a rifle shot moves 700 x 22 / 60 = 256 units a
     * step: the same speed per second as the original. */
    memset(b.shot, 0, sizeof b.shot);
    b.step_rate = 60; b.gen1_rate = 22;
    b.f[0].cooldown_ms = 0;
    b.f[1].ac.x = 50000 * 64;                         /* out of the way */
    acr_gen1_battle_step(&b, none, fire);
    int found = 0;
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k)
        if (b.shot[k].active) { found = 1; EXPECT(b.shot[k].vz / 64 == 256); }
    EXPECT(found);

    /* Blade: its own button, not in the weapon cycle. A swing lunges the AC
     * forward and hits a target ahead within reach once. */
    {
        static acr_gen1_battle bb;
        bb = b;
        memset(bb.shot, 0, sizeof bb.shot);
        bb.step_rate = 0; bb.gen1_rate = 0; bb.step_ms = 45;
        acr_gen1_fighter *me = &bb.f[0];
        me->weapons = 2;
        me->weapon[1] = (acr_gen1_weapon){ ACR_GEN1_BLADE, "BLADE", 1000, 0, 200, 1, 1000, 0, 0, 0, 0, 0, 0 };
        me->current = 0;
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 0, 600 * 64, 2048);    /* out of reach until the lunge */
        bb.f[1].ap = 8384;
        const acr_gen1_actions cycle[2] = { { 0, 1, 0 }, { 0, 0, 0 } };
        acr_gen1_battle_step(&bb, none, cycle);
        EXPECT(me->current == 0);                       /* only the gun to cycle to */
        const acr_gen1_actions swing[2] = { { 0, 0, 1 }, { 0, 0, 0 } };
        const acr_gen1_actions idle[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
        acr_gen1_battle_step(&bb, none, swing);
        EXPECT(me->swing > 0);
        for (int k = 0; k < 20; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(me->ac.z > 400 * 64 && me->ac.z < 516 * 64);   /* lunged until it hit */
        EXPECT(bb.f[1].ap == 8384 - acr_gen1_hit_ap(1000, 27));   /* one full hit */
        EXPECT(me->swing == 0);
        /* Nothing in reach: the full lunge, 516 straight ahead. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 0, 3000 * 64, 2048);
        me->blade_ms = 0;
        acr_gen1_battle_step(&bb, none, swing);
        for (int k = 0; k < 20; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(me->ac.z / 64 >= 510 && me->ac.z / 64 <= 522 && me->ac.x == 0);
        /* Steering: a target 45 degrees right turns the AC by the full turn. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 2000 * 64, 2000 * 64, 2048);
        me->blade_ms = 0;
        acr_gen1_battle_step(&bb, none, swing);
        for (int k = 0; k < 20; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(me->ac.yaw == ACR_GEN1_BLADE_TURN && me->ac.x > 0);
        /* Far to the side (70 degrees): no turn. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 2750 * 64, 1000 * 64, 2048);
        me->blade_ms = 0;
        acr_gen1_battle_step(&bb, none, swing);
        for (int k = 0; k < 20; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(me->ac.yaw == 0);
        acr_gen1_ac_spawn(&bb.f[1].ac, &def, 0, 600 * 64, 2048);
        /* Out of reach and behind: a swing hits nothing. */
        acr_gen1_ac_spawn(&me->ac, &def, 0, 0, 2048);
        me->blade_ms = 0;
        const int32_t before = bb.f[1].ap;
        acr_gen1_battle_step(&bb, none, swing);
        for (int k = 0; k < 20; ++k) acr_gen1_battle_step(&bb, none, idle);
        EXPECT(bb.f[1].ap == before);
    }

    /* Cannon stance: a two-legged AC kneels first, four legs need the
     * ground, caterpillars and Human PLUS fire freely. */
    memset(b.shot, 0, sizeof b.shot);
    b.step_rate = b.gen1_rate = 0;
    b.f[0].weapon[0].stance = 1;
    b.f[0].legs = ACR_GEN1_LEGS_TWO;
    b.f[0].cooldown_ms = 0;
    b.f[0].kneeling = 0;
    acr_gen1_battle_step(&b, none, fire);
    int shots = 0;
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) shots += b.shot[k].active;
    EXPECT(shots == 0 && b.f[0].kneeling);
    for (int k = 0; k < 12; ++k) acr_gen1_battle_step(&b, none, fire);
    shots = 0;
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) shots += b.shot[k].active;
    EXPECT(shots >= 1);
    b.f[0].legs = ACR_GEN1_LEGS_FOUR;
    b.f[0].ac.state = ACR_GEN1_AIR;
    EXPECT(!acr_gen1_stance_ok(&b.f[0]));
    b.f[0].legs = ACR_GEN1_LEGS_TRACKS;
    EXPECT(acr_gen1_stance_ok(&b.f[0]));
    b.f[0].legs = ACR_GEN1_LEGS_FOUR;
    b.f[0].human_plus = 1;
    EXPECT(acr_gen1_stance_ok(&b.f[0]));

    /* Missiles only leave once the lock is complete. */
    {
        static acr_gen1_battle mb;
        mb = b;
        memset(mb.shot, 0, sizeof mb.shot);
        mb.f[0].human_plus = 0;
        mb.f[0].weapon[0] = (acr_gen1_weapon){ ACR_GEN1_MISSILE, "MISSILE", 830, 40, 8000, 0, 900, 200, 200,
                                               1200, 350, 80, 0 };
        mb.f[0].ammo[0] = 40;
        mb.f[0].ac = b.f[0].ac;
        mb.f[1].ac = b.f[1].ac;
        mb.f[1].ac.x = b.f[0].ac.x;
        mb.f[1].ac.z = b.f[0].ac.z + 3000 * 64;
        mb.f[0].ac.yaw = 0;
        mb.f[0].lock_ms = 0; mb.f[0].locked = 0; mb.f[0].cooldown_ms = 0;
        mb.step_ms = 45;
        acr_gen1_battle_step(&mb, none, fire);
        int n = 0;
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) n += mb.shot[k].active;
        EXPECT(n == 0);
        for (int k = 0; k < 25; ++k) acr_gen1_battle_step(&mb, none, fire);
        EXPECT(mb.f[0].locked && mb.f[0].ammo[0] < 40);
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 battle: all passed (first hit after %d steps)\n", steps);
    return 0;
}
