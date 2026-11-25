/* Controller and keyboard mapping: which pad button and which key does
 * what in a fight, changeable on the CONTROLS page (SYSTEM > SETTINGS) and
 * kept in controls.toml beside the save. Pad: moving and turning stay on
 * the left stick and d-pad; Start and Back stay the game's own (reset,
 * menus, the test build's Back + button combinations). Keyboard: every
 * fight action has one key; the arrow keys always move and turn too, and
 * Esc, Enter, Backspace, the F and number keys and the test build's letter
 * keys (R, T, B, G, C, L, H) keep their jobs. Menus are not remapped. */
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

/* Keyboard actions (player 1). */
enum {
    CK_FORWARD, CK_BACKWARD, CK_TURN_L, CK_TURN_R, CK_STRAFE_L, CK_STRAFE_R, CK_BOOST, CK_FIRE,
    CK_SWITCH, CK_BLADE, CK_TARGET, CK_LOOK_UP, CK_LOOK_DOWN,
    CK_COUNT
};
/* The key is held (arrow keys count for moving and turning). */
int controls_key_held(int action);
/* This event presses the key (a key going down, not a repeat). */
int controls_key_pressed(const SDL_Event *e, int action);

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
    int tab;                   /* 0 controller, 1 keyboard */
    int cur;
    int waiting;               /* waiting for a button (or key) for row `cur` */
    char message[80];
    int message_ms;
} controls_ui;

void controls_open(controls_ui *ui);
/* Menu input (not while waiting); left / right (or LB / RB) switch between
 * the controller and keyboard tabs. Returns 1 when the page is left. */
int  controls_input(controls_ui *ui, garage_action a);
/* While waiting: every event goes here first. Returns 1 if it used it. */
int  controls_capture(controls_ui *ui, const SDL_Event *e);
void controls_draw(controls_ui *ui, float hud_w, int elapsed_ms);

#endif
