#include "save_list.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "../../gen1/acr_gen1_garage.h"
#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define BAD 255, 90, 70
#define GOOD 120, 220, 150
#define PANEL_W 620
#define ROWS_SHOWN 12

void save_list_dir(char *out, size_t size) {
    const char *env = SDL_getenv("ACR_SAVE_DIR");
    if (env && env[0]) { SDL_snprintf(out, size, "%s%s", env, env[SDL_strlen(env) - 1] == '/' ? "" : "/"); }
    else if ((env = SDL_getenv("ACR_SAVE")) != NULL && env[0]) {
        SDL_strlcpy(out, env, size);
        char *slash = SDL_strrchr(out, '/');
        if (slash) slash[1] = 0; else out[0] = 0;
        SDL_strlcat(out, "saves/", size);
    } else {
        char *pref = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
        SDL_snprintf(out, size, "%ssaves/", pref ? pref : "");
        SDL_free(pref);
    }
    SDL_CreateDirectory(out);
}

void save_list_say(save_list *s, const char *text) {
    SDL_strlcpy(s->message, text, sizeof s->message);
    s->message_ms = 5000;
}

static int by_name(const void *a, const void *b) {
    return SDL_strcmp(((const save_list_entry *)a)->file, ((const save_list_entry *)b)->file);
}

/* Reads one save's names and credits. */
static int read_entry(save_list_entry *e, const char *path) {
    SDL_zerop(e);
    SDL_strlcpy(e->path, path, sizeof e->path);
    const char *slash = SDL_strrchr(path, '/');
    SDL_strlcpy(e->file, slash ? slash + 1 : path, sizeof e->file);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info)) e->mtime = (long long)info.modify_time;
    char *t = SDL_LoadFile(path, NULL);
    static acr_gen1_hangar h;
    e->ok = t && acr_gen1_save_read(&h, t);
    SDL_free(t);
    if (e->ok) {
        SDL_strlcpy(e->pilot, h.pilot_name, sizeof e->pilot);
        SDL_strlcpy(e->ac, h.ac_name, sizeof e->ac);
        e->credits = h.credits;
    }
    return e->ok;
}

void save_list_refresh(save_list *s) {
    s->n = 0;
    int count = 0;
    char **files = SDL_GlobDirectory(s->dir, "*.acrsave", 0, &count);
    for (int i = 0; files && i < count && s->n < SAVE_LIST_MAX; ++i) {
        char path[1200];
        SDL_snprintf(path, sizeof path, "%s%s", s->dir, files[i]);
        read_entry(&s->e[s->n++], path);
    }
    SDL_free(files);
    SDL_qsort(s->e, (size_t)s->n, sizeof s->e[0], by_name);
    const int rows = s->n + 1;
    if (s->row >= rows) s->row = rows - 1;
}

void save_list_open(save_list *s, save_list_mode mode, const char *current) {
    s->mode = mode;
    save_list_dir(s->dir, sizeof s->dir);
    SDL_strlcpy(s->current, current ? current : "", sizeof s->current);
    s->menu = 0;
    s->message_ms = 0;
    save_list_refresh(s);
    s->row = 0;
    for (int i = 0; i < s->n; ++i) if (!SDL_strcmp(s->e[i].path, s->current)) s->row = i;
    s->top = 0;
}

void save_list_new_path(const save_list *s, char *out, size_t size) {
    for (int k = 1; k < 1000; ++k) {
        SDL_snprintf(out, size, "%ssave-%02d.acrsave", s->dir, k);
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(out, &info)) return;
    }
}

/* EXPORT: a copy in the documents folder (Armored-CoreR Saves). */
static void export_save(save_list *s, const save_list_entry *e) {
    const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    char dir[1200], out[1400];
    if (docs) SDL_snprintf(dir, sizeof dir, "%sArmored-CoreR Saves/", docs);
    else SDL_snprintf(dir, sizeof dir, "%sexport/", s->dir);
    SDL_CreateDirectory(dir);
    SDL_snprintf(out, sizeof out, "%s%s", dir, e->file);
    size_t n = 0;
    void *data = SDL_LoadFile(e->path, &n);
    const int ok = data && SDL_SaveFile(out, data, n);
    SDL_free(data);
    char msg[1500];
    SDL_snprintf(msg, sizeof msg, ok ? "EXPORTED TO %s" : "COULD NOT EXPORT TO %s", out);
    save_list_say(s, msg);
    SDL_Log("save list: %s", msg);
}

void save_list_import(save_list *s, const char *file) {
    save_list_entry e;
    if (!read_entry(&e, file)) { save_list_say(s, "THAT FILE IS NOT AN ARMORED-CORER SAVE."); return; }
    char out[1200];
    SDL_snprintf(out, sizeof out, "%s%s", s->dir, e.file);
    const char *dot = SDL_strrchr(out, '.');
    SDL_PathInfo info;
    if (!dot || SDL_strcmp(dot, ".acrsave") || SDL_GetPathInfo(out, &info)) save_list_new_path(s, out, sizeof out);
    size_t n = 0;
    void *data = SDL_LoadFile(file, &n);
    const int ok = data && SDL_SaveFile(out, data, n);
    SDL_free(data);
    save_list_refresh(s);
    for (int i = 0; i < s->n; ++i) if (!SDL_strcmp(s->e[i].path, out)) s->row = i;
    char msg[200];
    SDL_snprintf(msg, sizeof msg, ok ? "IMPORTED: %s (%s)." : "COULD NOT IMPORT %s%s.", e.pilot, ok ? e.ac : "");
    save_list_say(s, msg);
}

enum { MENU_SAVE, MENU_LOAD, MENU_EXPORT, MENU_CANCEL, MENU_N };
static const char *const k_menu[MENU_N] = { "SAVE HERE (OVERWRITE)", "LOAD THIS SAVE", "EXPORT A COPY", "CANCEL" };

static int menu_allowed(const save_list *s, int item) {
    if (item == MENU_SAVE) return s->mode == SL_MODE_SAVE;
    if (item == MENU_LOAD) return s->mode != SL_MODE_SAVE;
    return 1;
}

save_list_command save_list_input(save_list *s, garage_action a) {
    const int rows = s->n + 1;           /* the saves, then NEW SAVE / BACK */
    if (s->menu) {
        if (a == GA_UP || a == GA_DOWN) {
            const int d = a == GA_UP ? -1 : 1;
            do s->menu_row = (s->menu_row + d + MENU_N) % MENU_N; while (!menu_allowed(s, s->menu_row));
            return SL_NONE;
        }
        if (a == GA_CANCEL) { s->menu = 0; return SL_NONE; }
        if (a != GA_OK) return SL_NONE;
        s->menu = 0;
        const save_list_entry *e = &s->e[s->row];
        if (s->menu_row == MENU_EXPORT) { export_save(s, e); return SL_NONE; }
        if (s->menu_row == MENU_CANCEL) return SL_NONE;
        if (s->menu_row == MENU_LOAD && !e->ok) { save_list_say(s, "THIS SAVE CAN'T BE READ."); return SL_NONE; }
        SDL_strlcpy(s->path, e->path, sizeof s->path);
        return s->menu_row == MENU_SAVE ? SL_SAVE : SL_LOAD;
    }
    if (a == GA_UP) { s->row = (s->row + rows - 1) % rows; return SL_NONE; }
    if (a == GA_DOWN) { s->row = (s->row + 1) % rows; return SL_NONE; }
    if (a == GA_CANCEL) return SL_BACK;
    if (a != GA_OK) return SL_NONE;
    if (s->row == s->n) {                /* the last row */
        if (s->mode == SL_MODE_SAVE) { save_list_new_path(s, s->path, sizeof s->path); return SL_SAVE; }
        if (s->mode == SL_MODE_P2) { save_list_new_path(s, s->path, sizeof s->path); return SL_LOAD; }   /* a new save for player 2 */
        return SL_BACK;
    }
    s->menu = 1;
    s->menu_row = s->mode == SL_MODE_SAVE ? MENU_SAVE : MENU_LOAD;
    return SL_NONE;
}

static void row_bar(float x, float y, float w) { acr_hud_rect(x - 8, y - 3, w, 14, 60, 70, 40, 255, 1); }

void save_list_draw(save_list *s, float hud_w, int elapsed_ms) {
    char line[200];
    const float px = hud_w / 2 - PANEL_W / 2, x = px + 12;
    acr_hud_text(px, 12, s->mode == SL_MODE_SAVE ? "SYSTEM  >  SAVE" : s->mode == SL_MODE_LOAD ? "SYSTEM  >  LOAD"
                                                 : "SPLIT SCREEN  >  PLAYER 2'S SAVE", HILITE);
    acr_hud_rect(px, 34, PANEL_W, 330, 10, 14, 20, 210, 1);
    acr_hud_rect(px, 34, PANEL_W, 330, 70, 90, 100, 255, 0);
    acr_hud_text(x, 46, "FILE             PILOT        AC                 CREDITS  SAVED", DIM);
    const int rows = s->n + 1;
    if (s->row < s->top) s->top = s->row;
    if (s->row >= s->top + ROWS_SHOWN) s->top = s->row - ROWS_SHOWN + 1;
    float y = 64;
    for (int r = s->top; r < rows && r < s->top + ROWS_SHOWN; ++r, y += 18) {
        const int sel = r == s->row;
        if (sel) row_bar(x, y, PANEL_W - 8);
        if (r == s->n) {
            acr_hud_text(x, y, s->mode == SL_MODE_LOAD ? "BACK" : "+ NEW SAVE", sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
            continue;
        }
        const save_list_entry *e = &s->e[r];
        char when[24] = "";
        SDL_DateTime dt;
        if (e->mtime && SDL_TimeToDateTime((SDL_Time)e->mtime, &dt, true))
            SDL_snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
        char file[17];
        SDL_strlcpy(file, e->file, sizeof file);
        char *dot = SDL_strrchr(file, '.');
        if (dot && !SDL_strcmp(e->file + (dot - file), ".acrsave")) *dot = 0;
        if (e->ok) SDL_snprintf(line, sizeof line, "%-16.16s %-12.12s %-16.16s %9ld  %s", file, e->pilot, e->ac, e->credits, when);
        else SDL_snprintf(line, sizeof line, "%-16.16s (CAN'T BE READ)", file);
        const int cur = !SDL_strcmp(e->path, s->current);
        if (sel) acr_hud_text(x, y, line, HILITE);
        else if (!e->ok) acr_hud_text(x, y, line, BAD);
        else acr_hud_text(x, y, line, WHITE);
        if (cur) acr_hud_text(x - 8 - 10, y, ">", GOOD);
    }
    if (!s->n) acr_hud_text(x, y + 6, "NO SAVES YET.", DIM);
    if (s->menu) {
        const float mx = px + 140, my = 64 + (float)(s->row - s->top + 1) * 18;
        acr_hud_rect(mx, my, 220, 14 + MENU_N * 16, 20, 26, 34, 245, 1);
        acr_hud_rect(mx, my, 220, 14 + MENU_N * 16, 120, 150, 160, 255, 0);
        for (int i = 0; i < MENU_N; ++i) {
            const float iy = my + 8 + i * 16;
            if (!menu_allowed(s, i)) { acr_hud_text(mx + 12, iy, k_menu[i], 70, 80, 90); continue; }
            if (i == s->menu_row) { acr_hud_rect(mx + 4, iy - 3, 212, 14, 60, 70, 40, 255, 1); acr_hud_text(mx + 12, iy, k_menu[i], HILITE); }
            else acr_hud_text(mx + 12, iy, k_menu[i], WHITE);
        }
    }
    acr_hud_text(px, 372, "SAVES FOLDER (COPY SAVE FILES IN OR OUT HERE):", DIM);
    acr_hud_text_size(px, 386, s->dir, 6.5f, WHITE, 255);
    acr_hud_text(px, 402, "IMPORT: DROP A .ACRSAVE FILE ON THE WINDOW.", DIM);
    if (s->message_ms > 0) { s->message_ms -= elapsed_ms; acr_hud_text_size(px, 422, s->message, 6.5f, HILITE, 255); }
    acr_hud_text(px, 444, "A: CHOOSE   B: BACK", DIM);
}
