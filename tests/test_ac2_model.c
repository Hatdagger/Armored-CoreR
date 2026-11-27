/* Armored Core 2 part model decoding (tools/acr_import/acr_models_ac2.c) on a
 * hand-made one-triangle model: header, mesh entry, joint block, VIF UNPACK. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "acr_models_ac2.h"

static void put32(unsigned char *d, size_t at, unsigned v) { memcpy(d + at, &v, 4); }
static void putf(unsigned char *d, size_t at, float v) { memcpy(d + at, &v, 4); }

int main(void) {
    static unsigned char d[0x400];
    memset(d, 0, sizeof d);
    put32(d, 0, 1); put32(d, 4, 0x200); put32(d, 8, 0xFFFFFFFFu);   /* header */
    put32(d, 0x14, 0x80);                                            /* mesh 0's chain at 0x80 */
    putf(d, 0x20, 1.0f); putf(d, 0x24, -2.0f); putf(d, 0x28, 0.5f);  /* joint position (y down) */
    const size_t u = 0x80;
    put32(d, u, 0x6C0D0000u);            /* UNPACK V4-32, 13 qwords */
    put32(d, u + 4, 0x8003);             /* GIF tag: NLOOP 3 */
    const float p[3][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 } };
    for (int k = 0; k < 3; ++k) {
        const size_t v = u + 4 + 16 + 64u * (size_t)k;
        for (int c = 0; c < 3; ++c) putf(d, v + 4u * (size_t)c, p[k][c]);
        putf(d, v + 24, -1.0f);          /* normal (0, 0, -1): faces AC2's front (-z) */
    }
    acr_ac2_tri t[4];
    float piv[2][3];
    int meshes = 0;
    const unsigned n = acr_ac2_model_tris(d, sizeof d, NULL, t, 4, piv, 2, &meshes);
    int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
    CHECK(n == 1 && meshes == 1);
    CHECK(fabsf(piv[0][1] + 2.0f) < 1e-6f);
    /* model space: y up, z forward, scaled; the joint added */
    CHECK(fabsf(t[0].p[0][0] - 1.0f * ACR_AC2_SCALE) < 1e-3f);
    CHECK(fabsf(t[0].p[0][1] - 2.0f * ACR_AC2_SCALE) < 1e-3f);
    CHECK(fabsf(t[0].p[0][2] + 0.5f * ACR_AC2_SCALE) < 1e-3f);
    CHECK(t[0].n[0][2] > 0.99f);         /* now faces +z */
    {   /* winding follows the normals */
        float a[3], b[3];
        for (int q = 0; q < 3; ++q) { a[q] = t[0].p[1][q] - t[0].p[0][q]; b[q] = t[0].p[2][q] - t[0].p[0][q]; }
        CHECK(a[0] * b[1] - a[1] * b[0] > 0);
    }
    put32(d, 0, 0);                      /* not a model */
    CHECK(acr_ac2_model_tris(d, sizeof d, NULL, t, 4, NULL, 0, NULL) == 0);
    if (!fails) printf("ac2 model: ok\n");
    return fails != 0;
}
