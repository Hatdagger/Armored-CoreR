/* Loads one Gen 1 AC for the test build from an acr-import output folder:
 * the parts' stats (parts/...) and the legs' root motion (motion/...). */
#ifndef DEMO_DATA_H
#define DEMO_DATA_H

#include "../../gen1/acr_gen1_battle.h"
#include "../../gen1/acr_gen1_garage.h"

#define DEMO_MAX_KEYS 4096

/* Garage order, Master of Arena indices; 255 = none. */
enum { SLOT_CORE, SLOT_BOOSTER, SLOT_HEAD, SLOT_ARMS, SLOT_BLADE, SLOT_HAND,
       SLOT_BACK_L, SLOT_BACK_R, SLOT_GEN, SLOT_FCS, SLOT_LEGS, SLOT_COUNT };

typedef struct demo_ac {
    acr_gen1_ac_def def;
    char names[SLOT_COUNT][24];
    int model;
    acr_gen1_root_track tracks[ACR_GEN1_ANIM_COUNT];
    acr_gen1_root_key keys[DEMO_MAX_KEYS];
    /* Fighting: AP, damage factors and the weapons (hand, back left, blade). */
    int32_t ap;
    uint8_t shell_factor, energy_factor;
    acr_gen1_weapon weapon[ACR_GEN1_MAX_WEAPONS];
    int weapons;
    int plus_applied;      /* Human PLUS energy rules applied to def.stats */
} demo_ac;

/* 1 on success; otherwise 0 and a message for the player in `err`. */
int demo_load(const char *import_dir, const int parts[SLOT_COUNT], demo_ac *out,
              char *err, int err_size);

/* The shop catalogue from the import's part files: every part's price,
 * fitting flags and stats, and its name in `names` (may be NULL). 0 when
 * the import has no parts. */
int demo_catalog(const char *import_dir, acr_gen1_catalog *cat,
                 char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]);

#endif
