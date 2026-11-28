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

/* Every other picture in RTIM 61 is a battle effect (muzzle glows,
 * fireballs, smoke, fire, sparks, shot sprites, lens flare, ...): written
 * one PNG per picture and palette row, in the order they sit in the entry
 * (docs/research/gen1-effects.md). Colour 0 (0x0000) is see-through, as
 * the PS1 draws it. */
/* Where each exported picture sits in VRAM, for the effect atlas below. */
typedef struct fx_pic { unsigned index, vx, vy, words, h, bpp, cx, cy, rows; } fx_pic;
#define FX_PICS 96
static fx_pic g_pics[FX_PICS];
static unsigned g_npics;

static int export_fx(const uint8_t *d, size_t n, const char *out_dir) {
    char path[1200];
    g_npics = 0;
    size_t o = 0;
    unsigned index = 0;
    int ok = 1;
    while (ok && o + 8 <= n && u32(d + o) == 0x10) {
        const uint32_t flags = u32(d + o + 4);
        o += 8;
        const uint8_t *clut = NULL;
        unsigned cw = 0, ch = 0;
        if (flags & 8) {
            if (o + 12 > n) break;
            const uint32_t clen = u32(d + o);
            if (clen < 12 || o + clen > n) break;
            cw = u16(d + o + 8);
            ch = u16(d + o + 10);
            if ((size_t)cw * ch * 2 > clen - 12) break;
            clut = d + o + 12;
            o += clen;
        }
        if (o + 12 > n) break;
        const uint32_t len = u32(d + o);
        if (len < 12 || o + len > n) break;
        const unsigned vx = u16(d + o + 4), bpp = flags & 3;
        const unsigned words = u16(d + o + 8), h = u16(d + o + 10);
        const uint8_t *px = d + o + 12;
        o += len;
        const unsigned this_index = index++;
        if (vx >= 832 || !clut || bpp > 1) continue;            /* the HUD and text sheets */
        const unsigned w = bpp == 0 ? words * 4u : words * 2u, colours = bpp == 0 ? 16u : 256u;
        if (cw < colours || (size_t)words * 2 * h > len - 12) continue;
        if (g_npics < FX_PICS) {
            const uint8_t *ch_ = d + o - len;   /* this picture's header */
            fx_pic *pc = &g_pics[g_npics++];
            pc->index = this_index; pc->vx = vx; pc->vy = u16(ch_ + 6); pc->words = words; pc->h = h; pc->bpp = bpp;
            pc->cx = u16(clut - 12 + 4); pc->cy = u16(clut - 12 + 6); pc->rows = ch;
        }
        uint8_t *pixels = malloc((size_t)w * h);
        if (!pixels) return 0;
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x)
                pixels[y * w + x] = bpp == 0 ? (uint8_t)((px[y * words * 2 + x / 2] >> ((x & 1) * 4)) & 15)
                                             : px[y * words * 2 + x];
        for (unsigned row = 0; row < ch && ok; ++row) {
            uint8_t pal[768], alpha[256];
            memset(pal, 0, sizeof pal);
            memset(alpha, 0, sizeof alpha);
            for (unsigned c = 0; c < colours; ++c) {
                const uint16_t v = u16(clut + (row * cw + c) * 2);
                const unsigned r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
                pal[3 * c] = (uint8_t)(r << 3 | r >> 2);
                pal[3 * c + 1] = (uint8_t)(g << 3 | g >> 2);
                pal[3 * c + 2] = (uint8_t)(b << 3 | b >> 2);
                alpha[c] = v ? 255 : 0;
            }
            if (row) snprintf(path, sizeof path, "%s/fx-%02u-%u.png", out_dir, this_index, row);
            else snprintf(path, sizeof path, "%s/fx-%02u.png", out_dir, this_index);
            ok = acr_write_png8(path, w, h, pixels, pal, alpha);
        }
        free(pixels);
    }
    return ok;
}

/* The VS engine's effect atlas (FDAT 209, 0x80091A28: 297 x 12 bytes:
 * clut, tpage, u, v, w - 1, h - 1, r, g, b), written as fx/sprites.txt:
 * for each sprite the picture file it lies in, its rectangle there in
 * texels and its colour (128 = 1.0). docs/feel/projectiles.md. */
static int export_atlas(const char *unpacked_dir, const char *out_dir) {
    char path[1200];
    snprintf(path, sizeof path, "%s/GG/COM/FDAT/209.bin", unpacked_dir);
    size_t n = 0;
    uint8_t *vs = read_all(path, &n);
    if (!vs) return 1;
    const size_t at = 0x80091A28u - 0x80050DD8u;
    int ok = 1;
    if (at + 297 * 12 <= n) {
        snprintf(path, sizeof path, "%s/sprites.txt", out_dir);
        FILE *f = fopen(path, "wb");
        ok = f != NULL;
        if (f) {
            fprintf(f, "# Effect atlas (VS 0x80091A28), from your disc by acr-import.\n"
                       "# sprite picture-file u v w h r g b  (texels in that picture; colour 128 = 1.0)\n");
            for (unsigned i = 0; i < 297; ++i) {
                const uint8_t *e = vs + at + 12 * i;
                const unsigned clut = u16(e), tp = u16(e + 2);
                const unsigned tx = (tp & 15) * 64, ty = ((tp >> 4) & 1) * 256, mode = (tp >> 7) & 3;
                const unsigned per = mode == 0 ? 4 : 2;           /* texels per VRAM word */
                const unsigned cx = (clut & 63) * 16, cy = clut >> 6;
                const unsigned u = e[4], v = e[5], w = e[6] + 1u, h = e[7] + 1u;
                for (unsigned k = 0; k < g_npics; ++k) {
                    const fx_pic *pc = &g_pics[k];
                    const unsigned ppw = pc->bpp == 0 ? 4 : 2;
                    if (ppw != per) continue;
                    const unsigned wx = tx + u / per, wy = ty + v;
                    if (wx < pc->vx || wx >= pc->vx + pc->words || wy < pc->vy || wy >= pc->vy + pc->h) continue;
                    unsigned row = 0;
                    if (cx == pc->cx && cy >= pc->cy && cy < pc->cy + pc->rows) row = cy - pc->cy;
                    char file[32];
                    if (row) snprintf(file, sizeof file, "fx-%02u-%u.png", pc->index, row);
                    else snprintf(file, sizeof file, "fx-%02u.png", pc->index);
                    fprintf(f, "%u %s %u %u %u %u %u %u %u\n", i, file, (tx - pc->vx) * per + u, ty + v - pc->vy, w, h,
                            e[8], e[9], e[10]);
                    break;
                }
            }
            ok = fclose(f) == 0;
        }
    }
    free(vs);
    return ok;
}

int acr_export_moa_fx(const char *unpacked_dir, const char *out_dir, char *err, size_t err_size) {
    char path[1200];
    snprintf(path, sizeof path, "%s/GG/COM/RTIM/061.bin", unpacked_dir);
    size_t n = 0;
    uint8_t *d = read_all(path, &n);
    if (!d) return 1;     /* not there: the engine draws plain stand-ins */
    int ok = export_fx(d, n, out_dir);
    free(d);
    if (ok) ok = export_atlas(unpacked_dir, out_dir);
    if (!ok && err) snprintf(err, err_size, "could not write the effect pictures in %s", out_dir);
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
