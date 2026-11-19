/* Aiming: the arms, waist and back units follow the target over the
 * animation, as Master of Arena's VS pose routine does (0x80059624).
 * Presentation only: nothing here changes the match.
 *
 * Every tick the game clears the AC's aim table (AC +0x84) and the selected
 * weapon's fire routine sets it again (weapon detail +3, +1 for the right
 * back slot) with the aim angles (AC +0x7E / +0x80 / +0x82):
 * - x: the angle up or down to the target while it is in the lock box, else
 *   the view tilt;
 * - left / right to the target on y (cannons, weapon arms) or z (hand
 *   weapons), 0 when not aiming at it.
 * The pose routine then, for the table's joints:
 * - clamps the aim on each axis to the table's range around the first
 *   joint's parent (its angles now, 0x800148D0), the short way round
 *   (0x800595B0): delta = clamped (rest + aim) - rest;
 * - sets each joint to rest + delta x weight (4.12; one weight below 0, one
 *   above), in place of the animation;
 * - gives the first joint a rotation of its own (not added to its parent's:
 *   flag 0x20), so the gun stays on the target whatever the body does;
 * - eases the drawn angles toward these by 1/8 a tick (snapping within 64).
 * When the table changes, the old joints ease back into the animation the
 * same way (the first one's angles turned back into its parent's space),
 * and the new first joint starts from where it was drawn.
 * Notes: docs/research/gen1-models.md, "Aiming". */
#ifndef ACR_AIM_H
#define ACR_AIM_H

#include "acr_model.h"

typedef struct acr_aim_state {
    int     table;                                 /* in use, -1 none */
    uint8_t easing[ACR_SKEL_MAX_NODES];            /* drawn from ang[] (aimed, or settling back) */
    uint8_t aimed[ACR_SKEL_MAX_NODES];
    uint8_t own[ACR_SKEL_MAX_NODES];               /* ang[] is its own rotation (the table's first joint) */
    float   ang[ACR_SKEL_MAX_NODES][3];
} acr_aim_state;

void acr_aim_reset(acr_aim_state *st);

/* The game's clamp (0x800595B0): rest + add brought into min..max the short
 * way round a 4096 turn, less rest. */
int acr_aim_clamp(int rest, int min, int max, int add);

/* The angles the table sets on its joints for this aim, `parent` = the
 * first joint's parent's angles. out[k] for joint[k]. */
void acr_aim_targets(const acr_aim_table *t, const int16_t aim[3], const int16_t parent[3],
                     int16_t out[ACR_AIM_MAX_JOINTS][3]);

/* One drawn frame: `pose` comes in as the animation's pose and leaves with
 * the aim applied. `table` -1 = not aiming; `ticks` = Gen 1 ticks since the
 * last call (may be fractional; 0 = no easing, the first frame). */
void acr_aim_apply(acr_aim_state *st, const acr_skeleton *skel, const acr_anim_set *set, int table,
                   const int16_t aim[3], float ticks, acr_pose *pose);

#endif
