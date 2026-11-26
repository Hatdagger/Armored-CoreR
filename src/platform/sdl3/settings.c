#include "settings.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120

game_settings g_settings = { 0, 1, 0, 1, 7, 7 };

static const int k_sizes[SETTINGS_WINDOW_SIZES][2] = {
    { 960, 540 }, { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 },
};

void settings_window_size(int index, int *w, int *h) {
    if (index < 0 || index >= SETTINGS_WINDOW_SIZES) index = 1;
    *w = k_sizes[index][0];
    *h = k_sizes[index][1];
}

void settings_defaults(void) {
    const game_settings d = { 0, 1, 0, 1, 7, 7 };
    g_settings = d;
}

/* ---- file ---- */

static void file_path(char *out, size_t size) {
    const char *env = SDL_getenv("ACR_SETTINGS");
    if (env && env[0]) { SDL_strlcpy(out, env, size); return; }
    char *pref = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
    SDL_snprintf(out, size, "%ssettings.toml", pref ? pref : "");
    SDL_free(pref);
}

static int file_value(const char *t, const char *key, int fallback, int lo, int hi) {
    const size_t n = SDL_strlen(key);
    for (const char *p = t; p && *p; ) {
        if (!SDL_strncmp(p, key, n) && (p[n] == ' ' || p[n] == '=')) {
            const char *v = p + n;
            while (*v == ' ' || *v == '=') ++v;
            const int x = SDL_atoi(v);
            return x >= lo && x <= hi ? x : fallback;
        }
        p = SDL_strchr(p, '\n');
        if (p) ++p;
    }
    return fallback;
}

void settings_load(void) {
    char path[1200];
    file_path(path, sizeof path);
    char *t = SDL_LoadFile(path, NULL);
    if (!t) return;
    game_settings *s = &g_settings;
    s->fullscreen = file_value(t, "fullscreen", s->fullscreen, 0, 1);
    s->window = file_value(t, "window_size", s->window, 0, SETTINGS_WINDOW_SIZES - 1);
    s->hud_wide = file_value(t, "hud_wide", s->hud_wide, 0, 1);
    s->smooth_anim = file_value(t, "smooth_animation", s->smooth_anim, 0, 1);
    s->fx_volume = file_value(t, "fight_volume", s->fx_volume, 0, 10);
    s->menu_volume = file_value(t, "menu_volume", s->menu_volume, 0, 10);
    SDL_free(t);
}

int settings_save(void) {
    char path[1200], text[1024];
    file_path(path, sizeof path);
    int w, h;
    settings_window_size(g_settings.window, &w, &h);
    const int n = SDL_snprintf(text, sizeof text,
        "# Armored-CoreR settings (SYSTEM > SETTINGS writes this).\n"
        "fullscreen       = %d   # 0 window, 1 fullscreen\n"
        "window_size      = %d   # %dx%d\n"
        "hud_wide         = %d   # fight HUD: 0 4:3, 1 wide\n"
        "smooth_animation = %d   # 1 smooth, 0 stepped\n"
        "fight_volume     = %d   # 0..10\n"
        "menu_volume      = %d   # 0..10\n",
        g_settings.fullscreen, g_settings.window, w, h, g_settings.hud_wide, g_settings.smooth_anim,
        g_settings.fx_volume, g_settings.menu_volume);
    return n > 0 && n < (int)sizeof text && SDL_SaveFile(path, text, (size_t)n);
}

/* ---- the page ---- */

enum { V_FULLSCREEN, V_WINDOW, V_HUD, V_ANIM, V_COUNT };
enum { A_FIGHT, A_MENU, A_COUNT };

static int rows(const settings_ui *ui) { return (ui->page ? A_COUNT : V_COUNT) + 2; }   /* + STANDARD, BACK */

void settings_open(settings_ui *ui, int page) {
    memset(ui, 0, sizeof *ui);
    ui->page = page;
}

static void say(settings_ui *ui, const char *t) {
    SDL_strlcpy(ui->message, t, sizeof ui->message);
    ui->message_ms = 2500;
}

/* Left / right (or OK) changes the row's value. */
static void change(settings_ui *ui, int dir) {
    game_settings *s = &g_settings;
    if (ui->page == 0) {
        switch (ui->cur) {
        case V_FULLSCREEN: s->fullscreen = !s->fullscreen; break;
        case V_WINDOW: s->window = (s->window + dir + SETTINGS_WINDOW_SIZES) % SETTINGS_WINDOW_SIZES; break;
        case V_HUD: s->hud_wide = !s->hud_wide; break;
        case V_ANIM: s->smooth_anim = !s->smooth_anim; break;
        default: return;
        }
    } else {
        int *v = ui->cur == A_FIGHT ? &s->fx_volume : &s->menu_volume;
        *v = SDL_clamp(*v + dir, 0, 10);
    }
    ui->apply = 1;
    settings_save();
}

int settings_input(settings_ui *ui, garage_action a) {
    if (a == GA_NONE) return 0;
    const int n = rows(ui), values = n - 2;
    if (a == GA_UP) ui->cur = (ui->cur + n - 1) % n;
    if (a == GA_DOWN) ui->cur = (ui->cur + 1) % n;
    if (a == GA_CANCEL) { settings_save(); return 1; }
    if (ui->cur < values) {
        if (a == GA_LEFT) change(ui, -1);
        if (a == GA_RIGHT || a == GA_OK) change(ui, 1);
        return 0;
    }
    if (a != GA_OK) return 0;
    if (ui->cur == values) {   /* STANDARD: this page's defaults */
        const game_settings d = { 0, 1, 0, 1, 7, 7 };
        if (ui->page == 0) {
            g_settings.fullscreen = d.fullscreen; g_settings.window = d.window;
            g_settings.hud_wide = d.hud_wide; g_settings.smooth_anim = d.smooth_anim;
        } else {
            g_settings.fx_volume = d.fx_volume; g_settings.menu_volume = d.menu_volume;
        }
        ui->apply = 1;
        settings_save();
        say(ui, "BACK TO THE STANDARD SETTINGS.");
        return 0;
    }
    settings_save();
    return 1;
}

static void bar(float x, float y, int v, int sel) {
    for (int i = 0; i < 10; ++i) {
        const int on = i < v;
        acr_hud_rect(x + i * 12.0f, y, 9, 9, on ? (sel ? 255 : 200) : 50, on ? (sel ? 220 : 210) : 60,
                     on ? (sel ? 120 : 220) : 70, 255, 1);
    }
}

void settings_draw(settings_ui *ui, float hud_w, int elapsed_ms) {
    (void)hud_w;
    if (ui->message_ms > 0) ui->message_ms -= elapsed_ms;
    const float lx = 24, w = 460;
    const int n = rows(ui), values = n - 2;
    acr_hud_text(lx, 16, ui->page ? "SETTINGS: AUDIO" : "SETTINGS: VIDEO", HILITE);
    acr_hud_rect(lx, 40, w, 50 + n * 22.0f, 10, 14, 20, 210, 1);
    acr_hud_rect(lx, 40, w, 50 + n * 22.0f, 70, 90, 100, 255, 0);
    static const char *const k_video[V_COUNT] = { "DISPLAY", "WINDOW SIZE", "FIGHT HUD", "ANIMATION" };
    static const char *const k_audio[A_COUNT] = { "FIGHT SOUNDS", "MENU SOUNDS" };
    char line[64];
    for (int r = 0; r < n; ++r) {
        const float y = 56 + r * 22.0f;
        const int sel = r == ui->cur;
        if (sel) acr_hud_rect(lx + 8, y - 4, w - 16, 16, 60, 70, 40, 255, 1);
        const char *label = r < values ? (ui->page ? k_audio[r] : k_video[r]) : r == values ? "STANDARD SETTINGS" : "BACK";
        acr_hud_text(lx + 16, y, label, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
        if (r >= values) continue;
        const char *value = line;
        if (ui->page) {
            const int v = r == A_FIGHT ? g_settings.fx_volume : g_settings.menu_volume;
            bar(lx + 220, y - 1, v, sel);
            SDL_snprintf(line, sizeof line, "%2d", v);
            acr_hud_text(lx + 352, y, line, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
            continue;
        }
        switch (r) {
        case V_FULLSCREEN: value = g_settings.fullscreen ? "< FULLSCREEN >" : "< WINDOW >"; break;
        case V_WINDOW: {
            int ww, hh;
            settings_window_size(g_settings.window, &ww, &hh);
            SDL_snprintf(line, sizeof line, "< %d X %d >", ww, hh);
            break;
        }
        case V_HUD: value = g_settings.hud_wide ? "< WIDE >" : "< 4:3 >"; break;
        default: value = g_settings.smooth_anim ? "< SMOOTH >" : "< STEPPED >"; break;
        }
        acr_hud_text(lx + 220, y, value, sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
    }
    const float fy = 56 + n * 22.0f + 6;
    acr_hud_text(lx + 16, fy, "LEFT / RIGHT: CHANGE   ESC / B: BACK (SAVED)", DIM);
    if (ui->page == 0 && g_settings.fullscreen)
        acr_hud_text(lx + 16, fy + 16, "WINDOW SIZE APPLIES WHEN IN A WINDOW.", DIM);
    if (ui->message_ms > 0 && ui->message[0]) acr_hud_text(lx + 16, fy + 32, ui->message, HILITE);
}
