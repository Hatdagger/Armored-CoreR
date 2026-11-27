/* MULTIPLAYER > SPLIT SCREEN: two players on this PC. Pick VERSUS (you
 * against each other, on a stage) or CO-OP (the Arena, Rogue Arena or
 * missions together); player 2 has their own save and AC. Player 2 flies with the second controller (or the only one, with
 * player 1 on the keyboard). Draws on the HUD grid like the garage. */
#ifndef SPLIT_MENU_H
#define SPLIT_MENU_H

#include "garage.h"

#define SPLIT_MENU_STAGES 3

enum { SPLIT_COOP_ARENA, SPLIT_COOP_ROGUE, SPLIT_COOP_MISSIONS, SPLIT_COOP_WHERE };

typedef enum split_menu_command { SM_NONE, SM_VERSUS, SM_COOP, SM_EDIT_P2, SM_P2_SAVE, SM_BACK } split_menu_command;

typedef struct split_menu {
    int row;
    int coop;                            /* 0 VERSUS, 1 CO-OP */
    int where;                           /* co-op: SPLIT_COOP_... */
    char p2_ac[32];
    char p2_save[64];                    /* player 2's save file (set by the caller) */                      /* player 2's AC name (set by the caller) */
    int stage_pick;
    int stage_ok[SPLIT_MENU_STAGES];
    int players_ready;                   /* enough controls for two (set by the caller) */
    char message[120];
    int message_ms;
} split_menu;

void split_menu_init(split_menu *m);
int split_menu_stage(const split_menu *m);   /* 27 Green Silo, 39 Ocean Platform, 0 open floor */
split_menu_command split_menu_input(split_menu *m, garage_action a);
void split_menu_draw(split_menu *m, float hud_w, int elapsed_ms);

#endif
