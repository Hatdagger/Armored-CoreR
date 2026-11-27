/* Rogue Arena: a roguelike arena run, brought over from the AC1Recomp fork
 * (games/master-of-arena/moa_arena_run.c and moa_arena_run_boot.c, release
 * 0.8.4.4.2) at its rules, with a shorter ladder: 19 opponents + the player
 * (AC1Recomp: 49 + 1). No OS calls: the platform loads the opponents and
 * part info from the import and reads / writes the run file.
 *
 * How a run goes (as in AC1Recomp):
 *  - One seed makes the run: the ladder (19 of the game's own arena
 *    opponents, picked on a difficulty curve, KARASAWA users kept to the
 *    top 10, Human PLUS users to the top 8) and a random legal starting AC
 *    that has a real gun. The player starts at the bottom (rank 20) with 0 P.
 *  - Each win: +1 P (+ the +1 P PER WIN upgrades), then a prize screen: a
 *    cheap (1-2 P), mid (3-4 P) and expensive (5-7 P) part offer, or a
 *    special (HUMAN PLUS 13 P, ALL OPTIONS 15 P, +1 P PER WIN 6 P, ARMS SET
 *    7 P), PASS for +1 P, or RE-ROLL once for 1 P. A part is equipped now
 *    (what it replaces is gone for good) or saved for later (SWAP PARTS).
 *  - One loss ends the attempt: RETRY (the same ladder, a new random AC at
 *    0 P; the losing AC stays on the ladder as a ghost at the rank it held),
 *    NEW ARENA (a new seed) or QUIT. Beating rank 1 clears the arena.
 * Prices: a part's credit price as P by where it sits among all parts
 * (acr_gen1_rogue_price). */
#ifndef ACR_GEN1_ROGUE_H
#define ACR_GEN1_ROGUE_H

#include <stddef.h>
#include <stdint.h>

#include "acr_gen1_ai.h"
#include "acr_gen1_garage.h"

#define ACR_GEN1_ROGUE_SLOTS     20    /* rank 20 (the player's start) .. rank 1 */
#define ACR_GEN1_ROGUE_ROSTER    192   /* arena opponents read from the import */
#define ACR_GEN1_ROGUE_ARENAS    9     /* runs draw from Master of Arena's arenas 0-8 only */
#define ACR_GEN1_ROGUE_SAVED_MAX 24    /* parts saved for later */
#define ACR_GEN1_ROGUE_NAME      20

/* ---- what the platform supplies ---- */

/* One of the game's arena opponents (acr-import's arena/ files). */
typedef struct acr_gen1_rogue_foe {
    uint8_t  arena;            /* 0 main arena, 1-8 the Ex Arenas */
    uint8_t  rank;             /* 1 = that arena's top */
    uint8_t  entries;          /* opponents in that arena, the player's place included */
    uint8_t  parts[ACR_GEN1_SLOT_COUNT];   /* ACR_GEN1_NO_PART = none */
    uint16_t options;
    uint8_t  colours[4][3];
    uint8_t  karasawa;         /* carries the KARASAWA */
    acr_gen1_ai_profile ai;    /* ai.rebel = its Human PLUS level */
    char     pilot[ACR_GEN1_ROGUE_NAME], ac[ACR_GEN1_ROGUE_NAME];
    /* The Arena (acr_gen1_arena.h); Rogue Arena doesn't use them. */
    uint32_t prize_money;      /* credits for a win */
    uint8_t  prize_part;       /* the game's prize id (0-22), 255 none */
} acr_gen1_rogue_foe;

/* What the prize rules need to know about a part beyond the catalogue. */
#define ACR_GEN1_ROGUE_RADAR   0x01u   /* back radar: never offered */
#define ACR_GEN1_ROGUE_STEALTH 0x02u   /* P77-ST: never offered */
#define ACR_GEN1_ROGUE_STANCE  0x04u   /* back cannon: two-legged ACs kneel to fire */
typedef struct acr_gen1_rogue_parts {
    uint16_t attack[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX];
    uint8_t  flags[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX];
} acr_gen1_rogue_parts;

/* ---- the run ---- */

/* A rung of the ladder. */
enum { ACR_GEN1_ROGUE_PLAYER = 0, ACR_GEN1_ROGUE_FOE, ACR_GEN1_ROGUE_GHOST };
typedef struct acr_gen1_rogue_pick {
    uint8_t kind;              /* ACR_GEN1_ROGUE_* */
    uint8_t arena, rank;       /* FOE: the opponent; GHOST: rank = ghost index */
} acr_gen1_rogue_pick;

/* A losing AC from an earlier attempt, kept on the ladder. */
typedef struct acr_gen1_rogue_ghost {
    uint8_t  used, attempt;
    uint8_t  rank_held;        /* the rank it reached (where it lost) */
    uint8_t  plus;             /* had HUMAN PLUS */
    uint8_t  parts[ACR_GEN1_SLOT_COUNT];
    uint16_t options;
    uint8_t  colours[4][3];
    acr_gen1_rogue_pick ai_from;   /* AI: copied from the opponent it pushed down */
    char     pilot[ACR_GEN1_ROGUE_NAME], ac[ACR_GEN1_ROGUE_NAME];
} acr_gen1_rogue_ghost;

typedef struct acr_gen1_rogue {
    uint64_t seed;
    acr_gen1_rogue_pick slot[ACR_GEN1_ROGUE_SLOTS];     /* slot i = rank 20 - i; slot 0 the player */
    acr_gen1_rogue_ghost ghost[ACR_GEN1_ROGUE_SLOTS];
    uint8_t  ghosts;
    /* This attempt (Retry resets it). */
    uint8_t  attempt;          /* 0 = the first */
    uint8_t  wins;             /* opponents beaten: rank = 20 - wins */
    uint8_t  best;             /* most wins of any attempt */
    uint8_t  over;             /* 1 = lost: Retry or New Arena */
    uint8_t  cleared;          /* 1 = rank 1 beaten */
    uint8_t  prize_wins;       /* wins whose prize screen is done */
    uint8_t  reroll_win;       /* prize screen (win + 1) whose RE-ROLL was used, 0 none */
    uint8_t  income;           /* +1 P PER WIN bought (extra P a win) */
    uint8_t  plus;             /* HUMAN PLUS bought */
    uint8_t  opts;             /* ALL OPTIONS bought */
    uint16_t points;           /* P earned this attempt (wins and PASS) */
    uint16_t spent;            /* P spent this attempt */
    uint8_t  saved_n;
    uint8_t  saved[ACR_GEN1_ROGUE_SAVED_MAX][2];          /* category, index */
    uint8_t  stage;            /* last stage picked (0 = the default) */
    acr_gen1_hangar hangar;    /* the run's garage: credits are P */
} acr_gen1_rogue;

/* The roster the ladder is drawn from. */
typedef struct acr_gen1_rogue_world {
    const acr_gen1_rogue_foe *foe;
    int foes;
    const acr_gen1_catalog *cat;
    const acr_gen1_rogue_parts *info;
} acr_gen1_rogue_world;

/* Seeded generator (splitmix64), as AC1Recomp: one seed, one run. */
uint64_t acr_gen1_rogue_next(uint64_t *state);
uint32_t acr_gen1_rogue_below(uint64_t *state, uint32_t n);
/* A seed from text: up to 16 hex digits are the number, anything else hashed. */
uint64_t acr_gen1_rogue_seed_from_text(const char *s);

/* A part's credit price as P: 1 (cheapest 15%) .. 7 (top 5%). */
uint32_t acr_gen1_rogue_price(uint32_t credits);

/* The difficulty score (0-100) of an arena's opponent. */
int acr_gen1_rogue_score(const acr_gen1_rogue_foe *f);

/* Fill the ladder: slot 0 the player, then 19 opponents, easiest first.
 * Returns the slots filled (20 unless the roster is too small). */
int acr_gen1_rogue_lineup(const acr_gen1_rogue_world *w, uint64_t seed,
                          acr_gen1_rogue_pick out[ACR_GEN1_ROGUE_SLOTS]);
/* Co-op (acr_gen1_coop.h): the same curve with two opponents a rung, 38
 * drawn and paired easiest first; every rule that counts rungs (KARASAWA in
 * the top 10, Human PLUS in the top 8, the tight top 8) counts pairs. Slot
 * 0 is the players. Returns the rungs filled (20 unless the roster is too
 * small). */
int acr_gen1_rogue_lineup_pairs(const acr_gen1_rogue_world *w, uint64_t seed,
                                acr_gen1_rogue_pick out[ACR_GEN1_ROGUE_SLOTS][2]);

/* A random starting AC that passes the game's checks and carries a real
 * gun (a hand weapon or weapon arms with attack). 1 = ok. */
int acr_gen1_rogue_start_ac(const acr_gen1_rogue_world *w, uint64_t seed, uint8_t out[ACR_GEN1_SLOT_COUNT]);

/* The attempt's paint (the same for its ghost later). */
void acr_gen1_rogue_paint(uint64_t seed, uint8_t attempt, uint8_t colours[4][3]);

/* A new run. `pilot` / `ac` name the player's AC. 1 = ok. */
int acr_gen1_rogue_new(acr_gen1_rogue *r, const acr_gen1_rogue_world *w, uint64_t seed,
                       const char *pilot, const char *ac);
/* After a loss: the same ladder with the losing AC as a ghost at the rank
 * it held (when it had won at least once), a new random AC at 0 P. */
int acr_gen1_rogue_retry(acr_gen1_rogue *r, const acr_gen1_rogue_world *w);

/* The next opponent's rung (slot wins + 1); NULL when the arena is cleared. */
const acr_gen1_rogue_pick *acr_gen1_rogue_next_pick(const acr_gen1_rogue *r);
/* The opponent behind a rung (a ghost's AI comes from `ai_from`). */
const acr_gen1_rogue_foe *acr_gen1_rogue_find(const acr_gen1_rogue_world *w, uint8_t arena, uint8_t rank);
/* What a rung fights with: build, options, colours, AI, names. 1 = ok. */
int acr_gen1_rogue_rung(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w, const acr_gen1_rogue_pick *p,
                        acr_gen1_rogue_foe *out);

/* A fight's result: 1 = won (the caller then shows the prize screen while
 * acr_gen1_rogue_prize_due), 0 = lost. */
void acr_gen1_rogue_won(acr_gen1_rogue *r);
void acr_gen1_rogue_lost(acr_gen1_rogue *r);

/* Belt (hub background) of a rank: 0 white (rank 20) .. 10 black-gold (rank 1). */
int acr_gen1_rogue_belt(int wins);
const char *acr_gen1_rogue_belt_name(int belt);

/* ---- the prize screen ---- */

enum { ACR_GEN1_OFFER_PART = 0, ACR_GEN1_OFFER_SET, ACR_GEN1_OFFER_HPLUS, ACR_GEN1_OFFER_OPTS,
       ACR_GEN1_OFFER_INCOME };
#define ACR_GEN1_ROGUE_HPLUS_COST  13u
#define ACR_GEN1_ROGUE_OPTS_COST   15u
#define ACR_GEN1_ROGUE_INCOME_COST 6u
#define ACR_GEN1_ROGUE_SET_COST    7u

typedef struct acr_gen1_rogue_offer {
    uint8_t kind;              /* ACR_GEN1_OFFER_* */
    uint8_t cat, index;        /* the part (SET: the arms) */
    uint8_t cost, band;        /* band 0 cheap, 1 mid, 2 expensive */
    uint8_t xn, xcat[2], xindex[2];      /* SET: the hand weapon and blade that come with it */
    uint8_t nput;              /* places it can go (back weapons: left / right) */
    uint8_t put_slot[2];
    uint8_t put_fitted[2][ACR_GEN1_SLOT_COUNT];   /* the AC with it there */
    uint32_t put_lost[2];      /* slots whose part is thrown away (bit = slot) */
    uint32_t weight;           /* draw weight (1000 = normal) */
} acr_gen1_rogue_offer;

/* A prize screen is waiting (prize_wins < wins). */
int acr_gen1_rogue_prize_due(const acr_gen1_rogue *r);
/* The current prize screen's offers (after its re-roll, when used); 0..3. */
int acr_gen1_rogue_offers(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w,
                          acr_gen1_rogue_offer out[3]);
/* Take offer `o`: `put` = place to equip it (0/1), or -1 to save it for
 * later. 1 = taken (P paid, screen done). */
int acr_gen1_rogue_take(acr_gen1_rogue *r, const acr_gen1_rogue_world *w, const acr_gen1_rogue_offer *o, int put);
/* PASS: +1 P, screen done. */
void acr_gen1_rogue_pass(acr_gen1_rogue *r);
/* RE-ROLL: 1 P, once per screen. 1 = done. */
int acr_gen1_rogue_reroll(acr_gen1_rogue *r);
int acr_gen1_rogue_can_reroll(const acr_gen1_rogue *r);

/* ---- SWAP PARTS ---- */

/* Saved part n as an offer (where it can go, what it replaces). 1 = fits now. */
int acr_gen1_rogue_saved_fit(const acr_gen1_rogue *r, const acr_gen1_rogue_world *w, int n,
                             acr_gen1_rogue_offer *out);
/* Equip saved part n at place `put`; what it replaces is gone for good. */
int acr_gen1_rogue_swap(acr_gen1_rogue *r, const acr_gen1_rogue_world *w, int n, int put);

/* The run's own check of the fitted AC (HUMAN PLUS waives the load and
 * energy limits, ALL OPTIONS the option slots); ACR_GEN1_FAIL_* mask. */
unsigned acr_gen1_rogue_check(const acr_gen1_rogue *r, const acr_gen1_catalog *c);

/* ---- the run file (plain text; the garage is a separate save) ---- */
#define ACR_GEN1_ROGUE_FORMAT 1
size_t acr_gen1_rogue_write(const acr_gen1_rogue *r, char *buf, size_t size);
/* Reads the run (not its garage). 1 = ok. */
int acr_gen1_rogue_read(acr_gen1_rogue *r, const char *text);

#endif
