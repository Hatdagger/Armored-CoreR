/* Armored Core 2 part models (Gen 2): format notes in docs/research/gen2-models.md
 * and gen2-archive.md. */
#include "acr_models_ac2.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
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

#include "acr_models_moa.h"   /* acr_write_png8 */

#define AC2_PATH 1200
#define AC2_TABLE_BYTES 32768u      /* (sector, sectors) pairs; the pair's index is the file id */
#define AC2_MAX_IDS (AC2_TABLE_BYTES / 8)
#define AC2_MAX_MESHES 64
#define AC2_MAX_TEX 16
#define AC2_MAX_TRIS 20000

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }
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

/* The header: u32 mesh count, u32 bytes, 0xFFFFFFFF, 0, at the file's start. */
static int is_header(const uint8_t *d, size_t bytes) {
    if (bytes < 16) return 0;
    const uint32_t n = le32(d);
    return n > 0 && n < AC2_MAX_MESHES && le32(d + 8) == 0xFFFFFFFFu && le32(d + 12) == 0;
}

/* Per mesh, from its 0x50 block: pos[3] 1, rot[3] (radians) 1, scale[3] 1, ... */
typedef struct mesh_info { float pos[3], rot[3]; } mesh_info;

/* R = Rz Ry Rx (order unconfirmed: only small core pieces rotate). */
static void euler(const float r[3], float R[9]) {
    const float cx = cosf(r[0]), sx = sinf(r[0]), cy = cosf(r[1]), sy = sinf(r[1]), cz = cosf(r[2]), sz = sinf(r[2]);
    R[0] = cz * cy; R[1] = cz * sy * sx - sz * cx; R[2] = cz * sy * cx + sz * sx;
    R[3] = sz * cy; R[4] = sz * sy * sx + cz * cx; R[5] = sz * sy * cx - cz * sx;
    R[6] = -sy;     R[7] = cy * sx;                R[8] = cy * cx;
}

/* Decodes the model. local = 0: vertex = (joint + vertex + offset) x scale,
 * y and z negated (Gen 1 model space). local = 1: the mesh's own space
 * (joint and rotation taken off), y and z negated, AC2 units (glTF nodes). */
static uint32_t decode(const uint8_t *data, size_t bytes, int local, float scale, const float offset[3],
                       acr_ac2_tri *tri, uint32_t max, mesh_info *info, int max_info, int *meshes) {
    if (!is_header(data, bytes)) return 0;
    const uint32_t n = le32(data);
    size_t end_all = le32(data + 4);
    if (end_all > bytes) end_all = bytes;
    const size_t blocks = 16 + 16u * n;
    if (blocks + 0x50u * n > bytes) return 0;
    if (meshes) *meshes = (int)n;
    uint32_t count = 0;
    for (uint32_t m = 0; m < n; ++m) {
        const uint8_t *blk = data + blocks + 0x50u * m;
        float at[3], rot[3], R[9];
        for (int k = 0; k < 3; ++k) {
            at[k] = lef(blk + 4u * k) + (offset ? offset[k] : 0.0f);
            rot[k] = lef(blk + 16 + 4u * k);
            if (!isfinite(rot[k])) rot[k] = 0;
        }
        euler(rot, R);
        if (info && (int)m < max_info) { memcpy(info[m].pos, at, sizeof at); memcpy(info[m].rot, rot, sizeof rot); }
        /* The mesh's DMA chain runs from its start to the next mesh's. */
        size_t o = le32(data + 16 + 16u * m + 4);
        size_t end = m + 1 < n ? le32(data + 16 + 16u * (m + 1) + 4) : end_all;
        if (end > end_all) end = end_all;
        uint64_t tex0 = 0;
        while (o + 4 <= end) {
            const uint32_t w = le32(data + o);
            const uint32_t cmd = (w >> 24) & 0x7F, addr = w & 0x3FF;
            const uint32_t num = ((w >> 16) & 0xFF) ? ((w >> 16) & 0xFF) : 256;
            if (cmd == 0x6C && addr == 256 && o + 4 + 16u * num <= bytes) {
                /* A GIF A+D packet to VU address 256: register 0x06 (TEX0_1)
                 * picks the texture of the strips that follow. */
                for (uint32_t k = 1; k < num; ++k)
                    if (le64(data + o + 4 + 16u * k + 8) == 0x06) tex0 = le64(data + o + 4 + 16u * k);
                o += 4 + 16u * num;
                continue;
            }
            /* VIF UNPACK V4-32: a GIF tag (NLOOP vertices, triangle strip),
             * then per vertex position, normal, UV, colour (one qword each). */
            if (cmd == 0x6C && num >= 5 && (num - 1) % 4 == 0 && o + 4 + 16u * num <= bytes &&
                (le32(data + o + 4) & 0x7FFF) * 4 + 1 == num) {
                const uint32_t verts = (num - 1) / 4;
                const uint8_t *v0 = data + o + 4 + 16;
                const int marker = tex0 == 0 && verts == 3;
                for (uint32_t k = 0; k + 2 < verts; ++k) {
                    acr_ac2_tri t;
                    memset(&t, 0, sizeof t);
                    t.mesh = (uint8_t)m;
                    t.tex0 = tex0;
                    t.marker = (uint8_t)marker;
                    float nsum[3] = { 0, 0, 0 };
                    for (int c = 0; c < 3; ++c) {
                        const uint8_t *v = v0 + 64u * (k + (uint32_t)c);
                        float q[3], nn[3];
                        for (int i = 0; i < 3; ++i) { q[i] = lef(v + 4u * i); nn[i] = lef(v + 16 + 4u * i); }
                        if (local) {        /* R^T: back to the mesh's own space */
                            float a[3], b[3];
                            for (int i = 0; i < 3; ++i) {
                                a[i] = R[i] * q[0] + R[3 + i] * q[1] + R[6 + i] * q[2];
                                b[i] = R[i] * nn[0] + R[3 + i] * nn[1] + R[6 + i] * nn[2];
                            }
                            memcpy(q, a, sizeof q); memcpy(nn, b, sizeof nn);
                        } else {
                            for (int i = 0; i < 3; ++i) q[i] = (q[i] + at[i]) * scale;
                        }
                        /* AC2: y down, facing -z -> y up, facing +z */
                        t.p[c][0] = q[0]; t.p[c][1] = -q[1]; t.p[c][2] = -q[2];
                        t.n[c][0] = nn[0]; t.n[c][1] = -nn[1]; t.n[c][2] = -nn[2];
                        t.uv[c][0] = lef(v + 32);
                        t.uv[c][1] = lef(v + 36);
                        for (int i = 0; i < 3; ++i) nsum[i] += t.n[c][i];
                    }
                    float a[3], b[3];
                    for (int i = 0; i < 3; ++i) { a[i] = t.p[1][i] - t.p[0][i]; b[i] = t.p[2][i] - t.p[0][i]; }
                    const float cr[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
                    const float area = cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2];
                    const float tiny = local ? 1e-12f : 1e-6f;
                    if (area < tiny) continue;                           /* strip joins */
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

uint32_t acr_ac2_model_tris(const uint8_t *data, size_t bytes, const float offset[3], acr_ac2_tri *tri,
                            uint32_t max, float (*pivot)[3], int max_pivots, int *meshes) {
    mesh_info info[AC2_MAX_MESHES];
    const uint32_t n = decode(data, bytes, 0, ACR_AC2_SCALE, offset, tri, max, info, AC2_MAX_MESHES, meshes);
    if (pivot)
        for (int m = 0; m < max_pivots && m < AC2_MAX_MESHES && meshes && m < *meshes; ++m)
            memcpy(pivot[m], info[m].pos, sizeof info[m].pos);
    return n;
}

/* ---- shop package textures ---- */

/* Header 0x20: u32 total, u32 pixel bytes, u32 count, u32 0, u64 TEX0; pixels
 * (linear), then the palette: 16 RGBA (4-bit) or 256 RGBA with the CSM1 row
 * swap (8-bit). Alpha 0x80 = opaque. */
int acr_ac2_package_textures(const uint8_t *pkg, size_t bytes, acr_ac2_tex *out, int max) {
    if (bytes < 0x54 || memcmp(pkg, "JOIN", 4) != 0) return 0;
    int count = 0;
    for (int s = 0; s < 8 && count < max; ++s) {
        const uint32_t size = le32(pkg + 0x14 + 4u * s), at = le32(pkg + 0x34 + 4u * s);
        if (at == 0xFFFFFFFFu || at >= bytes) continue;
        const size_t lim = (size_t)at + size <= bytes ? (size_t)at + size : bytes;
        for (size_t o = at; o + 0x20 <= lim && count < max; o += 16) {
            const uint8_t *h = pkg + o;
            const uint32_t tot = le32(h), pix = le32(h + 4), n = le32(h + 8);
            if (le32(h + 12) || !pix || (pix & (pix - 1)) || n < 1 || n > 4 || !(pix < tot && tot <= pix + 0x1000)) continue;
            const uint64_t t0 = le64(h + 16);
            const unsigned psm = (unsigned)(t0 >> 20) & 0x3F;
            const unsigned w = 1u << ((t0 >> 26) & 0xF), hh = 1u << ((t0 >> 30) & 0xF);
            if (psm != 0x13 && psm != 0x14) continue;
            const unsigned bpp = psm == 0x13 ? 8 : 4;
            const size_t pal_bytes = bpp == 8 ? 1024 : 64;
            if ((size_t)w * hh * bpp / 8 != pix || o + 0x20 + pix + pal_bytes > lim) continue;
            acr_ac2_tex *t = &out[count];
            memset(t, 0, sizeof *t);
            t->tex0 = t0; t->w = w; t->h = hh;
            t->pixels = malloc((size_t)w * hh);
            if (!t->pixels) break;
            const uint8_t *p = h + 0x20;
            for (size_t i = 0; i < (size_t)w * hh; ++i)
                t->pixels[i] = bpp == 8 ? p[i] : (uint8_t)((i & 1) ? p[i / 2] >> 4 : p[i / 2] & 0x0F);
            const uint8_t *pal = p + pix;
            for (unsigned i = 0; i < (bpp == 8 ? 256u : 16u); ++i) {
                const unsigned src = bpp == 8 ? ((i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)) : i;
                memcpy(t->rgb + 3 * i, pal + 4 * src, 3);
                const unsigned a = pal[4 * src + 3] * 2u;
                t->alpha[i] = (uint8_t)(a > 255 ? 255 : a);
            }
            ++count;
            o += 0x20 + pix + pal_bytes - 16;   /* past this texture (the loop adds 16) */
        }
    }
    return count;
}

void acr_ac2_tex_free(acr_ac2_tex *t) {
    free(t->pixels);
    t->pixels = NULL;
}

/* ---- where the files come from: the disc, or a split folder ---- */

typedef struct ac2_src {
    acr_disc *disc;
    uint32_t lba, sectors;
    uint32_t start[AC2_MAX_IDS], len[AC2_MAX_IDS];   /* corrected start and length, sectors (len 0: none) */
    const char *dir;                                 /* split folder, or NULL */
} ac2_src;

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

/* The F block's categories: a package stores its index within one, which
 * tells it from a neighbour's. */
static const uint16_t k_f_groups[][2] = {
    { 3400, 3411 }, { 3420, 3425 }, { 3430, 3452 }, { 3460, 3490 }, { 3500, 3506 }, { 3520, 3529 },
    { 3540, 3547 }, { 3560, 3567 }, { 3580, 3586 }, { 3600, 3607 }, { 3620, 3651 }, { 3660, 3691 },
    { 3700, 3724 }, { 3730, 3738 }, { 3750, 3765 },
};

/* Whether the sector holds the start of file id (only ids whose start can be told). */
static int looks_like(uint32_t id, const uint8_t *sec) {
    if (id >= 3400) {
        if (memcmp(sec, "JOIN", 4) != 0) return 0;
        for (size_t g = 0; g < sizeof k_f_groups / sizeof k_f_groups[0]; ++g)
            if (id >= k_f_groups[g][0] && id <= k_f_groups[g][1]) return le32(sec + 0x10) == id - k_f_groups[g][0];
        return 1;
    }
    if (id >= 300 && id <= 1699) {
        const uint32_t s = le32(sec + 4);
        return is_header(sec, 16) && s > 0x40 && s < 0x80000;
    }
    return -1;     /* cannot tell */
}

static int cmp_ctx_lt(const ac2_src *a, uint32_t x, uint32_t y) {
    return a->start[x] != a->start[y] ? a->start[x] < a->start[y] : x < y;
}

/* AC2DATA.BIN's table points 16 sectors (32 KB) early: real start = table
 * offset + 16, checked per file against its header where it has one
 * (tools/gen2/split_bin.py does the same). A file runs to the next one's start. */
static int src_open_disc(ac2_src *a, acr_disc *disc, char *err, size_t err_size) {
    find_ctx f = { "AC2DATA.BIN", 0, 0, 0 };
    acr_iso_walk(disc, find_visit, &f);
    if (!f.found) { set_err(err, err_size, "no AC2DATA.BIN on the Armored Core 2 disc", NULL); return 0; }
    a->disc = disc;
    a->lba = f.lba;
    a->sectors = (uint32_t)(((uint64_t)f.bytes + 2047) / 2048);
    static uint32_t table[AC2_TABLE_BYTES / 4];
    uint8_t sec[2048];
    for (uint32_t s = 0; s < AC2_TABLE_BYTES / 2048; ++s) {
        int form2 = 0;
        if (!acr_disc_read_user(disc, a->lba + s, sec, &form2)) { set_err(err, err_size, "cannot read", "AC2DATA.BIN"); return 0; }
        for (int i = 0; i < 512; ++i) table[s * 512 + i] = le32(sec + 4 * i);
    }
    const uint32_t table_end = table[0] + table[1];
    uint32_t last = 0;
    static const int shifts[] = { 16, 0, 32, -16 };
    static uint32_t order[AC2_MAX_IDS];
    uint32_t n = 0;
    for (uint32_t id = 1; id < AC2_MAX_IDS; ++id) {
        const uint32_t off = table[2 * id], size = table[2 * id + 1];
        if (!size || off < table_end || off + size > a->sectors || off < last) continue;
        last = off;
        int shift = 16;
        for (size_t k = 0; k < sizeof shifts / sizeof shifts[0]; ++k) {
            int form2 = 0;
            if ((int64_t)off + shifts[k] < 0 || !acr_disc_read_user(disc, a->lba + off + (uint32_t)(int32_t)shifts[k], sec, &form2)) continue;
            const int r = looks_like(id, sec);
            if (r < 0) break;        /* no header to check: the common rule */
            if (r) { shift = shifts[k]; break; }
        }
        a->start[id] = off + (uint32_t)shift;
        a->len[id] = size;
        order[n++] = id;
    }
    /* insertion sort by start (almost sorted already) */
    for (uint32_t i = 1; i < n; ++i) {
        const uint32_t x = order[i];
        uint32_t j = i;
        while (j > 0 && cmp_ctx_lt(a, x, order[j - 1])) { order[j] = order[j - 1]; --j; }
        order[j] = x;
    }
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t id = order[i];
        const uint32_t end = i + 1 < n ? a->start[order[i + 1]] : a->sectors;
        if (end > a->start[id]) a->len[id] = end - a->start[id];
        if (a->start[id] + a->len[id] > a->sectors) a->len[id] = a->sectors - a->start[id];
    }
    return 1;
}

static const char *group_of(uint32_t id) {
    static const struct { uint32_t lo, hi; const char *g; } k[] = {
        { 1, 299, "A" }, { 300, 899, "B" }, { 900, 1699, "C" }, { 1700, 2499, "D" },
        { 2500, 3399, "E_sound" }, { 3400, 3999, "F" }, { 4000, 99999, "G" },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) if (id >= k[i].lo && id <= k[i].hi) return k[i].g;
    return "other";
}

static uint8_t *load_path(const char *path, size_t *bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *d = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (d = malloc((size_t)n)) != NULL) {
            if (fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
            else *bytes = (size_t)n;
        }
    }
    fclose(f);
    return d;
}

/* File `id`'s bytes (malloc'd), NULL if there is none. */
static uint8_t *src_read(ac2_src *a, uint32_t id, size_t *bytes) {
    *bytes = 0;
    if (id >= AC2_MAX_IDS) return NULL;
    if (a->dir) {
        char path[AC2_PATH];
        snprintf(path, sizeof path, "%s/%s/%05u.bin", a->dir, group_of(id), (unsigned)id);
        uint8_t *d = load_path(path, bytes);
        if (d) return d;
        snprintf(path, sizeof path, "%s/%05u.bin", a->dir, (unsigned)id);
        return load_path(path, bytes);
    }
    uint32_t n = a->len[id];
    if (!n) return NULL;
    if (n > 4096) n = 4096;           /* 8 MB: more than any part or package */
    uint8_t *d = malloc((size_t)n * 2048u);
    if (!d) return NULL;
    for (uint32_t s = 0; s < n; ++s) {
        int form2 = 0;
        if (!acr_disc_read_user(a->disc, a->lba + a->start[id] + s, d + (size_t)s * 2048u, &form2)) { free(d); return NULL; }
    }
    *bytes = (size_t)n * 2048u;
    return d;
}

/* ---- the parts ---- */

/* B block ids and their shop (F block) package, k-th to k-th
 * (docs/research/gen2-archive.md). f = 0: no package found (boosters: their
 * TEX0 is in none of 3500-3506, so they come out untextured). Arms: the F
 * block has 23 (14 with three textures, 9 with two), the B block 22 (13 and
 * 9): which of the first 14 has no model is not known, so arms 352-364 may
 * get a neighbour's textures. */
typedef struct ac2_range { const char *kind; uint16_t first, last, f; } ac2_range;
static const ac2_range k_ranges[] = {
    { "core", 301, 306, 3420 }, { "booster", 310, 325, 0 }, { "head", 332, 342, 3400 }, { "head", 351, 351, 3411 },
    { "arms", 352, 373, 3430 }, { "legs", 381, 411, 3460 }, { "weapon-r", 501, 525, 3700 },
    { "weapon-l", 561, 569, 3730 }, { "back", 601, 632, 3620 }, { "extension", 761, 768, 3600 },
};

static uint32_t f_of(uint32_t id) {
    for (size_t r = 0; r < sizeof k_ranges / sizeof k_ranges[0]; ++r)
        if (id >= k_ranges[r].first && id <= k_ranges[r].last) return k_ranges[r].f ? k_ranges[r].f + (id - k_ranges[r].first) : 0;
    return 0;
}

/* The test AC (2026-11-27): a complete standard biped, drawn and checked. */
enum { T_CORE = 301, T_HEAD = 332, T_ARMS = 352, T_LEGS = 390, T_BOOSTER = 310, T_WEAPON = 501 };

/* A part read in: model bytes and the textures of its package. */
typedef struct ac2_part {
    uint32_t id, f;
    const char *kind;
    uint8_t *data;
    size_t bytes;
    acr_ac2_tex tex[AC2_MAX_TEX];
    int ntex;
} ac2_part;

static int part_load(ac2_src *a, uint32_t id, const char *kind, ac2_part *p) {
    memset(p, 0, sizeof *p);
    p->id = id; p->kind = kind; p->f = f_of(id);
    p->data = src_read(a, id, &p->bytes);
    if (!p->data || !is_header(p->data, p->bytes)) { free(p->data); p->data = NULL; return 0; }
    if (p->f) {
        size_t n = 0;
        uint8_t *pkg = src_read(a, p->f, &n);
        if (pkg) p->ntex = acr_ac2_package_textures(pkg, n, p->tex, AC2_MAX_TEX);
        free(pkg);
    }
    return 1;
}

static void part_free(ac2_part *p) {
    free(p->data);
    for (int i = 0; i < p->ntex; ++i) acr_ac2_tex_free(&p->tex[i]);
    memset(p, 0, sizeof *p);
}

static int tex_index(const ac2_part *p, uint64_t tex0) {
    if (!tex0) return -1;
    for (int i = 0; i < p->ntex; ++i) if (p->tex[i].tex0 == tex0) return i;
    return -1;
}

static int write_tex_png(const char *path, const acr_ac2_tex *t) {
    return acr_write_png8(path, t->w, t->h, t->pixels, t->rgb, t->alpha);
}

/* ---- OBJ ---- */

typedef struct obj_out { FILE *f; uint32_t verts; } obj_out;

/* Triangles of one material (markers left out). */
static void obj_tris(obj_out *o, const acr_ac2_tri *t, uint32_t n, const char *name, const char *mtl, float lift,
                     const ac2_part *p, int want_tex) {
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (t[i].marker) continue;
        if (p && tex_index(p, t[i].tex0) != want_tex) continue;
        if (!k++) fprintf(o->f, "o %s\nusemtl %s\n", name, mtl);
        for (int c = 0; c < 3; ++c)
            fprintf(o->f, "v %.3f %.3f %.3f\nvn %.4f %.4f %.4f\nvt %.4f %.4f\n", t[i].p[c][0], t[i].p[c][1] + lift,
                    t[i].p[c][2], t[i].n[c][0], t[i].n[c][1], t[i].n[c][2], t[i].uv[c][0], 1.0f - t[i].uv[c][1]);
        const uint32_t b = o->verts + 1;
        fprintf(o->f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", b, b, b, b + 1, b + 1, b + 1, b + 2, b + 2, b + 2);
        o->verts += 3;
    }
}

/* ---- glTF (binary) ---- */

typedef struct dbuf { char *p; size_t n, cap; } dbuf;
static int db_put(dbuf *b, const void *d, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 4096;
        while (b->n + n + 1 > c) c *= 2;
        char *q = realloc(b->p, c);
        if (!q) return 0;
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
    b->p[b->n] = 0;
    return 1;
}
static int db_printf(dbuf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    return n >= 0 && (size_t)n < sizeof tmp && db_put(b, tmp, (size_t)n);
}
static int db_align4(dbuf *b, char fill) { while (b->n % 4) if (!db_put(b, &fill, 1)) return 0; return 1; }

#define GLB_MAX_NODES 512
#define GLB_MAX_KIDS 48
typedef struct gnode {
    char name[64];
    float t[3], q[4];
    int mesh, nkids, f_id;
    int kids[GLB_MAX_KIDS];
} gnode;

typedef struct glb {
    dbuf bin, acc, views, meshes, mats, texs, imgs, anims;
    int nanim;
    int nacc, nviews, nmesh, nmat, nimg;
    gnode nodes[GLB_MAX_NODES];
    int nnodes, roots[16], nroots;
    struct { uint32_t f; uint64_t tex0; int mat; } keys[96];
    int nkeys, plain_mat;
    int ok;
} glb;

static int g_view(glb *g, const void *d, size_t n, int target) {
    if (!db_align4(&g->bin, 0)) g->ok = 0;
    db_printf(&g->views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu", g->nviews ? "," : "", g->bin.n, n);
    if (target) db_printf(&g->views, ",\"target\":%d", target);
    db_put(&g->views, "}", 1);
    if (!db_put(&g->bin, d, n)) g->ok = 0;
    return g->nviews++;
}

static int g_acc(glb *g, const float *d, int count, int comps) {
    const int v = g_view(g, d, sizeof(float) * (size_t)count * (size_t)comps, 34962);
    db_printf(&g->acc, "%s{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC%d\"",
              g->nacc ? "," : "", v, count, comps);
    if (comps == 3) {
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (int i = 0; i < count; ++i)
            for (int c = 0; c < 3; ++c) { const float x = d[3 * i + c]; if (x < lo[c]) lo[c] = x; if (x > hi[c]) hi[c] = x; }
        db_printf(&g->acc, ",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
    }
    db_put(&g->acc, "}", 1);
    return g->nacc++;
}

/* Animation data: no bufferView target; SCALAR times get min/max (required by glTF). */
static int g_acc_anim(glb *g, const float *d, int count, int comps) {
    const int v = g_view(g, d, sizeof(float) * (size_t)count * (size_t)comps, 0);
    static const char *const ty[] = { "", "SCALAR", "VEC2", "VEC3", "VEC4" };
    db_printf(&g->acc, "%s{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"%s\"",
              g->nacc ? "," : "", v, count, ty[comps]);
    if (comps == 1) {
        float lo = 1e30f, hi = -1e30f;
        for (int i = 0; i < count; ++i) { if (d[i] < lo) lo = d[i]; if (d[i] > hi) hi = d[i]; }
        db_printf(&g->acc, ",\"min\":[%.9g],\"max\":[%.9g]", lo, hi);
    }
    db_put(&g->acc, "}", 1);
    return g->nacc++;
}

static int g_node(glb *g, int parent, const char *name) {
    if (g->nnodes >= GLB_MAX_NODES) { g->ok = 0; return 0; }
    gnode *n = &g->nodes[g->nnodes];
    memset(n, 0, sizeof *n);
    snprintf(n->name, sizeof n->name, "%s", name);
    n->q[3] = 1;
    n->mesh = -1;
    n->f_id = -1;
    if (parent < 0) { if (g->nroots < 16) g->roots[g->nroots++] = g->nnodes; }
    else if (g->nodes[parent].nkids < GLB_MAX_KIDS) g->nodes[parent].kids[g->nodes[parent].nkids++] = g->nnodes;
    else g->ok = 0;
    return g->nnodes++;
}

/* The material for a part's texture (the PNG read back from where it was written), or the plain one. */
static int g_material(glb *g, uint32_t f, uint64_t tex0, const char *png) {
    if (!png) {
        if (g->plain_mat >= 0) return g->plain_mat;
    } else {
        for (int i = 0; i < g->nkeys; ++i) if (g->keys[i].f == f && g->keys[i].tex0 == tex0) return g->keys[i].mat;
    }
    int img = -1;
    if (png) {
        size_t n = 0;
        uint8_t *d = load_path(png, &n);
        if (d) {
            const int v = g_view(g, d, n, 0);
            free(d);
            db_printf(&g->imgs, "%s{\"bufferView\":%d,\"mimeType\":\"image/png\"}", g->nimg ? "," : "", v);
            db_printf(&g->texs, "%s{\"sampler\":0,\"source\":%d}", g->nimg ? "," : "", g->nimg);
            img = g->nimg++;
        }
    }
    db_printf(&g->mats, "%s{\"name\":\"%s%016llx\",\"doubleSided\":true,\"pbrMetallicRoughness\":"
                        "{\"baseColorFactor\":[0.6,0.63,0.67,1],\"metallicFactor\":0.3,\"roughnessFactor\":0.6",
              g->nmat ? "," : "", img >= 0 ? "tex_" : "untextured_", (unsigned long long)tex0);
    if (img >= 0) db_printf(&g->mats, ",\"baseColorTexture\":{\"index\":%d}", img);
    db_put(&g->mats, "}}", 2);
    const int m = g->nmat++;
    if (!png || img < 0) g->plain_mat = m;
    else if (g->nkeys < 96) { g->keys[g->nkeys].f = f; g->keys[g->nkeys].tex0 = tex0; g->keys[g->nkeys].mat = m; ++g->nkeys; }
    return m;
}

static void quat_of(const float R[9], float q[4]) {
    const float tr = R[0] + R[4] + R[8];
    const float w = sqrtf(fmaxf(0.0f, 1.0f + tr)) / 2;
    if (w < 1e-6f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    q[0] = (R[7] - R[5]) / (4 * w); q[1] = (R[2] - R[6]) / (4 * w); q[2] = (R[3] - R[1]) / (4 * w); q[3] = w;
}

/* Parents (none are stored): arms and legs chain each mesh to the previous
 * one on the same side (x sign), centre pieces hang off mesh 0. */
static void parents_of(const char *kind, const mesh_info *mi, int n, int *par) {
    const int limb = !strcmp(kind, "arms") || !strcmp(kind, "legs");
    for (int i = 0; i < n; ++i) {
        par[i] = -1;
        if (!i) continue;
        if (!limb) { par[i] = 0; continue; }
        const float x = roundf(mi[i].pos[0] * 1000.0f);
        const int side = (x > 0) - (x < 0);
        int prev = -1;
        if (side)
            for (int j = 0; j < i; ++j) {
                const float xj = roundf(mi[j].pos[0] * 1000.0f);
                if (((xj > 0) - (xj < 0)) == side) prev = j;
            }
        par[i] = prev >= 0 ? prev : (!strcmp(kind, "arms") ? -1 : 0);
    }
}

/* Adds a part under `parent` (-1: the scene; `root_parent` -2 = under node 0's
 * holder given by caller). png_dir: where its textures were written as
 * <prefix>-K.png. Returns the part's node; hand (may be NULL) gets the right
 * hand's node (arms only: the last mesh on the -x side, Gen 1's right). */
typedef struct part_nodes { int n, node[AC2_MAX_MESHES]; float pos[AC2_MAX_MESHES][3]; } part_nodes;

static int g_part(glb *g, const ac2_part *p, int parent, const char *png_dir, const char *png_prefix, int *hand,
                  part_nodes *out) {
    static acr_ac2_tri tri[AC2_MAX_TRIS];
    mesh_info mi[AC2_MAX_MESHES];
    int meshes = 0;
    uint32_t n = decode(p->data, p->bytes, 1, 1.0f, NULL, tri, AC2_MAX_TRIS, mi, AC2_MAX_MESHES, &meshes);
    if (n > AC2_MAX_TRIS) n = AC2_MAX_TRIS;
    if (meshes > AC2_MAX_MESHES) meshes = AC2_MAX_MESHES;
    char name[64];
    snprintf(name, sizeof name, "%s_%04u", p->kind, (unsigned)p->id);
    const int root = g_node(g, parent, name);
    g->nodes[root].f_id = p->f ? (int)p->f : -1;
    int par[AC2_MAX_MESHES], node[AC2_MAX_MESHES];
    parents_of(p->kind, mi, meshes, par);
    float *buf = malloc(sizeof(float) * 8 * 3 * (n ? n : 1));
    if (!buf) { g->ok = 0; return root; }
    for (int m = 0; m < meshes; ++m) {
        snprintf(name, sizeof name, "%s_%04u_mesh%d", p->kind, (unsigned)p->id, m);
        node[m] = g_node(g, par[m] >= 0 ? node[par[m]] : root, name);
        gnode *nd = &g->nodes[node[m]];
        const float *pp = par[m] >= 0 ? mi[par[m]].pos : NULL;
        const float d[3] = { mi[m].pos[0] - (pp ? pp[0] : 0), mi[m].pos[1] - (pp ? pp[1] : 0), mi[m].pos[2] - (pp ? pp[2] : 0) };
        nd->t[0] = d[0]; nd->t[1] = -d[1]; nd->t[2] = -d[2];
        float R[9], M[9];
        euler(mi[m].rot, R);
        static const float S[3] = { 1, -1, -1 };     /* S R S */
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) M[3 * r + c] = S[r] * R[3 * r + c] * S[c];
        quat_of(M, nd->q);
        /* a primitive per texture, markers as empty nodes */
        uint64_t seen[AC2_MAX_TEX + 8];
        int nseen = 0, nmark = 0;
        dbuf prims = { 0 };
        for (uint32_t t = 0; t < n; ++t) {
            if (tri[t].mesh != m) continue;
            if (tri[t].marker) {
                snprintf(name, sizeof name, "%s_%04u_mesh%d_marker%d", p->kind, (unsigned)p->id, m, nmark++);
                const int k = g_node(g, node[m], name);
                for (int c = 0; c < 3; ++c) g->nodes[k].t[c] = (tri[t].p[0][c] + tri[t].p[1][c] + tri[t].p[2][c]) / 3;
                continue;
            }
            int s = 0;
            while (s < nseen && seen[s] != tri[t].tex0) ++s;
            if (s < nseen || nseen >= (int)(sizeof seen / sizeof seen[0])) continue;
            seen[nseen++] = tri[t].tex0;
            int cnt = 0;
            for (uint32_t u = t; u < n; ++u) {
                if (tri[u].mesh != m || tri[u].marker || tri[u].tex0 != tri[t].tex0) continue;
                for (int c = 0; c < 3; ++c) {
                    float *v = buf + 8 * (3 * cnt + c);
                    memcpy(v, tri[u].p[c], 12);
                    memcpy(v + 3, tri[u].n[c], 12);
                    const float l = sqrtf(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
                    if (l > 0) for (int q = 3; q < 6; ++q) v[q] /= l;
                    memcpy(v + 6, tri[u].uv[c], 8);
                }
                ++cnt;
            }
            /* split into attribute arrays */
            float *P = malloc(sizeof(float) * 9 * (size_t)cnt), *N = malloc(sizeof(float) * 9 * (size_t)cnt),
                  *U = malloc(sizeof(float) * 6 * (size_t)cnt);
            if (!P || !N || !U) { free(P); free(N); free(U); g->ok = 0; continue; }
            for (int i = 0; i < 3 * cnt; ++i) {
                memcpy(P + 3 * i, buf + 8 * i, 12); memcpy(N + 3 * i, buf + 8 * i + 3, 12); memcpy(U + 2 * i, buf + 8 * i + 6, 8);
            }
            const int ap = g_acc(g, P, 3 * cnt, 3), an = g_acc(g, N, 3 * cnt, 3), au = g_acc(g, U, 3 * cnt, 2);
            free(P); free(N); free(U);
            const int ti = tex_index(p, tri[t].tex0);
            char png[AC2_PATH];
            if (ti >= 0) snprintf(png, sizeof png, "%s/%s-%d.png", png_dir, png_prefix, ti);
            const int mat = g_material(g, p->f, tri[t].tex0, ti >= 0 ? png : NULL);
            db_printf(&prims, "%s{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"TEXCOORD_0\":%d},\"material\":%d}",
                      prims.n ? "," : "", ap, an, au, mat);
        }
        if (prims.n) {
            db_printf(&g->meshes, "%s{\"name\":\"%s_%04u_m%d\",\"primitives\":[", g->nmesh ? "," : "", p->kind,
                      (unsigned)p->id, m);
            db_put(&g->meshes, prims.p, prims.n);
            db_put(&g->meshes, "]}", 2);
            g->nodes[node[m]].mesh = g->nmesh++;
        }
        free(prims.p);
    }
    free(buf);
    if (out) {
        out->n = meshes;
        for (int m = 0; m < meshes; ++m) { out->node[m] = node[m]; memcpy(out->pos[m], mi[m].pos, 12); }
    }
    if (hand) {
        *hand = -1;
        for (int m = 0; m < meshes; ++m) if (mi[m].pos[0] < 0) *hand = node[m];
    }
    return root;
}

static int g_save(glb *g, const char *path) {
    dbuf js = { 0 };
    db_printf(&js, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"acr-import (Gen 2, acr_models_ac2.c)\"},"
                   "\"scene\":0,\"scenes\":[{\"nodes\":[");
    for (int i = 0; i < g->nroots; ++i) db_printf(&js, "%s%d", i ? "," : "", g->roots[i]);
    db_printf(&js, "]}],\"nodes\":[");
    for (int i = 0; i < g->nnodes; ++i) {
        const gnode *n = &g->nodes[i];
        db_printf(&js, "%s{\"name\":\"%s\",\"translation\":[%.6g,%.6g,%.6g],\"rotation\":[%.6g,%.6g,%.6g,%.6g]",
                  i ? "," : "", n->name, n->t[0], n->t[1], n->t[2], n->q[0], n->q[1], n->q[2], n->q[3]);
        if (n->mesh >= 0) db_printf(&js, ",\"mesh\":%d", n->mesh);
        if (n->f_id >= 0) db_printf(&js, ",\"extras\":{\"f_id\":%d}", n->f_id);
        if (n->nkids) {
            db_printf(&js, ",\"children\":[");
            for (int k = 0; k < n->nkids; ++k) db_printf(&js, "%s%d", k ? "," : "", n->kids[k]);
            db_put(&js, "]", 1);
        }
        db_put(&js, "}", 1);
    }
    db_align4(&g->bin, 0);
    db_printf(&js, "],\"meshes\":[");       db_put(&js, g->meshes.p ? g->meshes.p : "", g->meshes.n);
    db_printf(&js, "],\"materials\":[");    db_put(&js, g->mats.p ? g->mats.p : "", g->mats.n);
    db_printf(&js, "],\"textures\":[");     db_put(&js, g->texs.p ? g->texs.p : "", g->texs.n);
    db_printf(&js, "],\"images\":[");       db_put(&js, g->imgs.p ? g->imgs.p : "", g->imgs.n);
    db_printf(&js, "],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}]");
    if (g->nanim) { db_printf(&js, ",\"animations\":["); db_put(&js, g->anims.p, g->anims.n); db_put(&js, "]", 1); }
    db_printf(&js, ",\"accessors\":[");     db_put(&js, g->acc.p ? g->acc.p : "", g->acc.n);
    db_printf(&js, "],\"bufferViews\":[");  db_put(&js, g->views.p ? g->views.p : "", g->views.n);
    db_printf(&js, "],\"buffers\":[{\"byteLength\":%zu}]}", g->bin.n);
    db_align4(&js, ' ');
    int ok = g->ok && js.p;
    FILE *f = ok ? fopen(path, "wb") : NULL;
    if (f) {
        uint8_t h[20];
        const uint32_t total = 12 + 8 + (uint32_t)js.n + 8 + (uint32_t)g->bin.n;
        const uint32_t w[5] = { 0x46546C67u, 2, total, (uint32_t)js.n, 0x4E4F534Au };
        for (int i = 0; i < 5; ++i) for (int b = 0; b < 4; ++b) h[4 * i + b] = (uint8_t)(w[i] >> (8 * b));
        uint8_t h2[8];
        const uint32_t w2[2] = { (uint32_t)g->bin.n, 0x004E4942u };
        for (int i = 0; i < 2; ++i) for (int b = 0; b < 4; ++b) h2[4 * i + b] = (uint8_t)(w2[i] >> (8 * b));
        ok = fwrite(h, 1, 20, f) == 20 && fwrite(js.p, 1, js.n, f) == js.n && fwrite(h2, 1, 8, f) == 8 &&
             (g->bin.n == 0 || fwrite(g->bin.p, 1, g->bin.n, f) == g->bin.n);
        ok = fclose(f) == 0 && ok;
    } else {
        ok = 0;
    }
    free(js.p);
    return ok;
}

static void g_free(glb *g) {
    free(g->anims.p);
    free(g->bin.p); free(g->acc.p); free(g->views.p); free(g->meshes.p); free(g->mats.p); free(g->texs.p); free(g->imgs.p);
}

static glb *g_new(void) {
    glb *g = calloc(1, sizeof *g);
    if (g) { g->ok = 1; g->plain_mat = -1; }
    return g;
}

/* ---- export ---- */

static const char *flat_mtl(const char *kind) {
    if (!strcmp(kind, "booster")) return "joint";
    if (!strncmp(kind, "weapon", 6) || !strcmp(kind, "back")) return "weapon";
    return "frame";
}

static const char k_flat_mtls[] = "newmtl frame\nKd 0.62 0.64 0.60\nnewmtl joint\nKd 0.40 0.41 0.42\n"
                                  "newmtl weapon\nKd 0.33 0.34 0.36\n";

/* One part on its own: OBJ + MTL + PNGs + GLB in parts/<kind>/. */
static int export_part(const ac2_part *p, const char *out_dir, acr_ac2_tri *tri) {
    char dir[AC2_PATH], path[AC2_PATH], pre[16];
    snprintf(dir, sizeof dir, "%s/gen2/ac2/parts/%s", out_dir, p->kind);
    if (!make_dirs(dir)) return 0;
    snprintf(pre, sizeof pre, "%04u", (unsigned)p->id);
    uint32_t n = acr_ac2_model_tris(p->data, p->bytes, NULL, tri, AC2_MAX_TRIS, NULL, 0, NULL);
    if (!n) return 1;
    if (n > AC2_MAX_TRIS) n = AC2_MAX_TRIS;
    for (int i = 0; i < p->ntex; ++i) {
        snprintf(path, sizeof path, "%s/%s-%d.png", dir, pre, i);
        if (!write_tex_png(path, &p->tex[i])) return 0;
    }
    snprintf(path, sizeof path, "%s/%s.mtl", dir, pre);
    FILE *m = fopen(path, "w");
    if (!m) return 0;
    fputs(k_flat_mtls, m);
    for (int i = 0; i < p->ntex; ++i) fprintf(m, "newmtl tex%d\nKd 0.62 0.64 0.60\nmap_Kd %s-%d.png\n", i, pre, i);
    if (fclose(m) != 0) return 0;
    snprintf(path, sizeof path, "%s/%s.obj", dir, pre);
    obj_out o = { fopen(path, "w"), 0 };
    if (!o.f) return 0;
    fprintf(o.f, "# Armored Core 2 file %u (%s), shop package %u, Gen 1 model space x%.0f\nmtllib %s.mtl\n",
            (unsigned)p->id, p->kind, (unsigned)p->f, (double)ACR_AC2_SCALE, pre);
    for (int i = -1; i < p->ntex; ++i) {
        char mt[16];
        snprintf(mt, sizeof mt, "tex%d", i);
        obj_tris(&o, tri, n, p->kind, i < 0 ? flat_mtl(p->kind) : mt, 0.0f, p, i);
    }
    if (fclose(o.f) != 0) return 0;
    glb *g = g_new();
    if (!g) return 0;
    g_part(g, p, -1, dir, pre, NULL, NULL);
    snprintf(path, sizeof path, "%s/%s.glb", dir, pre);
    const int ok = g_save(g, path);
    g_free(g);
    free(g);
    return ok;
}

/* ---- motion (Gen 2; docs/research/gen2-archive.md "Motion", tools/gen2/skeleton.py) ----
 * Set file = 421 + 24 x core group + 8 x arms group + legs group (groups by shop F id,
 * tools/gen2/motion_sets.py). The bank starts 0x8000 into the set file and runs into the
 * next files: u32 count, 12 bytes, then 16-byte rows (u32 motion, u32 clip offset); row r
 * holds motion r - 1. Clip: 16-byte header (u16 nodes at +8, u16 frames at +0xc), 0x30 per
 * node (u32 key offset at +4), keys 0x80 bytes: u16 frame, u16 next frame (0xffff last),
 * position at +0x10, rotation (radians xyz) at +0x40. */
static const int8_t k_core_grp[6] = { 1, 1, 2, 2, 0, 0 };                                   /* F 3420- */
static const int8_t k_arms_grp[23] = { 1, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 }; /* 3430- */
static const int8_t k_legs_grp[31] = { 1, 1, 1, 1, 1, 2, 2, 2, 0, 0, 0, 4, 4, 4, 3, 3, 5, 5, 5, 5, 5,
                                       6, 6, 6, 6, 6, 7, 7, 7, 7, 7 };                     /* 3460- */

static int motion_set(uint32_t core_f, uint32_t arms_f, uint32_t legs_f, int *legs_grp) {
    if (core_f < 3420 || core_f > 3425 || arms_f < 3430 || arms_f > 3452 || legs_f < 3460 || legs_f > 3490) return 0;
    *legs_grp = k_legs_grp[legs_f - 3460];
    return 421 + 24 * k_core_grp[core_f - 3420] + 8 * k_arms_grp[arms_f - 3430] + *legs_grp;
}

static const char *motion_name(int m, char *buf, size_t n) {
    static const char *const fixed[76] = {
        [0] = "rest", [33] = "turn_a", [34] = "turn_b", [35] = "walk_stop", [36] = "jump_takeoff", [37] = "jump",
        [38] = "land", [47] = "idle_loop", [48] = "destroyed", [49] = "ob_ground_start", [50] = "ob_air_start",
        [51] = "land_damaged", [52] = "hit_ground_q0", [53] = "hit_ground_q2", [54] = "hit_ground_q3",
        [55] = "hit_ground_q1", [56] = "kneel_backL", [57] = "kneel_backL_rise", [58] = "kneel_backR",
        [59] = "kneel_backR_rise", [63] = "blade_a", [64] = "blade_b", [65] = "blade_c", [66] = "ob_ground_loop",
        [67] = "ob_air_loop", [75] = "kneel_centre",
    };
    static const int8_t boost[8] = { 17, 19, 20, 21, 22, 18, 23, 24 }, jump[8] = { 39, 40, 41, 42, 43, 44, 45, 46 };
    if (m >= 1 && m <= 16) snprintf(buf, n, "%02d_walk_%s_dir%d", m, (m & 1) ? "start" : "loop", (m - 1) / 2);
    else if (m >= 25 && m <= 32) snprintf(buf, n, "%02d_boost_stop_%d", m, m - 25);
    else {
        for (int d = 0; d < 8; ++d) {
            if (boost[d] == m) { snprintf(buf, n, "%02d_boost_start_dir%d", m, d); return buf; }
            if (jump[d] == m) { snprintf(buf, n, "%02d_jump_dir%d", m, d); return buf; }
        }
        snprintf(buf, n, "%02d_%s", m, m < 76 && fixed[m] ? fixed[m] : "unnamed");
    }
    return buf;
}

typedef struct ac2_key { float t, pos[3], rot[3]; } ac2_key;

/* Keys of node `k` of the clip at `o` (count; 0 if the node has none). */
static int clip_keys(const uint8_t *b, size_t n, size_t o, int k, ac2_key *out, int max) {
    if (o + 0x10 + (size_t)(k + 1) * 0x30 > n) return 0;
    const uint32_t off = le32(b + o + 0x10 + (size_t)k * 0x30 + 4);
    if (!off) return 0;
    int c = 0;
    for (size_t q = o + off; q + 0x80 <= n && c < max; q += 0x80) {
        const unsigned fr = b[q] | b[q + 1] << 8, nx = b[q + 2] | b[q + 3] << 8;
        out[c].t = (float)fr / 60.0f;
        for (int i = 0; i < 3; ++i) { out[c].pos[i] = lef(b + q + 0x10 + 4 * i); out[c].rot[i] = lef(b + q + 0x40 + 4 * i); }
        if (c && out[c].t <= out[c - 1].t) break;
        ++c;
        if (nx == 0xffff || nx <= fr) break;
    }
    return c;
}

/* Biped (legs groups 0-2, 33 nodes): motion node -> test-AC glTF node. Part slots:
 * 0 legs, 1 core; mesh index. Node 1 (centre) moves the legs' and core's first meshes,
 * node 22 (pelvis) turns the legs' first mesh, node 3 (waist) turns the core. */
typedef struct anim_target { int8_t motion_node, slot, mesh, what; } anim_target;   /* what: 0 rot, 1 centre move */
static const anim_target k_biped[] = {
    { 1, 0, 0, 1 }, { 1, 1, 0, 1 }, { 22, 0, 0, 0 }, { 3, 1, 0, 0 },
    { 13, 2, 0, 0 }, { 14, 2, 1, 0 }, { 15, 2, 2, 0 }, { 16, 2, 3, 0 },
    { 17, 2, 4, 0 }, { 18, 2, 5, 0 }, { 19, 2, 6, 0 }, { 20, 2, 7, 0 },
    { 23, 0, 1, 0 }, { 24, 0, 2, 0 }, { 25, 0, 3, 0 }, { 26, 0, 4, 0 },
    { 27, 0, 5, 0 }, { 28, 0, 6, 0 }, { 29, 0, 7, 0 }, { 30, 0, 8, 0 }, { 31, 0, 9, 0 },
};

/* Adds every clip of the set as a glTF animation. pn: part nodes for slots 0 legs, 1 core, 2 arms.
 * Returns the number of animations written. */
static int g_motions(glb *g, ac2_src *a, int set, const part_nodes *pn) {
    size_t total = 0, sz[4];
    uint8_t *f[4];
    for (int i = 0; i < 4; ++i) { f[i] = src_read(a, (uint32_t)(set + i), &sz[i]); total += f[i] ? sz[i] : 0; }
    uint8_t *b = malloc(total ? total : 1);
    size_t at = 0;
    for (int i = 0; i < 4; ++i) { if (f[i] && b) memcpy(b + at, f[i], sz[i]); at += f[i] ? sz[i] : 0; free(f[i]); }
    if (!b || total < 0x8010) { free(b); return 0; }
    const size_t st = 0x8000;
    const uint32_t rows = le32(b + st);
    if (rows < 2 || rows > 200) { free(b); return 0; }
    enum { MAXK = 1024 };
    ac2_key *k = malloc(sizeof *k * MAXK);
    float *tt = malloc(sizeof(float) * MAXK), *vv = malloc(sizeof(float) * 4 * MAXK);
    float rest_c[3] = { 0, 0, 0 };
    int made = 0;
    for (uint32_t r = 1; k && tt && vv && r < rows && st + 0x10 + (r + 1) * 16 <= total; ++r) {
        const uint32_t rel = le32(b + st + 0x10 + r * 16 + 4);
        const int m = (int)r - 1;
        const size_t o = st + rel;
        if (!rel || o + 0x10 > total) continue;
        if ((b[o + 8] | b[o + 9] << 8) != 33) continue;          /* biped sets only (for now) */
        if (m == 0) { const int c = clip_keys(b, total, o, 1, k, MAXK); if (c) memcpy(rest_c, k[0].pos, 12); }
        dbuf ch = { 0 }, sm = { 0 };
        int nch = 0;
        for (size_t i = 0; i < sizeof k_biped / sizeof k_biped[0]; ++i) {
            const anim_target *at2 = &k_biped[i];
            const part_nodes *P = &pn[at2->slot];
            if (at2->mesh >= P->n) continue;
            const int c = clip_keys(b, total, o, at2->motion_node, k, MAXK);
            if (!c) continue;
            for (int j = 0; j < c; ++j) {
                tt[j] = k[j].t;
                if (at2->what == 1) {         /* rest translation + the centre's offset from its rest */
                    const float *p0 = P->pos[at2->mesh];
                    float d[3];
                    for (int q = 0; q < 3; ++q) d[q] = p0[q] + k[j].pos[q] - rest_c[q];
                    vv[3 * j] = d[0]; vv[3 * j + 1] = -d[1]; vv[3 * j + 2] = -d[2];
                } else {
                    float R[9], M[9];
                    euler(k[j].rot, R);
                    static const float S[3] = { 1, -1, -1 };
                    for (int rr = 0; rr < 3; ++rr) for (int cc = 0; cc < 3; ++cc) M[3 * rr + cc] = S[rr] * R[3 * rr + cc] * S[cc];
                    quat_of(M, vv + 4 * j);
                    if (j) {                  /* keep neighbouring quaternions on the same side */
                        const float *q0 = vv + 4 * (j - 1); float *q1 = vv + 4 * j;
                        if (q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3] < 0)
                            for (int q = 0; q < 4; ++q) q1[q] = -q1[q];
                    }
                }
            }
            const int ai = g_acc_anim(g, tt, c, 1), ao = g_acc_anim(g, vv, c, at2->what == 1 ? 3 : 4);
            db_printf(&sm, "%s{\"input\":%d,\"output\":%d,\"interpolation\":\"LINEAR\"}", nch ? "," : "", ai, ao);
            db_printf(&ch, "%s{\"sampler\":%d,\"target\":{\"node\":%d,\"path\":\"%s\"}}", nch ? "," : "", nch,
                      P->node[at2->mesh], at2->what == 1 ? "translation" : "rotation");
            ++nch;
        }
        if (nch) {
            char nm[48];
            db_printf(&g->anims, "%s{\"name\":\"%s\",\"channels\":[", g->nanim ? "," : "", motion_name(m, nm, sizeof nm));
            db_put(&g->anims, ch.p, ch.n);
            db_printf(&g->anims, "],\"samplers\":[");
            db_put(&g->anims, sm.p, sm.n);
            db_put(&g->anims, "]}", 2);
            ++g->nanim; ++made;
        }
        free(ch.p); free(sm.p);
    }
    free(k); free(tt); free(vv); free(b);
    return made;
}

static int export_all(ac2_src *a, const char *out_dir, uint32_t *parts, char *err, size_t err_size) {
    if (parts) *parts = 0;
    acr_ac2_tri *tri = malloc(sizeof *tri * AC2_MAX_TRIS);
    ac2_part *p = calloc(8, sizeof *p);
    if (!tri || !p) { free(tri); free(p); set_err(err, err_size, "out of memory", NULL); return 0; }
    char path[AC2_PATH];
    int ok = 1;
    for (size_t r = 0; ok && r < sizeof k_ranges / sizeof k_ranges[0]; ++r)
        for (uint32_t id = k_ranges[r].first; ok && id <= k_ranges[r].last; ++id) {
            if (!part_load(a, id, k_ranges[r].kind, &p[0])) continue;
            ok = export_part(&p[0], out_dir, tri);
            if (!ok) snprintf(path, sizeof path, "%s/gen2/ac2/parts/%s/%04u", out_dir, k_ranges[r].kind, (unsigned)id);
            else if (parts) ++*parts;
            part_free(&p[0]);
        }
    if (!ok) { free(tri); free(p); set_err(err, err_size, "cannot write", path); return 0; }

    /* The test AC: every part at its own place (AC2 stores them assembled),
     * the arm weapon on the right hand. */
    static const struct { uint16_t id; const char *kind, *name; } k_test[] = {
        { T_LEGS, "legs", "legs" }, { T_CORE, "core", "core" }, { T_HEAD, "head", "head" },
        { T_ARMS, "arms", "arms" }, { T_BOOSTER, "booster", "booster" }, { T_WEAPON, "weapon-r", "weapon-r" },
    };
    enum { NT = sizeof k_test / sizeof k_test[0] };
    for (int i = 0; i < NT; ++i)
        if (!part_load(a, k_test[i].id, k_test[i].kind, &p[i])) {
            for (int j = 0; j < i; ++j) part_free(&p[j]);
            free(tri); free(p);
            set_err(err, err_size, "Armored Core 2 test AC: a part is missing", k_test[i].name);
            return 0;
        }
    uint32_t start[NT + 1] = { 0 }, used = 0;
    int anims = 0;
    float hand[AC2_MAX_MESHES][3];
    int arm_meshes = 0;
    acr_ac2_model_tris(p[3].data, p[3].bytes, NULL, NULL, 0, hand, AC2_MAX_MESHES, &arm_meshes);
    for (int i = 0; i < NT; ++i) {
        start[i] = used;
        /* Hand weapons sit at the origin: put them on the right hand, the
         * arms' last joint on the -x side (Gen 1's right is -x). */
        float off[3] = { 0, 0, 0 };
        if (k_test[i].id == T_WEAPON && arm_meshes >= 8) {
            const int h = hand[3][0] < 0 ? 3 : 7;
            memcpy(off, hand[h], sizeof off);
        }
        used += acr_ac2_model_tris(p[i].data, p[i].bytes, off, tri + used, AC2_MAX_TRIS - used, NULL, 0, NULL);
        if (used > AC2_MAX_TRIS) used = AC2_MAX_TRIS;
    }
    start[NT] = used;
    float floor_y = 1e9f, top = -1e9f;
    for (uint32_t t = 0; t < start[1]; ++t)          /* the legs' lowest point stands on 0 */
        if (!tri[t].marker) for (int c = 0; c < 3; ++c) if (tri[t].p[c][1] < floor_y) floor_y = tri[t].p[c][1];
    uint32_t drawn = 0;
    for (uint32_t t = 0; t < used; ++t) {
        if (tri[t].marker) continue;
        ++drawn;
        for (int c = 0; c < 3; ++c) if (tri[t].p[c][1] > top) top = tri[t].p[c][1];
    }
    char dir[AC2_PATH];
    snprintf(dir, sizeof dir, "%s/gen2/ac2/test-ac", out_dir);
    snprintf(path, sizeof path, "%s", dir);
    ok = make_dirs(dir);
    /* Textures: <part>-K.png beside ac.obj; a material each (the renderer's
     * MTL holds 16: three flat ones, the rest textures, then flat again). */
    int textured = 0;
    for (int i = 0; ok && i < NT; ++i)
        for (int k = 0; k < p[i].ntex; ++k) {
            snprintf(path, sizeof path, "%s/%s-%d.png", dir, k_test[i].name, k);
            ok = write_tex_png(path, &p[i].tex[k]);
        }
    if (ok) {
        snprintf(path, sizeof path, "%s/ac.mtl", dir);
        FILE *m = fopen(path, "w");
        ok = m != NULL;
        if (ok) {
            fputs(k_flat_mtls, m);
            for (int i = 0; i < NT; ++i)
                for (int k = 0; k < p[i].ntex && textured < 13; ++k, ++textured)
                    fprintf(m, "newmtl %s-%d\nKd 0.62 0.64 0.60\nmap_Kd %s-%d.png\n", k_test[i].name, k, k_test[i].name, k);
            ok = fclose(m) == 0;
        }
    }
    if (ok) {
        snprintf(path, sizeof path, "%s/ac.obj", dir);
        obj_out o = { fopen(path, "w"), 0 };
        ok = o.f != NULL;
        if (ok) {
            fprintf(o.f, "# Armored Core 2 test AC, Gen 1 model space (x%.0f), feet on 0\nmtllib ac.mtl\n",
                    (double)ACR_AC2_SCALE);
            int budget = 13;
            for (int i = 0; i < NT; ++i)
                for (int k = -1; k < p[i].ntex; ++k) {
                    char mt[48];
                    const int tex = k >= 0 && budget > 0;
                    if (k >= 0) --budget;
                    snprintf(mt, sizeof mt, "%s-%d", k_test[i].name, k);
                    obj_tris(&o, tri + start[i], start[i + 1] - start[i], k_test[i].name,
                             tex ? mt : flat_mtl(k_test[i].kind), -floor_y, &p[i], k);
                }
            ok = fclose(o.f) == 0;
        }
    }
    if (ok) {   /* one glTF: the arms' right hand carries the weapon */
        glb *g = g_new();
        ok = g != NULL;
        if (ok) {
            const int ac = g_node(g, -1, "ac2_test_ac");
            g->nodes[ac].t[1] = -floor_y / ACR_AC2_SCALE;
            /* Skeleton for animation (tools/gen2/skeleton.py): legs and core at the AC's root,
             * head, arms and booster hang off the core's first mesh (the waist), so they follow
             * its twist; the weapon is on the right hand. Parts are stored assembled, so a part
             * under a mesh node is shifted back by that mesh's position. */
            static part_nodes pn[NT];
            memset(pn, 0, sizeof pn);
            int hnode = -1;
            g_part(g, &p[0], ac, dir, k_test[0].name, NULL, &pn[0]);          /* legs */
            g_part(g, &p[1], ac, dir, k_test[1].name, NULL, &pn[1]);          /* core */
            const int waist = pn[1].n ? pn[1].node[0] : ac;
            for (int i = 2; i < NT - 1; ++i) {
                const int root = g_part(g, &p[i], waist, dir, k_test[i].name, k_test[i].id == T_ARMS ? &hnode : NULL,
                                        &pn[i]);
                if (pn[1].n) { g->nodes[root].t[0] = -pn[1].pos[0][0]; g->nodes[root].t[1] = pn[1].pos[0][1];
                               g->nodes[root].t[2] = pn[1].pos[0][2]; }
            }
            g_part(g, &p[NT - 1], hnode >= 0 ? hnode : ac, dir, k_test[NT - 1].name, NULL, NULL);
            int lg = -1;
            const int set = motion_set(p[1].f, p[3].f, p[0].f, &lg);
            if (set && lg <= 2) {
                const part_nodes slots[3] = { pn[0], pn[1], pn[3] };
                anims = g_motions(g, a, set, slots);
            }
            snprintf(path, sizeof path, "%s/ac.glb", dir);
            ok = g_save(g, path);
            g_free(g);
            free(g);
        }
    }
    if (ok) {
        snprintf(path, sizeof path, "%s/build.toml", dir);
        FILE *b = fopen(path, "w");
        ok = b != NULL;
        if (ok) {
            fprintf(b, "# Armored Core 2 test AC (AC2DATA.BIN file ids; shop packages in brackets)\n"
                       "core = %d      # [%u]\nhead = %d      # [%u]\narms = %d      # [%u]\nlegs = %d      # [%u]\n"
                       "booster = %d   # (no package texture)\nweapon_r = %d  # [%u]\n"
                       "scale = %.1f      # AC2 units -> Gen 1 model units (same height as a Gen 1 AC)\n"
                       "height = %.0f     # model units, feet to top (Gen 1 world = a quarter)\n"
                       "triangles = %u\ntextures = %d\nanimations = %d   # ac.glb, Gen 2 motion set\n",
                    T_CORE, (unsigned)p[1].f, T_HEAD, (unsigned)p[2].f, T_ARMS, (unsigned)p[3].f, T_LEGS,
                    (unsigned)p[0].f, T_BOOSTER, T_WEAPON, (unsigned)p[5].f, (double)ACR_AC2_SCALE,
                    (double)(top - floor_y), (unsigned)drawn, textured, anims);
            ok = fclose(b) == 0;
        }
    }
    for (int i = 0; i < NT; ++i) part_free(&p[i]);
    free(p);
    free(tri);
    if (!ok && err && !err[0]) set_err(err, err_size, "cannot write", path);
    return ok;
}

int acr_export_ac2_models(acr_disc *disc, const char *out_dir, uint32_t *parts, char *err, size_t err_size) {
    ac2_src *a = calloc(1, sizeof *a);
    if (!a) { set_err(err, err_size, "out of memory", NULL); return 0; }
    int ok = src_open_disc(a, disc, err, err_size) && export_all(a, out_dir, parts, err, err_size);
    free(a);
    return ok;
}

int acr_export_ac2_models_split(const char *split_dir, const char *out_dir, uint32_t *parts, char *err,
                                size_t err_size) {
    ac2_src *a = calloc(1, sizeof *a);
    if (!a) { set_err(err, err_size, "out of memory", NULL); return 0; }
    a->dir = split_dir;
    const int ok = export_all(a, out_dir, parts, err, err_size);
    free(a);
    return ok;
}
