/* Gen 1 moving map parts: the mission maps' doors (acr-import's
 * stages/<NN>/gimmicks.txt; docs/research/gen1-stages.md, "Doors").
 *
 * The rules are the door code most mission maps share (map 0: set-up
 * 0x800A2600, tick 0x800A2498 -> 0x800A217C, state 0x800A1F84, messages
 * 0x800A1FD4). A door is one area record; each tick it may move by a small
 * step, which the caller adds to that area's collision and drawing (the
 * game's 0x80077D8C adds the step to the area's centre and boxes).
 *
 * Game axes throughout (x NOT mirrored against the engine's, y down),
 * world units. Integer only. */
#ifndef ACR_GEN1_GIMMICK_H
#define ACR_GEN1_GIMMICK_H

#include <stdint.h>

#define ACR_GEN1_DOOR_PARTNERS 4
#define ACR_GEN1_MAX_DOORS     48    /* map 18 has the most: 36 */

typedef struct acr_gen1_door {
    /* From the disc. */
    int16_t  area, game_area;     /* the importer's area number, the game's */
    uint8_t  quarter;             /* the area's quarter turns */
    int16_t  centre[3];           /* where it sits shut (controller +34) */
    int16_t  size_xz, size_y;     /* the near test's reach (+22, +24) */
    uint16_t speed;               /* units a tick (data +0) */
    int16_t  distance;            /* how far it moves (data +2) */
    uint8_t  radio;               /* data +6: radio line while locked (0: not locked) */
    uint8_t  axis;                /* data +7: 0-3 sideways (+ quarter), 4 up */
    uint8_t  sound_open, sound_close, stays_open, opens_near;   /* data +8..+11 */
    uint8_t  partners;
    int16_t  partner[ACR_GEN1_DOOR_PARTNERS];   /* other pieces' area numbers (-1: none) */
    /* The controller. */
    uint8_t  state;               /* +16: 0 shut, 1 opening, 2 closing, 3 open, 4 opening for good, 6 open for good, 10 closing */
    uint8_t  count;               /* +17: ticks moved so far */
    uint8_t  ticks;               /* +18: ticks the move takes (distance / speed) */
    uint8_t  reopen;              /* +19 */
    int16_t  step_lo, step_hi;    /* +26 / +28: the two step sizes (spread evenly) */
    int16_t  acc, rem;            /* +30 / +32 */
    int32_t  offset[3];           /* how far it is from shut now */
    uint8_t  was_near;            /* the player near last tick (for the locked radio line) */
} acr_gen1_door;

/* A breakable area that tells a door something when it breaks (map 0's
 * generator, area 285; map 2's 69 / 70). Set-up 0x800A2A14, handler
 * 0x800A2804: message 0x8000 (a hit) takes its damage off data +0; at 0
 * the area breaks and (kind 1, message n) goes to game area `target`
 * (0x80090934): n = 1 open (0x8200), 2 unlock and open for good (0x8201). */
#define ACR_GEN1_MAX_BREAKABLES 8
typedef struct acr_gen1_breakable {
    int16_t  area, game_area;
    int16_t  centre[3];
    int16_t  size_xz, size_y;
    int32_t  hp;                  /* left; 0 = broken */
    uint8_t  message;
    int16_t  target;              /* game area */
    int32_t  offset[3];           /* where its area is put (moved away once broken) */
} acr_gen1_breakable;

/* Kept by value in the battle, so a rollback snapshot carries the doors;
 * the stage is put where they say by acr_gen1_stage_place_area. */
typedef struct acr_gen1_gimmicks {
    int16_t doors;
    int16_t stage;                /* 1 + the stage they were read for, 0 = none read */
    uint8_t swinging;             /* the player's blade was out last tick */
    acr_gen1_door door[ACR_GEN1_MAX_DOORS];
    int16_t breakables;
    acr_gen1_breakable breakable[ACR_GEN1_MAX_BREAKABLES];
} acr_gen1_gimmicks;

/* What a tick asks of the caller besides moving areas. */
typedef struct acr_gen1_gimmick_events {
    int16_t radio;                /* a locked door's line (0: none) */
    uint8_t sound;                /* an open / close sound (0: none) */
    int16_t sound_area;
} acr_gen1_gimmick_events;

/* Parses gimmicks.txt (unknown lines skipped; doors past the
 * ACR_GEN1_MAX_DOORS-th left out). All shut. */
void acr_gen1_gimmicks_parse(const char *text, acr_gen1_gimmicks *out);

/* Index of the door on game area `game_area`, or -1. */
int  acr_gen1_door_find(const acr_gen1_gimmicks *g, int game_area);

/* The state change 0x800A1F84: the door and its other pieces get state s;
 * 4 also unlocks it. */
void acr_gen1_door_set_state(acr_gen1_gimmicks *g, int i, int s);

/* Message 0x8201 (the map's script: unlock and open for good). */
void acr_gen1_door_unlock(acr_gen1_gimmicks *g, int i);

/* One Gen 1 tick for every door. `player` = the player's position (feet,
 * game axes), `radius` / `height` its body (object +40 / +42). For door i,
 * move[i] gets the step it moved this tick (add it to its area). */
void acr_gen1_gimmicks_tick(acr_gen1_gimmicks *g, const int32_t player[3], int32_t radius, int32_t height,
                            int32_t (*move)[3], acr_gen1_gimmick_events *ev);

/* The player's blade swing: message 0x8100 to every shut door the swing
 * reaches (`reach` past the body, across and up / down), doors that open
 * on their own included (map 0's door 61 to the generator opens to the
 * blade in play, though its data says it opens on approach): a locked one plays
 * its radio line, otherwise it opens. Call once, the tick the swing starts. */
void acr_gen1_gimmicks_blade(acr_gen1_gimmicks *g, const int32_t player[3], int32_t radius, int32_t height,
                             int32_t reach, acr_gen1_gimmick_events *ev);

/* A hit of `damage` at point p (game axes): every unbroken breakable whose
 * box, grown by `reach`, holds p takes it. Returns 1 if one was hit. */
int  acr_gen1_gimmicks_hit(acr_gen1_gimmicks *g, const int32_t p[3], int32_t reach, int32_t damage);

/* The near test 0x800A2130 (0x80012BAC): within the door's reach across
 * and its height. */
int  acr_gen1_door_near(const acr_gen1_door *d, const int32_t player[3], int32_t radius, int32_t height);

#endif
