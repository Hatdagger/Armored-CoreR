/* Gen 2 in a Gen 1 fight: an Armored Core 2 AC that moves by AC2's own rules
 * (acr_gen2_move.h, 60 frames a second) inside the Gen 1 battle, which steps
 * at its own rate and owns everything else (hits, AP, the AI, the HUD).
 *
 * Each Gen 1 step the link:
 *  - turns the fighter's Gen 1 controls into AC2 buttons (the AI's included),
 *  - runs as many AC2 frames as that step's share of a second holds,
 *  - keeps the AC2 body on the stage: the floor under it from the Gen 1 floor
 *    probe, walls and ceilings from the Gen 1 stage move (the game's own box),
 *  - and writes the result back into the Gen 1 AC as its position and facing,
 *    so hits, lock-on and the camera follow the Gen 2 body.
 *
 * Units and axes (docs/research/gen2-movement.md, "Scale"): 1 AC2 unit = 1 m =
 * 20.875 Gen 1 world units = 1,336 Gen 1 sim units (1/64 world). Both games
 * have y down. Gen 1 faces (sin, cos) at its yaw; AC2 faces -z at yaw 0 with
 * +x on its left, which is the same picture turned half round: Gen 1 x, z =
 * -(AC2 x, z), and the yaws (both growing to the right) are the same angle.
 *
 * Gen 2 PR (project "Gen 2 Research"). */
#ifndef ACR_GEN2_LINK_H
#define ACR_GEN2_LINK_H

#include <stdint.h>

#include "acr_gen2_move.h"
#include "acr_gen1_sim.h"
#include "acr_gen1_stage.h"

#define ACR_GEN2_WORLD_PER_UNIT 20.875f                 /* Gen 1 world units in one AC2 unit */
#define ACR_GEN2_SIM_PER_UNIT   (ACR_GEN2_WORLD_PER_UNIT * 64.0f)

typedef struct acr_gen2_link {
    acr_gen2_ac    ac;
    acr_gen2_stats stats;
    acr_gen2_gait  gait;
    const acr_gen2_tuning *tuning;
    int32_t frame_acc;      /* AC2 frames owed, in 1/rate parts */
    int32_t last_x, last_y, last_z;   /* where the link last put the Gen 1 AC */
    uint8_t synced;
    uint8_t ob_pending;     /* overboost pressed, not yet given to an AC2 frame */
    int32_t frames;         /* AC2 frames run since the last sync */
} acr_gen2_link;

/* The Armored Core 2 test AC (ZCX-F/ROOK, ZAN-414/SLA, ELN-STVM; the
 * "heavy two legs, test AC parts" build of the compile tests). */
extern const acr_gen2_build acr_gen2_test_build;
/* Its gait if the motion set is not to hand: B 453, heavy humanoid legs
 * group, core 0 arms 0 (gen2-movement.md, "Walking": 30 units in 64 frames). */
extern const acr_gen2_gait acr_gen2_test_gait;

void acr_gen2_link_init(acr_gen2_link *l, const acr_gen2_tuning *t, const acr_gen2_build *b,
                        const acr_gen2_gait *g);
/* Drops the body where the Gen 1 AC stands (spawn, round start, a teleport). */
void acr_gen2_link_sync(acr_gen2_link *l, const acr_gen1_ac *g1);

/* The AC2 buttons for a Gen 1 pilot's controls. */
acr_gen2_input acr_gen2_link_input(const acr_gen1_input *in, int overboost);

/* One Gen 1 step (in->overboost: pressed this step; it reaches the next
 * AC2 frame only) of step_rate a second: runs the AC2 frames it holds and
 * writes the body into *g1 (position, facing, the shown copies; velocity 0
 * so the Gen 1 rules leave it where it is). stage may be NULL: a flat floor
 * at 0, walls at +-arena_half world units. Re-syncs first if something else
 * moved the Gen 1 AC (more than a metre from where the link put it). */
void acr_gen2_link_step(acr_gen2_link *l, acr_gen1_ac *g1, const acr_gen2_input *in, int step_rate,
                        const acr_gen1_stage *stage, int32_t arena_half);

/* For the test HUD: speed in km/h as AC2's HUD shows it. */
float acr_gen2_link_kmh(const acr_gen2_link *l);

#endif
