/* The game's menus, laid out like Master of Arena's garage: the main menu
 * (GARAGE, SHOP, ARENA, MISSIONS, NETWORK, ROGUE ARENA, SYSTEM) and its
 * sub-menus (GARAGE: ASSEMBLY, AC TEST; SYSTEM: SAVE, LOAD, SETTINGS,
 * EXIT GAME; SETTINGS: CONTROLS, VIDEO, AUDIO), the assembly screen (slots
 * in the game's order, then the parts you own for the chosen slot), and
 * the shop (buy and sell by category). The AC turns in the middle with
 * the build's totals beside it. Controller first; the keyboard mirrors it. */
#ifndef GARAGE_H
#define GARAGE_H

#include "demo_data.h"

typedef enum garage_action {
    GA_NONE, GA_UP, GA_DOWN, GA_LEFT, GA_RIGHT, GA_OK, GA_CANCEL, GA_PREV_TAB, GA_NEXT_TAB,
    GA_INFO         /* Triangle / Tab: a part's specs (Rogue Arena's PART SPECS) */
} garage_action;

typedef enum garage_command {
    GC_NONE,
    GC_CHANGED,     /* the build or the stock changed: reload the AC and save */
    GC_TEST,        /* AC TEST with the current build (it passed the check) */
    GC_NETWORK,     /* the NETWORK screen (host or join a match; the build passed the check) */
    GC_ROGUE,       /* ROGUE ARENA */
    GC_CONTROLS,    /* SYSTEM > SETTINGS > CONTROLS: the controller mapping page */
    GC_SAVE,        /* SYSTEM > SAVE */
    GC_LOAD,        /* SYSTEM > LOAD: back to the saved garage */
    GC_QUIT         /* SYSTEM > EXIT GAME (in a Rogue Arena run: back to its hub) */
} garage_command;

enum { GARAGE_PAGE_MAIN, GARAGE_PAGE_GARAGE, GARAGE_PAGE_SYSTEM, GARAGE_PAGE_SETTINGS, GARAGE_PAGES };

typedef struct garage_ui {
    int screen;                 /* 0 menu, 1 assembly, 2 shop */
    int page;                   /* which menu (GARAGE_PAGE_...) */
    int cursor[GARAGE_PAGES];   /* each menu's cursor */
    int menu;                   /* the Rogue Arena run's menu cursor */
    int slot;                   /* assembly: row of the slot list */
    int list_open, list, list_top;
    int shop_sell, shop_cat, shop, shop_top;
    int confirm;                /* shop: waiting for yes/no */
    char message[96];
    int message_ms;
    int rogue;                  /* a Rogue Arena run's garage: assembly only, P */
} garage_ui;

typedef struct garage_data {
    acr_gen1_hangar *hangar;
    const acr_gen1_catalog *cat;
    char (*names)[ACR_GEN1_PART_MAX][24];
} garage_data;

void garage_init(garage_ui *ui);
/* Shows a line of text under the menus for a moment (SAVED. and so on). */
void garage_say(garage_ui *ui, const char *text);
garage_command garage_input(garage_ui *ui, garage_data *d, garage_action a);

/* Draws the menus on the HUD grid (480 high, `hud_w` wide). The 3D AC is
 * drawn by the caller; garage_view_rect gives where, in HUD units. */
void garage_draw(garage_ui *ui, const garage_data *d, float hud_w, int elapsed_ms);
void garage_view_rect(float hud_w, float *x, float *y, float *w, float *h);

#endif
