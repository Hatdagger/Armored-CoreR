/* The ARENA screens for the PC build (main menu > ARENA): the arena list
 * (the main arena and the Ex Arenas, with the match timer and the stage),
 * the ladder (RANK, PILOT, CHALLENGE / Reference Only, as Master of
 * Arena's), the challenge, and the result and prize windows. The rules are
 * in src/gen1/acr_gen1_arena.c; this file draws them, keeps arena.toml and
 * hands fights to main.c. The player fights with the main garage's AC and
 * the prizes go into that garage. */
#ifndef ARENA_UI_H
#define ARENA_UI_H

#include "rogue.h"

typedef enum arena_command {
    AC_NONE,
    AC_FIGHT,       /* start the fight in arena_fight_info() (now: when arena_vs_due()) */
    AC_LEAVE,       /* back to the main menu */
    AC_IMPORT_PP    /* the Phantasma row without its data: ask for the Project Phantasma disc */
} arena_command;

/* Reads the arena roster from the import and the standings file. The
 * player's garage is the main one: prizes are paid into `hangar`. 0 = the
 * import has no arena opponents (re-import the disc). */
int arena_init(const char *import_dir, acr_gen1_hangar *hangar, const acr_gen1_catalog *cat,
               char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]);
void arena_set_ac_drawer(rogue_ac_drawer fn);
/* Reads the roster again (after the Project Phantasma disc was imported). */
int arena_reload(void);
/* A line in the screen's message box. */
void arena_say(const char *t);
/* Main menu > ARENA. */
void arena_open(void);
/* The same, the cursor on arena list row `row` (0 Phantasma, 1 MoA
 * SUB-ARENA, 2 MoA, 3 on the Ex Arenas; the hangar menu's ARENA items). */
void arena_open_at(int row);
arena_command arena_input(garage_action a);
void arena_draw(float hud_w, int elapsed_ms);

/* After YES on a challenge the VS screen runs (as Rogue Arena's); 1 once
 * it is done: start the fight in arena_fight_info(). */
int arena_vs_due(void);

/* The fight to start (the same layout as Rogue Arena's) and its round
 * length in ms (0 = no time limit). */
const rogue_fight *arena_fight_info(void);
/* The imported arena opponent whose AC is called `ac` (any arena), or NULL. */
const acr_gen1_rogue_foe *arena_find_ac(const char *ac);
int32_t arena_round_ms(void);
/* The fight is over: 1 won, 2 lost, 3 draw (a loss). Books the result and
 * pays a win into the garage; 1 = the garage changed (save it). */
int arena_fight_over(int result);

#endif
