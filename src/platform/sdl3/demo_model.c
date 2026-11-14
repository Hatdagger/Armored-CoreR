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
    case ACR_PART_HAND: return parts[SLOT_HAND] == 255 ? 0 : parts[SLOT_HAND];   /* 0: the empty hand */
    case ACR_PART_BACK_LEFT: return parts[SLOT_BACK_L] == 255 ? -1 : parts[SLOT_BACK_L];
    case ACR_PART_BACK_RIGHT: return parts[SLOT_BACK_R] == 255 ? -1 : parts[SLOT_BACK_R];
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
        if (obj) acr_obj_parse(obj, &p->mtl, &p->piece[k]);
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

int demo_model_load(const char *import_dir, int set, const int parts[SLOT_COUNT],
                    const uint8_t colours[4][3], demo_model *m) {
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
        if (index >= 0) load_part(models, kind, index, colours, &m->part[kind]);
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

void demo_model_draw(const demo_model *m, int anim, unsigned tick, float x, float y, float z,
                     float yaw, const acr_lighting *light) {
    if (!m->ok) return;
    acr_pose pose;
    float mat[ACR_SKEL_MAX_NODES][12];
    acr_pose_play(&m->skel, &m->anims, anim, tick, &pose);
    acr_pose_matrices(&m->skel, &pose, mat);
    const float s = sinf(yaw), c = cosf(yaw);
    uint32_t group_tex[GROUPS] = { 0 };
    for (int k = 0; k < ACR_PART_KINDS; ++k) group_tex[k] = m->part[k].texture;
    int n = 0;
    for (int i = 0; i < m->skel.count; ++i) {
        const acr_skel_node *node = &m->skel.node[i];
        if (node->draw_kind >= ACR_PART_KINDS) continue;
        const demo_part_model *p = &m->part[node->draw_kind];
        if (!p->present || node->draw_piece >= p->pieces) continue;
        const acr_mesh *mesh = &p->piece[node->draw_piece];
        const float *M = mat[i];
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
                const acr_mesh_vertex *v = &mesh->v[t * 3 + j];
                /* model space has the AC's right hand at -x; the engine's world
                 * has the right at +x (see the camera in main.c) */
                const float lx = -(M[0] * v->p[0] + M[1] * v->p[1] + M[2] * v->p[2] + M[3]);
                const float ly = M[4] * v->p[0] + M[5] * v->p[1] + M[6] * v->p[2] + M[7];
                const float lz = M[8] * v->p[0] + M[9] * v->p[1] + M[10] * v->p[2] + M[11];
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
}
