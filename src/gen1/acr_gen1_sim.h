/* Gen 1 AC movement. The game's rules run once per Gen 1 tick (22 a second),
 * integer only, so every machine runs the same match and every step rate
 * moves exactly like the game in real time. Rules and numbers:
 * docs/research/gen1-movement.md.
 *
 * At faster step rates (def step_rate / gen1_rate) the ticks fall between
 * steps; the AC's game state (x, y, z, yaw ...) changes only at a tick, and
 * the shown state (sx, sy, sz, syaw) slides from the tick before to the
 * latest one, for drawing.
 *
 * Units: position in 1/64 world units (y up is negative, ground at 0),
 * speeds in 1/64 world units per tick, facing 0..4095 for a full turn. */
#ifndef ACR_GEN1_SIM_H
#define ACR_GEN1_SIM_H

#include <stdint.h>

#include "acr_gen1_stage.h"

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
    ACR_GEN1_ANIM_LAND = 36,
    ACR_GEN1_ANIM_COUNT = 64
};

typedef struct acr_gen1_input {
    int8_t  move;     /* +1 forward, -1 back */
    int8_t  turn;     /* +1 right, -1 left */
    int8_t  strafe;   /* +1 right, -1 left */
    uint8_t boost;    /* booster button */
    uint8_t boost_tapped; /* let go and pressed again since the last step (a quick tap) */
    int8_t  look;     /* +1 tilt the view up (L2), -1 down (R2), 2 both: back to level */
    uint8_t crouch;   /* crouched in the cannon stance (VS 0x8007E3E0): crouch turn rates,
                         sliding to a stop, no walking or boosting */
} acr_gen1_input;

/* View tilt, measured in the recomp (VS): L2 / R2 tilt the aim up / down
 * by up to ~66 degrees at ~80 degrees a second and it stays where it is let
 * go; L2+R2 together bring it back level in ~0.2 s. Facing units (4096 a
 * circle), per Gen 1 tick (22 a second). */
#define ACR_GEN1_PITCH_MAX        751
#define ACR_GEN1_PITCH_RATE       41
#define ACR_GEN1_PITCH_LEVEL_RATE 170

/* Everything fixed for one AC during a match. */
typedef struct acr_gen1_ac_def {
    acr_gen1_stats  stats;
    acr_gen1_motion motion;
    const acr_gen1_root_track *anim[ACR_GEN1_ANIM_COUNT];   /* NULL = missing */
    uint16_t landing_steps;   /* ticks of recovery after a hard landing (measured: about 20 after
                                 a hop that touched down at 5,000+) */
    /* Steps a second when the engine runs faster than Gen 1's 22 ticks a
     * second (step_rate, with gen1_rate = 22): the game's rules still run
     * 22 times a second, between steps. 0 / 0 = a tick every step. */
    uint16_t step_rate, gen1_rate;
    uint8_t  no_boost_hop;    /* caterpillars: re-pressing boost while sliding does not jump */
    uint8_t  no_jump;         /* caterpillars: no jump and no ground boost; the booster goes
                                 straight into the rising boost without a take-off kick */
    uint8_t  drive_walk;      /* four-legged and caterpillar legs: walking pushes the AC like
                                 the ground boost (VS 0x80085704), no root motion */
    /* Test: every landing lasts the landing animation's length (animation
     * 36's track), whatever the fall, as it would if the game's landing
     * state simply ends with its animation (0x8008389C). 0 = the stand-in
     * curve from landing_steps. */
    uint8_t  landing_from_anim;
    /* The arena (NULL = the open test floor at y = 0): its floors carry the
     * AC, its walls stop it (acr_gen1_stage.h; step heights are stand-ins). */
    const acr_gen1_stage *stage;
} acr_gen1_ac_def;

/* Arena stand-ins until the game's own rules are read: an AC walks up a
 * step this high, follows a floor dropping away by this much (slopes)
 * before it falls, and is a cylinder this wide (its hit cylinder). */
#define ACR_GEN1_STEP_UP   120
#define ACR_GEN1_STEP_DOWN 120

enum { ACR_GEN1_GROUND, ACR_GEN1_CROUCH, ACR_GEN1_AIR, ACR_GEN1_LANDING };

typedef struct acr_gen1_ac {
    /* The game's state after the latest tick: what the rules and the fight use. */
    int32_t  x, y, z;
    int32_t  vx, vy, vz;
    uint16_t yaw;
    int16_t  turn_rate;        /* facing units per tick */
    int32_t  pitch_fine;       /* view tilt in 1/256 facing units, + = up (moves every step) */
    int16_t  pitch;            /* view tilt in facing units (pitch_fine >> 8) */
    uint16_t coast;            /* ticks left at full speed after a ground boost ends */
    int32_t  energy;
    uint8_t  drained;          /* ran out: no boosting until the bar is full again */
    uint8_t  boost_was_down;   /* booster button at the latest tick, to see a fresh press */
    uint8_t  boost_released;   /* let go of since the latest tick (a tap between ticks) */
    uint8_t  state;
    uint16_t timer;
    int32_t  vy_at_touchdown;  /* fall speed at the latest touchdown */
    uint16_t landing_pause;    /* ticks the latest landing lasted (0: none) */
    uint8_t  anim;
    uint16_t anim_frame;
    uint16_t span_left;
    acr_gen1_stepper step[3];
    /* Ticks between steps (acr_gen1_ac_step): counts toward the next tick
     * in gen1_rate units out of step_rate. */
    uint16_t phase;
    /* The tick before the latest one, and what to draw: shown = previous +
     * (latest - previous) x show_num / show_den. */
    int32_t  px, py, pz;
    uint16_t pyaw;
    int32_t  sx, sy, sz;
    uint16_t syaw;
    uint16_t show_num, show_den;
} acr_gen1_ac;

/* How far the shown state is from the previous tick to the latest, 0..1
 * (1 when a tick runs every step). */
float acr_gen1_tick_fraction(const acr_gen1_ac *ac);

void acr_gen1_ac_spawn(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int32_t x, int32_t z, uint16_t yaw);

/* One engine step: the view tilt, then the Gen 1 ticks that fall in this
 * step (from def's rates and the AC's own phase), then the shown state.
 * Returns the number of ticks run. */
int acr_gen1_ac_step(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in);

/* The same with the ticks and the shown fraction given (the fight counts
 * ticks once for both ACs): `ticks` game ticks, then shown = num / den of
 * the way from the previous tick to the latest. */
void acr_gen1_ac_advance(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in,
                         int ticks, uint32_t num, uint32_t den);

/* One tick of the game's rules. */
void acr_gen1_ac_tick(acr_gen1_ac *ac, const acr_gen1_ac_def *def, acr_gen1_input in);

/* Moves or turns the AC outside the movement rules (blade lunge, knock-back,
 * walls), shown state included, so drawing stays smooth. */
void acr_gen1_ac_shift(acr_gen1_ac *ac, int32_t dx, int32_t dz);
void acr_gen1_ac_set_yaw(acr_gen1_ac *ac, uint16_t yaw);

/* sin(a) * 4096 for a = 0..4095 (a full turn), from a fixed table. */
int32_t acr_gen1_sin(uint16_t a);
int32_t acr_gen1_cos(uint16_t a);

#endif
