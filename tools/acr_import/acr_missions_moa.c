/* Master of Arena missions -> missions.toml and pictures. See
 * acr_missions_moa.h for where they live on the disc. */
#include "acr_missions_moa.h"
#include "acr_models_moa.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define acr_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define acr_mkdir(p) mkdir((p), 0755)
#endif

/* The mission table's layout: Master of Arena's (24 slots, 0-19 used,
 * 20-23 "Dummy") and Project Phantasma's (64 slots, 0-16 used; read
 * 2026-11-27, docs/research/recomp/phantasma/missions-import.md). */
typedef struct table_layout {
    unsigned slots, bytes, window, name, reward, engine, byte, pages;
} table_layout;
static const table_layout k_moa = { 20u, 1872u, 0u, 96u, 960u, 1056u, 1080u, 1104u };
static const table_layout k_pp  = { 17u, 4992u, 0u, 256u, 2560u, 2816u, 2880u, 2944u };

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | (uint32_t)u16(p + 2) << 16; }

static void set_err(char *err, size_t n, const char *what, const char *path) {
    if (err && n) snprintf(err, n, "%s%s%s", what, path ? ": " : "", path ? path : "");
}

static int make_dirs(const char *path) {
    char buf[1024];
    const size_t len = strlen(path);
    if (len >= sizeof buf) return 0;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i <= len; ++i) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            const char saved = buf[i];
            buf[i] = '\0';
            if (acr_mkdir(buf) != 0 && errno != EEXIST) return 0;
            buf[i] = saved;
        }
    }
    return 1;
}

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

static uint8_t *read_entry(const char *unpacked, const char *archive, unsigned k, size_t *size) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s/%03u.bin", unpacked, archive, k);
    return read_all(path, size);
}

/* A page's text up to its end mark ('>' a page, '/' the last, or a zero),
 * without carriage returns or the trailing line break. */
static void page_text(const uint8_t *d, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && d[i] && d[i] != '>' && d[i] != '/' && o + 1 < cap; ++i) {
        if (d[i] == '\r') continue;
        out[o++] = (d[i] >= 32 && d[i] < 127) || d[i] == '\n' ? (char)d[i] : '?';
    }
    while (o && (out[o - 1] == '\n' || out[o - 1] == ' ')) --o;
    out[o] = 0;
}

/* TOML basic string. */
static void put_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') fprintf(f, "\\%c", *s);
        else if (*s == '\n') fputs("\\n", f);
        else fputc(*s, f);
    }
    fputc('"', f);
}

/* The value after "key:" on a line of the header page ("" if missing). */
static void header_field(const char *text, const char *key, char *out, size_t cap) {
    out[0] = 0;
    const char *p = strstr(text, key);
    if (!p) return;
    p += strlen(key);
    while (*p == ' ') ++p;
    size_t o = 0;
    while (*p && *p != '\n' && o + 1 < cap) out[o++] = *p++;
    while (o && out[o - 1] == ' ') --o;
    out[o] = 0;
}

/* Finds the n-th TIM in an entry: bits per pixel, CLUT (colours), pixels. */
typedef struct tim_view {
    unsigned bpp, colours, w, h;
    const uint8_t *clut, *px;
} tim_view;

static int tim_at(const uint8_t *d, size_t n, unsigned want, tim_view *t) {
    size_t o = 0;
    for (unsigned k = 0; o + 8 <= n && u32(d + o) == 0x10; ++k) {
        const uint32_t flags = u32(d + o + 4);
        o += 8;
        memset(t, 0, sizeof *t);
        if (flags & 8) {
            if (o + 12 > n) return 0;
            const uint32_t len = u32(d + o);
            t->colours = u16(d + o + 8) * u16(d + o + 10);
            t->clut = d + o + 12;
            if (len < 12 || o + len > n) return 0;
            o += len;
        }
        if (o + 12 > n) return 0;
        const uint32_t len = u32(d + o);
        if (len < 12 || o + len > n) return 0;
        t->bpp = (flags & 3) == 0 ? 4 : (flags & 3) == 1 ? 8 : 16;
        const unsigned words = u16(d + o + 8);
        t->w = t->bpp == 4 ? words * 4 : t->bpp == 8 ? words * 2 : words;
        t->h = u16(d + o + 10);
        t->px = d + o + 12;
        if (k == want) return t->bpp != 16 && t->clut && (size_t)words * 2 * t->h <= len - 12;
        o += len;
    }
    return 0;
}

/* Writes a palette picture as PNG; colour 0x0000 see-through when `clear`. */
static int write_tim_png(const char *path, const tim_view *t, int clear) {
    uint8_t *pixels = malloc((size_t)t->w * t->h);
    if (!pixels) return 0;
    for (unsigned y = 0; y < t->h; ++y)
        for (unsigned x = 0; x < t->w; ++x)
            pixels[y * t->w + x] = t->bpp == 8 ? t->px[y * t->w + x]
                                               : (uint8_t)((t->px[(y * t->w + x) / 2] >> ((x & 1) * 4)) & 15);
    uint8_t pal[768], alpha[256];
    memset(pal, 0, sizeof pal);
    memset(alpha, 255, sizeof alpha);
    for (unsigned c = 0; c < 256 && c < t->colours; ++c) {
        const uint16_t v = u16(t->clut + 2 * c);
        pal[3 * c] = (uint8_t)((v & 31) << 3);
        pal[3 * c + 1] = (uint8_t)(((v >> 5) & 31) << 3);
        pal[3 * c + 2] = (uint8_t)(((v >> 10) & 31) << 3);
        if (clear && v == 0) alpha[c] = 0;
    }
    const int ok = acr_write_png8(path, t->w, t->h, pixels, pal, alpha);
    free(pixels);
    return ok;
}

/* Phantasma's narrow space "{4" reads as a plain space; runs of spaces
 * that leaves inside a name close up. */
static void narrow_spaces(char *s) {
    char *w = s;
    for (const char *r = s; *r; ++r) {
        if (r[0] == '{' && r[1] == '4') { ++r; if (w == s || w[-1] != ' ') *w++ = ' '; continue; }
        *w++ = *r;
    }
    *w = 0;
}

static int export_missions(const table_layout *L, const char *unpacked_dir, const char *out_dir, const char *toml_name,
                           const char *pic_prefix, unsigned map_base, const char *game, uint32_t *count,
                           char *err, size_t err_size) {
    if (count) *count = 0;
    size_t tn = 0;
    uint8_t *t = read_entry(unpacked_dir, "GG/MS/MIS", 0, &tn);
    if (!t || tn < L->bytes) {
        free(t);
        set_err(err, err_size, "the mission table (MIS.T entry 0) could not be read", unpacked_dir);
        return 0;
    }
    if (!make_dirs(out_dir)) { free(t); set_err(err, err_size, "cannot create", out_dir); return 0; }
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", out_dir, toml_name);
    FILE *f = fopen(path, "w");
    if (!f) { free(t); set_err(err, err_size, "cannot write", path); return 0; }
    fprintf(f, "# %s's missions, imported from your disc by acr-import (GG/MS/MIS.T).\n"
               "# Pages keep the disc's markup: \"@1 ... @0\" highlights words, \"%%n\" shows picture n\n"
               "# (file %sMM-pic-n.png) beside the page. map = the engine's mission map: %s.\n",
            game, pic_prefix, map_base ? "50 + the disc's mission number (stages/50-66)" : "mission n is played on map n");
    if (map_base) fprintf(f, "game = \"pp\"\n");
    int ok = 1;
    static char text[4096], head[512];
    for (unsigned s = 0; s < L->slots && ok; ++s) {
        char name[40];
        size_t k = 0;
        for (; k < 32 && t[L->name + 32 * s + k] && t[L->name + 32 * s + k] != '>'; ++k)
            name[k] = (char)t[L->name + 32 * s + k];
        name[k] = 0;
        narrow_spaces(name);
        const uint8_t *win = t + L->window + 4 * s, *pg = t + L->pages + 32 * s;
        fprintf(f, "\n[[mission]]\nslot     = %u\nname     = ", s);
        put_string(f, name);
        fprintf(f, "\nreward   = %u\nmap      = %u   # the engine's mission number: its map and mission code\n",
                (unsigned)u32(t + L->reward + 4 * s), map_base + (unsigned)t[L->engine + s]);
        fprintf(f, "byte_1160 = %u   # table +1160: 255 except three missions (what it does: unknown)\n",
                (unsigned)t[L->byte + s]);
        if (win[0] == 255) fprintf(f, "offered  = []   # chapter window (none); likely the chapters it is offered in\n");
        else fprintf(f, "offered  = [%u, %u]   # chapter window, first and last; likely the chapters it is offered in\n", win[0], win[1]);
        /* Text pages: the first is the mail header (sender, requester, reward). */
        int first = 1, pages = 0;
        char sender[64] = "", requester[64] = "";
        for (unsigned p = 0; p < 8; ++p) {
            const int16_t e = (int16_t)u16(pg + 16 + 2 * p);
            if (e < 0) continue;
            size_t n = 0;
            uint8_t *d = read_entry(unpacked_dir, "GG/MS/MIS", (unsigned)e + 1u, &n);
            if (!d) continue;
            page_text(d, n, first ? head : text, first ? sizeof head : sizeof text);
            narrow_spaces(first ? head : text);
            free(d);
            if (first) {
                header_field(head, "Sender:", sender, sizeof sender);
                header_field(head, "Requester:", requester, sizeof requester);
                fputs("sender   = ", f); put_string(f, sender);
                fputs("\nrequester = ", f); put_string(f, requester);
                fputs("\npages    = [\n", f);
                first = 0;
            } else {
                fputs("  ", f); put_string(f, text); fputs(",\n", f);
                ++pages;
            }
        }
        if (first) fputs("sender   = \"\"\nrequester = \"\"\npages    = [\n", f);
        fputs("]\n", f);
        /* Pictures, as "%n" in the pages counts them. */
        unsigned pics = 0;
        for (unsigned p = 0; p < 8; ++p) {
            const int16_t e = (int16_t)u16(pg + 2 * p);
            if (e < 0) continue;
            size_t n = 0;
            uint8_t *d = read_entry(unpacked_dir, "GG/MS/MIS", (unsigned)e + 1u, &n);
            tim_view tv;
            if (d && tim_at(d, n, 0, &tv)) {
                snprintf(path, sizeof path, "%s/%s%02u-pic-%u.png", out_dir, pic_prefix, s, p);
                if (!write_tim_png(path, &tv, 0)) { set_err(err, err_size, "cannot write", path); ok = 0; }
                pics = p + 1;
            }
            free(d);
        }
        fprintf(f, "pictures = %u\n", pics);
        (void)pages;
        if (count) ++*count;
    }
    if (fclose(f) != 0 && ok) { set_err(err, err_size, "cannot write", toml_name); ok = 0; }
    free(t);
    if (map_base) return ok;                 /* the report's pictures come from Master of Arena */
    /* The income and expense report and its digits. */
    for (unsigned r = 63; r <= 65 && ok; ++r) {
        size_t n = 0;
        uint8_t *d = read_entry(unpacked_dir, "GG/COM/RTIM", r, &n);
        tim_view tv;
        if (d && tim_at(d, n, 0, &tv)) {
            snprintf(path, sizeof path, "%s/report-%u.png", out_dir, r);
            if (!write_tim_png(path, &tv, 0)) { set_err(err, err_size, "cannot write", path); ok = 0; }
        }
        for (unsigned k = 1; r == 64 && ok && k <= 2; ++k)
            if (d && tim_at(d, n, k, &tv)) {
                snprintf(path, sizeof path, "%s/report-digits%s.png", out_dir, k == 2 ? "-red" : "");
                if (!write_tim_png(path, &tv, 1)) { set_err(err, err_size, "cannot write", path); ok = 0; }
            }
        free(d);
    }
    return ok;
}

int acr_export_moa_missions(const char *unpacked_dir, const char *out_dir, uint32_t *count,
                            char *err, size_t err_size) {
    return export_missions(&k_moa, unpacked_dir, out_dir, "missions.toml", "", 0, "Master of Arena", count, err, err_size);
}

int acr_export_pp_missions(const char *pp_unpacked_dir, const char *out_dir, uint32_t *count,
                           char *err, size_t err_size) {
    return export_missions(&k_pp, pp_unpacked_dir, out_dir, "missions-pp.toml", "pp-", ACR_PP_MISSION_MAP_BASE,
                           "Project Phantasma", count, err, err_size);
}
