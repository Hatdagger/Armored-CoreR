/* Master of Arena stages (0-19 mission maps, 20-40 arenas) as readable files. Format notes:
 * docs/research/gen1-stages.md. Everything is read from the user's own
 * import folder; nothing from the disc is built into this program. */
#include "acr_stages_moa.h"
#include "acr_models_moa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#ifdef _WIN32
#include <direct.h>
#define acr_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define acr_mkdir(p) mkdir((p), 0755)
#endif

#define STAGE_FIRST 0u
#define STAGE_LAST  40u
#define CHUNKS      14u
#define AREAS       512u
#define AREA_SIZE   52u
#define POINT_SIZE  40u
#define MAX_MATS    16u

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t s16(const uint8_t *p) { return (int16_t)u16(p); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | (uint32_t)u16(p + 2) << 16; }

static const struct { uint8_t id; const char *name; } k_names[] = {
    { 20, "LUNAR BASE" }, { 22, "PILLAR HALL" }, { 24, "SCAFFOLD YARD" }, { 25, "GARDEN PLAZA" },
    { 27, "GREEN SILO" }, { 29, "SEABED" },      { 33, "FOREST" },        { 36, "NIGHT CITY" },
    { 39, "OCEAN PLATFORM" },
};

const char *acr_moa_stage_name(unsigned stage) {
    for (size_t i = 0; i < sizeof k_names / sizeof k_names[0]; ++i)
        if (k_names[i].id == stage) return k_names[i].name;
    return NULL;
}

static void set_err(char *err, size_t n, const char *what, const char *path) {
    if (err && n) snprintf(err, n, "%s%s%s", what, path ? ": " : "", path ? path : "");
}

static uint8_t *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    size_t cap = 0, n = 0;
    for (;;) {
        if (n == cap) {
            cap = cap ? cap * 2 : 262144;
            uint8_t *nb = realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb;
        }
        const size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0) break;
    }
    fclose(f);
    *size = n;
    return buf;
}

static int make_dir(const char *path) {
    char buf[1200];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; ; ++p) {
        if (*p == '/' || *p == '\\' || *p == 0) {
            const char c = *p;
            *p = 0;
            if (acr_mkdir(buf) != 0 && errno != EEXIST) return 0;
            if (!c) break;
            *p = c;
        }
    }
    return 1;
}

typedef struct chunk { const uint8_t *p; uint32_t n; } chunk;

/* The stage file is a run of [u32 size][data] chunks, read in this order by
 * the arena engine's loader (FDAT 210, 0x80056338-0x800564A8). */
static int split(const uint8_t *d, size_t size, chunk out[CHUNKS]) {
    size_t o = 0;
    for (unsigned i = 0; i < CHUNKS; ++i) {
        if (o + 4 > size) return 0;
        const uint32_t n = u32(d + o);
        if (n > size - o - 4) return 0;
        out[i].p = d + o + 4;
        out[i].n = n;
        o += 4 + n;
    }
    return 1;
}

/* An area record (chunk 8, 52 bytes; 64 in RAM): min x/y/z +0, max +8,
 * centre +0x10, turn +0x16 (quarter turns in bits 10-11), model +0x1E,
 * light set +0x28 byte 0 (+0x30 in RAM). */
typedef struct area { int16_t min[3], max[3], centre[3]; uint8_t quarter, light; int16_t model; } area;

static unsigned read_areas(const chunk *c, area *out) {
    if (c->n < AREAS * AREA_SIZE) return 0;
    unsigned n = 0;
    for (unsigned i = 0; i < AREAS; ++i) {
        const uint8_t *r = c->p + i * AREA_SIZE;
        area a;
        for (int k = 0; k < 3; ++k) {
            a.min[k] = s16(r + 2 * k);
            a.max[k] = s16(r + 8 + 2 * k);
            a.centre[k] = s16(r + 0x10 + 2 * k);
        }
        if (!a.min[0] && !a.min[1] && !a.min[2] && !a.max[0] && !a.max[1] && !a.max[2]) continue;
        a.quarter = (uint8_t)((u16(r + 0x16) >> 10) & 3);
        a.model = s16(r + 0x1E);
        a.light = r[0x28];
        out[n++] = a;
    }
    return n;
}

/* Model point (game space, 4 model units a world unit) -> engine world:
 * turned by the area's quarter turns (collision 0x80074400 turns the other
 * way to go into the model), moved to its centre, then mirrored. */
static void place(const area *a, int x, int y, int z, double out[3]) {
    const double lx = x / 4.0, ly = y / 4.0, lz = z / 4.0;
    double wx = lx, wz = lz;
    switch (a->quarter) {
    case 1: wx = -lz; wz = lx; break;
    case 2: wx = -lx; wz = -lz; break;
    case 3: wx = lz; wz = -lx; break;
    default: break;
    }
    out[0] = -(a->centre[0] + wx);
    out[1] = -(a->centre[1] + ly);
    out[2] = a->centre[2] + wz;
}

/* ---- the visual models: a PlayStation TMD (chunk 4) ---- */

typedef struct mat { uint8_t r, g, b, semi; } mat;

static int find_mat(mat *m, unsigned *n, mat want) {
    for (unsigned i = 0; i < *n; ++i)
        if (m[i].r == want.r && m[i].g == want.g && m[i].b == want.b && m[i].semi == want.semi) return (int)i;
    if (*n >= MAX_MATS) return 0;
    m[*n] = want;
    return (int)(*n)++;
}

static void mat_name(const mat *m, char *out, size_t n) {
    snprintf(out, n, "c%02x%02x%02x%s", m->r, m->g, m->b, m->semi ? "-semi" : "");
}

/* One primitive's vertex indices and colour (standard TMD packets). */
static int prim(const uint8_t *p, const uint8_t *end, unsigned *nv, unsigned v[4], mat *col, unsigned *len) {
    if (p + 4 > end) return 0;
    const unsigned ilen = p[1], flag = p[2], mode = p[3];
    *len = 4 + ilen * 4;
    if (p + *len > end) return 0;
    const uint8_t *w = p + 4;
    const unsigned n = (mode & 8) ? 4 : 3, tex = mode & 4, gour = mode & 0x10;
    (void)flag;
    /* Master of Arena's packets always have the lit layout (a normal, then
     * the corners), whatever the flag's light bit says (Green Silo's arches
     * use flag 0x11); read as unlit, the padding became a corner at vertex
     * 0, the dome's top, and stretched faces up into the roof. The AC
     * importer reads them the same way. */
    unsigned i0 = tex ? 2 * n : 0;
    col->r = col->g = col->b = 128;
    col->semi = (uint8_t)((mode & 2) != 0);
    if (!tex) { col->r = w[0]; col->g = w[1]; col->b = w[2]; i0 = 2; }
    /* The one unlit layout seen: a flat textured triangle with a colour
     * instead of a normal (six words, the lit one is five): Green Silo's
     * sky (chunk 3, flag 1, mode 0xA4). */
    else if (!gour && n == 3 && ilen == 6) { col->r = w[i0 * 2]; col->g = w[i0 * 2 + 1]; col->b = w[i0 * 2 + 2]; i0 += 1; }
    if ((i0 + (gour ? 2 * n : 1 + n)) * 2 > ilen * 4) return 0;
    if (gour) for (unsigned j = 0; j < n; ++j) v[j] = u16(w + (i0 + 2 * j + 1) * 2);
    else for (unsigned j = 0; j < n; ++j) v[j] = u16(w + (i0 + 1 + j) * 2);
    *nv = n;
    return 1;
}

typedef struct tmd_obj { const uint8_t *vert, *prim; uint32_t nv, np; } tmd_obj;

static unsigned tmd_objects(const chunk *c, tmd_obj *out, unsigned max) {
    if (c->n < 12) return 0;
    const uint32_t nobj = u32(c->p + 8);
    const uint8_t *base = c->p + 12, *end = c->p + c->n;
    unsigned n = 0;
    for (uint32_t i = 0; i < nobj && n < max; ++i) {
        const uint8_t *o = base + i * 28;
        if (o + 28 > end) break;
        tmd_obj t = { base + u32(o), base + u32(o + 16), u32(o + 4) & 0xFFFF, u32(o + 20) & 0xFFFF };
        if (t.vert > end || t.prim > end || t.vert + t.nv * 8 > end) t.nv = t.np = 0;
        out[n++] = t;
    }
    return n;
}

static int write_visual(const char *dir, const chunk *tmd, const area *ar, unsigned areas, uint32_t *faces) {
    tmd_obj obj[256];
    const unsigned nobj = tmd_objects(tmd, obj, 256);
    mat mats[MAX_MATS];
    unsigned nm = 0;
    char path[1200];
    snprintf(path, sizeof path, "%s/simple.obj", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Master of Arena stage models, flat version (FDAT 5s+4 chunk 4), placed by the\n"
               "# area records. Engine world: x = -(game x), y up = -(game y), z = game z.\n"
               "mtllib simple.mtl\n");
    uint32_t base = 1, nf = 0;
    const uint8_t *end = tmd->p + tmd->n;
    for (unsigned a = 0; a < areas; ++a) {
        if (ar[a].model < 0 || (unsigned)ar[a].model >= nobj) continue;
        const tmd_obj *o = &obj[ar[a].model];
        if (!o->nv) continue;
        fprintf(f, "o area%u-model%d\n", a, ar[a].model);
        for (uint32_t k = 0; k < o->nv; ++k) {
            double w[3];
            const uint8_t *v = o->vert + k * 8;
            place(&ar[a], s16(v), s16(v + 2), s16(v + 4), w);
            fprintf(f, "v %.2f %.2f %.2f\n", w[0], w[1], w[2]);
        }
        const uint8_t *p = o->prim;
        int cur = -1;
        for (uint32_t k = 0; k < o->np; ++k) {
            unsigned n, v[4], len;
            mat m;
            if (!prim(p, end, &n, v, &m, &len)) break;
            p += len;
            int ok = 1;
            for (unsigned j = 0; j < n; ++j) if (v[j] >= o->nv) ok = 0;
            if (!ok) continue;
            const int mi = find_mat(mats, &nm, m);
            if (mi != cur) {
                char name[32];
                mat_name(&mats[mi], name, sizeof name);
                fprintf(f, "usemtl %s\n", name);
                cur = mi;
            }
            /* TMD quads are in strip order (0 1 2 3 = 0 1 3 2 around). */
            if (n == 4) fprintf(f, "f %u %u %u %u\n", base + v[0], base + v[1], base + v[3], base + v[2]);
            else fprintf(f, "f %u %u %u\n", base + v[0], base + v[1], base + v[2]);
            ++nf;
        }
        base += o->nv;
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/simple.mtl", dir);
    f = fopen(path, "w");
    if (!f) return 0;
    for (unsigned i = 0; i < nm; ++i) {
        char name[32];
        mat_name(&mats[i], name, sizeof name);
        fprintf(f, "newmtl %s\nKd %.4f %.4f %.4f\n", name, mats[i].r / 255.0, mats[i].g / 255.0, mats[i].b / 255.0);
        if (mats[i].semi) fprintf(f, "d 0.5\n");
    }
    fclose(f);
    *faces = nf;
    return 1;
}

/* ---- stage lights (docs/research/gen1-stages.md, "Stage lights") ----
 * 32 light sets of 44 bytes in FDAT 205 chunk 7: light matrix +0x00 (three
 * light directions, 4096 = 1), colour matrix +0x14, back colour +0x26 (r, g,
 * b), fog +0x2A. The arena engine's area draw (FDAT 210 0x80063844) sets the
 * area's set (+0x28 byte 0) with SetColorMatrix, SetBackColor and
 * SetLightMatrix; the light matrix is the set's own turned by the area's
 * quarter turns the other way (0x8005DAE4 / 0x800139F8), so the model's own
 * normals can be used. The stage draws (0x8006E77C, 0x8006F57C) light every
 * textured face once with NCCS: the face normal and the colour 0x404040.
 * GPU codes with bit 0 (TMD modes 0x25 / 0x2D) draw the texture as it is, so
 * those faces keep 128 (= the texel). No depth cue: the fog value is not
 * used by these draws. */
#define LIGHT_SETS     32u
#define LIGHT_SET_SIZE 44u

typedef struct light_set { int16_t llm[9], lcm[9]; uint8_t bk[3]; } light_set;

/* FDAT 205's chunk 7; 0 if it can't be read (stages then go without). */
static unsigned read_light_sets(const char *unpacked_dir, light_set *out) {
    char path[1200];
    snprintf(path, sizeof path, "%s/GG/COM/FDAT/205.bin", unpacked_dir);
    size_t n = 0;
    uint8_t *d = read_all(path, &n);
    if (!d) return 0;
    size_t o = 0;
    unsigned got = 0;
    for (unsigned c = 0; c <= 7 && o + 4 <= n; ++c) {
        const uint32_t len = u32(d + o);
        if (len > n - o - 4) break;
        if (c == 7 && len >= LIGHT_SETS * LIGHT_SET_SIZE) {
            for (unsigned i = 0; i < LIGHT_SETS; ++i) {
                const uint8_t *r = d + o + 4 + i * LIGHT_SET_SIZE;
                for (int k = 0; k < 9; ++k) { out[i].llm[k] = s16(r + 2 * k); out[i].lcm[k] = s16(r + 0x14 + 2 * k); }
                for (int k = 0; k < 3; ++k) out[i].bk[k] = r[0x26 + k];
            }
            got = LIGHT_SETS;
        }
        o += 4 + len;
    }
    free(d);
    return got;
}

static int32_t gte_lim(int64_t v) { return v < 0 ? 0 : v > 0x7FFF ? 0x7FFF : (int32_t)v; }

static void light_face(const light_set *ls, unsigned quarter, const int16_t n[3], uint8_t out[3]) {
    acr_moa_light_face(ls->llm, ls->lcm, ls->bk, quarter, n, out);
}

void acr_moa_light_face(const int16_t llm[9], const int16_t lcm[9], const uint8_t bk[3], unsigned quarter,
                        const int16_t n[3], uint8_t out[3]) {
    /* 0x800139F8: each light (x, y, z) turned k quarters: (-z, y, x),
     * (-x, y, -z), (z, y, -x). The area draw uses k = -quarter. */
    const unsigned k = (4u - (quarter & 3u)) & 3u;
    int32_t ir[3];
    for (int i = 0; i < 3; ++i) {
        const int32_t x = llm[i * 3], y = llm[i * 3 + 1], z = llm[i * 3 + 2];
        int32_t l[3] = { x, y, z };
        if (k == 1) { l[0] = -z; l[2] = x; }
        else if (k == 2) { l[0] = -x; l[2] = -z; }
        else if (k == 3) { l[0] = z; l[2] = -x; }
        ir[i] = gte_lim(((int64_t)l[0] * n[0] + (int64_t)l[1] * n[1] + (int64_t)l[2] * n[2]) >> 12);
    }
    for (int c = 0; c < 3; ++c) {
        const int64_t mac = ((int64_t)(bk[c] << 4) << 12) + (int64_t)lcm[c * 3] * ir[0] +
                            (int64_t)lcm[c * 3 + 1] * ir[1] + (int64_t)lcm[c * 3 + 2] * ir[2];
        const int32_t i = gte_lim(mac >> 12);
        const int32_t v = (int32_t)((((int64_t)0x40 << 4) * i >> 12) >> 4);
        out[c] = (uint8_t)(v > 255 ? 255 : v);
    }
}

/* ---- the textured models (chunk 1) and their textures (RTIM <stage>) ----
 * Chunk 1 is a model set (u16 count, then [u32 size][model] entries, size
 * < 5 = empty) holding, at entry k, the textured version of model k: a TMD
 * at +0xC. The arena engine loads RTIM entry <stage> (0x80056230) before
 * the stage: 8-bit TIMs straight into VRAM, where the faces' texture pages
 * (TMD tsb) and palettes (cba) find them. */

#define VRAM_W 1024
#define VRAM_H 512
#define MAX_TEX 24

struct acr_moa_vram { uint16_t px[VRAM_H][VRAM_W]; uint8_t set[VRAM_H][VRAM_W / 64]; };
typedef struct acr_moa_vram vram;

static int load_tims(const uint8_t *d, size_t n, vram *v) {
    size_t o = 0;
    int count = 0;
    while (o + 8 <= n && u32(d + o) == 0x10) {
        const uint32_t flags = u32(d + o + 4);
        o += 8;
        for (int part = (flags & 8) ? 0 : 1; part < 2; ++part) {
            if (o + 12 > n) return count;
            const uint32_t len = u32(d + o);
            const unsigned x = u16(d + o + 4), y = u16(d + o + 6), w = u16(d + o + 8), h = u16(d + o + 10);
            if (len < 12 || o + len > n) return count;
            for (unsigned r = 0; r < h; ++r)
                for (unsigned c = 0; c < w; ++c) {
                    const size_t at = o + 12 + ((size_t)r * w + c) * 2;
                    if (at + 2 > o + len || y + r >= VRAM_H || x + c >= VRAM_W) continue;
                    v->px[y + r][x + c] = u16(d + at);
                    v->set[y + r][(x + c) / 64] = 1;
                }
            o += len;
        }
        ++count;
    }
    return count;
}

/* A texture page + palette, and whether its faces blend: on the PS1 a
 * textured face with the semi-transparency bit (mode bit 1) blends only
 * where the texel's colour has bit 15 set. Here a palette with any such
 * colour makes the whole material see-through (Green Silo's glass: every
 * colour of palette 481 has it, none of 480's). */
typedef struct tex_key { uint16_t tpage, clut; uint8_t semi; } tex_key;

static int clut_has_stp(const vram *v, uint16_t clut, unsigned colours) {
    const unsigned cx = (clut & 63) * 16, cy = clut >> 6;
    for (unsigned i = 0; i < colours && cx + i < VRAM_W && cy < VRAM_H; ++i)
        if (v->px[cy][cx + i] & 0x8000) return 1;
    return 0;
}

/* Texture page as an indexed PNG with its palette (256 x 256 texels). */
static int write_page(const char *path, const vram *v, tex_key k) {
    const unsigned px = (k.tpage & 15) * 64, py = ((k.tpage >> 4) & 1) * 256, depth = (k.tpage >> 7) & 3;
    const unsigned cx = (k.clut & 63) * 16, cy = k.clut >> 6;
    static uint8_t pixels[256 * 256];
    uint8_t pal[768], alpha[256];
    memset(pal, 0, sizeof pal);
    memset(alpha, 0, sizeof alpha);
    const unsigned colours = depth == 0 ? 16 : 256;
    for (unsigned i = 0; i < colours && cx + i < VRAM_W && cy < VRAM_H; ++i) {
        const unsigned c = v->px[cy][cx + i];
        const unsigned r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        pal[3 * i] = (uint8_t)(r << 3 | r >> 2);
        pal[3 * i + 1] = (uint8_t)(g << 3 | g >> 2);
        pal[3 * i + 2] = (uint8_t)(b << 3 | b >> 2);
        alpha[i] = c == 0 ? 0 : 255;          /* 0x0000 is see-through on the PS1 */
    }
    for (unsigned t = 0; t < 256; ++t)
        for (unsigned u = 0; u < 256; ++u) {
            unsigned idx = 0;
            const unsigned row = py + t;
            if (depth == 0) {
                const unsigned w = px + u / 4;
                if (row < VRAM_H && w < VRAM_W) idx = (v->px[row][w] >> ((u & 3) * 4)) & 15;
            } else {
                const unsigned w = px + u / 2;
                if (row < VRAM_H && w < VRAM_W) idx = (v->px[row][w] >> ((u & 1) * 8)) & 255;
            }
            pixels[t * 256 + u] = (uint8_t)idx;
        }
    return acr_write_png8(path, 256, 256, pixels, pal, alpha);
}

/* Is the page's data there (some VRAM written under it)? */
static int page_loaded(const vram *v, unsigned tpage) {
    const unsigned px = (tpage & 15) * 64, py = ((tpage >> 4) & 1) * 256;
    return v->set[py][px / 64] || v->set[py + 128][px / 64];
}

/* `lights` (NULL: none) gives every face the colour the game lights it with,
 * written on its own copies of the face's points ("v x y z r g b", 0-1 =
 * 0-255; 128 = the texel as it is). */
static int write_textured(const char *dir, const char *name, const char *unpacked, unsigned stage, const chunk *set,
                          const area *ar, unsigned areas, const light_set *lights, uint32_t *faces,
                          uint32_t *textures) {
    *faces = *textures = 0;
    /* The models, by number. */
    const uint8_t *model[256];
    uint32_t model_n[256];
    unsigned nmodels = 0;
    if (set->n < 4) return 1;
    {
        const unsigned count = u16(set->p);
        size_t o = 4;
        for (unsigned k = 0; k < count && k < 256; ++k) {
            if (o + 4 > set->n) break;
            const uint32_t n = u32(set->p + o);
            if (n < 4 || o + n > set->n) break;
            model[k] = n >= 5 ? set->p + o : NULL;
            model_n[k] = n;
            nmodels = k + 1;
            o += n;
        }
    }
    if (!nmodels) return 1;
    static vram v;
    memset(&v, 0, sizeof v);
    {
        char path[1200];
        snprintf(path, sizeof path, "%s/GG/COM/RTIM/%03u.bin", unpacked, stage);
        size_t n = 0;
        uint8_t *d = read_all(path, &n);
        if (d) { load_tims(d, n, &v); free(d); }
    }
    tex_key keys[MAX_TEX];
    unsigned nkeys = 0;
    mat flat[MAX_MATS];
    unsigned nflat = 0;
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.obj", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Master of Arena stage models (FDAT 5s+4 chunk %s, textures from RTIM %u),\n"
               "# placed by the area records. Engine world: x = -(game x), y up = -(game y),\n"
               "# z = game z; world units. Materials tNN = texture NN, cRRGGBB = flat colour.\n"
               "%s"
               "mtllib %s.mtl\n", !strcmp(name, "sky") ? "3, the sky, around the origin" : !strcmp(name, "far") ? "3, the far plane, around the origin" : "1", stage,
            lights ? "# Lit by the stage's light sets (FDAT 205 chunk 7): each face has its own points,\n"
                     "# coloured as the game lights it (r g b x 255 = the PS1 colour, 128 = texel as is).\n" : "",
            name);
    uint32_t base_v = 1, base_t = 1, nf = 0;
    for (unsigned a = 0; a < areas; ++a) {
        const int m = ar[a].model;
        if (m < 0 || (unsigned)m >= nmodels || !model[m] || model_n[m] < 0x20) continue;
        const chunk mc = { model[m] + 0xC, model_n[m] - 0xC };
        tmd_obj obj[8];
        const unsigned nobj = tmd_objects(&mc, obj, 8);
        const uint8_t *end = mc.p + mc.n;
        for (unsigned oi = 0; oi < nobj; ++oi) {
            const tmd_obj *o = &obj[oi];
            if (!o->nv) continue;
            fprintf(f, "o area%u-model%d-%u\n", a, m, oi);
            const light_set *ls = lights && ar[a].light < LIGHT_SETS ? &lights[ar[a].light] : NULL;
            if (!ls)
                for (uint32_t k = 0; k < o->nv; ++k) {
                    double w[3];
                    const uint8_t *vv = o->vert + k * 8;
                    place(&ar[a], s16(vv), s16(vv + 2), s16(vv + 4), w);
                    fprintf(f, "v %.2f %.2f %.2f\n", w[0], w[1], w[2]);
                }
            /* The object's normals (8 bytes: x, y, z, pad; 4096 = 1). */
            const uint8_t *nrm = mc.p + 12 + u32(mc.p + 12 + oi * 28 + 8);
            const uint32_t nn = u32(mc.p + 12 + oi * 28 + 12) & 0xFFFF;
            const uint8_t *p = o->prim;
            char cur[32] = "";
            for (uint32_t k = 0; k < o->np; ++k) {
                unsigned n, vi[4], len;
                mat col;
                if (!prim(p, end, &n, vi, &col, &len)) break;
                const uint8_t mode = p[3];
                const uint8_t *w = p + 4;
                p += len;
                int ok = 1;
                for (unsigned j = 0; j < n; ++j) if (vi[j] >= o->nv) ok = 0;
                if (!ok) continue;
                char mname[32];
                int textured = 0;
                if (mode & 4) {
                    tex_key key = { (uint16_t)(u16(w + 6) & 0x1FF), u16(w + 2), 0 };
                    key.semi = (uint8_t)((mode & 2) && clut_has_stp(&v, key.clut, ((key.tpage >> 7) & 3) ? 256 : 16));
                    if (page_loaded(&v, key.tpage)) {
                        unsigned t = 0;
                        while (t < nkeys && (keys[t].tpage != key.tpage || keys[t].clut != key.clut ||
                                             keys[t].semi != key.semi)) ++t;
                        if (t == nkeys && nkeys < MAX_TEX) keys[nkeys++] = key;
                        if (t < nkeys) {
                            snprintf(mname, sizeof mname, "t%02u%s", t, keys[t].semi ? "-semi" : "");
                            textured = 1;
                            for (unsigned j = 0; j < n; ++j)
                                fprintf(f, "vt %.5f %.5f\n", (w[j * 4] + 0.5) / 256.0, 1.0 - (w[j * 4 + 1] + 0.5) / 256.0);
                        }
                    }
                    col.semi = 0;
                }
                if (!textured) {
                    if (mode & 4) { col.r = col.g = col.b = 96; }    /* page not loaded: grey */
                    find_mat(flat, &nflat, col);
                    mat_name(&col, mname, sizeof mname);
                }
                if (strcmp(mname, cur)) { fprintf(f, "usemtl %s\n", mname); snprintf(cur, sizeof cur, "%s", mname); }
                /* TMD quads are in strip order (0 1 2 3 = 0 1 3 2 around). */
                const unsigned order4[4] = { 0, 1, 3, 2 }, order3[3] = { 0, 1, 2 };
                const unsigned *ord = n == 4 ? order4 : order3;
                if (ls) {
                    /* The face normal follows the texture words (lit layout). */
                    uint8_t c[3] = { 128, 128, 128 };
                    const unsigned ni = u16(w + ((mode & 4) ? n : 1) * 4);   /* gouraud: corner 0's */
                    if (!(mode & 1) && ni < nn && nrm + ni * 8 + 6 <= end) {
                        const int16_t nv3[3] = { s16(nrm + ni * 8), s16(nrm + ni * 8 + 2), s16(nrm + ni * 8 + 4) };
                        light_face(ls, ar[a].quarter, nv3, c);
                    }
                    for (unsigned j = 0; j < n; ++j) {
                        double wp[3];
                        const uint8_t *vv = o->vert + vi[ord[j]] * 8;
                        place(&ar[a], s16(vv), s16(vv + 2), s16(vv + 4), wp);
                        fprintf(f, "v %.2f %.2f %.2f %.4f %.4f %.4f\n", wp[0], wp[1], wp[2], c[0] / 255.0, c[1] / 255.0,
                                c[2] / 255.0);
                    }
                }
                fprintf(f, "f");
                for (unsigned j = 0; j < n; ++j) {
                    const unsigned pv = ls ? base_v + j : base_v + vi[ord[j]];
                    if (textured) fprintf(f, " %u/%u", pv, base_t + ord[j]);
                    else fprintf(f, " %u", pv);
                }
                fprintf(f, "\n");
                if (ls) base_v += n;
                if (textured) base_t += n;
                ++nf;
            }
            if (!ls) base_v += o->nv;
        }
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/%s.mtl", dir, name);
    f = fopen(path, "w");
    if (!f) return 0;
    const char *png_name = strcmp(name, "visual") ? name : "texture";   /* visual: texture-NN.png, as before */
    for (unsigned t = 0; t < nkeys; ++t) {
        char png[1200];
        snprintf(png, sizeof png, "%s/%s-%02u.png", dir, png_name, t);
        if (!write_page(png, &v, keys[t])) { fclose(f); return 0; }
        /* Pages shared by a solid and a see-through material are written once each (same picture). */
        fprintf(f, "newmtl t%02u%s\nmap_Kd %s-%02u.png\n", t, keys[t].semi ? "-semi" : "", png_name, t);
    }
    for (unsigned i = 0; i < nflat; ++i) {
        char name[32];
        mat_name(&flat[i], name, sizeof name);
        fprintf(f, "newmtl %s\nKd %.4f %.4f %.4f\n", name, flat[i].r / 255.0, flat[i].g / 255.0, flat[i].b / 255.0);
        if (flat[i].semi) fprintf(f, "d 0.5\n");
    }
    fclose(f);
    *faces = nf;
    *textures = nkeys;
    return 1;
}

/* ---- collision (chunk 7) ----
 * 1,024 pairs of offsets (from the chunk's start), one per model number:
 * vertices (s16 x, y, z, pad) and polygons. Polygons: u16 count, three
 * 32-byte bit tables (x, y, z slices, for the search), then from +0x64
 * 20-byte records: u16 flags (bit 0 = four corners), s16 normal x, y, z
 * (4096 = 1), s32 plane distance (normal . point + d = 0, model units x
 * 4096), u16 corner[4] (byte offset into the vertices in bits 3-14; four
 * corners in strip order). */
static int write_collision(const char *dir, const chunk *c, const area *ar, unsigned areas, uint32_t *polys) {
    char path[1200];
    snprintf(path, sizeof path, "%s/collision.obj", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Master of Arena stage collision (FDAT 5s+4 chunk 7) placed by the area records.\n"
               "# Engine world: x = -(game x), y up = -(game y), z = game z; world units.\n");
    uint32_t base = 1, n_out = 0;
    for (unsigned a = 0; a < areas; ++a) {
        const int m = ar[a].model;
        if (m < 0 || m >= 1024 || c->n < 0x2000) continue;
        const uint32_t va = u32(c->p + m * 8), pb = u32(c->p + m * 8 + 4);
        if (!va || !pb || pb + 0x64 > c->n || va >= c->n) continue;
        const unsigned np = u16(c->p + pb);
        if (pb + 0x64 + np * 20 > c->n) continue;
        /* Corners used, so only those vertices are written. */
        unsigned max_v = 0;
        for (unsigned k = 0; k < np; ++k) {
            const uint8_t *r = c->p + pb + 0x64 + k * 20;
            const unsigned corners = (u16(r) & 1) ? 4 : 3;
            for (unsigned j = 0; j < corners; ++j) {
                const unsigned vi = (u16(r + 12 + 2 * j) & 0x7FF8) / 8;
                if (vi + 1 > max_v) max_v = vi + 1;
            }
        }
        if (va + max_v * 8 > c->n) continue;
        fprintf(f, "o area%u-model%d\n", a, m);
        for (unsigned k = 0; k < max_v; ++k) {
            double w[3];
            const uint8_t *v = c->p + va + k * 8;
            place(&ar[a], s16(v), s16(v + 2), s16(v + 4), w);
            fprintf(f, "v %.2f %.2f %.2f\n", w[0], w[1], w[2]);
        }
        for (unsigned k = 0; k < np; ++k) {
            const uint8_t *r = c->p + pb + 0x64 + k * 20;
            const unsigned corners = (u16(r) & 1) ? 4 : 3;
            unsigned v[4];
            for (unsigned j = 0; j < corners; ++j) v[j] = (u16(r + 12 + 2 * j) & 0x7FF8) / 8;
            /* Four corners are in strip order, as in the TMD (0 1 3 2 around). */
            if (corners == 4) fprintf(f, "f %u %u %u %u\n", base + v[0], base + v[1], base + v[3], base + v[2]);
            else fprintf(f, "f %u %u %u\n", base + v[0], base + v[1], base + v[2]);
            ++n_out;
        }
        base += max_v;
    }
    fclose(f);
    *polys = n_out;
    return 1;
}

/* The same collision as the game keeps it, for the engine's port of the
 * game's own box sweep (docs/research/gen1-stages.md, "Walls and floors"):
 * game axes (x not mirrored, y down), model units, every polygon's raw
 * fields (flags, normal, plane distance, the four corner words with their
 * low flag bits), and the areas that place each model.
 *
 *   area <minx> <miny> <minz> <maxx> <maxy> <maxz> <cx> <cy> <cz> <quarter> <model>
 *   model <number> <vertices> <polygons>
 *   v <x> <y> <z>
 *   p <flags> <nx> <ny> <nz> <d> <c0> <c1> <c2> <c3>                          */
static int write_collision_game(const char *dir, const chunk *c, const area *ar, unsigned areas) {
    char path[1200];
    snprintf(path, sizeof path, "%s/collision.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Master of Arena stage collision as the game keeps it (FDAT 5s+4 chunks 7 and 8).\n"
               "# Game axes (x not mirrored, y down). area: box (world units), centre, quarter\n"
               "# turns, model. model: its vertices (model units, 4 a world unit) and polygons\n"
               "# (flags, normal x 4096, plane distance, the four raw corner words).\n");
    static uint8_t used[1024];
    memset(used, 0, sizeof used);
    for (unsigned a = 0; a < areas; ++a) {
        fprintf(f, "area %d %d %d %d %d %d %d %d %d %u %d\n", ar[a].min[0], ar[a].min[1], ar[a].min[2],
                ar[a].max[0], ar[a].max[1], ar[a].max[2], ar[a].centre[0], ar[a].centre[1], ar[a].centre[2],
                ar[a].quarter, ar[a].model);
        if (ar[a].model >= 0 && ar[a].model < 1024) used[ar[a].model] = 1;
    }
    for (unsigned m = 0; m < 1024 && c->n >= 0x2000; ++m) {
        if (!used[m]) continue;
        const uint32_t va = u32(c->p + m * 8), pb = u32(c->p + m * 8 + 4);
        if (!va || !pb || pb + 0x64 > c->n || va >= c->n) continue;
        const unsigned np = u16(c->p + pb);
        if (pb + 0x64 + np * 20 > c->n) continue;
        unsigned max_v = 0;
        for (unsigned k = 0; k < np; ++k) {
            const uint8_t *r = c->p + pb + 0x64 + k * 20;
            const unsigned corners = (u16(r) & 1) ? 4 : 3;
            for (unsigned j = 0; j < corners; ++j) {
                const unsigned vi = (u16(r + 12 + 2 * j) & 0x7FF8) / 8;
                if (vi + 1 > max_v) max_v = vi + 1;
            }
        }
        if (va + max_v * 8 > c->n) continue;
        fprintf(f, "model %u %u %u\n", m, max_v, np);
        for (unsigned k = 0; k < max_v; ++k) {
            const uint8_t *v = c->p + va + k * 8;
            fprintf(f, "v %d %d %d\n", s16(v), s16(v + 2), s16(v + 4));
        }
        for (unsigned k = 0; k < np; ++k) {
            const uint8_t *r = c->p + pb + 0x64 + k * 20;
            fprintf(f, "p %u %d %d %d %ld %u %u %u %u\n", u16(r), s16(r + 2), s16(r + 4), s16(r + 6),
                    (long)(int32_t)u32(r + 8), u16(r + 12), u16(r + 14), u16(r + 16), u16(r + 18));
        }
    }
    fclose(f);
    return 1;
}

/* ---- out-of-area boxes (stage objects, chunk 10, and the stage code) ----
 * Chunk 10 holds the stage objects the arena engine sets up at load
 * (0x80079798): records of 32 bytes, s16 x, y, z, s16 kind at +6 (-1 =
 * none), and 16 bytes at +0x10 that land at the object's +0x22 in RAM. A
 * kind k calls the stage code's creator (FDAT 5s+2, loaded at 0x8009EFE8;
 * word 40 + k of its table). On Garden Plaza, Night City and Ocean Platform
 * one kind's creator starts the box handler (0x8009F364 / 0x8009F490): the
 * box is the object's position plus signed bytes x 256 at +0x22..+0x24
 * (min) and +0x25..+0x27 (max), worked out in 16 bits, and the byte at
 * +0x28 is the bit an AC inside it sets in its player block +0x08 (ends
 * included). The engine's end check (0x800528E4) takes "+0x08 & 7 == 1" as
 * out of area: inside box 0, outside boxes 1 and 2. Green Silo's code has no
 * box handler (enclosed: nothing to leave). The handler is found by its
 * code: the six box bytes and the bit read from the object, then both
 * player blocks (0x80045198). */

#define CODE_BASE    0x8009EFE8u
#define OBJ_RECORDS  128u
#define OBJ_SIZE     32u

typedef struct oa_box { int lo[3], hi[3]; unsigned bit; } oa_box;

static int is_box_handler(const uint8_t *code, size_t n, uint32_t addr) {
    if (addr < CODE_BASE || addr - CODE_BASE >= n) return 0;
    unsigned seen = 0;
    for (size_t o = addr - CODE_BASE; o + 4 <= n && o < addr - CODE_BASE + 0x200; o += 4) {
        const uint32_t w = u32(code + o);
        if (w >= 0x92220022u && w <= 0x92220027u) seen |= 1u << (w - 0x92220022u);   /* lbu v0, 0x22..0x27(s1) */
        if (w == 0x92260028u) seen |= 0x40;                                             /* lbu a2, 0x28(s1) */
        if (w == 0x26105198u) seen |= 0x80;                                             /* addiu s0, s0, 0x5198 */
        if (w == 0x03E00008u) break;                                                    /* jr ra */
    }
    return seen == 0xFF;
}

/* The task a creator starts: lui v1, 0x800A then addiu v1, v1, imm. */
static uint32_t creator_task(const uint8_t *code, size_t n, uint32_t addr) {
    if (addr < CODE_BASE || addr - CODE_BASE >= n) return 0;
    for (size_t o = addr - CODE_BASE; o + 8 <= n && o < addr - CODE_BASE + 0x40; o += 4)
        if (u32(code + o) == 0x3C03800Au && (u32(code + o + 4) & 0xFFFF0000u) == 0x24630000u)
            return 0x800A0000u + (uint32_t)(int32_t)(int16_t)(u32(code + o + 4) & 0xFFFF);
    return 0;
}

static unsigned read_out_of_area(const uint8_t *code, size_t code_n, const chunk *objs, oa_box *out, unsigned max) {
    if (!code || code_n < 0x200 || objs->n < OBJ_RECORDS * OBJ_SIZE) return 0;
    unsigned n = 0;
    for (unsigned r = 0; r < OBJ_RECORDS && n < max; ++r) {
        const uint8_t *p = objs->p + r * OBJ_SIZE;
        const int kind = s16(p + 6);
        if (kind < 0 || 40u + (unsigned)kind >= 64u) continue;
        const uint32_t creator = u32(code + (40u + (unsigned)kind) * 4u);
        if (!creator || !is_box_handler(code, code_n, creator_task(code, code_n, creator))) continue;
        int lo[3], hi[3];
        for (int k = 0; k < 3; ++k) {
            const uint16_t c = u16(p + 2 * k);
            lo[k] = (int16_t)(uint16_t)(c + (int8_t)p[16 + k] * 256);
            hi[k] = (int16_t)(uint16_t)(c + (int8_t)p[19 + k] * 256);
        }
        /* Engine axes: x mirrored. */
        oa_box b = { { -hi[0], lo[1], lo[2] }, { -lo[0], hi[1], hi[2] }, p[22] };
        out[n++] = b;
    }
    return n;
}

/* ---- points (chunk 13): 40-byte records; 1 and 2 are the two starts ---- */

static int write_toml(const char *dir, unsigned stage, const chunk *pts, const area *ar, unsigned areas,
                      uint32_t faces, uint32_t polys, const oa_box *oa, unsigned noa) {
    char path[1200];
    snprintf(path, sizeof path, "%s/stage.toml", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    const char *name = acr_moa_stage_name(stage);
    fprintf(f, "# Master of Arena stage %u (FDAT %u). Engine world units; y is down as in the\n"
               "# game (the floor of Green Silo is at -662), x mirrored (x = -(game x)).\n",
            stage, stage * 5 + 4);
    if (stage < 20)
        fprintf(f, "# A mission map. Its two start records are the arena's; missions don't use\n"
                   "# them as drops (some sit side by side or off the floor).\n");
    fprintf(f, "id = %u\nname = \"%s\"\nkind = \"%s\"\n", stage, name ? name : "", stage < 20 ? "mission" : "arena");
    fprintf(f, "visual_faces = %u\ncollision_polygons = %u\nareas = %u\n", (unsigned)faces, (unsigned)polys, areas);
    /* Starts: x, y, z, facing (4096 a turn; the engine's facing, which the
     * mirror leaves as it is). */
    for (unsigned k = 1; k <= 2; ++k) {
        if (pts->n < (k + 1) * POINT_SIZE) break;
        const uint8_t *r = pts->p + k * POINT_SIZE;
        fprintf(f, "start_%u = [%d, %d, %d, %u]\n", k, -s16(r), s16(r + 2), s16(r + 4), (unsigned)(u16(r + 10) & 4095));
    }
    /* Area boxes (engine x): min x, y, z, max x, y, z, model, quarter turns. */
    fprintf(f, "area_boxes = [\n");
    for (unsigned a = 0; a < areas; ++a)
        fprintf(f, "  [%d, %d, %d, %d, %d, %d, %d, %u],\n", -ar[a].max[0], ar[a].min[1], ar[a].min[2],
                -ar[a].min[0], ar[a].max[1], ar[a].max[2], ar[a].model, ar[a].quarter);
    fprintf(f, "]\n");
    /* Out-of-area boxes (engine x): min x, y, z, max x, y, z, bit. Out of
     * area = inside the bit 0 box and outside the bit 1 and bit 2 boxes. */
    if (noa) {
        fprintf(f, "out_of_area = [\n");
        for (unsigned i = 0; i < noa; ++i)
            fprintf(f, "  [%d, %d, %d, %d, %d, %d, %u],\n", oa[i].lo[0], oa[i].lo[1], oa[i].lo[2],
                    oa[i].hi[0], oa[i].hi[1], oa[i].hi[2], oa[i].bit);
        fprintf(f, "]\n");
    }
    fclose(f);
    return 1;
}

/* ---- the sky (chunk 3) ----
 * Chunk 3 is a model set loaded at model slot 0xA0, drawn by the arena
 * engine's stage-object routine (0x800932B4: slot 0xA0 + the object's
 * +6). Green Silo's entry 1 is a textured, unlit sphere of radius 4,096
 * around its origin (42 points, 80 faces, page (640, 0), palette row
 * 485): the sky. Ocean Platform's are a small object and a flat sea, not
 * spheres. Written as sky.obj around the origin; the engine draws it
 * around the eye, behind everything (how the game places it is not read
 * yet). Only models that are spheres around their origin count. */
/* want_plane 0: spheres around their origin (skies); 1: flat planes over
 * 20,000 model units across at the origin's height: Ocean Platform's sea
 * and Garden Plaza's grass, 11,750 world units square, see-through (mode
 * 0x2E). How the game places them isn't read; the engine tiles them out
 * to the horizon under the stage. Written as far.obj. */
static unsigned chunk3_areas(const chunk *set, area *out, unsigned max, int want_plane) {
    if (set->n < 4) return 0;
    const unsigned count = u16(set->p);
    size_t o = 4;
    unsigned n = 0;
    for (unsigned k = 0; k < count && n < max; ++k) {
        if (o + 4 > set->n) break;
        const uint32_t sz = u32(set->p + o);
        if (sz < 4 || o + sz > set->n) break;
        if (sz >= 0x20) {
            const chunk mc = { set->p + o + 0xC, sz - 0xC };
            tmd_obj obj[1];
            if (tmd_objects(&mc, obj, 1) == 1 && obj[0].nv >= 12) {
                double lo = 1e18, hi = 0, c[3] = { 0, 0, 0 };
                for (uint32_t i = 0; i < obj[0].nv; ++i) {
                    const uint8_t *vv = obj[0].vert + i * 8;
                    const double x = s16(vv), y = s16(vv + 2), z = s16(vv + 4), r = sqrt(x * x + y * y + z * z);
                    if (r < lo) lo = r;
                    if (r > hi) hi = r;
                    c[0] += x; c[1] += y; c[2] += z;
                }
                const double off = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) / obj[0].nv;
                int ylo = 32767, yhi = -32768, xlo = 32767, xhi = -32768;
                for (uint32_t i = 0; i < obj[0].nv; ++i) {
                    const int x = s16(obj[0].vert + i * 8), y = s16(obj[0].vert + i * 8 + 2);
                    if (y < ylo) ylo = y;
                    if (y > yhi) yhi = y;
                    if (x < xlo) xlo = x;
                    if (x > xhi) xhi = x;
                }
                const int plane = ylo == 0 && yhi == 0 && xhi - xlo > 20000;
                if (want_plane ? plane : (hi > 0 && lo > hi * 0.9 && off < hi * 0.05)) {
                    area a;
                    memset(&a, 0, sizeof a);
                    a.model = (int16_t)k;
                    out[n++] = a;
                }
            }
        }
        o += sz;
    }
    return n;
}

int acr_export_moa_stages(const char *unpacked_dir, const char *out_dir, uint32_t *stages,
                          char *err, size_t err_size) {
    *stages = 0;
    static area ar[AREAS];
    static light_set lights[LIGHT_SETS];
    const unsigned nlights = read_light_sets(unpacked_dir, lights);
    for (unsigned s = STAGE_FIRST; s <= STAGE_LAST; ++s) {
        char path[1200];
        snprintf(path, sizeof path, "%s/GG/COM/FDAT/%03u.bin", unpacked_dir, s * 5 + 4);
        size_t size = 0;
        uint8_t *d = read_all(path, &size);
        if (!d) continue;
        chunk ch[CHUNKS];
        if (!split(d, size, ch)) { free(d); continue; }
        const unsigned areas = read_areas(&ch[8], ar);
        if (!areas) { free(d); continue; }
        char dir[1200];
        snprintf(dir, sizeof dir, "%s/%02u", out_dir, s);
        uint32_t faces = 0, polys = 0, tfaces = 0, textures = 0;
        oa_box oa[8];
        unsigned noa = 0;
        {
            char cpath[1200];
            snprintf(cpath, sizeof cpath, "%s/GG/COM/FDAT/%03u.bin", unpacked_dir, s * 5 + 2);
            size_t cn = 0;
            uint8_t *code = read_all(cpath, &cn);
            noa = read_out_of_area(code, cn, &ch[10], oa, 8);
            free(code);
        }
        if (!make_dir(dir) || !write_visual(dir, &ch[4], ar, areas, &faces) ||
            !write_textured(dir, "visual", unpacked_dir, s, &ch[1], ar, areas, nlights ? lights : NULL, &tfaces,
                            &textures) ||
            !write_collision(dir, &ch[7], ar, areas, &polys) ||
            !write_collision_game(dir, &ch[7], ar, areas) ||
            !write_toml(dir, s, &ch[13], ar, areas, tfaces ? tfaces : faces, polys, oa, noa)) {
            free(d);
            set_err(err, err_size, "cannot write the stage files in", dir);
            return 0;
        }
        {
            area sky[4];
            const unsigned nsky = chunk3_areas(&ch[3], sky, 4, 0);
            uint32_t sf = 0, st = 0;
            char path2[1300];
            snprintf(path2, sizeof path2, "%s/sky.obj", dir);
            remove(path2);   /* a stage without a sky keeps none from an older import */
            if (nsky && !write_textured(dir, "sky", unpacked_dir, s, &ch[3], sky, nsky, NULL, &sf, &st)) {
                free(d);
                set_err(err, err_size, "cannot write the sky in", dir);
                return 0;
            }
            area far[4];
            const unsigned nfar = chunk3_areas(&ch[3], far, 4, 1);
            snprintf(path2, sizeof path2, "%s/far.obj", dir);
            remove(path2);
            /* Lit like an area on set 0 (the stages' floors around it). */
            if (nfar && !write_textured(dir, "far", unpacked_dir, s, &ch[3], far, nfar, nlights ? lights : NULL, &sf, &st)) {
                free(d);
                set_err(err, err_size, "cannot write the far plane in", dir);
                return 0;
            }
        }
        free(d);
        ++*stages;
    }
    return 1;
}

/* ---- VRAM for other exporters (mission units, acr_units_moa.c) ---- */

acr_moa_vram *acr_moa_vram_load(const char *unpacked_dir, unsigned stage) {
    char path[1200];
    snprintf(path, sizeof path, "%s/GG/COM/RTIM/%03u.bin", unpacked_dir, stage);
    size_t n = 0;
    uint8_t *d = read_all(path, &n);
    if (!d) return NULL;
    vram *v = calloc(1, sizeof *v);
    if (v && !load_tims(d, n, v)) { free(v); v = NULL; }
    free(d);
    return v;
}

void acr_moa_vram_free(acr_moa_vram *v) { free(v); }

int acr_moa_vram_has_page(const acr_moa_vram *v, unsigned tpage) { return page_loaded(v, tpage); }

int acr_moa_vram_semi(const acr_moa_vram *v, unsigned tpage, unsigned clut) {
    return clut_has_stp(v, (uint16_t)clut, ((tpage >> 7) & 3) ? 256 : 16);
}

unsigned acr_moa_vram_texel(const acr_moa_vram *v, unsigned tpage, unsigned clut, unsigned u, unsigned t) {
    const unsigned px = (tpage & 15) * 64, py = ((tpage >> 4) & 1) * 256 + (t & 255), depth = (tpage >> 7) & 3;
    unsigned idx;
    if (depth == 0) idx = (v->px[py][px + (u & 255) / 4] >> ((u & 3) * 4)) & 15;
    else if (depth == 1) idx = (v->px[py][px + (u & 255) / 2] >> ((u & 1) * 8)) & 255;
    else return v->px[py][(px + (u & 255)) % VRAM_W];
    const unsigned cx = (clut & 63) * 16 + idx, cy = clut >> 6;
    return cy < VRAM_H && cx < VRAM_W ? v->px[cy][cx] : 0;
}

int acr_moa_vram_write_page(const char *path, const acr_moa_vram *v, unsigned tpage, unsigned clut) {
    const tex_key k = { (uint16_t)(tpage & 0x1FF), (uint16_t)clut, 0 };
    return write_page(path, v, k);
}
