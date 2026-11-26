/* The garage's NETWORK screen: host a match, join one found on the local
 * network, or join by address. Controller first (the address has an
 * on-screen picker); the keyboard can type the address too. Draws on the
 * HUD grid like the garage; the caller does the networking. */
#ifndef NET_MENU_H
#define NET_MENU_H

#include "garage.h"
#include "net_link.h"

#define NET_MENU_FOUND 8
#define NET_MENU_STAGES 3
#define NET_MODE_ROGUE 3                 /* net_menu.coop_ai: Rogue Arena co-op */

typedef enum net_menu_command {
    NM_NONE, NM_HOST, NM_JOIN, NM_BACK,
    NM_HOST_ONLINE,              /* host over the internet: show a mission briefing */
    NM_JOIN_BRIEF                /* join over the internet: enter a mission briefing */
} net_menu_command;

typedef struct net_menu {
    int row;                         /* cursor */
    int stage_pick;                  /* index into the stage list */
    int stage_ok[NET_MENU_STAGES];   /* that stage is in this import */
    int delay;                       /* input delay to host with */
    int coop_ai;                     /* mode: 0 VS (1v1), 1 or 2: co-op against that many AI ACs,
                                        NET_MODE_ROGUE: Rogue Arena co-op (your run) */
    int ai_preset;                   /* co-op: the AI's style (ACR_GEN1_AI_BALANCED ...) */
    char address[64];                /* join by address */
    int picker;                      /* address picker: which key */
    net_found found[NET_MENU_FOUND];
    int found_n;
    char message[200];
    int message_ms;
} net_menu;

void net_menu_init(net_menu *m);
/* The stage number of the current pick (27 Green Silo, 39 Ocean Platform, 0 open floor). */
int net_menu_stage(const net_menu *m);
const char *net_menu_stage_name(int stage);

/* A menu action (from the pad, or the keyboard: `keyboard` makes OK on
 * the address row join at once). NM_JOIN: the address is in join_to. */
net_menu_command net_menu_input(net_menu *m, garage_action a, int keyboard, char *join_to, int join_size);
/* Typed text (keyboard), added to the address while its row is selected. */
void net_menu_text(net_menu *m, const char *text);
/* Typing goes to the address now (letters shouldn't move the cursor). */
int net_menu_typing(const net_menu *m);
void net_menu_say(net_menu *m, const char *text);

void net_menu_draw(net_menu *m, float hud_w, int elapsed_ms);
/* A waiting / error screen: lines of text in a panel. */
void net_menu_draw_status(float hud_w, const char *title, const char *text);

#endif
