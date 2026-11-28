/* The game's menus. The main menu is the hangar (user's design, project doc
 * claude/hangar-menu-and-garage-ui.md): the player's AC on its gantry and
 * an XMB-style menu on the console's CRT, columns SYSTEM, DATA, MULTIPLAYER,
 * ARENA, GARAGE, MISSION with their items under the one in focus.
 * GARAGE > ASSEMBLE: the slot list, the AC with the slot's part as a
 * pulsing red wireframe, the part you are looking at turning on the right
 * (A fits it), and a specs window on Y / Triangle. The shop and the other
 * garage pages keep Master of Arena's layout. A Rogue Arena run's garage
 * keeps its own small menu. Controller first; the keyboard mirrors it. */
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
    GC_SPLIT,       /* MULTIPLAYER > SPLIT SCREEN: two players on this PC */
    GC_LAN,         /* MULTIPLAYER > LAN: the NETWORK screen, local network / direct IP only */
    GC_ONLINE,      /* MULTIPLAYER > ONLINE: the NETWORK screen, internet (mission briefings) only */
    GC_ROGUE,       /* ROGUE ARENA */
    GC_ARENA,       /* ARENA: the ranked ladders (arena_row: which row to start on) */
    GC_MISSIONS,    /* MISSIONS: the mission list, briefings and sorties */
    GC_CONTROLS,    /* SYSTEM > SETTINGS > CONTROLS: the controller mapping page */
    GC_VIDEO,       /* SYSTEM > SETTINGS > VIDEO */
    GC_AUDIO,       /* SYSTEM > SETTINGS > AUDIO */
    GC_AI,          /* SYSTEM > SETTINGS > AI PILOTS */
    GC_UPDATES,     /* SYSTEM > SETTINGS > UPDATES (Android: a newer APK from GitHub) */
    GC_REIMPORT,    /* SYSTEM > SETTINGS > RE-IMPORT DISC: pick a disc, import it over the old one */
    GC_SAVE,        /* SYSTEM > SAVE */
    GC_LOAD,
    GC_NEWGAME,     /* SYSTEM > NEW GAME (confirmed): a fresh save, the starting AC and credits */        /* SYSTEM > LOAD: back to the saved garage */
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
    int opt, opt_top;           /* OPTIONAL PARTS: cursor in the owned options, first row shown */
    /* CHANGE COLOR: 0 its menu, 1 the part menu, 2 the colour editor */
    int paint_step, paint_menu, paint_part_cur, paint_colour, paint_channel;
    uint8_t paint_backup[4][4][3];   /* the paint as the editor found it (○ puts it back) */
    /* The hangar menu: the column and item in focus, where the slides are
     * (eased towards them), how long the item has been held (its details
     * show after 1.5 s). */
    int xmb_col, xmb_row;
    float xmb_pos, xmb_rpos;
    int xmb_hold_ms;
    int arena_row;              /* GC_ARENA: -1 the whole list, else the hub entry (arena_open_at) */
    int specs_open;             /* ASSEMBLE: the specs window (Y / Triangle) */
    int slot_run;               /* ASSEMBLE: the slot cursor without wrapping (slot = slot_run mod rows) */
    float slot_pos;             /* ASSEMBLE: where the slot list's slide is (eased towards slot_run) */
    int anim_ms;                /* a running clock for the menus' animations */
} garage_ui;

typedef struct garage_data {
    acr_gen1_hangar *hangar;
    const acr_gen1_catalog *cat;
    char (*names)[ACR_GEN1_PART_MAX][24];
    int have_pad;          /* a controller is connected (SP-MODERN needs one) */
    const demo_part_specs *specs;   /* the disc's type names and shop texts (may be NULL) */
    const uint8_t (*paint_default)[3];   /* CHANGE COLOR's DEFAULT: the disc's 4 colours (may be NULL) */
} garage_data;

void garage_init(garage_ui *ui);
/* Shows a line of text under the menus for a moment (SAVED. and so on). */
void garage_say(garage_ui *ui, const char *text);
/* Straight into GARAGE > ASSEMBLE (player 2's garage from SPLIT SCREEN). */
void garage_open_assembly(garage_ui *ui, const garage_data *d);
garage_command garage_input(garage_ui *ui, garage_data *d, garage_action a);

/* Draws the menus on the HUD grid (480 high, `hud_w` wide). The 3D AC is
 * drawn by the caller; garage_view_rect gives where, in HUD units. */
void garage_draw(garage_ui *ui, const garage_data *d, float hud_w, int elapsed_ms);
/* What goes behind the 3D models (the hangar, ASSEMBLE's panels): call
 * before drawing them, then garage_draw after. */
void garage_draw_back(const garage_ui *ui, const garage_data *d, float hud_w);
void garage_view_rect(float hud_w, float *x, float *y, float *w, float *h);
/* Where this screen's AC goes (HUD units); 1 = the hangar main menu (the
 * AC stands on its gantry, seen from the front). */
int  garage_view_rect_ui(const garage_ui *ui, float hud_w, float *x, float *y, float *w, float *h);
/* ASSEMBLE: the AC's part kinds (bits 1 << ACR_PART_*) to draw as the
 * selected slot's wireframe; 0 elsewhere. */
unsigned garage_wire_kinds(const garage_ui *ui);
/* ASSEMBLE: the part being looked at, shown turning on the right: its
 * models/menu folder ("head", "back"...) and number, and the box (HUD
 * units). 0 when there is none (NO EQUIP, or another screen). */
int garage_candidate_model(const garage_ui *ui, const garage_data *d, float hud_w, const char **kind, int *index,
                           float *x, float *y, float *w, float *h);
/* OPTIONAL PARTS shows the highlighted option's own model turning in a box
 * (HUD units) instead of the AC: its option bit, or -1 when the page isn't open. */
int garage_option_model(const garage_ui *ui, const garage_data *d, float hud_w,
                        float *x, float *y, float *w, float *h);

#endif
