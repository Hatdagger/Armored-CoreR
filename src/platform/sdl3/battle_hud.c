/* Master of Arena's battle HUD (docs/research/gen1-hud.md). Positions are
 * the PS1's 320 x 240 screen, measured against the game; pictures are the
 * game's own HUD sheet (RTIM 61, VRAM 896,256: four one-colour layers) and
 * text sheet (VRAM 832,256). */
#include "battle_hud.h"

#include <SDL3/SDL.h>

#include "../../model/acr_png.h"
#include "../../render/acr_render.h"

static uint32_t g_layer[4], g_font;
static int g_ready;

static uint32_t load_png(const char *path) {
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

int battle_hud_load(const char *import_dir) {
    char path[1024];
    g_ready = 1;
    for (int i = 0; i < 4; ++i) {
        SDL_snprintf(path, sizeof path, "%s/hud/hud-%d.png", import_dir, i);
        if (!g_layer[i]) g_layer[i] = load_png(path);
        if (!g_layer[i]) g_ready = 0;
    }
    SDL_snprintf(path, sizeof path, "%s/hud/font.png", import_dir);
    if (!g_font) g_font = load_png(path);
    if (!g_font) g_ready = 0;
    return g_ready;
}

int battle_hud_ready(void) { return g_ready; }

/* ---- layout transform ---------------------------------------------------- */

static float g_s, g_ox[3];

static void set_frame(float hud_w, int frame) {
    g_s = hud_w >= 640.0f ? 2.0f : hud_w / 320.0f;    /* PS1 pixel -> HUD grid */
    const float w = 320.0f * g_s;
    g_ox[1] = (hud_w - w) * 0.5f;
    g_ox[0] = frame == BATTLE_HUD_FRAME_WIDE ? 0.0f : g_ox[1];
    g_ox[2] = frame == BATTLE_HUD_FRAME_WIDE ? hud_w - w : g_ox[1];
}

void battle_hud_point(float hud_w, int frame, int anchor, float x, float y, float *hx, float *hy) {
    set_frame(hud_w, frame);
    *hx = g_ox[anchor + 1] + x * g_s;
    *hy = y * g_s;
}

/* A piece of a 256 x 256 sheet at a PS1 screen position. */
static void spr(int anchor, uint32_t tex, int u, int v, int w, int h, float x, float y,
                uint8_t r, uint8_t g, uint8_t b, uint8_t a, int blend) {
    acr_hud_sprite(tex, 256, 256, (float)u, (float)v, (float)w, (float)h, g_ox[anchor + 1] + x * g_s, y * g_s,
                   (float)w * g_s, (float)h * g_s, r, g, b, a, blend);
}

static void box(int anchor, float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int blend) {
    acr_hud_sprite(0, 0, 0, 0, 0, 0, 0, g_ox[anchor + 1] + x * g_s, y * g_s, w * g_s, h * g_s, r, g, b, a, blend);
}

/* ---- text ------------------------------------------------------------------ */

/* Both small fonts share this order on their first row; letters A-Z are on
 * the next row (6 x 7 font: v 0 / 8, 4 x 5 font: v 24 / 30). */
static const char k_symbols[] = "0123456789+-*/.,?!#%()_|:;<>";

static int glyph(int big, char c, int *u, int *v) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    const char *p = c ? SDL_strchr(k_symbols, c) : NULL;
    const int adv = big ? 6 : 4;
    if (p) { *u = (int)(p - k_symbols) * adv; *v = big ? 0 : 24; return 1; }
    if (c >= 'A' && c <= 'Z') { *u = (c - 'A') * adv; *v = big ? 8 : 30; return 1; }
    return 0;
}

static void text(int anchor, float x, float y, const char *s, int big, uint8_t r, uint8_t g, uint8_t b) {
    const int adv = big ? 6 : 4, h = big ? 7 : 5;
    if (!g_ready) {   /* no HUD pictures: the engine's own font, at the same spot */
        acr_hud_text(g_ox[anchor + 1] + x * g_s, y * g_s, s, r, g, b);
        return;
    }
    for (; *s; ++s, x += (float)adv) {
        int u, v;
        if (glyph(big, *s, &u, &v)) spr(anchor, g_layer[0], u, v, adv, h, x, y, r, g, b, 255, ACR_HUD_ALPHA);
    }
}

/* Right-aligned: the text ends at x. */
static void text_r(int anchor, float x, float y, const char *s, int big, uint8_t r, uint8_t g, uint8_t b) {
    text(anchor, x - (float)SDL_strlen(s) * (big ? 6 : 4), y, s, big, r, g, b);
}

void battle_hud_text(float hud_w, int frame, int anchor, float x, float y, const char *s, int big,
                     uint8_t r, uint8_t g, uint8_t b) {
    set_frame(hud_w, frame);
    text(anchor, x, y, s, big, r, g, b);
}

/* The text sheet (font.png): letters 6 wide, 10 tall; A-Z then a-p on the
 * row at v 14, q-z at v 27, digits and signs at v 0. */
static void name_text(int anchor, float x, float y, const char *s, float spacing, uint8_t r, uint8_t g, uint8_t b,
                      uint8_t a) {
    for (; *s; ++s, x += spacing) {
        const char c = *s;
        int u, v;
        const char *p = c ? SDL_strchr(k_symbols, c) : NULL;
        if (c >= 'A' && c <= 'Z') { u = (c - 'A') * 6; v = 14; }
        else if (c >= 'a' && c <= 'p') { u = 156 + (c - 'a') * 6; v = 14; }
        else if (c >= 'q' && c <= 'z') { u = (c - 'q') * 6; v = 27; }
        else if (p) { u = (int)(p - k_symbols) * 6; v = 0; }
        else continue;
        spr(anchor, g_font, u, v, 6, 12, x, y, r, g, b, a, ACR_HUD_ALPHA);
    }
}

/* ---- the HUD --------------------------------------------------------------- */

#define L (-1)
#define M 0
#define R 1
#define GREEN 58, 214, 58

void battle_hud_draw(const battle_hud_state *s, float hud_w, int frame) {
    set_frame(hud_w, frame);
    char line[64];

    /* Energy: the bar (green above the red zone, brown below it; all brown
     * while drained), the gauge frame with F / M / E over it, "ENERGY". */
    {
        const float top = 37, height = 157;
        const float cap = (float)(s->energy_max > 0 ? s->energy_max : 1);
        float fill = (float)s->energy / cap, red = (float)s->red_zone / cap;
        if (fill < 0) fill = 0;
        if (fill > 1) fill = 1;
        if (red > fill) red = fill;
        const float bottom = top + height;
        if (s->drained) box(L, 8, bottom - fill * height, 8, fill * height, 160, 80, 48, 255, ACR_HUD_ALPHA);
        else {
            box(L, 8, bottom - fill * height, 8, (fill - red) * height, 64, 144, 56, 255, ACR_HUD_ALPHA);
            box(L, 8, bottom - red * height, 8, red * height, 160, 80, 48, 255, ACR_HUD_ALPHA);
        }
        if (g_ready) spr(L, g_layer[0], 4, 72, 16, 168, 12, 36, GREEN, 255, ACR_HUD_ALPHA);
        text(L, 8, 28, "ENERGY", 0, GREEN);
    }

    /* AP: the big digits (16 apart, ending at x 96), and the enemy's AP
     * under them. */
    {
        int ap = s->ap < 0 ? 0 : s->ap;
        SDL_snprintf(line, sizeof line, "%d", ap);
        const int n = (int)SDL_strlen(line);
        for (int i = 0; i < n; ++i) {
            const float x = 96.0f - 16.0f * (float)(n - i);
            if (g_ready) spr(L, g_layer[0], 24 + 16 * (line[i] - '0'), 113, 16, 22, x, 23, GREEN, 255, ACR_HUD_ALPHA);
        }
        if (!g_ready) text(L, 32, 30, line, 1, GREEN);
        if (s->enemy_ap >= 0) {
            text(L, 40, 50, "ENEMY LIMIT", 0, GREEN);
            SDL_snprintf(line, sizeof line, "%d", s->enemy_ap);
            text_r(L, 96, 56, line, 1, GREEN);
        }
    }

    /* Compass: a darker band, and the strip (256 texels a turn: S W N E)
     * scrolled so the facing sits in the middle. */
    {
        const float x0 = 97, x1 = 225, cx = 161;
        box(M, 96, 16, 129, 14, 0, 0, 0, 30, ACR_HUD_ALPHA);
        if (g_ready) {
            const int centre = (128 + s->heading / 16) & 255;
            float x = x0;
            int u = (int)((float)centre - (cx - x0)) & 255;
            while (x < x1) {
                const float w = SDL_min(x1 - x, (float)(256 - u));
                spr(M, g_layer[0], u, 244, (int)w, 4, x, 17, 48, 128, 40, 255, ACR_HUD_ALPHA);    /* ticks */
                spr(M, g_layer[0], u, 250, (int)w, 5, x, 23, 42, 198, 42, 255, ACR_HUD_ALPHA);    /* S W N E */
                x += w;
                u = 0;
            }
        }
    }

    /* REC and the line under it (top right), only while a replay is being
     * recorded (arena and VS fights). */
    if (s->recording) {
        if (g_ready) spr(R, g_layer[1], 50, 72, 78, 8, 228, 20, 80, 255, 170, 255, ACR_HUD_ALPHA);
        else text(R, 232, 20, "REC", 1, 80, 255, 170);
        box(R, 229, 28, 80, 1, 120, 16, 16, 255, ACR_HUD_ALPHA);
    }

    /* Radar: the frame, the disc (its gap is the view ahead), the enemy. */
    if (s->radar_range > 0) {
        const float fx0 = 261, fy0 = 58, fx1 = 307, fy1 = 101;
        box(R, fx0, fy0, fx1 - fx0, 1, 100, 255, 170, 220, ACR_HUD_ALPHA);
        box(R, fx0, fy1, fx1 - fx0 + 1, 1, 100, 255, 170, 220, ACR_HUD_ALPHA);
        box(R, fx0, fy0, 1, fy1 - fy0, 100, 255, 170, 220, ACR_HUD_ALPHA);
        box(R, fx1, fy0, 1, fy1 - fy0, 100, 255, 170, 220, ACR_HUD_ALPHA);
        if (g_ready) spr(R, g_layer[0], 160, 16, 48, 48, 260, 56, 64, 120, 24, 255, ACR_HUD_ADD);
        else box(R, 260, 56, 48, 48, 64, 120, 24, 255, ACR_HUD_ADD);
        if (s->enemy_on_radar) {
            /* Forward is up: forward (sin, cos), right (cos, -sin). */
            const float a = (float)s->heading * (6.2831853f / 4096.0f);
            const float sn = SDL_sinf(a), cs = SDL_cosf(a);
            const float fwd = (float)s->enemy_dx * sn + (float)s->enemy_dz * cs;
            const float rgt = (float)s->enemy_dx * cs - (float)s->enemy_dz * sn;
            const float k = 23.0f / (float)s->radar_range;
            const float bx = rgt * k, by = -fwd * k;
            if (bx * bx + by * by <= 23.0f * 23.0f) box(R, 284 + bx - 1, 80 + by - 1, 2, 2, 255, 60, 50, 255, ACR_HUD_ALPHA);
        }
    }

    /* Weapons: the selected one in yellow with its ammo, the others in
     * green underneath, right-aligned under the radar. */
    if (s->weapons > 0 && s->current < s->weapons) {
        const battle_hud_weapon *w = &s->weapon[s->current];
        text_r(R, 312, 112, w->name ? w->name : "", 1, 255, 255, 0);
        if (w->ammo_max > 0) {
            SDL_snprintf(line, sizeof line, "%3d/%4d", w->ammo, w->ammo_max);
            text_r(R, 312, 120, line, 1, 255, 255, 0);
        }
        float y = 130;
        for (int i = 0; i < s->weapons; ++i) {
            if (i == s->current) continue;
            const battle_hud_weapon *o = &s->weapon[i];
            if (o->ammo_max > 0) SDL_snprintf(line, sizeof line, "%s %5d", o->name ? o->name : "", o->ammo);
            else SDL_snprintf(line, sizeof line, "%s", o->name ? o->name : "");
            text_r(R, 311, y, line, 0, GREEN);
            y += 7;
        }
    }

    /* Warning boxes, stacked bottom right (HIT green, the rest orange). */
    {
        float y = 184;
        for (int k = 0; k < 6; ++k) {
            if (!(s->messages & (1u << k))) continue;
            const int hit = k == 0;
            if (g_ready) spr(R, g_layer[0], 72, 40 + 8 * k, 72, 7, 240, y, hit ? 90 : 240, hit ? 230 : 130, hit ? 90 : 70,
                             255, ACR_HUD_ALPHA);
            else {
                static const char *const k_msg[6] = { "HIT", "DAMAGED", "LEAVING AREA", "ARMOR LOW", "CHARGING", "SYSTEM ERROR" };
                text(R, 244, y, k_msg[k], 1, hit ? 90 : 240, hit ? 230 : 130, hit ? 90 : 70);
            }
            y += 8;
        }
    }

    /* The AC's name, bottom left, letters spread wide. */
    if (s->ac_name && s->ac_name[0]) {
        if (g_ready) name_text(L, 13, 214, s->ac_name, 16, 170, 210, 215, 200);
        else text(L, 13, 216, s->ac_name, 1, 170, 210, 215);
    }
}

void battle_hud_word(float hud_w, int frame, int word, float y, uint8_t r, uint8_t g, uint8_t b) {
    static const int k_w[4] = { 120, 158, 184, 254 };
    static const char *const k_text[4] = { "WIN", "LOSE", "DRAW", "TIME UP" };
    if (word < 0 || word > 3) return;
    set_frame(hud_w, frame);
    if (g_ready) spr(M, g_layer[2], 0, 1 + 64 * word, k_w[word], 62, 160.0f - (float)k_w[word] * 0.5f, y, r, g, b, 255,
                     ACR_HUD_ALPHA);
    else acr_hud_text(g_ox[1] + (160.0f - 4.0f * (float)SDL_strlen(k_text[word])) * g_s, (y + 27) * g_s, k_text[word], r, g, b);
}
