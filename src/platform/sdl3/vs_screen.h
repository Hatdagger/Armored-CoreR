/* The VS screen before a fight (AC1Recomp's, design
 * docs/rogue-arena-v2-design.md), shared by Rogue Arena and the Arena:
 * the player's AC (blue, left) and the opponent (red, right, its emblem
 * behind it), FOR RANK n, VS, "UNITS DEPLOY IN 5..1" at a second a step,
 * then the hangar shutters close, it fades to black and the fight starts.
 * Each mode fills the bottom band itself (Rogue Arena: the strip of small
 * boxes; the Arena: its ladder, a row per rank). Also the drawing bits the
 * two modes share and the opponents' emblems from the import. */
#ifndef VS_SCREEN_H
#define VS_SCREEN_H

#include "rogue.h"

/* Timing (seconds since the screen opened): the count starts at VS_C0 and
 * steps every VS_STEP; the shutters start at VS_SHUT_AT. */
#define VS_C0        0.6f
#define VS_STEP      1.0f
#define VS_SHUT_AT   (VS_C0 + 5.0f * VS_STEP)

/* The bottom band's contents, drawn under the count: `t` seconds since the
 * screen opened, `sx` the horizontal scale of the layout (1 at 854 wide). */
typedef void (*vs_strip_fn)(float hud_w, float sx, float t, const uint8_t band[3]);

typedef struct vs_setup {
    const rogue_fight *fight;        /* the two ACs, their colours and "AC / PILOT" names, the stage */
    const char *mode;                /* top left ("ROGUE ARENA", "ARENA") */
    const char *right;               /* top right, in the band colour (the belt, the arena) */
    uint8_t band[3];                 /* the band's colour (the belt's) */
    int rank[2];                     /* RANK n badges on the two boxes, 0 none */
    uint32_t emblem[2];              /* behind each AC, 0 none */
    int for_rank;                    /* FOR RANK n in the middle, 0 none */
    const char *tag;                 /* under VS ("GHOST", "REMATCH"), NULL none */
    const char *stage;               /* under the count */
    const char *hint;                /* the bottom line */
    int shots;                       /* rifle shots on counts 3, 2, 1 (the last `shots` of them, 0-3) */
    vs_strip_fn strip;
} vs_setup;

/* main.c's AC drawer (rogue_set_ac_drawer and arena_set_ac_drawer pass it on). */
void vs_set_ac_drawer(rogue_ac_drawer fn);
void vs_draw_ac(const int parts[SLOT_COUNT], const uint8_t colours[4][3], float x, float y, float w, float h,
                float frac, float yaw, int lying, float shade);

/* Opens the screen (the setup is copied; `fight` must stay valid). */
void vs_open(const vs_setup *s);
/* Draws it over whatever backdrop the mode drew (dimmed to 42 %). */
void vs_draw(float hud_w, int elapsed_ms);
/* Skip the count: the shutters close now. */
void vs_skip(void);
/* The shutters are closing (input does nothing then). */
int vs_closing(void);
/* The screen is done: start the fight. */
int vs_due(void);

/* An opponent's emblem from the import (acr-import: arena/<AA-name>/
 * <RR>-emblem.tim, a 4-bit 64 x 64 TIM, colour 0 see-through); kept once
 * loaded. 0 = none. */
uint32_t vs_emblem(const char *import_dir, int arena, int rank);
/* An emblem drawn into the box (x, y, w, h), clipped to it, at `alpha`. */
void vs_emblem_draw(uint32_t tex, float x, float y, float w, float h, float size, float off_x, uint8_t alpha);

/* Shared drawing bits (HUD grid units, 480 high). */
void vs_text_at(float x, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b);
float vs_text_w(const char *t, float size);
void vs_text_mid(float cx, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b);
void vs_text_right(float rx, float y, const char *t, float size, uint8_t r, uint8_t g, uint8_t b);
/* A box filled top to bottom from colour c at k0 to k1 of its brightness. */
void vs_gradient(float x, float y, float w, float h, const uint8_t c[3], float k0, float k1);
/* The mock's frame: a dark rim, then `t` units in colour. */
void vs_frame(float x, float y, float w, float h, const uint8_t c[3], float t);
void vs_thick_line(float x0, float y0, float x1, float y1, float t, uint8_t r, uint8_t g, uint8_t b);
/* A soft round dot of colour (smoke, sparks). */
void vs_soft_dot(float cx, float cy, float rad, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

extern const uint8_t vs_blue[3], vs_red[3];

#endif
