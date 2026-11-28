/* SYSTEM > SETTINGS > VIDEO and AUDIO, kept in settings.toml beside the
 * save. VIDEO: fullscreen or a window, the window's size, the fight HUD's
 * frame (4:3 or wide; F9 in a fight) and smooth or stepped animation (F5).
 * AUDIO: fight sounds and menu sounds, 0..10 each. AI PILOTS: the
 * computer pilots' brain (ORIGINAL or ACR UTILITY (HARD)) and the original
 * brain's style (each opponent's own disc, Phantasma or Master of Arena);
 * docs/AI.md. Changes apply at once;
 * main.c reads the values and puts them into effect (settings_ui.apply). */
#ifndef SETTINGS_H
#define SETTINGS_H

#include "garage.h"

#define SETTINGS_WINDOW_SIZES 5

typedef struct game_settings {
    int fullscreen;    /* 0 window, 1 fullscreen */
    int window;        /* index into settings_window_size */
    int hud_wide;      /* 0 4:3, 1 wide */
    int smooth_anim;   /* 1 smooth, 0 stepped */
    int fx_volume;     /* 0..10 */
    int menu_volume;   /* 0..10 */
    int ai_utility;    /* 0 the original arena AI, 1 the ACR utility brain everywhere */
    int ai_style;      /* ACR_GEN1_STYLE_*: 0 own disc, 1 Phantasma, 2 Master of Arena */
} game_settings;

#define SETTINGS_PAGE_AI 2

extern game_settings g_settings;

void settings_defaults(void);
void settings_load(void);
int  settings_save(void);
void settings_window_size(int index, int *w, int *h);

typedef struct settings_ui {
    int page;          /* 0 VIDEO, 1 AUDIO, 2 AI PILOTS */
    int cur;
    int apply;         /* set when a value changed: main.c applies g_settings and clears it */
    char message[80];
    int message_ms;
} settings_ui;

void settings_open(settings_ui *ui, int page);
/* Returns 1 when the page is left (saved). */
int  settings_input(settings_ui *ui, garage_action a);
void settings_draw(settings_ui *ui, float hud_w, int elapsed_ms);

#endif
