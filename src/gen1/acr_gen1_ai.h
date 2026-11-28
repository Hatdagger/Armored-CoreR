/* Gen 1 computer pilots (docs/AI.md). Two brains share this interface:
 *
 * - ORIGINAL: a port of the arena AI of Project Phantasma / Master of Arena
 *   (ACG1PLUS docs/research/mapping/ai.md), run from each opponent's own AI
 *   record, in Phantasma style or Master of Arena style (acr_gen1_ai_orig.c).
 * - ACR UTILITY (HARD): the first, custom brain of this project, kept as it
 *   was for later Rogue Arena bonus fights (the rest of this comment and
 *   acr_gen1_ai.c).
 *
 * Which one flies is chosen per fight (acr_gen1_ai_profile.brain) or for
 * everything at once (acr_gen1_ai_set_options, the SETTINGS > AI page).
 *
 * The utility brain, first version:
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

/* Brains and styles. */
enum { ACR_GEN1_BRAIN_DEFAULT = 0, ACR_GEN1_BRAIN_ORIGINAL, ACR_GEN1_BRAIN_UTILITY, ACR_GEN1_BRAINS };
enum { ACR_GEN1_STYLE_NATIVE = 0, ACR_GEN1_STYLE_PP, ACR_GEN1_STYLE_MOA, ACR_GEN1_STYLES };

#define ACR_GEN1_AI_PP_RECORD_SIZE 60
#define ACR_GEN1_AI_ORIG_TUNES 7    /* behaviours 4, 5, 6, 7, 12, 13, 14 */

/* An opponent's AI record from the disc, in one shape for both games
 * (ai.md section 3). Percent fields are 0..100 as on the disc. */
typedef struct acr_gen1_ai_orig_record {
    uint8_t  present;          /* 0: none (the original brain makes one from the sliders) */
    uint8_t  disc;             /* ACR_GEN1_STYLE_PP or ACR_GEN1_STYLE_MOA: whose record it is */
    uint8_t  human_plus;       /* +0x00 */
    uint8_t  desire[3];        /* +0x01..03: A, B (ground), C (air) */
    uint8_t  reroll;           /* +0x04: desires re-rolled with chance 1/(22 v) a frame, 0 never */
    uint8_t  spread[3];        /* +0x05..07 */
    uint8_t  unk8;             /* +0x08: used by behaviour 6's fit */
    uint8_t  blade;            /* +0x09: blade lunge chance */
    uint8_t  gate;             /* +0x0A: frame gate of the long-range blade rule */
    uint8_t  tracking;         /* PP +0x10 / MoA +0x0B: 0 = never loses track */
    uint8_t  delay;            /* PP +0x11 / MoA +0x0C: reaction delay, frames (0..30) */
    uint8_t  tune[ACR_GEN1_AI_ORIG_TUNES][6];   /* base %, blend %, commit x22 frames, whim v, whim factor u16 % */
    /* Master of Arena only */
    uint8_t  fire;             /* +0x0D fire chance % (record value, "shoot" + 1) */
    uint8_t  avoid;            /* +0x0E shot-dodge chance % */
    uint8_t  desperate;        /* +0x0F desperation below this share of AP, % (0 = never) */
    /* Who it belongs to: the opponent number (= starting rank - 1, ai.md 3a)
     * and, for Master of Arena, whether it is in the main arena (fire hold). */
    uint8_t  opponent;
    uint8_t  main_arena;
} acr_gen1_ai_orig_record;

typedef struct acr_gen1_ai_profile {
    uint8_t preset;                        /* ACR_GEN1_AI_*; CUSTOM = own sliders */
    uint8_t slider[ACR_GEN1_AI_SLIDERS];   /* 0..100 */
    uint8_t rebel;                         /* Human PLUS level 0..9 (record byte 0) */
    /* How quick and accurate the pilot is, 0..100 (not in the game's
     * editor). It changes reaction time and aim, never the AC. */
    uint8_t skill;
    /* Which brain flies (ACR_GEN1_BRAIN_*; DEFAULT = the AI setting) and
     * the original brain's record (from the opponent's file). */
    uint8_t brain;
    acr_gen1_ai_orig_record rec;
} acr_gen1_ai_profile;

/* The original brain's records. Phantasma: 60 bytes, Master of Arena: 36
 * (opponent = starting rank - 1; main_arena: Master of Arena's main
 * ladder, where its top 11 hold their shots 10 frames and the rest 20). */
void acr_gen1_ai_orig_from_pp(acr_gen1_ai_orig_record *r, const uint8_t rec[ACR_GEN1_AI_PP_RECORD_SIZE], int opponent);
void acr_gen1_ai_orig_from_moa(acr_gen1_ai_orig_record *r, const uint8_t rec[ACR_GEN1_AI_RECORD_SIZE], int opponent,
                               int main_arena);

/* The AI setting (SETTINGS > AI). brain: DEFAULT/ORIGINAL = the original
 * brain wherever a profile doesn't name one, UTILITY = the utility brain
 * everywhere (the one switch back). style: NATIVE = each opponent's own
 * disc, PP or MoA = that style for all. Read when a pilot starts (init);
 * online, both machines must agree. */
void acr_gen1_ai_set_options(int brain, int style);
int  acr_gen1_ai_brain_option(void);
int  acr_gen1_ai_style_option(void);
const char *acr_gen1_ai_brain_name(int brain);   /* "ORIGINAL", "ACR UTILITY (HARD)" */
const char *acr_gen1_ai_style_name(int style);   /* "OWN DISC", "PHANTASMA", "MASTER OF ARENA" */
const char *acr_gen1_ai_style_note(int style);   /* one line: what differs */

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
    uint8_t selected;   /* the weapon picked: current, or a switch still waiting for the reload */
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
    /* For the original brain: the target in the world (world units; its
     * velocity in 1/64 units a tick), its facing, current weapon's reach
     * and AP; our own guns' shot speed (world units a tick, 0 = none). */
    int32_t  tx, ty, tz, tvx, tvy, tvz;
    uint16_t tyaw;
    int32_t  target_range, target_ap;
    int32_t  shot_speed[ACR_GEN1_MAX_WEAPONS];
    /* The nearest missile flying at us (missiles_in > 0): where it is,
     * world units, and its heading. */
    int32_t  missile_x, missile_z, missile_dist;
    uint16_t missile_yaw;
    /* Gun shots and rockets of an enemy heading at us within about 11
     * degrees (Master of Arena's shot dodge), the nearest one's heading. */
    uint8_t  shots_in;
    uint16_t shot_yaw;
    /* The battle's pace: steps a second and Gen 1 ticks a second (22);
     * 0 = one tick a step. The original brain thinks once a tick. */
    uint16_t step_rate, gen1_rate;
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
    /* The original brain (acr_gen1_ai_orig.c); unused by the utility one. */
    uint8_t  brain;           /* resolved at init: ORIGINAL or UTILITY */
    struct acr_gen1_ai_orig {
        uint8_t  style;       /* resolved: PP or MoA */
        uint32_t frame;
        int32_t  hx[32], hy[32], hz[32];     /* player position history, newest at `at` */
        uint8_t  at, filled;
        int8_t   cur;                         /* behaviour slot (0..6), -1 none */
        uint32_t cur_start;
        uint16_t learned[ACR_GEN1_AI_ORIG_TUNES];
        uint16_t desire[3];                   /* 0..4096 */
        uint16_t blade;                       /* 0..4096 */
        uint16_t fire, avoid;                 /* MoA, 0..4096 */
        uint8_t  delay, tracking_on;
        uint16_t track;                       /* record tracking -> v*4096/600 + 512 */
        uint16_t lost;                        /* 0..3276 */
        uint16_t wweight[ACR_GEN1_MAX_WEAPONS];
        int8_t   wpick;
        uint32_t wpick_frame;
        int8_t   side, wmove, wstrafe;
        int8_t   prev_turn;
        int32_t  last_ap, last_tap;
        uint16_t gate_count;
        uint8_t  desperate_done;
        uint32_t fire_q;                      /* MoA fire hold: one bit a frame */
        uint8_t  hold;
        uint8_t  dodge_frames;
        int8_t   dodge_dir;
        uint8_t  b7_side, b7_phase;
        uint8_t  switch_wait;
        uint8_t  boost_frames;
        uint8_t  gate;                        /* frame gate of the long-range blade rule */
        uint16_t tick_acc;                    /* toward the next Gen 1 tick (gen1_rate of step_rate) */
        acr_gen1_input held_in;               /* held between ticks */
        uint8_t  held_fire;
    } o;
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
