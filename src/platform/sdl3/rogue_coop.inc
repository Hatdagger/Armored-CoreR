/* Rogue Arena co-op screens (docs/ROGUE-ARENA-COOP.md), included at the end
 * of rogue.c so they share its drawing, sounds, stages and prize screen.
 * The rules are in src/gen1/acr_gen1_coop.c.
 *
 * This first step plays on one PC: player 1 on the keyboard or controller
 * 1, player 2 on controller 2, split screen in fights; both controllers
 * drive the menus, which say whose turn it is. Online (rollback, the host
 * keeping the run) comes next. */

#include "acr_gen1_coop.h"

enum { CS_MENU, CS_HUB, CS_STAGES, CS_STASH, CS_STASH_ASK, CS_VS, CS_RESULT, CS_REJOIN, CS_LOST, CS_DUEL,
       CS_CLEARED };

static struct {
    acr_gen1_coop run;
    int live;                                 /* a run exists (file or new) */
    int screen, cur, ask_cur, ask_n, ask_put[4];
    int who;                                  /* whose turn: rejoin, prize, the stash's equip */
    int prize_who;                            /* the player whose prize screen is borrowed */
    int last_down[2];                         /* the result screen: who was destroyed */
    int last_draw;                            /* the duel was a draw */
} C;

static const char *const k_coop_file[3] = { "rogue-coop-run.toml", "rogue-coop-p1.toml", "rogue-coop-p2.toml" };

static void coop_save(void) {
    if (!C.live) return;
    static char text[65536];
    char path[1200];
    size_t n = acr_gen1_coop_write(&C.run, text, sizeof text);
    file_path(k_coop_file[0], path, sizeof path);
    if (n < sizeof text) SDL_SaveFile(path, text, n);
    for (int p = 0; p < 2; ++p) {
        n = acr_gen1_save_write(&C.run.player[p].hangar, text, sizeof text);
        file_path(k_coop_file[1 + p], path, sizeof path);
        if (n < sizeof text) SDL_SaveFile(path, text, n);
    }
}

static int coop_load(void) {
    char path[1200];
    char *t[3];
    for (int i = 0; i < 3; ++i) { file_path(k_coop_file[i], path, sizeof path); t[i] = SDL_LoadFile(path, NULL); }
    int ok = t[0] && t[1] && t[2];
    static acr_gen1_coop c;
    SDL_zero(c);
    for (int p = 0; ok && p < 2; ++p) ok = acr_gen1_save_read(&c.player[p].hangar, t[1 + p]);
    ok = ok && acr_gen1_coop_read(&c, t[0]);
    /* The ladder has to name opponents this import has. */
    const acr_gen1_rogue_world w = world();
    for (int i = 1; ok && i < SLOTS; ++i)
        for (int k = 0; k < 2; ++k)
            if (c.pair[i][k].kind == ACR_GEN1_ROGUE_FOE && !acr_gen1_rogue_find(&w, c.pair[i][k].arena, c.pair[i][k].rank)) ok = 0;
    if (ok) C.run = c;
    for (int i = 0; i < 3; ++i) SDL_free(t[i]);
    return ok;
}

static int coop_stage(void) {
    for (int i = 0; i < R.nstages; ++i) if (k_stage[R.stages[i]].id == C.run.stage) return C.run.stage;
    return R.nstages ? k_stage[R.stages[0]].id : 27;
}

static int coop_rank(void) { return SLOTS - C.run.wins; }

/* Where the run goes next: each destroyed partner's choice, then each
 * player's prize screen (player 1 first), then the hub, the duel or the
 * end. */
static void coop_next(void) {
    C.cur = 0;
    if (C.run.over) { C.screen = CS_LOST; return; }
    if (C.run.cleared) { C.screen = CS_CLEARED; return; }
    for (int p = 0; p < 2; ++p)
        if (C.run.rejoin[p]) { C.who = p; C.screen = CS_REJOIN; return; }
    for (int p = 0; p < 2; ++p)
        if (acr_gen1_coop_prize_due(&C.run, p)) {
            C.prize_who = p;
            R.solo = R.run;
            R.run = C.run.player[p];
            R.borrow = &C.run.player[p];
            SDL_snprintf(R.whose, sizeof R.whose, "PLAYER %d", p + 1);
            to_hub();                         /* the prize screen, on this player's side */
            return;
        }
    C.screen = C.run.duel_due ? CS_DUEL : CS_HUB;
}

static void coop_prize_done(void) {
    acr_gen1_coop_collect(&C.run, C.prize_who);
    coop_save();
    coop_next();
}

static void coop_open(void) {
    R.screen = RS_COOP;
    C.live = coop_load();
    C.screen = CS_MENU;
    C.cur = 0;
}

static void coop_new_arena(void) {
    const acr_gen1_rogue_world w = world();
    /* Player 1's names are the garage's; player 2 has none on this PC yet. */
    const char *pilot[2] = { R.pilot, "PLAYER 2" }, *ac[2] = { R.ac, "PARTNER" };
    static acr_gen1_coop c;
    if (!acr_gen1_coop_new(&c, &w, fresh_seed(), pilot, ac)) { say("NO ARENA COULD BE MADE FROM THIS IMPORT."); return; }
    c.stage = (uint8_t)coop_stage();
    C.run = c;
    C.live = 1;
    coop_save();
    coop_next();
}

/* ---- the fights ---- */

static void coop_player_side(rogue_fight *g, int k, int p) {
    const acr_gen1_rogue *r = &C.run.player[p];
    for (int s = 0; s < SLOT_COUNT; ++s) g->parts[k][s] = r->hangar.fitted[s];
    memcpy(g->colours[k], r->hangar.colours, sizeof g->colours[k]);
    g->plus[k] = r->plus ? 9 : 0;
    SDL_snprintf(g->name[k], sizeof g->name[k], "%s / %s", r->hangar.ac_name, r->hangar.pilot_name);
}

static int coop_prepare_pair(void) {
    const acr_gen1_rogue_world w = world();
    acr_gen1_rogue_foe f[2];
    if (!acr_gen1_coop_next_pair(&C.run)) return 0;
    for (int k = 0; k < 2; ++k) if (!acr_gen1_coop_rung(&C.run, &w, C.run.wins + 1, k, &f[k])) return 0;
    rogue_fight *g = &R.fight;
    SDL_zero(*g);
    g->humans = 2;
    g->fighters = 4;
    for (int p = 0; p < 2; ++p) coop_player_side(g, p, p);
    for (int k = 0; k < 2; ++k) {
        for (int s = 0; s < SLOT_COUNT; ++s) g->parts[2 + k][s] = f[k].parts[s];
        memcpy(g->colours[2 + k], f[k].colours, sizeof g->colours[2 + k]);
        g->plus[2 + k] = f[k].ai.rebel;
        g->pair_ai[k] = f[k].ai;
        name_of(&f[k], g->name[2 + k], sizeof g->name[2 + k]);
    }
    g->ai = f[0].ai;
    g->stage = coop_stage();
    return 1;
}

/* The final duel: player 1 against player 2 on the host's stage, as built. */
static void coop_prepare_duel(void) {
    rogue_fight *g = &R.fight;
    SDL_zero(*g);
    g->humans = 2;
    g->fighters = 2;
    for (int p = 0; p < 2; ++p) coop_player_side(g, p, p);
    g->stage = coop_stage();
}

void rogue_coop_fight_over(int result, int p1_down, int p2_down) {
    if (!C.live) return;
    R.screen = RS_COOP;
    C.cur = 0;
    C.last_draw = 0;
    if (C.run.duel_due) {
        if (result == 1 || result == 2) acr_gen1_coop_duel(&C.run, result - 1);
        else C.last_draw = 1;                 /* a draw is fought again */
        coop_save();
        coop_next();
        return;
    }
    if (result == 1) {
        acr_gen1_coop_won(&C.run, p1_down, p2_down);
        C.last_down[0] = p1_down;
        C.last_down[1] = p2_down;
        C.screen = CS_RESULT;
    } else {
        acr_gen1_coop_lost(&C.run);
        C.screen = CS_LOST;
    }
    coop_save();
}

/* ---- input ---- */

enum { CH_NEXT, CH_STASH, CH_STAGE, CH_QUIT, CH_COUNT };

static int coop_menu_rows(int ids[4]) {
    int n = 0;
    if (C.live && !C.run.over && !C.run.cleared) ids[n++] = MI_CONTINUE;
    if (C.live && C.run.over) ids[n++] = MI_RETRY;
    ids[n++] = MI_NEW;
    ids[n++] = MI_BACK;
    return n;
}

static rogue_command coop_input(garage_action a) {
    const acr_gen1_rogue_world w = world();
    switch (C.screen) {
    case CS_MENU: {
        int ids[4];
        const int n = coop_menu_rows(ids);
        C.cur = step(C.cur, n, a);
        if (C.cur >= n) C.cur = 0;
        if (a == GA_CANCEL) { R.screen = RS_MODE; R.cur = 1; return RC_NONE; }
        if (a != GA_OK) return RC_NONE;
        switch (ids[C.cur]) {
        case MI_CONTINUE: coop_next(); break;
        case MI_RETRY: if (acr_gen1_coop_retry(&C.run, &w)) { coop_save(); coop_next(); } break;
        case MI_NEW: coop_new_arena(); break;
        default: R.screen = RS_MODE; R.cur = 1; break;
        }
        return RC_NONE;
    }
    case CS_HUB:
        C.cur = step(C.cur, CH_COUNT, a);
        if (a == GA_CANCEL) { C.cur = CH_QUIT; return RC_NONE; }
        if (a != GA_OK) return RC_NONE;
        switch (C.cur) {
        case CH_NEXT:
            for (int p = 0; p < 2; ++p)
                if (acr_gen1_rogue_check(&C.run.player[p], &R.cat)) {
                    say(p ? "PLAYER 2'S AC CAN'T SORTIE AS IT IS." : "PLAYER 1'S AC CAN'T SORTIE AS IT IS.");
                    return RC_NONE;
                }
            if (!R.nstages) { say("RE-IMPORT YOUR DISC: NO STAGES FOUND."); break; }
            if (!coop_prepare_pair()) { say("THE NEXT PAIR COULD NOT BE LOADED."); break; }
            C.screen = CS_VS;
            break;
        case CH_STASH:
            if (!C.run.stash_n) { say("NO PARTS SAVED: TAKE A PRIZE AND SAVE IT FOR LATER."); break; }
            C.screen = CS_STASH; C.cur = 0; C.who = 0;
            acr_gen1_coop_hold(&C.run, C.who, 0);
            break;
        case CH_STAGE:
            C.screen = CS_STAGES;
            C.cur = 0;
            for (int i = 0; i < R.nstages; ++i) if (k_stage[R.stages[i]].id == coop_stage()) C.cur = i;
            break;
        default: coop_save(); C.screen = CS_MENU; C.cur = 0; R.screen = RS_MODE; R.cur = 1; return RC_LEAVE;
        }
        return RC_NONE;
    case CS_STAGES:                           /* the host picks */
        C.cur = step(C.cur, R.nstages, a);
        if (a == GA_PREV_TAB || a == GA_NEXT_TAB) {
            uint64_t s = SDL_GetTicksNS();
            C.cur = (int)acr_gen1_rogue_below(&s, (uint32_t)R.nstages);
            a = GA_OK;
        }
        if (a == GA_CANCEL) { C.screen = CS_HUB; C.cur = CH_STAGE; }
        if (a == GA_OK && R.nstages) {
            C.run.stage = (uint8_t)k_stage[R.stages[C.cur]].id;
            coop_save();
            C.screen = CS_HUB; C.cur = CH_STAGE;
        }
        return RC_NONE;
    case CS_STASH: {
        if (a == GA_LEFT || a == GA_RIGHT) {  /* whose AC it goes on */
            acr_gen1_coop_hold(&C.run, C.who, -1);
            C.who = !C.who;
            if (C.cur < C.run.stash_n && !acr_gen1_coop_hold(&C.run, C.who, C.cur)) say("THE OTHER PLAYER IS ON THAT PART.");
            return RC_NONE;
        }
        const int was = C.cur;
        C.cur = step(C.cur, C.run.stash_n + 1, a);
        if (C.cur != was) {
            acr_gen1_coop_hold(&C.run, C.who, -1);
            if (C.cur < C.run.stash_n && !acr_gen1_coop_hold(&C.run, C.who, C.cur)) say("THE OTHER PLAYER IS ON THAT PART.");
        }
        if (a == GA_CANCEL || (a == GA_OK && C.cur == C.run.stash_n)) {
            acr_gen1_coop_hold(&C.run, 0, -1);
            acr_gen1_coop_hold(&C.run, 1, -1);
            C.screen = CS_HUB; C.cur = CH_STASH;
            return RC_NONE;
        }
        if (a == GA_OK) {
            acr_gen1_rogue_offer o;
            if (!acr_gen1_coop_stash_fit(&C.run, &w, C.who, C.cur, &o)) { say("IT CAN'T BE EQUIPPED WITH THOSE PARTS."); return RC_NONE; }
            C.ask_n = 0;
            for (int k = 0; k < o.nput; ++k) C.ask_put[C.ask_n++] = k;
            C.ask_put[C.ask_n++] = -1;
            C.ask_cur = 0;
            C.screen = CS_STASH_ASK;
        }
        return RC_NONE;
    }
    case CS_STASH_ASK:
        C.ask_cur = step(C.ask_cur, C.ask_n, a);
        if (a == GA_CANCEL || (a == GA_OK && C.ask_put[C.ask_cur] < 0)) { C.screen = CS_STASH; return RC_NONE; }
        if (a == GA_OK) {
            if (acr_gen1_coop_stash_take(&C.run, &w, C.who, C.cur, C.ask_put[C.ask_cur])) {
                say("EQUIPPED.");
                R.snd = SFX_UI_EQUIP;
                coop_save();
            }
            if (!C.run.stash_n) { C.screen = CS_HUB; C.cur = CH_STASH; return RC_NONE; }
            C.screen = CS_STASH;
            if (C.cur >= C.run.stash_n) C.cur = C.run.stash_n - 1;
            acr_gen1_coop_hold(&C.run, C.who, C.cur);
        }
        return RC_NONE;
    case CS_VS:
        if (a == GA_CANCEL) { C.screen = CS_HUB; C.cur = CH_NEXT; return RC_NONE; }
        if (a == GA_OK) return RC_FIGHT;
        R.snd = -2;
        return RC_NONE;
    case CS_RESULT:
        if (a != GA_OK && a != GA_CANCEL) { R.snd = -2; return RC_NONE; }
        coop_next();
        return RC_NONE;
    case CS_REJOIN: {
        C.cur = step(C.cur, 2, a);
        if (a != GA_OK) return RC_NONE;
        if (acr_gen1_coop_rejoin(&C.run, &w, C.who, C.cur == 0)) {
            say(C.cur == 0 ? "YOUR AC IS BACK." : "A NEW AC IS READY.");
            coop_save();
            coop_next();
        } else say("NO NEW AC COULD BE MADE.");
        return RC_NONE;
    }
    case CS_DUEL:
        if (a == GA_CANCEL) { coop_save(); C.screen = CS_MENU; C.cur = 0; return RC_NONE; }
        if (a != GA_OK) { R.snd = -2; return RC_NONE; }
        coop_prepare_duel();
        return RC_FIGHT;
    case CS_LOST:
        C.cur = step(C.cur, 3, a);
        if (a != GA_OK) return RC_NONE;
        if (C.cur == 0) { if (acr_gen1_coop_retry(&C.run, &w)) { coop_save(); coop_next(); } }
        else if (C.cur == 1) coop_new_arena();
        else { C.screen = CS_MENU; C.cur = 0; R.screen = RS_MODE; R.cur = 1; return RC_LEAVE; }
        return RC_NONE;
    case CS_CLEARED:
        C.cur = step(C.cur, 2, a);
        if (a != GA_OK) return RC_NONE;
        if (C.cur == 0) coop_new_arena();
        else { C.screen = CS_MENU; C.cur = 0; R.screen = RS_MODE; R.cur = 1; return RC_LEAVE; }
        return RC_NONE;
    }
    return RC_NONE;
}

/* ---- drawing ---- */

static void coop_header(float hud_w, const char *title) {
    char line[96];
    acr_hud_text(24, 16, title, HILITE);
    if (!C.live) return;
    SDL_snprintf(line, sizeof line, "P1 %ld P   P2 %ld P", (long)C.run.player[0].hangar.credits,
                 (long)C.run.player[1].hangar.credits);
    acr_hud_text(hud_w - 24 - 8.0f * (float)SDL_strlen(line), 16, line, WHITE);
}

static void coop_foe_name(int rung, int k, char *out, size_t size) {
    const acr_gen1_rogue_world w = world();
    acr_gen1_rogue_foe f;
    out[0] = 0;
    if (!acr_gen1_coop_rung(&C.run, &w, rung, k, &f)) return;
    SDL_snprintf(out, size, "%s%s", f.ac[0] ? f.ac : f.pilot, C.run.pair[rung][k].kind == ACR_GEN1_ROGUE_GHOST ? "*" : "");
}

/* One player's card on the hub: AC, P, upgrades. */
static void coop_card(float x, float y, int p) {
    char line[96];
    const acr_gen1_rogue *r = &C.run.player[p];
    SDL_snprintf(line, sizeof line, "PLAYER %d  %s", p + 1, r->hangar.ac_name);
    acr_hud_text(x, y, line, p ? 235 : 120, p ? 90 : 170, p ? 80 : 235);
    SDL_snprintf(line, sizeof line, "%ld P   %s%s%s", (long)r->hangar.credits, r->plus ? "HUMAN PLUS  " : "",
                 r->opts ? "ALL OPTIONS  " : "", r->income ? "+P PER WIN" : "");
    acr_hud_text(x + 16, y + 14, line, WHITE);
    const uint8_t hand = r->hangar.fitted[ACR_GEN1_SLOT_HAND];
    SDL_snprintf(line, sizeof line, "%-12s %-12s %-12s", pname(ACR_GEN1_CAT_ARMS, r->hangar.fitted[ACR_GEN1_SLOT_ARMS]),
                 hand == ACR_GEN1_NO_PART ? "" : pname(ACR_GEN1_CAT_HAND, hand),
                 pname(ACR_GEN1_CAT_LEGS, r->hangar.fitted[ACR_GEN1_SLOT_LEGS]));
    acr_hud_text(x + 16, y + 28, line, DIM);
}

static void coop_ladder(float hud_w) {
    char line[128], a[24], b[24];
    const float rx = hud_w - 24 - 340;
    panel(rx, 40, 340, 22 + SLOTS * 18.0f);
    for (int i = SLOTS - 1; i >= 0; --i) {
        const float y = 50 + (SLOTS - 1 - i) * 18.0f;
        if (i == C.run.wins) SDL_snprintf(line, sizeof line, "%2d  YOU TWO", SLOTS - i);
        else if (i == 0) SDL_snprintf(line, sizeof line, "%2d  -", SLOTS - i);
        else {
            coop_foe_name(i, 0, a, sizeof a);
            coop_foe_name(i, 1, b, sizeof b);
            SDL_snprintf(line, sizeof line, "%2d  %.17s + %.17s", SLOTS - i, a, b);
        }
        if (i == C.run.wins) acr_hud_text(rx + 12, y, line, HILITE);
        else if (i < C.run.wins) acr_hud_text(rx + 12, y, line, DIM);
        else if (i == C.run.wins + 1) acr_hud_text(rx + 12, y, line, BAD);
        else acr_hud_text(rx + 12, y, line, WHITE);
    }
    if (C.run.ghosts) acr_hud_text(rx + 12, 50 + SLOTS * 18.0f + 4, "* A GHOST: YOUR LOSING ACS FROM BEFORE", DIM);
}

static void coop_draw(float hud_w, int elapsed_ms) {
    (void)elapsed_ms;
    char line[128], title[96];
    const float lx = 24, cw = hud_w - 48;
    background(hud_w, C.live ? acr_gen1_rogue_belt(C.run.wins) : 0);
    switch (C.screen) {
    case CS_MENU: {
        coop_header(hud_w, "ROGUE ARENA  CO-OP");
        panel(lx, 40, cw, 300);
        acr_hud_text(lx + 16, 56, "TWO PLAYERS, ONE LADDER: 19 PAIRS, 2 AGAINST 2. NO TIME LIMIT.", WHITE);
        acr_hud_text(lx + 16, 72, "EACH WIN: +1 P EACH AND A PRIZE EACH. BOTH DOWN ENDS THE RUN.", DIM);
        int ids[4];
        const int n = coop_menu_rows(ids);
        if (C.cur >= n) C.cur = 0;
        for (int i = 0; i < n; ++i) row(lx + 24, 110 + i * 22.0f, 260, k_menu_name[ids[i]], i == C.cur, 0);
        if (C.live) {
            const char *state = C.run.cleared ? "CLEARED" : C.run.over ? "LOST" : C.run.duel_due ? "FINAL DUEL" : "IN PROGRESS";
            SDL_snprintf(line, sizeof line, "LAST RUN: %s  RANK %d  BEST %d  ATTEMPT %d  GHOSTS %d", state, coop_rank(),
                         SLOTS - C.run.best, C.run.attempt + 1, C.run.ghosts / 2);
            acr_hud_text(lx + 16, 230, line, WHITE);
            SDL_snprintf(line, sizeof line, "SEED %016llX", (unsigned long long)C.run.seed);
            acr_hud_text(lx + 16, 246, line, DIM);
        }
        acr_hud_text(lx + 16, 280, "THIS PC: PLAYER 1 KEYBOARD / CONTROLLER 1, PLAYER 2 CONTROLLER 2.", DIM);
        acr_hud_text(lx + 16, 320, "UP / DOWN  CHOOSE    ENTER / A  OK    ESC / B  BACK", DIM);
        break;
    }
    case CS_VS: {
        const rogue_fight *g = &R.fight;
        SDL_snprintf(title, sizeof title, "RANK %d  ON %s", coop_rank() - 1, stage_name(g->stage));
        coop_header(hud_w, title);
        panel(lx, 60, cw, 280);
        text_mid(hud_w * 0.5f, 80, "2 AGAINST 2", 16, HILITE);
        const float cx[2] = { lx + cw * 0.25f, lx + cw * 0.75f };
        text_mid(cx[0], 130, "YOU TWO", 10, WHITE);
        text_mid(cx[1], 130, "THE PAIR", 10, WHITE);
        text_mid(hud_w * 0.5f, 170, "VS", 24, HILITE);
        for (int k = 0; k < 2; ++k) {
            text_mid(cx[0], 160 + k * 24.0f, g->name[k], 8, k ? 235 : 120, k ? 90 : 170, k ? 80 : 235);
            text_mid(cx[1], 160 + k * 24.0f, g->name[2 + k], 8, WHITE);
        }
        text_mid(hud_w * 0.5f, 240, "NO TIME LIMIT. FRIENDLY FIRE IS ON; LOCK-ON NEVER PICKS YOUR PARTNER.", 8, DIM);
        text_mid(hud_w * 0.5f, 256, "DESTROYED? YOUR VIEW FOLLOWS YOUR PARTNER UNTIL THE FIGHT ENDS.", 8, DIM);
        text_mid(hud_w * 0.5f, 310, "ENTER / A  FIGHT    ESC / B  BACK", 8, DIM);
        break;
    }
    case CS_HUB: case CS_STAGES: case CS_STASH: case CS_STASH_ASK: {
        SDL_snprintf(title, sizeof title, "ROGUE ARENA CO-OP  RANK %d  %s BELT", coop_rank(),
                     acr_gen1_rogue_belt_name(acr_gen1_rogue_belt(C.run.wins)));
        coop_header(hud_w, title);
        const float pw = cw - 360;
        panel(lx, 40, pw, 300);
        static const char *const k_rows[CH_COUNT] = { "NEXT BATTLE", "SWAP PARTS", "STAGE", "QUIT" };
        for (int i = 0; i < CH_COUNT; ++i) {
            row(lx + 16, 56 + i * 24.0f, 220, k_rows[i], C.screen == CS_HUB && i == C.cur, C.screen != CS_HUB);
            if (i == CH_STASH) {
                SDL_snprintf(line, sizeof line, C.run.stash_n ? "%d SAVED (SHARED)" : "NONE", C.run.stash_n);
                acr_hud_text(lx + 150, 56 + i * 24.0f, line, DIM);
            }
            if (i == CH_STAGE) {
                SDL_snprintf(line, sizeof line, "%s (HOST)", stage_name(coop_stage()));
                acr_hud_text(lx + 130, 56 + i * 24.0f, line, DIM);
            }
        }
        {
            char a[24], b[24];
            coop_foe_name(C.run.wins + 1, 0, a, sizeof a);
            coop_foe_name(C.run.wins + 1, 1, b, sizeof b);
            SDL_snprintf(line, sizeof line, "NEXT  RANK %d  %s + %s", coop_rank() - 1, a, b);
            acr_hud_text(lx + 16, 162, line, WHITE);
        }
        coop_card(lx + 16, 190, 0);
        coop_card(lx + 16, 240, 1);
        SDL_snprintf(line, sizeof line, "WINS %d  BEST RANK %d  ATTEMPT %d", C.run.wins, SLOTS - C.run.best, C.run.attempt + 1);
        acr_hud_text(lx + 16, 300, line, DIM);
        coop_ladder(hud_w);
        if (C.screen == CS_STAGES) {
            panel(lx + 40, 120, 300, 40 + R.nstages * 20.0f);
            acr_hud_text(lx + 56, 130, "STAGE (THE HOST PICKS)", HILITE);
            for (int i = 0; i < R.nstages; ++i) row(lx + 64, 150 + i * 20.0f, 260, k_stage[R.stages[i]].name, i == C.cur, 0);
            acr_hud_text(lx + 56, 150 + R.nstages * 20.0f, "Q / LB  RANDOM", DIM);
        }
        if (C.screen == CS_STASH || C.screen == CS_STASH_ASK) {
            const acr_gen1_rogue_world w = world();
            const float h = 90 + (C.run.stash_n + 1) * 20.0f;
            panel(lx + 8, 60, pw - 16, h);
            SDL_snprintf(line, sizeof line, "SWAP PARTS  < FOR PLAYER %d >", C.who + 1);
            acr_hud_text(lx + 36, 70, line, HILITE);
            acr_hud_text(lx + 36, 84, "SHARED STASH. WHAT IT REPLACES IS GONE.", DIM);
            for (int i = 0; i <= C.run.stash_n; ++i) {
                if (i == C.run.stash_n) { row(lx + 44, 104 + i * 20.0f, 200, "BACK", C.cur == i, 0); break; }
                const acr_gen1_coop_item *it = &C.run.stash[i];
                acr_gen1_rogue_offer o;
                const int fits = acr_gen1_coop_stash_fit(&C.run, &w, C.who, i, &o);
                SDL_snprintf(line, sizeof line, "%-14s %-12s P%d'S", pname(it->cat, it->index), cat_label(it->cat), it->owner + 1);
                row(lx + 44, 104 + i * 20.0f, pw - 48, line, C.cur == i, !fits);
                if (C.cur == i && fits) {
                    char lost[80];
                    lost_text(&o, 0, lost, sizeof lost);
                    SDL_snprintf(line, sizeof line, "REPLACES: %s", lost[0] ? lost : "NOTHING");
                    acr_hud_text(lx + 36, 60 + h - 18, line, DIM);
                }
            }
            if (C.screen == CS_STASH_ASK) {
                acr_gen1_rogue_offer o;
                acr_gen1_coop_stash_fit(&C.run, &w, C.who, C.cur, &o);
                panel(lx + 260, 160, 220, 20 + C.ask_n * 20.0f);
                for (int k = 0; k < C.ask_n; ++k)
                    row(lx + 280, 170 + k * 20.0f, 190, C.ask_put[k] < 0 ? "BACK" : put_name(&o, C.ask_put[k]), k == C.ask_cur, 0);
            }
        }
        break;
    }
    case CS_RESULT: {
        SDL_snprintf(title, sizeof title, "PAIR DEFEATED  RANK %d", coop_rank());
        coop_header(hud_w, title);
        panel(lx, 120, cw, 200);
        text_mid(hud_w * 0.5f, 140, "YOU WIN", 24, HILITE);
        acr_hud_text(lx + 30, 190, "+1 P EACH (AND ANY +P PER WIN). A PRIZE SCREEN EACH, PLAYER 1 FIRST.", WHITE);
        for (int p = 0; p < 2; ++p)
            if (C.last_down[p]) {
                SDL_snprintf(line, sizeof line, "PLAYER %d WAS DESTROYED: KEEP THE OLD AC OR TAKE A NEW ONE NEXT.", p + 1);
                acr_hud_text(lx + 30, 210 + p * 16.0f, line, BAD);
            }
        acr_hud_text(lx + 30, 290, "ENTER / A  GO ON", DIM);
        break;
    }
    case CS_REJOIN: {
        SDL_snprintf(title, sizeof title, "PLAYER %d REJOINS", C.who + 1);
        coop_header(hud_w, title);
        panel(lx, 80, cw, 240);
        int parts = 0;
        int32_t pts = 0;
        acr_gen1_coop_keep_cost(&C.run, C.who, &parts, &pts);
        SDL_snprintf(line, sizeof line, "PLAYER %d, YOUR AC WAS DESTROYED, BUT YOUR TEAM WON.", C.who + 1);
        acr_hud_text(lx + 30, 100, line, WHITE);
        SDL_snprintf(line, sizeof line, "KEEP YOUR AC   COSTS YOUR %d SAVED PART%s AND ALL YOUR %ld P", parts, parts == 1 ? "" : "S", (long)pts);
        row(lx + 40, 140, cw - 60, line, C.cur == 0, 0);
        row(lx + 40, 164, cw - 60, "NEW RANDOM AC  FREE: YOUR P AND SAVED PARTS STAY", C.cur == 1, 0);
        coop_card(lx + 30, 210, C.who);
        acr_hud_text(lx + 30, 300, "UP / DOWN  CHOOSE    ENTER / A  OK", DIM);
        break;
    }
    case CS_DUEL: {
        coop_header(hud_w, "FINAL DUEL");
        panel(lx, 100, cw, 220);
        text_mid(hud_w * 0.5f, 120, "RANK 1'S PAIR IS DOWN.", 12, HILITE);
        SDL_snprintf(line, sizeof line, "NOW PLAYER 1 AGAINST PLAYER 2 ON %s, AS YOU ARE BUILT.", stage_name(coop_stage()));
        acr_hud_text(lx + 30, 160, line, WHITE);
        acr_hud_text(lx + 30, 176, "THE WINNER TAKES RANK 1, THE LOSER RANK 2. NO TIME LIMIT.", WHITE);
        if (C.last_draw) acr_hud_text(lx + 30, 200, "A DRAW: FIGHT AGAIN.", BAD);
        coop_card(lx + 30, 220, 0);
        coop_card(hud_w * 0.5f, 220, 1);
        acr_hud_text(lx + 30, 300, "ENTER / A  FIGHT    ESC / B  LATER", DIM);
        break;
    }
    case CS_LOST: {
        coop_header(hud_w, "DEFEATED");
        panel(lx, 120, cw, 200);
        SDL_snprintf(line, sizeof line, "BOTH ACS DOWN AT RANK %d.  BEST RANK %d.", coop_rank(), SLOTS - C.run.best);
        acr_hud_text(lx + 30, 140, line, WHITE);
        if (C.run.wins) {
            SDL_snprintf(line, sizeof line, "RETRY: YOUR TWO ACS STAY BEHIND AS A GHOST PAIR AT RANK %d.", coop_rank());
            acr_hud_text(lx + 30, 156, line, DIM);
        }
        static const char *const k_lost[3] = { "RETRY ARENA", "NEW ARENA", "QUIT" };
        for (int i = 0; i < 3; ++i) row(lx + 40, 190 + i * 22.0f, 260, k_lost[i], i == C.cur, 0);
        acr_hud_text(lx + 30, 280, "RETRY: THE SAME 19 PAIRS, NEW RANDOM ACS AT 0 P, THE STASH EMPTIED.", DIM);
        break;
    }
    case CS_CLEARED: {
        coop_header(hud_w, "ROGUE ARENA CO-OP CLEARED");
        panel(lx, 100, cw, 220);
        const int win = C.run.duel_winner;
        SDL_snprintf(line, sizeof line, "RANK 1  PLAYER %d  %s", win + 1, C.run.player[win].hangar.ac_name);
        text_mid(hud_w * 0.5f, 124, line, 14, HILITE);
        SDL_snprintf(line, sizeof line, "RANK 2  PLAYER %d  %s", 2 - win, C.run.player[1 - win].hangar.ac_name);
        text_mid(hud_w * 0.5f, 150, line, 10, WHITE);
        SDL_snprintf(line, sizeof line, "ATTEMPTS %d   GHOST PAIRS %d   SEED %016llX", C.run.attempt + 1, C.run.ghosts / 2,
                     (unsigned long long)C.run.seed);
        acr_hud_text(lx + 30, 180, line, DIM);
        static const char *const k_done[2] = { "NEW ARENA", "BACK TO THE GARAGE" };
        for (int i = 0; i < 2; ++i) row(lx + 40, 220 + i * 22.0f, 260, k_done[i], i == C.cur, 0);
        break;
    }
    }
}
