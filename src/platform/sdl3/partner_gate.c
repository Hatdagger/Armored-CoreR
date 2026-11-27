#include "partner_gate.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define GOOD 120, 220, 150
#define BAD 255, 90, 70

void partner_gate_open(partner_gate *g, int p2_in, const char *what) {
    memset(g, 0, sizeof *g);
    g->open = 1;
    g->p2_in = p2_in;
    SDL_strlcpy(g->what, what ? what : "FIGHT", sizeof g->what);
}

static partner_result go(partner_gate *g, partner_result r) { g->open = 0; return r; }

partner_result partner_gate_input(partner_gate *g, int who, garage_action a) {
    if (!g->open || a == GA_NONE) return PG_NONE;
    if (who) {                                   /* player 2: yes or not this one */
        if (a == GA_OK) { g->p2_ok = 1; g->p2_no = 0; return g->waiting ? go(g, PG_TOGETHER) : PG_NONE; }
        if (a == GA_CANCEL) { g->p2_ok = 0; g->p2_no = 1; g->waiting = 0; }
        return PG_NONE;
    }
    if (a == GA_CANCEL) {
        if (g->waiting) { g->waiting = 0; return PG_NONE; }   /* stop waiting */
        return go(g, PG_BACK);
    }
    if (g->waiting) return PG_NONE;
    if (a == GA_UP || a == GA_DOWN || a == GA_LEFT || a == GA_RIGHT) { g->pick = 1 - g->pick; return PG_NONE; }
    if (a != GA_OK) return PG_NONE;
    if (g->pick == 1) return go(g, PG_SOLO);
    if (g->p2_ok) return go(g, PG_TOGETHER);    /* player 2 already said yes */
    g->waiting = 1;
    return PG_NONE;
}

void partner_gate_draw(const partner_gate *g, float hud_w) {
    if (!g->open) return;
    const float w = 380, h = 132, x = hud_w / 2 - w / 2, y = 150;
    acr_hud_rect(0, 0, hud_w, 480, 0, 0, 0, 120, 1);
    acr_hud_rect(x, y, w, h, 10, 14, 20, 235, 1);
    acr_hud_rect(x, y, w, h, 70, 90, 100, 255, 0);
    char line[96];
    SDL_snprintf(line, sizeof line, "SPLIT SCREEN: START THE %s", g->what);
    acr_hud_text(x + 14, y + 12, line, HILITE);
    const char *opt[2] = { g->p2_in ? "SORTIE TOGETHER" : "WAIT FOR PLAYER 2", "GO SOLO (PLAYER 2 SITS OUT)" };
    for (int i = 0; i < 2; ++i) {
        const float ry = y + 38 + i * 18;
        if (i == g->pick && !g->waiting) {
            acr_hud_rect(x + 8, ry - 3, w - 16, 14, 60, 70, 40, 255, 1);
            acr_hud_text(x + 14, ry, opt[i], HILITE);
        } else if (g->waiting) acr_hud_text(x + 14, ry, opt[i], DIM);
        else acr_hud_text(x + 14, ry, opt[i], WHITE);
    }
    const float sy = y + 84;
    if (g->waiting) acr_hud_text(x + 14, sy, "WAITING FOR PLAYER 2: PRESS A TO GO", HILITE);
    else if (g->p2_ok) acr_hud_text(x + 14, sy, "PLAYER 2: READY TO GO", GOOD);
    else if (g->p2_no) acr_hud_text(x + 14, sy, "PLAYER 2: NOT THIS ONE (B)", BAD);
    else acr_hud_text(x + 14, sy, g->p2_in ? "PLAYER 2: PRESS A TO AGREE" : "PLAYER 2: PRESS A TO JOIN", WHITE);
    acr_hud_text(x + 14, y + h - 18, g->waiting ? "PLAYER 1  B: STOP WAITING" : "PLAYER 1  A: SELECT   B: BACK", DIM);
}
