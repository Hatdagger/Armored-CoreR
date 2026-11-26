/* Rogue Arena co-op: two players climb one ladder together
 * (docs/ROGUE-ARENA-COOP.md). Built on the solo run's rules
 * (acr_gen1_rogue.h): each player keeps a solo-style run of their own (AC,
 * P, prize screens, HUMAN PLUS / ALL OPTIONS / +1 P PER WIN), and this
 * holds what they share: the ladder of pairs, the ghosts, the stash of
 * parts saved for later, the final duel. No OS calls.
 *
 * How a run goes (user, 2026-11-25):
 *  - One seed makes the run: 19 rungs of two of the game's arena opponents
 *    (38, on the solo ladder's curve), each player a random legal starting
 *    AC with a real gun, both at rank 20 with 0 P.
 *  - A fight is the two players against the rung's pair, no time limit,
 *    until one side is all down.
 *  - A win: each player +1 P (+ their +1 P PER WIN), the destroyed partner
 *    too. A partner destroyed in the won fight rejoins: KEEP their old AC,
 *    which costs every part they saved in the stash and all their P, or
 *    take a NEW RANDOM AC for free. Then each player's own prize screen.
 *  - Both down: the attempt ends for both. RETRY keeps the ladder, gives
 *    both new random ACs at 0 P, and the losing duo stays on the ladder as
 *    a ghost pair at the rank it held. NEW ARENA starts a new seed.
 *  - Beating rank 1's pair: the final duel, the two players 1v1 on the
 *    host's stage with their current builds. The winner takes rank 1, the
 *    loser rank 2.
 *  - The stash: parts either player saves for later go into one shared
 *    stash, each marked with who saved it. Either player can equip any of
 *    them in SWAP PARTS; a part the other player is highlighting can't be
 *    highlighted or taken. */
#ifndef ACR_GEN1_COOP_H
#define ACR_GEN1_COOP_H

#include "acr_gen1_rogue.h"

#define ACR_GEN1_COOP_PLAYERS   2
#define ACR_GEN1_COOP_GHOSTS    (2 * ACR_GEN1_ROGUE_SLOTS)
/* Room for what both players could save in solo. */
#define ACR_GEN1_COOP_STASH_MAX (2 * ACR_GEN1_ROGUE_SAVED_MAX)

typedef struct acr_gen1_coop_item {
    uint8_t cat, index;
    uint8_t owner;              /* 0 player 1 (the host), 1 player 2: who saved it */
} acr_gen1_coop_item;

typedef struct acr_gen1_coop {
    uint64_t seed;
    /* rung i = rank 20 - i; rung 0 the players. A ghost pick's rank is its
     * index in `ghost`. */
    acr_gen1_rogue_pick pair[ACR_GEN1_ROGUE_SLOTS][2];
    acr_gen1_rogue_ghost ghost[ACR_GEN1_COOP_GHOSTS];
    uint8_t  ghosts;
    uint8_t  attempt, wins, best;
    uint8_t  over;              /* both down: RETRY or NEW ARENA */
    uint8_t  duel_due;          /* rank 1's pair beaten, the duel not yet won */
    uint8_t  cleared;           /* the duel is decided */
    uint8_t  duel_winner;       /* 0 player 1, 1 player 2 (when cleared) */
    uint8_t  rejoin[ACR_GEN1_COOP_PLAYERS];   /* destroyed in the won fight: the choice is due */
    uint8_t  stage;             /* the host's pick (0 = the default) */
    /* Each player's own side: hangar, P, prize screens, upgrades. Their
     * `wins` follow the team's; their ladder and `saved` aren't used (saved
     * parts move into the stash). */
    acr_gen1_rogue player[ACR_GEN1_COOP_PLAYERS];
    uint8_t  stash_n;
    acr_gen1_coop_item stash[ACR_GEN1_COOP_STASH_MAX];
    int8_t   hold[ACR_GEN1_COOP_PLAYERS];     /* the stash part each is highlighting, -1 none (not saved) */
} acr_gen1_coop;

/* A new run: both players' names (pilot, AC). 1 = ok. */
int acr_gen1_coop_new(acr_gen1_coop *c, const acr_gen1_rogue_world *w, uint64_t seed,
                      const char *pilot[ACR_GEN1_COOP_PLAYERS], const char *ac[ACR_GEN1_COOP_PLAYERS]);

/* The next pair (rung wins + 1); NULL when the ladder is done (duel or cleared). */
const acr_gen1_rogue_pick *acr_gen1_coop_next_pair(const acr_gen1_coop *c);
/* What one opponent of a pair fights with. 1 = ok. */
int acr_gen1_coop_rung(const acr_gen1_coop *c, const acr_gen1_rogue_world *w, int rung, int k,
                       acr_gen1_rogue_foe *out);

/* A fight against a pair: won, with who was destroyed in it. */
void acr_gen1_coop_won(acr_gen1_coop *c, int down0, int down1);
/* Both players down (or gave up). */
void acr_gen1_coop_lost(acr_gen1_coop *c);
/* After a loss: the same ladder, the losing duo as a ghost pair at the rank
 * it held (when it had won at least once), new random ACs at 0 P, the
 * stash emptied. 1 = ok. */
int acr_gen1_coop_retry(acr_gen1_coop *c, const acr_gen1_rogue_world *w);

/* The rejoin choice: `keep` 1 = the old AC (every stash part this player
 * saved and all their P are paid for it), 0 = a new random AC (free). */
int acr_gen1_coop_rejoin(acr_gen1_coop *c, const acr_gen1_rogue_world *w, int p, int keep);
/* What KEEP would cost: the stash parts and P. */
int acr_gen1_coop_keep_cost(const acr_gen1_coop *c, int p, int *parts, int32_t *points);

/* Player p's prize screen is waiting (after any rejoin choice). The screen
 * itself runs on c->player[p] with the solo calls (acr_gen1_rogue_offers,
 * _take, _pass, _reroll); then acr_gen1_coop_collect moves what was saved
 * for later into the stash. */
int acr_gen1_coop_prize_due(const acr_gen1_coop *c, int p);
/* Moves player p's saved-for-later parts into the stash, marked as theirs. */
void acr_gen1_coop_collect(acr_gen1_coop *c, int p);
/* Room in the stash for an offer saved for later (a SET is three parts). */
int acr_gen1_coop_stash_room(const acr_gen1_coop *c, const acr_gen1_rogue_offer *o);
/* Both players are done (rejoin and prize screens): the next fight can start. */
int acr_gen1_coop_ready(const acr_gen1_coop *c);

/* The stash lock: player p highlights item n (-1 lets go). 0 = the other
 * player is on it. */
int acr_gen1_coop_hold(acr_gen1_coop *c, int p, int n);
/* Stash item n as an offer for player p's AC. 1 = fits now. */
int acr_gen1_coop_stash_fit(const acr_gen1_coop *c, const acr_gen1_rogue_world *w, int p, int n,
                            acr_gen1_rogue_offer *out);
/* Player p equips stash item n at place `put` (the copy becomes theirs;
 * what it replaces is gone for good). 0 = refused (held by the other
 * player, or it doesn't fit). */
int acr_gen1_coop_stash_take(acr_gen1_coop *c, const acr_gen1_rogue_world *w, int p, int n, int put);

/* The final duel's result: winner 0 or 1. A draw is fought again. */
void acr_gen1_coop_duel(acr_gen1_coop *c, int winner);
/* A player's rank now: the team's rank during the run, 1 / 2 after the duel. */
int acr_gen1_coop_rank(const acr_gen1_coop *c, int p);

/* ---- the run file (the host's save; each player's garage is saved apart) ---- */
#define ACR_GEN1_COOP_FORMAT 1
size_t acr_gen1_coop_write(const acr_gen1_coop *c, char *buf, size_t size);
/* Reads the run; c->player[k].hangar must already hold each garage. 1 = ok. */
int acr_gen1_coop_read(acr_gen1_coop *c, const char *text);

/* ---- online (the host keeps the run; docs/ROGUE-ARENA-COOP.md) ---- */
/* The whole run in one text: the run file, both garages and the stash
 * lock (what the host sends whenever something changes). Returns the
 * length (>= size: too small). */
size_t acr_gen1_coop_state_write(const acr_gen1_coop *c, char *buf, size_t size);
/* Reads it whole. 1 = ok (c is left alone on a bad text). */
int acr_gen1_coop_state_read(acr_gen1_coop *c, const char *text);
/* One player's own side after their prize screen: garage, P, prize state,
 * upgrades and the parts saved for later (not yet in the stash). */
size_t acr_gen1_coop_side_write(const acr_gen1_coop *c, int p, char *buf, size_t size);
/* Puts that side in as player p's (the rest of the run stays). 1 = ok. */
int acr_gen1_coop_side_read(acr_gen1_coop *c, int p, const char *text);

#endif
