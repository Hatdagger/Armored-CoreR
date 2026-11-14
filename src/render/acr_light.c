#include "acr_light.h"

/* intensity = ambient + sum(max(0, -dir . n) * colour); result = surface x
 * intensity / 128, limited to 255 (the GTE's colour saturation). */
void acr_light_color(const acr_lighting *l, int32_t nx, int32_t ny, int32_t nz,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t out[3]) {
    int32_t ir = l->ambient_r, ig = l->ambient_g, ib = l->ambient_b;
    for (int i = 0; i < l->count && i < 3; ++i) {
        const acr_light_dir *d = &l->light[i];
        int32_t dot = -(d->dx * nx + d->dy * ny + d->dz * nz) / 4096;
        if (dot <= 0) continue;
        ir += d->r * dot / 4096;
        ig += d->g * dot / 4096;
        ib += d->b * dot / 4096;
    }
    const int32_t cr = r * ir / 128, cg = g * ig / 128, cb = b * ib / 128;
    out[0] = (uint8_t)(cr > 255 ? 255 : cr);
    out[1] = (uint8_t)(cg > 255 ? 255 : cg);
    out[2] = (uint8_t)(cb > 255 ? 255 : cb);
}

void acr_light_default(acr_lighting *l) {
    l->ambient_r = l->ambient_g = l->ambient_b = 56;
    l->count = 2;
    /* key: travelling down and slightly forward (world y up) */
    l->light[0] = (acr_light_dir){ -1200, -3600, -1600, 110, 108, 104 };
    l->light[1] = (acr_light_dir){ 2000, -1000, 3400, 40, 44, 52 };
}
