/* Gen 1 missions as the MISSIONS screens see them: the mission list (Master
 * of Arena's from acr-import's missions.toml, and Armored-CoreR's own), the
 * sortie check, the income and expense report after a sortie, and the
 * record of missions played, kept in the garage's save file.
 *
 * The rules are Master of Arena's, read from the disc (AC1Recomp
 * docs/mapping/missions.md and menus-screens.md; the report from the
 * missions engine, FDAT 207 0x800918E8, read 2026-11-25):
 *
 * - Sortie (menu 0x800967C4): three checks in this order, each with its
 *   own message: 0x80096288 "Your equipment is incomplete." (head, core,
 *   arms, legs, generator or FCS missing, or no booster on legs that
 *   aren't caterpillars), 0x8009632C "You exceed the weight limit." (the
 *   arms and what they hold over the core's arm limit, or the weight over
 *   the legs' limit), 0x800965B8 "You don't have enough energy.". Option
 *   slots are not checked.
 * - At launch (0x800976E0) the menu adds up the price of every fitted part
 *   but the options (0x8003EC48): the repair bill is figured from it.
 * - The report (0x800918E8), on the picture RTIM 63 or 64 after a success
 *   and 65 after a failure:
 *     Income   Reward             the reward, success only
 *              Special addition   mission tallies (success only)
 *     Expense  Ammunition         - sum over solid weapons (damage flag
 *                                   0x10) of rounds used x ammo price
 *              A.C. repair        - (AP max - AP left) x (AC price / 100) / 8000
 *              Special deduction  mission tallies
 *     Balance                     the sum of the five
 *     Total credits               credits + balance, kept within
 *                                 -999,999 .. 9,999,999
 *   Every division rounds down. The two "special" rows come from a
 *   mission's own code (hook +0x0C) and its breakable structures (the
 *   tallies at 0x801F8E94 / +4); missions without them pay 0.
 * - Record (0x80097660 and 0x80093CD8): sorties, successes and failures,
 *   each capped at 999, and per mission completed / failed.
 *
 * No operating-system calls: the platform reads the import's file and
 * keeps the save. */
#ifndef ACR_GEN1_MISSIONS_H
#define ACR_GEN1_MISSIONS_H

#include <stddef.h>
#include <stdint.h>

#include "acr_gen1_garage.h"

struct acr_gen1_fighter;

#define ACR_GEN1_MISSIONS_MAX   24
#define ACR_GEN1_MISSION_PAGES  8
#define ACR_GEN1_MISSION_TEXT   400   /* bytes per page (the disc's longest is under 200) */
#define ACR_GEN1_MISSION_ID     12

typedef struct acr_gen1_mission_info {
    char     id[ACR_GEN1_MISSION_ID];   /* "MOA-00".."MOA-19", or Armored-CoreR's own ("P4") */
    char     name[40];
    char     sender[40], requester[40]; /* "" = none on the briefing */
    int32_t  reward;
    int16_t  map;                       /* mission map 0-19 */
    uint8_t  own;                       /* Armored-CoreR's own mission (not from the disc) */
    uint8_t  playable;                  /* the engine can run it today */
    int32_t  time_ms;                   /* own missions: the time limit; 0 none */
    uint8_t  pages, pictures;
    /* Text pages with the disc's markup: "@1".."@0" highlight, "%n" picture n. */
    char     page[ACR_GEN1_MISSION_PAGES][ACR_GEN1_MISSION_TEXT];
} acr_gen1_mission_info;

typedef struct acr_gen1_mission_list {
    acr_gen1_mission_info m[ACR_GEN1_MISSIONS_MAX];
    int count;
} acr_gen1_mission_list;

/* The list as the MISSIONS screen shows it: Armored-CoreR's own playable
 * missions first, then Master of Arena's from the import's missions.toml
 * (`toml` may be NULL: none imported). Returns how many MoA missions were
 * read. */
int acr_gen1_missions_build(acr_gen1_mission_list *list, const char *toml);

/* ---- sortie check ---- */
typedef enum acr_gen1_sortie {
    ACR_GEN1_SORTIE_OK,
    ACR_GEN1_SORTIE_INCOMPLETE,
    ACR_GEN1_SORTIE_WEIGHT,
    ACR_GEN1_SORTIE_ENERGY
} acr_gen1_sortie;
/* From an ACR_GEN1_FAIL_* mask (acr_gen1_garage_check), in the game's order. */
acr_gen1_sortie acr_gen1_sortie_from_fail(unsigned fail);
const char *acr_gen1_sortie_message(acr_gen1_sortie s);   /* the game's words; NULL for OK */

/* ---- report ---- */
/* The price of every fitted part but the options (0x8003EC48). */
int32_t acr_gen1_mission_ac_price(const acr_gen1_hangar *h, const acr_gen1_catalog *c);
/* Ammunition used by a fighter: rounds fired x ammo price, solid weapons only. */
int32_t acr_gen1_mission_ammo_cost(const struct acr_gen1_fighter *f);
/* The repair bill: (AP max - AP left) x (price / 100) / 8000. */
int32_t acr_gen1_mission_repair_cost(int32_t ap_max, int32_t ap_left, int32_t ac_price);

typedef struct acr_gen1_mission_report {
    int32_t reward, addition, ammo, repair, deduction;  /* ammo, repair, deduction <= 0 */
    int32_t balance, credits;                           /* credits: the total after */
    uint8_t success;
} acr_gen1_mission_report;

#define ACR_GEN1_CREDITS_TOP    9999999
#define ACR_GEN1_CREDITS_BOTTOM (-999999)

/* Fills the report and pays it into the garage. Costs are passed as
 * positive amounts; `addition` / `deduction` are the mission's specials
 * (deduction positive too). */
void acr_gen1_mission_settle(acr_gen1_hangar *h, acr_gen1_mission_report *r, int success, int32_t reward,
                             int32_t addition, int32_t ammo_cost, int32_t repair_cost, int32_t deduction);

/* ---- record of missions played ---- */
#define ACR_GEN1_MISSION_RECORD_MAX 32
#define ACR_GEN1_MISSION_COUNT_CAP  999
enum { ACR_GEN1_MISSION_DONE_BIT = 2, ACR_GEN1_MISSION_FAILED_BIT = 4 };   /* the game's T+20 bits */

typedef struct acr_gen1_mission_record {
    uint16_t sorties, successes, failures;
    uint8_t  n;
    char     id[ACR_GEN1_MISSION_RECORD_MAX][ACR_GEN1_MISSION_ID];
    uint8_t  flags[ACR_GEN1_MISSION_RECORD_MAX];   /* ACR_GEN1_MISSION_*_BIT */
} acr_gen1_mission_record;

/* A sortie of mission `id` ended. */
void acr_gen1_mission_book(acr_gen1_mission_record *rec, const char *id, int success);
/* The mission's flags (0 = never played). */
unsigned acr_gen1_mission_flags(const acr_gen1_mission_record *rec, const char *id);

/* The save file's [missions] section (appended to acr_gen1_save_write's
 * text; acr_gen1_save_read skips it). Returns the length it needs. */
size_t acr_gen1_mission_record_write(const acr_gen1_mission_record *rec, char *buf, size_t size);
/* Reads the [missions] section of a save; no section = an empty record. */
void acr_gen1_mission_record_read(acr_gen1_mission_record *rec, const char *save_text);

#endif
