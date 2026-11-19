/* Master of Arena arena stages as readable files. Format notes:
 * docs/research/gen1-stages.md. Everything is read from the user's own
 * import folder; nothing from the disc is built into this program. */
#include "acr_stages_moa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define acr_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define acr_mkdir(p) mkdir((p), 0755)
#endif

#define STAGE_FIRST 20u
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
 * centre +0x10, turn +0x16 (quarter turns in bits 10-11), model +0x1E. */
typedef struct area { int16_t min[3], max[3], centre[3]; uint8_t quarter; int16_t model; } area;

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
    const unsigned n = (mode & 8) ? 4 : 3, tex = mode & 4, gour = mode & 0x10, lit = !(flag & 1);
    unsigned i0 = 0;
    if (tex) i0 = 2 * n;
    col->r = col->g = col->b = 128;
    col->semi = (uint8_t)((mode & 2) != 0);
    if (!lit) {
        col->r = w[i0 * 2]; col->g = w[i0 * 2 + 1]; col->b = w[i0 * 2 + 2];
        i0 += 2 * (gour ? n : 1);
        for (unsigned j = 0; j < n; ++j) v[j] = u16(w + (i0 + j) * 2);
    } else {
        if (!tex) { col->r = w[0]; col->g = w[1]; col->b = w[2]; i0 = 2; }
        if (gour) for (unsigned j = 0; j < n; ++j) v[j] = u16(w + (i0 + 2 * j + 1) * 2);
        else {
            v[0] = u16(w + (i0 + 1) * 2);
            for (unsigned j = 1; j < n; ++j) v[j] = u16(w + (i0 + 1 + j) * 2);
        }
    }
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
    snprintf(path, sizeof path, "%s/visual.obj", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Master of Arena stage models (FDAT 5s+4 chunk 4) placed by the area records.\n"
               "# Engine world: x = -(game x), y up = -(game y), z = game z; world units.\n"
               "mtllib visual.mtl\n");
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
    snprintf(path, sizeof path, "%s/visual.mtl", dir);
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

/* ---- points (chunk 13): 40-byte records; 1 and 2 are the two starts ---- */

static int write_toml(const char *dir, unsigned stage, const chunk *pts, const area *ar, unsigned areas,
                      uint32_t faces, uint32_t polys) {
    char path[1200];
    snprintf(path, sizeof path, "%s/stage.toml", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    const char *name = acr_moa_stage_name(stage);
    fprintf(f, "# Master of Arena stage %u (FDAT %u). Engine world units; y is down as in the\n"
               "# game (the floor of Green Silo is at -662), x mirrored (x = -(game x)).\n",
            stage, stage * 5 + 4);
    fprintf(f, "id = %u\nname = \"%s\"\n", stage, name ? name : "");
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
    fclose(f);
    return 1;
}

int acr_export_moa_stages(const char *unpacked_dir, const char *out_dir, uint32_t *stages,
                          char *err, size_t err_size) {
    *stages = 0;
    static area ar[AREAS];
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
        uint32_t faces = 0, polys = 0;
        if (!make_dir(dir) || !write_visual(dir, &ch[4], ar, areas, &faces) ||
            !write_collision(dir, &ch[7], ar, areas, &polys) ||
            !write_toml(dir, s, &ch[13], ar, areas, faces, polys)) {
            free(d);
            set_err(err, err_size, "cannot write the stage files in", dir);
            return 0;
        }
        free(d);
        ++*stages;
    }
    return 1;
}
