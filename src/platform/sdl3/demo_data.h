/* Loads one Gen 1 AC for the test build from an acr-import output folder:
 * the parts' stats (parts/...) and the legs' root motion (motion/...). */
#ifndef DEMO_DATA_H
#define DEMO_DATA_H

#include "../../gen1/acr_gen1_battle.h"
#include "../../gen1/acr_gen1_garage.h"
#include "../../gen1/acr_gen1_rogue.h"
#include "fx.h"

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
    char weapon_type[ACR_GEN1_MAX_WEAPONS][24];   /* the HUD's name for it: RIFLE, SMALL MISSILE, ... */
    fx_look look[ACR_GEN1_MAX_WEAPONS];           /* how its shots are drawn (presentation only) */
    int32_t radar_range;   /* head or back radar unit's range; 0 = no radar */
    int plus_applied;      /* Human PLUS energy rules applied to def.stats */
} demo_ac;

/* 1 on success; otherwise 0 and a message for the player in `err`. */
int demo_load(const char *import_dir, const int parts[SLOT_COUNT], demo_ac *out,
              char *err, int err_size);

/* The animation set (model) a build is drawn with; -1 = none in the import. */
int demo_pick_model(const char *import_dir, const int parts[SLOT_COUNT]);

/* The shop catalogue from the import's part files: every part's price,
 * fitting flags and stats, and its name in `names` (may be NULL). 0 when
 * the import has no parts. */
int demo_catalog(const char *import_dir, acr_gen1_catalog *cat,
                 char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]);

/* Rogue Arena: each part's attack and prize flags (radar, stealth unit,
 * cannon stance) from the import's part files. */
int demo_rogue_parts(const char *import_dir, acr_gen1_rogue_parts *info);
/* The arena opponents acr-import wrote to <import>/arena (none in older
 * imports); `names` marks KARASAWA users. Returns how many. */
int demo_rogue_roster(const char *import_dir, acr_gen1_rogue_foe *out, int max,
                      char names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24]);

/* PART SPECS (Rogue Arena): each part's numbers as the import's part files
 * have them, its type, and the shop's text from the disc (unpacked MIS.T;
 * options: their own description). */
enum { DS_WEIGHT, DS_DRAIN, DS_OUTPUT, DS_AP, DS_DEF_S, DS_DEF_E, DS_RADAR, DS_RADAR_R, DS_COMPUTER,
       DS_ARM_LOAD, DS_SLOTS, DS_LOAD, DS_SPEED, DS_TURN, DS_STAB, DS_POWER, DS_BOOST_D, DS_CAP, DS_RED,
       DS_LOCKS, DS_AREA, DS_ATTACK, DS_AMMO, DS_RANGE, DS_RELOAD, DS_EPS, DS_SALVO, DS_KIND, DS_AMMO_FLAGS,
       DS_IMPACT, DS_RADAR_FLAGS, DS_COUNT };
#define DEMO_SPEC_NONE (-2147483647 - 1)
typedef struct demo_part_specs {
    uint8_t present[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX];
    int32_t v[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][DS_COUNT];   /* DEMO_SPEC_NONE = not in the file */
    char type[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24];
    char text[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][3][76];
    uint8_t text_n[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX];
} demo_part_specs;
int demo_part_specs_load(const char *import_dir, demo_part_specs *out);

#endif
