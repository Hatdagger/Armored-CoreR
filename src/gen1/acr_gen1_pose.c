#include "acr_gen1_pose.h"

/* Poses by direction, in the order forward, back, left, right, forward-left,
 * forward-right, back-left, back-right. */
static const uint8_t k_air[8] = { 26, 27, 28, 29, 32, 33, 34, 35 };
static const uint8_t k_boost[8] = { 48, 49, 50, 51, 52, 53, 54, 55 };

enum { D_FWD, D_BACK, D_LEFT, D_RIGHT, D_FL, D_FR, D_BL, D_BR, D_NONE };

/* The stick's direction, diagonals first, as the game's input decoder
 * checks them (0x800853E0). */
static int stick(acr_gen1_input in) {
    if (in.move > 0) return in.strafe < 0 ? D_FL : in.strafe > 0 ? D_FR : D_FWD;
    if (in.move < 0) return in.strafe < 0 ? D_BL : in.strafe > 0 ? D_BR : D_BACK;
    if (in.strafe) return in.strafe < 0 ? D_LEFT : D_RIGHT;
    return D_NONE;
}

static int64_t mag(int64_t v) { return v < 0 ? -v : v; }

/* Where the AC is going, relative to its facing, in eight sectors of 45
 * degrees centred on the eight directions (the game: ratan2 of the speed
 * less the facing, + 1/16 turn, / 1/8 turn). */
static int travel(const acr_gen1_ac *ac) {
    const int64_t s = acr_gen1_sin(ac->yaw), c = acr_gen1_cos(ac->yaw);
    const int64_t fwd = ac->vx * s + ac->vz * c;      /* forward = (sin, cos) */
    const int64_t right = ac->vx * c - ac->vz * s;    /* right = (cos, -sin) */
    /* tan(22.5 degrees) = 0.4142 */
    if (mag(right) * 10000 <= mag(fwd) * 4142) return fwd >= 0 ? D_FWD : D_BACK;
    if (mag(fwd) * 10000 <= mag(right) * 4142) return right >= 0 ? D_RIGHT : D_LEFT;
    if (fwd >= 0) return right >= 0 ? D_FR : D_FL;
    return right >= 0 ? D_BR : D_BL;
}

static int opposite(int d) {
    static const uint8_t k[8] = { D_BACK, D_FWD, D_RIGHT, D_LEFT, D_BR, D_BL, D_FR, D_FL };
    return k[d];
}

static int walk_loop(int a) {
    return a == 2 || a == 4 || a == 6 || a == 9 || a == 12 || a == 15 || a == 17 || a == 19;
}

int acr_gen1_pose_shown(const acr_gen1_ac *ac, acr_gen1_input in, int swinging, unsigned jump_ticks) {
    return acr_gen1_pose_shown_legs(ac, in, swinging, jump_ticks, 0);
}

int acr_gen1_pose_shown_legs(const acr_gen1_ac *ac, acr_gen1_input in, int swinging, unsigned jump_ticks,
                             int drive_walk) {
    if (ac->state == ACR_GEN1_CROUCH) return ACR_GEN1_ANIM_JUMP;
    if (ac->state == ACR_GEN1_AIR) {
        if (ac->anim == ACR_GEN1_ANIM_JUMP && ac->anim_frame <= jump_ticks) return ACR_GEN1_ANIM_JUMP;
        const int d = stick(in);
        if (d != D_NONE) return k_air[d];
        if (in.turn) return in.turn < 0 ? ACR_GEN1_SHOW_AIR_TURN_L : ACR_GEN1_SHOW_AIR_TURN_R;
        return in.boost ? ACR_GEN1_SHOW_RISE : ACR_GEN1_SHOW_AIR;
    }
    if (swinging) return ACR_GEN1_SHOW_BLADE;
    if (ac->state == ACR_GEN1_LANDING) return ac->landing_soft ? ACR_GEN1_ANIM_LAND_SOFT : ACR_GEN1_ANIM_LAND;
    if (in.crouch) return ac->anim;
    if (drive_walk && walk_loop(ac->anim) && !in.boost && !in.cruise) return ac->anim;
    if (ac->vx || ac->vz) {
        const int d = stick(in);
        /* Boosting (or the modern boost toggle) with a direction: that way's pose. */
        if ((in.boost || in.cruise) && d != D_NONE && !ac->drained) return k_boost[d];
        return k_boost[opposite(travel(ac))];
    }
    if (ac->anim == ACR_GEN1_ANIM_IDLE && ac->turn_rate) return ACR_GEN1_SHOW_TURN;
    return ac->anim;
}

uint16_t acr_gen1_pose_fade_step(int from, int to) {
    if (from == to || to == ACR_GEN1_ANIM_LAND || to == ACR_GEN1_ANIM_LAND_SOFT) return ACR_GEN1_FADE_CUT;
    /* The landing ends in a cut to standing (0x80084C58); moving on from
     * there fades as from standing, and the landing's last pose is close
     * to it. */
    if ((from == ACR_GEN1_ANIM_LAND || from == ACR_GEN1_ANIM_LAND_SOFT) && to == ACR_GEN1_ANIM_IDLE) return ACR_GEN1_FADE_CUT;
    /* A walk's start animation runs into its loop without a fade. */
    if (to == from + 1 && (from == ACR_GEN1_ANIM_WALK_START || from == ACR_GEN1_ANIM_STRAFE_L_START ||
                           from == ACR_GEN1_ANIM_STRAFE_R_START || from == ACR_GEN1_ANIM_BACK_START))
        return ACR_GEN1_FADE_CUT;
    /* The take-off (0x80084648) and the blade swing (0x80084F98) set 0x400. */
    if (to == ACR_GEN1_ANIM_JUMP || to == ACR_GEN1_SHOW_BLADE) return ACR_GEN1_FADE_QUICK;
    /* The air slash too: at the normal fade half of its 16 ticks went on
     * blending in and the arm barely moved (assumed to share the swing's
     * flag; user report 2026-11-27). */
    if (to == ACR_GEN1_SHOW_AIR_SLASH) return ACR_GEN1_FADE_QUICK;
    return ACR_GEN1_FADE_NORMAL;
}

float acr_gen1_pose_fade_amount(uint16_t step, float ticks) {
    if (!step) return 1.0f;
    const float t = ticks * (float)step / 4096.0f;
    return t <= 0.0f ? 0.0f : t >= 1.0f ? 1.0f : t;
}

uint16_t acr_gen1_pose_fade_step_legs(int from, int to, int drive_walk) {
    if (from != to && to >= ACR_GEN1_SHOW_HIT_FRONT && to <= ACR_GEN1_SHOW_AIR_HIT_FRONT + 3) return ACR_GEN1_FADE_QUICK;
    if (drive_walk && from != to && (to == ACR_GEN1_ANIM_IDLE || walk_loop(to))) return ACR_GEN1_FADE_SLOW;
    return acr_gen1_pose_fade_step(from, to);
}

int acr_gen1_hit_side(uint16_t yaw, int64_t dx, int64_t dz) {
    /* The hit comes from -(dx, dz); seen from the AC: ahead and to the right. */
    const int64_t s = acr_gen1_sin(yaw), c = acr_gen1_cos(yaw);
    const int64_t ahead = -(dx * s + dz * c), right = -(dx * c - dz * s);
    if (mag(ahead) >= mag(right)) return ahead >= 0 ? 0 : 1;
    return right >= 0 ? 3 : 2;
}

int acr_gen1_hit_anim(int side, int air) {
    if (side < 0 || side > 3) side = 0;
    return (air ? ACR_GEN1_SHOW_AIR_HIT_FRONT : ACR_GEN1_SHOW_HIT_FRONT) + side;
}

float acr_gen1_kneel_tick(int32_t kneel_ms, int32_t stand_ms, int32_t need_ms, unsigned length) {
    if (need_ms <= 0 || !length) return 0.0f;
    const int32_t at = stand_ms > 0 ? stand_ms : kneel_ms;
    const float t = (float)at / (float)need_ms;
    return (t >= 1.0f ? 1.0f : t <= 0.0f ? 0.0f : t) * (float)length;
}
