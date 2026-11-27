/* Armored Core 2 (USA, SLUS-200.14): AC part models out of AC2DATA.BIN, and
 * one assembled test AC for AC TEST (Gen 2 proof of concept, 2026-11-27).
 * Format notes: docs/research/gen2-models.md.
 *
 *   <out>/gen2/ac2/parts/<kind>/NNNN.obj   every frame part and arm weapon,
 *                                          in Gen 1 model space (see below)
 *   <out>/gen2/ac2/test-ac/ac.obj, ac.mtl  the test AC, assembled, feet on 0
 *   <out>/gen2/ac2/test-ac/build.toml      its parts, scale and pivots
 *
 * Model space as Gen 1's export: x, y up, z forward (AC2 has y down and
 * faces -z, so y and z are negated), scaled by ACR_AC2_SCALE (see its note). */
#ifndef ACR_MODELS_AC2_H
#define ACR_MODELS_AC2_H

#include <stddef.h>
#include <stdint.h>

#include "acr_disc.h"

/* AC2 model units -> Gen 1 model units. Default: the same overall height.
 * A Gen 1 AC is 212 world units tall (its body height, which the camera
 * also uses) = 848 model units; the AC2 test AC is 10.16 AC2 units from
 * sole to top, so 848 / 10.16 = 83.5. (Matching the hip joint instead,
 * 462 / 7.2 = 64, makes Gen 2 ACs a quarter shorter: AC2's legs are longer
 * for its height.) Gen 2's canonical size relative to Gen 1 is not settled
 * yet: this is the one number to change (docs/research/gen2-models.md). */
#define ACR_AC2_SCALE 83.5f

/* A part model file's bytes -> triangles (x y z, nx ny nz, u v per corner),
 * already in Gen 1 model space. Returns the triangle count, 0 if `data` is
 * not a part model. `tri` may be NULL to count; `max` caps what is written.
 * `pivot` (may be NULL) gets each mesh's joint position, up to `max_pivots`. */
typedef struct acr_ac2_tri { float p[3][3], n[3][3], uv[3][2]; uint8_t mesh; } acr_ac2_tri;
/* `offset` (AC2 units, may be NULL) moves every mesh (hand weapons onto a hand);
 * `pivot` comes back in AC2 units (y down, facing -z), offset included. */
uint32_t acr_ac2_model_tris(const uint8_t *data, size_t bytes, const float offset[3], acr_ac2_tri *tri, uint32_t max,
                            float (*pivot)[3], int max_pivots, int *meshes);

/* Whether the disc is Armored Core 2 USA: its boot program SLUS_200.14. */
int acr_disc_is_ac2(acr_disc *disc);

/* Reads AC2DATA.BIN off the disc and writes the files above. */
int acr_export_ac2_models(acr_disc *disc, const char *out_dir, uint32_t *parts, char *err, size_t err_size);

#endif
