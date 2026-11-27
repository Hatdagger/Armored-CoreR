/* The VS screen shared by Rogue Arena and the Arena. See vs_screen.h. */
#include "vs_screen.h"

#include <SDL3/SDL.h>

#include "../../render/acr_render.h"
#include "rogue_sound.h"
#include "sfx.h"

#define VS_MEET      0.30f
#define VS_DOOR_PEAK 0.156f
#define VS_FADE_AT   0.7f
#define VS_FADE_LEN  0.4f

const uint8_t vs_blue[3] = { 120, 170, 235 }, vs_red[3] = { 235, 90, 80 };

static struct {
    vs_setup s;
    float t, shut;                 /* seconds since it opened; since the shutters started (< 0 not yet) */
    int shots, door;
    int open;
} V;

static rogue_ac_drawer g_draw_ac;
static uint32_t g_soft_tex;

void vs_set_ac_drawer(rogue_ac_drawer fn) { g_draw_ac = fn; }

void vs_draw_ac(const int parts[SLOT_COUNT], const uint8_t colours[4][3], float x, float y, float w, float h,
                float frac, float yaw, int lying, float shade) {
    if (g_draw_ac) g_draw_ac(parts, colours, x, y, w, h, frac, yaw, lying, shade);
}

/* ---- drawing bits ---- */

void vs_text_at(float x, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b) {
    acr_hud_text_size(x, y, t, size, r, g, b, 255);
}
float vs_text_w(const char *t, float size) { return size * (float)SDL_strlen(t); }
void vs_text_mid(float cx, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b) {
    vs_text_at(cx - vs_text_w(t, size) * 0.5f, y, t, size, r, g, b);
}
void vs_text_right(float rx, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b) {
    vs_text_at(rx - vs_text_w(t, size), y, t, size, r, g, b);
}

void vs_gradient(float x, float y, float w, float h, const uint8_t c[3], float k0, float k1) {
    const int steps = 24;
    for (int i = 0; i < steps; ++i) {
        const float k = k0 + (k1 - k0) * ((float)i + 0.5f) / (float)steps;
        acr_hud_rect(x, y + h * (float)i / steps, w, h / steps + 0.5f, (uint8_t)SDL_min(255.0f, c[0] * k),
                     (uint8_t)SDL_min(255.0f, c[1] * k), (uint8_t)SDL_min(255.0f, c[2] * k), 255, 1);
    }
}

void vs_frame(float x, float y, float w, float h, const uint8_t c[3], float t) {
    for (float k = 0; k < 2; k += 1) acr_hud_rect(x - t - 2 - k, y - t - 2 - k, w + 2 * (t + 2 + k), h + 2 * (t + 2 + k), 10, 10, 14, 255, 0);
    for (float k = 0; k < t; k += 1) acr_hud_rect(x - 1 - k, y - 1 - k, w + 2 * (1 + k), h + 2 * (1 + k), c[0], c[1], c[2], 255, 0);
}

void vs_thick_line(float x0, float y0, float x1, float y1, float t, uint8_t r, uint8_t g, uint8_t b) {
    const float dx = x1 - x0, dy = y1 - y0, l = SDL_sqrtf(dx * dx + dy * dy);
    if (l <= 0) return;
    const float nx = -dy / l, ny = dx / l;
    for (float k = -t * 0.5f; k <= t * 0.5f; k += 0.5f)
        acr_hud_line(x0 + nx * k, y0 + ny * k, x1 + nx * k, y1 + ny * k, r, g, b, 255);
}

void vs_soft_dot(float cx, float cy, float rad, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!g_soft_tex) {
        static uint8_t px[32 * 32 * 4];
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const float d = SDL_sqrtf((x - 15.5f) * (x - 15.5f) + (y - 15.5f) * (y - 15.5f)) / 16.0f;
                const float k = d >= 1 ? 0 : (1 - d) * (1 - d);
                uint8_t *q = px + (y * 32 + x) * 4;
                q[0] = q[1] = q[2] = 255;
                q[3] = (uint8_t)(255 * k);
            }
        g_soft_tex = acr_render_texture(px, 32, 32, 0);
    }
    acr_hud_sprite(g_soft_tex, 32, 32, 0, 0, 32, 32, cx - rad, cy - rad, rad * 2, rad * 2, r, g, b, a, ACR_HUD_ALPHA);
}

/* ---- the opponents' emblems (acr-import: arena/<AA-name>/<RR>-emblem.tim) ---- */

typedef struct emblem_find { int arena; char dir[1100]; } emblem_find;
static SDL_EnumerationResult SDLCALL emblem_dir_cb(void *user, const char *dirname, const char *fname) {
    emblem_find *f = user;
    if (SDL_atoi(fname) == f->arena && fname[0] >= '0' && fname[0] <= '9' && fname[2] == '-') {
        const size_t n = SDL_strlen(dirname);
        SDL_snprintf(f->dir, sizeof f->dir, "%s%s%s", dirname, n && (dirname[n - 1] == '/' || dirname[n - 1] == '\\') ? "" : "/", fname);
        return SDL_ENUM_SUCCESS;
    }
    return SDL_ENUM_CONTINUE;
}

/* A 4-bit 64 x 64 TIM (the game's emblems) to a texture; colour 0 is
 * see-through (AC1Recomp moa_screen_emblem_from_tim). 0 = none. */
static uint32_t emblem_load(const char *import_dir, int arena, int rank) {
    emblem_find f = { arena, "" };
    char path[1400];
    SDL_snprintf(path, sizeof path, "%s/arena", import_dir);
    SDL_EnumerateDirectory(path, emblem_dir_cb, &f);
    if (!f.dir[0]) return 0;
    SDL_snprintf(path, sizeof path, "%s/%02d-emblem.tim", f.dir, rank);
    size_t n = 0;
    uint8_t *d = SDL_LoadFile(path, &n);
    if (!d) return 0;
#define RD32(o) ((uint32_t)d[o] | (uint32_t)d[(o) + 1] << 8 | (uint32_t)d[(o) + 2] << 16 | (uint32_t)d[(o) + 3] << 24)
#define RD16(o) ((uint32_t)d[o] | (uint32_t)d[(o) + 1] << 8)
    uint32_t tex = 0;
    if (n >= 20 && RD32(0) == 0x10u && (RD32(4) & 3u) == 0 && (RD32(4) & 8u)) {
        uint32_t off = 8;
        const uint32_t clen = RD32(off);
        uint8_t pal[16][4];
        if (off + 12u + 32u <= n && clen >= 44u) {
            for (int i = 0; i < 16; ++i) {
                const uint32_t v = RD16(off + 12u + 2u * (uint32_t)i);
                const uint32_t r = (v & 31u) << 3, g = ((v >> 5) & 31u) << 3, b = ((v >> 10) & 31u) << 3;
                pal[i][0] = (uint8_t)(r | r >> 5); pal[i][1] = (uint8_t)(g | g >> 5); pal[i][2] = (uint8_t)(b | b >> 5);
                pal[i][3] = v ? 255 : 0;
            }
            off += clen;
            if (off + 12u + 2048u <= n && RD16(off + 8u) * 4u == 64u && RD16(off + 10u) == 64u) {
                static uint8_t px[64 * 64 * 4];
                for (uint32_t i = 0; i < 2048u; ++i) {
                    const uint8_t b = d[off + 12u + i];
                    SDL_memcpy(px + 8 * i, pal[b & 15u], 4);
                    SDL_memcpy(px + 8 * i + 4, pal[b >> 4], 4);
                }
                tex = acr_render_texture(px, 64, 64, 1);
            }
        }
    }
#undef RD32
#undef RD16
    SDL_free(d);
    return tex;
}

/* Kept once loaded: 9 arenas, ranks up to 50 (the import's limits). */
#define EMBLEM_ARENAS 10   /* 0-8 Master of Arena's, 9 Project Phantasma's */
#define EMBLEM_RANKS  51
static uint32_t g_emblem[EMBLEM_ARENAS][EMBLEM_RANKS];
static uint8_t g_emblem_tried[EMBLEM_ARENAS][EMBLEM_RANKS];

uint32_t vs_emblem(const char *import_dir, int arena, int rank) {
    if (!import_dir || arena < 0 || arena >= EMBLEM_ARENAS || rank < 1 || rank >= EMBLEM_RANKS) return 0;
    if (!g_emblem_tried[arena][rank]) {
        g_emblem_tried[arena][rank] = 1;
        g_emblem[arena][rank] = emblem_load(import_dir, arena, rank);
    }
    return g_emblem[arena][rank];
}

void vs_emblem_draw(uint32_t tex, float x, float y, float w, float h, float es, float off_x, uint8_t alpha) {
    if (!tex) return;
    const float ex = x + (w - es) * 0.5f + off_x, ey = y + (h - es) * 0.5f - h * 0.06f;
    /* clipped to the box: only the part inside is drawn */
    const float u0 = ex < x ? (x - ex) / es * 64 : 0, u1 = ex + es > x + w ? (x + w - ex) / es * 64 : 64;
    const float v0 = ey < y ? (y - ey) / es * 64 : 0, v1 = ey + es > y + h ? (y + h - ey) / es * 64 : 64;
    acr_hud_sprite(tex, 64, 64, u0, v0, u1 - u0, v1 - v0, ex + u0 / 64 * es, ey + v0 / 64 * es,
                   (u1 - u0) / 64 * es, (v1 - v0) / 64 * es, 255, 255, 255, alpha, ACR_HUD_ALPHA);
}

/* ---- the screen ---- */

void vs_open(const vs_setup *s) {
    rogue_sound_build();
    V.s = *s;
    if (V.s.shots < 0) V.s.shots = 0;
    if (V.s.shots > 3) V.s.shots = 3;
    if (V.s.count <= 0 || V.s.count > 5) V.s.count = 5;
    V.t = 0;
    V.shut = -1;
    V.shots = V.door = 0;
    V.open = 1;
}

void vs_skip(void) { if (V.shut < 0) V.shut = 0; }
int vs_closing(void) { return V.shut >= 0; }
int vs_due(void) { return V.open && V.shut >= VS_FADE_AT + VS_FADE_LEN; }

/* One side's box: the colour gradient, the emblem behind, the dark foot, the AC. */
static void panel(float x, float y, float w, float h, const int parts[SLOT_COUNT], const uint8_t colours[4][3],
                  const uint8_t tint[3], uint32_t emblem, int side) {
    vs_gradient(x, y, w, h, tint, 0.55f, 0.15f);
    vs_emblem_draw(emblem, x, y, w, h, h * 1.15f, side < 0 ? w * 0.10f : -w * 0.10f, 158);
    for (int i = 0; i < 12; ++i) {                         /* darker toward the foot */
        const float t = ((float)i + 0.5f) / 12.0f;
        acr_hud_rect(x, y + h * (0.55f + 0.45f * (float)i / 12), w, h * 0.45f / 12 + 0.5f, 0, 0, 0, (uint8_t)(150 * t), 1);
    }
    vs_draw_ac(parts, colours, x, y, w, h, 0.55f, side < 0 ? -0.35f : 0.35f, 0, 1.0f);
}

/* The hangar shutters closing (t seconds since they started). */
static void shutters(float hud_w, float t, const uint8_t band[3]) {
    float k;
    if (t < VS_MEET) { k = t / VS_MEET; k *= k; }
    else if (t < VS_MEET + 0.10f) k = 1.0f - 0.025f * SDL_sinf((t - VS_MEET) / 0.10f * 3.14159265f);
    else k = 1.0f;
    const float half = 240.0f * k;
    for (int top = 1; top >= 0; --top) {
        const float y0 = top ? 0 : 480 - half, y1 = top ? half : 480;
        acr_hud_rect(0, y0, hud_w, y1 - y0, 34, 35, 40, 255, 1);
        for (float yy = top ? y1 - 17 - 32 : y0 + 17 + 32; top ? yy > y0 : yy < y1; yy += top ? -32.0f : 32.0f)
            acr_hud_rect(0, yy - 1, hud_w, 2, 22, 22, 26, 255, 1);
        if (half > 17) {                                   /* the hazard stripe on the leading edge */
            const float ey = top ? y1 - 17 : y0;
            for (float x = -40; x < hud_w + 40; x += 27) {
                for (float s = 0; s < 13; s += 1)
                    acr_hud_line(x + s, ey + 17, x + s + 17, ey, band[0], band[1], band[2], 255);
            }
            acr_hud_rect(0, ey, hud_w, 17, 0, 0, 0, 255, 0);
            acr_hud_rect(0, ey + 1, hud_w, 15, 0, 0, 0, 255, 0);
        }
    }
    if (t >= VS_MEET && t < VS_MEET + 0.12f)
        acr_hud_rect(0, 238, hud_w, 4, 255, 240, 200, (uint8_t)(255 * (1 - (t - VS_MEET) / 0.12f)), 1);
}

void vs_draw(float hud_w, int elapsed_ms) {
    const float dt = (float)elapsed_ms / 1000.0f;
    V.t += dt;
    const float t = V.t;
    if (V.shut < 0 && t >= VS_C0 + (float)V.s.count * VS_STEP) V.shut = 0;
    else if (V.shut >= 0) V.shut += dt;
    const float sh = V.shut;
    /* the sounds: a shot with each slash, the shutters' slam on the meet */
    if (sh < 0) {
        while (V.shots < V.s.shots && t >= VS_C0 + (float)(2 + (3 - V.s.shots) + V.shots) * VS_STEP) {
            sfx_ui(V.shots + 1 == V.s.shots ? SFX_VS_SHOT_LAST : SFX_VS_SHOT);
            ++V.shots;
        }
    } else if (!V.door && sh >= VS_MEET - VS_DOOR_PEAK) {
        V.door = 1;
        sfx_ui(SFX_VS_DOOR);
    }
    const rogue_fight *g = V.s.fight;
    const uint8_t *band = V.s.band;
    const float cx = hud_w * 0.5f, half = SDL_min(427.0f, hud_w * 0.5f), sx = half / 427.0f;
    /* the backdrop dimmed to 42 % */
    acr_hud_rect(0, 0, hud_w, 480, 0, 0, 0, 148, 1);
    acr_hud_rect(0, 0, hud_w, 39, 0, 0, 0, 255, 1);
    acr_hud_rect(0, 39, hud_w, 2, band[0], band[1], band[2], 255, 1);
    if (V.s.mode) vs_text_at(cx - half + 20, 15, V.s.mode, 10, 150, 156, 170);
    vs_text_mid(cx, 11, "NEXT BATTLE", 16, 236, 236, 236);
    if (V.s.right) vs_text_right(cx + half - 20, 15, V.s.right, 10, band[0], band[1], band[2]);
    const float pw = 280 * sx, ph = 200, py = 56, lx = cx - half + 43 * sx, rx = cx + half - 43 * sx - pw;
    char s[64];
    if (g) {
        panel(lx, py, pw, ph, g->parts[0], g->colours[0], vs_blue, V.s.emblem[0], -1);
        panel(rx, py, pw, ph, g->parts[1], g->colours[1], vs_red, V.s.emblem[1], 1);
    }
    vs_frame(lx, py, pw, ph, vs_blue, 2);
    vs_frame(rx, py, pw, ph, vs_red, 2);
    for (int k = 0; k < 2; ++k) {
        const int rank = V.s.rank[k];
        if (rank <= 0) continue;
        SDL_snprintf(s, sizeof s, "RANK %d", rank);
        const float bx = k ? rx + pw - 7 - vs_text_w(s, 10) : lx + 7;
        acr_hud_rect(bx - 4, py + 4, vs_text_w(s, 10) + 7, 18, 0, 0, 0, 170, 1);
        vs_text_at(bx, py + 9, s, 10, 255, 196, 72);
    }
    /* names: pilot big, AC under it */
    if (g) {
        char pilot[2][24], ac[2][24];
        for (int k = 0; k < 2; ++k) {
            const char *n = g->name[k], *slash = SDL_strstr(n, " / ");
            if (slash) {
                SDL_snprintf(ac[k], sizeof ac[k], "%.*s", (int)(slash - n), n);
                SDL_strlcpy(pilot[k], slash + 3, sizeof pilot[k]);
            } else { SDL_strlcpy(ac[k], n, sizeof ac[k]); pilot[k][0] = 0; }
        }
        vs_text_at(lx + 1, py + ph + 10, pilot[0][0] ? pilot[0] : ac[0], 16, 236, 236, 236);
        if (pilot[0][0]) vs_text_at(lx + 3, py + ph + 34, ac[0], 10, vs_blue[0], vs_blue[1], vs_blue[2]);
        vs_text_right(rx + pw - 1, py + ph + 10, pilot[1][0] ? pilot[1] : ac[1], 16, 236, 236, 236);
        if (pilot[1][0]) vs_text_right(rx + pw - 3, py + ph + 34, ac[1], 10, vs_red[0], vs_red[1], vs_red[2]);
    }
    if (V.s.for_rank > 0) {
        vs_text_mid(cx, 64, "FOR RANK", 10, 150, 156, 170);
        SDL_snprintf(s, sizeof s, "%d", V.s.for_rank);
        vs_text_mid(cx, 82, s, 27, 255, 196, 72);
    }
    vs_text_mid(cx, 140, "VS", 37, 235, 90, 80);
    if (V.s.tag) vs_text_mid(cx, 182, V.s.tag, 10, 170, 176, 190);
    /* the bottom band, filled by the mode */
    acr_hud_rect(0, 325, hud_w, 155, 0, 0, 0, 255, 1);
    acr_hud_rect(0, 323, hud_w, 2, band[0], band[1], band[2], 255, 1);
    if (V.s.strip) V.s.strip(hud_w, sx, t, band);
    /* the count */
    const int count = sh < 0 && t >= VS_C0 ? V.s.count - (int)((t - VS_C0) / VS_STEP) : 0;
    if (count > 0) {
        SDL_snprintf(s, sizeof s, "%d", count);
        vs_text_mid(cx, 205, s, 27, 255, 196, 72);
        SDL_snprintf(s, sizeof s, "UNITS DEPLOY IN %d", count);
        vs_text_mid(cx, 268, s, 10, 236, 236, 236);
        if (V.s.stage) vs_text_mid(cx, 290, V.s.stage, 10, 150, 156, 170);
    } else if (sh < 0) {
        const float k = 0.55f + 0.45f * (0.5f + 0.5f * SDL_sinf(t * 3.0f));
        vs_text_mid(cx, 210, "STANDBY", 16, (uint8_t)(255 * k), (uint8_t)(200 * k), (uint8_t)(80 * k));
    }
    if (sh < 0 && V.s.hint) vs_text_mid(cx, 462, V.s.hint, 8, 120, 135, 145);
    if (sh >= 0) shutters(hud_w, sh, band);
    const float black = sh < 0 ? 0 : SDL_clamp((sh - VS_FADE_AT) / VS_FADE_LEN, 0.0f, 1.0f);
    if (black > 0) acr_hud_rect(0, 0, hud_w, 480, 0, 0, 0, (uint8_t)(255 * black), 1);
}
