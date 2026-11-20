/* Controller mapping: which pad button does what in a fight, changeable
 * on the CONTROLS page (garage menu) and kept in controls.toml beside the
 * save. Moving and turning stay on the left stick and d-pad; Start and
 * Back stay the game's own (reset, menus, the test build's Back + button
 * combinations). The keyboard is not remapped. */
#ifndef CONTROLS_H
#define CONTROLS_H

#include <SDL3/SDL.h>

#include "garage.h"

enum {
    CT_BOOST, CT_FIRE, CT_SWITCH, CT_BLADE, CT_STRAFE_L, CT_STRAFE_R, CT_LOOK_UP, CT_LOOK_DOWN,
    CT_COUNT
};
/* A binding is a pad button (SDL_GamepadButton) or one of the triggers. */
#define CT_LEFT_TRIGGER  100
#define CT_RIGHT_TRIGGER 101

void controls_load(void);
int  controls_save(void);
void controls_defaults(void);

/* The action is held on this pad. */
int controls_held(SDL_Gamepad *pad, int action);
/* This event presses the action (a button going down, or a trigger
 * pulled past half way). */
int controls_pressed(const SDL_Event *e, int action);
/* The pad an event came from (button or axis events), 0 if neither. */
SDL_JoystickID controls_event_pad(const SDL_Event *e);

/* ---- the CONTROLS page ---- */
typedef struct controls_ui {
    int cur;
    int waiting;               /* waiting for a button for row `cur` */
    char message[80];
    int message_ms;
} controls_ui;

void controls_open(controls_ui *ui);
/* Menu input (not while waiting). Returns 1 when the page is left. */
int  controls_input(controls_ui *ui, garage_action a);
/* While waiting: every event goes here first. Returns 1 if it used it. */
int  controls_capture(controls_ui *ui, const SDL_Event *e);
void controls_draw(controls_ui *ui, float hud_w, int elapsed_ms);

#endif
