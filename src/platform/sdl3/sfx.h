/* Fight sounds: Master of Arena's shared fight bank (FDAT 205, exported by
 * acr-import to <import>/sfx/NN.wav; which sample is which:
 * docs/research/gen1-sound.md), mixed in software and played through SDL.
 *
 * Presentation only, like fx.h: it watches the fight from outside (shots
 * appearing and ending, AP, boosting, locks, energy) and never changes it.
 * Sounds from either AC are placed by distance and side from the listening
 * AC; the warnings (perimeter, low AP, energy red zone, forced charging)
 * and the lock beeps are the listening AC's own, as the game's are.
 *
 * ACR_SFX_VOLUME=0..100 sets the volume (default 70; 0 = off).
 * ACR_SFX_LOG=1 prints each sound as it starts (for headless tests). */
#ifndef ACR_SFX_H
#define ACR_SFX_H

#include <stdint.h>

#include "../../gen1/acr_gen1_battle.h"

/* Loads the sounds and opens the audio device (once). 1 when sounds are
 * ready; 0 (silent) if the import has no sfx/ or there is no audio. */
int  sfx_open(const char *import_dir);
void sfx_close(void);

/* The HUD's weapon names for a fighter's weapon slots, as fx_set_weapons:
 * they pick each weapon's firing sound. Kept by pointer. */
void sfx_set_weapons(int fighter, const char (*types)[24], int count);

/* Forget every shot seen and stop every sound (a new round). */
void sfx_reset(void);

typedef struct sfx_listen {
    int view;            /* the AC whose ears and warnings these are */
    int red_zone;        /* its energy red zone (compiled stat) */
    int out_of_area;     /* it is outside the area's inner box */
    int round_over;      /* no warnings, no loops */
} sfx_listen;

/* Once a frame, after the fight has stepped. `now_ns` is a steady clock. */
void sfx_update(const acr_gen1_battle *b, const sfx_listen *l, uint64_t now_ns);

#endif
