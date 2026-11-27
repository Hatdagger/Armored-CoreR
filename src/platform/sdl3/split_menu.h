/* MULTIPLAYER > SPLIT SCREEN: the split hub. Two players on this PC, each
 * with their own half of the screen, their own cursor and their own
 * controls (player 1: keyboard / controller 1; player 2: controller 2, or
 * the only controller with player 1 on the keyboard). The top box (VERSUS /
 * CO-OP and where) is shared: either player can change it. Each player
 * picks their save, opens their own garage and says READY; the game starts
 * once everyone in is ready. B leaves (only that player); A joins again.
 * When both have left, it's back to the hangar. */
#ifndef SPLIT_MENU_H
#define SPLIT_MENU_H

#include "garage.h"

#define SPLIT_MENU_STAGES 3

enum { SPLIT_COOP_ARENA, SPLIT_COOP_ROGUE, SPLIT_COOP_MISSIONS, SPLIT_COOP_WHERE };

typedef enum split_menu_command {
    SM_NONE,
    SM_START,      /* everyone in is ready: start (joined[] says who plays) */
    SM_GARAGE,     /* player `who` opens their garage */
    SM_SAVE,       /* player `who` picks their save */
    SM_ALL_LEFT,   /* both players left: back to the hangar */
    SM_VERSUS_GO,  /* (the caller's own: start the versus fight) */
} split_menu_command;

typedef struct split_menu {
    int row[2];                          /* each player's own cursor */
    int joined[2];
    int ready[2];
    int who;                             /* the player the last command is for */
    int coop;                            /* 0 VERSUS, 1 CO-OP */
    int where;                           /* co-op: SPLIT_COOP_... */
    char ac[2][32];                      /* each player's AC name (set by the caller) */
    char save[2][64];                    /* each player's save file (set by the caller) */
    int stage_pick;
    int stage_ok[SPLIT_MENU_STAGES];
    int p2_controls;                     /* player 2 has something to play with (set by the caller) */
    char message[120];
    int message_ms;
} split_menu;

void split_menu_init(split_menu *m);
void split_menu_enter(split_menu *m, int p1_in, int p2_in);   /* who is in when the hub opens; nobody ready */
int split_menu_stage(const split_menu *m);   /* 27 Green Silo, 39 Ocean Platform, 0 open floor */
void split_menu_say(split_menu *m, const char *text);
split_menu_command split_menu_input(split_menu *m, int who, garage_action a);
void split_menu_draw(split_menu *m, float hud_w, int elapsed_ms);

#endif
