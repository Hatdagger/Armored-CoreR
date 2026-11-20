/* Gen 1 fighting: lock-on, weapons, shots, missiles and damage for two ACs.
 * Integer only and stepped with the movement, so every machine agrees.
 *
 * Follows Master of Arena's split-screen VS engine (FDAT 209) as mapped in
 * the AC1Recomp fork (docs/mapping/battle-system.md, stat-block.md,
 * parts-tables.md). Numbers, sources and what is still a stand-in:
 * docs/research/gen1-weapons.md.
 *
 * Time: the game counts most things in its own steps (22 a second, "ticks"
 * here). The battle may run faster (step_rate); counters stay in ticks and
 * movement is spread over the steps, so a match plays the same per second. */
#ifndef ACR_GEN1_BATTLE_H
#define ACR_GEN1_BATTLE_H

#include <stdint.h>

#include "acr_gen1_sim.h"

enum { ACR_GEN1_GUN, ACR_GEN1_MISSILE, ACR_GEN1_BLADE };
enum { ACR_GEN1_MOUNT_HAND, ACR_GEN1_MOUNT_BACK, ACR_GEN1_MOUNT_ARMS };

/* One row of the VS engine's missile flight table (0x80093114), all per tick. */
typedef struct acr_gen1_flight {
    uint16_t fuel;      /* ticks of thrust; after it the nose drops */
    uint16_t life;      /* ticks before the missile is removed */
    uint16_t thrust;    /* added along the heading each tick (world units) */
    uint16_t top;       /* top speed, world units a tick */
    uint16_t turn;      /* most the heading turns a tick, 4096 a turn */
    uint16_t cone;      /* gives up once the target leaves +- this after being inside */
    uint8_t  kind;      /* 3 splits into four, 5 and 11 never give up */
} acr_gen1_flight;

typedef struct acr_gen1_weapon {
    uint8_t  kind;
    char     name[20];
    uint16_t attack;
    uint16_t ammo;          /* 0 = unlimited (blade) */
    int32_t  range;         /* world units, already capped by the FCS; blade: reach */
    uint8_t  energy;        /* energy damage (else shell) */
    uint16_t lock_ms;       /* guns: non-zero = locks on (the time itself comes from the FCS
                             * value, the target's speed and distance: see gun lock below) */
    uint16_t box_w, box_h;  /* FCS outer lock box: angles off the facing / view tilt, 4096 a turn */
    uint16_t interval;      /* refire wait after a shot or the last volley, in ticks (detail +0x11) */
    int32_t  speed;         /* gun shot speed, world units a tick */
    uint8_t  stance;        /* back cannons: needs a steady stance to fire (see below) */
    /* Guns */
    uint8_t  shots;         /* per trigger: 1, 2 or 4 (detail +2 bits 0-1) */
    uint8_t  straight;      /* sight bit 2 (rockets, dispensers): no aiming, straight ahead */
    uint8_t  hold;          /* post-shot hold in ticks (detail +4 != 255; length a stand-in) */
    /* Aiming (presentation): the animation set's aim table this weapon uses
     * (detail +3, +1 on the right back slot; 255 none), and whether left /
     * right goes on the y axis (cannons, weapon arms: fire pattern bit 3)
     * rather than z (hand weapons). */
    uint8_t  aim_table;
    uint8_t  aim_y;
    uint8_t  mount;         /* ACR_GEN1_MOUNT_*: which part the shots leave from */
    int32_t  lock_range;    /* FCS class range: how far the sight looks (0 = range) */
    int32_t  hard_lock_range; /* modern controls: hard lock only starts this close (0 = the sight's
                               * range); once on it holds to the full sight range */
    /* Missiles */
    uint16_t inner_w, inner_h;  /* FCS inner box (angles): where a lock can start; guns hold one only here */
    uint8_t  lock_value;    /* FCS class value. Missiles: the cursor moves value x 3 / 16 a tick;
                             * guns: lock time grows with it (gun lock below) */
    uint8_t  lock_ticks;    /* ticks on target per lock: (256 - FCS value) / 8 */
    uint8_t  max_locks;     /* the smaller of the FCS's and the weapon's (detail +0x10) */
    uint8_t  volley_a, volley_b;     /* missiles per volley from each side */
    uint8_t  ignition;      /* ticks of coasting before the motor lights */
    uint16_t spread_pitch, spread_yaw;
    uint8_t  auto_launch;   /* SP-M/AUTO: fires by itself when the locks are full */
    acr_gen1_flight flight, split;   /* split: the four a kind-3 missile breaks into */
    /* Blade */
    uint16_t charge_drain;  /* energy a swing takes (blade +0x2C) */
} acr_gen1_weapon;

/* Legs, for the cannon stance rule: two legs (humanoid, reverse joint) must
 * crouch to fire a cannon, four legs only fire them on the ground,
 * caterpillars fire them freely. Human PLUS (level 4 and up, body frame 3)
 * lifts the rule for two legs. */
enum { ACR_GEN1_LEGS_TWO, ACR_GEN1_LEGS_FOUR, ACR_GEN1_LEGS_TRACKS };
#define ACR_GEN1_KNEEL_MS 400   /* time to crouch before a cannon fires (stand-in) */

#define ACR_GEN1_MAX_WEAPONS 3
#define ACR_GEN1_MAX_SHOTS   128
#define ACR_GEN1_MAX_LOCKS   6     /* locks on one target (lock slot +0x26) */

typedef struct acr_gen1_fighter {
    acr_gen1_ac ac;
    const acr_gen1_ac_def *def;
    int32_t  ap, ap_max;
    uint8_t  shell_factor, energy_factor;     /* damage taken, 1..45 */
    acr_gen1_weapon weapon[ACR_GEN1_MAX_WEAPONS];
    uint8_t  weapons, current;
    uint16_t ammo[ACR_GEN1_MAX_WEAPONS];
    uint16_t cooldown;                         /* ticks until the next shot (AC +0x48) */
    uint16_t hold_left;                        /* ticks of post-shot hold (AC +0x6D) */
    uint8_t  locked;                           /* guns: locked; missiles: at least one lock */
    /* Gun lock (lock record at player block +0x7C, VS 0x800800E4) */
    uint16_t lock_wait;                        /* +0x08: ticks toward the lock time */
    uint16_t lock_lead;                        /* +0x0A: ticks of aiming ahead since */
    uint16_t lead_yaw;                         /* where locked shots go (+0x18 / +0x1A, absolute) */
    int16_t  lead_pitch;
    /* Where the current weapon's shots leave from (game: the weapon's joint,
     * VS 0x80078888), from the drawn model: right, up, forward of the AC's
     * feet in world units. muzzle_set = 0: the stand-in, 120 up the middle. */
    int16_t  muzzle[3];
    uint8_t  muzzle_set;
    /* Missiles (lock slot at player block +0x7C) */
    uint8_t  locks;                            /* locks held on the target */
    uint8_t  lock_count;                       /* ticks with the cursor on the target */
    int16_t  cursor_yaw, cursor_pitch;         /* lock cursor, relative to the view (+0x20 / +0x22) */
    uint8_t  volley_ticks;                     /* AC +0x47: volleys left x 4, counting down */
    /* Blade */
    uint16_t swing;                            /* ms into the swing, 0 = not swinging */
    uint8_t  swing_hits;                       /* hits landed this swing (no guard, as in the game) */
    uint8_t  lunge_done;                       /* a hit or body contact ended the lunge */
    int8_t   swing_target;                     /* AC found ahead at the swing's start, -1 none */
    int32_t  lunge_x, lunge_z;                 /* lunge direction (4096 = 1) */
    /* Stance */
    uint8_t  legs;                             /* ACR_GEN1_LEGS_* */
    uint8_t  human_plus;
    uint8_t  kneeling;
    int32_t  kneel_ms;
    /* Defences */
    uint8_t  jammer;                           /* SP-JAM: missile locks on it break 5% a tick */
    uint8_t  intercept_burst;                  /* AC +0x118: ticks left in a burst */
    int16_t  intercept_shot;                   /* the missile the burst is aimed at, -1 none */
    uint16_t hit_flash;                        /* ticks of the "HIT" blink (player block +0x24C) */
    /* Camera swing (presentation; camera record +0x2C / +0x2E, VS
     * 0x8008774C): the chase camera's pitch and yaw offsets from the view
     * tilt and the facing, turned each tick toward the target (see
     * acr_gen1_camera_angles). prev = the tick before, for drawing between
     * ticks. */
    int16_t  cam_pitch, cam_yaw, cam_pitch_prev, cam_yaw_prev;
    int16_t  last_missile;                     /* shot index of the latest missile launched (+0x1C) */
    /* Modern controls (SP-MODERN fitted; see ACR_GEN1_MODERN_* below) */
    uint8_t  modern;
    uint8_t  hard_lock;                        /* on: the AC turns and tilts toward the target */
    uint16_t sight_yaw;                        /* where the FCS sight points (modern: lags the facing) */
    int16_t  sight_pitch;
    uint16_t hist_yaw[4];                      /* facing / tilt over the last ticks, newest first */
    int16_t  hist_pitch[4];
    uint8_t  hist_n;
    uint16_t recover_ms;                       /* modern blade: recovery left after a swing */
} acr_gen1_fighter;

typedef struct acr_gen1_shot {
    uint8_t  active, owner, weapon;
    int32_t  x, y, z;                          /* 1/64 world units */
    int32_t  vx, vy, vz;                       /* guns: 1/64 units a step; missiles: 1/64 units a tick */
    int32_t  travelled;                        /* world units */
    /* Missiles */
    uint8_t  missile, flags;                   /* bit 0 has been in the cone, bit 1 gave up */
    uint16_t yaw;                              /* heading, 4096 a turn */
    int16_t  pitch;                            /* + = up */
    uint16_t age;                              /* ticks since launch */
    uint8_t  ignition, split;                  /* split: ticks after ignition, 0 = never */
    acr_gen1_flight flight;
} acr_gen1_shot;

/* ---- Modern controls (the SP-MODERN option) ----
 * Twin sticks give free movement; the price is paid in aim, so a skilled
 * pilot on the original controls stays competitive. Everything else (the
 * AC's turn speed, weapons, switching) is the original's.
 * - FCS: the lock boxes are BOX_PCT of the FCS's own (inner and outer);
 *   locked gun shots aim only LEAD_PCT of the way to the predicted point;
 *   missile locks take MISSILE_PCT of the ticks. The FCS sight does not sit
 *   on the facing: it follows where the facing was SIGHT_LAG ticks ago, and
 *   turns no faster than the AC's own top turn rate (tilt: the L2 / R2
 *   rate). A target crossing quickly slips out of the sight (the lock
 *   breaks) while staying on screen.
 * - Blade: easier to land, dearer to miss. Lunge steering at STEER_PCT of
 *   the turn rate (the blade ignores the weaker sight), each swing costs
 *   BLADE_EN_PCT of the charge drain, and after the swing the AC stands
 *   still for RECOVER_HIT_MS after a hit or RECOVER_MISS_MS after a miss
 *   (no moving, firing or swinging).
 * - Running dry: no energy weapons (or blade, as for everyone) until the
 *   bar is full again; the boost toggle still works (acr_gen1_sim.h).
 * - Hard lock (a toggle): while the target is in the current weapon's
 *   (lagging, smaller) sight, the AC turns and tilts toward it at its normal
 *   rates whenever the pilot isn't turning / tilting. It drops when the
 *   target leaves the sight and needs a fresh press. With a no-lock weapon
 *   (rockets) selected it is off and can't be turned on: they stay aimed by
 *   hand, as in the original.
 * Stand-in values to tune in play (mirror matches, modern vs original). */
/* Long-range FCS (class 0 range at or above this: QX-9009, RATOR on the
 * Master of Arena disc): a modern pilot's hard lock only starts within the
 * furthest range a normal FCS reaches for that lock class (the platform
 * works it out from the import's FCS parts into hard_lock_range), but once
 * on it holds out to the long-range FCS's own range. So a sniper has to
 * close in to lock, then can pull back out. */
#define ACR_GEN1_LONG_FCS_RANGE 10000

#define ACR_GEN1_MODERN_BOX_PCT          30    /* aiming is automatic: a much smaller sight */
#define ACR_GEN1_MODERN_LEAD_PCT         60
#define ACR_GEN1_MODERN_SIGHT_LAG        2     /* ticks, ~0.1 s; at most 3 */
#define ACR_GEN1_MODERN_MISSILE_PCT      140
#define ACR_GEN1_MODERN_BLADE_EN_PCT     125
#define ACR_GEN1_MODERN_STEER_PCT        115
#define ACR_GEN1_MODERN_RECOVER_HIT_MS   100
#define ACR_GEN1_MODERN_RECOVER_MISS_MS  300

typedef struct acr_gen1_modern_tuning {
    uint16_t box_pct, lead_pct, sight_lag, missile_pct, blade_en_pct, steer_pct, recover_hit_ms, recover_miss_ms;
} acr_gen1_modern_tuning;

typedef struct acr_gen1_battle {
    acr_gen1_fighter f[2];
    acr_gen1_shot shot[ACR_GEN1_MAX_SHOTS];
    uint16_t step_ms;                          /* length of a step, for times in ms */
    /* Shots fly at the original's speed per second whatever the step rate:
     * per step they move speed x gen1_rate / step_rate (0 / 0 = Gen 1). */
    uint16_t step_rate, gen1_rate;
    uint32_t tick_phase;                       /* counts toward the next tick, in gen1_rate units */
    uint32_t rng;                              /* the battle's own random numbers (0 = default seed) */
    const acr_gen1_stage *stage;               /* the arena (NULL = open floor): stops shots */
    acr_gen1_modern_tuning modern;             /* 0 fields = the defaults below */
} acr_gen1_battle;

typedef struct acr_gen1_actions {
    uint8_t fire;            /* held: the selected gun or missile */
    uint8_t next_weapon;     /* pressed this step: cycles guns and missiles (not the blade) */
    uint8_t blade;           /* pressed this step: the blade has its own button, as in the game */
    uint8_t hard_lock;       /* pressed this step: hard lock on / off (modern controls only) */
} acr_gen1_actions;

/* The game draws an AC at a quarter of its model's units (VS model draw
 * 0x80062C00: "a model unit is 1/4 of a world unit"). So ranges, speeds and
 * the lock box keep the game's world numbers and the models are scaled down. */
#define ACR_GEN1_MODEL_SCALE 0.25f

/* An AC's hit cylinder (AC object +0x72 / +0x74): radius 75, 212 tall,
 * standing on its feet. */
#define ACR_GEN1_HIT_RADIUS 75
#define ACR_GEN1_HIT_HEIGHT 212

/* FCS lock boxes are angles (4096 a turn) either side of the facing and
 * above / below the view tilt, measured from the weapon (VS sight sweep
 * 0x80078230). The default FCS's gun boxes, 240 outer and 200 inner, are
 * 21 and 17.6 degrees; the recomp measured the rifle locking out to about
 * 17 (tan 0.29-0.32).
 *
 * Gun lock (VS 0x800800E4), each tick, while the target is inside the
 * inner box (leaving it drops the lock at once):
 *   lock time T = (target speed a tick x FCS value / 256) x distance / 32768
 *   first T ticks waiting, then T ticks aiming ahead; locked from there.
 * So a target standing still is locked the tick it enters the box, and one
 * boosting across far away takes longer (WG-RF35, default FCS, a target
 * moving 100 a tick 4,000 away: T = 2, locked in 5 ticks). Aiming ahead
 * (main program 0x800155DC): the point the target will reach when a shot
 * gets there, refined (ticks aiming / 4) times, at most 8. */
#define ACR_GEN1_LEAD_MAX 8

/* The HUD's sight (VS 0x800576E0) is the inner box drawn as an octagon:
 * half-sizes sin(inner angle), corners cut by 512 at 16,384 out. */
#define ACR_GEN1_SIGHT_DEPTH 16384
#define ACR_GEN1_SIGHT_CORNER 512

/* Blade swing (VS 0x80083224 / 0x8007EB48, plus measurements). A press
 * needs energy above the charge drain and takes it. The swing looks for an
 * AC up to SEEK ahead within +-45 degrees and turns toward it at the AC's
 * top turn rate until the lunge, then lunges along its facing (measured
 * profile, acr_gen1_blade_lunge). The game tests the blade line twice a tick
 * over ticks 7-11 with no "already hit" guard, so a close target can be hit
 * twice (492 on the default AC, 246 each). Here the arc crosses the front
 * twice, HIT_TICK and HIT_TICK + 1 lunge ticks in: the first crossing hits
 * a target within REACH, the second within REACH2; a hit pushes the target
 * KNOCK back and ends the lunge. Fitted to the recomp's harness runs (11
 * swings each): 2 hits from 650 and closer, 1 at 700, none at 800. Bodies
 * stop the lunge at contact (two hit radii). */
#define ACR_GEN1_BLADE_SEEK       3000
#define ACR_GEN1_BLADE_SEEK_ANGLE 512     /* 45 degrees */
#define ACR_GEN1_BLADE_STEER_TICKS 7
#define ACR_GEN1_BLADE_LUNGE_MS   330
#define ACR_GEN1_BLADE_STEPS      7
#define ACR_GEN1_BLADE_HIT_TICK   2
#define ACR_GEN1_BLADE_REACH      450
#define ACR_GEN1_BLADE_REACH2     420
#define ACR_GEN1_BLADE_KNOCK      10
#define ACR_GEN1_BLADE_SWING_MS   1450    /* swing to swing, measured */
extern const int16_t acr_gen1_blade_lunge[ACR_GEN1_BLADE_STEPS];

/* Missile interceptor (VS 0x800829E4): each tick, chance core +0x34 / 128
 * to look for an enemy missile within 5,000 and +- core +0x35 x 4 of the
 * facing; a find starts a 15-tick burst, and each shot of it (odd ticks)
 * has a 1 in 32 chance to destroy that missile. */
#define ACR_GEN1_INTERCEPT_RANGE 5000
#define ACR_GEN1_INTERCEPT_BURST 15

/* Damage factor from the summed head, core, arms and legs defense
 * (menu module 0x800A83D8), with or without the matching screen option. */
uint8_t acr_gen1_damage_factor(uint32_t defense, int energy, int screen);
/* AP a hit takes (VS engine 0x80079500). */
int32_t acr_gen1_hit_ap(uint16_t attack, uint8_t factor);
/* Knockback level of a hit (VS 0x80079AA4): impact x 256 / (stability x 25 / 4), 0..5. */
int acr_gen1_knockback_level(uint16_t impact, uint8_t stability);

void acr_gen1_battle_step(acr_gen1_battle *b, const acr_gen1_input in[2], const acr_gen1_actions act[2]);

/* Can the fighter fire its current weapon in its present stance? */
int acr_gen1_stance_ok(const acr_gen1_fighter *f);

/* Does fighter i's sight see the other AC for weapon w: inside the outer
 * lock box and the FCS range (lock record +0x16 bit 1)? */
int acr_gen1_in_lock_box(const acr_gen1_battle *b, int i, const acr_gen1_weapon *w);

/* What fighter i aims with (presentation only), as the game's fire routines
 * set it every tick (VS 0x80080DF4, 0x800800E4): returns the selected
 * weapon's aim table (-1: none, e.g. during a blade swing) and the aim
 * angles (4096 a turn): x = down (+) / up (-), the angle to the target while
 * it is in the weapon's lock box, else the view tilt; y or z = the target's
 * angle off the facing (+ = to the right) while in the box, else 0.
 * Straight-firing weapons (rockets) follow the tilt only. */
int acr_gen1_aim(const acr_gen1_battle *b, int i, int16_t aim[3]);

/* The chase camera's swing (VS 0x8008774C, once a tick): its pitch / yaw
 * offsets turn toward
 * - 3/4 of the target's angle from the view, 64 a tick, while the latest
 *   missile launched at it is still flying (player block +0x1C);
 * - else 1/2 of it, 20 a tick, while the target is in the selected
 *   weapon's sight (lock record +0x16 bit 1: inside the outer lock box);
 * - else back to 0, 20 a tick.
 * The camera then looks along facing + yaw offset, at view tilt + pitch
 * offset (clamped to +-800), so it swings toward the target and the FCS
 * box, which stays on the AC's facing, drifts off the screen's centre
 * (VS chase camera 0x800878FC, HUD sight 0x800576E0). Run by the battle;
 * angles in 4096 a turn, pitch + = up, yaw + = right. */
#define ACR_GEN1_CAM_PITCH_MAX 800

/* Out of area for fighter i (the stage's boxes, acr_gen1_stage_bound):
 * 0 inside, 1 outside the first inner box only (the warning), 2 outside
 * both (that side loses). 0 on a stage without the boxes. */
int acr_gen1_battle_out_of_area(const acr_gen1_battle *b, int i);

/* How a round stands (VS main loop end checks): 0 still fighting, 1 or 2 that
 * player won, 3 a draw. Knock-out: the one standing wins, both down is a
 * draw; out of area counts as down. Time up: the higher AP wins, equal AP is
 * a draw. */
int acr_gen1_battle_result(const acr_gen1_battle *b, int time_up);

#endif
