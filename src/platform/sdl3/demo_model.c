#include "demo_model.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../model/acr_png.h"
#include "../../render/acr_render.h"

static char *load_text(const char *path) { return SDL_LoadFile(path, NULL); }

/* Which garage slot fills each part kind of the skeleton. */
static int part_index(int kind, const int parts[SLOT_COUNT]) {
    switch (kind) {
    case ACR_PART_CORE: return parts[SLOT_CORE];
    case ACR_PART_HEAD: return parts[SLOT_HEAD];
    case ACR_PART_ARMS: return parts[SLOT_ARMS];
    case ACR_PART_LEGS: return parts[SLOT_LEGS];
    case ACR_PART_HAND:                      /* weapon arms (12 and up) have no hand */
        if (parts[SLOT_ARMS] >= 12) return -1;
        return parts[SLOT_HAND] == 255 ? 0 : parts[SLOT_HAND];   /* 0: the empty hand */
    /* A two-slot unit (52 and up) is fitted in one slot but has a model
     * half for each shoulder: an empty other slot shows its other half. */
    case ACR_PART_BACK_LEFT:
        if (parts[SLOT_BACK_L] == 255) return parts[SLOT_BACK_R] != 255 && parts[SLOT_BACK_R] >= 52 ? parts[SLOT_BACK_R] : -1;
        return parts[SLOT_BACK_L];
    case ACR_PART_BACK_RIGHT:
        if (parts[SLOT_BACK_R] == 255) return parts[SLOT_BACK_L] != 255 && parts[SLOT_BACK_L] >= 52 ? parts[SLOT_BACK_L] : -1;
        return parts[SLOT_BACK_R];
    case ACR_PART_BOOSTER: return parts[SLOT_BOOSTER] == 255 ? -1 : parts[SLOT_BOOSTER];
    default: return -1;
    }
}

static int patterned(int kind) {
    return kind == ACR_PART_CORE || kind == ACR_PART_HEAD || kind == ACR_PART_ARMS || kind == ACR_PART_LEGS;
}

static uint32_t load_texture(const char *path, int kind, const uint8_t colours[4][3]) {
    size_t size = 0;
    uint8_t *data = SDL_LoadFile(path, &size);
    if (!data) return 0;
    acr_image img;
    const int ok = acr_png_decode(data, size, &img);
    SDL_free(data);
    if (!ok) return 0;
    uint8_t *rgba = malloc((size_t)img.w * img.h * 4);
    uint32_t tex = 0;
    if (rgba) {
        if (img.indexed) {
            uint8_t pal[256][4];
            memcpy(pal, img.palette, sizeof pal);
            if (colours && patterned(kind)) acr_colour_ramp(colours, pal);
            for (int i = 0; i < img.w * img.h; ++i) memcpy(rgba + 4 * i, pal[img.pixels[i]], 4);
        } else {
            memcpy(rgba, img.pixels, (size_t)img.w * img.h * 4);
        }
        tex = acr_render_texture(rgba, img.w, img.h, 1);
        free(rgba);
    }
    acr_image_free(&img);
    return tex;
}

static int load_part(const char *models, int kind, int index, const uint8_t colours[4][3], demo_part_model *p) {
    char dir[1100], path[1200];
    SDL_snprintf(dir, sizeof dir, "%s/%s/%02d", models, acr_part_kind_name(kind), index);
    SDL_snprintf(path, sizeof path, "%s/part.toml", dir);
    char *toml = load_text(path);
    if (!toml) return 0;
    const char *v = SDL_strstr(toml, "\npieces");
    int pieces = 0;
    if (v) { v = SDL_strchr(v, '='); pieces = v ? SDL_atoi(v + 1) : 0; }
    {
        int k = 0;
        for (const char *b = SDL_strstr(toml, "[[piece]]"); b && k < DEMO_MAX_PIECES; ++k) {
            const char *next = SDL_strstr(b + 9, "[[piece]]");
            const char *j = SDL_strstr(b, "\njoint");
            if (j && (!next || j < next) && (j = SDL_strchr(j, '[')) && (!next || j < next)) {
                char *q = (char *)j + 1;
                for (int c = 0; c < 3; ++c) {
                    p->joint[k][c] = (int)SDL_strtol(q, &q, 10);
                    while (*q == ',' || *q == ' ') ++q;
                }
                p->has_joint[k] = 1;
            }
            b = next;
        }
    }
    SDL_free(toml);
    if (pieces <= 0) return 0;
    if (pieces > DEMO_MAX_PIECES) pieces = DEMO_MAX_PIECES;
    SDL_snprintf(path, sizeof path, "%s/part.mtl", dir);
    char *mtl = load_text(path);
    if (!mtl || !acr_mtl_parse(mtl, &p->mtl)) { SDL_free(mtl); return 0; }
    SDL_free(mtl);
    for (int k = 0; k < pieces; ++k) {
        SDL_snprintf(path, sizeof path, "%s/piece%d.obj", dir, k);
        char *obj = load_text(path);
        if (obj && acr_obj_parse(obj, &p->mtl, &p->piece[k]))
            p->marker[k] = p->piece[k].tri_count == 1 && !p->mtl.m[p->piece[k].material[0]].textured;
        SDL_free(obj);
    }
    p->pieces = pieces;
    SDL_snprintf(path, sizeof path, "%s/texture.png", dir);
    p->texture = load_texture(path, kind, colours);
    p->index = index;
    p->present = 1;
    return 1;
}

void demo_model_free(demo_model *m) {
    for (int k = 0; k < ACR_PART_KINDS; ++k)
        for (int i = 0; i < m->part[k].pieces; ++i) acr_mesh_free(&m->part[k].piece[i]);
    acr_anim_set_free(&m->anims);
    memset(m, 0, sizeof *m);
}

/* Which part's paint a patterned part kind takes (the game's order: head,
 * core, arms, legs); -1 for the rest. */
static int paint_part(int kind) {
    switch (kind) {
    case ACR_PART_HEAD: return 0;
    case ACR_PART_CORE: return 1;
    case ACR_PART_ARMS: return 2;
    case ACR_PART_LEGS: return 3;
    default: return -1;
    }
}

int demo_model_load(const char *import_dir, int set, const int parts[SLOT_COUNT],
                    const uint8_t colours[4][3], demo_model *m) {
    if (!colours) return demo_model_load_paint(import_dir, set, parts, NULL, m);
    uint8_t paint[4][4][3];
    for (int k = 0; k < 4; ++k) memcpy(paint[k], colours, sizeof paint[k]);
    return demo_model_load_paint(import_dir, set, parts, (const uint8_t (*)[4][3])paint, m);
}

int demo_model_load_paint(const char *import_dir, int set, const int parts[SLOT_COUNT],
                          const uint8_t paint[4][4][3], demo_model *m) {
    memset(m, 0, sizeof *m);
    char models[1024], path[1100];
    SDL_snprintf(models, sizeof models, "%s/models", import_dir);
    SDL_snprintf(path, sizeof path, "%s/sets/set-%02d.toml", models, set);
    char *t = load_text(path);
    const int skel_ok = t && acr_skeleton_parse(t, &m->skel);
    SDL_free(t);
    if (!skel_ok) return 0;
    SDL_snprintf(path, sizeof path, "%s/sets/set-%02d-anims.txt", models, set);
    t = load_text(path);
    if (t) acr_anim_set_parse(t, &m->skel, &m->anims);   /* without it the bind pose is shown */
    SDL_free(t);
    for (int kind = 0; kind < ACR_PART_KINDS; ++kind) {
        const int index = part_index(kind, parts);
        const int pp = paint_part(kind);
        if (index >= 0) load_part(models, kind, index, paint && pp >= 0 ? paint[pp] : NULL, &m->part[kind]);
    }
    m->ok = m->part[ACR_PART_CORE].present && m->part[ACR_PART_LEGS].present;
    if (!m->ok) demo_model_free(m);
    return m->ok;
}

/* ---- drawing ---- */

#define MAX_MODEL_TRIS 8192
#define GROUPS (ACR_PART_KINDS + 1)          /* a texture per part kind, plus untextured */
static acr_vtx g_out[MAX_MODEL_TRIS * 3], g_batch[MAX_MODEL_TRIS * 3];
static uint8_t g_group[MAX_MODEL_TRIS];

/* Garage > ASSEMBLE: the part kinds drawn as a wireframe in one colour
 * instead of solid (demo_model_set_wire). */
static unsigned g_wire_kinds;
static uint8_t g_wire_rgba[4];
#define MAX_WIRE_VERTS (MAX_MODEL_TRIS * 6)
static acr_vtx g_wire[MAX_WIRE_VERTS];

void demo_model_set_wire(unsigned kinds, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    g_wire_kinds = kinds;
    g_wire_rgba[0] = r; g_wire_rgba[1] = g; g_wire_rgba[2] = b; g_wire_rgba[3] = a;
}

void demo_model_draw(const demo_model *m, int anim, unsigned tick, float x, float y, float z,
                     float yaw, float scale, const acr_lighting *light) {
    if (!m->ok) return;
    acr_pose pose;
    acr_pose_play(&m->skel, &m->anims, anim, tick, &pose);
    demo_model_draw_pose(m, &pose, x, y, z, yaw, scale, light);
}

int demo_model_bounds(const demo_model *m, int anim, unsigned tick, float mn[3], float mx[3]) {
    if (!m->ok) return 0;
    acr_pose pose;
    acr_pose_play(&m->skel, &m->anims, anim, tick, &pose);
    float mat[ACR_SKEL_MAX_NODES][12];
    acr_pose_matrices(&m->skel, &pose, mat);
    int found = 0;
    for (int i = 0; i < m->skel.count; ++i) {
        const acr_skel_node *node = &m->skel.node[i];
        if (node->draw_kind >= ACR_PART_KINDS) continue;
        const demo_part_model *p = &m->part[node->draw_kind];
        if (!p->present || node->draw_piece >= p->pieces) continue;
        if (p->marker[node->draw_piece]) continue;   /* not seen: not framed either */
        const acr_mesh *mesh = &p->piece[node->draw_piece];
        const float *M = mat[i];
        float d[3] = { 0, 0, 0 };
        if (p->has_joint[node->draw_piece]) {   /* as demo_model_draw_pose */
            const int *jt = p->joint[node->draw_piece];
            d[0] = (float)(node->pos[0] + jt[0]);
            d[1] = -(float)(node->pos[1] + jt[1]);
            d[2] = -(float)(node->pos[2] + jt[2]);
        }
        for (int v = 0; v < mesh->tri_count * 3; ++v) {
            const float px = mesh->v[v].p[0] + d[0], py = mesh->v[v].p[1] + d[1], pz = mesh->v[v].p[2] + d[2];
            const float o[3] = { -(M[0] * px + M[1] * py + M[2] * pz + M[3]), M[4] * px + M[5] * py + M[6] * pz + M[7],
                                 M[8] * px + M[9] * py + M[10] * pz + M[11] };
            for (int k = 0; k < 3; ++k) {
                if (!found || o[k] < mn[k]) mn[k] = o[k];
                if (!found || o[k] > mx[k]) mx[k] = o[k];
            }
            found = 1;
        }
    }
    return found;
}

int demo_model_part_tip(const demo_model *m, const acr_pose *pose, unsigned kinds, float scale, float out[3]) {
    if (!m->ok) return 0;
    float mat[ACR_SKEL_MAX_NODES][12];
    acr_pose_matrices(&m->skel, pose, mat);
    int found = 0;
    for (int i = 0; i < m->skel.count; ++i) {
        const acr_skel_node *node = &m->skel.node[i];
        if (node->draw_kind >= ACR_PART_KINDS || !(kinds & (1u << node->draw_kind))) continue;
        const demo_part_model *p = &m->part[node->draw_kind];
        if (!p->present || node->draw_piece >= p->pieces) continue;
        const acr_mesh *mesh = &p->piece[node->draw_piece];
        const float *M = mat[i];
        float d[3] = { 0, 0, 0 };     /* as demo_model_draw_pose */
        if (p->has_joint[node->draw_piece]) {
            const int *jt = p->joint[node->draw_piece];
            d[0] = (float)(node->pos[0] + jt[0]);
            d[1] = -(float)(node->pos[1] + jt[1]);
            d[2] = -(float)(node->pos[2] + jt[2]);
        }
        for (int v = 0; v < mesh->tri_count * 3; ++v) {
            const float vx = mesh->v[v].p[0] + d[0], vy = mesh->v[v].p[1] + d[1], vz = mesh->v[v].p[2] + d[2];
            const float lz = (M[8] * vx + M[9] * vy + M[10] * vz + M[11]) * scale;
            if (found && lz <= out[2]) continue;
            out[0] = -(M[0] * vx + M[1] * vy + M[2] * vz + M[3]) * scale;
            out[1] = (M[4] * vx + M[5] * vy + M[6] * vz + M[7]) * scale;
            out[2] = lz;
            found = 1;
        }
    }
    return found;
}

/* Each posed vertex of the parts in `kinds` (right, up, forward, world
 * units), as demo_model_part_tip; `node_of` gets the node's index. */
typedef void (*vertex_fn)(void *ctx, int node, const float p[3]);
static int each_vertex(const demo_model *m, const acr_pose *pose, unsigned kinds, float scale,
                       float mat[ACR_SKEL_MAX_NODES][12], vertex_fn fn, void *ctx) {
    if (!m->ok) return 0;
    acr_pose_matrices(&m->skel, pose, mat);
    int found = 0;
    for (int i = 0; i < m->skel.count; ++i) {
        const acr_skel_node *node = &m->skel.node[i];
        if (node->draw_kind >= ACR_PART_KINDS || !(kinds & (1u << node->draw_kind))) continue;
        const demo_part_model *p = &m->part[node->draw_kind];
        if (!p->present || node->draw_piece >= p->pieces || p->marker[node->draw_piece]) continue;
        const acr_mesh *mesh = &p->piece[node->draw_piece];
        const float *M = mat[i];
        float d[3] = { 0, 0, 0 };
        if (p->has_joint[node->draw_piece]) {
            const int *jt = p->joint[node->draw_piece];
            d[0] = (float)(node->pos[0] + jt[0]);
            d[1] = -(float)(node->pos[1] + jt[1]);
            d[2] = -(float)(node->pos[2] + jt[2]);
        }
        for (int v = 0; v < mesh->tri_count * 3; ++v) {
            const float vx = mesh->v[v].p[0] + d[0], vy = mesh->v[v].p[1] + d[1], vz = mesh->v[v].p[2] + d[2];
            const float q[3] = { -(M[0] * vx + M[1] * vy + M[2] * vz + M[3]) * scale,
                                 (M[4] * vx + M[5] * vy + M[6] * vz + M[7]) * scale,
                                 (M[8] * vx + M[9] * vy + M[10] * vz + M[11]) * scale };
            fn(ctx, i, q);
            found = 1;
        }
    }
    return found;
}

typedef struct { int side, n; float mn[3], mx[3]; } box_ctx;
static void box_vertex(void *c, int node, const float p[3]) {
    (void)node;
    box_ctx *b = c;
    if ((b->side < 0 && p[0] > 0) || (b->side > 0 && p[0] < 0)) return;
    for (int k = 0; k < 3; ++k) {
        if (!b->n || p[k] < b->mn[k]) b->mn[k] = p[k];
        if (!b->n || p[k] > b->mx[k]) b->mx[k] = p[k];
    }
    b->n++;
}

int demo_model_part_box(const demo_model *m, const acr_pose *pose, unsigned kinds, int side, float scale,
                        float mn[3], float mx[3]) {
    float mat[ACR_SKEL_MAX_NODES][12];
    box_ctx b = { side, 0, { 0 }, { 0 } };
    each_vertex(m, pose, kinds, scale, mat, box_vertex, &b);
    if (!b.n) return 0;
    memcpy(mn, b.mn, sizeof b.mn);
    memcpy(mx, b.mx, sizeof b.mx);
    return 1;
}

/* Booster nozzles: each drawn booster piece's back face. Pass 1 boxes each
 * piece (per node); pass 2 keeps the vertices near the back of their piece
 * and splits them where there is a gap across (twin pods in one piece). */
#define NZ_MAX_V 4096
typedef struct {
    int pass, nnode, node[8];
    float mn[8][3], mx[8][3];
    int cnt[8];
    int nv; float v[NZ_MAX_V][3]; int vn[NZ_MAX_V];
} nozzle_ctx;
static int nz_slot(nozzle_ctx *c, int node) {
    for (int i = 0; i < c->nnode; ++i) if (c->node[i] == node) return i;
    if (c->nnode >= 8) return -1;
    c->node[c->nnode] = node; c->cnt[c->nnode] = 0;
    return c->nnode++;
}
static void nozzle_vertex(void *ctx, int node, const float p[3]) {
    nozzle_ctx *c = ctx;
    const int k = nz_slot(c, node);
    if (k < 0) return;
    if (c->pass == 0) {
        for (int a = 0; a < 3; ++a) {
            if (!c->cnt[k] || p[a] < c->mn[k][a]) c->mn[k][a] = p[a];
            if (!c->cnt[k] || p[a] > c->mx[k][a]) c->mx[k][a] = p[a];
        }
        c->cnt[k]++;
        return;
    }
    const float depth = c->mx[k][2] - c->mn[k][2];
    const float band = depth * 0.3f > 3.0f ? depth * 0.3f : 3.0f;
    if (p[2] > c->mn[k][2] + band || c->nv >= NZ_MAX_V) return;
    memcpy(c->v[c->nv], p, sizeof c->v[0]);
    c->vn[c->nv++] = k;
}
static int cmp_x(const void *a, const void *b) {
    const float x = ((const float *)a)[0], y = ((const float *)b)[0];
    return x < y ? -1 : x > y;
}

int demo_model_part_nozzles(const demo_model *m, const acr_pose *pose, unsigned kinds, float scale,
                            float out[][3], int max) {
    static nozzle_ctx c;   /* main thread only */
    float mat[ACR_SKEL_MAX_NODES][12];
    SDL_zero(c);
    if (!each_vertex(m, pose, kinds, scale, mat, nozzle_vertex, &c)) return 0;
    c.pass = 1;
    c.nnode = 0;   /* nodes come back in the same order */
    each_vertex(m, pose, kinds, scale, mat, nozzle_vertex, &c);
    int n = 0;
    static float pts[NZ_MAX_V][3];
    for (int k = 0; k < c.nnode && n < max; ++k) {
        int np = 0;
        for (int i = 0; i < c.nv; ++i) if (c.vn[i] == k) memcpy(pts[np++], c.v[i], sizeof pts[0]);
        if (!np) continue;
        qsort(pts, (size_t)np, sizeof pts[0], cmp_x);
        const float width = pts[np - 1][0] - pts[0][0];
        const float gap_min = width * 0.15f > 6.0f ? width * 0.15f : 6.0f;
        int start = 0;
        for (int i = 1; i <= np && n < max; ++i) {
            if (i < np && pts[i][0] - pts[i - 1][0] < gap_min) continue;
            float mn[3] = { pts[start][0], pts[start][1], pts[start][2] }, mx[3] = { mn[0], mn[1], mn[2] };
            for (int j = start; j < i; ++j)
                for (int a = 0; a < 3; ++a) { if (pts[j][a] < mn[a]) mn[a] = pts[j][a]; if (pts[j][a] > mx[a]) mx[a] = pts[j][a]; }
            out[n][0] = (mn[0] + mx[0]) * 0.5f;
            out[n][1] = (mn[1] + mx[1]) * 0.5f;
            out[n][2] = mn[2];
            /* the same spot twice (pieces drawn over each other): keep one */
            int dup = 0;
            for (int q = 0; q < n; ++q)
                if (fabsf(out[q][0] - out[n][0]) < 4.0f && fabsf(out[q][1] - out[n][1]) < 4.0f) dup = 1;
            if (!dup) ++n;
            start = i;
        }
    }
    return n;
}

typedef struct { int side, n; float root[3], tip[3], best; } reach_ctx;
static void reach_vertex(void *c, int node, const float p[3]) {
    (void)node;
    reach_ctx *r = c;
    if ((r->side < 0 && p[0] > 0) || (r->side > 0 && p[0] < 0)) return;
    const float dx = p[0] - r->root[0], dy = p[1] - r->root[1], dz = p[2] - r->root[2];
    const float d = dx * dx + dy * dy + dz * dz;
    if (!r->n || d > r->best) { r->best = d; memcpy(r->tip, p, sizeof r->tip); }
    r->n++;
}

int demo_model_part_reach(const demo_model *m, const acr_pose *pose, unsigned kinds, int side, float scale,
                          float root[3], float tip[3]) {
    if (!m->ok) return 0;
    float mat[ACR_SKEL_MAX_NODES][12];
    acr_pose_matrices(&m->skel, pose, mat);
    /* The shoulder: the side's first node drawing these parts whose parent
     * draws something else (parents come before children). */
    int found = 0;
    reach_ctx r = { side, 0, { 0 }, { 0 }, 0 };
    for (int i = 0; i < m->skel.count && !found; ++i) {
        const acr_skel_node *node = &m->skel.node[i];
        if (node->draw_kind >= ACR_PART_KINDS || !(kinds & (1u << node->draw_kind))) continue;
        const int par = node->parent;
        if (par >= 0 && m->skel.node[par].draw_kind < ACR_PART_KINDS && (kinds & (1u << m->skel.node[par].draw_kind)))
            continue;
        const float *M = mat[i];
        const float q[3] = { -M[3] * scale, M[7] * scale, M[11] * scale };
        if ((side < 0 && q[0] > 0) || (side > 0 && q[0] < 0)) continue;
        memcpy(r.root, q, sizeof r.root);
        found = 1;
    }
    if (!found) return 0;
    each_vertex(m, pose, kinds, scale, mat, reach_vertex, &r);
    if (!r.n) return 0;
    memcpy(root, r.root, sizeof r.root);
    memcpy(tip, r.tip, sizeof r.tip);
    return 1;
}

void demo_model_draw_pose(const demo_model *m, const acr_pose *pose, float x, float y, float z,
                          float yaw, float scale, const acr_lighting *light) {
    if (!m->ok) return;
    float mat[ACR_SKEL_MAX_NODES][12];
    acr_pose_matrices(&m->skel, pose, mat);
    const float s = sinf(yaw), c = cosf(yaw);
    uint32_t group_tex[GROUPS] = { 0 };
    for (int k = 0; k < ACR_PART_KINDS; ++k) group_tex[k] = m->part[k].texture;
    int n = 0, wn = 0;
    for (int i = 0; i < m->skel.count; ++i) {
        const acr_skel_node *node = &m->skel.node[i];
        if (node->draw_kind >= ACR_PART_KINDS) continue;
        const demo_part_model *p = &m->part[node->draw_kind];
        if (!p->present || node->draw_piece >= p->pieces) continue;
        if (p->marker[node->draw_piece]) continue;   /* a launch point, not part of the look */
        const int wire = (g_wire_kinds >> node->draw_kind) & 1u;
        const acr_mesh *mesh = &p->piece[node->draw_piece];
        const float *M = mat[i];
        /* The pose matrices take off this set's bind position for the node,
         * but the piece's vertices were moved by its bind position in the set
         * it was exported from. They differ where the skeletons differ:
         * weapon-arm sets hang the hips (legs piece 0) under a root joint, so
         * parts came out in the wrong place. Put the difference back (model
         * space: y and z negated). From AC1Recomp's launcher (moa_ac_view.c). */
        float d[3] = { 0, 0, 0 };
        if (p->has_joint[node->draw_piece]) {
            const int *jt = p->joint[node->draw_piece];
            d[0] = (float)(node->pos[0] + jt[0]);
            d[1] = -(float)(node->pos[1] + jt[1]);
            d[2] = -(float)(node->pos[2] + jt[2]);
        }
        for (int t = 0; t < mesh->tri_count && n < MAX_MODEL_TRIS; ++t) {
            const acr_material *mt = &p->mtl.m[mesh->material[t]];
            int group = ACR_PART_KINDS;
            if (mt->textured) {
                const int kind = mt->kind < ACR_PART_KINDS ? mt->kind : node->draw_kind;
                group = group_tex[kind] ? kind : (group_tex[node->draw_kind] ? node->draw_kind : ACR_PART_KINDS);
            }
            const uint8_t base[3] = { group < ACR_PART_KINDS ? 128 : mt->r, group < ACR_PART_KINDS ? 128 : mt->g,
                                      group < ACR_PART_KINDS ? 128 : mt->b };
            /* On the PS1 a textured face blends only where the texel's
             * semi-transparency bit is set; the export does not keep that bit
             * and the colour ramps never set it, so textured faces stay solid. */
            const int semi = mt->semi && group == ACR_PART_KINDS;
            g_group[n] = (uint8_t)(group | (semi ? 0x80 : 0));
            for (int j = 0; j < 3; ++j) {
                acr_mesh_vertex sv = mesh->v[t * 3 + j];
                sv.p[0] += d[0]; sv.p[1] += d[1]; sv.p[2] += d[2];
                const acr_mesh_vertex *v = &sv;
                /* model space has the AC's right hand at -x; the engine's world
                 * has the right at +x (see the camera in main.c) */
                const float lx = -(M[0] * v->p[0] + M[1] * v->p[1] + M[2] * v->p[2] + M[3]) * scale;
                const float ly = (M[4] * v->p[0] + M[5] * v->p[1] + M[6] * v->p[2] + M[7]) * scale;
                const float lz = (M[8] * v->p[0] + M[9] * v->p[1] + M[10] * v->p[2] + M[11]) * scale;
                const float nx = -(M[0] * v->n[0] + M[1] * v->n[1] + M[2] * v->n[2]);
                const float ny = M[4] * v->n[0] + M[5] * v->n[1] + M[6] * v->n[2];
                const float nz = M[8] * v->n[0] + M[9] * v->n[1] + M[10] * v->n[2];
                uint8_t col[3];
                acr_light_color(light, (int32_t)((nx * c + nz * s) * 4096.0f), (int32_t)(ny * 4096.0f),
                                (int32_t)((-nx * s + nz * c) * 4096.0f), base[0], base[1], base[2], col);
                acr_vtx *o = &g_out[n * 3 + j];
                o->x = x + lx * c + lz * s;
                o->y = y + ly;
                o->z = z - lx * s + lz * c;
                o->r = col[0]; o->g = col[1]; o->b = col[2];
                o->a = semi ? 128 : 255;
                o->u = v->uv[0];
                o->v = 1.0f - v->uv[1];
            }
            if (wire) {   /* its three edges as lines, in the wire colour */
                for (int j = 0; j < 3 && wn + 2 <= MAX_WIRE_VERTS; ++j) {
                    acr_vtx e0 = g_out[n * 3 + j], e1 = g_out[n * 3 + (j + 1) % 3];
                    e0.r = e1.r = g_wire_rgba[0]; e0.g = e1.g = g_wire_rgba[1];
                    e0.b = e1.b = g_wire_rgba[2]; e0.a = e1.a = g_wire_rgba[3];
                    g_wire[wn++] = e0; g_wire[wn++] = e1;
                }
                continue;   /* not drawn solid */
            }
            ++n;
        }
    }
    /* Solid faces first, then the semi-transparent ones. */
    for (int semi = 0; semi < 2; ++semi)
        for (int g = 0; g < GROUPS; ++g) {
            int count = 0;
            for (int t = 0; t < n; ++t)
                if (g_group[t] == (uint8_t)(g | (semi ? 0x80 : 0))) {
                    memcpy(&g_batch[count * 3], &g_out[t * 3], 3 * sizeof(acr_vtx));
                    ++count;
                }
            if (count)
                acr_render_mesh(g_batch, count * 3, g < ACR_PART_KINDS ? group_tex[g] : 0,
                                ACR_MESH_PS1_TEXEL | (semi ? ACR_MESH_SEMI : 0));
        }
    if (wn) acr_render_lines(g_wire, wn);
}

/* ---- one garage model (OPTIONAL PARTS) ---- */

/* Boosters' garage models carry both pods; the menus show one (user,
 * 2026-11-27). Triangles sorted by their middle's x: where there is a clear
 * gap across, keep the +x side. A booster with no gap is left whole. */
static int cmp_float(const void *a, const void *b) {
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}
static void keep_one_pod(acr_mesh *m) {
    const int n = m->tri_count;
    if (n < 2) return;
    float *xs = SDL_malloc(sizeof(float) * (size_t)n);
    if (!xs) return;
    float lo = 1e9f, hi = -1e9f;
    for (int t = 0; t < n; ++t) {
        xs[t] = (m->v[3 * t].p[0] + m->v[3 * t + 1].p[0] + m->v[3 * t + 2].p[0]) / 3.0f;
        for (int j = 0; j < 3; ++j) { const float x = m->v[3 * t + j].p[0]; if (x < lo) lo = x; if (x > hi) hi = x; }
    }
    float *sorted = SDL_malloc(sizeof(float) * (size_t)n);
    if (!sorted) { SDL_free(xs); return; }
    memcpy(sorted, xs, sizeof(float) * (size_t)n);
    qsort(sorted, (size_t)n, sizeof(float), cmp_float);
    /* the widest gap between triangle middles, near the centre */
    float best = 0, cut = 0;
    const float mid = (lo + hi) * 0.5f, width = hi - lo;
    for (int i = 1; i < n; ++i) {
        const float g = sorted[i] - sorted[i - 1], at = (sorted[i] + sorted[i - 1]) * 0.5f;
        if (fabsf(at - mid) > width * 0.25f) continue;
        if (g > best) { best = g; cut = at; }
    }
    SDL_free(sorted);
    /* and nothing of either pod's own surface crosses the cut */
    int crossing = 0;
    for (int t = 0; t < n && !crossing; ++t) {
        float a = 1e9f, b = -1e9f;
        for (int j = 0; j < 3; ++j) { const float x = m->v[3 * t + j].p[0]; if (x < a) a = x; if (x > b) b = x; }
        if (a < cut - 0.5f && b > cut + 0.5f) crossing = 1;
    }
    if (best < width * 0.08f || crossing) { SDL_free(xs); return; }
    int k = 0;
    for (int t = 0; t < n; ++t) {
        if (xs[t] < cut) continue;
        if (k != t) { memcpy(&m->v[3 * k], &m->v[3 * t], sizeof(acr_mesh_vertex) * 3); m->material[k] = m->material[t]; }
        ++k;
    }
    if (k > 0) m->tri_count = k;
    SDL_free(xs);
}

int demo_object_load(const char *import_dir, const char *kind, int index, demo_object *o) {
    memset(o, 0, sizeof *o);
    char dir[1100], path[1200];
    SDL_snprintf(dir, sizeof dir, "%s/models/menu/%s/%02d", import_dir, kind, index);
    SDL_snprintf(path, sizeof path, "%s/part.mtl", dir);
    char *mtl = load_text(path);
    if (!mtl || !acr_mtl_parse(mtl, &o->mtl)) { SDL_free(mtl); return 0; }
    SDL_free(mtl);
    SDL_snprintf(path, sizeof path, "%s/object0.obj", dir);
    char *obj = load_text(path);
    const int ok = obj && acr_obj_parse(obj, &o->mtl, &o->mesh) && o->mesh.tri_count > 0;
    SDL_free(obj);
    if (!ok) { acr_mesh_free(&o->mesh); return 0; }
    if (!SDL_strcmp(kind, "booster")) keep_one_pod(&o->mesh);   /* centred on the pod kept, below */
    float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
    for (int i = 0; i < o->mesh.tri_count * 3; ++i)
        for (int c = 0; c < 3; ++c) {
            if (o->mesh.v[i].p[c] < mn[c]) mn[c] = o->mesh.v[i].p[c];
            if (o->mesh.v[i].p[c] > mx[c]) mx[c] = o->mesh.v[i].p[c];
        }
    float r = 0;
    for (int c = 0; c < 3; ++c) {
        o->center[c] = (mn[c] + mx[c]) * 0.5f;
        const float h = (mx[c] - mn[c]) * 0.5f;
        r += h * h;
    }
    o->radius = sqrtf(r) > 1.0f ? sqrtf(r) : 1.0f;
    SDL_snprintf(path, sizeof path, "%s/texture.png", dir);
    o->texture = load_texture(path, ACR_PART_KINDS, NULL);
    o->ok = 1;
    return 1;
}

void demo_object_free(demo_object *o) {
    acr_mesh_free(&o->mesh);
    memset(o, 0, sizeof *o);
}

void demo_object_draw(const demo_object *o, float size, float yaw, float pitch, const acr_lighting *light) {
    if (!o->ok) return;
    const float k = size * 0.5f / o->radius;
    const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    int n = 0;
    for (int t = 0; t < o->mesh.tri_count && n < MAX_MODEL_TRIS; ++t, ++n) {
        const acr_material *mt = &o->mtl.m[o->mesh.material[t]];
        const int tex = mt->textured && o->texture;
        for (int j = 0; j < 3; ++j) {
            const acr_mesh_vertex *v = &o->mesh.v[t * 3 + j];
            /* model space: y and z down / back, as the AC parts (see above) */
            const float px = -(v->p[0] - o->center[0]) * k, py = -(v->p[1] - o->center[1]) * k,
                        pz = -(v->p[2] - o->center[2]) * k;
            const float nx0 = -v->n[0], ny0 = -v->n[1], nz0 = -v->n[2];
            /* turn about y, then tip about x */
            const float x1 = px * cy + pz * sy, z1 = -px * sy + pz * cy;
            const float y2 = py * cp - z1 * sp, z2 = py * sp + z1 * cp;
            const float nx1 = nx0 * cy + nz0 * sy, nz1 = -nx0 * sy + nz0 * cy;
            const float ny2 = ny0 * cp - nz1 * sp, nz2 = ny0 * sp + nz1 * cp;
            uint8_t col[3];
            acr_light_color(light, (int32_t)(nx1 * 4096.0f), (int32_t)(ny2 * 4096.0f), (int32_t)(nz2 * 4096.0f),
                            tex ? 128 : mt->r, tex ? 128 : mt->g, tex ? 128 : mt->b, col);
            acr_vtx *out = &g_batch[n * 3 + j];
            out->x = x1; out->y = y2; out->z = z2;
            out->r = col[0]; out->g = col[1]; out->b = col[2]; out->a = 255;
            out->u = v->uv[0];
            out->v = 1.0f - v->uv[1];
        }
    }
    if (n) acr_render_mesh(g_batch, n * 3, o->texture, ACR_MESH_PS1_TEXEL);
}
