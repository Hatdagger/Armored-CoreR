/* Armored Core 2 part models: format notes in docs/research/gen2-models.md. */
#include "acr_models_ac2.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define ac2_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define ac2_mkdir(p) mkdir((p), 0755)
#endif

#define AC2_PATH 1200
#define AC2_TABLE_BYTES 32768u      /* (sector, sectors) pairs; the pair's index is the file id */
#define AC2_MAX_MESHES 64

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float lef(const uint8_t *p) { uint32_t u = le32(p); float f; memcpy(&f, &u, 4); return f; }

static void set_err(char *err, size_t n, const char *msg, const char *detail) {
    if (err && n) snprintf(err, n, "%s%s%s", msg, detail ? ": " : "", detail ? detail : "");
}

static int make_dirs(const char *path) {
    char buf[AC2_PATH];
    const size_t len = strlen(path);
    if (len >= sizeof buf) return 0;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i <= len; ++i)
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            const char saved = buf[i];
            buf[i] = '\0';
            if (ac2_mkdir(buf) != 0 && errno != EEXIST) return 0;
            buf[i] = saved;
        }
    return 1;
}

/* ---- one part model ---- */

/* The header: u32 mesh count, u32 bytes, 0xFFFFFFFF, 0. Part files put it at
 * 0, or at 0x8000 behind 32 KB of other data (texture pages). */
static int is_header(const uint8_t *d, size_t bytes, size_t at) {
    if (at + 16 > bytes) return 0;
    const uint32_t n = le32(d + at);
    return n > 0 && n < AC2_MAX_MESHES && le32(d + at + 8) == 0xFFFFFFFFu && le32(d + at + 12) == 0;
}

uint32_t acr_ac2_model_tris(const uint8_t *data, size_t bytes, const float offset[3], acr_ac2_tri *tri,
                            uint32_t max, float (*pivot)[3], int max_pivots, int *meshes) {
    size_t base;
    if (is_header(data, bytes, 0)) base = 0;
    else if (is_header(data, bytes, 0x8000)) base = 0x8000;
    else return 0;
    const uint32_t n = le32(data + base);
    size_t end_all = base + le32(data + base + 4);
    if (end_all > bytes) end_all = bytes;
    const size_t blocks = base + 16 + 16u * n;        /* then n blocks of 0x50: joint position first */
    if (blocks + 0x50u * n > bytes) return 0;
    if (meshes) *meshes = (int)n;
    uint32_t count = 0;
    for (uint32_t m = 0; m < n; ++m) {
        float at[3];
        for (int k = 0; k < 3; ++k) at[k] = lef(data + blocks + 0x50u * m + 4u * k) + (offset ? offset[k] : 0.0f);
        if (pivot && (int)m < max_pivots) memcpy(pivot[m], at, sizeof at);
        /* The mesh's DMA chain runs from its start to the next mesh's. */
        size_t o = base + le32(data + base + 16 + 16u * m + 4);
        size_t end = m + 1 < n ? base + le32(data + base + 16 + 16u * (m + 1) + 4) : end_all;
        if (end > end_all) end = end_all;
        while (o + 4 <= end) {
            const uint32_t w = le32(data + o);
            const uint32_t cmd = (w >> 24) & 0x7F, num = (w >> 16) & 0xFF;
            /* VIF UNPACK V4-32: a GIF tag (NLOOP vertices, triangle strip),
             * then per vertex position, normal, UV, colour (one qword each). */
            if (cmd == 0x6C && num >= 5 && (num - 1) % 4 == 0 && o + 4 + 16u * num <= bytes &&
                (le32(data + o + 4) & 0x7FFF) * 4 + 1 == num) {
                const uint32_t verts = (num - 1) / 4;
                const uint8_t *v0 = data + o + 4 + 16;
                for (uint32_t k = 0; k + 2 < verts; ++k) {
                    const uint32_t idx[3] = { k, k + 1, k + 2 };
                    acr_ac2_tri t;
                    memset(&t, 0, sizeof t);
                    t.mesh = (uint8_t)m;
                    float nsum[3] = { 0, 0, 0 };
                    for (int c = 0; c < 3; ++c) {
                        const uint8_t *v = v0 + 64u * idx[c];
                        /* AC2: y down, facing -z -> model space: y up, facing +z */
                        t.p[c][0] = (lef(v + 0) + at[0]) * ACR_AC2_SCALE;
                        t.p[c][1] = -(lef(v + 4) + at[1]) * ACR_AC2_SCALE;
                        t.p[c][2] = -(lef(v + 8) + at[2]) * ACR_AC2_SCALE;
                        t.n[c][0] = lef(v + 16);
                        t.n[c][1] = -lef(v + 20);
                        t.n[c][2] = -lef(v + 24);
                        t.uv[c][0] = lef(v + 32);
                        t.uv[c][1] = lef(v + 36);
                        for (int q = 0; q < 3; ++q) nsum[q] += t.n[c][q];
                    }
                    float a[3], b[3];
                    for (int q = 0; q < 3; ++q) { a[q] = t.p[1][q] - t.p[0][q]; b[q] = t.p[2][q] - t.p[0][q]; }
                    const float cr[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
                    const float area = cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2];
                    if (area < 1e-6f) continue;                           /* strip joins */
                    if (cr[0] * nsum[0] + cr[1] * nsum[1] + cr[2] * nsum[2] < 0) {   /* face the normals */
                        acr_ac2_tri s = t;
                        memcpy(t.p[1], s.p[2], sizeof t.p[1]); memcpy(t.p[2], s.p[1], sizeof t.p[2]);
                        memcpy(t.n[1], s.n[2], sizeof t.n[1]); memcpy(t.n[2], s.n[1], sizeof t.n[2]);
                        memcpy(t.uv[1], s.uv[2], sizeof t.uv[1]); memcpy(t.uv[2], s.uv[1], sizeof t.uv[2]);
                    }
                    if (tri && count < max) tri[count] = t;
                    ++count;
                }
                o += 4 + 16u * num;
                continue;
            }
            o += 4;
        }
    }
    return count;
}

/* ---- OBJ ---- */

typedef struct obj_out { FILE *f; uint32_t verts; } obj_out;

static void obj_tris(obj_out *o, const acr_ac2_tri *t, uint32_t n, const char *name, const char *mtl,
                     float lift) {
    fprintf(o->f, "o %s\nusemtl %s\n", name, mtl);
    for (uint32_t i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c)
            fprintf(o->f, "v %.3f %.3f %.3f\nvn %.4f %.4f %.4f\nvt %.4f %.4f\n", t[i].p[c][0], t[i].p[c][1] + lift,
                    t[i].p[c][2], t[i].n[c][0], t[i].n[c][1], t[i].n[c][2], t[i].uv[c][0], 1.0f - t[i].uv[c][1]);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t b = o->verts + 3 * i + 1;
        fprintf(o->f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", b, b, b, b + 1, b + 1, b + 1, b + 2, b + 2, b + 2);
    }
    o->verts += 3 * n;
}

/* ---- AC2DATA.BIN ---- */

typedef struct ac2_archive {
    acr_disc *disc;
    uint32_t lba, sectors;
    uint32_t table[AC2_TABLE_BYTES / 4];
} ac2_archive;

typedef struct find_ctx { const char *want; uint32_t lba, bytes; int found; } find_ctx;
static int find_visit(void *ctx, const char *path, uint32_t lba, uint32_t bytes, int is_dir) {
    find_ctx *f = (find_ctx *)ctx;
    if (is_dir) return 1;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t i = 0;
    for (; base[i] && f->want[i]; ++i)
        if (toupper((unsigned char)base[i]) != f->want[i]) break;
    if (!base[i] && !f->want[i]) { f->lba = lba; f->bytes = bytes; f->found = 1; return 0; }
    return 1;
}

int acr_disc_is_ac2(acr_disc *disc) {
    find_ctx f = { "SLUS_200.14", 0, 0, 0 };
    acr_iso_walk(disc, find_visit, &f);
    return f.found;
}

/* File `id`'s bytes, with 32 KB more after it: a model's header size can
 * run past the table's end for the file (legs 381 into 382). */
static uint8_t *read_file(ac2_archive *a, uint32_t id, size_t *bytes) {
    if (id >= AC2_TABLE_BYTES / 8) return NULL;
    const uint32_t off = a->table[2 * id], size = a->table[2 * id + 1];
    if (!size || off < AC2_TABLE_BYTES / 2048 || off + size > a->sectors) return NULL;
    uint32_t n = size + 16;
    if (off + n > a->sectors) n = a->sectors - off;
    uint8_t *d = malloc((size_t)n * 2048u);
    if (!d) return NULL;
    for (uint32_t s = 0; s < n; ++s) {
        int form2 = 0;
        if (!acr_disc_read_user(a->disc, a->lba + off + s, d + (size_t)s * 2048u, &form2)) { free(d); return NULL; }
    }
    *bytes = (size_t)n * 2048u;
    return d;
}

/* What the ids are (docs/research/gen2-models.md; checked by drawing them). */
typedef struct ac2_range { const char *kind; uint16_t first, last; } ac2_range;
static const ac2_range k_ranges[] = {
    { "core", 301, 306 }, { "booster", 310, 325 }, { "head", 332, 342 }, { "head", 351, 351 },
    { "arms", 352, 373 }, { "legs", 381, 411 }, { "weapon-r", 501, 525 },
};

/* The test AC (2026-11-27): a complete standard biped, drawn and checked. */
enum { T_CORE = 301, T_HEAD = 332, T_ARMS = 352, T_LEGS = 390, T_BOOSTER = 310, T_WEAPON = 501 };

#define AC2_MAX_TRIS 20000

int acr_export_ac2_models(acr_disc *disc, const char *out_dir, uint32_t *parts, char *err, size_t err_size) {
    if (parts) *parts = 0;
    find_ctx f = { "AC2DATA.BIN", 0, 0, 0 };
    acr_iso_walk(disc, find_visit, &f);
    if (!f.found) { set_err(err, err_size, "no AC2DATA.BIN on the Armored Core 2 disc", NULL); return 0; }
    ac2_archive *a = calloc(1, sizeof *a);
    acr_ac2_tri *tri = malloc(sizeof *tri * AC2_MAX_TRIS);
    if (!a || !tri) { free(a); free(tri); set_err(err, err_size, "out of memory", NULL); return 0; }
    a->disc = disc;
    a->lba = f.lba;
    a->sectors = (uint32_t)(((uint64_t)f.bytes + 2047) / 2048);
    uint8_t sec[2048];
    for (uint32_t s = 0; s < AC2_TABLE_BYTES / 2048; ++s) {
        int form2 = 0;
        if (!acr_disc_read_user(disc, a->lba + s, sec, &form2)) {
            free(a); free(tri);
            set_err(err, err_size, "cannot read", "AC2DATA.BIN");
            return 0;
        }
        for (int i = 0; i < 512; ++i) a->table[s * 512 + i] = le32(sec + 4 * i);
    }
    char path[AC2_PATH];
    int ok = 1;
    /* Every part on its own (at its place on the AC, feet not lifted). */
    for (size_t r = 0; ok && r < sizeof k_ranges / sizeof k_ranges[0]; ++r)
        for (uint32_t id = k_ranges[r].first; ok && id <= k_ranges[r].last; ++id) {
            size_t bytes = 0;
            uint8_t *d = read_file(a, id, &bytes);
            if (!d) continue;
            const uint32_t n = acr_ac2_model_tris(d, bytes, NULL, tri, AC2_MAX_TRIS, NULL, 0, NULL);
            free(d);
            if (!n) continue;
            snprintf(path, sizeof path, "%s/gen2/ac2/parts/%s", out_dir, k_ranges[r].kind);
            if (!make_dirs(path)) { ok = 0; break; }
            snprintf(path, sizeof path, "%s/gen2/ac2/parts/%s/%04u.obj", out_dir, k_ranges[r].kind, (unsigned)id);
            obj_out o = { fopen(path, "w"), 0 };
            if (!o.f) { ok = 0; break; }
            fprintf(o.f, "# Armored Core 2 file %u (%s), Gen 1 model space x%.0f\n", (unsigned)id, k_ranges[r].kind,
                    (double)ACR_AC2_SCALE);
            obj_tris(&o, tri, n < AC2_MAX_TRIS ? n : AC2_MAX_TRIS, k_ranges[r].kind, "part", 0.0f);
            ok = fclose(o.f) == 0;
            if (ok && parts) ++*parts;
        }
    if (!ok) { free(a); free(tri); set_err(err, err_size, "cannot write", path); return 0; }

    /* The test AC: every part at its own place (AC2 stores them assembled),
     * the arm weapon on the right hand. */
    static const struct { uint16_t id; const char *name, *mtl; } k_test[] = {
        { T_LEGS, "legs", "frame" }, { T_CORE, "core", "frame" }, { T_HEAD, "head", "frame" },
        { T_ARMS, "arms", "frame" }, { T_BOOSTER, "booster", "joint" }, { T_WEAPON, "weapon-r", "weapon" },
    };
    uint32_t start[8] = { 0 }, used = 0;
    float hand[AC2_MAX_MESHES][3];
    int arm_meshes = 0;
    {
        size_t bytes = 0;
        uint8_t *d = read_file(a, T_ARMS, &bytes);
        if (d) acr_ac2_model_tris(d, bytes, NULL, NULL, 0, hand, AC2_MAX_MESHES, &arm_meshes);
        free(d);
    }
    for (size_t i = 0; i < sizeof k_test / sizeof k_test[0]; ++i) {
        start[i] = used;
        size_t bytes = 0;
        uint8_t *d = read_file(a, k_test[i].id, &bytes);
        if (!d) { set_err(err, err_size, "Armored Core 2 test AC: a part is missing", k_test[i].name); ok = 0; break; }
        /* Hand weapons sit at the origin: put them on the right hand,
         * the arms' last joint on the -x side (Gen 1's right is -x). */
        float off[3] = { 0, 0, 0 };
        if (k_test[i].id == T_WEAPON && arm_meshes >= 8) {
            const int h = hand[3][0] < 0 ? 3 : 7;
            memcpy(off, hand[h], sizeof off);
        }
        used += acr_ac2_model_tris(d, bytes, off, tri + used, AC2_MAX_TRIS - used, NULL, 0, NULL);
        if (used > AC2_MAX_TRIS) used = AC2_MAX_TRIS;
        free(d);
    }
    start[sizeof k_test / sizeof k_test[0]] = used;
    float floor_y = 1e9f, top = -1e9f;
    for (uint32_t t = 0; t < start[1]; ++t)          /* the legs' lowest point stands on 0 */
        for (int c = 0; c < 3; ++c) if (tri[t].p[c][1] < floor_y) floor_y = tri[t].p[c][1];
    for (uint32_t t = 0; t < used; ++t)
        for (int c = 0; c < 3; ++c) if (tri[t].p[c][1] > top) top = tri[t].p[c][1];
    if (ok) {
        snprintf(path, sizeof path, "%s/gen2/ac2/test-ac", out_dir);
        ok = make_dirs(path);
    }
    if (ok) {
        snprintf(path, sizeof path, "%s/gen2/ac2/test-ac/ac.obj", out_dir);
        obj_out o = { fopen(path, "w"), 0 };
        ok = o.f != NULL;
        if (ok) {
            fprintf(o.f, "# Armored Core 2 test AC, Gen 1 model space (x%.0f), feet on 0\nmtllib ac.mtl\n",
                    (double)ACR_AC2_SCALE);
            for (size_t i = 0; i < sizeof k_test / sizeof k_test[0]; ++i)
                obj_tris(&o, tri + start[i], start[i + 1] - start[i], k_test[i].name, k_test[i].mtl, -floor_y);
            ok = fclose(o.f) == 0;
        }
    }
    if (ok) {
        /* No textures yet (AC2's texture pages are not decoded): flat paint. */
        snprintf(path, sizeof path, "%s/gen2/ac2/test-ac/ac.mtl", out_dir);
        FILE *m = fopen(path, "w");
        ok = m != NULL;
        if (ok) {
            fputs("newmtl frame\nKd 0.62 0.64 0.60\nnewmtl joint\nKd 0.40 0.41 0.42\n"
                  "newmtl weapon\nKd 0.33 0.34 0.36\n", m);
            ok = fclose(m) == 0;
        }
    }
    if (ok) {
        snprintf(path, sizeof path, "%s/gen2/ac2/test-ac/build.toml", out_dir);
        FILE *b = fopen(path, "w");
        ok = b != NULL;
        if (ok) {
            fprintf(b, "# Armored Core 2 test AC (AC2DATA.BIN file ids)\n"
                       "core = %d\nhead = %d\narms = %d\nlegs = %d\nbooster = %d\nweapon_r = %d\n"
                       "scale = %.1f      # AC2 units -> Gen 1 model units (same height as a Gen 1 AC)\n"
                       "height = %.0f     # model units, feet to top (Gen 1 world = a quarter)\n"
                       "triangles = %u\n",
                    T_CORE, T_HEAD, T_ARMS, T_LEGS, T_BOOSTER, T_WEAPON, (double)ACR_AC2_SCALE,
                    (double)(top - floor_y), (unsigned)used);
            ok = fclose(b) == 0;
        }
    }
    free(a);
    free(tri);
    if (!ok && err && !err[0]) set_err(err, err_size, "cannot write", path);
    return ok;
}
