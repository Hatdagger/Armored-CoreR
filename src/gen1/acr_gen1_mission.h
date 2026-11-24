/* Mission scripts: each mission map's own rules, as its mission code's
 * slot 0 runs them every tick (FDAT 5s+0, loaded at 0x800A0DD0; see
 * docs/research/gen1-stages.md, "Mission script"). Read so far: maps 2,
 * 6, 13 and 15; the other maps keep the plain rule (won when every enemy
 * is down).
 *
 * Part of the battle (acr_gen1_battle.mission), stepped with the units, so
 * netplay stays in step. The game's cutscenes (chunk 5, run by 0x80093844
 * while the fight waits) are skipped: what they change happens at once,
 * and the big MT's climb out of the sea follows cutscene 3's path during
 * the fight (see the .c). */
#ifndef ACR_GEN1_MISSION_H
#define ACR_GEN1_MISSION_H

#include <stdint.h>

struct acr_gen1_battle;

/* How a unit waits for its map's script (acr_gen1_unit.wait). */
enum {
    ACR_GEN1_WAIT_NONE = 0,     /* in the fight from the start */
    ACR_GEN1_WAIT_HIDDEN,       /* not made yet: not drawn, not in the fight */
    ACR_GEN1_WAIT_SHOWN         /* made but out of the fight (map 6's big MT under the sea) */
};

/* acr_gen1_mission.result */
enum { ACR_GEN1_MISSION_OPEN = 0, ACR_GEN1_MISSION_DONE = 1 };

/* acr_gen1_mission.flags */
#define ACR_GEN1_MISSION_BONUS 0x01   /* map 6: the tank AC was brought down (event 18) */

typedef struct acr_gen1_mission {
    uint8_t  map;          /* 1 + the mission map with a script, 0 = none (plain rule) */
    int8_t   phase;        /* the script's own step (map 6: the game's 0x800AD838) */
    uint16_t timer;        /* ticks left in a timed step */
    uint8_t  flags;
    uint8_t  result;       /* ACR_GEN1_MISSION_*; DONE once the end delay ran out */
    uint8_t  end_ticks;    /* the end delay counting down (0x800A0DC0's 100) */
    int32_t  from[3];      /* map 6: where the big MT's current cutscene key started (1/64 units) */
} acr_gen1_mission;

/* 1 if map `map` (0-19) has a script here. */
int acr_gen1_mission_has_script(int map);
/* How chunk 13 record `record` of map `map` waits (ACR_GEN1_WAIT_*). */
int acr_gen1_mission_wait(int map, int record);
/* Starts map `map`'s script on battle b (after its units joined); a map
 * without one clears it. */
void acr_gen1_mission_start(struct acr_gen1_battle *b, int map);
/* One game tick of the script (the battle calls it after the units). */
void acr_gen1_mission_tick(struct acr_gen1_battle *b);

#endif
