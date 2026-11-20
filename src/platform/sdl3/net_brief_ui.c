#include "net_brief_ui.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 140, 150
#define GOLD 255, 220, 120
#define GREEN 120, 220, 150
#define RED 255, 90, 70
#define PANEL_W 460
#define LIST_ROWS 12

enum { ROW_CONFIRM = ACR_BRIEF_FIELDS, ROW_PASTE, ROW_CANCEL, ROWS };

/* ---------------- drawing helpers ---------------- */

static void panel(float x, float y, float w, float h) {
    acr_hud_rect(x, y, w, h, 10, 14, 20, 225, 1);
    acr_hud_rect(x, y, w, h, 70, 96, 106, 255, 0);
}

static void upper(char *s) { for (; *s; ++s) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }

/* Draws text wrapped at spaces to `cols` characters; returns the y after it. */
static float wrapped(float x, float y, int cols, const char *text, uint8_t r, uint8_t g, uint8_t b) {
    char line[128];
    const char *p = text;
    while (*p) {
        size_t n = strlen(p);
        const char *nl = strchr(p, '\n');
        if (nl && (size_t)(nl - p) < n) n = (size_t)(nl - p);
        if (n > (size_t)cols) {
            size_t cut = (size_t)cols;
            while (cut > 0 && p[cut] != ' ') --cut;
            n = cut ? cut : (size_t)cols;
        }
        SDL_snprintf(line, sizeof line, "%.*s", (int)(n < 127 ? n : 127), p);
        acr_hud_text(x, y, line, r, g, b);
        y += 12;
        p += n;
        while (*p == ' ' || *p == '\n') ++p;
    }
    return y;
}

/* The briefing card: title, the order as a sentence, payout / time / wingman. */
static float card(float x, float y, const char *title, int title_good, const acr_brief *b) {
    const float w = PANEL_W - 24;
    acr_hud_rect(x, y, w, 118, 14, 20, 26, 255, 1);
    acr_hud_rect(x, y, w, 118, 90, 116, 126, 255, 0);
    if (title_good) acr_hud_text(x + 8, y + 7, title, GREEN);
    else acr_hud_text(x + 8, y + 7, title, GOLD);
    acr_hud_line(x + 6, y + 20, x + w - 6, y + 20, 70, 96, 106, 255);
    acr_hud_text(x + 8, y + 27, "CLIENT ORDER", DIM);
    char s[200];
    acr_brief_sentence(b, s, sizeof s);
    upper(s);
    wrapped(x + 8, y + 40, (int)(w - 16) / 8, s, WHITE);
    const float by = y + 72, bw = (w - 16 - 12) / 3;
    char tmp[24];
    const char *label[3] = { "PAYOUT", "TIME", "WINGMAN" };
    const int field[3] = { ACR_BRIEF_PAYOUT, ACR_BRIEF_TIME, ACR_BRIEF_WINGMAN };
    for (int k = 0; k < 3; ++k) {
        const float bx = x + 8 + k * (bw + 6);
        acr_hud_rect(bx, by, bw, 36, 44, 60, 68, 255, 0);
        acr_hud_text(bx + 5, by + 5, label[k], DIM);
        char v[32];
        SDL_snprintf(v, sizeof v, "%s", acr_brief_choice(field[k], b->v[field[k]], tmp, sizeof tmp));
        upper(v);
        acr_hud_text(bx + 5, by + 20, v, WHITE);
    }
    return y + 126;
}

void brief_copy_text(const net_addr *a, int kind, char *out, int size) {
    acr_brief b;
    acr_brief_encode(a->ip, a->port, kind, &b);
    acr_brief_format(&b, kind, out, size);
}

/* ---------------- the editor ---------------- */

void brief_edit_start(brief_edit *e, int kind) {
    memset(e, 0, sizeof *e);
    e->kind = kind;
}

void brief_edit_say(brief_edit *e, const char *text) {
    SDL_strlcpy(e->message, text, sizeof e->message);
    e->message_ms = 5000;
}

static int page(int field) { return field == ACR_BRIEF_TIME ? 60 : 10; }

brief_result brief_edit_input(brief_edit *e, garage_action a, net_addr *to) {
    if (e->open) {
        const int n = acr_brief_choices(e->row);
        switch (a) {
        case GA_UP: e->pick = (e->pick + n - 1) % n; break;
        case GA_DOWN: e->pick = (e->pick + 1) % n; break;
        case GA_LEFT: case GA_PREV_TAB: e->pick = (e->pick - page(e->row) + n * 8) % n; break;
        case GA_RIGHT: case GA_NEXT_TAB: e->pick = (e->pick + page(e->row)) % n; break;
        case GA_OK:
            e->b.v[e->row] = (uint16_t)e->pick;
            e->open = 0;
            if (e->row < ACR_BRIEF_FIELDS - 1) ++e->row;
            else e->row = ROW_CONFIRM;
            break;
        case GA_CANCEL: e->open = 0; break;
        default: break;
        }
        return BE_NONE;
    }
    switch (a) {
    case GA_UP: e->row = (e->row + ROWS - 1) % ROWS; return BE_NONE;
    case GA_DOWN: e->row = (e->row + 1) % ROWS; return BE_NONE;
    case GA_CANCEL: return BE_CANCEL;
    case GA_OK: break;
    default: return BE_NONE;
    }
    if (e->row < ACR_BRIEF_FIELDS) { e->open = 1; e->pick = e->b.v[e->row]; e->typed[0] = 0; return BE_NONE; }
    if (e->row == ROW_PASTE) return BE_PASTE;
    if (e->row == ROW_CANCEL) return BE_CANCEL;
    if (!acr_brief_decode(&e->b, e->kind, to->ip, &to->port)) {
        brief_edit_say(e, acr_brief_decode(&e->b, !e->kind, to->ip, &to->port)
                              ? (e->kind == ACR_BRIEF_OFFER ? "THAT'S A READ-BACK. PLAYER 2 ENTERS PLAYER 1'S BRIEFING HERE."
                                                            : "THAT'S YOUR OWN KIND OF CODE: ENTER PLAYER 2'S READ-BACK.")
                              : "CODE NOT RECOGNISED: CHECK EACH FIELD AGAINST THE MESSAGE.");
        return BE_NONE;
    }
    return BE_DONE;
}

static int starts_with(const char *word, const char *prefix) {
    for (; *prefix; ++prefix, ++word) {
        int a = (unsigned char)*word, b = (unsigned char)*prefix;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

void brief_edit_text(brief_edit *e, const char *text, uint64_t now_ms) {
    if (e->row >= ACR_BRIEF_FIELDS) return;
    if (!e->open) { e->open = 1; e->pick = e->b.v[e->row]; e->typed[0] = 0; }
    if (now_ms - e->typed_ms > 1200) e->typed[0] = 0;
    e->typed_ms = now_ms;
    for (const char *t = text; *t; ++t) {
        const char c = *t;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) continue;
        const size_t n = strlen(e->typed);
        if (n + 1 < sizeof e->typed) { e->typed[n] = c; e->typed[n + 1] = 0; }
    }
    char tmp[24];
    for (int i = 0; i < acr_brief_choices(e->row); ++i) {
        const char *w = acr_brief_choice(e->row, i, tmp, sizeof tmp);
        if (starts_with(w, e->typed)) { e->pick = i; return; }
    }
}

int brief_edit_paste(brief_edit *e, const char *text) {
    acr_brief b;
    if (!text || !acr_brief_parse(text, &b)) {
        brief_edit_say(e, "THE CLIPBOARD DOESN'T HOLD A BRIEFING OR READ-BACK.");
        return 0;
    }
    e->b = b;
    e->open = 0;
    e->row = ROW_CONFIRM;
    brief_edit_say(e, "PASTED. CHECK IT, THEN CONFIRM.");
    return 1;
}

void brief_edit_draw(brief_edit *e, float hud_w, int elapsed_ms) {
    const float px = hud_w / 2 - PANEL_W / 2, x = px + 12;
    acr_hud_text(px, 12, e->kind == ACR_BRIEF_OFFER ? "ENTER PLAYER 1'S MISSION BRIEFING" : "ENTER PLAYER 2'S READ-BACK", GOLD);
    panel(px, 34, PANEL_W, 400);
    float y = 42;
    const float after = card(x, y, e->kind == ACR_BRIEF_OFFER ? "MISSION BRIEFING" : "READ-BACK", e->kind == ACR_BRIEF_REPLY, &e->b);
    y = after + 4;
    char line[96], tmp[24];
    const float list_x = x + 120;
    float open_y = 0;
    for (int r = 0; r < ROWS; ++r) {
        const int sel = r == e->row;
        if (r < ACR_BRIEF_FIELDS) {
            char v[32];
            SDL_snprintf(v, sizeof v, "%s", acr_brief_choice(r, e->b.v[r], tmp, sizeof tmp));
            upper(v);
            SDL_snprintf(line, sizeof line, "%-12s %s", acr_brief_label(r), v);
        } else {
            static const char *const k_rows[3] = { "CONFIRM AND CONNECT", "PASTE FROM CLIPBOARD", "CANCEL" };
            SDL_snprintf(line, sizeof line, "%s", k_rows[r - ROW_CONFIRM]);
            if (r == ROW_CONFIRM) y += 4;
        }
        if (sel) { acr_hud_rect(x - 6, y - 2, PANEL_W - 12, 12, 60, 70, 40, 255, 1); acr_hud_text(x, y, line, GOLD); open_y = y; }
        else acr_hud_text(x, y, line, WHITE);
        if (r < ACR_BRIEF_FIELDS) acr_hud_text(x + PANEL_W - 40, y, sel && !e->open ? "v" : "", GOLD);
        y += 13;
    }
    if (e->open) {
        /* The drop-down, over the rows, under its field. */
        const int n = acr_brief_choices(e->row);
        const float lw = 200, lh = LIST_ROWS * 12 + 8;
        float ly = open_y + 12;
        if (ly + lh > 430) ly = 430 - lh;
        acr_hud_rect(list_x - 4, ly, lw, lh, 8, 12, 16, 250, 1);
        acr_hud_rect(list_x - 4, ly, lw, lh, 255, 220, 120, 255, 0);
        int top = e->pick - LIST_ROWS / 2;
        if (top < 0) top = 0;
        if (top > n - LIST_ROWS) top = n - LIST_ROWS;
        for (int i = 0; i < LIST_ROWS && top + i < n; ++i) {
            char v[32];
            SDL_snprintf(v, sizeof v, "%s", acr_brief_choice(e->row, top + i, tmp, sizeof tmp));
            upper(v);
            const float ry = ly + 4 + i * 12;
            if (top + i == e->pick) { acr_hud_rect(list_x - 2, ry - 2, lw - 4, 12, 60, 70, 40, 255, 1); acr_hud_text(list_x + 2, ry, v, GOLD); }
            else acr_hud_text(list_x + 2, ry, v, WHITE);
        }
        SDL_snprintf(line, sizeof line, "%d/%d", e->pick + 1, n);
        acr_hud_text(list_x + lw - 12 - 8 * (float)SDL_strlen(line), ly + lh - 10, line, DIM);
    }
    if (e->message_ms > 0) {
        e->message_ms -= elapsed_ms;
        acr_hud_text(px, 442, e->message, GOLD);
    }
    acr_hud_text(px, 460, e->open ? "UP/DOWN: SCROLL  LB/RB: PAGE  A: PICK  B: CLOSE  TYPE: JUMP"
                                  : "UP/DOWN: FIELD  A: OPEN LIST  B: BACK  CTRL+V: PASTE", DIM);
}

/* ---------------- waiting screens ---------------- */

void brief_draw_host(float hud_w, int stun, const net_addr *me, int readback, int waited_s,
                     const char *message, int elapsed_ms) {
    (void)elapsed_ms;
    const float px = hud_w / 2 - PANEL_W / 2, x = px + 12;
    const int cols = (PANEL_W - 24) / 8;
    acr_hud_text(px, 12, "HOST ONLINE (PLAYER 1)", GOLD);
    panel(px, 34, PANEL_W, 400);
    float y = 42;
    char text[400];
    if (stun == 0) {
        y = wrapped(x, y + 6, cols, "CONTACTING THE NETWORK FOR YOUR BRIEFING...", WHITE);
    } else if (stun < 0) {
        y = wrapped(x, y + 6, cols,
                    "COULD NOT FIND YOUR INTERNET ADDRESS: NO ANSWER FROM THE NETWORK.\n\n"
                    "CHECK THAT THIS PC IS ONLINE, OR PLAY OVER LAN / DIRECT IP.", RED);
    } else {
        acr_brief b;
        acr_brief_encode(me->ip, me->port, ACR_BRIEF_OFFER, &b);
        y = card(x, y, "MISSION BRIEFING", 0, &b);
        if (!readback) {
            y = wrapped(x, y + 4, cols,
                        "SEND THIS BRIEFING TO PLAYER 2: READ IT OUT, OR COPY IT INTO A CHAT.\n"
                        "PLAYER 2 PICKS NETWORK > JOIN WITH A MISSION BRIEFING.\n\n"
                        "IF THEIR SCREEN SHOWS AN URGENT MISSION REQUEST, THEY'LL READ BACK A CODE: "
                        "PRESS A TO ENTER IT.", WHITE);
        } else {
            y = wrapped(x, y + 4, cols, "READ-BACK RECEIVED. LINKING UP WITH PLAYER 2...", GREEN);
        }
        SDL_snprintf(text, sizeof text, "\nWAITING FOR PLAYER 2...  %d S\n\nDIRECT IP (IF UDP PORT %d IS FORWARDED): %d.%d.%d.%d:%d",
                     waited_s, NET_PORT_DEFAULT, me->ip[0], me->ip[1], me->ip[2], me->ip[3], NET_PORT_DEFAULT);
        y = wrapped(x, y, cols, text, DIM);
    }
    if (message && message[0]) acr_hud_text(px, 442, message, GOLD);
    acr_hud_text(px, 460, stun == 1 ? "X / C: COPY BRIEFING   A: ENTER READ-BACK   B / ESC: CANCEL" : "B / ESC: CANCEL", DIM);
}

void brief_draw_guest(float hud_w, int stun, const net_addr *me, uint64_t since_ms, const char *message) {
    const float px = hud_w / 2 - PANEL_W / 2, x = px + 12;
    const int cols = (PANEL_W - 24) / 8;
    acr_hud_text(px, 12, "JOIN ONLINE (PLAYER 2)", GOLD);
    panel(px, 34, PANEL_W, 400);
    float y = 50;
    char text[300];
    if (since_ms < BRIEF_TRY_MS) {
        SDL_snprintf(text, sizeof text, "MISSION ACCEPTED. CONTACTING THE CLIENT...  %d S", (int)(since_ms / 1000));
        wrapped(x, y, cols, text, WHITE);
        acr_hud_text(px, 460, "B / ESC: CANCEL", DIM);
        return;
    }
    if (since_ms < BRIEF_TRY_MS + BRIEF_URGENT_MS) {
        /* The banner flashes across the screen, three times. */
        const int on = ((since_ms - BRIEF_TRY_MS) / 400) % 2 == 0;
        if (on) {
            acr_hud_rect(0, 196, hud_w, 52, 120, 20, 16, 235, 1);
            acr_hud_line(0, 198, hud_w, 198, 255, 90, 70, 255);
            acr_hud_line(0, 245, hud_w, 245, 255, 90, 70, 255);
            const char *t = "URGENT MISSION REQUEST";
            acr_hud_text(hud_w / 2 - 4 * (float)SDL_strlen(t), 212, t, 255, 230, 200);
            const char *u = "THE CLIENT NEEDS YOUR READ-BACK";
            acr_hud_text(hud_w / 2 - 4 * (float)SDL_strlen(u), 228, u, 255, 160, 140);
        }
        return;
    }
    if (stun == 1) {
        acr_brief b;
        acr_brief_encode(me->ip, me->port, ACR_BRIEF_REPLY, &b);
        y = card(x, 42, "READ-BACK", 1, &b);
        y = wrapped(x, y + 4, cols,
                    "PLAYER 1'S ROUTER DIDN'T LET US IN YET. READ THIS BACK TO PLAYER 1, "
                    "OR COPY IT INTO THE CHAT.\n"
                    "PLAYER 1 PRESSES A ON THEIR BRIEFING SCREEN AND ENTERS IT.", WHITE);
    } else {
        if (stun == 0) y = wrapped(x, y, cols, "PREPARING YOUR READ-BACK...", WHITE);
        else y = wrapped(x, y, cols, "COULD NOT REACH THE NETWORK TO MAKE A READ-BACK.\n\n"
                                     "ASK PLAYER 1 TO FORWARD UDP PORT 7777 AND USE DIRECT IP, OR PLAY OVER LAN.", RED);
    }
    SDL_snprintf(text, sizeof text, "\nSTILL TRYING...  %d S", (int)(since_ms / 1000));
    wrapped(x, y, cols, text, DIM);
    if (message && message[0]) acr_hud_text(px, 442, message, GOLD);
    acr_hud_text(px, 460, stun == 1 ? "X / C: COPY READ-BACK   B / ESC: CANCEL" : "B / ESC: CANCEL", DIM);
}
