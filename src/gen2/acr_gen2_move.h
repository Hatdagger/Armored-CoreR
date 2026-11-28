/* Gen 2 AC movement: one step per Armored Core 2 frame (60 a second), by the
 * game's own rules. Read from the AC state code in SLUS_200.14 (state tables
 * 0x3c8020 / 0x3c8368 / 0x3c86d8, per-frame update 0x139050); the rules,
 * numbers and what is still unconfirmed are in docs/research/gen2-movement.md.
 *
 * What is here: standing, walking, turning, ground boost and its slide,
 * jumping, flying, landing, overboost and energy, for all four leg kinds, on
 * a ground plane the caller supplies. What is not: blade lunges, kneeling to
 * fire, hit reactions, walls (the battle and the stage own those).
 *
 * Axes are the game's: y points down (up is negative, the ground at
 * ground_y), an AC at yaw 0 faces -z, +x is its left. Units as in
 * acr_gen2_compile.h. */
#ifndef ACR_GEN2_MOVE_H
#define ACR_GEN2_MOVE_H

#include <stdint.h>

#include "acr_gen2_compile.h"

/* The game's own state numbers (the ones this module uses). */
enum {
    ACR_GEN2_ST_STAND = 0,
    ACR_GEN2_ST_WALK_START = 1,
    ACR_GEN2_ST_WALK = 3,
    ACR_GEN2_ST_TURN = 4,
    ACR_GEN2_ST_DASH = 5,        /* ground boost */
    ACR_GEN2_ST_BRAKE = 6,       /* the slide after a ground boost */
    ACR_GEN2_ST_CROUCH = 7,      /* jump take-off */
    ACR_GEN2_ST_AIR = 8,
    ACR_GEN2_ST_LAND = 9,
    ACR_GEN2_ST_HARD_LAND = 10,
    ACR_GEN2_ST_OVERBOOST = 16
};

/* Directions as the game numbers them (0x13fa68); 8 = none held. */
enum {
    ACR_GEN2_DIR_FWD = 0, ACR_GEN2_DIR_FWD_L = 1, ACR_GEN2_DIR_FWD_R = 2,
    ACR_GEN2_DIR_L = 3, ACR_GEN2_DIR_R = 4,
    ACR_GEN2_DIR_BACK = 5, ACR_GEN2_DIR_BACK_L = 6, ACR_GEN2_DIR_BACK_R = 7,
    ACR_GEN2_DIR_NONE = 8
};

typedef struct acr_gen2_input {
    uint8_t forward, back, left, right;   /* move and strafe */
    int8_t  turn;                         /* +1 / -1: the game's two turn buttons */
    uint8_t boost;                        /* booster button held */
    uint8_t overboost;                    /* overboost button pressed this frame */
} acr_gen2_input;

/* What the AC's motion set (AC2 B 421-492) decides: two-legged ACs walk at
 * the speed their walk clips are authored at, and several states wait for a
 * clip to end. tools/gen2/movement.py --walk lists all 72 sets. */
typedef struct acr_gen2_gait {
    float   walk_speed[8];          /* walk loop: units a frame, by direction */
    float   walk_start_dist[8];     /* walk start: units covered over the clip */
    uint8_t walk_start_frames[8];
    uint8_t crouch_frames;          /* motion 35: jump take-off */
    uint8_t land_frames;            /* motion 38 */
    uint8_t hard_land_frames;       /* motion 51 */
} acr_gen2_gait;

typedef struct acr_gen2_ac {
    float   x, y, z;
    float   vx, vy, vz;
    float   yaw, yaw_speed;
    int32_t energy;
    uint8_t charging;       /* ran dry: no boosting until the bar is full */
    uint8_t state, substate;
    uint8_t pending;        /* a state queued for when the current one ends (unit +0xce2) */
    int32_t state_frame;
    uint8_t move_dir;       /* direction the walk / boost is going */
    int32_t dash_idle;      /* ground boost: frames with no direction held */
    int32_t dash_coast;     /* ground boost: frames since it was let go */
    int32_t ob_charge;      /* overboost: frames into the charge, 0 = not charging */
    float   speed_shown;    /* the HUD's smoothed speed (x 216 = km/h) */
    uint8_t events;         /* scratch: this frame's inputs may change the state */
    uint8_t changed;        /* scratch: the state or substate changed this frame */
} acr_gen2_ac;

#define ACR_GEN2_KMH_PER_SPEED 216.0f   /* HUD: speed x 60 x 60 x 60 x 10 / 1000, shown / 10 */
#define ACR_GEN2_OB_CHARGE_FRAMES 90    /* overboost: button to ignition */

/* A full-energy AC standing at the origin. */
void acr_gen2_move_init(acr_gen2_ac *ac, const acr_gen2_stats *s);

/* One frame. ground_y is the height of the ground under the AC. */
void acr_gen2_move_step(acr_gen2_ac *ac, const acr_gen2_tuning *t, const acr_gen2_stats *s,
                        const acr_gen2_gait *g, const acr_gen2_input *in, float ground_y);

/* The direction number for an input (0x13fa68). */
int acr_gen2_dir(const acr_gen2_input *in);

#endif
