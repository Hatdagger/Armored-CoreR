/* Gen 1 fighting: lock-on, weapons, shots and damage for two ACs.
 * Integer only and stepped with the movement, so every machine agrees.
 * Numbers and what is still a stand-in: docs/research/gen1-weapons.md. */
#ifndef ACR_GEN1_BATTLE_H
#define ACR_GEN1_BATTLE_H

#include <stdint.h>

#include "acr_gen1_sim.h"

enum { ACR_GEN1_GUN, ACR_GEN1_MISSILE, ACR_GEN1_BLADE };

typedef struct acr_gen1_weapon {
    uint8_t  kind;
    char     name[20];
    uint16_t attack;
    uint16_t ammo;          /* 0 = unlimited (blade) */
    int32_t  range;         /* world units, already capped by the FCS */
    uint8_t  energy;        /* energy damage (else shell) */
    uint16_t lock_ms;       /* FCS lock time for this weapon's class; 0 = no lock */
    uint16_t box_w, box_h;  /* FCS lock box, in the original's screen units */
    uint16_t interval_ms;   /* time between shots */
    int32_t  speed;         /* shot speed (missiles: top speed), world units per Gen 1 step */
    uint16_t homing;        /* missiles: push toward the target each Gen 1 step (VS engine 0x80088BF8) */
    uint8_t  stance;        /* back cannons: needs a steady stance to fire (see below) */
} acr_gen1_weapon;

/* Legs, for the cannon stance rule: two legs (humanoid, reverse joint) must
 * kneel to fire a cannon, four legs only fire them on the ground,
 * caterpillars fire them freely. Human PLUS lifts the rule. */
enum { ACR_GEN1_LEGS_TWO, ACR_GEN1_LEGS_FOUR, ACR_GEN1_LEGS_TRACKS };
#define ACR_GEN1_KNEEL_MS 400   /* time to kneel before a cannon fires (stand-in) */

#define ACR_GEN1_MAX_WEAPONS 3
#define ACR_GEN1_MAX_SHOTS   64

typedef struct acr_gen1_fighter {
    acr_gen1_ac ac;
    const acr_gen1_ac_def *def;
    int32_t  ap, ap_max;
    uint8_t  shell_factor, energy_factor;     /* damage taken, 1..45 */
    acr_gen1_weapon weapon[ACR_GEN1_MAX_WEAPONS];
    uint8_t  weapons, current;
    uint16_t ammo[ACR_GEN1_MAX_WEAPONS];
    int32_t  cooldown_ms;
    int32_t  lock_ms;                          /* time the target has been in the box */
    uint8_t  locked;
    uint16_t swing;                            /* blade swing steps left, for drawing */
    uint8_t  legs;                             /* ACR_GEN1_LEGS_* */
    uint8_t  human_plus;
    uint8_t  kneeling;
    int32_t  kneel_ms;
} acr_gen1_fighter;

typedef struct acr_gen1_shot {
    uint8_t  active, owner, weapon;
    int32_t  x, y, z;                          /* 1/64 world units */
    int32_t  vx, vy, vz;
    int32_t  travelled;                        /* world units */
} acr_gen1_shot;

typedef struct acr_gen1_battle {
    acr_gen1_fighter f[2];
    acr_gen1_shot shot[ACR_GEN1_MAX_SHOTS];
    uint16_t step_ms;                          /* length of a step, for times in ms */
    /* Shots fly at the original's speed per second whatever the step rate:
     * per step they move speed x gen1_rate / step_rate (0 / 0 = Gen 1). */
    uint16_t step_rate, gen1_rate;
} acr_gen1_battle;

typedef struct acr_gen1_actions {
    uint8_t fire;            /* held */
    uint8_t next_weapon;     /* pressed this step */
} acr_gen1_actions;

/* Damage factor from the summed head, core, arms and legs defense
 * (menu module 0x800A83D8), with or without the matching screen option. */
uint8_t acr_gen1_damage_factor(uint32_t defense, int energy, int screen);
/* AP a hit takes (VS engine 0x80079500). */
int32_t acr_gen1_hit_ap(uint16_t attack, uint8_t factor);

void acr_gen1_battle_step(acr_gen1_battle *b, const acr_gen1_input in[2], const acr_gen1_actions act[2]);

/* Can the fighter fire its current weapon in its present stance? */
int acr_gen1_stance_ok(const acr_gen1_fighter *f);

/* Is the other AC inside fighter i's lock box for weapon w (and in range)? */
int acr_gen1_in_lock_box(const acr_gen1_battle *b, int i, const acr_gen1_weapon *w);

#endif
