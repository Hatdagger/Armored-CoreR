/* The MISSIONS screens for the PC build (main menu > MISSIONS): the mission
 * list (Armored-CoreR's own missions, then Master of Arena's from the
 * player's disc), the briefing (the mail header with sender, requester and
 * reward, then the text pages with their pictures), the sortie, and Master
 * of Arena's income and expense report after it. The rules are in
 * src/gen1/acr_gen1_missions.c; this file draws them and hands the sortie
 * to main.c. The player flies the main garage's AC, the report is paid
 * into that garage, and the record of missions played is kept in the
 * garage's save (its [missions] section). */
#ifndef MISSIONS_UI_H
#define MISSIONS_UI_H

#include "../../gen1/acr_gen1_missions.h"
#include "rogue.h"

typedef enum missions_command {
    MC_NONE,
    MC_SORTIE,      /* start the sortie in missions_fight_info() */
    MC_LEAVE        /* back to the main menu */
} missions_command;

/* Reads the mission list and the report pictures from the import. */
void missions_init(const char *import_dir, acr_gen1_hangar *hangar, const acr_gen1_catalog *cat);
/* Main menu > MISSIONS. */
void missions_open(void);
missions_command missions_input(garage_action a);
void missions_draw(float hud_w, int elapsed_ms);

/* The sortie to start: the player's AC and the opponent (mission P4: the
 * Nest examiner), the mission map, and the time limit in ms (0 none). */
const rogue_fight *missions_fight_info(void);
int32_t missions_round_ms(void);
/* The opponent's option bits. */
uint16_t missions_foe_options(void);
/* The sortie is over: 1 won, anything else failed. The player's AP left
 * and AP max, and the ammunition bill (acr_gen1_mission_ammo_cost). Pays
 * the report into the garage and books the record; returns 1 (the garage
 * changed: save it). */
int missions_fight_over(int result, int32_t ap_left, int32_t ap_max, int32_t ammo_cost);

/* The record of missions played, as the save file keeps it. */
acr_gen1_mission_record *missions_record(void);

#endif
