#include "net_menu.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define BAD 255, 90, 70
#define GOOD 120, 220, 150
#define PANEL_W 460

static const int k_stage[NET_MENU_STAGES] = { 27, 39, 0 };
/* The address picker: characters, then delete and join. */
static const char k_keys[] = "0123456789.:";
#define KEY_DEL  ((int)sizeof k_keys - 1)
#define KEY_JOIN ((int)sizeof k_keys)
#define KEYS     ((int)sizeof k_keys + 1)

enum { ROW_HOST, ROW_STAGE, ROW_DELAY, ROW_FOUND };

static int rows(const net_menu *m) { return ROW_FOUND + m->found_n + 2; }
static int row_address(const net_menu *m) { return ROW_FOUND + m->found_n; }
static int row_back(const net_menu *m) { return ROW_FOUND + m->found_n + 1; }

const char *net_menu_stage_name(int stage) {
    switch (stage) {
    case 27: return "GREEN SILO";
    case 39: return "OCEAN PLATFORM";
    case 0: return "OPEN TEST FLOOR";
    default: return "STAGE";
    }
}

void net_menu_init(net_menu *m) {
    memset(m, 0, sizeof *m);
    m->delay = 1;
    for (int i = 0; i < NET_MENU_STAGES; ++i) m->stage_ok[i] = 1;
    m->picker = 0;
}

int net_menu_stage(const net_menu *m) { return k_stage[m->stage_pick]; }

void net_menu_say(net_menu *m, const char *text) {
    SDL_strlcpy(m->message, text, sizeof m->message);
    m->message_ms = 4000;
}

int net_menu_typing(const net_menu *m) { return m->row == row_address(m); }

static void add_char(net_menu *m, char c) {
    const size_t n = SDL_strlen(m->address);
    if (n + 1 < sizeof m->address) { m->address[n] = c; m->address[n + 1] = 0; }
}

void net_menu_text(net_menu *m, const char *text) {
    if (!net_menu_typing(m)) return;
    for (const char *t = text; *t; ++t) {
        if (*t == '\b') {
            const size_t n = SDL_strlen(m->address);
            if (n) m->address[n - 1] = 0;
        } else if ((*t >= '0' && *t <= '9') || *t == '.' || *t == ':' || *t == '-' ||
                   (*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z')) {
            add_char(m, *t);
        }
    }
}

static void step_stage(net_menu *m, int d) {
    for (int k = 0; k < NET_MENU_STAGES; ++k) {
        m->stage_pick = (m->stage_pick + d + NET_MENU_STAGES) % NET_MENU_STAGES;
        if (m->stage_ok[m->stage_pick]) return;
    }
}

net_menu_command net_menu_input(net_menu *m, garage_action a, int keyboard, char *join_to, int join_size) {
    const int n = rows(m);
    if (m->row >= n) m->row = n - 1;
    if (a == GA_UP) { m->row = (m->row + n - 1) % n; return NM_NONE; }
    if (a == GA_DOWN) { m->row = (m->row + 1) % n; return NM_NONE; }
    const int addr_row = m->row == row_address(m);
    if (a == GA_LEFT || a == GA_RIGHT) {
        const int d = a == GA_LEFT ? -1 : 1;
        if (m->row == ROW_STAGE) step_stage(m, d);
        if (m->row == ROW_DELAY) m->delay = (m->delay + d + 5) % 5;    /* 0..4 */
        if (addr_row) m->picker = (m->picker + d + KEYS) % KEYS;
        return NM_NONE;
    }
    if (a == GA_CANCEL) {
        if (addr_row && !keyboard && m->address[0]) { net_menu_text(m, "\b"); return NM_NONE; }
        return NM_BACK;
    }
    if (a != GA_OK) return NM_NONE;
    if (m->row == ROW_HOST) return NM_HOST;
    if (m->row == ROW_STAGE) { step_stage(m, 1); return NM_NONE; }
    if (m->row == ROW_DELAY) { m->delay = (m->delay + 1) % 5; return NM_NONE; }
    if (m->row >= ROW_FOUND && m->row < row_address(m)) {
        const net_found *g = &m->found[m->row - ROW_FOUND];
        if (!g->same_data) {
            net_menu_say(m, "THAT HOST'S GAME DATA IS DIFFERENT: IMPORT THE SAME DISC ON BOTH PCS.");
            return NM_NONE;
        }
        SDL_strlcpy(join_to, g->addr, (size_t)join_size);
        return NM_JOIN;
    }
    if (addr_row) {
        if (!keyboard && m->picker < KEY_DEL) { add_char(m, k_keys[m->picker]); return NM_NONE; }
        if (!keyboard && m->picker == KEY_DEL) { net_menu_text(m, "\b"); return NM_NONE; }
        if (!m->address[0]) { net_menu_say(m, "TYPE THE HOST'S ADDRESS FIRST (LIKE 192.168.1.20)."); return NM_NONE; }
        SDL_strlcpy(join_to, m->address, (size_t)join_size);
        return NM_JOIN;
    }
    return NM_BACK;
}

static void panel(float x, float y, float w, float h) {
    acr_hud_rect(x, y, w, h, 10, 14, 20, 210, 1);
    acr_hud_rect(x, y, w, h, 70, 90, 100, 255, 0);
}

static void row_text(float x, float y, int sel, const char *text) {
    if (sel) {
        acr_hud_rect(x - 8, y - 3, PANEL_W - 8, 14, 60, 70, 40, 255, 1);
        acr_hud_text(x, y, text, HILITE);
    } else {
        acr_hud_text(x, y, text, WHITE);
    }
}

void net_menu_draw(net_menu *m, float hud_w, int elapsed_ms) {
    char line[128];
    const float px = hud_w / 2 - PANEL_W / 2, x = px + 12;
    acr_hud_text(px, 12, "NETWORK MATCH", HILITE);
    panel(px, 34, PANEL_W, 400);
    if (m->row >= rows(m)) m->row = rows(m) - 1;
    float y = 48;

    acr_hud_text(x, y, "HOST", DIM); y += 14;
    row_text(x, y, m->row == ROW_HOST, "HOST A MATCH (YOU ARE PLAYER 1)"); y += 18;
    SDL_snprintf(line, sizeof line, "STAGE         < %s >", net_menu_stage_name(net_menu_stage(m)));
    row_text(x, y, m->row == ROW_STAGE, line); y += 18;
    SDL_snprintf(line, sizeof line, "INPUT DELAY   < %d >  %s", m->delay,
                 m->delay == 0 ? "(INSTANT, MORE FIXES)" : m->delay == 1 ? "(DEFAULT)" : "(STEADIER, MORE LAG)");
    row_text(x, y, m->row == ROW_DELAY, line); y += 30;

    acr_hud_text(x, y, "JOIN: GAMES ON YOUR NETWORK", DIM); y += 14;
    if (!m->found_n) { acr_hud_text(x, y, "LOOKING...  (NONE YET: HOST ON THE OTHER PC FIRST)", DIM); y += 18; }
    for (int i = 0; i < m->found_n; ++i) {
        const net_found *g = &m->found[i];
        SDL_snprintf(line, sizeof line, "%-12.12s %-21s %s", g->name[0] ? g->name : "AC", g->this_pc ? "THIS PC" : g->addr,
                     g->same_data ? net_menu_stage_name(g->stage) : "DIFFERENT DATA");
        row_text(x, y, m->row == ROW_FOUND + i, line);
        if (!g->same_data && m->row != ROW_FOUND + i) acr_hud_text(x + 8 * 35, y, "DIFFERENT DATA", BAD);
        y += 18;
    }
    y += 12;

    acr_hud_text(x, y, "JOIN BY ADDRESS (ANOTHER NETWORK, OR NOT LISTED)", DIM); y += 14;
    const int addr_sel = m->row == row_address(m);
    const int blink = (SDL_GetTicks() / 400) % 2;
    SDL_snprintf(line, sizeof line, "ADDRESS  %s%s", m->address, addr_sel && blink ? "_" : "");
    row_text(x, y, addr_sel, line); y += 18;
    if (addr_sel) {
        /* The picker, for controllers. */
        float kx = x + 72;
        for (int k = 0; k < KEYS; ++k) {
            const char *label = k == KEY_DEL ? "DEL" : k == KEY_JOIN ? "JOIN" : NULL;
            char one[2] = { k < KEY_DEL ? k_keys[k] : 0, 0 };
            const char *t = label ? label : one;
            const float w = (float)SDL_strlen(t) * 8 + 6;
            if (k == m->picker) acr_hud_rect(kx - 3, y - 3, w, 14, 60, 70, 40, 255, 1);
            acr_hud_text(kx, y, t, k == m->picker ? 255 : 220, k == m->picker ? 220 : 230, k == m->picker ? 120 : 235);
            kx += w + 4;
        }
        y += 18;
    }
    y += 12;
    row_text(x, y, m->row == row_back(m), "BACK TO THE GARAGE");

    if (m->message_ms > 0) {
        m->message_ms -= elapsed_ms;
        acr_hud_text(px, 442, m->message, HILITE);
    }
    acr_hud_text(px, 460,
                 addr_sel ? "LEFT/RIGHT, A: PICK   B: DELETE   KEYBOARD: TYPE, ENTER"
                          : "A: SELECT   LEFT/RIGHT: CHANGE   B: BACK", DIM);
}

void net_menu_draw_status(float hud_w, const char *title, const char *text) {
    const float px = hud_w / 2 - PANEL_W / 2;
    acr_hud_text(px, 12, title, HILITE);
    panel(px, 34, PANEL_W, 400);
    float y = 50;
    const int wrap = (PANEL_W - 24) / 8;
    for (const char *p = text; *p;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        int more = 0;
        if (n > (size_t)wrap) {            /* wrap at the last space that fits */
            size_t cut = (size_t)wrap;
            while (cut > 0 && p[cut] != ' ') --cut;
            n = cut ? cut : (size_t)wrap;
            more = 1;
        }
        char line[96];
        SDL_snprintf(line, sizeof line, "%.*s", (int)(n < 95 ? n : 95), p);
        acr_hud_text(px + 12, y, line, WHITE);
        y += 14;
        if (more) { p += n; while (*p == ' ') ++p; continue; }
        if (!nl) break;
        p = nl + 1;
    }
}
