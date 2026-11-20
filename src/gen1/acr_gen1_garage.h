/* Gen 1 garage and shop: what a player owns, what their AC is built from,
 * buying and selling, fitting parts under the game's rules, and a readable
 * save file. No operating-system calls: the platform reads and writes the
 * file and fills the catalogue from acr-import's part files.
 *
 * Economy as in the PS1 games: parts sell back for what they cost, and
 * while the balance is below zero nothing can be bought or sold. A new game
 * starts with the starting AC's parts and a starting balance. Parts sold to
 * the shop stay in its stock and can be bought back, even parts it does not
 * otherwise sell (the test builds start the player with every part and an
 * empty shop this way). */
#ifndef ACR_GEN1_GARAGE_H
#define ACR_GEN1_GARAGE_H

#include <stddef.h>
#include <stdint.h>

#include "acr_gen1_fit.h"

#define ACR_GEN1_PART_MAX   64    /* part numbers per category */
#define ACR_GEN1_NO_PART    255
#define ACR_GEN1_OWN_MAX    99    /* copies of one part */
#define ACR_GEN1_OPTION_MAX 16

/* Shop categories. Both back slots use the back table. */
typedef enum acr_gen1_category {
    ACR_GEN1_CAT_CORE, ACR_GEN1_CAT_BOOSTER, ACR_GEN1_CAT_HEAD, ACR_GEN1_CAT_ARMS,
    ACR_GEN1_CAT_BLADE, ACR_GEN1_CAT_HAND, ACR_GEN1_CAT_BACK, ACR_GEN1_CAT_GENERATOR,
    ACR_GEN1_CAT_FCS, ACR_GEN1_CAT_LEGS, ACR_GEN1_CAT_OPTION,
    ACR_GEN1_CAT_COUNT
} acr_gen1_category;

acr_gen1_category acr_gen1_slot_category(acr_gen1_slot slot);

/* One part as the shop and the fitting rules see it. */
typedef struct acr_gen1_catalog_part {
    uint8_t  present;      /* in the game's part table */
    uint8_t  for_sale;     /* the shop sells it */
    uint8_t  slots;        /* core: option slots it has; option: slots it uses */
    uint16_t flags;        /* ACR_GEN1_FIT_* */
    uint32_t price;
    acr_gen1_part stats;   /* fitting and compile stats */
} acr_gen1_catalog_part;

typedef struct acr_gen1_catalog {
    acr_gen1_catalog_part part[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX];
    uint8_t sell_percent;  /* of the price paid back on a sale; 100 in the game */
} acr_gen1_catalog;

/* A player's garage. */
typedef struct acr_gen1_hangar {
    int32_t  credits;
    uint8_t  owned[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX];   /* copies, fitted ones included */
    uint8_t  fitted[ACR_GEN1_SLOT_COUNT];                    /* part number or ACR_GEN1_NO_PART */
    uint16_t options;                                        /* bit n = option n fitted */
    char     ac_name[20], pilot_name[20];
    uint8_t  colours[4][3];                                  /* 0..63 per channel */
    uint8_t  stock[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX];   /* sold to the shop, can be bought back */
} acr_gen1_hangar;

/* New game: owns one of each starting part (options too) with them fitted,
 * and one SP-MODERN (not fitted). */
void acr_gen1_hangar_new(acr_gen1_hangar *h, const uint8_t start[ACR_GEN1_SLOT_COUNT],
                         uint16_t start_options, int32_t credits);

/* Adds Armored-CoreR's own parts (not on the disc) to a catalogue read
 * from acr-import: SP-MODERN, for sale from the start at no cost. `name`
 * gets its shop name (may be NULL). */
void acr_gen1_catalog_add_own_parts(acr_gen1_catalog *c, char name[24]);

/* Test builds: own (at least) one of every part in the catalogue, options too. */
void acr_gen1_hangar_give_all(acr_gen1_hangar *h, const acr_gen1_catalog *c);

/* The shop offers a part: it sells it, or holds copies sold to it. */
int acr_gen1_shop_offers(const acr_gen1_hangar *h, const acr_gen1_catalog *c,
                         acr_gen1_category cat, unsigned index);

/* Copies of a part fitted to the AC (0..2; options 0..1). */
unsigned acr_gen1_fitted_copies(const acr_gen1_hangar *h, acr_gen1_category cat, unsigned index);
/* Copies free to fit or sell. */
unsigned acr_gen1_spare_copies(const acr_gen1_hangar *h, acr_gen1_category cat, unsigned index);

typedef enum acr_gen1_result {
    ACR_GEN1_OK,
    ACR_GEN1_NOT_IN_SHOP,      /* no such part, or not sold */
    ACR_GEN1_NO_CREDITS,
    ACR_GEN1_IN_DEBT,          /* balance below zero: no buying or selling */
    ACR_GEN1_STOCK_FULL,       /* ACR_GEN1_OWN_MAX copies already */
    ACR_GEN1_NONE_SPARE,       /* every copy is fitted (or none owned) */
    ACR_GEN1_ALREADY_FITTED,
    ACR_GEN1_REQUIRED,         /* head, core, arms, legs, generator and FCS can't be left empty */
    ACR_GEN1_NOT_ALLOWED,      /* the fitting rules refuse it with the current parts */
    ACR_GEN1_NO_OPTION_SLOTS
} acr_gen1_result;

acr_gen1_result acr_gen1_shop_buy(acr_gen1_hangar *h, const acr_gen1_catalog *c,
                                  acr_gen1_category cat, unsigned index);
acr_gen1_result acr_gen1_shop_sell(acr_gen1_hangar *h, const acr_gen1_catalog *c,
                                   acr_gen1_category cat, unsigned index);

/* Fits part `index` into `slot` (ACR_GEN1_NO_PART empties an optional slot).
 * Parts the new one can't be used with come off, as in the game; `removed`
 * gets a mask of those slots (bit = acr_gen1_slot). A part that the current
 * build rules out (a hand weapon on weapon arms, a booster on caterpillars)
 * is refused instead. */
acr_gen1_result acr_gen1_garage_fit(acr_gen1_hangar *h, const acr_gen1_catalog *c,
                                    acr_gen1_slot slot, unsigned index, uint32_t *removed);

/* Fits or removes option `bit`. Fitting needs a spare copy and free core
 * slots; SP-MODERN takes every other option off instead (it uses all the
 * slots), and while it is fitted no other option fits. Exception to come:
 * the player's Human PLUS option part (not added yet) goes on with
 * SP-MODERN and stays on when SP-MODERN is fitted (docs/MODERN-CONTROLS.md). */
acr_gen1_result acr_gen1_garage_option(acr_gen1_hangar *h, const acr_gen1_catalog *c,
                                       unsigned bit, int on);

/* The fitted parts as the compile and fitting code take them. */
void acr_gen1_garage_build(const acr_gen1_hangar *h, const acr_gen1_catalog *c, acr_gen1_fit_build *out);

/* VS START / AC TEST check of the fitted build (ACR_GEN1_FAIL_* mask; 0 = legal). */
unsigned acr_gen1_garage_check(const acr_gen1_hangar *h, const acr_gen1_catalog *c,
                               acr_gen1_fit_numbers *out);

/* ---- save file ----
 * Plain text in the same style as acr-import's part files. `format` is the
 * file's version; readers upgrade older formats. */
#define ACR_GEN1_SAVE_FORMAT 2   /* 2: the shop's stock ([shop]) */

/* Writes the save text into buf (always zero-terminated when size > 0).
 * Returns the length it needs, not counting the zero. */
size_t acr_gen1_save_write(const acr_gen1_hangar *h, char *buf, size_t size);

/* Reads a save; 1 on success. On failure *h is left as it was. */
int acr_gen1_save_read(acr_gen1_hangar *h, const char *text);

#endif
