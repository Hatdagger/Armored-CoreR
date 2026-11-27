/* Master of Arena's battle HUD (docs/research/gen1-hud.md). Positions are
 * the PS1's 320 x 240 screen, measured against the game; pictures are the
 * game's own HUD sheet (RTIM 61, VRAM 896,256: four one-colour layers) and
 * text sheet (VRAM 832,256). */
#include "battle_hud.h"
#include "battle_warn.h"

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
     * while drained), the gauge frame with F / M / E over it, "ENERGY".
     * The game (arena 0x80057020) draws it in whole pixels, 157 to the full
     * bar, straight from the energy each frame: no easing, so it moves in
     * steps of capacity / 157 at the game's 22 frames a second (the
     * energy only changes on its ticks). */
    {
        const int top = 37, height = BATTLE_HUD_BAR;
        const long long cap = s->energy_max > 0 ? s->energy_max : 1;
        long long fill = (long long)(s->energy < 0 ? 0 : s->energy) * height / cap;
        long long red = (long long)s->red_zone * height / cap;
        if (fill > height) fill = height;
        if (red > fill) red = fill;
        const int bottom = top + height;
        if (s->drained) box(L, 8, (float)(bottom - fill), 8, (float)fill, 160, 80, 48, 255, ACR_HUD_ALPHA);
        else {
            box(L, 8, (float)(bottom - fill), 8, (float)(fill - red), 64, 144, 56, 255, ACR_HUD_ALPHA);
            box(L, 8, (float)(bottom - red), 8, (float)red, 160, 80, 48, 255, ACR_HUD_ALPHA);
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
        /* The digits are the AP as it is (arena 0x800572A4: (AP x 4 + 3)
         * / 4 each frame, no counting down). They turn red at a tenth of
         * the AC's AP or below, as the game (arena 0x80057280, the same
         * test as ARMOR LOW). */
        const int low = battle_warn_ap_low(ap, s->ap_max);
        const uint8_t ar = low ? 230 : 58, ag = low ? 48 : 214, ab = low ? 40 : 58;
        for (int i = 0; i < n; ++i) {
            const float x = 96.0f - 16.0f * (float)(n - i);
            if (g_ready) spr(L, g_layer[0], 24 + 16 * (line[i] - '0'), 113, 16, 22, x, 23, ar, ag, ab, 255, ACR_HUD_ALPHA);
        }
        if (!g_ready) text(L, 32, 30, line, 1, ar, ag, ab);
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
     * recorded (arena and VS fights). The game has it at y 20, where the
     * round clock now sits (above the radar, the user's layout), so it
     * moves up 12 lines to clear it. */
    if (s->recording) {
        if (g_ready) spr(R, g_layer[1], 50, 72, 78, 8, 228, 8, 80, 255, 170, 255, ACR_HUD_ALPHA);
        else text(R, 232, 8, "REC", 1, 80, 255, 170);
        box(R, 229, 16, 80, 1, 120, 16, 16, 255, ACR_HUD_ALPHA);
    }

    /* Round clock: above the radar, in the AP's big digits (MM:SS, the
     * colon an 8-wide cell), right-aligned with the radar frame. */
    if (s->time_ms >= 0) {
        const int secs = (s->time_ms + 999) / 1000;
        SDL_snprintf(line, sizeof line, "%02d:%02d", secs / 60 % 100, secs % 60);
        float x = 308;
        for (int i = (int)SDL_strlen(line) - 1; i >= 0; --i) {
            const int colon = line[i] == ':';
            x -= colon ? 8 : 16;
            if (g_ready) spr(R, g_layer[0], colon ? 184 : 24 + 16 * (line[i] - '0'), 113, colon ? 8 : 16, 22, x, 23, GREEN, 255,
                             ACR_HUD_ALPHA);
        }
        if (!g_ready) text_r(R, 308, 30, line, 1, GREEN);
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
        /* Forward is up: forward (sin, cos), right (cos, -sin). */
        const float a = (float)s->heading * (6.2831853f / 4096.0f);
        const float sn = SDL_sinf(a), cs = SDL_cosf(a);
        const float k = 23.0f / (float)s->radar_range;
        for (int i = 0; i < s->others && i < BATTLE_HUD_MAX_OTHERS; ++i) {   /* co-op: the others first */
            const float fwd = (float)s->other[i].dx * sn + (float)s->other[i].dz * cs;
            const float rgt = (float)s->other[i].dx * cs - (float)s->other[i].dz * sn;
            const float bx = rgt * k, by = -fwd * k;
            if (bx * bx + by * by > 23.0f * 23.0f) continue;
            if (s->other[i].ally) box(R, 284 + bx - 1, 80 + by - 1, 2, 2, 90, 230, 255, 255, ACR_HUD_ALPHA);
            else box(R, 284 + bx - 1, 80 + by - 1, 2, 2, 255, 140, 60, 255, ACR_HUD_ALPHA);
        }
        if (s->enemy_on_radar) {
            const float fwd = (float)s->enemy_dx * sn + (float)s->enemy_dz * cs;
            const float rgt = (float)s->enemy_dx * cs - (float)s->enemy_dz * sn;
            const float bx = rgt * k, by = -fwd * k;
            if (bx * bx + by * by <= 23.0f * 23.0f) box(R, 284 + bx - 1, 80 + by - 1, 2, 2, 255, 60, 50, 255, ACR_HUD_ALPHA);
        }
    }

    /* Weapons: the selected one in yellow with its ammo, the others in
     * green underneath, right-aligned under the radar. A weapon out of
     * ammo shows its name and count in red (VS 0x80056D98: colour 1 when
     * slot +0x10, the ammo, is 0, for the selected weapon and the rest). */
    if (s->weapons > 0 && s->current < s->weapons) {
        const battle_hud_weapon *w = &s->weapon[s->current];
        const int empty = w->ammo_max > 0 && w->ammo <= 0;
        const uint8_t cr = 255, cg = empty ? 48 : 255, cb = empty ? 40 : 0;
        text_r(R, 312, 112, w->name ? w->name : "", 1, cr, cg, cb);
        if (w->ammo_max > 0) {
            SDL_snprintf(line, sizeof line, "%3d/%4d", w->ammo, w->ammo_max);
            text_r(R, 312, 120, line, 1, cr, cg, cb);
        }
        float y = 130;
        for (int i = 0; i < s->weapons; ++i) {
            if (i == s->current) continue;
            const battle_hud_weapon *o = &s->weapon[i];
            if (o->ammo_max > 0) SDL_snprintf(line, sizeof line, "%s %5d", o->name ? o->name : "", o->ammo);
            else SDL_snprintf(line, sizeof line, "%s", o->name ? o->name : "");
            if (o->ammo_max > 0 && o->ammo <= 0) text_r(R, 311, y, line, 0, 230, 48, 40);
            else text_r(R, 311, y, line, 0, GREEN);
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

/* ---- lock-on markers (VS 0x80057B8C guns, 0x80057E8C missiles, 0x800580EC cursor) ---- */

/* HUD icons, from the VS engine's icon table (0x80091A28, 12 bytes each:
 * clut, page, u, v, w, h). All of these are layer 0 of the HUD sheet. */
typedef struct hud_icon { uint8_t u, v, w, h; } hud_icon;
static const hud_icon k_icon_bracket = { 1, 36, 12, 8 };     /* 50: gun marker, sides */
static const hud_icon k_icon_tick    = { 14, 36, 7, 2 };     /* 51: gun marker, top and bottom */
static const hud_icon k_icon_bar     = { 22, 36, 3, 8 };     /* 52: missile marker, the four sides */
static const hud_icon k_icon_pip     = { 41, 36, 7, 9 };     /* 54: a missile lock's box */
static const hud_icon k_icon_cursor  = { 1, 46, 16, 16 };    /* 55: the missile lock cursor */

/* The game's colour table (0x8008FDC8), PS1 values: textured pieces are
 * tinted at 128 = as drawn, so they are doubled here. */
#define C_GREEN_DARK  10, 80, 10      /* 11: marker, no lock */
#define C_RED_DARK    80, 10, 10      /* 12: marker, locked */
#define C_GREEN_LINE  20, 120, 20     /* 13: leader line, no lock */
#define C_RED_LINE    120, 20, 20     /* 14: leader line, locked */
#define C_CURSOR      120, 40, 20     /* 1: missile cursor */

static uint8_t tint(int c) { return (uint8_t)(c * 2 > 255 ? 255 : c * 2); }

/* One icon `turns` quarter turns round (VS 0x80065494 modes 0-3), its top-left
 * dx, dy PS1 pixels from (hx, hy) in HUD units, drawn added to the screen. */
static void icon(const hud_icon *ic, int turns, float hx, float hy, int dx, int dy, int r, int g, int b) {
    const float w = (float)(turns & 1 ? ic->h : ic->w), h = (float)(turns & 1 ? ic->w : ic->h);
    acr_hud_sprite_turned(g_layer[0], 256, 256, ic->u, ic->v, ic->w, ic->h, hx + (float)dx * g_s, hy + (float)dy * g_s,
                          w * g_s, h * g_s, tint(r), tint(g), tint(b), 255, ACR_HUD_ADD, turns);
}

/* The leader line from the marker to its label (VS 0x80057634): three
 * points, PS1 offsets from the marker. */
static void leader(float hx, float hy, const int p[6], int r, int g, int b) {
    for (int k = 0; k < 2; ++k)
        acr_hud_line(hx + (float)p[2 * k] * g_s, hy + (float)p[2 * k + 1] * g_s,
                     hx + (float)p[2 * k + 2] * g_s, hy + (float)p[2 * k + 3] * g_s,
                     (uint8_t)r, (uint8_t)g, (uint8_t)b, 255);
}

/* Small (4 x 5) text with its top-left at PS1 offset dx, dy from (hx, hy). */
static void label(float hx, float hy, int dx, int dy, const char *t, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; t[i]; ++i) {
        int u, v;
        if (g_ready && glyph(0, t[i], &u, &v))
            acr_hud_sprite(g_layer[0], 256, 256, (float)u, (float)v, 4, 5, hx + (float)(dx + 4 * i) * g_s,
                           hy + (float)dy * g_s, 4 * g_s, 5 * g_s, r, g, b, 255, ACR_HUD_ALPHA);
    }
}

void battle_hud_lock_marker(float hud_w, int frame, float hx, float hy, int missile, int locked, int distance, int locks) {
    set_frame(hud_w, frame);
    if (!g_ready) return;
    char num[12];
    SDL_snprintf(num, sizeof num, "%d", distance < 0 ? 0 : distance);
    const char *t = locked ? "LOCK" : num;
    const int tw = 4 * (int)SDL_strlen(t);
    if (!missile) {
        /* Guns: brackets either side, ticks above and below (template 0x8008FE68). */
        const int r = locked ? 80 : 10, g = locked ? 10 : 80, b = 10;
        icon(&k_icon_bracket, 0, hx, hy, -20, -4, r, g, b);
        icon(&k_icon_bracket, 2, hx, hy, 8, -4, r, g, b);
        icon(&k_icon_tick, 0, hx, hy, -3, -11, r, g, b);
        icon(&k_icon_tick, 2, hx, hy, -3, 11, r, g, b);
        /* The label up and to the right: LOCK once locked, else the distance. */
        const int line[6] = { 8, -6, 14, -10, 14 + tw, -10 };
        if (locked) leader(hx, hy, line, C_RED_LINE); else leader(hx, hy, line, C_GREEN_LINE);
        label(hx, hy, locked ? 14 : 15, -15 - 1, t, locked ? 255 : 58, locked ? 60 : 214, locked ? 50 : 58);
        return;
    }
    /* Missiles: four bars round the target (template 0x8008FE90), the label
     * down and to the right, and a numbered box per missile locked on it. */
    const int r = locked ? 80 : 10, g = locked ? 10 : 80, b = 10;
    icon(&k_icon_bar, 0, hx, hy, -15, -4, r, g, b);
    icon(&k_icon_bar, 1, hx, hy, -4, 11, r, g, b);
    icon(&k_icon_bar, 2, hx, hy, 13, -4, r, g, b);
    icon(&k_icon_bar, 3, hx, hy, -4, -11, r, g, b);
    const int line[6] = { 16, 4, 22, 12, 22 + tw, 12 };
    if (locked) leader(hx, hy, line, C_RED_LINE); else leader(hx, hy, line, C_GREEN_LINE);
    label(hx, hy, locked ? 22 : 23, 7, t, locked ? 255 : 58, locked ? 60 : 214, locked ? 50 : 58);
    /* Lock boxes (template 0x8008FEB8): 1-3 above the marker, 4 below. */
    static const int8_t at[4][2] = { { -12, -21 }, { -3, -21 }, { 6, -21 }, { -12, 15 } };
    if (locks > 4) locks = 4;
    for (int k = 0; k < locks; ++k) {
        icon(&k_icon_pip, 0, hx, hy, at[k][0], at[k][1], C_RED_DARK);
        int u, v;
        if (glyph(0, (char)('1' + k), &u, &v)) {   /* icons 57-60: the 4 x 5 digits, 3 wide */
            const hud_icon dg = { (uint8_t)u, (uint8_t)v, 3, 5 };
            icon(&dg, 0, hx, hy, at[k][0] + 2, at[k][1] + 2, C_GREEN_DARK);
        }
    }
}

void battle_hud_missile_cursor(float hud_w, int frame, float hx, float hy) {
    set_frame(hud_w, frame);
    if (g_ready) icon(&k_icon_cursor, 0, hx, hy, -8, -8, C_CURSOR);
}
