/* Mission units on a mission map. Formats: tools/acr_import/acr_units_moa.h;
 * notes: docs/research/gen1-stages.md, "Mission units". */
#include "demo_units.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "acr_png.h"
#include "acr_render.h"

static char *load_text(const char *path) { return SDL_LoadFile(path, NULL); }

static uint32_t load_texture(const char *path) {
    size_t size = 0;
    uint8_t *data = SDL_LoadFile(path, &size);
    if (!data) return 0;
    acr_image img;
    const int ok = acr_png_decode(data, size, &img);
    SDL_free(data);
    if (!ok) return 0;
    uint32_t tex = 0;
    uint8_t *rgba = SDL_malloc((size_t)img.w * img.h * 4);
    if (rgba) {
        if (img.indexed) for (int i = 0; i < img.w * img.h; ++i) SDL_memcpy(rgba + 4 * i, img.palette[img.pixels[i]], 4);
        else SDL_memcpy(rgba, img.pixels, (size_t)img.w * img.h * 4);
        tex = acr_render_texture(rgba, img.w, img.h, 1);
        SDL_free(rgba);
    }
    acr_image_free(&img);
    return tex;
}

/* ---- objects.toml: a few keys of each [[object]] ---- */

static const char *line_end(const char *p) { while (*p && *p != '\n') ++p; return p; }

/* `key = ...` at the line's start -> the value, or NULL. */
static const char *value(const char *s, const char *e, const char *key) {
    const size_t n = SDL_strlen(key);
    if ((size_t)(e - s) <= n || SDL_memcmp(s, key, n) != 0 || (s[n] != ' ' && s[n] != '=')) return NULL;
    s += n;
    while (s < e && (*s == ' ' || *s == '=')) ++s;
    return s;
}

static void quoted(const char *v, const char *e, char *out, size_t size) {
    out[0] = 0;
    if (!v || v >= e || *v != '"') return;
    const char *a = v + 1, *b = a;
    while (b < e && *b != '"') ++b;
    size_t n = (size_t)(b - a);
    if (n >= size) n = size - 1;
    SDL_memcpy(out, a, n);
    out[n] = 0;
}

/* ---- one unit ---- */

static void free_model(demo_unit_model *m) {
    for (int k = 0; k < m->pieces; ++k) acr_mesh_free(&m->piece[k]);
    acr_anim_set_free(&m->anims);
    SDL_memset(m, 0, sizeof *m);
}

static int load_model(const char *import_dir, const char *id, const char *skin, demo_unit_model *m) {
    SDL_memset(m, 0, sizeof *m);
    SDL_snprintf(m->id, sizeof m->id, "%s", id);
    SDL_snprintf(m->skin, sizeof m->skin, "%s", skin[0] ? skin : "unit.mtl");
    char dir[1100], path[1300];
    SDL_snprintf(dir, sizeof dir, "%s/units/%s", import_dir, id);
    SDL_snprintf(path, sizeof path, "%s/skeleton.toml", dir);
    char *t = load_text(path);
    const int ok = t && acr_skeleton_parse(t, &m->skel);
    SDL_free(t);
    if (!ok) return 0;
    SDL_snprintf(path, sizeof path, "%s/anims.txt", dir);
    t = load_text(path);
    if (t) acr_anim_set_parse(t, &m->skel, &m->anims);   /* without it: the bind pose */
    SDL_free(t);
    SDL_snprintf(path, sizeof path, "%s/%s", dir, m->skin);
    t = load_text(path);
    const int mok = t && acr_mtl_parse(t, &m->mtl);
    SDL_free(t);
    if (!mok) { acr_anim_set_free(&m->anims); return 0; }
    for (int i = 0; i < m->mtl.count; ++i) {
        if (!m->mtl.m[i].textured) continue;
        SDL_snprintf(path, sizeof path, "%s/%s", dir, m->mtl.m[i].texture);
        m->texture[i] = load_texture(path);
    }
    for (int k = 0; k < DEMO_UNIT_PIECES; ++k) {
        SDL_snprintf(path, sizeof path, "%s/piece%d.obj", dir, k);
        t = load_text(path);
        if (!t) break;
        if (!acr_obj_parse(t, &m->mtl, &m->piece[k])) SDL_memset(&m->piece[k], 0, sizeof m->piece[k]);
        SDL_free(t);
        m->pieces = k + 1;
    }
    return m->pieces > 0;
}

void demo_units_free(demo_units *u) {
    for (int i = 0; i < u->models; ++i) free_model(&u->model[i]);
    SDL_memset(u, 0, sizeof *u);
    u->map = -1;
}

int demo_units_load(const char *import_dir, int map, demo_units *u) {
    SDL_memset(u, 0, sizeof *u);
    u->map = map;
    char path[1100];
    SDL_snprintf(path, sizeof path, "%s/stages/%02d/objects.toml", import_dir, map);
    char *text = load_text(path);
    if (!text) return 0;
    demo_unit_object *o = NULL;
    char id[16] = "", skin[32] = "";
    for (const char *p = text;; ) {
        const char *e = line_end(p), *v;
        const int header = *p == '[';
        if ((header || !*p) && o) {               /* the object just read: find or load its unit */
            o->model = -1;
            for (int i = 0; id[0] && i < u->models && o->model < 0; ++i)
                if (!SDL_strcmp(u->model[i].id, id) && !SDL_strcmp(u->model[i].skin, skin[0] ? skin : "unit.mtl"))
                    o->model = i;
            if (id[0] && o->model < 0 && u->models < DEMO_UNIT_MODELS) {
                if (load_model(import_dir, id, skin, &u->model[u->models])) o->model = u->models++;
                else SDL_Log("mission map %02d: unit %s did not load", map, id);
            }
            if (o->model >= 0) ++u->drawn;
            o = NULL;
        }
        if (!*p) break;
        if (header && !SDL_strncmp(p, "[[object]]", 10) && u->objects < DEMO_UNIT_OBJECTS) {
            o = &u->object[u->objects++];
            SDL_memset(o, 0, sizeof *o);
            id[0] = skin[0] = 0;
        } else if (o) {
            if ((v = value(p, e, "record"))) o->record = SDL_atoi(v);
            else if ((v = value(p, e, "handler"))) o->handler = SDL_atoi(v);
            else if ((v = value(p, e, "type"))) o->type = SDL_atoi(v);
            else if ((v = value(p, e, "facing"))) o->yaw = (float)(SDL_atoi(v) & 4095) * (6.2831853f / 4096.0f);
            else if ((v = value(p, e, "unit"))) quoted(v, e, id, sizeof id);
            else if ((v = value(p, e, "skin"))) quoted(v, e, skin, sizeof skin);
            else if ((v = value(p, e, "position")) && *v == '[') {
                char *q = (char *)v + 1;
                long c[3];
                for (int k = 0; k < 3; ++k) {
                    c[k] = SDL_strtol(q, &q, 10);
                    while (*q == ',' || *q == ' ') ++q;
                }
                /* engine x, y down, z -> drawn world (y up) */
                o->pos[0] = (float)c[0];
                o->pos[1] = -(float)c[1];
                o->pos[2] = (float)c[2];
            }
        }
        p = *e ? e + 1 : e;
    }
    SDL_free(text);
    SDL_Log("mission map %02d: %d objects, %d drawn with %d units", map, u->objects, u->drawn, u->models);
    return u->drawn;
}

/* ---- drawing ---- */

#define UNIT_SCALE 0.25f     /* 4 model units a world unit, as ACs (ACR_GEN1_MODEL_SCALE) and stages */
#define MAX_UNIT_TRIS 4096
static acr_vtx g_out[MAX_UNIT_TRIS * 3], g_batch[MAX_UNIT_TRIS * 3];
static uint8_t g_mat[MAX_UNIT_TRIS];

/* Animation 0 looping: the frames' holds, then the last one eases back into the first. */
static float loop_tick(const acr_anim_set *set, float tick) {
    const acr_anim *a = &set->anim[0];
    if (a->frames < 2) return 0.0f;
    unsigned last = set->frame[a->first_frame + a->frames - 1].hold;
    const float period = (float)a->ticks + (float)(last ? last : 1);
    return period > 0 ? fmodf(tick, period) : 0.0f;
}

static void draw_object(const demo_unit_model *m, const demo_unit_object *o, float tick, const acr_lighting *light) {
    acr_pose pose;
    acr_pose_play_smooth(&m->skel, &m->anims, 0, loop_tick(&m->anims, tick), 1, &pose);
    float mat[ACR_SKEL_MAX_NODES][12];
    acr_pose_matrices(&m->skel, &pose, mat);
    const float s = sinf(o->yaw), c = cosf(o->yaw);
    int n = 0;
    for (int i = 0; i < m->skel.count; ++i) {
        const acr_skel_node *node = &m->skel.node[i];
        if (node->draw_kind != ACR_PART_UNIT || node->draw_piece >= m->pieces) continue;
        const acr_mesh *mesh = &m->piece[node->draw_piece];
        const float *M = mat[i];
        for (int t = 0; t < mesh->tri_count && n < MAX_UNIT_TRIS; ++t) {
            const int mi = mesh->material[t];
            const acr_material *mt = &m->mtl.m[mi];
            const int textured = mt->textured && m->texture[mi];
            const uint8_t base[3] = { textured ? 128 : mt->r, textured ? 128 : mt->g, textured ? 128 : mt->b };
            g_mat[n] = (uint8_t)mi;
            for (int j = 0; j < 3; ++j) {
                const acr_mesh_vertex *v = &mesh->v[t * 3 + j];
                /* as demo_model_draw_pose: model space has the right hand at -x */
                const float lx = -(M[0] * v->p[0] + M[1] * v->p[1] + M[2] * v->p[2] + M[3]) * UNIT_SCALE;
                const float ly = (M[4] * v->p[0] + M[5] * v->p[1] + M[6] * v->p[2] + M[7]) * UNIT_SCALE;
                const float lz = (M[8] * v->p[0] + M[9] * v->p[1] + M[10] * v->p[2] + M[11]) * UNIT_SCALE;
                const float nx = -(M[0] * v->n[0] + M[1] * v->n[1] + M[2] * v->n[2]);
                const float ny = M[4] * v->n[0] + M[5] * v->n[1] + M[6] * v->n[2];
                const float nz = M[8] * v->n[0] + M[9] * v->n[1] + M[10] * v->n[2];
                uint8_t col[3];
                acr_light_color(light, (int32_t)((nx * c + nz * s) * 4096.0f), (int32_t)(ny * 4096.0f),
                                (int32_t)((-nx * s + nz * c) * 4096.0f), base[0], base[1], base[2], col);
                acr_vtx *out = &g_out[n * 3 + j];
                out->x = o->pos[0] + lx * c + lz * s;
                out->y = o->pos[1] + ly;
                out->z = o->pos[2] - lx * s + lz * c;
                out->r = col[0]; out->g = col[1]; out->b = col[2];
                out->a = mt->semi ? 128 : 255;
                out->u = v->uv[0];
                out->v = 1.0f - v->uv[1];
            }
            ++n;
        }
    }
    /* A batch per material: solid ones first, then the see-through ones. */
    for (int semi = 0; semi < 2; ++semi)
        for (int mi = 0; mi < m->mtl.count; ++mi) {
            if (m->mtl.m[mi].semi != semi) continue;
            int count = 0;
            for (int t = 0; t < n; ++t)
                if (g_mat[t] == mi) SDL_memcpy(&g_batch[3 * count++], &g_out[t * 3], 3 * sizeof(acr_vtx));
            if (count)
                acr_render_mesh(g_batch, count * 3, m->mtl.m[mi].textured ? m->texture[mi] : 0,
                                ACR_MESH_PS1_TEXEL | (semi ? ACR_MESH_SEMI : 0));
        }
}

void demo_units_draw(const demo_units *u, float tick, const acr_lighting *light) {
    for (int i = 0; i < u->objects; ++i) {
        const demo_unit_object *o = &u->object[i];
        if (o->model >= 0) draw_object(&u->model[o->model], o, tick, light);
    }
}
