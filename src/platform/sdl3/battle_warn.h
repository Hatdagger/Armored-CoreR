/* The fight's warnings, timed as Master of Arena times them: when each
 * warning box is lit and when each warning beep plays. Read from the VS
 * engine's HUD routine (FDAT 209, 0x8005675C; the arena engine's copy, FDAT
 * 210, does the same with its own bar height), see docs/feel/sound-hud.md.
 *
 * The game runs this once a game tick (22 a second: one HUD frame per logic
 * step), off a frame counter that never resets, so the blinking boxes keep
 * one rhythm for the whole fight. Presentation only: no SDL, nothing here
 * changes the fight. One per AC; the HUD reads the boxes of the AC it shows,
 * the sounds play the beeps of the AC that is listening. */
#ifndef BATTLE_WARN_H
#define BATTLE_WARN_H

#include <stdint.h>

/* The arena HUD's energy bar is 157 PS1 pixels tall (FDAT 210 0x80057020:
 * energy x 157 / capacity); the red-zone beep counts in those pixels. */
#define BATTLE_WARN_BAR 157

/* Beeps due this tick (sample numbers: docs/research/gen1-sound.md). */
enum {
    BATTLE_WARN_BEEP_PERIMETER = 1,   /* sound 8, #27, volume 90 */
    BATTLE_WARN_BEEP_AP_LOW    = 2,   /* sound 9, #28, volume 90 */
    BATTLE_WARN_BEEP_RED_ZONE  = 4,   /* sound 41, #29, volume 100 */
    BATTLE_WARN_BEEP_DRAINED   = 8    /* sound 40, #30, volume 90 */
};

/* Boxes lit this tick. */
enum {
    BATTLE_WARN_BOX_LEAVING_AREA = 1,
    BATTLE_WARN_BOX_ARMOR_LOW    = 2,
    BATTLE_WARN_BOX_CHARGING     = 4
};

typedef struct battle_warn_in {
    int live;                 /* fighting: AP above 0 and the round not over */
    int ap, ap_max;
    int energy, energy_max, red_zone;
    int drained;              /* over-drained: the booster is locked until the bar is full */
    int out_of_area;          /* in the warning zone (acr_gen1_battle_out_of_area == 1) */
    int edge;                 /* world units to the nearest edge of the zone it may not leave; < 0 unknown */
    int burning;              /* [Gen 2] burning (acr_gen1_fighter burn_steps): Gen 1 has no heat
                               * display, so it flashes ARMOR LOW with its beep, as low AP does */
} battle_warn_in;

typedef struct battle_warn {
    uint32_t frame;           /* the game's HUD frame counter (0x801A8C34) */
    int16_t  area_count, area_on;   /* player block +0x248 / +0x24A */
    int16_t  red_count, red_prev;   /* +0x244 / +0x246 */
    unsigned boxes;           /* BATTLE_WARN_BOX_*, as of the latest tick */
} battle_warn;

void battle_warn_reset(battle_warn *w);

/* One game tick. Returns the beeps due (BATTLE_WARN_BEEP_*); w->boxes holds
 * the boxes to draw until the next tick. */
unsigned battle_warn_tick(battle_warn *w, const battle_warn_in *in);

/* The game's low-AP test (0x80056BF0): AP at or below a tenth of the
 * AC's full AP. ARMOR LOW and its beep use it. */
int battle_warn_ap_low(int ap, int ap_max);

/* HIT / DAMAGED blink on their own countdown (player block +0x24C /
 * +0x24E, ticks left): lit while (left >> 2) & 3 is not 0, so 12 of every
 * 16 ticks. */
int battle_warn_blink(int ticks_left);

#endif
