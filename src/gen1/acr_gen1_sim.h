/* Gen 1 AC movement, one step at a time (22 steps a second). Integer only,
 * so every machine runs the same match. Rules and numbers:
 * docs/research/gen1-movement.md.
 *
 * Units: position in 1/64 world units (y up is negative, ground at 0),
 * speeds in 1/64 world units per step, facing 0..4095 for a full turn. */
#ifndef ACR_GEN1_SIM_H
#define ACR_GEN1_SIM_H

#include <stdint.h>

#include "acr_gen1_anim.h"
#include "acr_gen1_compile.h"

/* Animations the movement uses (Master of Arena numbering). */
enum {
    ACR_GEN1_ANIM_IDLE = 0,
    ACR_GEN1_ANIM_WALK_START = 1, ACR_GEN1_ANIM_WALK = 2,
    ACR_GEN1_ANIM_STRAFE_L_START = 8, ACR_GEN1_ANIM_STRAFE_L = 9,
    ACR_GEN1_ANIM_STRAFE_R_START = 11, ACR_GEN1_ANIM_STRAFE_R = 12,
    ACR_GEN1_ANIM_BACK_START = 14, ACR_GEN1_ANIM_BACK = 15,
    ACR_GEN1_ANIM_JUMP = 22,
    ACR_GEN1_ANIM_COUNT = 64
};

typedef struct acr_gen1_input {
    int8_t  move;     /* +1 forward, -1 back */
    int8_t  turn;     /* +1 right, -1 left */
    int8_t  strafe;   /* +1 right, -1 left */
    uint8_t boost;    /* booster button */
} acr_gen1_input;

/* Everything fixed for one AC during a match. */
typedef struct acr_gen1_ac_def {
    acr_gen1_stats  stats;
    acr_gen1_motion motion;
    const acr_gen1_root_track *anim[ACR_GEN1_ANIM_COUNT];   /* NULL = missing */
    uint16_t landing_steps;   /* recovery after touching down (measured: about 20) */
    uint8_t  no_boost_hop;    /* caterpillars: re-pressing boost while sliding does not jump */
} acr_gen1_ac_def;

enum { ACR_GEN1_GROUND, ACR_GEN1_CROUCH, ACR_GEN1_AIR, ACR_GEN1_LANDING };

typedef struct acr_gen1_ac {
    int32_t  x, y, z;
    int32_t  vx, vy, vz;
    uint16_t yaw;
    int16_t  turn_rate;
    int32_t  energy;
    uint8_t  drained;          /* ran out: no boosting until back to the red zone */
    uint8_t  boost_was_down;   /* booster button last step, to see a fresh press */
    uint8_t  state;
    uint16_t timer;
    uint8_t  anim;
    uint16_t anim_frame;
    uint16_t span_left;
    acr_gen1_stepper step[3];
} acr_gen1_ac;

void acr_gen1_ac_spawn(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int32_t x, int32_t z, uint16_t yaw);
void acr_gen1_ac_step(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in);

/* sin(a) * 4096 for a = 0..4095 (a full turn), from a fixed table. */
int32_t acr_gen1_sin(uint16_t a);
int32_t acr_gen1_cos(uint16_t a);

#endif
