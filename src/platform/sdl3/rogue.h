/* Rogue Arena screens for the PC build: the start menu, the hub (NEXT
 * BATTLE, SWAP PARTS, STAGE, GARAGE), the VS screen, the prize screen, the
 * loss and victory screens. The run's rules are in src/gen1/acr_gen1_rogue.c;
 * this file draws them, keeps the run file and hands fights to main.c.
 * Controller first; the keyboard mirrors it (garage_action). */
#ifndef ROGUE_H
#define ROGUE_H

#include "garage.h"

typedef enum rogue_command {
    RC_NONE,
    RC_FIGHT,       /* start the fight in rogue_fight_info() */
    RC_GARAGE,      /* open the run's garage (assembly only) */
    RC_LEAVE        /* back to the main garage menu */
} rogue_command;

typedef struct rogue_fight {
    int parts[2][SLOT_COUNT];          /* 0 the player, 1 the opponent */
    uint8_t colours[2][4][3];
    uint8_t plus[2];                   /* Human PLUS level (0 none, 9 full) */
    acr_gen1_ai_profile ai;            /* the opponent's pilot */
    int stage;                         /* stage id */
    char name[2][48];
} rogue_fight;

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
/* The VS screen's countdown ran out: start the fight. */
int rogue_vs_due(void);
/* The fight is over: 1 the player won, 2 lost, 3 draw (a loss), as
 * acr_gen1_battle_result. */
void rogue_fight_over(int result);

/* The run's garage (assembly) and its catalogue (prices in P, no shop). */
garage_data rogue_garage_data(void);
/* Back from the run's garage: saves. */
void rogue_garage_done(void);
/* The player's AC as the hub shows it (parts and colours); 0 = no run. */
int rogue_player_ac(int parts[SLOT_COUNT], uint8_t colours[4][3]);

#endif
