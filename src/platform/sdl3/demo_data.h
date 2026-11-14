/* Loads one Gen 1 AC for the test build from an acr-import output folder:
 * the parts' stats (parts/...) and the legs' root motion (motion/...). */
#ifndef DEMO_DATA_H
#define DEMO_DATA_H

#include "../../gen1/acr_gen1_sim.h"

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
} demo_ac;

/* 1 on success; otherwise 0 and a message for the player in `err`. */
int demo_load(const char *import_dir, const int parts[SLOT_COUNT], demo_ac *out,
              char *err, int err_size);

#endif
