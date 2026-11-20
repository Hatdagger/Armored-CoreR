#ifndef ACR_SFX_MOA_H
#define ACR_SFX_MOA_H

#include <stddef.h>
#include <stdint.h>

/* Master of Arena's shared fight sounds (FDAT entry 205, a "pBAV" sound
 * bank: 4 programs, 51 tones, 38 samples; docs/research/gen1-sound.md), as
 * WAV files the engine reads:
 *
 *   <out>/NN.wav      sample NN (01..38), 16-bit mono, 22,050 Hz (the rate
 *                     that sounds right by ear)
 *   <out>/sounds.toml one [[sound]] per sample: n, frames, loop_start (the
 *                     frame a looping sample repeats from; -1 = plays once)
 *
 * A disc without the entry writes nothing and still succeeds. Returns 0
 * with a message in `err` when files cannot be written. */
int acr_export_moa_sfx(const char *unpacked_dir, const char *out_dir, char *err, size_t err_size);

/* The menu bank's clicks (GG/MS/MENU_VAB.T), for Rogue Arena's menus:
 * <out>/menu-back.wav, menu-cursor.wav, menu-ok.wav, menu-equip.wav
 * (16-bit mono, 22,050 Hz). A disc without the bank writes nothing and
 * still succeeds. */
int acr_export_moa_menu_sfx(const char *unpacked_dir, const char *out_dir, char *err, size_t err_size);

/* Decodes one PS1 ADPCM sample (16-byte blocks, 28 frames each) until its
 * end flag. `out` holds up to `max_frames`; returns the frames written and
 * sets *loop_start to the loop's first frame (-1: no loop). */
size_t acr_moa_vag_decode(const uint8_t *data, size_t bytes, int16_t *out, size_t max_frames,
                          long *loop_start);

#endif
