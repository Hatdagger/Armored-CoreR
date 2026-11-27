/* Arena stages in the test build. Formats: tools/acr_import/acr_stages_moa.h;
 * notes: docs/research/gen1-stages.md. */
#include "demo_stage.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "acr_model.h"
#include "acr_png.h"
#include "../../gen1/acr_gen1_mission.h"

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

/* out_of_area = [ [lo x, y, z, hi x, y, z, bit], ... ] (engine axes). */
static uint8_t read_out_of_area(const char *toml, acr_gen1_stage_bound *out, unsigned max) {
    const char *p = SDL_strstr(toml, "\nout_of_area");
    if (!p || !(p = SDL_strchr(p, '['))) return 0;
    ++p;
    unsigned n = 0;
    while (n < max) {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == ',') ++p;
        if (*p != '[') break;
        ++p;
        long v[7];
        int k = 0;
        for (; k < 7; ++k) {
            char *e;
            v[k] = SDL_strtol(p, &e, 10);
            if (e == p) break;
            p = e;
            while (*p == ',' || *p == ' ') ++p;
        }
        if (k < 7 || *p != ']' || v[6] < 0 || v[6] > 7) return 0;
        ++p;
        for (int i = 0; i < 3; ++i) { out[n].lo[i] = (int32_t)v[i]; out[n].hi[i] = (int32_t)v[3 + i]; }
        out[n].bit = (uint8_t)v[6];
        ++n;
    }
    return (uint8_t)n;
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

/* Faces lit the game's way when the import wrote the stage lights (a
 * colour on every point: r g b x 255 = the PS1 colour, 128 = the texel as
 * it is; docs/research/gen1-stages.md, "Stage lights"); otherwise (older
 * imports, custom maps) shaded by a fixed light from above. Textured faces
 * go in a batch per texture, coloured the PS1 way. */
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
        /* The game's light for this face, per channel (1 = the texel as it is). */
        float gl[3] = { lit, lit, lit };
        if (mesh.has_colour && !unlit)
            for (int k = 0; k < 3; ++k) gl[k] = SDL_roundf(mesh.v[t * 3].c[k] * 255.0f) / 128.0f;
        const float fr = (m ? m->r : 160) * gl[0], fg = (m ? m->g : 160) * gl[1], fb = (m ? m->b : 160) * gl[2];
        const uint8_t r = (uint8_t)SDL_min(fr, 255.0f), g = (uint8_t)SDL_min(fg, 255.0f), bl = (uint8_t)SDL_min(fb, 255.0f);
        if (mark[t] == 2) continue;                    /* an exact copy of an earlier face */
        if (m && m->textured && m->kind < s->batches) {
            uint8_t c[3];
            for (int k = 0; k < 3; ++k) c[k] = (uint8_t)SDL_min(128.0f * gl[k] + 0.5f, 255.0f);
            const int bi = m->kind + (mark[t] ? 1 : 0);
            acr_vtx *out = s->batch[bi].v + s->batch[bi].count;
            for (int j = 0; j < 3; ++j) {
                out[j] = vtx(mesh.v[t * 3 + j].p, c[0], c[1], c[2], s->batch[bi].semi ? 128 : 255);
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

/* ---- our backdrop (made up, not from the disc; ARENA.md "Backgrounds") ---- */

/* Per stage: zenith, horizon and cloud colours, cloud cover. Green Silo
 * has the game's starfield instead; Night City is black in the game, so
 * its sky stays dark. */
static const struct { int id; uint8_t zenith[3], horizon[3], cloud[3]; float cover; } k_backdrops[] = {
    { 25, { 62, 104, 168 }, { 186, 196, 204 }, { 236, 236, 240 }, 0.45f },   /* Garden Plaza */
    { 36, { 2, 3, 8 }, { 18, 22, 40 }, { 40, 44, 62 }, 0.55f },              /* Night City */
    { 39, { 38, 86, 156 }, { 168, 192, 212 }, { 240, 242, 246 }, 0.35f },   /* Ocean Platform */
};

#define DOME_R      1000.0f
#define DOME_RINGS  12
#define DOME_SEGS   32
#define CLOUD_TEX   128
#define CLOUD_CELL  2400.0f     /* one texture repeat, sky units */
#define CLOUD_CELLS 8           /* each way */
#define CLOUD_H     420.0f      /* above the eye, sky units */

/* Tileable value noise (lattice wraps every `per`). */
static float lattice(int x, int y, int per, uint32_t seed) {
    uint32_t h = (uint32_t)(((x % per) + per) % per) * 374761393u + (uint32_t)(((y % per) + per) % per) * 668265263u + seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
static float vnoise(float x, float y, int per, uint32_t seed) {
    const int x0 = (int)SDL_floorf(x), y0 = (int)SDL_floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    const float a = lattice(x0, y0, per, seed), b = lattice(x0 + 1, y0, per, seed);
    const float c = lattice(x0, y0 + 1, per, seed), d = lattice(x0 + 1, y0 + 1, per, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static uint32_t make_cloud_texture(float cover) {
    uint8_t *px = SDL_malloc(CLOUD_TEX * CLOUD_TEX * 4);
    if (!px) return 0;
    for (int y = 0; y < CLOUD_TEX; ++y)
        for (int x = 0; x < CLOUD_TEX; ++x) {
            float n = 0, amp = 0.5f, sum = 0;
            for (int o = 0, per = 4; o < 5; ++o, per *= 2, amp *= 0.5f) {
                n += amp * vnoise(x * per / (float)CLOUD_TEX, y * per / (float)CLOUD_TEX, per, 17u + (uint32_t)o * 101u);
                sum += amp;
            }
            n /= sum;
            const float lo = 0.62f - cover * 0.3f, hi = lo + 0.25f;
            float a = (n - lo) / (hi - lo);
            a = a < 0 ? 0 : a > 1 ? 1 : a;
            a = a * a * (3 - 2 * a);
            uint8_t *p = px + 4 * (y * CLOUD_TEX + x);
            const uint8_t shade = (uint8_t)(200 + 55 * a);   /* thicker = lighter */
            p[0] = p[1] = p[2] = shade;
            p[3] = (uint8_t)(a * 230);
        }
    const uint32_t tex = acr_render_texture(px, CLOUD_TEX, CLOUD_TEX, 0);
    SDL_free(px);
    return tex;
}

static void mix(const uint8_t a[3], const uint8_t b[3], float t, uint8_t out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = (uint8_t)(a[k] + (b[k] - a[k]) * t + 0.5f);
}

/* A sphere around the eye: the horizon colour below the horizon and at it,
 * blending to the zenith colour overhead (eased, so the haze band is wide). */
static int build_dome(demo_stage *s) {
    const int n = DOME_RINGS * DOME_SEGS * 6 + DOME_SEGS * 6;
    s->dome = SDL_malloc(sizeof *s->dome * (size_t)n);
    if (!s->dome) return 0;
    int k = 0;
    for (int r = -1; r < DOME_RINGS; ++r) {
        /* ring -1: from straight down to the horizon */
        const float e0 = r < 0 ? -1.5707963f : 1.5707963f * r / DOME_RINGS;
        const float e1 = r < 0 ? 0.0f : 1.5707963f * (r + 1) / DOME_RINGS;
        uint8_t c0[3], c1[3];
        const float t0 = r < 0 ? 0 : SDL_powf(e0 / 1.5707963f, 0.6f), t1 = SDL_powf(e1 / 1.5707963f, 0.6f);
        mix(s->horizon, s->zenith, t0, c0);
        mix(s->horizon, s->zenith, t1, c1);
        for (int g = 0; g < DOME_SEGS; ++g) {
            const float a0 = 6.2831853f * g / DOME_SEGS, a1 = 6.2831853f * (g + 1) / DOME_SEGS;
            float p[4][3];
            const float e[4] = { e0, e0, e1, e1 }, a[4] = { a0, a1, a1, a0 };
            for (int i = 0; i < 4; ++i) {
                p[i][0] = DOME_R * SDL_cosf(e[i]) * SDL_cosf(a[i]);
                p[i][1] = DOME_R * SDL_sinf(e[i]);
                p[i][2] = DOME_R * SDL_cosf(e[i]) * SDL_sinf(a[i]);
            }
            const int idx[6] = { 0, 1, 2, 0, 2, 3 };
            for (int i = 0; i < 6; ++i) {
                const uint8_t *c = idx[i] < 2 ? c0 : c1;
                s->dome[k++] = vtx(p[idx[i]], c[0], c[1], c[2], 255);
            }
        }
    }
    s->dome_count = k;
    return 1;
}

static void setup_backdrop(demo_stage *s) {
    if (s->skies) return;                                   /* the game's own sky */
    for (size_t i = 0; i < SDL_arraysize(k_backdrops); ++i) {
        if (k_backdrops[i].id != s->id) continue;
        SDL_memcpy(s->zenith, k_backdrops[i].zenith, 3);
        SDL_memcpy(s->horizon, k_backdrops[i].horizon, 3);
        SDL_memcpy(s->cloud, k_backdrops[i].cloud, 3);
        s->cloud_cover = k_backdrops[i].cover;
        if (!build_dome(s)) return;
        s->cloud_tex = make_cloud_texture(s->cloud_cover);
        s->backdrop = 1;
    }
}

/* far.obj: built like a stage, then every batch copied into a grid of
 * tiles out to the horizon, a little under the stage so the stage's own
 * ground wins where both are. Drawn solid (the game's is see-through). */
static void load_far(demo_stage *s, const char *sdir) {
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/far.obj", sdir);
    char *obj = load_text(path);
    SDL_snprintf(path, sizeof path, "%s/far.mtl", sdir);
    char *mtl = load_text(path);
    demo_stage tmp;
    SDL_memset(&tmp, 0, sizeof tmp);
    if (obj && build_faces(&tmp, obj, mtl, sdir, 0)) {
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < tmp.batches; ++i)
            for (int j = 0; j < tmp.batch[i].count; ++j) {
                lo = SDL_min(lo, tmp.batch[i].v[j].x);
                hi = SDL_max(hi, tmp.batch[i].v[j].x);
            }
        const float size = hi - lo;
        const int reach = size > 1000.0f ? (int)SDL_ceilf(40000.0f / size) : 0;
        const int tiles = (2 * reach + 1) * (2 * reach + 1);
        for (int i = 0; i < tmp.batches && s->fars < 4 && reach; ++i) {
            const int n = tmp.batch[i].count;
            if (!n) continue;
            acr_vtx *v = SDL_malloc(sizeof *v * (size_t)n * (size_t)tiles);
            if (!v) break;
            int k = 0;
            for (int tz = -reach; tz <= reach; ++tz)
                for (int tx = -reach; tx <= reach; ++tx)
                    for (int j = 0; j < n; ++j) {
                        v[k] = tmp.batch[i].v[j];
                        v[k].x += tx * size;
                        v[k].z += tz * size;
                        v[k].y -= 6.0f;
                        v[k].a = 255;
                        ++k;
                    }
            s->far[s->fars].v = v;
            s->far[s->fars].count = k;
            s->far[s->fars].texture = tmp.batch[i].texture;
            ++s->fars;
        }
    }
    demo_stage_free_draw(&tmp);
    SDL_free(obj); SDL_free(mtl);
}

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
    if (ok) {
        out->own_bounds = read_out_of_area(toml, out->own_bound, ACR_GEN1_STAGE_BOUNDS);
        SDL_memcpy(out->collision.bound, out->own_bound, sizeof out->own_bound);
        out->collision.bounds = out->own_bounds;
    }
    /* Mission maps (stages 0-19) keep the arena's two start records, but
     * the mission engine doesn't use them as drops: some sit side by side or
     * off the floor. Maps used by the mission tests get their own starts
     * here until missions bring their drop points (docs/research/gen1-stages.md,
     * "Mission maps"). */
    if (ok) {
        static const struct { int id; int32_t start[2][4]; } k_mission_starts[] = {
            /* 11, the four-pillar hall (P4 Qualifier): facing each other down the
             * middle, clear of the four pillars; the floor is at 3,000. */
            { 11, { { 0, 3000, -8000, 0 }, { 0, 3000, 8000, 2048 } } },
            /* 1 and 6 are sea maps: their class 1 / 4 ground is the water,
             * which ACs pass through, and the arena starts sit over it. 1: on
             * the long ship's deck (-250), facing each other along it; 6: on
             * the platform in the middle (-500). */
            { 1, { { -6600, -250, 0, 3072 }, { -9000, -250, 0, 1024 } } },
            { 6, { { 0, -500, -2500, 0 }, { 0, -500, 2500, 2048 } } },
        };
        int fixed = 0;
        for (size_t i = 0; i < sizeof k_mission_starts / sizeof k_mission_starts[0]; ++i)
            if (k_mission_starts[i].id == id) {
                SDL_memcpy(out->start, k_mission_starts[i].start, sizeof out->start);
                fixed = 1;
            }
        /* The other mission maps: the player at the map's record 0, which
         * stands on the floor on 15 of the 20 maps and over it on 3 and 5
         * (a drop from the air), so it is very likely the mission's drop
         * (docs/research/gen1-stages.md, "Missions"); the target 2,500
         * ahead, facing back. Not 13 (record 0 is outside the map). Imports
         * from before 2026-11-23 have no objects.toml and keep the arena's. */
        if (!fixed && acr_gen1_mission_map_ok(id) && id != 13) {
            SDL_snprintf(path, sizeof path, "%s/objects.toml", sdir);
            char *objs = load_text(path);
            const char *r0 = objs ? SDL_strstr(objs, "[[point]]\nrecord   = 0\n") : NULL;
            int32_t p[4] = { 0, 0, 0, 0 };
            const char *pos = r0 ? SDL_strstr(r0, "position") : NULL;
            int got = 0;
            if (pos && (pos = SDL_strchr(pos, '['))) {
                char *q = (char *)pos + 1, *e;
                for (; got < 3; ++got) {
                    p[got] = (int32_t)SDL_strtol(q, &e, 10);
                    if (e == q) break;
                    q = e;
                    while (*q == ',' || *q == ' ') ++q;
                }
            }
            if (got == 3) {
                const char *f = SDL_strstr(r0, "facing");
                p[3] = f && (f = SDL_strchr(f, '=')) ? (int32_t)SDL_atoi(f + 1) & 4095 : 0;
                const float a = (float)p[3] * (6.2831853f / 4096.0f);
                SDL_memcpy(out->start[0], p, sizeof p);
                out->start[1][0] = p[0] + (int32_t)(sinf(a) * 2500.0f);
                out->start[1][1] = p[1];
                out->start[1][2] = p[2] + (int32_t)(cosf(a) * 2500.0f);
                out->start[1][3] = (p[3] + 2048) & 4095;
            }
            SDL_free(objs);
        }
    }
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
        load_far(out, sdir);
        setup_backdrop(out);
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
    for (int i = 0; i < s->fars; ++i) SDL_free(s->far[i].v);
    s->fars = 0;
    SDL_free(s->dome);
    s->dome = NULL;
    s->dome_count = 0;
    s->backdrop = 0;
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
        for (int i = 0; i < s->fars; ++i) acr_render_mesh(s->far[i].v, s->far[i].count, s->far[i].texture, ACR_MESH_PS1_TEXEL);
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

void demo_stage_draw_sky(const demo_stage *s, const float eye[3], float t) {
    if (!s->ok) return;
    for (int i = 0; i < s->skies; ++i) acr_render_sky(s->sky[i].v, s->sky[i].count, s->sky[i].texture, eye);
    if (!s->backdrop) return;
    acr_render_sky(s->dome, s->dome_count, 0, eye);
    if (!s->cloud_tex) return;
    /* The cloud layer: a grid of texture repeats overhead, drifting, fading
     * out toward the horizon. */
    static acr_vtx v[CLOUD_CELLS * CLOUD_CELLS * 6];
    const float half = CLOUD_CELL * CLOUD_CELLS / 2, drift = SDL_fmodf(t * 18.0f, CLOUD_CELL);
    int k = 0;
    for (int cz = 0; cz < CLOUD_CELLS; ++cz)
        for (int cx = 0; cx < CLOUD_CELLS; ++cx) {
            const float x0 = -half + cx * CLOUD_CELL + drift - CLOUD_CELL / 2, z0 = -half + cz * CLOUD_CELL;
            const float px[4] = { x0, x0 + CLOUD_CELL, x0 + CLOUD_CELL, x0 };
            const float pz[4] = { z0, z0, z0 + CLOUD_CELL, z0 + CLOUD_CELL };
            const float u[4] = { 0, 1, 1, 0 }, w[4] = { 0, 0, 1, 1 };
            const int idx[6] = { 0, 1, 2, 0, 2, 3 };
            for (int i = 0; i < 6; ++i) {
                const int c = idx[i];
                const float d = SDL_sqrtf(px[c] * px[c] + pz[c] * pz[c]) / half;
                const float fade = d >= 1 ? 0 : 1 - d * d;
                const float p[3] = { px[c], CLOUD_H, pz[c] };
                v[k] = vtx(p, (uint8_t)(s->cloud[0] / 2), (uint8_t)(s->cloud[1] / 2), (uint8_t)(s->cloud[2] / 2),
                           (uint8_t)(255 * fade));
                v[k].u = u[c];
                v[k].v = w[c];
                ++k;
            }
        }
    acr_render_sky(v, k, s->cloud_tex, eye);
}

int demo_stage_fog(const demo_stage *s, acr_fog *out) {
    if (!s->ok || !s->backdrop) return 0;
    out->start = 18000.0f;
    out->end = 39000.0f;
    out->r = s->horizon[0];
    out->g = s->horizon[1];
    out->b = s->horizon[2];
    return 1;
}
