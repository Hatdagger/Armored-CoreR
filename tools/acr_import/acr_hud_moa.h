#ifndef ACR_HUD_MOA_H
#define ACR_HUD_MOA_H

#include <stddef.h>

/* Master of Arena's battle HUD pictures (docs/research/gen1-hud.md), from
 * RTIM entry 61, as PNGs the engine reads:
 *
 *   <out>/hud-0.png .. hud-3.png  the HUD sheet (VRAM 896,256), one picture
 *                                 per layer: the 4-bit sheet holds four
 *                                 one-colour pictures, one per bit, and the
 *                                 game picks one with its palette row
 *   <out>/font.png                the text sheet (VRAM 832,256), layer 0
 *
 * White where drawn, see-through elsewhere; the game tints them as it draws.
 * Returns 0 with a message in `err` on failure. */
int acr_export_moa_hud(const char *unpacked_dir, const char *out_dir, char *err, size_t err_size);

/* The battle effect pictures from the same entry (everything but the two
 * sheets above): <out>/fx-NN.png per picture, NN its place in the entry,
 * and fx-NN-R.png for palette rows past the first. Colour 0 see-through.
 * docs/research/gen1-effects.md lists what each one is. */
int acr_export_moa_fx(const char *unpacked_dir, const char *out_dir, char *err, size_t err_size);

#endif
