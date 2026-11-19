#include "acr_hud_moa.h"
#include "acr_models_moa.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RTIM 61 is loaded for every arena fight. Among its pictures are two 4-bit
 * 256 x 256 sheets with four palette rows each, where row n has white in
 * every colour whose bit n is set: four one-colour pictures in one sheet.
 *   VRAM (896, 256): the HUD (gauge, digits, radar, compass, messages, ...)
 *   VRAM (832, 256): text (letters, digits, kana and kanji), layer 0 only */

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | (uint32_t)u16(p + 2) << 16; }

static uint8_t *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    *size = d ? (size_t)n : 0;
    return d;
}

/* Finds the 4-bit TIM whose pixels go to VRAM (x, y); its pixel data and
 * size in texels. */
static const uint8_t *find_tim4(const uint8_t *d, size_t n, unsigned x, unsigned y, unsigned *w, unsigned *h) {
    size_t o = 0;
    while (o + 8 <= n && u32(d + o) == 0x10) {
        const uint32_t flags = u32(d + o + 4);
        o += 8;
        if (flags & 8) {
            if (o + 4 > n) return NULL;
            o += u32(d + o);
        }
        if (o + 12 > n) return NULL;
        const uint32_t len = u32(d + o);
        if (len < 12 || o + len > n) return NULL;
        if ((flags & 3) == 0 && u16(d + o + 4) == x && u16(d + o + 6) == y) {
            *w = u16(d + o + 8) * 4u;
            *h = u16(d + o + 10);
            if ((size_t)*w / 2 * *h > len - 12) return NULL;
            return d + o + 12;
        }
        o += len;
    }
    return NULL;
}

static int write_layer(const char *path, const uint8_t *px, unsigned w, unsigned h, unsigned bit) {
    uint8_t *pixels = malloc((size_t)w * h);
    if (!pixels) return 0;
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const unsigned idx = (px[(y * w + x) / 2] >> ((x & 1) * 4)) & 15;
            pixels[y * w + x] = (uint8_t)((idx >> bit) & 1);
        }
    uint8_t pal[768], alpha[256];
    memset(pal, 0, sizeof pal);
    memset(alpha, 0, sizeof alpha);
    pal[3] = pal[4] = pal[5] = 255;
    alpha[1] = 255;
    const int ok = acr_write_png8(path, w, h, pixels, pal, alpha);
    free(pixels);
    return ok;
}

int acr_export_moa_hud(const char *unpacked_dir, const char *out_dir, char *err, size_t err_size) {
    char path[1200];
    snprintf(path, sizeof path, "%s/GG/COM/RTIM/061.bin", unpacked_dir);
    size_t n = 0;
    uint8_t *d = read_all(path, &n);
    if (!d) return 1;     /* not there: no HUD pictures, the engine draws without them */
    unsigned w, h;
    const uint8_t *hud = find_tim4(d, n, 896, 256, &w, &h);
    int ok = 1;
    if (hud && w == 256 && h == 256) {
        for (unsigned b = 0; b < 4 && ok; ++b) {
            snprintf(path, sizeof path, "%s/hud-%u.png", out_dir, b);
            ok = write_layer(path, hud, w, h, b);
        }
    }
    const uint8_t *font = ok ? find_tim4(d, n, 832, 256, &w, &h) : NULL;
    if (font && w == 256 && h == 256) {
        snprintf(path, sizeof path, "%s/font.png", out_dir);
        ok = write_layer(path, font, w, h, 0);
    }
    free(d);
    if (!ok && err) snprintf(err, err_size, "could not write the HUD pictures in %s", out_dir);
    return ok;
}
