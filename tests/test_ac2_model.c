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
    CHECK(t[0].tex0 == 0 && t[0].marker == 1);   /* untextured lone triangle: a locator */
    {   /* a GIF A+D packet to VU 256 sets TEX0_1 for the strips after it */
        static unsigned char e[0x400];
        memcpy(e, d, sizeof e);
        put32(e, 4, 0x300);
        const size_t a = 0x80;
        memmove(e + a + 36, e + a, 0x100);          /* the strip moves 36 bytes on */
        put32(e, a, 0x6C020100u);           /* UNPACK V4-32, 2 qwords, to address 256 */
        put32(e, a + 4, 0x8001);            /* GIF tag (A+D) */
        put32(e, a + 20, 0x1234); put32(e, a + 24, 0x5);   /* TEX0 value (low, high) */
        put32(e, a + 28, 0x06);             /* register 0x06 = TEX0_1 */
        const unsigned m = acr_ac2_model_tris(e, sizeof e, NULL, t, 4, NULL, 0, NULL);
        CHECK(m == 1 && t[0].tex0 == 0x500001234ull && t[0].marker == 0);
    }
    {   /* a shop package with one 4x4 4-bit texture */
        static unsigned char j[0x200];
        memset(j, 0, sizeof j);
        memcpy(j, "JOIN", 4);
        for (int k = 0; k < 8; ++k) put32(j, 0x34 + 4 * k, 0xFFFFFFFFu);
        put32(j, 0x14, 0x100); put32(j, 0x34, 0x60);  /* section 0 */
        const size_t h = 0x60;
        put32(j, h, 8 + 64 + 0x20); put32(j, h + 4, 8); put32(j, h + 8, 1);
        const unsigned long long t0 = (0x14ull << 20) | (2ull << 26) | (2ull << 30);
        put32(j, h + 16, (unsigned)t0); put32(j, h + 20, (unsigned)(t0 >> 32));
        j[h + 0x20] = 0x21;                 /* texels 1, 2 */
        j[h + 0x28 + 4 + 0] = 200; j[h + 0x28 + 4 + 3] = 0x80;   /* palette 1: red-ish, opaque */
        acr_ac2_tex tx[2];
        const int nt = acr_ac2_package_textures(j, sizeof j, tx, 2);
        CHECK(nt == 1);
        if (nt == 1) {
            CHECK(tx[0].w == 4 && tx[0].h == 4 && tx[0].tex0 == t0);
            CHECK(tx[0].pixels[0] == 1 && tx[0].pixels[1] == 2);
            CHECK(tx[0].rgb[3] == 200 && tx[0].alpha[1] == 255);
            acr_ac2_tex_free(&tx[0]);
        }
    }
    put32(d, 0, 0);                      /* not a model */
    CHECK(acr_ac2_model_tris(d, sizeof d, NULL, t, 4, NULL, 0, NULL) == 0);
    if (!fails) printf("ac2 model: ok\n");
    return fails != 0;
}
