/* Armored Core 2 (USA, SLUS-200.14): AC part models out of AC2DATA.BIN, and
 * one assembled test AC for AC TEST (Gen 2, 2026-11-27).
 * Format notes: docs/research/gen2-models.md, gen2-archive.md.
 *
 *   <out>/gen2/ac2/parts/<kind>/NNNN.obj, .mtl  every part, Gen 1 model space
 *   <out>/gen2/ac2/parts/<kind>/NNNN-K.png      its textures (from the shop package)
 *   <out>/gen2/ac2/parts/<kind>/NNNN.glb        the part as glTF: a node per mesh
 *                                               (pivots), textures, markers
 *   <out>/gen2/ac2/test-ac/ac.obj, ac.mtl, *.png  the test AC, assembled, feet on 0
 *   <out>/gen2/ac2/test-ac/ac.glb               the same as one glTF, weapon under the hand
 *   <out>/gen2/ac2/test-ac/build.toml           its parts, scale and size
 *
 * Model space (OBJ) as Gen 1's export: x, y up, z forward (AC2 has y down and
 * faces -z, so y and z are negated), scaled by ACR_AC2_SCALE. The glTF files
 * use the same axes in AC2 units (an AC is about 10 tall). */
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
 * not a part model (the header must be at 0: files read with the corrected
 * offsets, or split by tools/gen2/split_bin.py). `tri` may be NULL to count;
 * `max` caps what is written. `pivot` (may be NULL) gets each mesh's joint
 * position, up to `max_pivots`.
 * tex0: the GS TEX0 the strip was drawn with (0 = untextured); the part's
 * shop package holds the texture with the same TEX0. marker: an untextured
 * lone triangle, a locator (muzzle, mount, foot), not geometry. */
typedef struct acr_ac2_tri {
    float p[3][3], n[3][3], uv[3][2];
    uint64_t tex0;
    uint8_t mesh, marker;
} acr_ac2_tri;
/* `offset` (AC2 units, may be NULL) moves every mesh (hand weapons onto a hand);
 * `pivot` comes back in AC2 units (y down, facing -z), offset included. */
uint32_t acr_ac2_model_tris(const uint8_t *data, size_t bytes, const float offset[3], acr_ac2_tri *tri, uint32_t max,
                            float (*pivot)[3], int max_pivots, int *meshes);

/* One texture of a shop (F block) JOIN package, as palette indices. */
typedef struct acr_ac2_tex {
    uint64_t tex0;
    unsigned w, h;
    uint8_t *pixels;            /* w*h indices (malloc'd) */
    uint8_t rgb[256 * 3], alpha[256];
} acr_ac2_tex;
/* The textures in a JOIN package's sections. Returns the count (<= max);
 * free each with acr_ac2_tex_free. */
int acr_ac2_package_textures(const uint8_t *pkg, size_t bytes, acr_ac2_tex *out, int max);
void acr_ac2_tex_free(acr_ac2_tex *t);

/* Whether the disc is Armored Core 2 USA: its boot program SLUS_200.14. */
int acr_disc_is_ac2(acr_disc *disc);

/* Reads AC2DATA.BIN off the disc (file starts corrected: table offset + 32 KB,
 * checked per file) and writes the files above. */
int acr_export_ac2_models(acr_disc *disc, const char *out_dir, uint32_t *parts, char *err, size_t err_size);

/* The same from tools/gen2/split_bin.py's output: `split_dir` holds
 * <group>/NNNNN.bin (B/00301.bin, F/03400.bin...) or NNNNN.bin. */
int acr_export_ac2_models_split(const char *split_dir, const char *out_dir, uint32_t *parts, char *err,
                                size_t err_size);

#endif
