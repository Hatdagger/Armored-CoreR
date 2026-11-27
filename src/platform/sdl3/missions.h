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
/* The opponent's option bits (the challenger's once it came). */
uint16_t missions_foe_options(void);
/* The sortie's variant (1..3) and its rule for the battle (ACR_GEN1_OWN_*,
 * acr_gen1_mission_start_own), and the time a late arrival adds (ms). */
int missions_variant(void);
int missions_rule(void);
/* The sortie is a Master of Arena mission: the player alone on its map
 * against the map's units, run by the map's script (no opponent AC). */
int missions_solo(void);
/* Testing aid (ACR_MISSION=MOA-05, P4, ...): picks that mission and
 * prepares its sortie, skipping the screens; 1 when it can fly. */
int missions_launch(const char *id);
int32_t missions_extra_ms(void);
/* The radio line for an event of the sortie (0 the start, 1 the challenger
 * comes), NULL none. */
const char *missions_radio(int event);
/* P4 Altered circumstances: puts the challenger (an arena AC from the
 * import) in missions_fight_info()'s opponent place. 0 = not in the import. */
int missions_challenger(void);
/* The sortie is over: 1 won, anything else failed. The player's AP left
 * and AP max, the ammunition bill (acr_gen1_mission_ammo_cost), and
 * whether the variant's bonus was earned (its special addition). Pays
 * the report into the garage and books the record; returns 1 (the garage
 * changed: save it). */
int missions_fight_over(int result, int32_t ap_left, int32_t ap_max, int32_t ammo_cost, int bonus);
void missions_settle_other(acr_gen1_hangar *h, int result, int32_t ap_left, int32_t ap_max, int32_t ammo_cost, int bonus);

/* The record of missions played, as the save file keeps it. */
acr_gen1_mission_record *missions_record(void);

#endif
