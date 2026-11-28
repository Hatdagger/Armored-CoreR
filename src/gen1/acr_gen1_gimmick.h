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
 * (0x80090934): n = 1 open (0x8200), 2 unlock and open for good (0x8201).
 * Kind 0 tells nobody: it just breaks (map 1's submarine hull, 54-56,
 * handler 0x800A51B8; the map's script watches it). */
#define ACR_GEN1_MAX_BREAKABLES 64   /* map 2 has 53 */
typedef struct acr_gen1_breakable {
    int16_t  area, game_area;
    int16_t  centre[3];
    int16_t  size_xz, size_y;
    int16_t  top, bottom;         /* its box's y (y down); older imports: centre - size_y, centre */
    int32_t  hp;                  /* left; 0 = broken */
    uint8_t  message;
    int16_t  target;              /* game area */
    int32_t  offset[3];           /* where its area is put (moved away once broken) */
} acr_gen1_breakable;

/* A lift (the 12-byte kind: map 18 set-up 0x800A21B0, tick 0x800A1F44,
 * messages 0x800A1E54, flip 0x800A1DB4; the same code on maps 1, 5, 7, 9,
 * 16, 18, 19). It runs between two heights (y a, y b: its centre's y),
 * x / z following y in a straight line when they differ. Speeds are in
 * 1/32 units a tick: up to speed x 32, speeding up and slowing down by
 * `speed` a tick, slowing once within speed x 17 of the end. A flip (the
 * player bumping it, its call button, the blade) turns it round. */
#define ACR_GEN1_MAX_LIFTS   8
#define ACR_GEN1_MAX_BUTTONS 8
typedef struct acr_gen1_lift {
    int16_t  area, game_area;
    int16_t  centre[3];           /* where the disc puts it */
    int16_t  size_xz, size_y;     /* box: centre at its foot, size_y up */
    uint16_t speed;               /* data +0 */
    int16_t  ya, yb;              /* data +2 / +4 */
    int16_t  xa, za, xb, zb;      /* data +12..+18 */
    uint8_t  sound_move, sound_stop, sound_flip;   /* data +8 / +9 / +10 */
    /* The controller. */
    uint8_t  way;                 /* +16: 0 toward y a, 1 toward y b */
    int16_t  vel;                 /* +18: 1/32 units a tick (y) */
    int16_t  step;                /* +10: y moved this tick */
    uint16_t blocked;             /* +26 (not used: nothing stops a lift here) */
    uint8_t  was_near;            /* the player touching it last tick */
    int32_t  offset[3];           /* from centre[] */
} acr_gen1_lift;

/* A lift's call button (map 18 set-up 0x800A2248, handler 0x800A1ECC):
 * 0x8100 from the player flips the lift on its link [0]. */
typedef struct acr_gen1_button {
    int16_t  area, game_area;
    int16_t  centre[3];
    int16_t  size_xz, size_y;
    int16_t  lift;                /* the lift's game area */
    uint8_t  sound;
    uint8_t  was_near;
} acr_gen1_button;

/* A ship (the 28-byte kind: map 1 set-up 0x800A4F34, tick 0x800A499C;
 * map 6 has one too): sails back and forth inside its range. Per axis the
 * speed grows by `acc` a tick up to `top`; once the centre passes the
 * range's end the speed-up turns round. Units standing on it at the start
 * ride along (ours: the game's rider link isn't read). */
#define ACR_GEN1_MAX_SHIPS 4
typedef struct acr_gen1_ship {
    int16_t  area, game_area;
    int16_t  centre[3];
    int16_t  size[3];             /* the area's box */
    int16_t  lo[3], hi[3];        /* record +0 / +8: the range */
    int16_t  top[3];              /* data +0.. (made positive) */
    int16_t  acc[3];              /* data +6.. (turns round) */
    int16_t  vel[3];              /* starts at data +0.. as written */
    int32_t  offset[3];
} acr_gen1_ship;

/* Kept by value in the battle, so a rollback snapshot carries the doors;
 * the stage is put where they say by acr_gen1_stage_place_area. */
typedef struct acr_gen1_gimmicks {
    int16_t doors;
    int16_t stage;                /* 1 + the stage they were read for, 0 = none read */
    uint8_t swinging;             /* the player's blade was out last tick */
    acr_gen1_door door[ACR_GEN1_MAX_DOORS];
    int16_t breakables;
    acr_gen1_breakable breakable[ACR_GEN1_MAX_BREAKABLES];
    int16_t lifts, buttons;
    acr_gen1_lift lift[ACR_GEN1_MAX_LIFTS];
    acr_gen1_button button[ACR_GEN1_MAX_BUTTONS];
    int16_t ships;
    acr_gen1_ship ship[ACR_GEN1_MAX_SHIPS];
    uint8_t riders_set;           /* the battle has put its units on the ships */
    int8_t  rider[64];            /* unit u rides ship rider[u] - 1 (0: none; ACR_GEN1_MAX_UNITS) */
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

/* Index of the lift on game area `game_area`, or -1. */
int  acr_gen1_lift_find(const acr_gen1_gimmicks *g, int game_area);

/* The flip 0x800A1DB4: a stopped lift turns round; a moving one only once
 * it is more than twice its slowing distance from where it set off. */
void acr_gen1_lift_flip(acr_gen1_lift *l);

/* One Gen 1 tick for every lift and button. `player` as for the doors.
 * A lift the player comes to touch (or a button the player comes to) gets
 * 0x8100: it flips. For lift i, move[i] gets the step it moved. */
void acr_gen1_lifts_tick(acr_gen1_gimmicks *g, const int32_t player[3], int32_t radius, int32_t height,
                         int32_t (*move)[3]);

/* One Gen 1 tick for every ship; move[i] gets ship i's step. */
void acr_gen1_ships_tick(acr_gen1_gimmicks *g, int32_t (*move)[3]);

/* 1 if p (game axes) is over ship s's box across (x / z). */
int  acr_gen1_ship_over(const acr_gen1_ship *s, const int32_t p[3], int32_t radius);

/* Index of the breakable on game area `game_area`, or -1. */
int  acr_gen1_breakable_find(const acr_gen1_gimmicks *g, int game_area);

/* 1 if feet at p (radius r) stand on lift l's top. */
int  acr_gen1_lift_under(const acr_gen1_lift *l, const int32_t p[3], int32_t radius);

/* The near test 0x800A2130 (0x80012BAC): within the door's reach across
 * and its height. */
int  acr_gen1_door_near(const acr_gen1_door *d, const int32_t player[3], int32_t radius, int32_t height);

#endif
