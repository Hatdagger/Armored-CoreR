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
 * Volumes come from SETTINGS > AUDIO (sfx_set_volume); ACR_SFX_VOLUME=0..100
 * sets both instead and wins over the settings (default 70; 0 = off).
 * ACR_SFX_LOG=1 prints each sound as it starts (for headless tests). */
#ifndef ACR_SFX_H
#define ACR_SFX_H

#include <stdint.h>

#include "../../gen1/acr_gen1_battle.h"

/* Loads the sounds and opens the audio device (once). 1 when sounds are
 * ready; 0 (silent) if the import has no sfx/ or there is no audio. The
 * menus call it too (their clicks), before any fight. */
int  sfx_open(const char *import_dir);
void sfx_close(void);

/* SETTINGS > AUDIO: fight sounds and menu / screen sounds, 0..1 each
 * (ignored when ACR_SFX_VOLUME is set). */
void sfx_set_volume(float fight, float menu);

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

/* ---- menu and screen sounds (not placed in the world) ---- */

enum {
    SFX_UI_BACK, SFX_UI_CURSOR, SFX_UI_OK, SFX_UI_EQUIP,   /* the menu bank's (sfx/menu-*.wav) */
    SFX_VS_SHOT, SFX_VS_SHOT_LAST, SFX_VS_DOOR,           /* Rogue Arena's VS screen (rogue_sound.c) */
    SFX_WIN_STING, SFX_WIN_TICK,                          /* its victory screen */
    SFX_UI_COUNT
};
/* Plays one, centred, at its own level. */
void sfx_ui(int id);
/* Stops one if it is playing. */
void sfx_ui_stop(int id);
/* Gives slot `id` a sound (22050 Hz mono float; sfx keeps the buffer). */
void sfx_set_ui(int id, float *pcm, int frames);
/* A fight bank sample (sfx/NN.wav) as loaded, or NULL. */
const float *sfx_sample(int n, int *frames);

#endif
