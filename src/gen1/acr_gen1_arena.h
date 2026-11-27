/* The Arena: Master of Arena's ranked ladders (the main arena and the
 * eight Ex Arenas), with their prize money and prize parts. No OS calls:
 * the platform reads the opponents from the import (acr-import's arena/
 * files, the same roster Rogue Arena uses) and keeps the save file.
 *
 * From the AC1Recomp mapping (docs/mapping/arena-prizes.md,
 * menus-screens.md, games/master-of-arena/moa_arena_run_boot.c):
 *  - Arena 0 has 50 places (49 opponents and the player). Ranks 1-15 are
 *    the ranked list, 16-50 the sub arena; the ranked list opens once the
 *    player is at the top of the sub arena (rank 16). (In the game a
 *    beaten sub-arena opponent can't be fought again: see rematches below.)
 *  - A win over anyone above moves the player up one rank (the ranked list
 *    and the Ex Arenas).
 *  - A win pays the opponent's prize money (credits, capped at
 *    99,999,999). Some opponents also give a part (one of 23 prize ids);
 *    the part goes into the garage (+1 copy, +1 more for back weapons).
 *  - Champion (arena 5) and Guest (6) give their part (record 0's) only
 *    once every opponent there has been beaten.
 *  - Ex Arenas 1-4 need the matching legs ("Requires a specific leg type.").
 * Project Phantasma's arena (Armored-CoreR's main one, number 9): 49
 * opponents and the player, one list (no sub arena), its own prize money
 * and nine prize parts (ACG1PLUS docs/research/mapping/content-data.md
 * section 4a). Armored-CoreR's unlocks (user, 2026-11-27; docs/ARENA.md):
 *  - Phantasma's arena is open from the start;
 *  - Master of Arena's main ladder opens once Phantasma's is topped (or at
 *    once when the import has no Phantasma arena);
 *  - Master of Arena's Ex Arenas open as before, from its main ladder.
 * Armored-CoreR's own choices where the mapping is silent (docs/ARENA.md):
 *  - The beaten opponent drops to just below the player; the ones between
 *    move up one (so the sub arena never strands the player). Beating the
 *    one just above means taking its place.
 *  - Rematches (user, 2026-11-25): a beaten ("DEFEATED") opponent below
 *    the player can be fought again, in any arena, for fun only: no rank
 *    change, no money, no part. It still counts in the ARENA REPORT.
 *  - Prize money and prize parts are paid the first time an opponent is
 *    beaten only (user, 2026-11-24).
 *  - Ex Arenas open: 1-4 at main-arena rank 15, 5-7 at main-arena rank 1,
 *    8 (Master) once 5, 6 and 7 have each been topped.
 *  - A draw (both down at once) counts as a loss.
 *  - No match timer in the Arena (user, 2026-11-25): a fight goes on until
 *    one side is down or out of the area, or the player gives up (a loss).
 *    The timer setting below stays in the save file but isn't used.
 *
 * Absent opponents (the menu module's challenge code, FDAT 206 0x800BF43C):
 * after YES to "Do you want to challenge?", an opponent who can't fight
 * (Armored-CoreR: killed in a mission) and hasn't been handled yet gives
 * "Opponent unavailable, default win.": it's marked handled and the player
 * moves up one rank with no fight. Challenging a handled one again says
 * "Opponent is unable to compete." The default win pays nothing (the game
 * pays prizes after a fight; this path skips it). */
#ifndef ACR_GEN1_ARENA_H
#define ACR_GEN1_ARENA_H

#include <stddef.h>
#include <stdint.h>

#include "acr_gen1_rogue.h"

/* Arena numbers: 0-8 are Master of Arena's (0 its main 50-place ladder,
 * 1-8 its Ex Arenas), 9 Project Phantasma's 49 + 1, Armored-CoreR's main
 * Arena (user, 2026-11-27: the normal Arena is Phantasma's; Master of
 * Arena's ladders stay as Ex Arenas). Master of Arena keeps the low numbers
 * so saves and Rogue Arena runs made before stay as they are. */
#define ACR_GEN1_ARENA_COUNT  10
#define ACR_GEN1_ARENA_MOA    0     /* Master of Arena's main ladder (ranked + sub arena) */
#define ACR_GEN1_ARENA_PP     9     /* Project Phantasma's: the main Arena */
#define ACR_GEN1_ARENA_PLACES 50    /* the most places in one arena (arena 0) */
#define ACR_GEN1_ARENA_RANKED 15    /* arena 0: ranks 1-15 ranked, the rest the sub arena */
#define ACR_GEN1_ARENA_YOU    255   /* the player's place in holder[] */
#define ACR_GEN1_ARENA_PRIZES 32    /* Master of Arena's 0-22, Project Phantasma's 0-8 as 23-31 */
#define ACR_GEN1_ARENA_CREDITS_MAX 99999999

/* An opponent's presence (ladder.absent[]). */
enum { ACR_GEN1_ARENA_PRESENT = 0, ACR_GEN1_ARENA_ABSENT = 1, ACR_GEN1_ARENA_ABSENT_DONE = 2 };

/* Leg kinds, numbered as the Ex Arenas that ask for them. */
enum { ACR_GEN1_ARENA_LEGS_NONE = 0, ACR_GEN1_ARENA_LEGS_HUMANOID, ACR_GEN1_ARENA_LEGS_FOUR,
       ACR_GEN1_ARENA_LEGS_REVERSE, ACR_GEN1_ARENA_LEGS_CATERPILLAR };

/* What the platform supplies: the arena roster (with prize_money and
 * prize_part filled), the catalogue, and each leg part's kind. */
typedef struct acr_gen1_arena_world {
    const acr_gen1_rogue_foe *foe;
    int foes;
    const acr_gen1_catalog *cat;
    uint8_t legs_kind[ACR_GEN1_PART_MAX];   /* ACR_GEN1_ARENA_LEGS_* per legs part */
} acr_gen1_arena_world;

/* One arena's standings. Opponents are named by their disc rank (1 = the
 * arena's top on a new game). */
typedef struct acr_gen1_arena_ladder {
    uint8_t  places;                              /* opponents + the player; 0 = not in the import */
    uint8_t  holder[ACR_GEN1_ARENA_PLACES];       /* holder[r - 1]: disc rank at rank r, or ARENA_YOU */
    uint8_t  beaten[ACR_GEN1_ARENA_PLACES];       /* by disc rank - 1: beaten at least once */
    uint8_t  part_given[ACR_GEN1_ARENA_PLACES];   /* by disc rank - 1: its prize part was given */
 uint8_t  absent[ACR_GEN1_ARENA_PLACES];       /* by disc rank - 1: ACR_GEN1_ARENA_ABSENT_* */
    uint8_t  set_given;                           /* Champion / Guest: the all-beaten prize was given */
    uint8_t  best;                                /* the player's best rank (0 = never fought) */
    uint16_t challenges, wins, losses;            /* ARENA REPORT */
} acr_gen1_arena_ladder;

typedef struct acr_gen1_arena {
    acr_gen1_arena_ladder a[ACR_GEN1_ARENA_COUNT];
    uint8_t timer;                                /* ACR_GEN1_ARENA_TIMER_* */
    uint8_t stage;                                /* stage id last picked, 0 = the default */
} acr_gen1_arena;

enum { ACR_GEN1_ARENA_TIMER_2MIN = 0, ACR_GEN1_ARENA_TIMER_5MIN, ACR_GEN1_ARENA_TIMER_NONE,
       ACR_GEN1_ARENA_TIMERS };
/* The round's length in ms; 0 = no time limit. */
int32_t acr_gen1_arena_timer_ms(int timer);
const char *acr_gen1_arena_timer_name(int timer);

const char *acr_gen1_arena_name(int arena);

/* New standings from the roster: every arena the import has, the player
 * at the bottom of each. */
void acr_gen1_arena_new(acr_gen1_arena *s, const acr_gen1_arena_world *w);
/* After reading a save: an arena whose opponents no longer match the
 * import starts over. Returns the arenas that were reset. */
int acr_gen1_arena_sync(acr_gen1_arena *s, const acr_gen1_arena_world *w);

/* The player's rank in arena `a` (places when at the bottom, 0 no data). */
int acr_gen1_arena_rank(const acr_gen1_arena *s, int a);
/* The opponent at rank r: its disc rank, 0 for the player or none. */
int acr_gen1_arena_holder(const acr_gen1_arena *s, int a, int rank);
/* The roster entry for (arena, disc rank). */
const acr_gen1_rogue_foe *acr_gen1_arena_foe(const acr_gen1_arena_world *w, int a, int disc_rank);
/* Master of Arena's main ladder's sub arena: rank > 15. */
int acr_gen1_arena_is_sub(int a, int rank);

/* Why a challenge can or can't happen (the game's messages in comments). */
typedef enum acr_gen1_arena_why {
    ACR_GEN1_ARENA_OK = 0,
    ACR_GEN1_ARENA_NO_DATA,      /* "No Arena data exists." */
    ACR_GEN1_ARENA_LOCKED,       /* "You may not enter this Arena now." */
    ACR_GEN1_ARENA_WRONG_LEGS,   /* "Requires a specific leg type." */
    ACR_GEN1_ARENA_REFERENCE,    /* at or below the player: "Reference Only" */
    ACR_GEN1_ARENA_DONE,         /* the game's "beaten, can't refight" (unused: rematches are allowed) */
    ACR_GEN1_ARENA_SUB_FIRST,    /* ranked list before the sub arena is cleared: the same message */
    ACR_GEN1_ARENA_INCOMPLETE    /* the AC fails the garage's checks: "Your equipment is incomplete." */
} acr_gen1_arena_why;
const char *acr_gen1_arena_why_text(acr_gen1_arena_why why);

/* Whether arena `a` is open (OK, NO_DATA or LOCKED). */
acr_gen1_arena_why acr_gen1_arena_open(const acr_gen1_arena *s, int a);
/* Whether the player can challenge rank `rank` of arena `a`, with legs of
 * kind `legs_kind` and `build_fail` from the garage check (0 = legal). */
acr_gen1_arena_why acr_gen1_arena_can_challenge(const acr_gen1_arena *s, int a, int rank, int legs_kind,
                                                unsigned build_fail);

/* A fight against rank `rank` would be a rematch: an opponent below the
 * player that has been beaten before (no rank change, no prize). */
int acr_gen1_arena_is_rematch(const acr_gen1_arena *s, int a, int rank);
/* The opponent at rank `rank` has been beaten at least once (DEFEATED). */
int acr_gen1_arena_defeated(const acr_gen1_arena *s, int a, int rank);

/* A prize part as a catalogue entry. 1 = it maps to a part (prize 22's
 * part is not in the ten part tables: 0). */
int acr_gen1_arena_prize_part(int prize_id, int *cat, int *index);

/* What a won fight pays. */
typedef struct acr_gen1_arena_payout {
    int     won;
    int     old_rank, new_rank;
    uint32_t money;
    int     prize_id;              /* -1 none */
    int     part_cat, part_index;  /* -1 when prize_id has no part */
    int     set_prize;             /* the part is Champion's / Guest's all-beaten prize */
    int     rematch;               /* a rematch: no rank change, nothing paid */
    int     default_win;           /* the opponent was absent: no fight, no pay */
} acr_gen1_arena_payout;

/* Books a fight against rank `rank` of arena `a` (a challenge or a rematch
 * the player could make): won = 1 or 0 (draws are losses). Moves the ranks
 * (not for a rematch) and fills `out`. 1 = booked. */
int acr_gen1_arena_result(acr_gen1_arena *s, const acr_gen1_arena_world *w, int a, int rank, int won,
                          acr_gen1_arena_payout *out);
/* Pays a booked win into the garage: credits (capped), the part's copies
 * (+1, +1 more for the paired back weapons, capped at ACR_GEN1_OWN_MAX). */
void acr_gen1_arena_pay(acr_gen1_hangar *h, const acr_gen1_arena_payout *p);

/* Marks (arena, disc rank) as unable to fight (missions call this; the
 * ACR_ARENA_ABSENT test switch too). One already handled stays handled.
 * 1 = marked. */
int acr_gen1_arena_set_absent(acr_gen1_arena *s, int a, int disc_rank);
/* The presence of the opponent at rank `rank` (ACR_GEN1_ARENA_*; PRESENT
 * for the player or no one). */
int acr_gen1_arena_absent_at(const acr_gen1_arena *s, int a, int rank);
/* "Opponent unavailable, default win.": books a challenge to an absent
 * opponent at rank `rank` (one the player could challenge) as a win with
 * no money and no part, and marks it handled. 1 = booked. A handled one
 * ("Opponent is unable to compete.") can't be fought, not even as a
 * rematch: acr_gen1_arena_result refuses absent opponents. */
int acr_gen1_arena_default_win(acr_gen1_arena *s, int a, int rank, acr_gen1_arena_payout *out);

/* ---- save file (plain text, arena.toml) ---- */
#define ACR_GEN1_ARENA_FORMAT 1
size_t acr_gen1_arena_write(const acr_gen1_arena *s, char *buf, size_t size);
/* 1 = ok; *s is left as it was on failure. */
int acr_gen1_arena_read(acr_gen1_arena *s, const char *text);

#endif
