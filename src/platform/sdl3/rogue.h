/* Rogue Arena screens for the PC build: SOLO / CO-OP, the start menu, the hub (NEXT
 * BATTLE, SWAP PARTS, STAGE, GARAGE), the VS screen, the prize screen, the
 * loss and victory screens. The run's rules are in src/gen1/acr_gen1_rogue.c;
 * this file draws them, keeps the run file and hands fights to main.c.
 * Controller first; the keyboard mirrors it (garage_action). */
#ifndef ROGUE_H
#define ROGUE_H

#include "garage.h"
#include "net_link.h"

typedef enum rogue_command {
    RC_NONE,
    RC_FIGHT,       /* start the fight in rogue_fight_info() */
    RC_GARAGE,      /* open the run's garage (assembly only) */
    RC_LEAVE,       /* back to the main garage menu */
    RC_NETWORK,     /* co-op: open NETWORK with ROGUE ARENA CO-OP picked */
    RC_NET_CLOSE    /* online co-op is over: close the link */
} rogue_command;

typedef struct rogue_fight {
    /* Solo and the Arena: 0 the player, 1 the opponent. Co-op (humans 2):
     * 0 player 1, 1 player 2, 2 and 3 the pair (fighters 4), or the final
     * duel, player 1 against player 2 (fighters 2). */
    int parts[4][SLOT_COUNT];
    uint8_t colours[4][4][3];
    uint8_t plus[4];                   /* Human PLUS level (0 none, 9 full) */
    acr_gen1_ai_profile ai;            /* the opponent's pilot (solo, the Arena) */
    acr_gen1_ai_profile pair_ai[2];    /* co-op: fighters 2 and 3's pilots */
    int stage;                         /* stage id */
    int humans;                        /* 0 or 1 solo / the Arena, 2 co-op */
    int fighters;                      /* co-op: 4 against a pair, 2 the duel */
    char name[4][48];
    /* Online co-op (online 1): this PC's player (0 host, 1 guest), the
     * fight's random seed (the host's), the input delay. The fight runs on
     * rollback, the pair's pilots inside the match. */
    int online;
    int net_local;
    uint32_t net_seed;
    int net_delay;
} rogue_fight;

/* main.c draws the ACs of the VS and victory screens: build `parts` in
 * `colours`, inside the HUD box (x, y, w, h) of the 480-high grid, the top
 * `frac` of the AC showing (1 = all of it), turned `yaw` radians from
 * facing the viewer, on its back when `lying`, `shade` 0..1 of its normal
 * light. The drawer leaves the HUD set up again for 2D. */
typedef void (*rogue_ac_drawer)(const int parts[SLOT_COUNT], const uint8_t colours[4][3], float x, float y,
                                float w, float h, float frac, float yaw, int lying, float shade);
void rogue_set_ac_drawer(rogue_ac_drawer fn);

/* Reads the opponents and part info from the import. 0 = the import has
 * no arena opponents yet (an older import: re-import the disc). */
int rogue_init(const char *import_dir, const acr_gen1_catalog *cat,
               char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]);
/* From the garage's ROGUE ARENA: the start menu. Names for a new run. */
void rogue_open(const char *pilot, const char *ac);
rogue_command rogue_input(garage_action a);
/* Draws the current screen on the HUD grid (480 high, `hud_w` wide). */
void rogue_draw(float hud_w, int elapsed_ms);

const rogue_fight *rogue_fight_info(void);
/* The VS screen is done (the count ran out, or was skipped, and the
 * shutters are shut): start the fight. */
int rogue_vs_due(void);
/* The fight is over: 1 the player won, 2 lost, 3 draw (a loss), as
 * acr_gen1_battle_result. */
void rogue_fight_over(int result);
/* A co-op fight is over (rogue_fight_info()->humans == 2): the result as
 * acr_gen1_battle_result (1 the players' team / player 1 in the duel, 2 the
 * pair / player 2, 3 a draw) and whether each player's AC was destroyed.
 * Online, 0 means the fight was abandoned (a player left, the connection
 * was lost): it doesn't count, and the session ends. */
void rogue_coop_fight_over(int result, int p1_down, int p2_down);

/* Online co-op (docs/ROGUE-ARENA-COOP.md): the link is connected with
 * ROGUE ARENA CO-OP picked; the co-op screens take it from here (the host
 * keeps the run). main.c keeps the link and closes it on RC_NET_CLOSE. */
void rogue_coop_online_begin(net_link *link, const net_match_info *info);
/* Once a frame while the co-op screens or an online co-op fight are on:
 * messages, the connection, timers. RC_FIGHT starts the fight in
 * rogue_fight_info(); RC_NET_CLOSE ends the online session. */
rogue_command rogue_tick(uint64_t now_ms, int in_fight);
/* Online co-op fight, the guest: the host has sent the result (as
 * rogue_coop_fight_over takes it). 1 = it came. */
int rogue_coop_net_result(int *result, int *p1_down, int *p2_down);
/* Online co-op: this PC is in a session. */
int rogue_coop_online(void);

/* The run's garage (assembly) and its catalogue (prices in P, no shop). */
garage_data rogue_garage_data(void);
/* Back from the run's garage: saves. */
void rogue_garage_done(void);
/* The player's AC as the hub shows it (parts and colours); 0 = no run. */
int rogue_player_ac(int parts[SLOT_COUNT], uint8_t colours[4][3]);

#endif
