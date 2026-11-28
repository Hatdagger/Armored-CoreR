#include "gen2_ac.h"

#include <SDL3/SDL.h>
#include <math.h>

#include "acr_render.h"
#include "../../model/acr_png.h"

#define GEN2_MAX_TRIS 8192
static acr_vtx g_out[GEN2_MAX_TRIS * 3], g_batch[GEN2_MAX_TRIS * 3];

/* The material's map_Kd (beside ac.mtl), as a renderer texture; 0 if none. */
static uint32_t load_texture(const char *dir, const char *name) {
    char path[1400];
    SDL_snprintf(path, sizeof path, "%s/%s", dir, name);
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
        for (int i = 0; i < img.w * img.h; ++i)
            SDL_memcpy(rgba + 4 * i, img.indexed ? img.palette[img.pixels[i]] : img.pixels + 4 * i, 4);
        tex = acr_render_texture(rgba, img.w, img.h, 0);
        SDL_free(rgba);
    }
    acr_image_free(&img);
    return tex;
}

int gen2_ac_load(const char *import_dir, gen2_ac *g) {
    if (g->ok || g->tried) return g->ok;
    g->tried = 1;
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/gen2/ac2/test-ac/ac.mtl", import_dir);
    char *t = SDL_LoadFile(path, NULL);
    const int mok = t && acr_mtl_parse(t, &g->mtl);
    SDL_free(t);
    if (!mok) { SDL_Log("gen 2: no test AC (import an Armored Core 2 disc)"); return 0; }
    SDL_snprintf(path, sizeof path, "%s/gen2/ac2/test-ac/ac.obj", import_dir);
    t = SDL_LoadFile(path, NULL);
    g->ok = t && acr_obj_parse(t, &g->mtl, &g->mesh) && g->mesh.tri_count > 0;
    SDL_free(t);
    SDL_snprintf(path, sizeof path, "%s/gen2/ac2/test-ac", import_dir);
    int textures = 0;
    for (int i = 0; g->ok && i < g->mtl.count; ++i)
        if (g->mtl.m[i].textured && (g->tex[i] = load_texture(path, g->mtl.m[i].texture)) != 0) ++textures;
    SDL_Log("gen 2: %d textures", textures);
    SDL_Log("gen 2: test AC %s (%d triangles)", g->ok ? "loaded" : "did not load", g->ok ? g->mesh.tri_count : 0);
    return g->ok;
}

void gen2_ac_free(gen2_ac *g) {
    acr_mesh_free(&g->mesh);
    SDL_memset(g, 0, sizeof *g);
}

void gen2_ac_draw(const gen2_ac *g, float x, float y, float z, float yaw, float scale, int flash,
                  const acr_lighting *light) {
    if (!g->ok) return;
    const float s = sinf(yaw), c = cosf(yaw);
    const int n = g->mesh.tri_count < GEN2_MAX_TRIS ? g->mesh.tri_count : GEN2_MAX_TRIS;
    for (int t = 0; t < n; ++t) {
        const acr_material *mt = &g->mtl.m[g->mesh.material[t]];
        for (int j = 0; j < 3; ++j) {
            const acr_mesh_vertex *v = &g->mesh.v[t * 3 + j];
            /* model space has the right hand at -x, the world at +x (as demo_model_draw_pose) */
            const float lx = -v->p[0] * scale, ly = v->p[1] * scale, lz = v->p[2] * scale;
            const float nx = -v->n[0], ny = v->n[1], nz = v->n[2];
            uint8_t col[3];
            acr_light_color(light, (int32_t)((nx * c + nz * s) * 4096.0f), (int32_t)(ny * 4096.0f),
                            (int32_t)((-nx * s + nz * c) * 4096.0f), mt->r, mt->g, mt->b, col);
            if (flash) for (int k = 0; k < 3; ++k) col[k] = (uint8_t)(col[k] + (255 - col[k]) / 2);
            acr_vtx *o = &g_out[t * 3 + j];
            o->x = x + lx * c + lz * s;
            o->y = y + ly;
            o->z = z - lx * s + lz * c;
            o->r = col[0]; o->g = col[1]; o->b = col[2]; o->a = 255;
            o->u = v->uv[0]; o->v = 1.0f - v->uv[1];
        }
    }
    for (int mi = 0; mi < g->mtl.count; ++mi) {   /* a batch per material: its texture tinted, or flat */
        int count = 0;
        for (int t = 0; t < n; ++t)
            if (g->mesh.material[t] == mi) SDL_memcpy(&g_batch[3 * count++], &g_out[t * 3], 3 * sizeof(acr_vtx));
        /* PS2 modulate: vertex colour 128 = the texel as is, so x2 as the PS1 path */
        if (count) acr_render_mesh(g_batch, count * 3, g->tex[mi], g->tex[mi] ? ACR_MESH_PS1_TEXEL : 0);
    }
}
