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
#define TOP_W 460

static const int k_stage[SPLIT_MENU_STAGES] = { 27, 39, 0 };
/* Each player's rows: the shared two at the top, then their own. */
enum { ROW_MODE, ROW_STAGE, ROW_SAVE, ROW_GARAGE, ROW_READY, ROWS };
static const char *const k_where[SPLIT_COOP_WHERE] = { "ARENA", "ROGUE ARENA", "MISSIONS" };

void split_menu_init(split_menu *m) {
    memset(m, 0, sizeof *m);
    for (int i = 0; i < SPLIT_MENU_STAGES; ++i) m->stage_ok[i] = 1;
    m->row[0] = m->row[1] = ROW_READY;
}

void split_menu_enter(split_menu *m, int p1_in, int p2_in) {
    m->joined[0] = p1_in; m->joined[1] = p2_in;
    m->ready[0] = m->ready[1] = 0;
    m->row[0] = m->row[1] = ROW_READY;
}

int split_menu_stage(const split_menu *m) { return k_stage[m->stage_pick]; }

void split_menu_say(split_menu *m, const char *t) { SDL_strlcpy(m->message, t, sizeof m->message); m->message_ms = 4000; }

static void step_stage(split_menu *m, int d) {
    for (int k = 0; k < SPLIT_MENU_STAGES; ++k) {
        m->stage_pick = (m->stage_pick + d + SPLIT_MENU_STAGES) % SPLIT_MENU_STAGES;
        if (m->stage_ok[m->stage_pick]) return;
    }
}

/* Everyone in is ready: can it start? (A reason in the message if not.) */
static int can_start(split_menu *m) {
    if (!m->joined[0] && !m->joined[1]) return 0;
    for (int p = 0; p < 2; ++p) if (m->joined[p] && !m->ready[p]) return 0;
    if (m->coop && !m->joined[0]) {
        split_menu_say(m, "CO-OP PLAYS PLAYER 1'S CAREER: PLAYER 1, PRESS A TO JOIN.");
        m->ready[1] = 0;
        return 0;
    }
    return 1;
}

split_menu_command split_menu_input(split_menu *m, int who, garage_action a) {
    if (a == GA_NONE) return SM_NONE;
    who = who ? 1 : 0;
    m->who = who;
    if (!m->joined[who]) {                       /* out: A joins, nothing else */
        if (a != GA_OK) return SM_NONE;
        if (who == 1 && !m->p2_controls) { split_menu_say(m, "PLAYER 2 NEEDS A CONTROLLER: PLUG ONE IN."); return SM_NONE; }
        m->joined[who] = 1; m->ready[who] = 0; m->row[who] = ROW_READY;
        return SM_NONE;
    }
    int *row = &m->row[who];
    if (a == GA_UP) { *row = (*row + ROWS - 1) % ROWS; return SM_NONE; }
    if (a == GA_DOWN) { *row = (*row + 1) % ROWS; return SM_NONE; }
    if (a == GA_CANCEL) {                        /* this player leaves (the other stays) */
        if (m->ready[who]) { m->ready[who] = 0; return SM_NONE; }   /* first B: not ready any more */
        m->joined[who] = 0;
        return !m->joined[0] && !m->joined[1] ? SM_ALL_LEFT : SM_NONE;
    }
    if (a == GA_LEFT || a == GA_RIGHT || (a == GA_OK && (*row == ROW_MODE || *row == ROW_STAGE))) {
        if (*row != ROW_MODE && *row != ROW_STAGE) return SM_NONE;
        const int d = a == GA_LEFT ? -1 : 1;
        if (*row == ROW_MODE) m->coop = !m->coop;
        else if (m->coop) m->where = (m->where + d + SPLIT_COOP_WHERE) % SPLIT_COOP_WHERE;
        else step_stage(m, d);
        m->ready[0] = m->ready[1] = 0;           /* a change: both say ready again */
        return SM_NONE;
    }
    if (a != GA_OK) return SM_NONE;
    if (*row == ROW_SAVE) { m->ready[who] = 0; return SM_SAVE; }
    if (*row == ROW_GARAGE) { m->ready[who] = 0; return SM_GARAGE; }
    m->ready[who] = !m->ready[who];               /* ROW_READY */
    return m->ready[who] && can_start(m) ? SM_START : SM_NONE;
}

static void row_text(float x, float y, float w, int sel, const char *text) {
    if (sel) {
        acr_hud_rect(x - 6, y - 3, w - 12, 14, 60, 70, 40, 255, 1);
        acr_hud_text(x, y, text, HILITE);
    } else acr_hud_text(x, y, text, WHITE);
}

void split_menu_draw(split_menu *m, float hud_w, int elapsed_ms) {
    char line[128];
    const float tx = hud_w / 2 - TOP_W / 2;
    acr_hud_text(tx, 12, "MULTIPLAYER  >  SPLIT SCREEN", HILITE);
    /* The shared box: either player changes it (their cursor on it shows). */
    acr_hud_rect(tx, 30, TOP_W, 50, 10, 14, 20, 210, 1);
    acr_hud_rect(tx, 30, TOP_W, 50, 70, 90, 100, 255, 0);
    const int on_mode = (m->joined[0] && m->row[0] == ROW_MODE) || (m->joined[1] && m->row[1] == ROW_MODE);
    const int on_stage = (m->joined[0] && m->row[0] == ROW_STAGE) || (m->joined[1] && m->row[1] == ROW_STAGE);
    SDL_snprintf(line, sizeof line, "MODE    < %s >", m->coop ? "CO-OP  (TOGETHER, AGAINST THE GAME)" : "VERSUS  (YOU AGAINST EACH OTHER)");
    row_text(tx + 12, 40, TOP_W, on_mode, line);
    if (m->coop) SDL_snprintf(line, sizeof line, "PLAY    < %s >", k_where[m->where]);
    else SDL_snprintf(line, sizeof line, "STAGE   < %s >", net_menu_stage_name(split_menu_stage(m)));
    row_text(tx + 12, 58, TOP_W, on_stage, line);

    /* The two halves. */
    const float gap = 12, pw = (hud_w - 3 * gap) / 2, py = 92, ph = 200;
    for (int p = 0; p < 2; ++p) {
        const float px = gap + p * (pw + gap), x = px + 12;
        const int in = m->joined[p];
        acr_hud_rect(px, py, pw, ph, 10, 14, 20, 210, 1);
        if (in && m->ready[p]) acr_hud_rect(px, py, pw, ph, 120, 220, 150, 255, 0);
        else acr_hud_rect(px, py, pw, ph, 70, 90, 100, 255, 0);
        float y = py + 10;
        acr_hud_text(x, y, p == 0 ? "PLAYER 1" : "PLAYER 2", HILITE);
        if (p == 1 && !m->p2_controls) acr_hud_text(x + 80, y, "NO CONTROLLER", BAD);
        else acr_hud_text(x + 80, y, p == 0 ? "KEYBOARD / CONTROLLER 1" : "CONTROLLER", DIM);
        y += 24;
        if (!in) {
            acr_hud_text(x, y + 40, p == 1 && !m->p2_controls ? "PLUG IN A CONTROLLER, THEN PRESS A" : "PRESS A TO JOIN", WHITE);
            acr_hud_text(x, y + 60, "(NOT PLAYING)", DIM);
            continue;
        }
        const int r = m->row[p];
        const int top_sel = r == ROW_MODE || r == ROW_STAGE;
        if (top_sel) acr_hud_text(x, y, "^ CHANGING THE MODE ABOVE", HILITE);
        y += 18;
        SDL_snprintf(line, sizeof line, "SAVE:   %s", m->save[p][0] ? m->save[p] : "-");
        row_text(x, y, pw, r == ROW_SAVE, line); y += 18;
        SDL_snprintf(line, sizeof line, "AC:     %s   (GARAGE)", m->ac[p][0] ? m->ac[p] : "AC");
        row_text(x, y, pw, r == ROW_GARAGE, line); y += 26;
        row_text(x, y, pw, r == ROW_READY, m->ready[p] ? "READY!   (A: NOT READY)" : "READY?   (A: I'M READY)");
        if (m->ready[p]) acr_hud_text(x, y + 20, "WAITING FOR THE OTHER PLAYER...", GOOD);
        acr_hud_text(x, py + ph - 18, "B: LEAVE (THE OTHER PLAYER STAYS)", DIM);
    }
    const float by = py + ph + 10;
    acr_hud_text(gap, by, m->coop ? "CO-OP: PLAYER 2 STARTS BEHIND PLAYER 1. BOTH EARN THE PRIZES."
                                  : "VERSUS: ALONE, YOU FIGHT THE COMPUTER.", DIM);
    if (m->message_ms > 0) { m->message_ms -= elapsed_ms; acr_hud_text(gap, by + 16, m->message, HILITE); }
    acr_hud_text(gap, by + 32, "EACH PLAYER: UP/DOWN MOVE   A: SELECT   LEFT/RIGHT: CHANGE   B: LEAVE", DIM);
    acr_hud_text(gap, by + 46, "IN A FIGHT: ESC (KEYBOARD) OR BACK+START TWICE LEAVES; THE OTHER PLAYER PLAYS ON.", DIM);
}
