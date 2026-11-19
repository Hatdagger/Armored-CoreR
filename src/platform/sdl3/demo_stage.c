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
        tex = acr_render_texture(rgba, img.w, img.h, 2);   /* sharp, mipmapped far off */
        SDL_free(rgba);
    }
    acr_image_free(&img);
    return tex;
}

/* Faces shaded by a fixed light from above (no stage lights read yet).
 * Textured faces go in a batch per texture, coloured the PS1 way (128 =
 * the texel as it is). */
/* Faces lying on another face in the same plane: the smaller of two such
 * faces is the overlay (a decal or a model's second layer); an exact copy
 * of an earlier face is dropped (2). Plane within half a unit, normals
 * within about 2.5 degrees, the overlay's middle inside the other face. */
static void find_overlays(const acr_mesh *mesh, uint8_t *mark) {
    const int n = mesh->tri_count;
    float (*nrm)[4] = SDL_malloc(sizeof *nrm * (size_t)(n ? n : 1));
    float (*box)[6] = SDL_malloc(sizeof *box * (size_t)(n ? n : 1));
    if (!nrm || !box) { SDL_free(nrm); SDL_free(box); return; }
    for (int t = 0; t < n; ++t) {
        const float *a = mesh->v[t * 3].p, *b = mesh->v[t * 3 + 1].p, *c = mesh->v[t * 3 + 2].p;
        const float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        float q[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
        const float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
        nrm[t][3] = len * 0.5f;                                     /* area */
        for (int k = 0; k < 3; ++k) nrm[t][k] = len > 0 ? q[k] / len : 0;
        for (int k = 0; k < 3; ++k) {
            box[t][k] = SDL_min(a[k], SDL_min(b[k], c[k]));
            box[t][3 + k] = SDL_max(a[k], SDL_max(b[k], c[k]));
        }
    }
    for (int i = 0; i < n; ++i) {
        if (nrm[i][3] <= 0) continue;
        for (int j = i + 1; j < n; ++j) {
            if (nrm[j][3] <= 0 || mark[i] == 2 || mark[j] == 2) continue;
            if (box[j][0] > box[i][3] + 0.5f || box[j][3] < box[i][0] - 0.5f || box[j][1] > box[i][4] + 0.5f ||
                box[j][4] < box[i][1] - 0.5f || box[j][2] > box[i][5] + 0.5f || box[j][5] < box[i][2] - 0.5f) continue;
            const float d = nrm[i][0] * nrm[j][0] + nrm[i][1] * nrm[j][1] + nrm[i][2] * nrm[j][2];
            if (d < 0.999f && d > -0.999f) continue;
            const float *pi = mesh->v[i * 3].p;
            int same_plane = 1;
            for (int k = 0; k < 3 && same_plane; ++k) {
                const float *pj = mesh->v[j * 3 + k].p;
                const float off = (pj[0] - pi[0]) * nrm[i][0] + (pj[1] - pi[1]) * nrm[i][1] + (pj[2] - pi[2]) * nrm[i][2];
                if (off > 0.5f || off < -0.5f) same_plane = 0;
            }
            if (!same_plane) continue;
            /* Exact copy? */
            int copy = 1;
            for (int k = 0; k < 3 && copy; ++k) {
                int found = 0;
                for (int m = 0; m < 3; ++m) {
                    const float *a = mesh->v[i * 3 + m].p, *b = mesh->v[j * 3 + k].p;
                    if (SDL_fabsf(a[0] - b[0]) < 0.01f && SDL_fabsf(a[1] - b[1]) < 0.01f && SDL_fabsf(a[2] - b[2]) < 0.01f)
                        found = 1;
                }
                if (!found) copy = 0;
            }
            if (copy) { mark[j] = 2; continue; }
            /* The smaller one's middle inside the bigger one. */
            const int small = nrm[j][3] <= nrm[i][3] ? j : i, big = small == j ? i : j;
            float mid[3] = { 0, 0, 0 };
            for (int k = 0; k < 3; ++k)
                for (int m = 0; m < 3; ++m) mid[m] += mesh->v[small * 3 + k].p[m] / 3.0f;
            int pos = 0, neg = 0;
            for (int k = 0; k < 3; ++k) {
                const float *a = mesh->v[big * 3 + k].p, *b = mesh->v[big * 3 + (k + 1) % 3].p;
                const float e[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, r[3] = { mid[0] - a[0], mid[1] - a[1], mid[2] - a[2] };
                const float c[3] = { e[1] * r[2] - e[2] * r[1], e[2] * r[0] - e[0] * r[2], e[0] * r[1] - e[1] * r[0] };
                const float side = c[0] * nrm[big][0] + c[1] * nrm[big][1] + c[2] * nrm[big][2];
                if (side > 0) ++pos;
                if (side < 0) ++neg;
            }
            if (pos && neg) continue;
            if (!mark[small]) mark[small] = 1;
        }
    }
    SDL_free(nrm);
    SDL_free(box);
}

static int build_faces(demo_stage *s, const char *obj, const char *mtl_text, const char *dir, int unlit) {
    acr_mtl mtl;
    if (!acr_mtl_parse(mtl_text ? mtl_text : "", &mtl)) return 0;
    acr_mesh mesh;
    if (!acr_obj_parse(obj, &mtl, &mesh)) return 0;
    if (!mesh.tri_count) { acr_mesh_free(&mesh); return 0; }
    for (int i = 0; i < mtl.count && s->batches + 2 <= 32; ++i) {
        if (!mtl.m[i].textured) continue;
        char path[1200];
        SDL_snprintf(path, sizeof path, "%s/%s", dir, mtl.m[i].texture);
        const uint32_t tex = load_texture(path);
        for (int ov = 0; ov < 2; ++ov) {
            s->batch[s->batches + ov].texture = tex;
            s->batch[s->batches + ov].semi = mtl.m[i].semi;
            s->batch[s->batches + ov].overlay = (uint8_t)ov;
            s->batch[s->batches + ov].v = SDL_malloc(sizeof(acr_vtx) * (size_t)(mesh.tri_count * 3 + 3));
            if (!s->batch[s->batches + ov].v) { acr_mesh_free(&mesh); return 0; }
        }
        mtl.m[i].kind = (uint8_t)s->batches;          /* which batch (kind is unused here); +1 = overlays */
        s->batches += 2;
        s->textured = 1;
    }
    uint8_t *mark = SDL_calloc((size_t)mesh.tri_count + 1, 1);
    if (!mark) { acr_mesh_free(&mesh); return 0; }
    find_overlays(&mesh, mark);
    {
        int ov = 0, cp = 0;
        for (int t = 0; t < mesh.tri_count; ++t) { ov += mark[t] == 1; cp += mark[t] == 2; }
        SDL_Log("stage faces: %d, lying on another face: %d, exact copies dropped: %d", mesh.tri_count, ov, cp);
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
        float lit = unlit ? 1.0f : 0.75f;
        if (len > 0 && !unlit) lit = 0.55f + 0.45f * fabsf((n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / len);
        const acr_material *m = mesh.material[t] < mtl.count ? &mtl.m[mesh.material[t]] : NULL;
        const uint8_t r = (uint8_t)((m ? m->r : 160) * lit), g = (uint8_t)((m ? m->g : 160) * lit),
                      bl = (uint8_t)((m ? m->b : 160) * lit);
        if (mark[t] == 2) continue;                    /* an exact copy of an earlier face */
        if (m && m->textured && m->kind < s->batches) {
            const uint8_t c = (uint8_t)(128 * lit);
            const int bi = m->kind + (mark[t] ? 1 : 0);
            acr_vtx *out = s->batch[bi].v + s->batch[bi].count;
            for (int j = 0; j < 3; ++j) {
                out[j] = vtx(mesh.v[t * 3 + j].p, c, c, c, s->batch[bi].semi ? 128 : 255);
                out[j].u = mesh.v[t * 3 + j].uv[0];
                out[j].v = 1.0f - mesh.v[t * 3 + j].uv[1];
            }
            s->batch[bi].count += 3;
            continue;
        }
        const int semi = m && m->semi;
        acr_vtx *out = semi ? s->semi + s->semi_count : s->tris + s->tri_count;
        for (int j = 0; j < 3; ++j) out[j] = vtx(mesh.v[t * 3 + j].p, r, g, bl, semi ? 40 : 255);
        if (semi) s->semi_count += 3;
        else s->tri_count += 3;
    }
    SDL_free(mark);
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
    /* The game's own collision (imports from 2026-11-19 on): walls and
     * floors then follow the game's rules instead of the stand-ins. */
    if (ok) {
        SDL_snprintf(path, sizeof path, "%s/collision.txt", sdir);
        char *game = load_text(path);
        if (game && !acr_gen1_stage_parse_game(game, &out->collision))
            SDL_Log("stage %02d: collision.txt unreadable; using the stand-in walls and floors", id);
        SDL_free(game);
    }
    /* The textured models; imports from before 2026-11-19 (or a stage
     * without them) fall back to the flat green ones, with outlines. */
    int drawn = ok && vis && build_faces(out, vis, mtl, sdir, 0);
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
        drawn = vis && build_faces(out, vis, mtl, sdir, 0) && build_edges(out, vis);
    }
    ok = ok && drawn;
    if (ok) {
        /* The sky: its faces built as a stage of their own, then kept. */
        SDL_snprintf(path, sizeof path, "%s/sky.obj", sdir);
        char *sky = load_text(path);
        SDL_snprintf(path, sizeof path, "%s/sky.mtl", sdir);
        char *sky_mtl = load_text(path);
        demo_stage tmp;
        SDL_memset(&tmp, 0, sizeof tmp);
        if (sky && build_faces(&tmp, sky, sky_mtl, sdir, 1)) {
            for (int i = 0; i < tmp.batches; ++i) {
                if (tmp.batch[i].count && out->skies < 4) {
                    out->sky[out->skies].v = tmp.batch[i].v;
                    out->sky[out->skies].count = tmp.batch[i].count;
                    out->sky[out->skies].texture = tmp.batch[i].texture;
                    ++out->skies;
                    tmp.batch[i].v = NULL;
                }
            }
        }
        demo_stage_free_draw(&tmp);
        SDL_free(sky); SDL_free(sky_mtl);
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
    for (int i = 0; i < s->skies; ++i) SDL_free(s->sky[i].v);
    s->skies = 0;
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
            if (s->batch[i].count && !s->batch[i].semi)
                acr_render_mesh(s->batch[i].v, s->batch[i].count, s->batch[i].texture,
                                ACR_MESH_PS1_TEXEL | (s->batch[i].overlay ? ACR_MESH_OVERLAY : 0));
        if (s->tri_count) acr_render_mesh(s->tris, s->tri_count, 0, 0);
        if (s->edge_count) acr_render_lines(s->edges, s->edge_count);
    } else {
        /* See-through glass and the like: 50/50 (the PS1's default blend). */
        for (int i = 0; i < s->batches; ++i)
            if (s->batch[i].count && s->batch[i].semi)
                acr_render_mesh(s->batch[i].v, s->batch[i].count, s->batch[i].texture,
                                ACR_MESH_PS1_TEXEL | ACR_MESH_SEMI | (s->batch[i].overlay ? ACR_MESH_OVERLAY : 0));
        if (s->semi_count) acr_render_mesh(s->semi, s->semi_count, 0, ACR_MESH_SEMI);
    }
}

void demo_stage_draw_sky(const demo_stage *s, const float eye[3]) {
    if (!s->ok) return;
    for (int i = 0; i < s->skies; ++i) acr_render_sky(s->sky[i].v, s->sky[i].count, s->sky[i].texture, eye);
}
