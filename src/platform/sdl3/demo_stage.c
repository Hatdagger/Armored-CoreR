/* Arena stages in the test build. Formats: tools/acr_import/acr_stages_moa.h;
 * notes: docs/research/gen1-stages.md. */
#include "demo_stage.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "acr_model.h"
#include "acr_png.h"

static char *load_text(const char *path) {
    size_t n = 0;
    char *t = SDL_LoadFile(path, &n);
    return t;
}

/* start_1 = [x, y, z, facing] */
static int read_start(const char *toml, const char *key, int32_t out[4]) {
    const char *p = SDL_strstr(toml, key);
    if (!p || !(p = SDL_strchr(p, '['))) return 0;
    ++p;
    for (int i = 0; i < 4; ++i) {
        char *e;
        out[i] = (int32_t)SDL_strtol(p, &e, 10);
        if (e == p) return 0;
        p = e;
        while (*p == ',' || *p == ' ') ++p;
    }
    return 1;
}

static acr_vtx vtx(const float *p, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    acr_vtx v = { p[0], p[1], p[2], r, g, b, a, 0, 0 };
    return v;
}

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

/* Faces shaded by a fixed light from above (no stage lights read yet).
 * Textured faces go in a batch per texture, coloured the PS1 way (128 =
 * the texel as it is). */
static int build_faces(demo_stage *s, const char *obj, const char *mtl_text, const char *dir) {
    acr_mtl mtl;
    if (!acr_mtl_parse(mtl_text ? mtl_text : "", &mtl)) return 0;
    acr_mesh mesh;
    if (!acr_obj_parse(obj, &mtl, &mesh)) return 0;
    if (!mesh.tri_count) { acr_mesh_free(&mesh); return 0; }
    for (int i = 0; i < mtl.count && s->batches < 16; ++i) {
        if (!mtl.m[i].textured) continue;
        char path[1200];
        SDL_snprintf(path, sizeof path, "%s/%s", dir, mtl.m[i].texture);
        s->batch[s->batches].texture = load_texture(path);
        s->batch[s->batches].v = SDL_malloc(sizeof(acr_vtx) * (size_t)(mesh.tri_count * 3 + 3));
        if (!s->batch[s->batches].v) { acr_mesh_free(&mesh); return 0; }
        mtl.m[i].kind = (uint8_t)s->batches++;      /* which batch (kind is unused here) */
        s->textured = 1;
    }
    s->tris = SDL_malloc(sizeof *s->tris * (size_t)(mesh.tri_count * 3 + 3));
    s->semi = SDL_malloc(sizeof *s->semi * (size_t)(mesh.tri_count * 3 + 3));
    if (!s->tris || !s->semi) { acr_mesh_free(&mesh); return 0; }
    const float L[3] = { 0.3f, 0.9f, 0.3f };
    for (int t = 0; t < mesh.tri_count; ++t) {
        const float *a = mesh.v[t * 3].p, *b = mesh.v[t * 3 + 1].p, *c = mesh.v[t * 3 + 2].p;
        const float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        float n[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
        const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        float lit = 0.75f;
        if (len > 0) lit = 0.55f + 0.45f * fabsf((n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / len);
        const acr_material *m = mesh.material[t] < mtl.count ? &mtl.m[mesh.material[t]] : NULL;
        const uint8_t r = (uint8_t)((m ? m->r : 160) * lit), g = (uint8_t)((m ? m->g : 160) * lit),
                      bl = (uint8_t)((m ? m->b : 160) * lit);
        if (m && m->textured && m->kind < s->batches) {
            const uint8_t c = (uint8_t)(128 * lit);
            acr_vtx *out = s->batch[m->kind].v + s->batch[m->kind].count;
            for (int j = 0; j < 3; ++j) {
                out[j] = vtx(mesh.v[t * 3 + j].p, c, c, c, 255);
                out[j].u = mesh.v[t * 3 + j].uv[0];
                out[j].v = 1.0f - mesh.v[t * 3 + j].uv[1];
            }
            s->batch[m->kind].count += 3;
            continue;
        }
        const int semi = m && m->semi;
        acr_vtx *out = semi ? s->semi + s->semi_count : s->tris + s->tri_count;
        for (int j = 0; j < 3; ++j) out[j] = vtx(mesh.v[t * 3 + j].p, r, g, bl, semi ? 40 : 255);
        if (semi) s->semi_count += 3;
        else s->tri_count += 3;
    }
    acr_mesh_free(&mesh);
    return 1;
}

/* Outlines of the faces as written (before triangulation), in the face colour. */
static int build_edges(demo_stage *s, const char *obj) {
    int lines = 0, cap = 0, nv = 0, vcap = 0;
    float (*v)[3] = NULL;
    for (int pass = 0; pass < 2; ++pass) {
        nv = 0;
        for (const char *p = obj; *p; ) {
            const char *e = SDL_strchr(p, '\n');
            if (!e) e = p + SDL_strlen(p);
            if (p[0] == 'v' && p[1] == ' ') {
                if (pass == 0) {
                    if (nv == vcap) {
                        vcap = vcap ? vcap * 2 : 1024;
                        float (*nvv)[3] = SDL_realloc(v, sizeof *v * (size_t)vcap);
                        if (!nvv) { SDL_free(v); return 0; }
                        v = nvv;
                    }
                    char *q;
                    v[nv][0] = SDL_strtod(p + 2, &q);
                    v[nv][1] = SDL_strtod(q, &q);
                    v[nv][2] = SDL_strtod(q, &q);
                }
                ++nv;
            } else if (p[0] == 'f' && p[1] == ' ') {
                long idx[8];
                int k = 0;
                const char *q = p + 1;
                while (q < e && k < 8) {
                    while (q < e && *q == ' ') ++q;
                    if (q >= e || *q < '0' || *q > '9') break;
                    char *qe;
                    idx[k++] = SDL_strtol(q, &qe, 10);
                    q = qe;
                    while (q < e && *q != ' ') ++q;
                }
                if (pass == 1)
                    for (int j = 0; j < k; ++j) {
                        const long a = idx[j], b = idx[(j + 1) % k];
                        if (a < 1 || b < 1 || a > nv || b > nv || lines + 2 > cap) continue;
                        s->edges[lines++] = vtx(v[a - 1], 120, 255, 140, 200);
                        s->edges[lines++] = vtx(v[b - 1], 120, 255, 140, 200);
                    }
                else cap += 2 * k;
            }
            p = *e ? e + 1 : e;
        }
        if (pass == 0) {
            s->edges = SDL_malloc(sizeof *s->edges * (size_t)(cap + 2));
            if (!s->edges) { SDL_free(v); return 0; }
        }
    }
    SDL_free(v);
    s->edge_count = lines;
    return 1;
}

static void demo_stage_free_draw(demo_stage *s);

int demo_stage_load(const char *dir, int id, demo_stage *out) {
    SDL_memset(out, 0, sizeof *out);
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/stages/%02d/collision.obj", dir, id);
    char *coll = load_text(path);
    SDL_snprintf(path, sizeof path, "%s/stages/%02d/stage.toml", dir, id);
    char *toml = load_text(path);
    char sdir[1100];
    SDL_snprintf(sdir, sizeof sdir, "%s/stages/%02d", dir, id);
    SDL_snprintf(path, sizeof path, "%s/visual.obj", sdir);
    char *vis = load_text(path);
    SDL_snprintf(path, sizeof path, "%s/visual.mtl", sdir);
    char *mtl = load_text(path);
    int ok = coll && toml && acr_gen1_stage_parse(coll, &out->collision) &&
             read_start(toml, "start_1", out->start[0]) && read_start(toml, "start_2", out->start[1]);
    /* The textured models; imports from before 2026-11-19 (or a stage
     * without them) fall back to the flat green ones, with outlines. */
    int drawn = ok && vis && build_faces(out, vis, mtl, sdir);
    if (ok && !drawn) {
        demo_stage_free_draw(out);
        SDL_free(vis); SDL_free(mtl);
        SDL_snprintf(path, sizeof path, "%s/simple.obj", sdir);
        vis = load_text(path);
        SDL_snprintf(path, sizeof path, "%s/simple.mtl", sdir);
        mtl = load_text(path);
        if (!vis) {   /* the first stage import named the flat models visual.obj */
            SDL_snprintf(path, sizeof path, "%s/visual.obj", sdir);
            vis = load_text(path);
            SDL_snprintf(path, sizeof path, "%s/visual.mtl", sdir);
            mtl = load_text(path);
        }
        drawn = vis && build_faces(out, vis, mtl, sdir) && build_edges(out, vis);
    }
    ok = ok && drawn;
    if (ok) {
        out->ok = 1;
        out->id = id;
        const char *n = SDL_strstr(toml, "name = \"");
        if (n) {
            n += 8;
            int k = 0;
            while (n[k] && n[k] != '"' && k < (int)sizeof out->name - 1) { out->name[k] = n[k]; ++k; }
            out->name[k] = 0;
        }
        /* A square inside the walls (round arenas: the circle's inner square). */
        const acr_gen1_stage *c = &out->collision;
        const int32_t hx = (c->hi[0] - c->lo[0]) / 2, hz = (c->hi[2] - c->lo[2]) / 2;
        out->half = (hx < hz ? hx : hz) * 7 / 10;
    }
    SDL_free(coll); SDL_free(toml); SDL_free(vis); SDL_free(mtl);
    if (!ok) demo_stage_free(out);
    return ok;
}

static void demo_stage_free_draw(demo_stage *s) {
    SDL_free(s->tris); SDL_free(s->semi); SDL_free(s->edges);
    s->tris = s->semi = s->edges = NULL;
    s->tri_count = s->semi_count = s->edge_count = 0;
    for (int i = 0; i < s->batches; ++i) SDL_free(s->batch[i].v);   /* textures stay with GL */
    s->batches = 0;
    s->textured = 0;
}

void demo_stage_free(demo_stage *s) {
    acr_gen1_stage_free(&s->collision);
    demo_stage_free_draw(s);
    SDL_memset(s, 0, sizeof *s);
}

void demo_stage_draw(const demo_stage *s, int pass) {
    if (!s->ok) return;
    if (pass == 0) {
        for (int i = 0; i < s->batches; ++i)
            if (s->batch[i].count) acr_render_mesh(s->batch[i].v, s->batch[i].count, s->batch[i].texture, ACR_MESH_PS1_TEXEL);
        if (s->tri_count) acr_render_mesh(s->tris, s->tri_count, 0, 0);
        if (s->edge_count) acr_render_lines(s->edges, s->edge_count);
    } else if (s->semi_count) {
        acr_render_mesh(s->semi, s->semi_count, 0, ACR_MESH_SEMI);
    }
}
