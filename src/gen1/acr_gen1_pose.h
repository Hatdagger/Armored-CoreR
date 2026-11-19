/* Which body animation the game shows on an AC, and how it blends into it.
 * Presentation only: nothing here changes the match.
 *
 * Read from Master of Arena's VS engine (FDAT 209), the state handler
 * two-legged ACs use (0x8008389C, installed by 0x80085688), and the pose
 * routine 0x80059624 (AC1Recomp mapping, renderer.md "ACs (VS)"):
 * - Within an animation the game eases from key to key (frame + fraction).
 * - On a change of animation it cross-fades: each joint starts from where
 *   it was drawn and moves to the new animation by a step per tick (AC
 *   +0x7A, 4.12: 0x200 = 8 ticks, 0x400 = 4, 0 = cut).
 * Notes and addresses: docs/research/gen1-models.md, "Blending". */
#ifndef ACR_GEN1_POSE_H
#define ACR_GEN1_POSE_H

#include <stdint.h>

#include "acr_gen1_sim.h"

/* Animations shown over the movement (Master of Arena numbering). Single
 * poses unless noted. */
enum {
    ACR_GEN1_SHOW_TURN       = 21,  /* turning on the spot */
    ACR_GEN1_SHOW_AIR        = 24,  /* in the air, no input */
    ACR_GEN1_SHOW_RISE       = 25,  /* in the air, booster alone */
    ACR_GEN1_SHOW_AIR_FWD    = 26, ACR_GEN1_SHOW_AIR_BACK = 27,
    ACR_GEN1_SHOW_AIR_LEFT   = 28, ACR_GEN1_SHOW_AIR_RIGHT = 29,
    ACR_GEN1_SHOW_AIR_TURN_L = 30, ACR_GEN1_SHOW_AIR_TURN_R = 31,
    ACR_GEN1_SHOW_AIR_FL     = 32, ACR_GEN1_SHOW_AIR_FR = 33,
    ACR_GEN1_SHOW_AIR_BL     = 34, ACR_GEN1_SHOW_AIR_BR = 35,
    ACR_GEN1_SHOW_BOOST_FWD  = 48, ACR_GEN1_SHOW_BOOST_BACK = 49,
    ACR_GEN1_SHOW_BOOST_LEFT = 50, ACR_GEN1_SHOW_BOOST_RIGHT = 51,
    ACR_GEN1_SHOW_BOOST_FL   = 52, ACR_GEN1_SHOW_BOOST_FR = 53,
    ACR_GEN1_SHOW_BOOST_BL   = 54, ACR_GEN1_SHOW_BOOST_BR = 55,
    ACR_GEN1_SHOW_BLADE      = 56   /* blade swing (animated) */
};

/* Cross-fade steps (AC +0x7A), 4.12 a tick. */
enum { ACR_GEN1_FADE_CUT = 0, ACR_GEN1_FADE_NORMAL = 0x200, ACR_GEN1_FADE_QUICK = 0x400 };

/* The animation the game would show now. `in` is the AC's latest input;
 * `swinging` = a blade swing is under way; `jump_ticks` = the length of the
 * AC's jump take-off animation (22), which plays on into the air until it
 * ends. Walking, standing and the take-off come from the movement
 * (ac->anim); the rest are picked here:
 * - in the air, by the input (0x8008389C states 0x40-0x4B): a direction
 *   picks one of eight poses (diagonals first), else turning, else the
 *   booster alone (25), else 24;
 * - boosting along the ground, by the stick direction (states 0x30-0x37);
 * - sliding after a boost, by where the AC is going, as a brake: the pose
 *   of a boost the other way (state 0x38, table at 0x800517F8);
 * - turning on the spot: 21. */
int acr_gen1_pose_shown(const acr_gen1_ac *ac, acr_gen1_input in, int swinging, unsigned jump_ticks);

/* Fade step for a change from animation `from` to `to`: landings cut in
 * and out, the jump take-off and the blade fade in 4 ticks, a walk's
 * start animation runs straight into its loop, everything else 8 ticks. */
uint16_t acr_gen1_pose_fade_step(int from, int to);

/* How far a fade with this step has got after `ticks` Gen 1 ticks (may be
 * fractional, for drawing between ticks): 0 = the old pose, 1 = the new. */
float acr_gen1_pose_fade_amount(uint16_t step, float ticks);

#endif
