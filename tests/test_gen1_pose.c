/* Which pose the game shows and how it fades into it (acr_gen1_pose):
 * the rules read from Master of Arena's VS engine, on made-up AC states. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_pose.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_input in(int move, int strafe, int turn, int boost) {
    acr_gen1_input i;
    memset(&i, 0, sizeof i);
    i.move = (int8_t)move; i.strafe = (int8_t)strafe; i.turn = (int8_t)turn; i.boost = (uint8_t)boost;
    return i;
}

static acr_gen1_ac ac_in(int state) {
    acr_gen1_ac a;
    memset(&a, 0, sizeof a);
    a.state = (uint8_t)state;
    return a;
}

/* Sliding at `speed` along local (forward, right), facing `yaw`. */
static int slide_pose(uint16_t yaw, int fwd, int right) {
    acr_gen1_ac a = ac_in(ACR_GEN1_GROUND);
    a.yaw = yaw;
    const int s = acr_gen1_sin(yaw), c = acr_gen1_cos(yaw);
    /* forward = (sin, cos), right = (cos, -sin) */
    a.vx = (fwd * s + right * c) / 4096;
    a.vz = (fwd * c - right * s) / 4096;
    return acr_gen1_pose_shown(&a, in(0, 0, 0, 0), 0, 9);
}

/* A standing jump through the movement: the take-off plays its frames
 * through the crouch and into the air, then the rising pose takes over. */
static void jump_sequence(void) {
    static const acr_gen1_root_key k_jump[] = { { 7, 0, 0, 0 }, { 9, 0, -400, 0 } };
    static acr_gen1_root_track jump = { 9, 2, k_jump };
    acr_gen1_ac_def d;
    memset(&d, 0, sizeof d);
    d.stats.energy_capacity = 28000; d.stats.rise_drain = 400; d.stats.refill_standing = 200;
    d.stats.refill_moving = 100; d.stats.red_zone = 8000;
    d.stats.thrust = 32; d.stats.thrust_80 = 25; d.stats.thrust_120 = 38;
    d.stats.turn_accel = 22; d.stats.turn_speed = 41; d.stats.friction = 43;
    acr_gen1_motion_from_stats(&d.stats, &d.motion);
    d.anim[ACR_GEN1_ANIM_JUMP] = &jump;
    acr_gen1_ac a;
    acr_gen1_ac_spawn(&a, &d, 0, 0, 0);
    int seen[64] = { 0 }, order[8], n = 0, last = -1;
    for (int t = 0; t < 30; ++t) {
        const acr_gen1_input i = in(0, 0, 0, t < 20);
        acr_gen1_ac_step(&a, &d, i);
        const int s = acr_gen1_pose_shown(&a, i, 0, jump.length);
        if (s == ACR_GEN1_ANIM_JUMP) EXPECT(a.anim_frame == (unsigned)t + 1);   /* a frame a tick */
        if (s != last && n < 8) order[n++] = s;
        last = s;
        ++seen[s];
    }
    EXPECT(n == 3 && order[0] == ACR_GEN1_ANIM_JUMP && order[1] == ACR_GEN1_SHOW_RISE && order[2] == ACR_GEN1_SHOW_AIR);
    EXPECT(seen[ACR_GEN1_ANIM_JUMP] == 9);
}

int main(void) {
    jump_sequence();

    /* In the air: by input, diagonals before straight, then turning, then
     * the booster alone (25), else 24 (states 0x40-0x4B). */
    acr_gen1_ac air = ac_in(ACR_GEN1_AIR);
    EXPECT(acr_gen1_pose_shown(&air, in(0, 0, 0, 0), 0, 9) == 24);
    EXPECT(acr_gen1_pose_shown(&air, in(0, 0, 0, 1), 0, 9) == 25);
    EXPECT(acr_gen1_pose_shown(&air, in(1, 0, 0, 1), 0, 9) == 26);
    EXPECT(acr_gen1_pose_shown(&air, in(-1, 0, 0, 0), 0, 9) == 27);
    EXPECT(acr_gen1_pose_shown(&air, in(0, -1, 0, 0), 0, 9) == 28);
    EXPECT(acr_gen1_pose_shown(&air, in(0, 1, 0, 0), 0, 9) == 29);
    EXPECT(acr_gen1_pose_shown(&air, in(0, 0, -1, 1), 0, 9) == 30);
    EXPECT(acr_gen1_pose_shown(&air, in(0, 0, 1, 0), 0, 9) == 31);
    EXPECT(acr_gen1_pose_shown(&air, in(1, -1, 1, 0), 0, 9) == 32);
    EXPECT(acr_gen1_pose_shown(&air, in(1, 1, 0, 0), 0, 9) == 33);
    EXPECT(acr_gen1_pose_shown(&air, in(-1, -1, 0, 0), 0, 9) == 34);
    EXPECT(acr_gen1_pose_shown(&air, in(-1, 1, 0, 0), 0, 9) == 35);
    /* The take-off plays on into the air until it ends. */
    air.anim = ACR_GEN1_ANIM_JUMP;
    air.anim_frame = 9;
    EXPECT(acr_gen1_pose_shown(&air, in(1, 0, 0, 1), 0, 9) == ACR_GEN1_ANIM_JUMP);
    air.anim_frame = 10;
    EXPECT(acr_gen1_pose_shown(&air, in(1, 0, 0, 1), 0, 9) == 26);
    acr_gen1_ac crouch = ac_in(ACR_GEN1_CROUCH);
    EXPECT(acr_gen1_pose_shown(&crouch, in(0, 0, 0, 1), 0, 9) == ACR_GEN1_ANIM_JUMP);

    /* Boosting along the ground: by the stick (states 0x30-0x37). */
    acr_gen1_ac g = ac_in(ACR_GEN1_GROUND);
    g.vz = 3000;
    EXPECT(acr_gen1_pose_shown(&g, in(1, 0, 0, 1), 0, 9) == 48);
    EXPECT(acr_gen1_pose_shown(&g, in(-1, 0, 0, 1), 0, 9) == 49);
    EXPECT(acr_gen1_pose_shown(&g, in(0, -1, 0, 1), 0, 9) == 50);
    EXPECT(acr_gen1_pose_shown(&g, in(0, 1, 0, 1), 0, 9) == 51);
    EXPECT(acr_gen1_pose_shown(&g, in(1, -1, 0, 1), 0, 9) == 52);
    EXPECT(acr_gen1_pose_shown(&g, in(1, 1, 0, 1), 0, 9) == 53);
    EXPECT(acr_gen1_pose_shown(&g, in(-1, -1, 0, 1), 0, 9) == 54);
    EXPECT(acr_gen1_pose_shown(&g, in(-1, 1, 0, 1), 0, 9) == 55);
    /* Out of energy: no boost, so it slides. */
    g.drained = 1;
    EXPECT(acr_gen1_pose_shown(&g, in(0, 1, 0, 1), 0, 9) == 49);

    /* Sliding: a brake, the pose of a boost the other way (table at
     * 0x800517F8: sectors 0-7 = 49, 55, 51, 53, 48, 52, 50, 54), at any facing. */
    for (int yaw = 0; yaw < 4096; yaw += 512) {
        const uint16_t y = (uint16_t)yaw;
        EXPECT(slide_pose(y, 3000, 0) == 49);
        EXPECT(slide_pose(y, 2000, -2000) == 55);
        EXPECT(slide_pose(y, 0, -3000) == 51);
        EXPECT(slide_pose(y, -2000, -2000) == 53);
        EXPECT(slide_pose(y, -3000, 0) == 48);
        EXPECT(slide_pose(y, -2000, 2000) == 52);
        EXPECT(slide_pose(y, 0, 3000) == 50);
        EXPECT(slide_pose(y, 2000, 2000) == 54);
        /* within 22.5 degrees of straight ahead still counts as straight */
        EXPECT(slide_pose(y, 3000, 1200) == 49);
        EXPECT(slide_pose(y, 3000, 1300) == 54);
    }

    /* Standing, turning, landing, the blade, the cannon stance. */
    acr_gen1_ac st = ac_in(ACR_GEN1_GROUND);
    EXPECT(acr_gen1_pose_shown(&st, in(0, 0, 0, 0), 0, 9) == ACR_GEN1_ANIM_IDLE);
    st.turn_rate = 6;
    EXPECT(acr_gen1_pose_shown(&st, in(0, 0, 1, 0), 0, 9) == ACR_GEN1_SHOW_TURN);
    st.anim = ACR_GEN1_ANIM_WALK;
    EXPECT(acr_gen1_pose_shown(&st, in(1, 0, 1, 0), 0, 9) == ACR_GEN1_ANIM_WALK);
    EXPECT(acr_gen1_pose_shown(&st, in(1, 0, 1, 0), 1, 9) == ACR_GEN1_SHOW_BLADE);
    acr_gen1_ac land = ac_in(ACR_GEN1_LANDING);
    EXPECT(acr_gen1_pose_shown(&land, in(1, 0, 0, 0), 0, 9) == ACR_GEN1_ANIM_LAND);
    acr_gen1_input kneel = in(0, 0, 0, 0);
    kneel.crouch = 1;
    g.drained = 0;
    EXPECT(acr_gen1_pose_shown(&g, kneel, 0, 9) == g.anim);

    /* Fade steps. */
    EXPECT(acr_gen1_pose_fade_step(0, 1) == 0x200);
    EXPECT(acr_gen1_pose_fade_step(1, 2) == 0);           /* walk start into its loop */
    EXPECT(acr_gen1_pose_fade_step(8, 9) == 0);
    EXPECT(acr_gen1_pose_fade_step(2, 9) == 0x200);
    EXPECT(acr_gen1_pose_fade_step(2, 0) == 0x200);
    EXPECT(acr_gen1_pose_fade_step(0, 22) == 0x400);      /* take-off */
    EXPECT(acr_gen1_pose_fade_step(22, 25) == 0x200);
    EXPECT(acr_gen1_pose_fade_step(25, 26) == 0x200);
    EXPECT(acr_gen1_pose_fade_step(24, 36) == 0);         /* touchdown cuts in */
    EXPECT(acr_gen1_pose_fade_step(36, 0) == 0);          /* and out to standing */
    EXPECT(acr_gen1_pose_fade_step(36, 2) == 0x200);
    EXPECT(acr_gen1_pose_fade_step(48, 49) == 0x200);
    EXPECT(acr_gen1_pose_fade_step(2, 56) == 0x400);      /* blade */
    EXPECT(acr_gen1_pose_fade_step(56, 0) == 0x200);
    EXPECT(acr_gen1_pose_fade_step(5, 5) == 0);

    /* Fade amount: a step a tick out of 4096. */
    EXPECT(acr_gen1_pose_fade_amount(0x200, 0.0f) == 0.0f);
    EXPECT(acr_gen1_pose_fade_amount(0x200, 4.0f) == 0.5f);
    EXPECT(acr_gen1_pose_fade_amount(0x200, 8.0f) == 1.0f);
    EXPECT(acr_gen1_pose_fade_amount(0x200, 20.0f) == 1.0f);
    EXPECT(acr_gen1_pose_fade_amount(0x400, 2.0f) == 0.5f);
    EXPECT(acr_gen1_pose_fade_amount(0, 0.0f) == 1.0f);

    if (failures) printf("%d failures\n", failures);
    else printf("gen1 pose: ok\n");
    return failures != 0;
}
