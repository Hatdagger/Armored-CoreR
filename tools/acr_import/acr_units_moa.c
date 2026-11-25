/* Master of Arena mission units (chunk 2 of mission maps 0-19) and their
 * object records (chunk 13) as readable files. Format notes:
 * docs/research/gen1-stages.md, "Mission units". Everything is read from
 * the user's own import folder; nothing from the disc is built into this
 * program.
 *
 * A unit entry (after its u32 size) is an animated model laid out like an
 * AC: an animation block in PA entry 0's layout without that entry's size
 * word (so its offsets count from 4 bytes before the entry: byte 0 =
 * animation count, byte 1 = nodes, u32 block length, 0x1C, joint table,
 * animation list, end), then a u32 (1-3), u32 objects and the AC part
 * mesh table (objects = pieces x LODs, LOD 0 first; pieces = the joint
 * table's slots). Joint table slot K draws piece K.
 *
 * Spaces as for AC parts (acr_models_moa.c): OBJ files are x right, y up,
 * z back, (x, -y, -z) of the game's model space, each piece moved to its
 * node's bind position; the skeleton and animations keep the game's numbers. */
#include "acr_units_moa.h"
#include "acr_models_moa.h"
#include "acr_stages_moa.h"
#include "acr_motion_moa.h"

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

#define MAPS        20u
#define CHUNKS      14u
#define POINT_SIZE  40u
#define MAX_UNITS   128u
#define MAX_USES    64u
#define MAX_SLOTS   64u
#define MAX_NODES   64u
#define MAX_PIECES  64u
#define MAX_ANIMS   64u
#define MAX_KEYS    16u     /* texture page + palette pairs a unit uses */
#define MAX_MATS    16u     /* the engine's material limit (ACR_MTL_MAX) */
#define MAX_SKINS   8u

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t s16(const uint8_t *p) { return (int16_t)u16(p); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | (uint32_t)u16(p + 2) << 16; }

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

static int make_dirs(const char *path) {
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

/* ---- the maps' files ---------------------------------------------------- */

typedef struct chunk { const uint8_t *p; uint32_t n; } chunk;

typedef struct map_file {
    uint8_t *data; size_t size;
    chunk ch[CHUNKS];
    unsigned n_slots;
    const uint8_t *slot[MAX_SLOTS];   /* chunk 2 entries, after the size word; NULL = empty */
    uint32_t slot_n[MAX_SLOTS];
    int unit[MAX_SLOTS];              /* unit number of each slot, -1 none */
    acr_moa_vram *vram;               /* RTIM <map>, loaded when needed */
    int vram_tried;
} map_file;

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

/* Chunk 2: u16 count, then [u32 size][entry], size < 5 = empty. */
static void read_slots(map_file *m) {
    const chunk *c = &m->ch[2];
    m->n_slots = 0;
    if (c->n < 4) return;
    const unsigned count = u16(c->p);
    size_t o = 4;
    for (unsigned k = 0; k < count && k < MAX_SLOTS; ++k) {
        if (o + 4 > c->n) break;
        const uint32_t n = u32(c->p + o);
        if (n < 4 || o + n > c->n) break;
        m->slot[k] = n >= 5 ? c->p + o + 4 : NULL;
        m->slot_n[k] = n >= 5 ? n - 4 : 0;
        m->unit[k] = -1;
        m->n_slots = k + 1;
        o += n;
    }
}

static const acr_moa_vram *map_vram(map_file *m, const char *unpacked, unsigned map) {
    if (!m->vram_tried) {
        m->vram = acr_moa_vram_load(unpacked, map);
        m->vram_tried = 1;
    }
    return m->vram;
}

/* ---- one unit ------------------------------------------------------------ */

typedef struct unit_use { uint8_t map, slot; } unit_use;

typedef struct tex_key { uint16_t tpage, clut; } tex_key;

typedef struct material {
    int tex;            /* texture number, -1 = flat colour */
    unsigned rgb;
    uint8_t semi, two_sided;
} material;

typedef struct unit {
    uint8_t *buf; size_t n;            /* 4 zero bytes, then the entry */
    unit_use use[MAX_USES];
    unsigned n_use;
    /* parsed */
    unsigned n_anims, n_nodes, node[MAX_NODES];
    int parent[MAX_NODES], slot[MAX_NODES];   /* by node id, -1 none */
    int slot_node[MAX_PIECES];
    int has_rot[MAX_NODES], has_pos[MAX_NODES];
    int rot[MAX_NODES][3], pos[MAX_NODES][3];
    uint32_t list, end;                /* animation list, end of the animation block */
    const uint8_t *mesh;               /* object table */
    size_t mesh_n;
    unsigned pieces, lods, mesh_word;
    int parsed;                        /* 0: did not check out, no folder */
    /* textures and materials, in the order the faces first use them */
    tex_key key[MAX_KEYS];
    unsigned n_keys;
    material mat[MAX_MATS];
    unsigned n_mats;
    uint32_t faces;
    /* skins: maps whose textures look the same share one */
    uint8_t skin_map[MAX_SKINS];
    unsigned n_skins;
    uint8_t use_skin[MAX_USES];
} unit;

/* Joint table: u16, then [8 node] entries, then [2 node slot] / [3 node parent]. */
static int parse_joints(unit *u) {
    const uint8_t *d = u->buf;
    const uint32_t jt = u32(d + 12);
    if (jt >= u->list || u->list > u->n) return 0;
    for (unsigned i = 0; i < MAX_NODES; ++i) u->parent[i] = u->slot[i] = -1;
    for (unsigned i = 0; i < MAX_PIECES; ++i) u->slot_node[i] = -1;
    size_t p = jt + 2;
    u->n_nodes = 0;
    while (p + 2 <= u->list && d[p] == 8 && u->n_nodes < MAX_NODES) {
        if (d[p + 1] >= MAX_NODES) return 0;
        u->node[u->n_nodes++] = d[p + 1];
        p += 2;
    }
    while (p + 4 <= u->list && (d[p] == 2 || d[p] == 3)) {
        const unsigned a = d[p + 1], b = u16(d + p + 2);
        if (a >= MAX_NODES) return 0;
        if (d[p] == 2) u->slot[a] = (int)b;
        else u->parent[a] = b == 0xFFFF ? -1 : (int)b;
        p += 4;
    }
    for (unsigned i = 0; i < u->n_nodes; ++i) {
        const int sl = u->slot[u->node[i]];
        if (sl >= 0 && sl < (int)MAX_PIECES) u->slot_node[sl] = (int)u->node[i];
    }
    return u->n_nodes > 0;
}

/* Byte range of animation `a`; 0 if the unit has none there. */
static int anim_range(const unit *u, unsigned a, size_t *start, size_t *end) {
    if (a >= u->n_anims || u->list + 4 * (size_t)u->n_anims > u->end) return 0;
    const uint32_t off = u32(u->buf + u->list + 4 * a);
    if (!off && a) return 0;
    uint32_t next = u->end;
    for (unsigned k = 0; k < u->n_anims; ++k) {
        const uint32_t o = u32(u->buf + u->list + 4 * k);
        if (o > off && u->list + o < next) next = u->list + o;
    }
    *start = u->list + off;
    *end = next;
    return *start < *end && *end <= u->end;
}

typedef void (*node_fn)(void *ctx, unsigned frame, unsigned node, unsigned rot_mode,
                        const int *rot, unsigned pos_mode, const int *pos);

/* An animation record (as acr_models_moa.c's walk_anim): s16 last tick,
 * s16 frames, s16, holds, then per frame s16 count and node records. */
static int walk_anim(const unit *u, size_t p, size_t end, unsigned *frames, int *holds,
                     node_fn cb, void *ctx) {
    const uint8_t *d = u->buf;
    if (p + 6 > end) return 0;
    const int count = s16(d + p + 2);
    if (count < 1 || count > 512) return 0;
    *frames = (unsigned)count;
    p += 6;
    for (int i = 0; i < count - 1; ++i) {
        if (p + 2 > end) return 0;
        if (holds) holds[i] = s16(d + p);
        p += 2;
    }
    for (int f = 0; f < count; ++f) {
        if (p + 2 > end) return 0;
        const int n = s16(d + p);
        p += 2;
        if (n < 0 || n > (int)MAX_NODES) return 0;
        for (int k = 0; k < n; ++k) {
            if (p + 2 > end) return 0;
            const unsigned desc = u16(d + p), fl = desc & 0xFF, node = desc >> 8;
            const unsigned rm = (fl >> 2) & 3, pm = (fl >> 6) & 3;
            p += 2;
            int rot[3] = { 0, 0, 0 }, pos[3] = { 0, 0, 0 };
            for (int pass = 0; pass < 2; ++pass) {
                const unsigned m = pass ? pm : rm;
                int *v = pass ? pos : rot;
                if (m == 1) {
                    if (p + 6 > end) return 0;
                    for (int c = 0; c < 3; ++c) v[c] = s16(d + p + 2 * c);
                    p += 6;
                } else if (m == 2) {
                    if (p + 4 > end) return 0;
                    for (int c = 0; c < 3; ++c) v[c] = (int8_t)d[p + c];
                    p += 4;
                }
            }
            if (node >= MAX_NODES) return 0;
            if (cb) cb(ctx, (unsigned)f, node, rm, rot, pm, pos);
        }
    }
    return end - p <= 4;
}

static void bind_cb(void *ctx, unsigned frame, unsigned node, unsigned rm, const int *rot,
                    unsigned pm, const int *pos) {
    unit *u = ctx;
    if (frame) return;
    if (rm == 1 || rm == 2) { u->has_rot[node] = 1; memcpy(u->rot[node], rot, sizeof u->rot[node]); }
    if (pm == 1 || pm == 2) { u->has_pos[node] = 1; memcpy(u->pos[node], pos, sizeof u->pos[node]); }
}

static int parse_unit(unit *u) {
    const uint8_t *d = u->buf;
    if (u->n < 0x1C + 8) return 0;
    u->n_anims = d[4];
    const uint32_t alen = u32(d + 8);
    u->list = u32(d + 16);
    u->end = u32(d + 20);
    if (u32(d + 12) != 0x1C || u->n_anims == 0 || u->n_anims > MAX_ANIMS) return 0;
    if (4 + (size_t)alen + 8 > u->n || u->end > 4 + alen || u->list >= u->end) return 0;
    /* u32 (1-3, not the LOD count: the jets have 2 LODs and say 2, most ACs
     * 3 and say 3, but U02 says 1), u32 objects = pieces x LODs, LOD 0 first. */
    u->mesh_word = u32(d + 4 + alen);
    const uint32_t nobj = u32(d + 4 + alen + 4);
    u->mesh = d + 4 + alen + 8;
    u->mesh_n = u->n - (4 + alen + 8);
    if (!parse_joints(u)) return 0;
    u->pieces = 0;
    for (unsigned i = 0; i < u->n_nodes; ++i) {
        const int sl = u->slot[u->node[i]];
        if (sl >= 0 && (unsigned)sl + 1 > u->pieces) u->pieces = (unsigned)sl + 1;
    }
    if (!u->pieces || u->pieces > MAX_PIECES || nobj % u->pieces || 28 * (size_t)nobj > u->mesh_n) return 0;
    u->lods = nobj / u->pieces;
    size_t a, b;
    unsigned frames;
    if (!anim_range(u, 0, &a, &b) || !walk_anim(u, a, b, &frames, NULL, bind_cb, u)) return 0;
    return 1;
}

/* ---- faces ---------------------------------------------------------------- */

typedef struct face {
    unsigned n, vi[4], ni[4];
    int textured;
    tex_key key;
    uint8_t uv[4][2];
    unsigned rgb;
    int semi_bit, two_sided;
} face;

/* Faces of piece `pc` (LOD 0), lit layout as in AC parts: calls `cb` for each. */
typedef int (*face_fn)(void *ctx, unsigned pc, const face *f);

static int each_face(const unit *u, unsigned pc, face_fn cb, void *ctx) {
    const uint8_t *m = u->mesh;
    const size_t size = u->mesh_n, ob = 28 * (size_t)pc;
    const uint32_t vt = u32(m + ob), nv = u32(m + ob + 4) & 0xFFFF, nt = u32(m + ob + 8),
                   nn = u32(m + ob + 12) & 0xFFFF, pt = u32(m + ob + 16), np = u32(m + ob + 20) & 0xFFFF;
    if ((size_t)vt + 8 * nv > size || (size_t)nt + 8 * nn > size) return 0;
    size_t p = pt;
    for (uint32_t k = 0; k < np; ++k) {
        if (p + 4 > size) return 0;
        const uint32_t h = u32(m + p);
        const unsigned ilen = (h >> 8) & 0xFF, flag = (h >> 16) & 0xFF, mode = h >> 24;
        if (p + 4 + 4 * (size_t)ilen > size) return 0;
        const uint8_t *w = m + p + 4;
        p += 4 + 4 * (size_t)ilen;
        face f;
        memset(&f, 0, sizeof f);
        const int quad = (mode & 8) != 0, tex = (mode & 4) != 0, gour = (mode & 0x10) != 0;
        f.n = quad ? 4u : 3u;
        const unsigned idx_at = tex ? f.n * 2 : 2;               /* halfwords before the indices */
        if (idx_at + (gour ? 2 * f.n : 1 + f.n) > ilen * 2) return 0;
        for (unsigned j = 0; j < f.n; ++j) {
            if (gour) { f.ni[j] = u16(w + 2 * (idx_at + 2 * j)); f.vi[j] = u16(w + 2 * (idx_at + 2 * j + 1)); }
            else { f.ni[j] = u16(w + 2 * idx_at); f.vi[j] = u16(w + 2 * (idx_at + 1 + j)); }
        }
        int bad = 0;
        for (unsigned j = 0; j < f.n; ++j) if (f.vi[j] >= nv || f.ni[j] >= nn) bad = 1;
        if (bad) continue;
        f.two_sided = (flag & 2) != 0;
        f.semi_bit = (mode & 2) != 0;
        if (tex) {
            f.textured = 1;
            f.key.clut = u16(w + 2);
            f.key.tpage = (uint16_t)(u16(w + 6) & 0x1FF);
            for (unsigned j = 0; j < f.n; ++j) { f.uv[j][0] = w[4 * j]; f.uv[j][1] = w[4 * j + 1]; }
        } else {
            f.rgb = (unsigned)w[0] << 16 | (unsigned)w[1] << 8 | w[2];
        }
        if (!cb(ctx, pc, &f)) return 0;
    }
    return 1;
}

/* The material a face draws with (added if new). */
static int face_material(unit *u, const acr_moa_vram *v, const face *f) {
    material m = { -1, f->rgb, 0, (uint8_t)f->two_sided };
    if (f->textured) {
        if (v && acr_moa_vram_has_page(v, f->key.tpage)) {
            unsigned t = 0;
            while (t < u->n_keys && (u->key[t].tpage != f->key.tpage || u->key[t].clut != f->key.clut)) ++t;
            if (t == u->n_keys) {
                if (u->n_keys >= MAX_KEYS) return -1;
                u->key[u->n_keys++] = f->key;
            }
            m.tex = (int)t;
            m.semi = (uint8_t)(f->semi_bit && acr_moa_vram_semi(v, f->key.tpage, f->key.clut));
        } else {
            m.rgb = 0x606060;   /* page not loaded: grey, as the stages do */
        }
    } else {
        m.semi = (uint8_t)f->semi_bit;
    }
    for (unsigned i = 0; i < u->n_mats; ++i) {
        const material *o = &u->mat[i];
        if (o->tex == m.tex && (m.tex >= 0 || o->rgb == m.rgb) && o->semi == m.semi && o->two_sided == m.two_sided)
            return (int)i;
    }
    if (u->n_mats >= MAX_MATS) return -1;
    u->mat[u->n_mats] = m;
    return (int)u->n_mats++;
}

static void material_name(const material *m, char *out, size_t n) {
    if (m->tex >= 0) snprintf(out, n, "t%02d%s%s", m->tex, m->semi ? "-semi" : "", m->two_sided ? "-2side" : "");
    else snprintf(out, n, "rgb-%06X%s%s", m->rgb & 0xFFFFFFu, m->semi ? "-semi" : "", m->two_sided ? "-2side" : "");
}

typedef struct obj_ctx {
    unit *u;
    const acr_moa_vram *v;
    FILE *f;
    int cur_mat;
    unsigned vt_count;
} obj_ctx;

static int write_face(void *ctx, unsigned pc, const face *fc) {
    (void)pc;
    obj_ctx *c = ctx;
    const int mi = face_material(c->u, c->v, fc);
    if (mi < 0) return 0;
    if (mi != c->cur_mat) {
        char name[48];
        material_name(&c->u->mat[mi], name, sizeof name);
        fprintf(c->f, "usemtl %s\n", name);
        c->cur_mat = mi;
    }
    const int tex = c->u->mat[mi].tex >= 0;
    unsigned t0 = 0;
    if (tex) {
        for (unsigned j = 0; j < fc->n; ++j)
            fprintf(c->f, "vt %.5f %.5f\n", (fc->uv[j][0] + 0.5) / 256.0, 1.0 - (fc->uv[j][1] + 0.5) / 256.0);
        t0 = c->vt_count + 1;
        c->vt_count += fc->n;
    }
    const unsigned order[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
    for (unsigned tri = 0; tri < (fc->n == 4 ? 2u : 1u); ++tri) {
        fputc('f', c->f);
        for (unsigned j = 0; j < 3; ++j) {
            const unsigned q = order[tri][j];
            if (tex) fprintf(c->f, " %u/%u/%u", fc->vi[q] + 1, t0 + q, fc->ni[q] + 1);
            else fprintf(c->f, " %u//%u", fc->vi[q] + 1, fc->ni[q] + 1);
        }
        fputc('\n', c->f);
        ++c->u->faces;
    }
    return 1;
}

static int write_pieces(unit *u, const acr_moa_vram *v, const char *dir, const char *id) {
    u->faces = 0;
    for (unsigned pc = 0; pc < u->pieces; ++pc) {
        const size_t ob = 28 * (size_t)pc;
        const uint32_t vt = u32(u->mesh + ob), nv = u32(u->mesh + ob + 4) & 0xFFFF,
                       nt = u32(u->mesh + ob + 8), nn = u32(u->mesh + ob + 12) & 0xFFFF;
        if ((size_t)vt + 8 * nv > u->mesh_n || (size_t)nt + 8 * nn > u->mesh_n) return 0;
        const int node = u->slot_node[pc];
        int off[3] = { 0, 0, 0 };
        if (node >= 0 && u->has_pos[node]) memcpy(off, u->pos[node], sizeof off);
        char path[1400];
        snprintf(path, sizeof path, "%s/piece%u.obj", dir, pc);
        FILE *f = fopen(path, "wb");
        if (!f) return 0;
        fprintf(f, "# Master of Arena mission unit %s, piece %u, imported from your disc by acr-import.\n"
                   "# At its node's bind position: x right, y up, z back (game units).\n"
                   "mtllib unit.mtl\no piece%u\n", id, pc, pc);
        for (uint32_t i = 0; i < nv; ++i) {
            const uint8_t *p = u->mesh + vt + 8 * i;
            fprintf(f, "v %d %d %d\n", s16(p) + off[0], -(s16(p + 2) + off[1]), -(s16(p + 4) + off[2]));
        }
        for (uint32_t i = 0; i < nn; ++i) {
            const uint8_t *p = u->mesh + nt + 8 * i;
            fprintf(f, "vn %.5f %.5f %.5f\n", s16(p) / 4096.0, -s16(p + 2) / 4096.0, -s16(p + 4) / 4096.0);
        }
        obj_ctx c = { u, v, f, -1, 0 };
        const int ok = each_face(u, pc, write_face, &c);
        if (fclose(f) != 0 || !ok) return 0;
    }
    return 1;
}

/* ---- skins ------------------------------------------------------------------
 * The same unit on another map draws from that map's VRAM. Two maps share a
 * skin when every texel its faces cover looks the same. */

typedef struct cmp_ctx { const acr_moa_vram *a, *b; int same; } cmp_ctx;

static int compare_face(void *ctx, unsigned pc, const face *f) {
    (void)pc;
    cmp_ctx *c = ctx;
    if (!c->same || !f->textured) return 1;
    const int ha = acr_moa_vram_has_page(c->a, f->key.tpage), hb = acr_moa_vram_has_page(c->b, f->key.tpage);
    if (ha != hb) { c->same = 0; return 1; }
    if (!ha) return 1;
    unsigned u0 = 255, u1 = 0, t0 = 255, t1 = 0;
    for (unsigned j = 0; j < f->n; ++j) {
        if (f->uv[j][0] < u0) u0 = f->uv[j][0];
        if (f->uv[j][0] > u1) u1 = f->uv[j][0];
        if (f->uv[j][1] < t0) t0 = f->uv[j][1];
        if (f->uv[j][1] > t1) t1 = f->uv[j][1];
    }
    for (unsigned t = t0; t <= t1 && c->same; ++t)
        for (unsigned s = u0; s <= u1; ++s)
            if (acr_moa_vram_texel(c->a, f->key.tpage, f->key.clut, s, t) !=
                acr_moa_vram_texel(c->b, f->key.tpage, f->key.clut, s, t)) { c->same = 0; break; }
    return 1;
}

static int same_skin(const unit *u, const acr_moa_vram *a, const acr_moa_vram *b) {
    if (a == b) return 1;
    if (!a || !b) return !a && !b;
    cmp_ctx c = { a, b, 1 };
    for (unsigned pc = 0; pc < u->pieces && c.same; ++pc) each_face(u, pc, compare_face, &c);
    return c.same;
}

static int write_mtl(const unit *u, const acr_moa_vram *v, const char *dir, const char *id,
                     const char *mtl, const char *tex_prefix, unsigned map) {
    char path[1400];
    for (unsigned t = 0; t < u->n_keys; ++t) {
        snprintf(path, sizeof path, "%s/%stexture-%02u.png", dir, tex_prefix, t);
        if (!acr_moa_vram_write_page(path, v, u->key[t].tpage, u->key[t].clut)) return 0;
    }
    snprintf(path, sizeof path, "%s/%s", dir, mtl);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "# Materials for mission unit %s as map %u paints it (textures from RTIM %u).\n"
               "# tNN = texture page NN with its palette, rgb-RRGGBB = flat colour;\n"
               "# -semi = semi-transparent, -2side = no culling.\n", id, map, map);
    for (unsigned i = 0; i < u->n_mats; ++i) {
        const material *m = &u->mat[i];
        char name[48];
        material_name(m, name, sizeof name);
        fprintf(f, "\nnewmtl %s\n", name);
        if (m->tex >= 0) fprintf(f, "Kd 1 1 1\nmap_Kd %stexture-%02d.png\n", tex_prefix, m->tex);
        else fprintf(f, "Kd %.4f %.4f %.4f\n", ((m->rgb >> 16) & 255) / 255.0, ((m->rgb >> 8) & 255) / 255.0,
                     (m->rgb & 255) / 255.0);
        if (m->semi) fprintf(f, "d 0.5\n");
    }
    return fclose(f) == 0;
}

static void skin_mtl(unsigned skin_map, int first, char *out, size_t n) {
    if (first) snprintf(out, n, "unit.mtl");
    else snprintf(out, n, "map-%02u.mtl", skin_map);
}

/* ---- skeleton and animations -------------------------------------------------- */

static int write_skeleton(const unit *u, const char *dir, const char *id) {
    char path[1400];
    snprintf(path, sizeof path, "%s/skeleton.toml", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "# Master of Arena mission unit %s skeleton, imported from your disc.\n"
               "# Game space (x right, y down, z forward), 4096 = a full turn.\n"
               "# A node's matrix = parent's matrix x T(position) x Rx x Ry x Rz.\n"
               "# draws = \"unit K\": the node draws pieceK.obj.\n"
               "# Bind pose = animation 0, frame 0 (other animations set their own values).\n"
               "unit = \"%s\"\nanimations = \"anims.txt\"\n", id, id);
    for (unsigned i = 0; i < u->n_nodes; ++i) {
        const unsigned n = u->node[i];
        fprintf(f, "\n[[node]]\nid       = %u\nparent   = %d\n", n, u->parent[n]);
        if (u->slot[n] >= 0 && (unsigned)u->slot[n] < u->pieces)
            fprintf(f, "slot     = %d\ndraws    = \"unit %d\"\n", u->slot[n], u->slot[n]);
        if (u->has_pos[n]) fprintf(f, "position = [%d, %d, %d]\n", u->pos[n][0], u->pos[n][1], u->pos[n][2]);
        if (u->has_rot[n]) fprintf(f, "rotation = [%d, %d, %d]\n", u->rot[n][0], u->rot[n][1], u->rot[n][2]);
    }
    return fclose(f) == 0;
}

typedef struct text_ctx { FILE *f; int cur; } text_ctx;

static void text_cb(void *ctx, unsigned frame, unsigned node, unsigned rm, const int *rot,
                    unsigned pm, const int *pos) {
    text_ctx *t = ctx;
    if ((int)frame != t->cur) { fprintf(t->f, "frame %u\n", frame); t->cur = (int)frame; }
    fprintf(t->f, "  node %u", node);
    if (rm == 3) fprintf(t->f, " rot zero");
    else if (rm) fprintf(t->f, " rot %d %d %d", rot[0], rot[1], rot[2]);
    if (pm == 3) fprintf(t->f, " pos zero");
    else if (pm) fprintf(t->f, " pos %d %d %d", pos[0], pos[1], pos[2]);
    fputc('\n', t->f);
}

static int write_anims(const unit *u, const char *dir, const char *id, unsigned *present) {
    char path[1400];
    snprintf(path, sizeof path, "%s/anims.txt", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "# Master of Arena mission unit %s, joint animations, imported from your disc.\n"
               "# Each frame lists only the nodes that change. Frame 0 sets values; later\n"
               "# frames add their values as steps (\"zero\" there: no change).\n"
               "# Frame 0: rot / pos = value, \"zero\" = 0; a node not listed keeps its value.\n"
               "# hold = ticks each frame is shown before the next (one fewer than frames).\n", id);
    int ok = 1;
    *present = 0;
    for (unsigned a = 0; ok && a < u->n_anims; ++a) {
        size_t start, end;
        if (!anim_range(u, a, &start, &end)) continue;
        unsigned frames = 0;
        int holds[512];
        if (!walk_anim(u, start, end, &frames, holds, NULL, NULL)) { ok = 0; break; }
        fprintf(f, "\nanim %u\nlast_tick %d\nframes %u\nhold", a, s16(u->buf + start), frames);
        for (unsigned i = 0; i + 1 < frames; ++i) fprintf(f, " %d", holds[i]);
        fputc('\n', f);
        text_ctx t = { f, -1 };
        ok = walk_anim(u, start, end, &frames, NULL, text_cb, &t);
        ++*present;
    }
    return (fclose(f) == 0) && ok;
}

/* ---- one unit's folder ------------------------------------------------------- */

static int export_unit(unit *u, unsigned index, map_file *maps, const char *unpacked, const char *units_dir) {
    char id[16], dir[1200];
    snprintf(id, sizeof id, "U%02u", index);
    snprintf(dir, sizeof dir, "%s/%s", units_dir, id);
    if (!make_dirs(dir)) return 0;
    const unsigned m0 = u->use[0].map;
    const acr_moa_vram *v0 = map_vram(&maps[m0], unpacked, m0);
    if (!write_pieces(u, v0, dir, id)) return 0;
    /* Skins: the first map's, then one for each map that paints it differently. */
    u->n_skins = 1;
    u->skin_map[0] = (uint8_t)m0;
    for (unsigned k = 0; k < u->n_use; ++k) {
        const unsigned mk = u->use[k].map;
        const acr_moa_vram *vk = map_vram(&maps[mk], unpacked, mk);
        unsigned s = 0;
        while (s < u->n_skins && !same_skin(u, map_vram(&maps[u->skin_map[s]], unpacked, u->skin_map[s]), vk)) ++s;
        if (s == u->n_skins && u->n_skins < MAX_SKINS) u->skin_map[u->n_skins++] = (uint8_t)mk;
        u->use_skin[k] = (uint8_t)(s < u->n_skins ? s : 0);
    }
    for (unsigned s = 0; s < u->n_skins; ++s) {
        char mtl[32], prefix[16];
        skin_mtl(u->skin_map[s], s == 0, mtl, sizeof mtl);
        if (s == 0) prefix[0] = 0;
        else snprintf(prefix, sizeof prefix, "map-%02u-", u->skin_map[s]);
        const unsigned ms = u->skin_map[s];
        if (!write_mtl(u, map_vram(&maps[ms], unpacked, ms), dir, id, mtl, prefix, ms)) return 0;
    }
    if (!write_skeleton(u, dir, id)) return 0;
    unsigned present = 0;
    if (!write_anims(u, dir, id, &present)) return 0;

    char path[1400];
    snprintf(path, sizeof path, "%s/unit.toml", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "# Master of Arena mission unit %s, imported from your disc by acr-import.\n"
               "# A mission map's chunk 2 slot K is drawn by object handler K (objects.toml).\n"
               "# Each piece is drawn with its node's matrix (skeleton.toml), less the\n"
               "# node's bind position, as AC parts are.\n"
               "id         = \"%s\"\npieces     = %u\nlods       = %u   # pieceK.obj is LOD 0\n"
               "mesh_word  = %u   # the u32 before the object count; not the LOD count\n"
               "nodes      = %u\nfaces      = %u\n"
               "animations = %u   # in anims.txt (the list has %u places)\n"
               "textures   = %u\nskeleton   = \"skeleton.toml\"\nanims      = \"anims.txt\"\n",
            id, id, u->pieces, u->lods, u->mesh_word, u->n_nodes, (unsigned)u->faces, present, u->n_anims, u->n_keys);
    fprintf(f, "maps       = [");
    for (unsigned k = 0; k < u->n_use; ++k) fprintf(f, "%s%u", k ? ", " : "", u->use[k].map);
    fprintf(f, "]\nslots      = [");
    for (unsigned k = 0; k < u->n_use; ++k) fprintf(f, "%s%u", k ? ", " : "", u->use[k].slot);
    fprintf(f, "]   # chunk 2 slot on each of those maps\n");
    for (unsigned s = 0; s < u->n_skins; ++s) {
        char mtl[32];
        skin_mtl(u->skin_map[s], s == 0, mtl, sizeof mtl);
        fprintf(f, "\n[[skin]]\nmtl  = \"%s\"\nmaps = [", mtl);
        int first = 1;
        for (unsigned k = 0; k < u->n_use; ++k) {
            if (u->use_skin[k] != s) continue;
            int dup = 0;
            for (unsigned j = 0; j < k; ++j) if (u->use[j].map == u->use[k].map) dup = 1;
            if (dup) continue;
            fprintf(f, "%s%u", first ? "" : ", ", u->use[k].map);
            first = 0;
        }
        fprintf(f, "]\n");
    }
    return fclose(f) == 0;
}

/* ---- a map's objects ---------------------------------------------------------- */

/* Chunk 12 (mission engine loader 0x800795A8): u16 count, then entries
 * [u32 size][36-byte header][size - 36 bytes of data]; entry k belongs to
 * object handler k + 1. The object takes its body box from the data's
 * halfwords 2 and 3, each / 4 (0x8007EB00: object +0x28 / +0x2A, the AC's
 * stage box fields): half width either side, and height. */
static int handler_box(const map_file *m, int handler, unsigned *half, unsigned *height) {
    const chunk *c = &m->ch[12];
    if (handler < 1 || c->n < 4) return 0;
    const unsigned count = u16(c->p);
    size_t o = 4;
    for (unsigned k = 0; k < count; ++k) {
        if (o + 4 > c->n) return 0;
        const uint32_t n = u32(c->p + o);
        if (n > c->n - o - 4) return 0;
        if (k + 1 == (unsigned)handler) {
            if (n < 36 + 8) return 0;
            *half = u16(c->p + o + 4 + 36 + 4) >> 2;
            *height = u16(c->p + o + 4 + 36 + 6) >> 2;
            return *half && *height;
        }
        o += 4 + n;
    }
    return 0;
}

/* Each chunk 12 entry's motion set (the data after its 36-byte header, in
 * the AC format) as <stages>/MM/motion-HH.toml, HH = its handler: how the
 * unit's animations move it (an MT walks by them). Entries without a usable
 * set are skipped. */
static int write_motions(const map_file *m, const char *dir) {
    const chunk *c = &m->ch[12];
    if (c->n < 4) return 1;
    const unsigned count = u16(c->p);
    size_t o = 4;
    for (unsigned k = 0; k < count; ++k) {
        if (o + 4 > c->n) break;
        const uint32_t n = u32(c->p + o);
        if (n > c->n - o - 4) break;
        if (n > 36 + 16) {
            char path[1400];
            snprintf(path, sizeof path, "%s/motion-%02u.toml", dir, k + 1);
            if (!acr_write_root_motion(path, k + 1, c->p + o + 4 + 36, n - 36, 1)) remove(path);
        }
        o += 4 + n;
    }
    return 1;
}

static int write_objects(const map_file *m, const unit *units, unsigned map, const char *stages_dir) {
    char dir[1200], path[1400];
    snprintf(dir, sizeof dir, "%s/%02u", stages_dir, map);
    if (!make_dirs(dir)) return 0;
    write_motions(m, dir);
    snprintf(path, sizeof path, "%s/objects.toml", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    const chunk *c = &m->ch[13];
    fprintf(f, "# Master of Arena mission map %u: its object records (FDAT %u chunk 13,\n"
               "# 40 bytes each), imported from your disc by acr-import.\n"
               "# position: engine x = -(game x), y down and z as in the game (as stage.toml).\n"
               "# facing: 4096 a turn. handler K = mission code slot 8 + K, which draws\n"
               "# chunk 2 slot K: unit (units/UNN), skin = the unit's .mtl for this map.\n"
               "# box: the body (chunk 12: half width, height). ap = record +22, attack =\n"
               "# its gun's attack (+24), energy_n = +16 bits 8-11 (energy damage taken\n"
               "# x (8 - n) / 8). Not read yet: type (+14); raw = all twenty s16 words.\n"
               "# Records 0-2 are kept as points (1 and 2 are the arena's starts).\n"
               "map = %u\n", map, 5 * map + 4, map);
    const unsigned count = c->n / POINT_SIZE;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *r = c->p + i * POINT_SIZE;
        int any = 0;
        for (unsigned k = 0; k < POINT_SIZE; ++k) if (r[k]) any = 1;
        const int handler = s16(r + 6);
        if (!any || (i >= 3 && handler == -1)) continue;
        fprintf(f, "\n[[%s]]\nrecord   = %u\n", i < 3 ? "point" : "object", i);
        if (i >= 3) {
            fprintf(f, "handler  = %d\n", handler);
            const char *uid = "";
            char idbuf[16] = "", mtl[32] = "";
            if (handler >= 0 && (unsigned)handler < m->n_slots && m->unit[handler] >= 0) {
                const unit *u = &units[m->unit[handler]];
                snprintf(idbuf, sizeof idbuf, "U%02d", m->unit[handler]);
                uid = idbuf;
                for (unsigned k = 0; k < u->n_use; ++k)
                    if (u->use[k].map == map && u->use[k].slot == (unsigned)handler)
                        skin_mtl(u->skin_map[u->use_skin[k]], u->use_skin[k] == 0, mtl, sizeof mtl);
            }
            fprintf(f, "unit     = \"%s\"%s\nskin     = \"%s\"\n", uid, uid[0] ? "" : "   # no model", mtl);
        }
        fprintf(f, "position = [%d, %d, %d]\nfacing   = %u\n", -s16(r), s16(r + 2), s16(r + 4),
                (unsigned)(u16(r + 10) & 4095));
        if (i >= 3) {
            fprintf(f, "type     = %d\n", s16(r + 14));
            unsigned half = 0, height = 0;
            if (handler_box(m, handler, &half, &height))
                fprintf(f, "box      = [%u, %u]   # half width, height (chunk 12 entry %d)\n", half, height, handler - 1);
            fprintf(f, "ap       = %d\nattack   = %d\nenergy_n = %d\n", u16(r + 22), u16(r + 24),
                    (int)(((u16(r + 16) >> 8) & 15) ^ 8) - 8);
        }
        fprintf(f, "raw      = [");
        for (unsigned k = 0; k < POINT_SIZE / 2; ++k) fprintf(f, "%s%d", k ? ", " : "", s16(r + 2 * k));
        fprintf(f, "]\n");
    }
    return fclose(f) == 0;
}

/* ---- all of it ------------------------------------------------------------------ */

int acr_export_moa_units(const char *unpacked_dir, const char *units_dir, const char *stages_dir,
                         uint32_t *units_out, char *err, size_t err_size) {
    *units_out = 0;
    map_file *maps = calloc(MAPS, sizeof *maps);
    unit *units = calloc(MAX_UNITS, sizeof *units);
    unsigned n_units = 0;
    int ok = maps && units;
    if (!ok) set_err(err, err_size, "out of memory", NULL);
    for (unsigned s = 0; ok && s < MAPS; ++s) {
        map_file *m = &maps[s];
        char path[1200];
        snprintf(path, sizeof path, "%s/GG/COM/FDAT/%03u.bin", unpacked_dir, 5 * s + 4);
        m->data = read_all(path, &m->size);
        if (!m->data) continue;                           /* not on this import: skipped */
        if (!split(m->data, m->size, m->ch)) { free(m->data); m->data = NULL; continue; }
        read_slots(m);
        for (unsigned k = 0; ok && k < m->n_slots; ++k) {
            if (!m->slot[k]) continue;
            unsigned i = 0;
            while (i < n_units && !(units[i].n == m->slot_n[k] + 4 &&
                                    !memcmp(units[i].buf + 4, m->slot[k], m->slot_n[k]))) ++i;
            if (i == n_units) {
                if (n_units >= MAX_UNITS) break;
                unit *u = &units[n_units];
                u->n = m->slot_n[k] + 4;
                u->buf = calloc(1, u->n);
                if (!u->buf) { ok = 0; set_err(err, err_size, "out of memory", NULL); break; }
                memcpy(u->buf + 4, m->slot[k], m->slot_n[k]);
                u->parsed = parse_unit(u);    /* keeps its number either way, so U-numbers stay put */
                ++n_units;
            }
            unit *u = &units[i];
            if (u->n_use < MAX_USES) {
                u->use[u->n_use].map = (uint8_t)s;
                u->use[u->n_use].slot = (uint8_t)k;
                ++u->n_use;
            }
            if (units[i].parsed) m->unit[k] = (int)i;
        }
    }
    if (ok && n_units && !make_dirs(units_dir)) { ok = 0; set_err(err, err_size, "cannot create", units_dir); }
    for (unsigned i = 0; ok && i < n_units; ++i) {
        if (!units[i].parsed) continue;
        if (!export_unit(&units[i], i, maps, unpacked_dir, units_dir)) {
            ok = 0;
            set_err(err, err_size, "a mission unit did not check out while exporting into", units_dir);
        } else {
            ++*units_out;
        }
    }
    for (unsigned s = 0; ok && s < MAPS; ++s) {
        if (!maps[s].data) continue;
        if (!write_objects(&maps[s], units, s, stages_dir)) {
            ok = 0;
            set_err(err, err_size, "cannot write the mission objects in", stages_dir);
        }
    }
    if (maps)
        for (unsigned s = 0; s < MAPS; ++s) { free(maps[s].data); acr_moa_vram_free(maps[s].vram); }
    if (units)
        for (unsigned i = 0; i < n_units; ++i) free(units[i].buf);
    free(maps);
    free(units);
    return ok;
}
