#include "split_menu.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "net_menu.h"
#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define BAD 255, 90, 70
#define GOOD 120, 220, 150
#define PANEL_W 460

static const int k_stage[SPLIT_MENU_STAGES] = { 27, 39, 0 };
enum { ROW_MODE, ROW_STAGE, ROW_START, ROW_BACK, ROWS };

void split_menu_init(split_menu *m) {
    memset(m, 0, sizeof *m);
    for (int i = 0; i < SPLIT_MENU_STAGES; ++i) m->stage_ok[i] = 1;
    m->row = ROW_START;
}

int split_menu_stage(const split_menu *m) { return k_stage[m->stage_pick]; }

static void say(split_menu *m, const char *t) { SDL_strlcpy(m->message, t, sizeof m->message); m->message_ms = 4000; }

static void step_stage(split_menu *m, int d) {
    for (int k = 0; k < SPLIT_MENU_STAGES; ++k) {
        m->stage_pick = (m->stage_pick + d + SPLIT_MENU_STAGES) % SPLIT_MENU_STAGES;
        if (m->stage_ok[m->stage_pick]) return;
    }
}

split_menu_command split_menu_input(split_menu *m, garage_action a) {
    if (a == GA_UP) { do m->row = (m->row + ROWS - 1) % ROWS; while (m->coop && m->row == ROW_STAGE); return SM_NONE; }
    if (a == GA_DOWN) { do m->row = (m->row + 1) % ROWS; while (m->coop && m->row == ROW_STAGE); return SM_NONE; }
    if (a == GA_CANCEL) return SM_BACK;
    if (a == GA_LEFT || a == GA_RIGHT || (a == GA_OK && (m->row == ROW_MODE || m->row == ROW_STAGE))) {
        const int d = a == GA_LEFT ? -1 : 1;
        if (m->row == ROW_MODE) m->coop = !m->coop;
        if (m->row == ROW_STAGE) step_stage(m, d);
        return SM_NONE;
    }
    if (a != GA_OK) return SM_NONE;
    if (m->row == ROW_BACK) return SM_BACK;
    if (!m->players_ready) { say(m, "PLAYER 2 NEEDS A CONTROLLER: PLUG ONE IN."); return SM_NONE; }
    return m->coop ? SM_COOP : SM_VERSUS;
}

static void row_text(float x, float y, int sel, const char *text) {
    if (sel) {
        acr_hud_rect(x - 8, y - 3, PANEL_W - 8, 14, 60, 70, 40, 255, 1);
        acr_hud_text(x, y, text, HILITE);
    } else acr_hud_text(x, y, text, WHITE);
}

void split_menu_draw(split_menu *m, float hud_w, int elapsed_ms) {
    char line[128];
    const float px = hud_w / 2 - PANEL_W / 2, x = px + 12;
    acr_hud_text(px, 12, "MULTIPLAYER  >  SPLIT SCREEN", HILITE);
    acr_hud_rect(px, 34, PANEL_W, 260, 10, 14, 20, 210, 1);
    acr_hud_rect(px, 34, PANEL_W, 260, 70, 90, 100, 255, 0);
    float y = 50;
    SDL_snprintf(line, sizeof line, "MODE    < %s >", m->coop ? "CO-OP  (ROGUE ARENA TOGETHER)" : "VERSUS  (YOU AGAINST EACH OTHER)");
    row_text(x, y, m->row == ROW_MODE, line); y += 18;
    if (m->coop) acr_hud_text(x, y, "STAGE   (PICKED IN THE RUN)", DIM);
    else {
        SDL_snprintf(line, sizeof line, "STAGE   < %s >", net_menu_stage_name(split_menu_stage(m)));
        row_text(x, y, m->row == ROW_STAGE, line);
    }
    y += 30;
    acr_hud_text(x, y, "PLAYERS", DIM); y += 14;
    acr_hud_text(x, y, "PLAYER 1   KEYBOARD OR CONTROLLER 1", WHITE); y += 14;
    if (m->players_ready) acr_hud_text(x, y, "PLAYER 2   CONTROLLER READY", GOOD);
    else acr_hud_text(x, y, "PLAYER 2   PLUG IN A CONTROLLER", BAD);
    y += 30;
    row_text(x, y, m->row == ROW_START, m->coop ? "START CO-OP" : "START VERSUS"); y += 18;
    row_text(x, y, m->row == ROW_BACK, "BACK TO THE HANGAR"); y += 26;
    acr_hud_text(x, y, "PLAYER 2 FLIES A COPY OF YOUR AC FOR NOW.", DIM); y += 14;
    acr_hud_text(x, y, "(THEIR OWN SAVE AND HANGAR COME NEXT.)", DIM);
    if (m->message_ms > 0) { m->message_ms -= elapsed_ms; acr_hud_text(px, 302, m->message, HILITE); }
    acr_hud_text(px, 320, "A: SELECT   LEFT/RIGHT: CHANGE   B: BACK", DIM);
}
