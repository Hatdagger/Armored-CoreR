/* Fight sounds from FDAT 205 (acr_sfx_moa.h). The bank layout: "pBAV"
 * header (32 bytes), 128 program records (16 bytes), 16 tone records (32
 * bytes) per program, a table of 256 sample sizes (u16 x 8 bytes), then the
 * sample data. In MoA's bank the data starts 4 bytes after the size table,
 * and sample 0's size is the unused slot 0. */
#include "acr_sfx_moa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VAB_RATE 22050

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

size_t acr_moa_vag_decode(const uint8_t *data, size_t bytes, int16_t *out, size_t max_frames,
                          long *loop_start) {
    static const int f0[5] = { 0, 60, 115, 98, 122 }, f1[5] = { 0, 0, -52, -55, -60 };
    int s1 = 0, s2 = 0;
    size_t n = 0;
    long loop = -1;
    int repeats = 0;
    for (size_t b = 0; b + 16 <= bytes; b += 16) {
        const int shift = data[b] & 15, flags = data[b + 1];
        int filter = data[b] >> 4;
        if (filter > 4) filter = 0;
        if ((flags & 4) && loop < 0) loop = (long)n;
        if (flags & 2) repeats = 1;
        for (int j = 0; j < 28 && n < max_frames; ++j) {
            int nib = (data[b + 2 + j / 2] >> ((j & 1) * 4)) & 15;
            if (nib >= 8) nib -= 16;
            int x = shift <= 12 ? (nib * 4096) >> shift : 0;
            x += (s1 * f0[filter] + s2 * f1[filter] + 32) >> 6;
            if (x > 32767) x = 32767;
            if (x < -32768) x = -32768;
            out[n++] = (int16_t)x;
            s2 = s1;
            s1 = x;
        }
        if ((flags & 1) && b > 0) break;   /* end (block 0 is the silent lead-in) */
    }
    /* A loop runs from its start flag, or from the top when every block
     * carries the repeat flag without one. */
    if (loop_start) *loop_start = repeats ? (loop < 0 ? 0 : loop) : -1;
    return n;
}

static int write_wav(const char *path, const int16_t *pcm, size_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    const uint32_t data = (uint32_t)(frames * 2);
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    const uint32_t v[] = { 36 + data, 0, 16, 0, VAB_RATE, VAB_RATE * 2, 0, data };
    for (int i = 0; i < 4; ++i) h[4 + i] = (uint8_t)(v[0] >> (8 * i));
    memcpy(h + 8, "WAVEfmt ", 8);
    for (int i = 0; i < 4; ++i) h[16 + i] = (uint8_t)(v[2] >> (8 * i));
    h[20] = 1; h[21] = 0;            /* PCM */
    h[22] = 1; h[23] = 0;            /* mono */
    for (int i = 0; i < 4; ++i) h[24 + i] = (uint8_t)(v[4] >> (8 * i));
    for (int i = 0; i < 4; ++i) h[28 + i] = (uint8_t)(v[5] >> (8 * i));
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    for (int i = 0; i < 4; ++i) h[40 + i] = (uint8_t)(v[7] >> (8 * i));
    int ok = fwrite(h, 1, 44, f) == 44;
    for (size_t i = 0; i < frames && ok; ++i) {
        const uint8_t s[2] = { (uint8_t)(pcm[i] & 255), (uint8_t)((uint16_t)pcm[i] >> 8) };
        ok = fwrite(s, 1, 2, f) == 2;
    }
    return fclose(f) == 0 && ok;
}

static uint8_t *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = size > 0 ? malloc((size_t)size) : NULL;
    if (d && fread(d, 1, (size_t)size, f) != (size_t)size) { free(d); d = NULL; }
    fclose(f);
    *n = d ? (size_t)size : 0;
    return d;
}

static int export_bank(const uint8_t *d, size_t n, size_t base, const char *out_dir) {
    if (base + 32 > n) return 1;
    const unsigned progs = le16(d + base + 18), samples = le16(d + base + 22);
    const size_t sizes = base + 32 + 128 * 16 + (size_t)progs * 16 * 32;
    if (progs == 0 || progs > 128 || samples == 0 || samples > 254 || sizes + 512 + 4 > n) return 1;
    char path[1200];
    snprintf(path, sizeof path, "%s/sounds.toml", out_dir);
    FILE *toml = fopen(path, "w");
    if (!toml) return 0;
    fprintf(toml, "# Master of Arena's shared fight sounds (FDAT 205), written by acr-import.\n"
                  "# frames at 22050 a second; loop_start = -1: plays once.\n");
    size_t pos = sizes + 512 + 4 + (size_t)le16(d + sizes) * 8;   /* after slot 0 */
    int ok = 1;
    for (unsigned v = 1; v <= samples && ok; ++v) {
        const size_t len = (size_t)le16(d + sizes + 2 * v) * 8;
        if (pos + len > n) break;
        const size_t cap = len / 16 * 28 + 28;
        int16_t *pcm = malloc(cap * sizeof *pcm);
        if (!pcm) { ok = 0; break; }
        long loop = -1;
        const size_t frames = acr_moa_vag_decode(d + pos, len, pcm, cap, &loop);
        snprintf(path, sizeof path, "%s/%02u.wav", out_dir, v);
        ok = write_wav(path, pcm, frames);
        free(pcm);
        fprintf(toml, "\n[[sound]]\nn = %u\nframes = %lu\nloop_start = %ld\n", v, (unsigned long)frames, loop);
        pos += len;
    }
    return fclose(toml) == 0 && ok;
}

int acr_export_moa_sfx(const char *unpacked_dir, const char *out_dir, char *err, size_t err_size) {
    char path[1200];
    snprintf(path, sizeof path, "%s/GG/COM/FDAT/205.bin", unpacked_dir);
    size_t n = 0;
    uint8_t *d = read_all(path, &n);
    if (!d) return 1;     /* not there: the engine stays silent */
    size_t base = 0;
    while (base + 4 <= n && memcmp(d + base, "pBAV", 4)) ++base;
    const int ok = base + 4 <= n ? export_bank(d, n, base, out_dir) : 1;
    free(d);
    if (!ok && err) snprintf(err, err_size, "could not write the fight sounds in %s", out_dir);
    return ok;
}
