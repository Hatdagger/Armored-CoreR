#include "garage.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "../../model/acr_model.h"
#include "../../render/acr_render.h"
#include "hangar_scene.h"
#include "sfx.h"

/* ---- text (one table, so it can be translated later) ---- */

/* The game's main menu and its sub-menus (user, 2026-11-19): GARAGE,
 * SHOP, ARENA, MISSIONS, NETWORK, ROGUE ARENA, SYSTEM. GARAGE holds
 * ASSEMBLY, OPTIONAL PARTS, PERFORMANCE, CHANGE COLOR and AC TEST; SYSTEM holds SAVE, LOAD and SETTINGS (CONTROLS,
 * VIDEO, AUDIO, and UPDATES in the Android app). Entries for modes not built yet are shown dimmed. In a
 * Rogue Arena run the garage is assembly only: parts come from the prize
 * screen, so no shop and no AC TEST. */
enum {
    M_GARAGE, M_SHOP, M_ARENA, M_MISSIONS, M_NETWORK, M_ROGUE, M_SYSTEM,
    M_ASSEMBLY, M_OPTIONS, M_PERFORMANCE, M_COLOR, M_TEST, M_SAVE, M_LOAD, M_NEWGAME, M_SETTINGS, M_CONTROLS, M_VIDEO, M_AUDIO, M_UPDATES, M_REIMPORT, M_QUIT, M_BACK, M_EXIT_RUN,
    /* the hangar menu's own entries */
    M_EMBLEM, M_SPLIT, M_LAN, M_ONLINE, M_AR_PP, M_AR_MOA, M_AR_MOA_SUB, M_AR_EX, M_MAIL,
    M_COUNT
};
static const char *const k_menu[M_COUNT] = {
    "GARAGE", "PARTS SHOP", "ARENA", "MISSION SELECT", "NETWORK", "ROGUE ARENA", "SYSTEM",
    "ASSEMBLE", "OPTIONAL PARTS", "PERFORMANCE", "PAINT", "AC TEST", "SAVE", "LOAD", "NEW GAME", "SETTINGS", "CONTROLS", "DISPLAY", "SOUND", "UPDATES", "RE-IMPORT DISC", "EXIT GAME", "BACK",
    "BACK TO THE ARENA",
    "EMBLEM", "SPLIT SCREEN", "LAN", "ONLINE", "PHANTASMA ARENA", "MoA ARENA", "MoA SUB-ARENA", "EX ARENA", "MAIL"
};
/* The hangar menu's details box: two lines for each entry. */
static const char *const k_menu_info[M_COUNT][2] = {
    [M_SHOP] = { "BUY AND SELL PARTS.", "" },
    [M_ROGUE] = { "ROGUE ARENA: A RUN OF FIGHTS,", "PARTS FROM EACH WIN." },
    [M_ASSEMBLY] = { "SWAP FRAME PARTS AND WEAPONS.", "A FITS THE PART, Y SHOWS SPECS." },
    [M_OPTIONS] = { "FIT OPTIONAL PARTS", "TO THE CORE'S SLOTS." },
    [M_PERFORMANCE] = { "THE AC'S PERFORMANCE CHART.", "" },
    [M_COLOR] = { "COLORS FOR EACH BODY PART.", "" },
    [M_TEST] = { "TRY THE AC IN A TEST FIGHT.", "" },
    [M_SAVE] = { "WRITE THE GARAGE NOW.", "(IT ALSO SAVES BY ITSELF.)" },
    [M_LOAD] = { "BACK TO THE SAVED GARAGE.", "" },
    [M_NEWGAME] = { "START OVER: FRESH GARAGE, MISSIONS", "AND ARENA RANKS. PRESS A TWICE." },
    [M_CONTROLS] = { "CONTROLLER AND KEY LAYOUTS.", "" },
    [M_VIDEO] = { "RESOLUTION AND FILTERS.", "" },
    [M_AUDIO] = { "MUSIC, FIGHT AND MENU VOLUME.", "" },
    [M_UPDATES] = { "LOOK FOR A NEWER VERSION.", "" },
    [M_REIMPORT] = { "IMPORT A DISC AGAIN.", "SAVES ARE KEPT." },
    [M_QUIT] = { "CLOSE THE GAME.", "" },
    [M_EMBLEM] = { "DESIGN YOUR MARK.", "COMING SOON." },
    [M_SPLIT] = { "TWO PLAYERS ON ONE SCREEN.", "OPENS THE NETWORK SCREEN." },
    [M_LAN] = { "FIGHT ON THE SAME NETWORK.", "OPENS THE NETWORK SCREEN." },
    [M_ONLINE] = { "NETPLAY OVER THE INTERNET.", "OPENS THE NETWORK SCREEN." },
    [M_AR_PP] = { "PROJECT PHANTASMA'S", "RANKING LADDER." },
    [M_AR_MOA] = { "MASTER OF ARENA'S", "MAIN RANKS." },
    [M_AR_MOA_SUB] = { "MASTER OF ARENA'S", "LOWER-TIER RANKED BOUTS." },
    [M_AR_EX] = { "THE EX ARENAS:", "UNLOCKED CHALLENGERS." },
    [M_MISSIONS] = { "CONTRACTS FROM RAVEN NET.", "" },
    [M_MAIL] = { "MISSION MAIL AND BRIEFINGS.", "(OPENS MISSION SELECT.)" },
};
/* The hangar menu's columns, left to right; GARAGE is in focus at the start. */
enum { X_SYSTEM, X_DATA, X_VERSUS, X_ARENA, X_GARAGE, X_MISSION, X_COLS };
static const char *const k_col_name[X_COLS] = { "SYSTEM", "DATA", "VERSUS", "ARENA", "GARAGE", "MISSION" };
#define X_ROWS_MAX 8
static const int k_col_items[X_COLS][X_ROWS_MAX + 1] = {   /* -1 ends a column */
#ifdef SDL_PLATFORM_ANDROID
    { M_CONTROLS, M_AUDIO, M_VIDEO, M_UPDATES, M_REIMPORT, M_QUIT, -1 },
#else
    { M_CONTROLS, M_AUDIO, M_VIDEO, M_REIMPORT, M_QUIT, -1 },
#endif
    { M_SAVE, M_LOAD, M_NEWGAME, -1 },
    { M_SPLIT, M_LAN, M_ONLINE, -1 },
    { M_AR_PP, M_AR_MOA, M_AR_MOA_SUB, M_AR_EX, M_ROGUE, -1 },
    { M_ASSEMBLY, M_COLOR, M_EMBLEM, M_SHOP, M_OPTIONS, M_PERFORMANCE, M_TEST, -1 },
    { M_MISSIONS, M_MAIL, -1 },
};
static int col_items(int col) {
    int n = 0;
    while (n < X_ROWS_MAX && k_col_items[col][n] >= 0) ++n;
    return n;
}
/* Not built yet: shown, not chosen. */
static int menu_ready(int id) {
    (void)id;
    return 1;
}
static const int k_page_main[] = { M_GARAGE, M_SHOP, M_ARENA, M_MISSIONS, M_NETWORK, M_ROGUE, M_SYSTEM };
static const int k_page_garage[] = { M_ASSEMBLY, M_OPTIONS, M_PERFORMANCE, M_COLOR, M_TEST, M_BACK };
static const int k_page_system[] = { M_SAVE, M_LOAD, M_NEWGAME, M_SETTINGS, M_QUIT, M_BACK };
/* RE-IMPORT DISC: import a disc again over the old import (saves kept).
 * UPDATES only in the Android app (update.h); the PC gets new builds from GitHub. */
#ifdef SDL_PLATFORM_ANDROID
static const int k_page_settings[] = { M_CONTROLS, M_VIDEO, M_AUDIO, M_UPDATES, M_REIMPORT, M_BACK };
#else
static const int k_page_settings[] = { M_CONTROLS, M_VIDEO, M_AUDIO, M_REIMPORT, M_BACK };
#endif
static const int k_page_rogue[] = { M_ASSEMBLY, M_EXIT_RUN };
static const char *const k_page_title[GARAGE_PAGES] = { "MAIN MENU", "GARAGE", "SYSTEM", "SETTINGS" };
static const int k_page_parent[GARAGE_PAGES] = { GARAGE_PAGE_MAIN, GARAGE_PAGE_MAIN, GARAGE_PAGE_MAIN, GARAGE_PAGE_SYSTEM };
static int menu_items(const garage_ui *ui, const int **ids) {
    if (ui->rogue) { *ids = k_page_rogue; return 2; }
    switch (ui->page) {
    case GARAGE_PAGE_GARAGE: *ids = k_page_garage; return 6;
    case GARAGE_PAGE_SYSTEM: *ids = k_page_system; return (int)SDL_arraysize(k_page_system);
    case GARAGE_PAGE_SETTINGS: *ids = k_page_settings; return (int)SDL_arraysize(k_page_settings);
    default: *ids = k_page_main; return 7;
    }
}
static int *menu_cursor(garage_ui *ui) {
    return ui->rogue ? &ui->menu : &ui->cursor[ui->page];
}
static void open_page(garage_ui *ui, int page) {
    ui->page = page;
    ui->cursor[page] = 0;
}

/* Assembly rows in Master of Arena's order; -1 is the optional parts row. */
static const int k_rows[] = {
    ACR_GEN1_SLOT_HEAD, ACR_GEN1_SLOT_CORE, ACR_GEN1_SLOT_ARMS, ACR_GEN1_SLOT_LEGS,
    ACR_GEN1_SLOT_GENERATOR, ACR_GEN1_SLOT_FCS, -1, ACR_GEN1_SLOT_BOOSTER,
    ACR_GEN1_SLOT_BACK_L, ACR_GEN1_SLOT_BACK_R, ACR_GEN1_SLOT_BLADE, ACR_GEN1_SLOT_HAND
};
#define ROWS 12
static const char *const k_row_name[ROWS] = {
    "HEAD", "CORE", "ARMS", "LEGS", "GENERATOR", "FCS", "OPTIONAL", "BOOSTER",
    "BACK WEAPON L", "BACK WEAPON R", "ARM WEAPON L", "ARM WEAPON R"
};
static const char *const k_cat_name[ACR_GEN1_CAT_COUNT] = {
    "CORE", "BOOSTER", "HEAD", "ARMS", "ARM WEAPON L", "ARM WEAPON R", "BACK WEAPON",
    "GENERATOR", "FCS", "LEGS", "OPTIONAL"
};
/* Shop tabs in the assembly order. */
static const int k_shop_cats[ACR_GEN1_CAT_COUNT] = {
    ACR_GEN1_CAT_HEAD, ACR_GEN1_CAT_CORE, ACR_GEN1_CAT_ARMS, ACR_GEN1_CAT_LEGS,
    ACR_GEN1_CAT_GENERATOR, ACR_GEN1_CAT_FCS, ACR_GEN1_CAT_OPTION, ACR_GEN1_CAT_BOOSTER,
    ACR_GEN1_CAT_BACK, ACR_GEN1_CAT_BLADE, ACR_GEN1_CAT_HAND
};

static const char *result_text(acr_gen1_result r) {
    switch (r) {
    case ACR_GEN1_OK: return "";
    case ACR_GEN1_NOT_IN_SHOP: return "THE SHOP DOES NOT SELL THIS PART.";
    case ACR_GEN1_NO_CREDITS: return "NOT ENOUGH CREDITS.";
    case ACR_GEN1_IN_DEBT: return "NO TRADING WHILE IN DEBT.";
    case ACR_GEN1_STOCK_FULL: return "YOU CAN'T HOLD MORE OF THIS PART.";
    case ACR_GEN1_NONE_SPARE: return "NO SPARE COPY: UNEQUIP IT FIRST.";
    case ACR_GEN1_ALREADY_FITTED: return "ALREADY EQUIPPED.";
    case ACR_GEN1_REQUIRED: return "THIS SLOT CAN'T BE LEFT EMPTY.";
    case ACR_GEN1_NOT_ALLOWED: return "CAN'T BE EQUIPPED WITH THE CURRENT PARTS.";
    case ACR_GEN1_NO_OPTION_SLOTS: return "NOT ENOUGH OPTION SLOTS IN THE CORE.";
    }
    return "";
}

static const char *slot_short(int s) {
    static const char *const n[ACR_GEN1_SLOT_COUNT] = {
        "CORE", "BOOSTER", "HEAD", "ARMS", "ARM WEAPON L", "ARM WEAPON R", "BACK WEAPON L",
        "BACK WEAPON R", "GENERATOR", "FCS", "LEGS" };
    return n[s];
}

static void say(garage_ui *ui, const char *text) {
    SDL_strlcpy(ui->message, text, sizeof ui->message);
    ui->message_ms = 2500;
}

void garage_init(garage_ui *ui) {
    memset(ui, 0, sizeof *ui);
    ui->xmb_col = X_GARAGE;
    ui->xmb_pos = (float)X_GARAGE;
}

/* The hangar menu is showing (not a Rogue Arena run's garage). */
static int hangar_menu(const garage_ui *ui) { return ui->screen == 0 && !ui->rogue; }

void garage_say(garage_ui *ui, const char *text) {
    say(ui, text);
}

/* ---- lists ---- */

#define LIST_MAX (ACR_GEN1_PART_MAX + 1)
#define LIST_ROWS 18

/* Assembly: what can go in the current row (NO_PART first for optional slots). */
static int assembly_list(const garage_ui *ui, const garage_data *d, int out[LIST_MAX]) {
    const int s = k_rows[ui->slot];
    int n = 0;
    const acr_gen1_category cat = s < 0 ? ACR_GEN1_CAT_OPTION : acr_gen1_slot_category((acr_gen1_slot)s);
    if (s >= 0 && (s == ACR_GEN1_SLOT_BOOSTER || s == ACR_GEN1_SLOT_BLADE || s == ACR_GEN1_SLOT_HAND ||
                   s == ACR_GEN1_SLOT_BACK_L || s == ACR_GEN1_SLOT_BACK_R))
        out[n++] = ACR_GEN1_NO_PART;
    for (int i = 0; i < ACR_GEN1_PART_MAX; ++i)
        if (d->hangar->owned[cat][i] && d->cat->part[cat][i].present) out[n++] = i;
    return n;
}

static int shop_list(const garage_ui *ui, const garage_data *d, int out[LIST_MAX]) {
    const int cat = k_shop_cats[ui->shop_cat];
    int n = 0;
    for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) {
        const acr_gen1_catalog_part *p = &d->cat->part[cat][i];
        if (!p->present) continue;
        if (ui->shop_sell ? d->hangar->owned[cat][i] > 0
                          : acr_gen1_shop_offers(d->hangar, d->cat, (acr_gen1_category)cat, (unsigned)i)) out[n++] = i;
    }
    return n;
}

static void move_cursor(int *cur, int *top, int n, int delta) {
    if (n <= 0) { *cur = *top = 0; return; }
    *cur = (*cur + delta + n) % n;
    if (*cur < *top) *top = *cur;
    if (*cur >= *top + LIST_ROWS) *top = *cur - LIST_ROWS + 1;
}

/* ASSEMBLE: start the slot's part list on the part fitted there (the
 * first optional part on the OPTIONAL row). */
static void assembly_open_slot(garage_ui *ui, const garage_data *d) {
    int list[LIST_MAX];
    const int n = assembly_list(ui, d, list);
    const int s = k_rows[ui->slot];
    ui->list = ui->list_top = 0;
    for (int i = 0; s >= 0 && i < n; ++i)
        if (list[i] == (int)d->hangar->fitted[s]) ui->list = i;
}

static void assembly_open(garage_ui *ui, const garage_data *d) {
    ui->screen = 1;
    ui->list_open = 0;
    assembly_open_slot(ui, d);
}

/* OPTIONAL PARTS: the options owned, in option-bit order (as the game lists them). */
#define OPT_ROWS 5
static int option_list(const garage_data *d, int out[ACR_GEN1_OPTION_MAX]) {
    int n = 0;
    for (int b = 0; b < ACR_GEN1_OPTION_MAX; ++b)
        if (d->hangar->owned[ACR_GEN1_CAT_OPTION][b] && d->cat->part[ACR_GEN1_CAT_OPTION][b].present) out[n++] = b;
    return n;
}

/* Fits or takes off option `bit`, with the garage's messages. */
static garage_command toggle_option(garage_ui *ui, garage_data *d, int bit) {
    if (bit == (int)ACR_GEN1_OPTION_MODERN_BIT && !d->have_pad && !((d->hangar->options >> bit) & 1u)) {
        say(ui, "MODERN CONTROLS NEED A TWIN-STICK CONTROLLER.");
        return GC_NONE;
    }
    const uint16_t before = d->hangar->options;
    const acr_gen1_result r = acr_gen1_garage_option(d->hangar, d->cat, (unsigned)bit, !((before >> bit) & 1u));
    if (r != ACR_GEN1_OK) {
        if (r == ACR_GEN1_NO_OPTION_SLOTS && (before & ACR_GEN1_OPTION_MODERN)) say(ui, "SP-MODERN USES EVERY OPTION SLOT.");
        else say(ui, result_text(r));
        return GC_NONE;
    }
    if (before & (uint16_t)~d->hangar->options & (uint16_t)~ACR_GEN1_OPTION_MODERN)
        say(ui, "OTHER OPTIONAL PARTS UNEQUIPPED.");
    return GC_CHANGED;
}

/* CHANGE COLOR (AC1Recomp menus-screens.md §2l). Its menu: CHANGE PATTERN
 * (the 15 ready-made patterns: not built yet) and EDIT COLOR. The part
 * menu: GENERAL, HEAD, CORE, ARMS, LEGS, DEFAULT. The editor: up / down
 * pick R, G or B; left / right -1 / +1 (LB / RB: -2 / +2, the game's
 * L1 / R1 held); triangle / Tab the next colour; A keeps; B puts the part
 * back as it was (GENERAL: all four). */
enum { PM_PATTERN, PM_EDIT, PM_COUNT };
static const char *const k_paint_menu[PM_COUNT] = { "CHANGE PATTERN", "EDIT COLOR" };
#define PAINT_PARTS 6   /* GENERAL, HEAD, CORE, ARMS, LEGS, DEFAULT */
static const char *const k_paint_part[PAINT_PARTS] = { "GENERAL", "HEAD", "CORE", "ARMS", "LEGS", "DEFAULT" };
static const int k_paint_part_id[PAINT_PARTS - 1] = {
    ACR_GEN1_PAINT_GENERAL, ACR_GEN1_PAINT_HEAD, ACR_GEN1_PAINT_CORE, ACR_GEN1_PAINT_ARMS, ACR_GEN1_PAINT_LEGS };
static const char *const k_colour_name[4] = { "BASE COLOR", "OPTIONAL COLOR", "DETAIL COLOR", "JOINT COLOR" };

static garage_command paint_input(garage_ui *ui, garage_data *d, garage_action a) {
    uint8_t (*paint)[4][3] = d->hangar->paint;
    if (ui->paint_step == 0) {
        if (a == GA_UP || a == GA_DOWN) ui->paint_menu = (ui->paint_menu + 1) % PM_COUNT;
        if (a == GA_CANCEL) ui->screen = 0;
        if (a != GA_OK) return GC_NONE;
        if (ui->paint_menu == PM_PATTERN) { say(ui, "NOT AVAILABLE YET."); return GC_NONE; }
        ui->paint_step = 1;
        ui->paint_part_cur = 0;
        return GC_NONE;
    }
    if (ui->paint_step == 1) {
        if (a == GA_UP) ui->paint_part_cur = (ui->paint_part_cur + PAINT_PARTS - 1) % PAINT_PARTS;
        if (a == GA_DOWN) ui->paint_part_cur = (ui->paint_part_cur + 1) % PAINT_PARTS;
        if (a == GA_CANCEL) ui->paint_step = 0;
        if (a != GA_OK) return GC_NONE;
        if (ui->paint_part_cur == PAINT_PARTS - 1) {   /* DEFAULT: at once, no question */
            if (!d->paint_default) return GC_NONE;
            acr_gen1_paint_fill(paint, d->paint_default);
            say(ui, "DEFAULT COLORS.");
            return GC_CHANGED;
        }
        memcpy(ui->paint_backup, paint, sizeof ui->paint_backup);
        ui->paint_step = 2;
        ui->paint_colour = ACR_GEN1_COLOUR_BASE;
        ui->paint_channel = 0;
        return GC_NONE;
    }
    const int part = k_paint_part_id[ui->paint_part_cur];
    if (a == GA_UP) ui->paint_channel = (ui->paint_channel + 2) % 3;
    if (a == GA_DOWN) ui->paint_channel = (ui->paint_channel + 1) % 3;
    if (a == GA_INFO) ui->paint_colour = (ui->paint_colour + 1) % 4;
    int step = 0;
    if (a == GA_LEFT) step = -1;
    if (a == GA_RIGHT) step = 1;
    if (a == GA_PREV_TAB) step = -2;
    if (a == GA_NEXT_TAB) step = 2;
    if (step) {
        const int v = acr_gen1_paint_get((const uint8_t (*)[4][3])paint, part, ui->paint_colour, ui->paint_channel);
        acr_gen1_paint_set(paint, part, ui->paint_colour, ui->paint_channel, v + step);
    }
    if (a == GA_OK) { ui->paint_step = 1; return GC_CHANGED; }   /* keep */
    if (a == GA_CANCEL) {                                        /* put the part back */
        if (part == ACR_GEN1_PAINT_GENERAL) memcpy(paint, ui->paint_backup, sizeof ui->paint_backup);
        else memcpy(paint[part], ui->paint_backup[part], sizeof paint[part]);
        ui->paint_step = 1;
    }
    return GC_NONE;
}

/* ---- input ---- */

/* NEW GAME asks for a second A on it (any other press cancels). */
static int g_new_game_armed;

/* Opens a menu entry. *err: it could not be opened (a message says why). */
static garage_command choose(garage_ui *ui, garage_data *d, int id, int *err) {
    *err = 0;
    if (id != M_NEWGAME) g_new_game_armed = 0;
    if (!menu_ready(id)) { say(ui, "NOT AVAILABLE YET."); *err = 1; return GC_NONE; }
    switch (id) {
        case M_GARAGE: open_page(ui, GARAGE_PAGE_GARAGE); return GC_NONE;
        case M_SYSTEM: open_page(ui, GARAGE_PAGE_SYSTEM); return GC_NONE;
        case M_SETTINGS: open_page(ui, GARAGE_PAGE_SETTINGS); return GC_NONE;
        case M_BACK: ui->page = k_page_parent[ui->page]; return GC_NONE;
        case M_ASSEMBLY: assembly_open(ui, d); return GC_NONE;
        case M_PERFORMANCE: {
            /* The game opens the page only on a complete AC (menu 0x80092BB4's caller). */
            acr_gen1_perf_build b;
            acr_gen1_garage_perf_build(d->hangar, d->cat, 0, &b);
            if (acr_gen1_perf_complete(&b)) ui->screen = 3;
            else { say(ui, "YOUR EQUIPMENT IS INCOMPLETE."); *err = 1; }
            return GC_NONE;
        }
        case M_OPTIONS: {
            int l[ACR_GEN1_OPTION_MAX];
            if (!option_list(d, l)) { say(ui, "NO OPTIONAL PARTS AVAILABLE."); *err = 1; return GC_NONE; }
            ui->screen = 4;
            ui->opt = ui->opt_top = 0;
            return GC_NONE;
        }
        case M_COLOR:
            /* The game needs a head, arms and legs to open it (§2l). */
            if (d->hangar->fitted[ACR_GEN1_SLOT_HEAD] == ACR_GEN1_NO_PART || d->hangar->fitted[ACR_GEN1_SLOT_ARMS] == ACR_GEN1_NO_PART ||
                d->hangar->fitted[ACR_GEN1_SLOT_LEGS] == ACR_GEN1_NO_PART) { say(ui, "YOUR EQUIPMENT IS INCOMPLETE."); *err = 1; return GC_NONE; }
            ui->screen = 5;
            ui->paint_step = 0;
            ui->paint_menu = 1;
            return GC_NONE;
        case M_SHOP: ui->screen = 2; ui->confirm = 0; ui->shop = ui->shop_top = 0; return GC_NONE;
        case M_ROGUE: return GC_ROGUE;
        case M_ARENA: ui->arena_row = 0; return GC_ARENA;
        /* the arena list's rows: Phantasma, MoA SUB-ARENA, MoA, then the Ex Arenas (arena.c) */
        case M_AR_PP: ui->arena_row = 0; return GC_ARENA;
        case M_AR_MOA_SUB: ui->arena_row = 1; return GC_ARENA;
        case M_AR_MOA: ui->arena_row = 2; return GC_ARENA;
        case M_AR_EX: ui->arena_row = 3; return GC_ARENA;
        case M_MISSIONS: case M_MAIL: return GC_MISSIONS;
        case M_EMBLEM: say(ui, "EMBLEM: COMING SOON."); *err = 1; return GC_NONE;
        case M_CONTROLS: return GC_CONTROLS;
        case M_VIDEO: return GC_VIDEO;
        case M_AUDIO: return GC_AUDIO;
        case M_UPDATES: return GC_UPDATES;
        case M_REIMPORT: return GC_REIMPORT;
        case M_SAVE: return GC_SAVE;
        case M_NEWGAME:
            if (!g_new_game_armed) { g_new_game_armed = 1; say(ui, "A AGAIN: NEW GAME. YOUR SAVE AND ARENA RANKS ARE REPLACED."); return GC_NONE; }
            g_new_game_armed = 0;
            return GC_NEWGAME;
        case M_LOAD: return GC_LOAD;
        case M_QUIT: case M_EXIT_RUN: return GC_QUIT;
        default: break;
        }
    if (id == M_TEST || id == M_NETWORK || id == M_SPLIT || id == M_LAN || id == M_ONLINE) {
        acr_gen1_fit_numbers n;
        const unsigned fail = acr_gen1_garage_check(d->hangar, d->cat, &n);
        if (!fail) return id == M_TEST ? GC_TEST : GC_NETWORK;
        say(ui, fail & ACR_GEN1_FAIL_MISSING_PART ? "AC EQUIPMENT INCOMPLETE." :
                fail & ACR_GEN1_FAIL_NO_BOOSTER ? "THESE LEGS NEED A BOOSTER." :
                fail & ACR_GEN1_FAIL_ENERGY ? "ENERGY OUTPUT TOO LOW." :
                fail & ACR_GEN1_FAIL_ARM_LOAD ? "ARM LOAD OVER THE CORE'S LIMIT." :
                fail & ACR_GEN1_FAIL_WEIGHT ? "TOO HEAVY FOR THESE LEGS." :
                "TOO MANY OPTIONAL PARTS FOR THIS CORE.");
        *err = 1;
    }
    return GC_NONE;
}

/* The hangar menu: Left / Right move the columns under the focus spot,
 * Up / Down the items under it; neither wraps. */
static garage_command xmb_input(garage_ui *ui, garage_data *d, garage_action a) {
    const int col = ui->xmb_col, row = ui->xmb_row;
    const int n = col_items(col);
    if (a == GA_LEFT && col > 0) { --ui->xmb_col; ui->xmb_row = 0; ui->xmb_rpos = 0; }
    if (a == GA_RIGHT && col + 1 < X_COLS) { ++ui->xmb_col; ui->xmb_row = 0; ui->xmb_rpos = 0; }
    if (a == GA_UP && row > 0) --ui->xmb_row;
    if (a == GA_DOWN && row + 1 < n) ++ui->xmb_row;
    if (ui->xmb_col != col || ui->xmb_row != row) {
        ui->xmb_hold_ms = 0;   /* the details wait for a new 1.5 s hold */
        g_new_game_armed = 0;
        sfx_ui(SFX_UI_CURSOR);
        return GC_NONE;
    }
    if (a != GA_OK) { g_new_game_armed = 0; return GC_NONE; }
    int err = 0;
    const garage_command c = choose(ui, d, k_col_items[col][row], &err);
    sfx_ui(err ? SFX_UI_BACK : SFX_UI_OK);
    ui->xmb_hold_ms = 0;
    return c;
}

garage_command garage_input(garage_ui *ui, garage_data *d, garage_action a) {
    int list[LIST_MAX];
    if (a == GA_NONE) return GC_NONE;
    if (hangar_menu(ui)) return xmb_input(ui, d, a);
    if (ui->screen == 0) {   /* a Rogue Arena run's garage menu */
        const int *ids;
        const int items = menu_items(ui, &ids);
        int *cur = menu_cursor(ui);
        if (*cur >= items) *cur = 0;
        if (a == GA_UP) *cur = (*cur + items - 1) % items;
        if (a == GA_DOWN) *cur = (*cur + 1) % items;
        if (a == GA_CANCEL) { *cur = items - 1; g_new_game_armed = 0; return GC_NONE; }
        if (a != GA_OK) { g_new_game_armed = 0; return GC_NONE; }
        const int id = ids[*cur];
        int err = 0;
        switch (id) {
        case M_GARAGE: open_page(ui, GARAGE_PAGE_GARAGE); return GC_NONE;
        case M_SYSTEM: open_page(ui, GARAGE_PAGE_SYSTEM); return GC_NONE;
        case M_SETTINGS: open_page(ui, GARAGE_PAGE_SETTINGS); return GC_NONE;
        case M_BACK: ui->page = k_page_parent[ui->page]; return GC_NONE;
        default: return choose(ui, d, id, &err);
        }
    }

    if (ui->screen == 5) return paint_input(ui, d, a);

    if (ui->screen == 4) {   /* OPTIONAL PARTS */
        int l[ACR_GEN1_OPTION_MAX];
        const int n = option_list(d, l);
        if (a == GA_CANCEL || n == 0) { ui->screen = 0; return GC_NONE; }
        if (a == GA_UP && ui->opt > 0) --ui->opt;          /* no wrap, as the game's list */
        if (a == GA_DOWN && ui->opt + 1 < n) ++ui->opt;
        if (ui->opt >= n) ui->opt = n - 1;
        if (ui->opt < ui->opt_top) ui->opt_top = ui->opt;
        if (ui->opt >= ui->opt_top + OPT_ROWS) ui->opt_top = ui->opt - OPT_ROWS + 1;
        if (a == GA_OK) return toggle_option(ui, d, l[ui->opt]);
        return GC_NONE;
    }

    if (ui->screen == 3) {   /* PERFORMANCE: a display page, closed by either button */
        if (a == GA_OK || a == GA_CANCEL) ui->screen = 0;
        return GC_NONE;
    }

    if (ui->screen == 1) {   /* ASSEMBLE */
        const int n = assembly_list(ui, d, list);
        if (a == GA_UP || a == GA_DOWN) {
            ui->slot = (ui->slot + (a == GA_UP ? ROWS - 1 : 1)) % ROWS;
            assembly_open_slot(ui, d);
            sfx_ui(SFX_UI_CURSOR);
            return GC_NONE;
        }
        if ((a == GA_LEFT || a == GA_RIGHT) && n > 1) {   /* look through the parts you own: not fitted yet */
            ui->list = (ui->list + (a == GA_LEFT ? n - 1 : 1)) % n;
            sfx_ui(SFX_UI_CURSOR);
            return GC_NONE;
        }
        if (a == GA_INFO) { ui->specs_open = !ui->specs_open; sfx_ui(ui->specs_open ? SFX_UI_OK : SFX_UI_BACK); return GC_NONE; }
        if (a == GA_CANCEL) { ui->screen = 0; ui->specs_open = 0; sfx_ui(SFX_UI_BACK); return GC_NONE; }
        if (a != GA_OK || n == 0) return GC_NONE;
        if (ui->list >= n) ui->list = n - 1;
        /* A fits the part (the user, 2026-11-27: looking at it is not enough) */
        const int s = k_rows[ui->slot], index = list[ui->list];
        if (s < 0) {
            const garage_command c = toggle_option(ui, d, index);
            sfx_ui(c == GC_CHANGED ? SFX_UI_EQUIP : SFX_UI_BACK);
            return c;
        }
        if (index == (int)d->hangar->fitted[s]) { say(ui, "ALREADY EQUIPPED."); sfx_ui(SFX_UI_BACK); return GC_NONE; }
        uint32_t removed = 0;
        const acr_gen1_result r = acr_gen1_garage_fit(d->hangar, d->cat, (acr_gen1_slot)s, (unsigned)index, &removed);
        if (r != ACR_GEN1_OK) { say(ui, result_text(r)); sfx_ui(SFX_UI_BACK); return GC_NONE; }
        if (removed) {
            char text[96] = "UNEQUIPPED:";
            for (int k = 0; k < ACR_GEN1_SLOT_COUNT; ++k)
                if (removed & (1u << k)) { SDL_strlcat(text, " ", sizeof text); SDL_strlcat(text, slot_short(k), sizeof text); }
            say(ui, text);
        } else say(ui, "EQUIPPED.");
        sfx_ui(SFX_UI_EQUIP);
        return GC_CHANGED;
    }

    /* shop */
    const int n = shop_list(ui, d, list);
    if (ui->confirm) {
        if (a == GA_CANCEL) ui->confirm = 0;
        if (a != GA_OK || n == 0) return GC_NONE;
        ui->confirm = 0;
        const acr_gen1_category cat = (acr_gen1_category)k_shop_cats[ui->shop_cat];
        const acr_gen1_result r = ui->shop_sell ? acr_gen1_shop_sell(d->hangar, d->cat, cat, (unsigned)list[ui->shop])
                                                : acr_gen1_shop_buy(d->hangar, d->cat, cat, (unsigned)list[ui->shop]);
        if (r != ACR_GEN1_OK) { say(ui, result_text(r)); return GC_NONE; }
        say(ui, ui->shop_sell ? "SOLD." : "BOUGHT.");
        if (ui->shop_sell) move_cursor(&ui->shop, &ui->shop_top, shop_list(ui, d, list), 0);
        return GC_CHANGED;
    }
    if (a == GA_UP) move_cursor(&ui->shop, &ui->shop_top, n, -1);
    if (a == GA_DOWN) move_cursor(&ui->shop, &ui->shop_top, n, 1);
    if (a == GA_LEFT || a == GA_RIGHT) {
        ui->shop_cat = (ui->shop_cat + (a == GA_LEFT ? ACR_GEN1_CAT_COUNT - 1 : 1)) % ACR_GEN1_CAT_COUNT;
        ui->shop = ui->shop_top = 0;
    }
    if (a == GA_PREV_TAB || a == GA_NEXT_TAB) { ui->shop_sell = !ui->shop_sell; ui->shop = ui->shop_top = 0; }
    if (a == GA_CANCEL) ui->screen = 0;
    if (a == GA_OK && n > 0) ui->confirm = 1;
    return GC_NONE;
}

/* ---- drawing ---- */

#define PANEL_W 250.0f

void garage_view_rect(float hud_w, float *x, float *y, float *w, float *h) {
    *x = PANEL_W + 24;
    *y = 34;
    *w = hud_w - 2 * (PANEL_W + 24);
    *h = 400;
}

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define BAD 255, 90, 70
#define GOOD 120, 220, 150

static void panel(float x, float y, float w, float h) {
    acr_hud_rect(x, y, w, h, 10, 14, 20, 200, 1);
    acr_hud_rect(x, y, w, h, 70, 90, 100, 255, 0);
}

/* Text wrapped to `cols` characters a line at spaces; returns the y after it. */
static float wrapped(float x, float y, const char *text, int cols, uint8_t r, uint8_t g, uint8_t b) {
    char line[96];
    while (*text) {
        while (*text == ' ') ++text;
        int n = (int)SDL_strlen(text);
        if (n > cols) {
            n = cols;
            while (n > 0 && text[n] != ' ') --n;
            if (n == 0) n = cols;
        }
        if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
        memcpy(line, text, (size_t)n);
        line[n] = 0;
        if (n) { acr_hud_text(x, y, line, r, g, b); y += 12; }
        text += n;
    }
    return y;
}

/* The disc's shop text for a part (MIS.T), or an option's description. */
static void part_text(const garage_data *d, int cat, int index, float x, float y) {
    if (!d->specs || index < 0 || index >= ACR_GEN1_PART_MAX || !d->specs->present[cat][index]) return;
    char all[3 * 76 + 4] = "";
    for (int k = 0; k < d->specs->text_n[cat][index]; ++k) {
        if (k) SDL_strlcat(all, " ", sizeof all);
        SDL_strlcat(all, d->specs->text[cat][index][k], sizeof all);
    }
    wrapped(x, y, all, 28, DIM);
}

static const char *part_name(const garage_data *d, int cat, int index) {
    if (index == ACR_GEN1_NO_PART) return "NO EQUIP";
    const char *n = d->names ? d->names[cat][index] : "";
    return n[0] ? n : "?";
}

/* Price, weight and drain of a part, in the right-hand column. */
static void part_info(const garage_data *d, int cat, int index, float x, float y) {
    if (index == ACR_GEN1_NO_PART || index < 0) return;
    const acr_gen1_catalog_part *p = &d->cat->part[cat][index];
    char line[64];
    acr_hud_text(x, y, part_name(d, cat, index), HILITE);
    SDL_snprintf(line, sizeof line, "PRICE     %8u", (unsigned)p->price);
    acr_hud_text(x, y + 12, line, WHITE);
    if (cat == ACR_GEN1_CAT_OPTION) {
        if (index == (int)ACR_GEN1_OPTION_MODERN_BIT) {
            acr_hud_text(x, y + 24, "SLOTS          ALL", WHITE);
            acr_hud_text(x, y + 36, "MODERN CONTROLS:", DIM);
            acr_hud_text(x, y + 48, "TWIN STICKS, HARD LOCK", DIM);
            acr_hud_text(x, y + 60, "WEAKER FCS AND BLADE", DIM);
            return;
        }
        SDL_snprintf(line, sizeof line, "SLOTS     %8u", p->slots);
        acr_hud_text(x, y + 24, line, WHITE);
        part_text(d, cat, index, x, y + 44);
        return;
    }
    SDL_snprintf(line, sizeof line, "WEIGHT    %8u", p->stats.weight);
    acr_hud_text(x, y + 24, line, WHITE);
    SDL_snprintf(line, sizeof line, cat == ACR_GEN1_CAT_GENERATOR ? "OUTPUT    %8u" : "EN. DRAIN %8u",
                 p->stats.energy_drain);
    acr_hud_text(x, y + 36, line, WHITE);
    SDL_snprintf(line, sizeof line, "OWNED     %5u/%2u", d->hangar->owned[cat][index],
                 acr_gen1_fitted_copies(d->hangar, (acr_gen1_category)cat, (unsigned)index));
    acr_hud_text(x, y + 48, line, DIM);
    part_text(d, cat, index, x, y + 68);
}

/* The build's totals and what keeps it from a test. */
static void totals(const garage_data *d, float x, float y) {
    acr_gen1_fit_numbers n;
    const unsigned fail = acr_gen1_garage_check(d->hangar, d->cat, &n);
    acr_gen1_fit_build b;
    acr_gen1_garage_build(d->hangar, d->cat, &b);
    const acr_gen1_build *p = &b.parts;
    const long ap = (p->head.present ? p->head.stat_2c : 0) + (p->core.present ? p->core.stat_2c : 0) +
                    (p->arms.present ? p->arms.stat_2c : 0) + (p->legs.present ? p->legs.stat_2c : 0);
    char line[64];
    acr_hud_text(x, y, "AC STATUS", HILITE);
    SDL_snprintf(line, sizeof line, "AP        %8ld", ap);
    acr_hud_text(x, y + 14, line, WHITE);
    SDL_snprintf(line, sizeof line, "WEIGHT   %5u/%5u", (unsigned)n.carried_weight, p->legs.carry_limit);
    acr_hud_text(x, y + 26, line, fail & ACR_GEN1_FAIL_WEIGHT ? 255 : 220, fail & ACR_GEN1_FAIL_WEIGHT ? 90 : 230,
                 fail & ACR_GEN1_FAIL_WEIGHT ? 70 : 235);
    SDL_snprintf(line, sizeof line, "ARM LOAD %5u/%5u", (unsigned)n.arm_load, p->core.carry_limit);
    if (fail & ACR_GEN1_FAIL_ARM_LOAD) acr_hud_text(x, y + 38, line, BAD); else acr_hud_text(x, y + 38, line, WHITE);
    SDL_snprintf(line, sizeof line, "EN. SPARE %8ld", (long)n.energy_surplus);
    if (fail & ACR_GEN1_FAIL_ENERGY) acr_hud_text(x, y + 50, line, BAD); else acr_hud_text(x, y + 50, line, WHITE);
    SDL_snprintf(line, sizeof line, "OPTIONS     %2u/%2u", (unsigned)n.option_slots_used, b.core_option_slots);
    if (fail & ACR_GEN1_FAIL_OPTION_SLOTS) acr_hud_text(x, y + 62, line, BAD); else acr_hud_text(x, y + 62, line, WHITE);
    if (!fail) acr_hud_text(x, y + 80, "READY FOR SORTIE", GOOD);
    else {
        float ly = y + 80;
        if (fail & ACR_GEN1_FAIL_MISSING_PART) { acr_hud_text(x, ly, "PARTS MISSING", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_NO_BOOSTER) { acr_hud_text(x, ly, "NO BOOSTER", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_ENERGY) { acr_hud_text(x, ly, "ENERGY SHORT", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_ARM_LOAD) { acr_hud_text(x, ly, "ARMS OVERLOADED", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_WEIGHT) { acr_hud_text(x, ly, "OVERWEIGHT", BAD); ly += 12; }
        if (fail & ACR_GEN1_FAIL_OPTION_SLOTS) acr_hud_text(x, ly, "OPTION SLOTS FULL", BAD);
    }
}

/* OPTIONAL PARTS (Master of Arena's garage page, AC1Recomp menus-screens.md
 * §2j): the owned options, 5 rows at a time (fitted ones marked), the
 * highlighted one's type, the slots it needs and the core has free, its
 * description; its model turns in the middle (garage_option_model). */
static void options_page(const garage_ui *ui, const garage_data *d, float lx, float rx) {
    int l[ACR_GEN1_OPTION_MAX];
    const int n = option_list(d, l);
    char line[64];
    acr_hud_text(lx + 10, 46, "OPTIONAL PARTS", HILITE);
    for (int i = ui->opt_top; i < n && i < ui->opt_top + OPT_ROWS; ++i) {
        const float y = 72 + (float)(i - ui->opt_top) * 24;
        const int fitted = (d->hangar->options >> l[i]) & 1u, sel = i == ui->opt;
        if (sel) acr_hud_rect(lx + 4, y - 5, PANEL_W - 8, 18, 60, 70, 40, 255, 1);
        SDL_snprintf(line, sizeof line, "%c %s", fitted ? '*' : ' ', part_name(d, ACR_GEN1_CAT_OPTION, l[i]));
        if (sel) acr_hud_text(lx + 14, y, line, HILITE);
        else if (fitted) acr_hud_text(lx + 14, y, line, GOOD);
        else acr_hud_text(lx + 14, y, line, WHITE);
    }
    if (ui->opt_top > 0) acr_hud_text(lx + PANEL_W - 20, 72, "^", DIM);
    if (ui->opt_top + OPT_ROWS < n) acr_hud_text(lx + PANEL_W - 20, 72 + (OPT_ROWS - 1) * 24, "v", DIM);
    if (n == 0) return;
    const int bit = l[ui->opt];
    const acr_gen1_catalog_part *p = &d->cat->part[ACR_GEN1_CAT_OPTION][bit];
    /* slots: the core's, less every fitted option's */
    const acr_gen1_catalog_part *core = d->hangar->fitted[ACR_GEN1_SLOT_CORE] < ACR_GEN1_PART_MAX
        ? &d->cat->part[ACR_GEN1_CAT_CORE][d->hangar->fitted[ACR_GEN1_SLOT_CORE]] : NULL;
    int used = 0;
    for (int b = 0; b < ACR_GEN1_OPTION_MAX; ++b)
        if (((d->hangar->options >> b) & 1u) && d->cat->part[ACR_GEN1_CAT_OPTION][b].present)
            used += b == (int)ACR_GEN1_OPTION_MODERN_BIT ? (core ? core->slots : 0) : d->cat->part[ACR_GEN1_CAT_OPTION][b].slots;
    const int free_slots = (core ? core->slots : 0) - used;
    float y = 72 + OPT_ROWS * 24 + 10;
    acr_hud_rect(lx + 8, y - 6, PANEL_W - 16, 1, 70, 90, 100, 255, 1);
    const char *type = d->specs && d->specs->type[ACR_GEN1_CAT_OPTION][bit][0] ? d->specs->type[ACR_GEN1_CAT_OPTION][bit]
                     : bit == (int)ACR_GEN1_OPTION_MODERN_BIT ? "MODERN CONTROLS" : "";
    acr_hud_text(lx + 10, y + 4, type, WHITE);
    if (bit == (int)ACR_GEN1_OPTION_MODERN_BIT) SDL_strlcpy(line, "ALL SLOTS REQUIRED", sizeof line);
    else SDL_snprintf(line, sizeof line, "%d SLOT%s REQUIRED", p->slots, p->slots == 1 ? "" : "S");
    acr_hud_text(lx + 10, y + 20, line, WHITE);
    SDL_snprintf(line, sizeof line, "%d SLOT%s AVAILABLE", free_slots, free_slots == 1 ? "" : "S");
    if (free_slots < 0) acr_hud_text(lx + 10, y + 32, line, BAD); else acr_hud_text(lx + 10, y + 32, line, WHITE);
    y += 56;
    if (bit == (int)ACR_GEN1_OPTION_MODERN_BIT)
        wrapped(lx + 10, y, "TWIN STICKS, HARD LOCK. WEAKER FCS AND BLADE.", 28, DIM);
    else part_text(d, ACR_GEN1_CAT_OPTION, bit, lx + 10, y);
    (void)rx;
}

int garage_option_model(const garage_ui *ui, const garage_data *d, float hud_w,
                        float *x, float *y, float *w, float *h) {
    if (ui->screen != 4) return -1;
    int l[ACR_GEN1_OPTION_MAX];
    const int n = option_list(d, l);
    if (n == 0 || ui->opt >= n) return -1;
    garage_view_rect(hud_w, x, y, w, h);
    return l[ui->opt];
}

/* A paint value (0..64) as the middle shade of the game's ramp (0..31 runs
 * from v down to 1, 32..64 from 31 down to v - 30: either way the middle is
 * about (v + 1) / 2), so the swatch looks like most of the painted part. */
static uint8_t shade8(uint8_t v) { const int m = (v + 1) / 2; return (uint8_t)((m > 31 ? 31 : m) * 255 / 31); }

static void paint_page(const garage_ui *ui, const garage_data *d, float lx) {
    const uint8_t (*paint)[4][3] = (const uint8_t (*)[4][3])d->hangar->paint;
    char line[64];
    acr_hud_text(lx + 10, 46, "CHANGE COLOR", HILITE);
    if (ui->paint_step == 0) {
        for (int i = 0; i < PM_COUNT; ++i) {
            const float y = 72 + i * 22.0f;
            const int sel = i == ui->paint_menu;
            if (sel) acr_hud_rect(lx + 4, y - 3, PANEL_W - 8, 14, 60, 70, 40, 255, 1);
            if (i == PM_PATTERN) acr_hud_text(lx + 14, y, k_paint_menu[i], DIM);
            else if (sel) acr_hud_text(lx + 14, y, k_paint_menu[i], HILITE);
            else acr_hud_text(lx + 14, y, k_paint_menu[i], WHITE);
        }
        return;
    }
    if (ui->paint_step == 1) {
        acr_hud_text(lx + 10, 62, "EDIT COLOR", WHITE);
        for (int i = 0; i < PAINT_PARTS; ++i) {
            const float y = 86 + i * 22.0f;
            const int sel = i == ui->paint_part_cur;
            if (sel) acr_hud_rect(lx + 4, y - 3, PANEL_W - 8, 14, 60, 70, 40, 255, 1);
            if (sel) acr_hud_text(lx + 14, y, k_paint_part[i], HILITE); else acr_hud_text(lx + 14, y, k_paint_part[i], WHITE);
            if (i < PAINT_PARTS - 1) {   /* the part's four colours */
                const int part = k_paint_part_id[i] == ACR_GEN1_PAINT_GENERAL ? ACR_GEN1_PAINT_CORE : k_paint_part_id[i];
                for (int c = 0; c < 4; ++c)
                    acr_hud_rect(lx + 130 + c * 26.0f, y - 2, 20, 11, shade8(paint[part][c][0]), shade8(paint[part][c][1]),
                                 shade8(paint[part][c][2]), 255, 1);
            }
        }
        return;
    }
    const int part = k_paint_part_id[ui->paint_part_cur];
    const int shown = part == ACR_GEN1_PAINT_GENERAL ? ACR_GEN1_PAINT_CORE : part;
    acr_hud_text(lx + 10, 66, k_paint_part[ui->paint_part_cur], WHITE);
    acr_hud_text(lx + 10, 82, k_colour_name[ui->paint_colour], HILITE);
    for (int c = 0; c < 4; ++c) {   /* the four colours, the one being edited framed */
        const float x = lx + 10 + c * 58.0f;
        acr_hud_rect(x, 100, 50, 24, shade8(paint[shown][c][0]), shade8(paint[shown][c][1]), shade8(paint[shown][c][2]), 255, 1);
        if (c == ui->paint_colour) acr_hud_rect(x - 2, 98, 54, 28, 255, 220, 120, 255, 0);
    }
    static const char *const k_rgb[3] = { "R", "G", "B" };
    static const uint8_t k_bar[3][3] = { { 220, 70, 60 }, { 80, 200, 90 }, { 80, 120, 230 } };
    for (int k = 0; k < 3; ++k) {
        const float y = 146 + k * 24.0f;
        const int sel = k == ui->paint_channel;
        const int v = paint[shown][ui->paint_colour][k];
        if (sel) acr_hud_rect(lx + 4, y - 5, PANEL_W - 8, 18, 60, 70, 40, 255, 1);
        if (sel) acr_hud_text(lx + 12, y, k_rgb[k], HILITE); else acr_hud_text(lx + 12, y, k_rgb[k], WHITE);
        acr_hud_rect(lx + 30, y, 2.5f * ACR_GEN1_PAINT_MAX, 8, 30, 36, 42, 255, 1);
        acr_hud_rect(lx + 30, y, 2.5f * (float)v, 8, k_bar[k][0], k_bar[k][1], k_bar[k][2], 255, 1);
        SDL_snprintf(line, sizeof line, "%2d", v);
        if (sel) acr_hud_text(lx + 200, y, line, HILITE); else acr_hud_text(lx + 200, y, line, WHITE);
    }
    wrapped(lx + 10, 230, "LEFT/RIGHT: -1/+1  LB/RB: -2/+2  Y/TAB: NEXT COLOR  A: KEEP  B: CANCEL", 28, DIM);
}

/* PERFORMANCE (Master of Arena's garage page, AC1Recomp menus-screens.md
 * §2k): the AC's name, the rated rows with sub-items indented, OVERALL and
 * the GRADE on the left; AP, WEIGHT and PRICE on the right. */
static void performance_page(const garage_data *d, float lx, float rx) {
    acr_gen1_perf_build b;
    acr_gen1_perf o;
    acr_gen1_garage_perf_build(d->hangar, d->cat, 0, &b);
    acr_gen1_performance(&b, &o);
    const struct { const char *label; int sub; int32_t value; } rows[] = {
        { "OFFENSIVE POINT", 0, o.offensive }, { "DEFENSIVE POINT", 0, o.defensive },
        { "STABILITY", 0, o.stability }, { "MOBILITY", 0, o.mobility },
        { "MOVING SPEED", 1, o.moving_speed }, { "TURNING SPEED", 1, o.turning_speed },
        { "BOOST ABILITY", 1, o.boost_ability }, { "SUPPORT SYSTEM", 0, o.support },
        { "RADAR RANGE", 1, o.radar_range }, { "FCS PERFORMANCE", 1, o.fcs_performance },
    };
    char line[64];
    acr_hud_text(lx + 10, 46, d->hangar->ac_name[0] ? d->hangar->ac_name : "AC", HILITE);
    float y = 72;
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; ++i, y += 20) {
        acr_hud_text(lx + (rows[i].sub ? 24 : 10), y, rows[i].label, rows[i].sub ? 150 : 220,
                     rows[i].sub ? 165 : 230, rows[i].sub ? 175 : 235);
        SDL_snprintf(line, sizeof line, "%6ld", (long)rows[i].value);
        acr_hud_text(lx + 186, y, line, WHITE);
    }
    acr_hud_rect(lx + 8, y - 2, PANEL_W - 16, 1, 70, 90, 100, 255, 1);
    y += 8;
    acr_hud_text(lx + 10, y, "OVERALL", 220, 230, 235);
    SDL_snprintf(line, sizeof line, "%6ld", (long)o.overall);
    acr_hud_text(lx + 186, y, line, HILITE);
    y += 34;
    acr_hud_text(lx + 10, y, "GRADE", 220, 230, 235);
    static const uint8_t k_grade_rgb[5][3] = {
        { 255, 90, 70 }, { 220, 230, 235 }, { 120, 220, 150 }, { 120, 200, 255 }, { 255, 220, 120 } };
    const uint8_t *g = k_grade_rgb[o.grade];
    acr_hud_text(lx + 100, y, acr_gen1_grade_name(o.grade), g[0], g[1], g[2]);

    acr_hud_text(rx + 10, 46, "AC STATUS", HILITE);
    SDL_snprintf(line, sizeof line, "AP        %8ld", (long)o.ap);
    acr_hud_text(rx + 10, 72, line, WHITE);
    SDL_snprintf(line, sizeof line, "WEIGHT    %8ld", (long)o.weight);
    acr_hud_text(rx + 10, 92, line, WHITE);
    SDL_snprintf(line, sizeof line, "PRICE     %8ld", (long)o.price);
    acr_hud_text(rx + 10, 112, line, WHITE);
    acr_hud_text(rx + 10, 150, "GRADE BY OVERALL", DIM);
    static const char *const k_scale[] = {
        "25000+  SUPERFINE", "20000+  GREAT", "15000+  FINE", "10000+  OK", "BELOW   POOR" };
    for (int i = 0; i < 5; ++i) {
        const int lit = 4 - i == (int)o.grade;
        if (lit) acr_hud_text(rx + 18, 168 + i * 16, k_scale[i], HILITE);
        else acr_hud_text(rx + 18, 168 + i * 16, k_scale[i], DIM);
    }
}

/* ---- the hangar menu (mockup A) ---- */

/* Eases `pos` towards `to`: about 95% of the way in 0.25 s (the mockup's
 * 0.28 s ease-out slide). */
static void ease(float *pos, float to, int elapsed_ms) {
    float k = (float)elapsed_ms / 80.0f;
    if (k > 1.0f) k = 1.0f;
    *pos += (to - *pos) * k;
    if (*pos - to < 0.002f && to - *pos < 0.002f) *pos = to;
}

#define CRT_GREEN 0xc8, 0xff, 0xe6
static void xmb_draw(garage_ui *ui, const garage_data *d, float hud_w, int elapsed_ms) {
    const unsigned ms = (unsigned)ui->anim_ms;
    hangar_fx(hud_w, ms);
    float cx, cy, cw, ch;
    hangar_console(hud_w, d->hangar->ac_name, (long)d->hangar->credits, &cx, &cy, &cw, &ch);
    hangar_crt_back(cx, cy, cw, ch);
    ease(&ui->xmb_pos, (float)ui->xmb_col, elapsed_ms);
    ease(&ui->xmb_rpos, (float)ui->xmb_row, elapsed_ms);
    ui->xmb_hold_ms += elapsed_ms;
    const float k = 480.0f / 720.0f;
    const float fx = cx + 330 * k;             /* the focus spot: about a third in */
    const float icon_y = cy + 58 * k, sub_y = cy + 136 * k;
    /* the focus column's faint glow */
    const uint8_t g0[4] = { 120, 255, 190, 0 }, g1[4] = { 120, 255, 190, 15 };
    {
        const float gx = fx - 10 * k, gw = 112 * k;
        acr_vtx q[12];
        const acr_vtx a0 = { gx, cy, 0, g0[0], g0[1], g0[2], g0[3], 0, 0 }, a1 = { gx + gw * 0.5f, cy, 0, g1[0], g1[1], g1[2], g1[3], 0, 0 };
        const acr_vtx a2 = { gx + gw, cy, 0, g0[0], g0[1], g0[2], g0[3], 0, 0 };
        acr_vtx b0 = a0, b1 = a1, b2 = a2;
        b0.y = b1.y = b2.y = cy + ch;
        q[0] = a0; q[1] = a1; q[2] = b1; q[3] = a0; q[4] = b1; q[5] = b0;
        q[6] = a1; q[7] = a2; q[8] = b2; q[9] = a1; q[10] = b2; q[11] = b1;
        acr_render_tris(q, 12, 0);
    }
    /* the columns: 104 px apart, sliding so the chosen one sits on the spot,
     * running off both edges of the screen (clipped to the CRT) */
    for (int i = 0; i < X_COLS; ++i) {
        const float x = fx + ((float)i - ui->xmb_pos) * 104 * k;
        const float w = 92 * k;
        if (x < cx || x + w > cx + cw) continue;   /* the CRT's edges cut them off */
        const int on = i == ui->xmb_col;
        const uint8_t r = on ? 0xc8 : 0x3f, g = on ? 0xff : 0x6f, b = on ? 0xe6 : 0x5e, a = on ? 255 : 191;
        const float isz = 34 * k;
        if (on) hs_puff(x + w * 0.5f, icon_y + 4 + isz * 0.5f, isz * 0.9f, 120, 255, 190, 40);
        hangar_icon(i, x + (w - isz) * 0.5f, icon_y + 4, isz, r, g, b, a);
        const float ts = 8.5f, tw = (float)SDL_strlen(k_col_name[i]) * ts;
        if (x + w * 0.5f - tw * 0.5f >= cx && x + w * 0.5f + tw * 0.5f <= cx + cw)
            acr_hud_text_size(x + w * 0.5f - tw * 0.5f, icon_y + 8 + isz, k_col_name[i], ts, r, g, b, a);
    }
    /* the items under the focus spot: the chosen one just below the icon,
     * the ones already passed above it, dimmed */
    const int col = ui->xmb_col, n = col_items(col);
    for (int j = 0; j < n; ++j) {
        const float kk = (float)j - ui->xmb_rpos;
        const float y = kk >= 0 ? sub_y + kk * 34 * k : icon_y - 6 * k + kk * 28 * k;
        if (y < cy + 2 || y > cy + ch - 12) continue;
        const int on = j == ui->xmb_row;
        const int id = k_col_items[col][j];
        const float ts = on ? 12.5f : 10.0f;
        const uint8_t a = kk < -0.5f ? 90 : 255;
        const uint8_t r = on ? 0xc8 : 0x5f, g = on ? 0xff : 0x9d, b = on ? 0xe6 : 0x84;
        hangar_icon(6 + j, fx + 10 * k, y, 22 * k, r, g, b, a);
        acr_hud_text_size(fx + 10 * k + 22 * k + 8, y + (22 * k - ts) * 0.5f, k_menu[id], ts, r, g, b, a);
    }
    /* the details box: after a 1.5 s hold, fading in over 0.3 s */
    if (ui->xmb_hold_ms > 1500 && n) {
        float f = (float)(ui->xmb_hold_ms - 1500) / 300.0f;
        if (f > 1.0f) f = 1.0f;
        const int id = k_col_items[col][ui->xmb_row];
        /* right of the item (long names push it along) */
        float bx = fx + 220 * k;
        const float label_end = fx + 10 * k + 22 * k + 8 + (float)SDL_strlen(k_menu[id]) * 12.5f + 12;
        if (label_end > bx) bx = label_end;
        const float by = sub_y - 2 * k, bw = cx + cw - 8 - bx, bh = 54 * k;
        if (bw > 120) {
            acr_hud_rect(bx, by, bw, bh, 10, 30, 24, (uint8_t)(178 * f), 1);
            acr_hud_line(bx, by, bx, by + bh, 0x2f, 0x6b, 0x54, (uint8_t)(255 * f));
            acr_hud_text_size(bx + 10, by + 8, k_menu[id], 8, 0x7f, 0xc4, 0xa6, (uint8_t)(255 * f));
            /* fitted to the box: up to 7 HUD units a letter */
            for (int l = 0; l < 2; ++l) {
                const char *t = k_menu_info[id][l];
                if (!t || !t[0]) continue;
                float ts = (bw - 20) / (float)SDL_strlen(t);
                if (ts > 7) ts = 7;
                acr_hud_text_size(bx + 10, by + 21 + l * 10, t, ts, 0x7f, 0xc4, 0xa6, (uint8_t)(255 * f));
            }
        }
    }
    if (ui->message_ms > 0) {
        ui->message_ms -= elapsed_ms;
        acr_hud_text_size(cx + 10, cy + ch - 16, ui->message, 8, 0xff, 0xd2, 0x7a, 255);
    }
    hangar_crt_front(cx, cy, cw, ch, ms);
}

/* ---- GARAGE > ASSEMBLE (mockup B) ---- */

#define AS_TOP 56.0f
#define AS_BOTTOM 429.0f
#define AS_LEFT_W 200.0f
#define AS_RIGHT_W 220.0f
#define AS_GAP 13.0f
static void as_cols(float hud_w, float *lx, float *mx, float *mw, float *rx) {
    *lx = 16;
    *mx = 16 + AS_LEFT_W + AS_GAP;
    *rx = hud_w - 16 - AS_RIGHT_W;
    *mw = *rx - AS_GAP - *mx;
}

/* The slot row's part kind on the AC model, for the wireframe. Inner parts
 * light up where they sit: generator and optional parts in the core, the
 * FCS in the head, the blade on the arms. */
unsigned garage_wire_kinds(const garage_ui *ui) {
    if (ui->screen != 1) return 0;
    switch (k_rows[ui->slot]) {
    case ACR_GEN1_SLOT_HEAD: case ACR_GEN1_SLOT_FCS: return 1u << ACR_PART_HEAD;
    case ACR_GEN1_SLOT_CORE: case ACR_GEN1_SLOT_GENERATOR: case -1: return 1u << ACR_PART_CORE;
    case ACR_GEN1_SLOT_ARMS: case ACR_GEN1_SLOT_BLADE: return 1u << ACR_PART_ARMS;
    case ACR_GEN1_SLOT_LEGS: return 1u << ACR_PART_LEGS;
    case ACR_GEN1_SLOT_BOOSTER: return 1u << ACR_PART_BOOSTER;
    case ACR_GEN1_SLOT_BACK_L: return 1u << ACR_PART_BACK_LEFT;
    case ACR_GEN1_SLOT_BACK_R: return 1u << ACR_PART_BACK_RIGHT;
    case ACR_GEN1_SLOT_HAND: return 1u << ACR_PART_HAND;
    default: return 0;
    }
}

static int row_cat(int row) {
    const int s = k_rows[row];
    return s < 0 ? ACR_GEN1_CAT_OPTION : (int)acr_gen1_slot_category((acr_gen1_slot)s);
}

static const char *const k_cat_folder[ACR_GEN1_CAT_COUNT] = {
    "core", "booster", "head", "arms", "blade", "hand", "back", "generator", "fcs", "legs", "option"
};

static void cand_box(float hud_w, float *x, float *y, float *w, float *h) {
    float lx, mx, mw, rx;
    as_cols(hud_w, &lx, &mx, &mw, &rx);
    *x = rx + 10;
    *y = AS_TOP + 48;
    *w = AS_RIGHT_W - 20;
    *h = 200;
}

int garage_candidate_model(const garage_ui *ui, const garage_data *d, float hud_w, const char **kind, int *index,
                           float *x, float *y, float *w, float *h) {
    if (ui->screen != 1) return 0;
    int list[LIST_MAX];
    const int n = assembly_list(ui, d, list);
    if (n == 0 || ui->list >= n || list[ui->list] == ACR_GEN1_NO_PART || list[ui->list] < 0) return 0;
    *kind = k_cat_folder[row_cat(ui->slot)];
    *index = list[ui->list];
    cand_box(hud_w, x, y, w, h);
    return 1;
}

int garage_view_rect_ui(const garage_ui *ui, float hud_w, float *x, float *y, float *w, float *h) {
    if (hangar_menu(ui)) { hangar_ac_rect(hud_w, x, y, w, h); return 1; }
    if (ui->screen == 1) {
        float lx, rx;
        as_cols(hud_w, &lx, x, w, &rx);
        *y = AS_TOP + 18;
        *h = AS_BOTTOM - AS_TOP - 20;
        return 0;
    }
    garage_view_rect(hud_w, x, y, w, h);
    return 0;
}

void garage_draw_back(const garage_ui *ui, const garage_data *d, float hud_w) {
    (void)d;
    if (hangar_menu(ui)) { hangar_back(hud_w); return; }
    if (ui->screen != 1) return;
    float lx, mx, mw, rx;
    as_cols(hud_w, &lx, &mx, &mw, &rx);
    acr_hud_rect(0, 0, hud_w, 480, 0x1e, 0x1f, 0x23, 255, 1);
    /* the monitor's tinted glass */
    const uint8_t t0[4] = { 160, 175, 190, 36 }, t1[4] = { 40, 50, 60, 128 };
    hs_quad(0, 0, hud_w, 480, t0, t1);
    acr_hud_rect(lx, AS_TOP, AS_LEFT_W, AS_BOTTOM - AS_TOP, 170, 185, 195, 51, 1);
    acr_hud_rect(mx, AS_TOP, mw, AS_BOTTOM - AS_TOP, 200, 215, 225, 20, 1);
    acr_hud_rect(rx, AS_TOP, AS_RIGHT_W, AS_BOTTOM - AS_TOP, 14, 18, 50, 204, 1);
}

/* PART SPECS rows: label, which way is better (1 more, -1 less, 0 neither), the number. */
static const struct { const char *label; int better; int key; } k_spec_rows[] = {
    { "WEIGHT", -1, DS_WEIGHT }, { "ENERGY DRAIN", -1, DS_DRAIN }, { "OUTPUT", 1, DS_OUTPUT }, { "AP", 1, DS_AP },
    { "SHELL DEFENSE", 1, DS_DEF_S }, { "ENERGY DEFENSE", 1, DS_DEF_E }, { "RADAR RANGE", 1, DS_RADAR_R },
    { "ARM LOAD", 1, DS_ARM_LOAD }, { "OPTION SLOTS", 1, DS_SLOTS }, { "LOAD", 1, DS_LOAD }, { "SPEED", 1, DS_SPEED },
    { "TURNING", 1, DS_TURN }, { "STABILITY", 1, DS_STAB }, { "BOOST POWER", 1, DS_POWER }, { "BOOST DRAIN", -1, DS_BOOST_D },
    { "CAPACITY", 1, DS_CAP }, { "RED ZONE", -1, DS_RED }, { "LOCK TARGETS", 1, DS_LOCKS }, { "ATTACK", 1, DS_ATTACK },
    { "AMMO", 1, DS_AMMO }, { "RANGE", 1, DS_RANGE }, { "RELOAD TIME", -1, DS_RELOAD }, { "ENERGY PER SHOT", -1, DS_EPS },
    { "MISSILE LOCKS", 1, DS_SALVO }, { "IMPACT", 1, DS_IMPACT },
};

/* The see-through specs window over the AC (Y / Triangle): the part being
 * looked at, against the one fitted (green better, red worse). Drawn every
 * frame from the cursor, so it follows the navigation. */
static void specs_window(const garage_ui *ui, const garage_data *d, float mx, float mw) {
    int list[LIST_MAX];
    const int n = assembly_list(ui, d, list);
    const int cat = row_cat(ui->slot), s = k_rows[ui->slot];
    const int index = n && ui->list < n ? list[ui->list] : ACR_GEN1_NO_PART;
    const int fitted = s >= 0 ? (int)d->hangar->fitted[s] : ACR_GEN1_NO_PART;
    const float w = 253, x = mx + (mw - w) * 0.5f;
    int rows = 0;
    const int has = index != ACR_GEN1_NO_PART && index >= 0 && d->specs && d->specs->present[cat][index];
    if (has)
        for (size_t r = 0; r < SDL_arraysize(k_spec_rows); ++r)
            if (d->specs->v[cat][index][k_spec_rows[r].key] != DEMO_SPEC_NONE) ++rows;
    if (rows > 14) rows = 14;
    const float h = 40 + (float)(rows + 1) * 15 + 14;
    const float y = AS_TOP + (AS_BOTTOM - AS_TOP - h) * 0.5f;
    acr_hud_rect(x, y, w, h, 10, 12, 34, 184, 1);
    acr_hud_rect(x, y, w, h, 0xff, 0x5a, 0x4e, 255, 0);
    acr_hud_rect(x + 1, y + 1, w - 2, h - 2, 0xff, 0x5a, 0x4e, 120, 0);
    char line[64];
    SDL_snprintf(line, sizeof line, "%s - SPECS", part_name(d, cat, index));
    acr_hud_text_size(x + 12, y + 10, line, 10, 0xff, 0x5a, 0x4e, 255);
    float ly = y + 32;
    if (index == ACR_GEN1_NO_PART || index < 0) {
        acr_hud_text(x + 12, ly, "NOTHING FITTED IN THIS SLOT.", 0x9a, 0xa4, 0xc0);
    } else {
        SDL_snprintf(line, sizeof line, "%u", (unsigned)d->cat->part[cat][index].price);
        acr_hud_text(x + 12, ly, "PRICE", 0x9a, 0xa4, 0xc0);
        acr_hud_text(x + w - 12 - (float)SDL_strlen(line) * 8, ly, line, 230, 235, 239);
        acr_hud_line(x + 10, ly + 11, x + w - 10, ly + 11, 0x2c, 0x33, 0x66, 255);
        ly += 15;
        int shown = 0;
        for (size_t r = 0; has && r < SDL_arraysize(k_spec_rows) && shown < 14; ++r) {
            const int32_t v = d->specs->v[cat][index][k_spec_rows[r].key];
            if (v == DEMO_SPEC_NONE) continue;
            ++shown;
            uint8_t cr = 230, cg = 235, cb = 239;
            if (fitted != ACR_GEN1_NO_PART && fitted >= 0 && fitted != index && d->specs->present[cat][fitted]) {
                const int32_t f = d->specs->v[cat][fitted][k_spec_rows[r].key];
                if (f != DEMO_SPEC_NONE && f != v && k_spec_rows[r].better) {
                    const int up = (v > f) == (k_spec_rows[r].better > 0);
                    if (up) { cr = 120; cg = 220; cb = 150; } else { cr = 255; cg = 110; cb = 90; }
                }
            }
            SDL_snprintf(line, sizeof line, "%ld", (long)v);
            acr_hud_text(x + 12, ly, k_spec_rows[r].label, 0x9a, 0xa4, 0xc0);
            acr_hud_text(x + w - 12 - (float)SDL_strlen(line) * 8, ly, line, cr, cg, cb);
            acr_hud_line(x + 10, ly + 11, x + w - 10, ly + 11, 0x2c, 0x33, 0x66, 255);
            ly += 15;
        }
    }
    acr_hud_text_size(x + 12, y + h - 14, "Y / TRIANGLE: CLOSE", 7, 0x9a, 0xa4, 0xc0, 255);
}

static void assemble_draw(garage_ui *ui, const garage_data *d, float hud_w, int elapsed_ms) {
    char line[96];
    float lx, mx, mw, rx;
    as_cols(hud_w, &lx, &mx, &mw, &rx);
    /* top bezel: GARAGE > ASSEMBLE, the AC and its COAM */
    const uint8_t bz0[4] = { 0x4a, 0x47, 0x44, 255 }, bz1[4] = { 0x2b, 0x2a, 0x29, 255 };
    hs_quad(0, 0, hud_w, 43, bz0, bz1);
    acr_hud_line(0, 43, hud_w, 43, 0x11, 0x11, 0x11, 255);
    acr_hud_line(0, 44, hud_w, 44, 0x11, 0x11, 0x11, 255);
    acr_hud_text_size(19, 15, ui->rogue ? "ROGUE GARAGE" : "GARAGE", 14, 0xf1, 0xe6, 0xc8, 255);
    const float gx = 19 + (ui->rogue ? 12 : 6) * 14 + 8;
    acr_hud_text_size(gx, 15, ">", 14, 0x8f, 0x8a, 0x80, 255);
    acr_hud_text_size(gx + 22, 15, "ASSEMBLE", 14, 0xd8, 0x32, 0x2a, 255);
    if (ui->rogue) SDL_snprintf(line, sizeof line, "AC: %s   BALANCE %ld P", d->hangar->ac_name, (long)d->hangar->credits);
    else SDL_snprintf(line, sizeof line, "AC: %s   COAM %ld", d->hangar->ac_name, (long)d->hangar->credits);
    acr_hud_text_size(hud_w - 19 - (float)SDL_strlen(line) * 7.5f, 18, line, 7.5f, 0xc9, 0xc2, 0xb2, 255);

    /* the slot list */
    acr_hud_rect(lx, AS_TOP, AS_LEFT_W, AS_BOTTOM - AS_TOP, 0x8d, 0x9a, 0xa3, 255, 0);
    acr_hud_text_size(lx + 9, AS_TOP + 8, "PART SLOTS", 7, 0xc9, 0xd0, 0xd6, 255);
    const float rh = (AS_BOTTOM - AS_TOP - 26) / ROWS;
    for (int r = 0; r < ROWS; ++r) {
        const int s = k_rows[r];
        const float y = AS_TOP + 22 + r * rh;
        const int sel = r == ui->slot;
        if (sel) {
            acr_hud_rect(lx + 4, y, AS_LEFT_W - 8, rh - 2, 0xd8, 0x32, 0x2a, 90, 1);
            acr_hud_rect(lx + 4, y, AS_LEFT_W - 8, rh - 2, 0xff, 0x5a, 0x4e, 255, 0);
        }
        acr_hud_text_size(lx + 10, y + 3, k_row_name[r], 6.5f, sel ? 0xff : 0xb0, sel ? 0xd0 : 0xb8, sel ? 0xc8 : 0xc0, 220);
        if (s >= 0) SDL_strlcpy(line, part_name(d, acr_gen1_slot_category((acr_gen1_slot)s), d->hangar->fitted[s]), sizeof line);
        else {
            int count = 0;
            for (int b = 0; b < ACR_GEN1_OPTION_MAX; ++b) count += (d->hangar->options >> b) & 1;
            SDL_snprintf(line, sizeof line, "%d FITTED", count);
        }
        acr_hud_text_size(lx + 18, y + 13, line, 8, sel ? 255 : 230, sel ? 240 : 235, sel ? 235 : 239, 255);
    }

    /* the AC preview (the 3D AC and its wireframe are main.c's) */
    acr_hud_rect(mx, AS_TOP, mw, AS_BOTTOM - AS_TOP, 0x8d, 0x9a, 0xa3, 255, 0);
    SDL_snprintf(line, sizeof line, "PREVIEW // %s", k_row_name[ui->slot]);
    acr_hud_text_size(mx + 8, AS_TOP + 7, line, 7, 0xe6, 0xeb, 0xef, 255);
    acr_hud_rect(mx + mw - 82, AS_TOP + 7, 6, 6, 0xff, 0x7a, 0x70, 255, 1);
    acr_hud_text_size(mx + mw - 72, AS_TOP + 7, "SELECTED", 7, 0xff, 0x7a, 0x70, 255);

    /* the part being looked at (its model turns in cand_box: main.c) */
    int list[LIST_MAX];
    const int n = assembly_list(ui, d, list);
    if (ui->list >= n) ui->list = n ? n - 1 : 0;
    const int cat = row_cat(ui->slot), s = k_rows[ui->slot];
    const int index = n ? list[ui->list] : ACR_GEN1_NO_PART;
    acr_hud_rect(rx, AS_TOP, AS_RIGHT_W, AS_BOTTOM - AS_TOP, 0x8d, 0x9a, 0xa3, 255, 0);
    acr_hud_text_size(rx + 11, AS_TOP + 11, n ? part_name(d, cat, index) : "NO PARTS OWNED", 13, 0xd8, 0x32, 0x2a, 255);
    acr_hud_line(rx + 11, AS_TOP + 28, rx + AS_RIGHT_W - 11, AS_TOP + 28, 0xd8, 0x32, 0x2a, 255);
    acr_hud_line(rx + 11, AS_TOP + 29, rx + AS_RIGHT_W - 11, AS_TOP + 29, 0xd8, 0x32, 0x2a, 255);
    acr_hud_text_size(rx + 11, AS_TOP + 35, k_row_name[ui->slot], 7, 0xc8, 0xd2, 0xff, 255);
    SDL_snprintf(line, sizeof line, "%d/%d", n ? ui->list + 1 : 0, n);
    acr_hud_text_size(rx + AS_RIGHT_W - 11 - (float)SDL_strlen(line) * 7, AS_TOP + 35, line, 7, 0xc8, 0xd2, 0xff, 255);
    if (n) {
        const int is_fitted = s < 0 ? (index >= 0 && index < ACR_GEN1_OPTION_MAX && ((d->hangar->options >> index) & 1))
                                    : index == (int)d->hangar->fitted[s];
        if (is_fitted) acr_hud_text_size(rx + 11, AS_TOP + 258, "* EQUIPPED", 8, 120, 220, 150, 255);
        else acr_hud_text_size(rx + 11, AS_TOP + 258, s < 0 ? "A: EQUIP" : "NOT FITTED - A: EQUIP", 8, 255, 210, 122, 255);
        if (index != ACR_GEN1_NO_PART && index >= 0 && cat != ACR_GEN1_CAT_OPTION) {
            const acr_gen1_catalog_part *p = &d->cat->part[cat][index];
            SDL_snprintf(line, sizeof line, "WEIGHT %u   PRICE %u", p->stats.weight, (unsigned)p->price);
            acr_hud_text_size(rx + 11, AS_TOP + 274, line, 7, 0xc8, 0xd2, 0xff, 255);
        }
    }
    acr_hud_text_size(rx + 11, AS_BOTTOM - 16, "< > CHANGE PART   Y SPECS", 7, 0xc8, 0xd2, 0xff, 255);

    if (ui->specs_open) specs_window(ui, d, mx, mw);

    /* bottom bezel: the buttons */
    hs_quad(0, 440, hud_w, 40, (const uint8_t[4]){ 0x3b, 0x39, 0x36, 255 }, (const uint8_t[4]){ 0x23, 0x22, 0x21, 255 });
    acr_hud_line(0, 440, hud_w, 440, 0x11, 0x11, 0x11, 255);
    acr_hud_line(0, 441, hud_w, 441, 0x11, 0x11, 0x11, 255);
    if (ui->message_ms > 0) {
        ui->message_ms -= elapsed_ms;
        acr_hud_text_size(hud_w * 0.5f - (float)SDL_strlen(ui->message) * 4, 446, ui->message, 8, 0xff, 0xd2, 0x7a, 255);
    }
    static const char k_hint[] = "UP/DOWN SLOT   LEFT/RIGHT PART   A EQUIP   Y SPECS   B BACK";
    acr_hud_text_size(hud_w * 0.5f - (float)(sizeof k_hint - 1) * 3.5f, 462, k_hint, 7, 0xc9, 0xc2, 0xb2, 255);
}

void garage_draw(garage_ui *ui, const garage_data *d, float hud_w, int elapsed_ms) {
    ui->anim_ms += elapsed_ms;
    if (hangar_menu(ui)) { xmb_draw(ui, d, hud_w, elapsed_ms); return; }
    if (ui->screen == 1) { assemble_draw(ui, d, hud_w, elapsed_ms); return; }
    char line[96];
    const float lx = 16, rx = hud_w - PANEL_W - 16;
    static const char *const k_title[] = { "GARAGE", "ASSEMBLY", "SHOP", "PERFORMANCE", "OPTIONAL PARTS", "CHANGE COLOR" };
    acr_hud_text(lx, 12, ui->screen == 0 && !ui->rogue ? k_page_title[ui->page] : k_title[ui->screen], HILITE);
    if (ui->rogue) SDL_snprintf(line, sizeof line, "BALANCE %6ld P", (long)d->hangar->credits);
    else SDL_snprintf(line, sizeof line, "CREDITS %10ld", (long)d->hangar->credits);
    if (d->hangar->credits < 0) acr_hud_text(rx + 60, 12, line, BAD); else acr_hud_text(rx + 60, 12, line, WHITE);

    panel(lx, 34, PANEL_W, 400);
    panel(rx, 34, PANEL_W, 400);
    int list[LIST_MAX];

    if (ui->screen == 0) {
        const int *ids;
        const int items = menu_items(ui, &ids);
        const int cur = *menu_cursor(ui);
        for (int i = 0; i < items; ++i) {
            const char *name = k_menu[ids[i]];
            const int ready = menu_ready(ids[i]);
            if (i == cur) {
                acr_hud_rect(lx + 4, 48 + i * 22 - 3, PANEL_W - 8, 14, 60, 70, 40, 255, 1);
                if (ready) acr_hud_text(lx + 12, 48 + i * 22, name, HILITE); else acr_hud_text(lx + 12, 48 + i * 22, name, DIM);
            } else if (ready) acr_hud_text(lx + 12, 48 + i * 22, name, WHITE);
            else acr_hud_text(lx + 12, 48 + i * 22, name, DIM);
        }
        totals(d, rx + 10, 46);
    } else if (ui->screen == 1) {
        for (int r = 0; r < ROWS; ++r) {
            const int s = k_rows[r];
            const float y = 46 + r * 26;
            const int sel = r == ui->slot;
            if (sel) acr_hud_rect(lx + 4, y - 3, PANEL_W - 8, 25, ui->list_open ? 40 : 60, ui->list_open ? 46 : 70, ui->list_open ? 30 : 40, 255, 1);
            acr_hud_text(lx + 10, y, k_row_name[r], DIM);
            if (s >= 0) {
                acr_hud_text(lx + 20, y + 11, part_name(d, acr_gen1_slot_category((acr_gen1_slot)s), d->hangar->fitted[s]),
                             sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
            } else {
                int count = 0;
                for (int b = 0; b < ACR_GEN1_OPTION_MAX; ++b) count += (d->hangar->options >> b) & 1;
                SDL_snprintf(line, sizeof line, "%d FITTED", count);
                acr_hud_text(lx + 20, y + 11, line, WHITE);
            }
        }
        if (ui->list_open) {
            /* the parts you own for this slot, over the AC view's left edge */
            const int s = k_rows[ui->slot];
            const int cat = s < 0 ? ACR_GEN1_CAT_OPTION : acr_gen1_slot_category((acr_gen1_slot)s);
            const int n = assembly_list(ui, d, list);
            const float px = lx + PANEL_W + 8;
            panel(px, 34, 230, 24 + LIST_ROWS * 12);
            acr_hud_text(px + 8, 40, k_row_name[ui->slot], HILITE);
            for (int i = ui->list_top; i < n && i < ui->list_top + LIST_ROWS; ++i) {
                const float y = 56 + (i - ui->list_top) * 12;
                const int fitted = s < 0 ? (list[i] < ACR_GEN1_OPTION_MAX && ((d->hangar->options >> list[i]) & 1))
                                         : list[i] == d->hangar->fitted[s];
                SDL_snprintf(line, sizeof line, "%c %s", fitted ? '*' : ' ', part_name(d, cat, list[i]));
                if (i == ui->list) acr_hud_rect(px + 4, y - 2, 222, 12, 60, 70, 40, 255, 1);
                acr_hud_text(px + 8, y, line, i == ui->list ? 255 : 220, i == ui->list ? 220 : 230, i == ui->list ? 120 : 235);
            }
            if (n == 0) acr_hud_text(px + 8, 56, "NO PARTS OWNED", DIM);
            totals(d, rx + 10, 46);
            if (n) part_info(d, cat, list[ui->list], rx + 10, 200);
        } else {
            totals(d, rx + 10, 46);
        }
    } else if (ui->screen == 3) {
        performance_page(d, lx, rx);
    } else if (ui->screen == 4) {
        options_page(ui, d, lx, rx);
        totals(d, rx + 10, 46);
    } else if (ui->screen == 5) {
        paint_page(ui, d, lx);
        totals(d, rx + 10, 46);
    } else {
        const int cat = k_shop_cats[ui->shop_cat];
        const int n = shop_list(ui, d, list);
        acr_hud_text(lx + 10, 42, ui->shop_sell ? "  BUY  [SELL]" : " [BUY]  SELL ", HILITE);
        SDL_snprintf(line, sizeof line, "< %s >", k_cat_name[cat]);
        acr_hud_text(lx + 10, 56, line, WHITE);
        for (int i = ui->shop_top; i < n && i < ui->shop_top + LIST_ROWS; ++i) {
            const float y = 76 + (i - ui->shop_top) * 18;
            const acr_gen1_catalog_part *p = &d->cat->part[cat][list[i]];
            if (i == ui->shop) acr_hud_rect(lx + 4, y - 3, PANEL_W - 8, 16, 60, 70, 40, 255, 1);
            SDL_snprintf(line, sizeof line, "%-13.13s %7u %2u", part_name(d, cat, list[i]), (unsigned)p->price,
                         d->hangar->owned[cat][list[i]]);
            acr_hud_text(lx + 8, y, line, i == ui->shop ? 255 : 220, i == ui->shop ? 220 : 230, i == ui->shop ? 120 : 235);
        }
        if (n == 0) acr_hud_text(lx + 10, 76, ui->shop_sell ? "NOTHING TO SELL" : "SHOP EMPTY: SELL TO STOCK IT", DIM);
        totals(d, rx + 10, 46);
        if (n) part_info(d, cat, list[ui->shop], rx + 10, 200);
        if (ui->confirm && n) {
            const acr_gen1_catalog_part *p = &d->cat->part[cat][list[ui->shop]];
            const uint32_t price = ui->shop_sell ? (uint32_t)((uint64_t)p->price * d->cat->sell_percent / 100) : p->price;
            SDL_snprintf(line, sizeof line, "%s %s FOR %u CREDITS?", ui->shop_sell ? "SELL" : "BUY",
                         part_name(d, cat, list[ui->shop]), (unsigned)price);
            const float w = (float)SDL_strlen(line) * 8 + 24;
            panel(hud_w / 2 - w / 2, 200, w, 44);
            acr_hud_text(hud_w / 2 - w / 2 + 12, 210, line, HILITE);
            acr_hud_text(hud_w / 2 - 60, 226, "A: YES   B: NO", WHITE);
        }
    }

    if (ui->message_ms > 0) {
        ui->message_ms -= elapsed_ms;
        acr_hud_text(lx, 442, ui->message, HILITE);
    }
    static const char *const k_hints[] = {
        "A: SELECT   B: BACK",
        "A: EQUIP   B: BACK   LEFT/RIGHT: PAGE",
        "A: BUY/SELL   B: BACK   LEFT/RIGHT: CATEGORY   LB/RB: BUY OR SELL",
        "A/B: BACK",
        "A: EQUIP / UNEQUIP   B: BACK",
        "A: SELECT   B: BACK" };
    const int hint = ui->screen;
    acr_hud_text(lx, 460, k_hints[hint], DIM);
}
