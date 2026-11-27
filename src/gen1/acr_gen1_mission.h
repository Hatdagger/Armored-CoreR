/* Mission scripts: each mission map's own rules, as its mission code's
 * slot 0 runs them every tick (FDAT 5s+0, loaded at 0x800A0DD0; see
 * docs/research/gen1-stages.md, "Mission script"). Read so far: maps 2,
 * 6, 12, 13 and 15; the other maps keep the plain rule (won when every
 * enemy is down).
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
enum { ACR_GEN1_MISSION_OPEN = 0, ACR_GEN1_MISSION_DONE = 1, ACR_GEN1_MISSION_FAILED = 2 };

/* acr_gen1_mission.flags */
#define ACR_GEN1_MISSION_BONUS 0x01   /* map 6: the tank AC was brought down (event 18) */
#define ACR_GEN1_MISSION_FAIL  0x02   /* the ending running is "mission failed" (0x80052490(256)) */
#define ACR_GEN1_MISSION_SWAP  0x04   /* P4 Altered: the challenger is due (the platform brings it in) */

/* Armored-CoreR's own mission rules (acr_gen1_mission.own), set by the
 * platform for the variant it rolled (acr_gen1_missions.h). Variant 1
 * (Standard) needs none: the plain 1v1 rule.
 * - P4 Alternate layout: two drone MTs beside the examiner (fighter 1).
 *   Only the examiner counts: won when it is down, or with more AP than it
 *   at time up; the drones are optional.
 * - P4 Altered circumstances: once the examiner is below half its AP, the
 *   script asks for the challenger (flag SWAP, phase 1); the platform puts
 *   the challenger in fighter 1's place (the examiner withdraws) and calls
 *   acr_gen1_mission_challenger_in (phase 2). Until then the examiner can't
 *   be knocked out (the challenger comes in instead); at time up the AP of
 *   whoever is fighter 1 decides. The challenger down sets BONUS. */
enum { ACR_GEN1_OWN_NONE = 0, ACR_GEN1_OWN_P4_ALTERNATE, ACR_GEN1_OWN_P4_ALTERED };

typedef struct acr_gen1_mission {
    uint8_t  map;          /* 1 + the mission map with a script, 0 = none (plain rule) */
    uint8_t  own;          /* ACR_GEN1_OWN_*: an Armored-CoreR mission's rule (map is 0 then) */
    int8_t   phase;        /* the script's own step (map 6: the game's 0x800AD838) */
    uint16_t timer;        /* ticks left in a timed step */
    uint8_t  flags;
    uint8_t  result;       /* ACR_GEN1_MISSION_*; DONE / FAILED once the end delay ran out */
    uint16_t end_ticks;    /* the end delay counting down (0x800A0DC0's 100, plus the map's own wait) */
    uint16_t clock;        /* maps 3 and 14: game ticks left on the mission clock (0x80090FD8), 0 = none */
    uint16_t timer2;       /* a second timed step (map 3's drones, map 5's reinforcements) */
    uint8_t  step;         /* a second step counter (map 17's waves, map 5's calls) */
    uint8_t  foe;          /* own missions: the opponent's fighter (0 = fighter 1; co-op on one PC: 2) */
    int32_t  from[3];      /* map 6: where the big MT's current cutscene key started (1/64 units) */
} acr_gen1_mission;

/* 1 if map `map` (0-19) has a script here (since 2026-11-27: all 20;
 * docs/research/gen1-stages.md "Mission scripts, all maps"). */
int acr_gen1_mission_has_script(int map);
/* How chunk 13 record `record` of map `map` waits (ACR_GEN1_WAIT_*). */
int acr_gen1_mission_wait(int map, int record);
/* The same from the record's own flags (record +16, its raw word 8): bit
 * 0x40 = "not made at the start" (read 2026-11-27: on all 20 maps it marks
 * exactly the records the scripts make later with 0x8007ECEC or a
 * cutscene's 1003). Those wait hidden; map 6's big MT waits shown. */
#define ACR_GEN1_RECORD_LATE 0x40
int acr_gen1_mission_wait_flags(int map, int record, unsigned flags);
/* Maps 3 and 14: the mission clock at the start, in game ticks (0 none). */
int acr_gen1_mission_clock_ticks(int map);
/* The battle team of record `record` of map `map`: 1 the enemy side, 0
 * the player's (map 12's friendly units, the game's team 4). */
int acr_gen1_mission_team(int map, int record);
/* Starts map `map`'s script on battle b (after its units joined); a map
 * without one clears it. */
void acr_gen1_mission_start(struct acr_gen1_battle *b, int map);
/* Starts an Armored-CoreR mission's rule (ACR_GEN1_OWN_*) on battle b. */
void acr_gen1_mission_start_own(struct acr_gen1_battle *b, int own);
/* The players' side (team 0) is down: every AC on it (co-op: both players). */
int acr_gen1_mission_players_down(const struct acr_gen1_battle *b);
/* The own mission's opponent fighter (mission.foe, else 1). */
int acr_gen1_mission_foe(const struct acr_gen1_battle *b);
/* P4 Altered: the challenger has taken fighter 1's place. */
void acr_gen1_mission_challenger_in(struct acr_gen1_battle *b);
/* One game tick of the script (the battle calls it after the units). */
void acr_gen1_mission_tick(struct acr_gen1_battle *b);

#endif
