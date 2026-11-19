/* A two-player match: the fight plus the round around it (clock, knock-out,
 * result, next round), in one plain struct that can be copied as a whole.
 *
 * That copy is the snapshot online play needs: rollback saves one before
 * every step and goes back to it when a late input arrives, and a player
 * joining mid-fight will be sent one. Everything that decides what happens
 * next lives in here (integer only, no clocks, no globals); the pointers
 * inside (each AC's def, the stage) point at data that stays the same for
 * the whole match on both machines.
 *
 * The round rules are the VS mode's, as the test build's main loop ran them:
 * a two-minute clock; a knock-out (or leaving the area) settles 12 Gen 1
 * ticks after it; at time up the higher AP wins. After the round the game
 * keeps running with the controls let go, so the ACs drop where they are,
 * then the next round starts by itself. */
#ifndef ACR_GEN1_MATCH_H
#define ACR_GEN1_MATCH_H

#include <stdint.h>

#include "acr_gen1_battle.h"

typedef struct acr_gen1_match_rules {
    int32_t round_ms;       /* length of a round (0 = no clock) */
    int32_t next_round_ms;  /* after a round ends, the next one starts this much later (0 = never) */
    int32_t arena_half;     /* the open test floor's square walls, world units (used without a stage) */
} acr_gen1_match_rules;

typedef struct acr_gen1_match {
    acr_gen1_battle b;
    uint32_t frame;         /* steps run since the match began */
    int32_t  round_ms;      /* time left in the round */
    int32_t  ko_ms;         /* since the knock-out (or leaving the area) */
    int32_t  over_ms;       /* since the round ended */
    uint8_t  round_over;
    uint8_t  result;        /* 1 or 2: that player won; 3: a draw */
    uint8_t  time_up;       /* the round ended on the clock */
    uint8_t  round;         /* rounds started, 1 = the first */
    uint8_t  wins[2];
} acr_gen1_match;

/* A new match from a fight set up and ready to go (both ACs placed,
 * weapons loaded). `start` is kept by the caller and used again for each
 * new round. */
void acr_gen1_match_init(acr_gen1_match *m, const acr_gen1_battle *start, const acr_gen1_match_rules *r);

/* One step (battle.step_ms) with both players' controls. While a round is
 * over the controls are ignored. */
void acr_gen1_match_step(acr_gen1_match *m, const acr_gen1_battle *start, const acr_gen1_match_rules *r,
                         const acr_gen1_input in[2], const acr_gen1_actions act[2]);

/* A check number of everything that decides the fight (positions, speeds,
 * energy, AP, ammo, shots, random numbers, the clock). Two machines whose
 * matches have the same hash at the same frame are still in step. Built
 * field by field, so it does not depend on pointers or padding. */
uint32_t acr_gen1_match_hash(const acr_gen1_match *m);

#endif
