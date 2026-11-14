/* Lighting as the PS1 did it, on the CPU: an ambient colour plus up to
 * three directional lights, worked out per vertex (or per face) and
 * multiplied with the surface colour, in integers like the GTE's
 * normal-colour operations. No global illumination or shadows, so the look
 * stays the original's and every platform shows the same result. Stage
 * light settings are to be read from the game; these are the defaults
 * until then. */
#ifndef ACR_LIGHT_H
#define ACR_LIGHT_H

#include <stdint.h>

typedef struct acr_light_dir {
    int16_t dx, dy, dz;      /* direction the light travels, length 4096 */
    uint8_t r, g, b;         /* 0..255; 128 = full strength like the GTE's 1.0 */
} acr_light_dir;

typedef struct acr_lighting {
    uint8_t ambient_r, ambient_g, ambient_b;
    uint8_t count;
    acr_light_dir light[3];
} acr_lighting;

/* Lit colour for a surface colour and a unit normal (x 4096). */
void acr_light_color(const acr_lighting *l, int32_t nx, int32_t ny, int32_t nz,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t out[3]);

/* A default: soft ambient, a key light from above-front, a dim fill. */
void acr_light_default(acr_lighting *l);

#endif
