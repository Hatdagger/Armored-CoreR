/* The Gen 1 battle HUD, drawn as Master of Arena draws it
 * (docs/research/gen1-hud.md): the game's own HUD pictures (acr-import
 * writes them to <import>/hud), placed on the PS1's 320 x 240 screen as
 * measured from the game.
 *
 * Framing: the PS1 screen is 4:3. ACR_HUD_FRAME_43 keeps the whole layout
 * in the middle 4:3 of a wider view (default); ACR_HUD_FRAME_WIDE moves the
 * left and right columns out to the view's edges. Views narrower than 4:3
 * (split screen) shrink the layout to fit. */
#ifndef BATTLE_HUD_H
#define BATTLE_HUD_H

#include <stdint.h>

enum { BATTLE_HUD_FRAME_43 = 0, BATTLE_HUD_FRAME_WIDE = 1 };

/* Warning boxes, bottom right, as the game shows them. */
enum { BATTLE_HUD_MSG_HIT = 1, BATTLE_HUD_MSG_DAMAGED = 2, BATTLE_HUD_MSG_LEAVING_AREA = 4,
       BATTLE_HUD_MSG_ARMOR_LOW = 8, BATTLE_HUD_MSG_CHARGING = 16, BATTLE_HUD_MSG_SYSTEM_ERROR = 32 };

/* The energy bar's height in PS1 pixels (arena engine 0x80057020). */
#define BATTLE_HUD_BAR 157

#define BATTLE_HUD_MAX_WEAPONS 4
#define BATTLE_HUD_MAX_OTHERS 72   /* co-op ACs and mission units */

typedef struct battle_hud_weapon {
    const char *name;
    int ammo, ammo_max;      /* ammo_max 0: no ammo count (blade, energy weapons) */
} battle_hud_weapon;

typedef struct battle_hud_state {
    int ap, ap_max;          /* ap_max: the AC's full AP (the digits redden at a tenth) */
    int energy, energy_max, red_zone;
    int drained;
    int enemy_ap;            /* < 0: no enemy */
    int heading;             /* the AC's facing, 4096 a turn */
    battle_hud_weapon weapon[BATTLE_HUD_MAX_WEAPONS];
    int weapons, current;
    int radar_range;         /* 0: no radar fitted, no radar shown */
    int enemy_dx, enemy_dz;  /* enemy minus own position, world units (engine axes) */
    int enemy_on_radar;
    /* Co-op: the other ACs on the radar (not the target): enemies orange, teammates light blue. */
    struct { int dx, dz, ally; } other[BATTLE_HUD_MAX_OTHERS];
    int others;
    unsigned messages;       /* BATTLE_HUD_MSG_* */
    int recording;           /* REC: only while the game records replay data
                                (arena and VS fights, never missions) */
    const char *ac_name;
    int time_ms;             /* round clock; < 0: none (shown above the radar) */
} battle_hud_state;

/* Loads the PNGs in <import_dir>/hud. 0 if the import has no HUD pictures (an
 * older import): the HUD is then drawn with plain text and boxes. */
int  battle_hud_load(const char *import_dir);
int  battle_hud_ready(void);

/* Draws into the current HUD rectangle (acr_render_hud), hud_w x 480. */
void battle_hud_draw(const battle_hud_state *s, float hud_w, int frame);

/* The layout's transform for this view, for the parts the PC shell draws
 * itself: PS1 screen x, y (320 x 240) to the HUD grid. `anchor` -1 left
 * column, 0 middle, 1 right column. */
void battle_hud_point(float hud_w, int frame, int anchor, float x, float y, float *hx, float *hy);

/* Text in the game's small HUD fonts at a PS1 screen position, for the
 * shell (LOCK and the like). big: 6 x 7 letters, else 4 x 5. */
void battle_hud_text(float hud_w, int frame, int anchor, float x, float y, const char *text, int big,
                     uint8_t r, uint8_t g, uint8_t b);

/* The game's big round words (layer 2 of the HUD sheet), centred:
 * WIN, LOSE, DRAW, TIME UP. y is the top, PS1 screen lines. */
enum { BATTLE_HUD_WIN, BATTLE_HUD_LOSE, BATTLE_HUD_DRAW, BATTLE_HUD_TIME_UP };
void battle_hud_word(float hud_w, int frame, int word, float y, uint8_t r, uint8_t g, uint8_t b);

/* Lock-on markers on a target (VS 0x80057B8C guns, 0x80057E8C missiles),
 * centred on (hx, hy) in HUD units: the game's brackets (guns) or bars
 * (missiles), dark green before a lock and dark red once locked, with a
 * leader line to the distance or LOCK, and for missiles one numbered box
 * per missile locked on (up to 4). */
void battle_hud_lock_marker(float hud_w, int frame, float hx, float hy, int missile, int locked, int distance, int locks);
/* The missile lock cursor (VS 0x800580EC): the diamond, centred on (hx, hy). */
void battle_hud_missile_cursor(float hud_w, int frame, float hx, float hy);

#endif
