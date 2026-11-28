#include "gen2_ac.h"

#include <SDL3/SDL.h>
#include <math.h>

#define CGLTF_IMPLEMENTATION
#include "../../third_party/cgltf/cgltf.h"

#include "acr_render.h"
#include "acr_gen2_move.h"
#include "../../model/acr_png.h"

/* glb units -> Gen 1 model space (tools/acr_import/acr_models_ac2.h ACR_AC2_SCALE) */
#define GEN2_UNIT 83.5f
#define GEN2_MAX_VERTS (8192 * 3)
#define GEN2_MAX_TEX 32

typedef struct gen2_prim {
    int node, verts, tex;          /* tex: index into tex[] or -1 */
    uint8_t r, g, b;
    float *pos, *nrm, *uv;         /* node-local, verts x 3/3/2 */
} gen2_prim;

typedef struct gen2_track {       /* one animation channel, resolved */
    int node, path;                /* path: 1 translation, 2 rotation */
    int keys;
    const float *t, *v;            /* times, values (3 or 4 per key) */
} gen2_track;

typedef struct gen2_clip {
    char name[48];
    float length;
    int loop, first, count;        /* tracks[first .. first + count) */
} gen2_clip;

struct gen2_ac {
    cgltf_data *data;
    int nprim, nclip, ntrack, nnodes;
    gen2_prim *prim;
    gen2_clip *clip;
    gen2_track *track;
    float *rest;                   /* per node: t[3], q[4] */
    float *world;                  /* per node: 4x4 column-major */
    float *keybuf;                 /* all key data, unpacked */
    uint32_t tex[GEN2_MAX_TEX];
    int ntex;
    /* playback */
    int cur, forced;
    float time;
    Uint64 last_ns;
    float lx, ly, lz, air_s;
    int have_last;
    int idle, walk, jump;
    int driven;                    /* gen2_ac_follow picks the clip */
};

static acr_vtx g_out[GEN2_MAX_VERTS];

static uint32_t png_texture(const uint8_t *data, size_t size) {
    acr_image img;
    if (!acr_png_decode(data, size, &img)) return 0;
    uint32_t tex = 0;
    uint8_t *rgba = SDL_malloc((size_t)img.w * img.h * 4);
    if (rgba) {
        for (int i = 0; i < img.w * img.h; ++i)
            SDL_memcpy(rgba + 4 * i, img.indexed ? img.palette[img.pixels[i]] : img.pixels + 4 * i, 4);
        tex = acr_render_texture(rgba, img.w, img.h, 0);
        SDL_free(rgba);
    }
    acr_image_free(&img);
    return tex;
}

static int find_clip(const gen2_ac *g, const char *prefix) {
    const size_t n = SDL_strlen(prefix);
    for (int i = 0; i < g->nclip; ++i)
        if (!SDL_strncmp(g->clip[i].name, prefix, n)) return i;
    return -1;
}

static float *read_floats(cgltf_accessor *a, int comps, float **at) {
    float *o = *at;
    for (cgltf_size i = 0; i < a->count; ++i) cgltf_accessor_read_float(a, i, o + i * comps, (cgltf_size)comps);
    *at += a->count * comps;
    return o;
}

gen2_ac *gen2_ac_load(const char *import_dir) {
    static gen2_ac *cached;
    static int tried;
    if (cached || tried) return cached;
    tried = 1;
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/gen2/ac2/test-ac/ac.glb", import_dir);
    cgltf_options opt;
    SDL_memset(&opt, 0, sizeof opt);
    cgltf_data *d = NULL;
    if (cgltf_parse_file(&opt, path, &d) != cgltf_result_success || cgltf_load_buffers(&opt, d, path) != cgltf_result_success) {
        SDL_Log("gen 2: no test AC at %s (re-run acr-import on an Armored Core 2 disc)", path);
        if (d) cgltf_free(d);
        return NULL;
    }
    gen2_ac *g = SDL_calloc(1, sizeof *g);
    g->data = d;
    g->nnodes = (int)d->nodes_count;
    g->rest = SDL_calloc((size_t)g->nnodes, 7 * sizeof(float));
    g->world = SDL_calloc((size_t)g->nnodes, 16 * sizeof(float));
    for (int i = 0; i < g->nnodes; ++i) {
        cgltf_node *n = &d->nodes[i];
        SDL_memcpy(g->rest + 7 * i, n->translation, 12);
        if (n->has_rotation) SDL_memcpy(g->rest + 7 * i + 3, n->rotation, 16);
        else { g->rest[7 * i + 6] = 1; }
        n->has_translation = n->has_rotation = 1;
        SDL_memcpy(n->rotation, g->rest + 7 * i + 3, 16);
    }
    /* textures: embedded PNGs */
    g->ntex = (int)(d->images_count < GEN2_MAX_TEX ? d->images_count : GEN2_MAX_TEX);
    for (int i = 0; i < g->ntex; ++i) {
        cgltf_buffer_view *bv = d->images[i].buffer_view;
        if (bv) g->tex[i] = png_texture((const uint8_t *)bv->buffer->data + bv->offset, bv->size);
    }
    /* how many floats we need for all vertex and key data */
    size_t floats = 0;
    int nprim = 0;
    for (cgltf_size i = 0; i < d->nodes_count; ++i)
        if (d->nodes[i].mesh)
            for (cgltf_size p = 0; p < d->nodes[i].mesh->primitives_count; ++p) {
                const cgltf_primitive *pr = &d->nodes[i].mesh->primitives[p];
                for (cgltf_size a = 0; a < pr->attributes_count; ++a) floats += pr->attributes[a].data->count * 3;
                ++nprim;
            }
    int ntrack = 0;
    for (cgltf_size i = 0; i < d->animations_count; ++i) {
        for (cgltf_size c = 0; c < d->animations[i].channels_count; ++c) {
            const cgltf_animation_sampler *s = d->animations[i].channels[c].sampler;
            floats += s->input->count * 5;
            ++ntrack;
        }
    }
    g->keybuf = SDL_malloc(floats * sizeof(float) + 16);
    g->prim = SDL_calloc((size_t)(nprim ? nprim : 1), sizeof *g->prim);
    g->track = SDL_calloc((size_t)(ntrack ? ntrack : 1), sizeof *g->track);
    g->clip = SDL_calloc(d->animations_count ? d->animations_count : 1, sizeof *g->clip);
    float *at = g->keybuf;
    int verts = 0;
    for (cgltf_size i = 0; i < d->nodes_count; ++i) {
        const cgltf_mesh *m = d->nodes[i].mesh;
        if (!m) continue;
        for (cgltf_size p = 0; p < m->primitives_count; ++p) {
            const cgltf_primitive *pr = &m->primitives[p];
            gen2_prim *P = &g->prim[g->nprim];
            P->node = (int)i;
            P->tex = -1;
            P->r = P->g = P->b = 160;
            if (pr->material) {
                const float *c = pr->material->pbr_metallic_roughness.base_color_factor;
                P->r = (uint8_t)(c[0] * 255); P->g = (uint8_t)(c[1] * 255); P->b = (uint8_t)(c[2] * 255);
                const cgltf_texture *t = pr->material->pbr_metallic_roughness.base_color_texture.texture;
                if (t && t->image) {
                    const int im = (int)(t->image - d->images);
                    if (im < g->ntex && g->tex[im]) P->tex = im;
                }
            }
            for (cgltf_size a = 0; a < pr->attributes_count; ++a) {
                cgltf_attribute *A = &pr->attributes[a];
                if (A->type == cgltf_attribute_type_position) { P->pos = read_floats(A->data, 3, &at); P->verts = (int)A->data->count; }
                else if (A->type == cgltf_attribute_type_normal) P->nrm = read_floats(A->data, 3, &at);
                else if (A->type == cgltf_attribute_type_texcoord && A->index == 0) P->uv = read_floats(A->data, 2, &at);
            }
            /* acr-import writes plain triangle lists (no indices) */
            if (!P->pos || pr->indices || verts + P->verts > GEN2_MAX_VERTS) continue;
            verts += P->verts;
            ++g->nprim;
        }
    }
    for (cgltf_size i = 0; i < d->animations_count; ++i) {
        const cgltf_animation *an = &d->animations[i];
        gen2_clip *C = &g->clip[g->nclip++];
        SDL_strlcpy(C->name, an->name ? an->name : "", sizeof C->name);
        C->loop = SDL_strstr(C->name, "loop") != NULL;
        C->first = g->ntrack;
        for (cgltf_size c = 0; c < an->channels_count; ++c) {
            const cgltf_animation_channel *ch = &an->channels[c];
            const int path = ch->target_path == cgltf_animation_path_type_translation ? 1
                           : ch->target_path == cgltf_animation_path_type_rotation ? 2 : 0;
            if (!path || !ch->target_node) continue;
            gen2_track *T = &g->track[g->ntrack++];
            T->node = (int)(ch->target_node - d->nodes);
            T->path = path;
            T->keys = (int)ch->sampler->input->count;
            T->t = read_floats(ch->sampler->input, 1, &at);
            T->v = read_floats(ch->sampler->output, path == 1 ? 3 : 4, &at);
            if (T->keys && T->t[T->keys - 1] > C->length) C->length = T->t[T->keys - 1];
        }
        C->count = g->ntrack - C->first;
    }
    g->idle = find_clip(g, "47_");
    g->walk = find_clip(g, "02_");
    g->jump = find_clip(g, "37_");
    g->cur = g->idle;
    SDL_Log("gen 2: test AC loaded from ac.glb: %d primitives, %d textures, %d clips", g->nprim, g->ntex, g->nclip);
    cached = g;
    return g;
}

void gen2_ac_free(gen2_ac *g) {
    if (!g) return;
    cgltf_free(g->data);
    SDL_free(g->prim); SDL_free(g->track); SDL_free(g->clip);
    SDL_free(g->rest); SDL_free(g->world); SDL_free(g->keybuf);
    SDL_free(g);
}

int gen2_ac_force_clip(gen2_ac *g, const char *prefix) {
    if (!g) return 0;
    if (!prefix) { g->forced = 0; return 1; }
    const int c = find_clip(g, prefix);
    if (c < 0) return 0;
    g->forced = 1; g->cur = c; g->time = 0;
    return 1;
}

void gen2_ac_cycle_clip(gen2_ac *g) {
    if (!g || !g->nclip) return;
    if (!g->forced) { g->forced = 1; g->cur = 0; }
    else if (g->cur + 1 < g->nclip) ++g->cur;
    else { g->forced = 0; g->cur = g->idle; }
    g->time = 0;
}

const char *gen2_ac_clip_name(const gen2_ac *g) {
    return g && g->cur >= 0 && g->cur < g->nclip ? g->clip[g->cur].name : "";
}

int gen2_ac_forced(const gen2_ac *g) { return g && g->forced; }

static void sample(const gen2_track *T, float t, float *out) {
    const int w = T->path == 1 ? 3 : 4;
    int k = 0;
    while (k + 1 < T->keys && T->t[k + 1] <= t) ++k;
    if (k + 1 >= T->keys || t <= T->t[0]) { SDL_memcpy(out, T->v + w * (t <= T->t[0] ? 0 : k), w * sizeof(float)); return; }
    const float span = T->t[k + 1] - T->t[k], f = span > 0 ? (t - T->t[k]) / span : 0;
    const float *a = T->v + w * k, *b = a + w;
    float sgn = 1;
    if (w == 4 && a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0) sgn = -1;
    float len = 0;
    for (int i = 0; i < w; ++i) { out[i] = a[i] + (sgn * b[i] - a[i]) * f; len += out[i] * out[i]; }
    if (w == 4 && len > 0) { len = 1.0f / sqrtf(len); for (int i = 0; i < 4; ++i) out[i] *= len; }
}

/* Which clip from how the AC moved since last frame (units: AC heights). */
static void pick_clip(gen2_ac *g, float x, float y, float z, float scale, float dt) {
    if (g->forced || g->driven) return;
    const float height = 9.5f * GEN2_UNIT * scale;
    int want = g->idle;
    if (g->have_last && dt > 0) {
        const float vx = (x - g->lx) / dt, vy = (y - g->ly) / dt, vz = (z - g->lz) / dt;
        const float hs = sqrtf(vx * vx + vz * vz) / height;
        g->air_s = fabsf(vy) / height > 0.15f ? g->air_s + dt : 0;
        if (g->air_s > 0.1f && g->jump >= 0) want = g->jump;
        else if (hs > 0.3f && g->walk >= 0) want = g->walk;
    }
    g->lx = x; g->ly = y; g->lz = z; g->have_last = 1;
    if (want >= 0 && want != g->cur) { g->cur = want; g->time = 0; }
}

static void pose(gen2_ac *g) {
    cgltf_data *d = g->data;
    for (int i = 0; i < g->nnodes; ++i) {
        SDL_memcpy(d->nodes[i].translation, g->rest + 7 * i, 12);
        SDL_memcpy(d->nodes[i].rotation, g->rest + 7 * i + 3, 16);
    }
    if (g->cur >= 0 && g->cur < g->nclip) {
        const gen2_clip *C = &g->clip[g->cur];
        for (int i = 0; i < C->count; ++i) {
            const gen2_track *T = &g->track[C->first + i];
            sample(T, g->time, T->path == 1 ? d->nodes[T->node].translation : d->nodes[T->node].rotation);
        }
    }
    for (int i = 0; i < g->nnodes; ++i) cgltf_node_transform_world(&d->nodes[i], g->world + 16 * i);
}

void gen2_ac_draw(gen2_ac *g, float x, float y, float z, float yaw, float scale, int flash,
                  const acr_lighting *light) {
    if (!g) return;
    const Uint64 now = SDL_GetTicksNS();
    float dt = g->last_ns ? (float)(now - g->last_ns) * 1e-9f : 0;
    if (dt > 0.1f) dt = 0.1f;
    g->last_ns = now;
    pick_clip(g, x, y, z, scale, dt);
    if (g->cur >= 0) {
        const gen2_clip *C = &g->clip[g->cur];
        g->time += dt;
        if (C->length > 0) {
            if (C->loop) g->time = fmodf(g->time, C->length);
            else if (g->time > C->length) g->time = C->length;
        }
    }
    pose(g);
    const float s = sinf(yaw), c = cosf(yaw), k = GEN2_UNIT * scale;
    for (int p = 0; p < g->nprim; ++p) {
        const gen2_prim *P = &g->prim[p];
        const float *M = g->world + 16 * P->node;
        for (int v = 0; v < P->verts; ++v) {
            const float *lp = P->pos + 3 * v;
            float wp[3], wn[3] = { 0, 1, 0 };
            for (int r = 0; r < 3; ++r) wp[r] = M[r] * lp[0] + M[4 + r] * lp[1] + M[8 + r] * lp[2] + M[12 + r];
            if (P->nrm) {
                const float *ln = P->nrm + 3 * v;
                for (int r = 0; r < 3; ++r) wn[r] = M[r] * ln[0] + M[4 + r] * ln[1] + M[8 + r] * ln[2];
            }
            /* model space has the right hand at -x, the world at +x (as demo_model_draw_pose) */
            const float lx = -wp[0] * k, ly = wp[1] * k, lz = wp[2] * k;
            const float nx = -wn[0], ny = wn[1], nz = wn[2];
            uint8_t col[3];
            acr_light_color(light, (int32_t)((nx * c + nz * s) * 4096.0f), (int32_t)(ny * 4096.0f),
                            (int32_t)((-nx * s + nz * c) * 4096.0f), P->r, P->g, P->b, col);
            if (flash) for (int q = 0; q < 3; ++q) col[q] = (uint8_t)(col[q] + (255 - col[q]) / 2);
            acr_vtx *o = &g_out[v];
            o->x = x + lx * c + lz * s;
            o->y = y + ly;
            o->z = z - lx * s + lz * c;
            o->r = col[0]; o->g = col[1]; o->b = col[2]; o->a = 255;
            o->u = P->uv ? P->uv[2 * v] : 0; o->v = P->uv ? P->uv[2 * v + 1] : 0;
        }
        /* PS2 modulate: vertex colour 128 = the texel as is, so x2 as the PS1 path */
        const uint32_t tex = P->tex >= 0 ? g->tex[P->tex] : 0;
        acr_render_mesh(g_out, P->verts, tex, tex ? ACR_MESH_PS1_TEXEL : 0);
    }
}

static int clip_of_motion(const gen2_ac *g, int m) {
    char p[8];
    SDL_snprintf(p, sizeof p, "%02d_", m);
    return find_clip(g, p);
}

/* AC2's motion number for a movement state (gen2-movement.md; the boost
 * clips by direction as acr_models_ac2.c names them). */
static int motion_of_state(const acr_gen2_ac *ac) {
    static const int8_t boost[8] = { 17, 19, 20, 21, 22, 18, 23, 24 };
    const int d = ac->move_dir < 8 ? ac->move_dir : 0;
    switch (ac->state) {
    case ACR_GEN2_ST_WALK_START: return 1 + 2 * d;
    case ACR_GEN2_ST_WALK:       return 2 + 2 * d;
    case ACR_GEN2_ST_TURN:       return ac->yaw_speed >= 0 ? 33 : 34;
    case ACR_GEN2_ST_DASH:       return boost[d];
    case ACR_GEN2_ST_BRAKE:      return 25 + d;
    case ACR_GEN2_ST_CROUCH:     return 35;
    case ACR_GEN2_ST_AIR:        return 37;
    case ACR_GEN2_ST_LAND:       return 38;
    case ACR_GEN2_ST_HARD_LAND:  return 51;
    case ACR_GEN2_ST_OVERBOOST:  return d == ACR_GEN2_DIR_L ? 64 : d == ACR_GEN2_DIR_R ? 65 : 63;
    default:                     return 47;
    }
}

void gen2_ac_follow(gen2_ac *g, const acr_gen2_ac *ac) {
    if (!g) return;
    g->driven = ac != NULL;
    if (!ac || g->forced) return;
    const int c = clip_of_motion(g, motion_of_state(ac));
    if (c >= 0 && c != g->cur) { g->cur = c; g->time = 0; }
}

static int clip_frames(const gen2_ac *g, int m) {
    const int c = clip_of_motion(g, m);
    return c < 0 ? 0 : (int)lroundf(g->clip[c].length * 60.0f);
}

int gen2_ac_gait(const gen2_ac *g, acr_gen2_gait *out) {
    if (!g) return 0;
    acr_gen2_gait t = *out;
    for (int d = 0; d < 8; ++d) {
        const int loop = clip_frames(g, 2 + 2 * d), start = clip_frames(g, 1 + 2 * d);
        if (loop <= 0) return 0;
        t.walk_speed[d] = (d >= 5 ? 15.0f : 30.0f) / (float)loop;
        if (start > 0) { t.walk_start_frames[d] = (uint8_t)start; t.walk_start_dist[d] = d >= 5 ? 1.0f : 1.5f; }
    }
    int f;
    if ((f = clip_frames(g, 35)) > 0) t.crouch_frames = (uint8_t)f;
    if ((f = clip_frames(g, 38)) > 0) t.land_frames = (uint8_t)f;
    if ((f = clip_frames(g, 51)) > 0) t.hard_land_frames = (uint8_t)f;
    *out = t;
    return 1;
}
