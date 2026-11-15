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
    uint8_t boost_tapped; /* let go and pressed again since the last step (a quick tap) */
    int8_t  look;     /* +1 tilt the view up (L2), -1 down (R2), 2 both: back to level */
} acr_gen1_input;

/* View tilt, measured in the recomp (VS): L2 / R2 tilt the aim up / down
 * by up to ~66 degrees at ~80 degrees a second and it stays where it is let
 * go; L2+R2 together bring it back level in ~0.2 s. Facing units (4096 a
 * circle), per Gen 1 step (22 a second). */
#define ACR_GEN1_PITCH_MAX        751
#define ACR_GEN1_PITCH_RATE       41
#define ACR_GEN1_PITCH_LEVEL_RATE 170

/* Everything fixed for one AC during a match. */
typedef struct acr_gen1_ac_def {
    acr_gen1_stats  stats;
    acr_gen1_motion motion;
    const acr_gen1_root_track *anim[ACR_GEN1_ANIM_COUNT];   /* NULL = missing */
    uint16_t landing_steps;   /* recovery after a hard landing (measured: about 20 after a hop
                                 that touched down at 5,000+) */
    /* Running faster than Gen 1's 22 steps a second (gen1_rate / step_rate):
     * movement speeds stay per step (so the game runs faster), while energy,
     * turning and the slide after a ground boost are kept per second.
     * 0 / 0 = Gen 1's own pace. */
    uint16_t step_rate, gen1_rate;
    uint8_t  no_boost_hop;    /* caterpillars: re-pressing boost while sliding does not jump */
} acr_gen1_ac_def;

enum { ACR_GEN1_GROUND, ACR_GEN1_CROUCH, ACR_GEN1_AIR, ACR_GEN1_LANDING };

typedef struct acr_gen1_ac {
    int32_t  x, y, z;
    int32_t  vx, vy, vz;
    uint16_t yaw;
    int16_t  turn_rate;        /* facing units per step at Gen 1's pace */
    int32_t  yaw_fine;         /* facing in 1/256 units (lets faster step rates turn smoothly) */
    int32_t  turn_fine;        /* turn speed in 1/256 units per step */
    int32_t  pitch_fine;       /* view tilt in 1/256 facing units, + = up */
    int16_t  pitch;            /* view tilt in facing units (pitch_fine >> 8) */
    uint16_t coast;            /* steps left at full speed after a ground boost ends */
    int32_t  energy;
    uint8_t  drained;          /* ran out: no boosting until the bar is full again */
    uint8_t  boost_was_down;   /* booster button last step, to see a fresh press */
    uint8_t  state;
    uint16_t timer;
    int32_t  vy_at_touchdown;
    uint8_t  anim;
    uint16_t anim_frame;
    uint16_t span_left;
    acr_gen1_stepper step[3];
    /* Walking at faster step rates: the walk still advances one Gen 1 tick
     * at a time (22 a second, measured), each tick's movement spread over
     * the steps it lasts, so the legs keep pace with the ground.
     * walk_phase counts toward the next tick in gen1_rate units out of
     * step_rate (0xFFFF = tick on the next step). */
    uint16_t walk_phase;
    int32_t  tick_mx, tick_mz;     /* this tick's movement, 1/64 units */
    int32_t  rem_mx, rem_mz;       /* remainders of the per-step shares */
} acr_gen1_ac;

/* How far into the current walk tick the AC is, 0..1 (0 at Gen 1's pace). */
float acr_gen1_walk_fraction(const acr_gen1_ac *ac, const acr_gen1_ac_def *def);

void acr_gen1_ac_spawn(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int32_t x, int32_t z, uint16_t yaw);
void acr_gen1_ac_step(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in);

/* sin(a) * 4096 for a = 0..4095 (a full turn), from a fixed table. */
int32_t acr_gen1_sin(uint16_t a);
int32_t acr_gen1_cos(uint16_t a);

#endif
