/* Gen 1 computer pilot, first version (docs/AI.md).
 *
 * The AI flies an AC exactly as a player does: every step it fills in the
 * same controller input (acr_gen1_input) and buttons (acr_gen1_actions) a
 * pad produces, and the battle applies the Gen 1 rules to them. So an AI AC
 * keeps every Gen 1 quirk, and online play can send an AI's buttons like a
 * player's.
 *
 * It sees the battle only through acr_gen1_ai_view (what its own AC and
 * FCS would tell a pilot), and it is deterministic: integer maths, its own
 * random numbers from a seed, timers in milliseconds of battle time.
 *
 * Personality comes from the game's own AI editor (RANKER MK AI in Master
 * of Arena's menus): six sliders, 0..100, and the five presets of the
 * AC1Recomp AI cards. acr_gen1_ai_record writes the 36-byte record the
 * game's arena engine reads, exactly as the editor does, so the game's own
 * scoring rules (arena engine 0x80098890, not yet mapped) can later be
 * driven by the same profile. Until then the brain here is our own, tuned
 * by the same sliders. */
#ifndef ACR_GEN1_AI_H
#define ACR_GEN1_AI_H

#include <stdint.h>

#include "acr_gen1_battle.h"

/* The editor's rows, in its order (REBEL POINT, the Human PLUS level, is
 * kept apart). Range: 0 short, 50 mid, 100 long. */
enum {
    ACR_GEN1_AI_SHOOT = 0, ACR_GEN1_AI_BLADE, ACR_GEN1_AI_LOCK, ACR_GEN1_AI_AVOID,
    ACR_GEN1_AI_FLY, ACR_GEN1_AI_RANGE,
    ACR_GEN1_AI_SLIDERS
};

/* The AC1Recomp AI card presets (Raven Card Builder). */
enum {
    ACR_GEN1_AI_CUSTOM = 0, ACR_GEN1_AI_BALANCED, ACR_GEN1_AI_AGGRESSOR,
    ACR_GEN1_AI_SNIPER, ACR_GEN1_AI_DUELIST, ACR_GEN1_AI_EVASIVE,
    ACR_GEN1_AI_PRESETS
};

#define ACR_GEN1_AI_RECORD_SIZE 36

typedef struct acr_gen1_ai_profile {
    uint8_t preset;                        /* ACR_GEN1_AI_*; CUSTOM = own sliders */
    uint8_t slider[ACR_GEN1_AI_SLIDERS];   /* 0..100 */
    uint8_t rebel;                         /* Human PLUS level 0..9 (record byte 0) */
    /* How quick and accurate the pilot is, 0..100 (not in the game's
     * editor). It changes reaction time and aim, never the AC. */
    uint8_t skill;
} acr_gen1_ai_profile;

/* "BALANCED" ...; "CUSTOM" for 0 or anything unknown. */
const char *acr_gen1_ai_preset_name(int preset);
/* A preset's profile (skill 50). Unknown presets give BALANCED. */
void acr_gen1_ai_preset(acr_gen1_ai_profile *p, int preset);
/* Reads "key = value" lines (TOML style; # starts a comment):
 * preset = "sniper", shoot, blade, lock, avoid, fly, range, rebel, skill.
 * A preset line sets every slider first; later lines adjust. Unknown keys
 * are skipped. Returns 1 if anything was read. */
int acr_gen1_ai_parse(acr_gen1_ai_profile *p, const char *text);
/* Writes the profile in the same form (at most `size` bytes, terminated);
 * returns the length. */
int acr_gen1_ai_write(const acr_gen1_ai_profile *p, char *out, int size);
/* The 36-byte AI record of the arena engine (0x8004AC80), from the game's
 * default record with the sliders written as the editor writes them (menu
 * 0x8007FBC4..0x8007FCC8). */
void acr_gen1_ai_record(const acr_gen1_ai_profile *p, uint8_t record[ACR_GEN1_AI_RECORD_SIZE]);

/* What the pilot can see. World units (y up is negative), facing 0..4095. */
typedef struct acr_gen1_ai_view {
    /* Own AC */
    int32_t  x, y, z;
    uint16_t yaw;
    int16_t  pitch, turn_rate;
    uint16_t turn_accel;
    uint8_t  state;                        /* ACR_GEN1_GROUND ... */
    int32_t  speed;                        /* across the ground, 1/64 units a tick */
    int32_t  energy, energy_max;
    uint8_t  drained;
    int32_t  ap, ap_max;
    uint8_t  legs, human_plus, kneeling;
    /* Weapons (blade included) */
    uint8_t  weapons, current;
    uint8_t  kind[ACR_GEN1_MAX_WEAPONS];
    uint16_t ammo[ACR_GEN1_MAX_WEAPONS];   /* 0xFFFF = unlimited */
    int32_t  range[ACR_GEN1_MAX_WEAPONS];
    uint8_t  stance[ACR_GEN1_MAX_WEAPONS]; /* needs a steady stance (back cannons) */
    uint8_t  max_locks[ACR_GEN1_MAX_WEAPONS];
    uint16_t blade_drain;
    int8_t   blade;                        /* index of the blade, -1 none */
    uint8_t  ready;                        /* the current weapon can fire now */
    uint8_t  in_box;                       /* the target is in the current weapon's lock box */
    uint8_t  locked, locks, volley, swinging;
    /* Target */
    uint8_t  target_alive;
    int32_t  dist;                         /* across the ground */
    int16_t  bearing;                      /* off the facing, + = to the right */
    int16_t  elevation;                    /* + = above the view's level */
    int32_t  target_height;                /* how high the target is off the ground */
    uint8_t  target_locked;                /* it has a lock on us */
    uint8_t  target_swinging;
    uint8_t  missiles_in;                  /* its missiles in flight toward us */
    /* Arena: half its width (0 = no walls) and where we stand in it, as
     * offsets ahead and to the right of the facing toward the centre. */
    int32_t  arena_half;
    int32_t  centre_ahead, centre_side;
} acr_gen1_ai_view;

/* What fighter `me` sees of battle `b` in a square arena of half-width
 * `arena_half` (world units, centred on 0, 0; 0 = open). */
void acr_gen1_ai_look(const acr_gen1_battle *b, int me, int32_t arena_half, acr_gen1_ai_view *v);

typedef struct acr_gen1_ai {
    acr_gen1_ai_profile prof;
    uint32_t rng;
    int32_t  think_ms;        /* until the next decision */
    /* The current plan, held between decisions */
    int8_t   move, strafe;
    int32_t  strafe_ms;       /* until the strafe direction may change */
    uint8_t  boost;           /* holding the booster */
    int32_t  boost_ms;        /* how much longer the boost burst lasts */
    uint8_t  hop;             /* this burst is a jump into the air */
    uint8_t  fire;
    uint8_t  want_locks;      /* missiles: volley at this many locks */
    int8_t   want_weapon;
    int32_t  switch_ms;       /* wait after a weapon change */
    int32_t  blade_ms;        /* wait before the next blade press */
    int32_t  plant_ms;        /* committed to a cannon stance for this long */
    int16_t  aim_error;       /* facing error held between decisions */
    int32_t  last_ap;
    int32_t  hurt_ms;         /* since the latest hit taken */
    uint8_t  prev_boost;
} acr_gen1_ai;

/* Ready to fly: a profile and a seed (the same seed replays the same match). */
void acr_gen1_ai_init(acr_gen1_ai *ai, const acr_gen1_ai_profile *p, uint32_t seed);

/* One engine step of `step_ms`: the pilot's input and buttons for it. */
void acr_gen1_ai_think(acr_gen1_ai *ai, const acr_gen1_ai_view *v, int step_ms,
                       acr_gen1_input *in, acr_gen1_actions *act);

/* acr_gen1_ai_look then acr_gen1_ai_think, with the battle's step length. */
void acr_gen1_ai_step(acr_gen1_ai *ai, const acr_gen1_battle *b, int me, int32_t arena_half,
                      acr_gen1_input *in, acr_gen1_actions *act);

#endif
