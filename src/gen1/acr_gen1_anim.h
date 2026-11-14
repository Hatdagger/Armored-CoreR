/* Gen 1 root motion: how animations move the AC (walking, strafing, the
 * jump take-off). Tracks come from the player's own disc (acr-import writes
 * them to <out>/motion); nothing here is game data.
 *
 * A track is a list of keys: frame, and the root's displacement since the
 * animation began, in 1/4 world units. Between two keys the game spreads the
 * displacement over the frames in between with a small integer stepper
 * (EXE 0x800135DC, run each step by VS engine 0x80054FF8), so a walk of 249
 * over 2 frames moves 125 then 124. acr_gen1_stepper reproduces it exactly. */
#ifndef ACR_GEN1_ANIM_H
#define ACR_GEN1_ANIM_H

#include <stdint.h>

typedef struct acr_gen1_root_key {
    uint16_t frame;
    int16_t  x, y, z;
} acr_gen1_root_key;

typedef struct acr_gen1_root_track {
    uint16_t length;                 /* frames */
    uint16_t count;
    const acr_gen1_root_key *keys;
} acr_gen1_root_track;

/* The game's per-axis stepper: spreads `total` over `frames` steps. */
typedef struct acr_gen1_stepper {
    int16_t step, step_alt;          /* the two step sizes it alternates between */
    int16_t counter, dec, frames;
} acr_gen1_stepper;

void    acr_gen1_stepper_init(acr_gen1_stepper *s, int frames, int total);
int16_t acr_gen1_stepper_next(acr_gen1_stepper *s);

/* Key span that `frame` falls in: the displacement from the previous key
 * (or the start) to the next key, and the number of frames it covers.
 * Returns 0 past the end of the track. */
int acr_gen1_root_span(const acr_gen1_root_track *t, unsigned frame,
                       int32_t delta[3], unsigned *frames);

#endif
