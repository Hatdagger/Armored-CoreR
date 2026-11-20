/* The garage for the test build, laid out like Master of Arena's: a main
 * menu (ASSEMBLY, PARTS SHOP, AC TEST, EXIT), the assembly screen (slots in
 * the game's order, then the parts you own for the chosen slot), and the
 * parts shop (buy and sell by category). The AC turns in the middle with
 * the build's totals beside it. Controller first; the keyboard mirrors it. */
#ifndef GARAGE_H
#define GARAGE_H

#include "demo_data.h"

typedef enum garage_action {
    GA_NONE, GA_UP, GA_DOWN, GA_LEFT, GA_RIGHT, GA_OK, GA_CANCEL, GA_PREV_TAB, GA_NEXT_TAB
} garage_action;

typedef enum garage_command {
    GC_NONE,
    GC_CHANGED,     /* the build or the stock changed: reload the AC and save */
    GC_TEST,        /* AC TEST with the current build (it passed the check) */
    GC_QUIT
} garage_command;

typedef struct garage_ui {
    int screen;                 /* 0 menu, 1 assembly, 2 shop */
    int menu;                   /* main menu cursor */
    int slot;                   /* assembly: row of the slot list */
    int list_open, list, list_top;
    int shop_sell, shop_cat, shop, shop_top;
    int confirm;                /* shop: waiting for yes/no */
    char message[96];
    int message_ms;
} garage_ui;

typedef struct garage_data {
    acr_gen1_hangar *hangar;
    const acr_gen1_catalog *cat;
    char (*names)[ACR_GEN1_PART_MAX][24];
    int have_pad;          /* a controller is connected (SP-MODERN needs one) */
} garage_data;

void garage_init(garage_ui *ui);
garage_command garage_input(garage_ui *ui, garage_data *d, garage_action a);

/* Draws the menus on the HUD grid (480 high, `hud_w` wide). The 3D AC is
 * drawn by the caller; garage_view_rect gives where, in HUD units. */
void garage_draw(garage_ui *ui, const garage_data *d, float hud_w, int elapsed_ms);
void garage_view_rect(float hud_w, float *x, float *y, float *w, float *h);

#endif
