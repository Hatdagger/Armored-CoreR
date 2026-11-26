#include "controls.h"

#include <string.h>

#include "../../render/acr_render.h"

#define WHITE 220, 230, 235
#define DIM 120, 135, 145
#define HILITE 255, 220, 120
#define BAD 255, 90, 70

static const char *const k_action[CT_COUNT] = {
    "BOOST", "FIRE", "SWITCH WEAPON", "BLADE", "STRAFE LEFT", "STRAFE RIGHT", "TILT VIEW UP", "TILT VIEW DOWN",
};
static const char *const k_key[CT_COUNT] = {
    "boost", "fire", "switch", "blade", "strafe_left", "strafe_right", "look_up", "look_down",
};
/* The test build's own layout (README): A boost, X fire, Y switch, B blade,
 * LB / RB strafe, L2 / R2 tilt. */
static const int k_default[CT_COUNT] = {
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, CT_LEFT_TRIGGER, CT_RIGHT_TRIGGER,
};
static int g_bind[CT_COUNT] = {
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, CT_LEFT_TRIGGER, CT_RIGHT_TRIGGER,
};

#define TRIGGER_ON 8000

/* ---- keyboard ---- */

static const char *const k_key_action[CK_COUNT] = {
    "FORWARD", "BACK", "TURN LEFT", "TURN RIGHT", "STRAFE LEFT", "STRAFE RIGHT", "BOOST", "FIRE",
    "SWITCH WEAPON", "BLADE", "SWITCH TARGET", "TILT VIEW UP", "TILT VIEW DOWN",
};
static const char *const k_key_key[CK_COUNT] = {
    "key_forward", "key_back", "key_turn_left", "key_turn_right", "key_strafe_left", "key_strafe_right",
    "key_boost", "key_fire", "key_switch", "key_blade", "key_target", "key_look_up", "key_look_down",
};
/* The test build's keyboard layout (README). */
static const int k_key_default[CK_COUNT] = {
    SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_Q, SDL_SCANCODE_E,
    SDL_SCANCODE_SPACE, SDL_SCANCODE_F, SDL_SCANCODE_TAB, SDL_SCANCODE_V, SDL_SCANCODE_X,
    SDL_SCANCODE_PAGEUP, SDL_SCANCODE_PAGEDOWN,
};
static int g_key[CK_COUNT] = {
    SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_Q, SDL_SCANCODE_E,
    SDL_SCANCODE_SPACE, SDL_SCANCODE_F, SDL_SCANCODE_TAB, SDL_SCANCODE_V, SDL_SCANCODE_X,
    SDL_SCANCODE_PAGEUP, SDL_SCANCODE_PAGEDOWN,
};
/* The arrow keys move and turn whatever the mapping. */
static const int k_key_fixed[CK_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
};

/* Keys that keep their jobs: the arrows, menus and leaving (Esc, Enter,
 * Backspace), the F and number keys and the test build's letter keys. */
static int key_bindable(int sc) {
    if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT) return 0;
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return 0;
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return 0;
    switch (sc) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN: case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_R: case SDL_SCANCODE_T: case SDL_SCANCODE_B: case SDL_SCANCODE_G:
    case SDL_SCANCODE_C: case SDL_SCANCODE_L: case SDL_SCANCODE_H:
        return 0;
    default:
        return SDL_GetScancodeName((SDL_Scancode)sc)[0] != 0;
    }
}

static const char *key_name(int sc) {
    static char buf[32];
    const char *n = SDL_GetScancodeName((SDL_Scancode)sc);
    size_t i = 0;
    for (; n[i] && i + 1 < sizeof buf; ++i) buf[i] = (char)SDL_toupper((unsigned char)n[i]);
    buf[i] = 0;
    return buf[0] ? buf : "?";
}

int controls_key_held(int action) {
    if (action < 0 || action >= CK_COUNT) return 0;
    const bool *k = SDL_GetKeyboardState(NULL);
    return k[g_key[action]] || (k_key_fixed[action] && k[k_key_fixed[action]]);
}

int controls_key_pressed(const SDL_Event *e, int action) {
    if (action < 0 || action >= CK_COUNT || e->type != SDL_EVENT_KEY_DOWN || e->key.repeat) return 0;
    return (int)e->key.scancode == g_key[action];
}

static const char *input_name(int code) {
    switch (code) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A / CROSS";
    case SDL_GAMEPAD_BUTTON_EAST: return "B / CIRCLE";
    case SDL_GAMEPAD_BUTTON_WEST: return "X / SQUARE";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y / TRIANGLE";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB / L1";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB / R1";
    case CT_LEFT_TRIGGER: return "LT / L2";
    case CT_RIGHT_TRIGGER: return "RT / R2";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3 (LEFT STICK)";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3 (RIGHT STICK)";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "GUIDE";
    case SDL_GAMEPAD_BUTTON_MISC1: return "MISC / SHARE";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "PADDLE R1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "PADDLE L1";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "PADDLE R2";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "PADDLE L2";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "TOUCHPAD";
    default: return "?";
    }
}

/* Start, Back and the d-pad keep their jobs (menus, reset, moving). */
static int bindable(int code) {
    if (code == CT_LEFT_TRIGGER || code == CT_RIGHT_TRIGGER) return 1;
    if (code < 0 || code >= SDL_GAMEPAD_BUTTON_COUNT) return 0;
    return code != SDL_GAMEPAD_BUTTON_START && code != SDL_GAMEPAD_BUTTON_BACK &&
           code != SDL_GAMEPAD_BUTTON_DPAD_UP && code != SDL_GAMEPAD_BUTTON_DPAD_DOWN &&
           code != SDL_GAMEPAD_BUTTON_DPAD_LEFT && code != SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
}

/* ---- file ---- */

static void file_path(char *out, size_t size) {
    const char *env = SDL_getenv("ACR_CONTROLS");
    if (env && env[0]) { SDL_strlcpy(out, env, size); return; }
    char *pref = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
    SDL_snprintf(out, size, "%scontrols.toml", pref ? pref : "");
    SDL_free(pref);
}

void controls_defaults(void) {
    memcpy(g_bind, k_default, sizeof g_bind);
    memcpy(g_key, k_key_default, sizeof g_key);
}

/* The number after "key =" at the start of a line, or `fallback`. */
static int file_value(const char *t, const char *key, int fallback) {
    const size_t n = SDL_strlen(key);
    for (const char *p = t; p && *p; ) {
        if (!SDL_strncmp(p, key, n) && (p[n] == ' ' || p[n] == '=')) {
            const char *v = p + n;
            while (*v == ' ' || *v == '=') ++v;
            return SDL_atoi(v);
        }
        p = SDL_strchr(p, '\n');
        if (p) ++p;
    }
    return fallback;
}

void controls_load(void) {
    char path[1200];
    file_path(path, sizeof path);
    char *t = SDL_LoadFile(path, NULL);
    if (!t) return;
    for (int a = 0; a < CT_COUNT; ++a) {
        const int code = file_value(t, k_key[a], -1);
        if (bindable(code)) g_bind[a] = code;
    }
    /* Keyboard keys (files from before keyboard remapping have none: the
     * standard keys stay). A key given twice keeps only its first action. */
    int key[CK_COUNT];
    memcpy(key, g_key, sizeof key);
    for (int a = 0; a < CK_COUNT; ++a) {
        const int sc = file_value(t, k_key_key[a], -1);
        if (key_bindable(sc)) key[a] = sc;
    }
    int clash = 0;
    for (int a = 0; a < CK_COUNT; ++a)
        for (int b = 0; b < a; ++b) clash |= key[a] == key[b];
    if (!clash) memcpy(g_key, key, sizeof g_key);
    SDL_free(t);
}

int controls_save(void) {
    char path[1200], text[3072];
    file_path(path, sizeof path);
    int n = SDL_snprintf(text, sizeof text,
                         "# Armored-CoreR controller and keyboard mapping (the CONTROLS page writes this).\n"
                         "# Controller: SDL gamepad buttons; 100 = left trigger, 101 = right trigger.\n");
    for (int a = 0; a < CT_COUNT && n < (int)sizeof text - 80; ++a)
        n += SDL_snprintf(text + n, sizeof text - (size_t)n, "%s = %d   # %s\n", k_key[a], g_bind[a], input_name(g_bind[a]));
    n += SDL_snprintf(text + n, sizeof text - (size_t)n, "# Keyboard: SDL scancodes.\n");
    for (int a = 0; a < CK_COUNT && n < (int)sizeof text - 80; ++a)
        n += SDL_snprintf(text + n, sizeof text - (size_t)n, "%s = %d   # %s\n", k_key_key[a], g_key[a], key_name(g_key[a]));
    return SDL_SaveFile(path, text, (size_t)n);
}

/* ---- reading the pad ---- */

int controls_held(SDL_Gamepad *pad, int action) {
    if (!pad || action < 0 || action >= CT_COUNT) return 0;
    const int c = g_bind[action];
    if (c == CT_LEFT_TRIGGER) return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_ON;
    if (c == CT_RIGHT_TRIGGER) return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_ON;
    return SDL_GetGamepadButton(pad, (SDL_GamepadButton)c);
}

/* Which triggers were past half way, per pad (a few pads at most). */
static struct { SDL_JoystickID id; uint8_t on[2]; } g_trig[8];

/* A trigger event: 1 if it was just pulled past half way. Updates the state. */
static int trigger_pulled(const SDL_Event *e, int *which) {
    if (e->type != SDL_EVENT_GAMEPAD_AXIS_MOTION) return 0;
    const int t = e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 0 : e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : -1;
    if (t < 0) return 0;
    int slot = -1;
    for (int i = 0; i < 8; ++i) if (g_trig[i].id == e->gaxis.which) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < 8; ++i) if (!g_trig[i].id) { slot = i; g_trig[i].id = e->gaxis.which; break; }
    if (slot < 0) slot = 0;
    const int on = e->gaxis.value > TRIGGER_ON, was = g_trig[slot].on[t];
    g_trig[slot].on[t] = (uint8_t)on;
    *which = t ? CT_RIGHT_TRIGGER : CT_LEFT_TRIGGER;
    return on && !was;
}

/* The same event can be asked about more than once (several actions): the
 * trigger state is updated once per event (told apart by its time stamp,
 * pad and axis). */
static int event_input(const SDL_Event *e) {
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) return e->gbutton.button;
    if (e->type != SDL_EVENT_GAMEPAD_AXIS_MOTION) return -1;
    static Uint64 last_ts;
    static SDL_JoystickID last_pad;
    static int last_axis = -1, last_result = -1;
    if (e->common.timestamp == last_ts && e->gaxis.which == last_pad && e->gaxis.axis == last_axis) return last_result;
    int which = -1;
    last_result = trigger_pulled(e, &which) ? which : -1;
    last_ts = e->common.timestamp;
    last_pad = e->gaxis.which;
    last_axis = e->gaxis.axis;
    return last_result;
}

int controls_pressed(const SDL_Event *e, int action) {
    if (action < 0 || action >= CT_COUNT) return 0;
    const int c = event_input(e);
    return c >= 0 && c == g_bind[action];
}

SDL_JoystickID controls_event_pad(const SDL_Event *e) {
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_UP) return e->gbutton.which;
    if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) return e->gaxis.which;
    return 0;
}

/* ---- the page ---- */

/* Rows: the tab's actions, then STANDARD LAYOUT and BACK. */
static int row_count(const controls_ui *ui) { return (ui->tab ? CK_COUNT : CT_COUNT) + 2; }
static int actions(const controls_ui *ui) { return ui->tab ? CK_COUNT : CT_COUNT; }

static void say(controls_ui *ui, const char *t) {
    SDL_strlcpy(ui->message, t, sizeof ui->message);
    ui->message_ms = 2500;
}

void controls_open(controls_ui *ui) {
    memset(ui, 0, sizeof *ui);
}

int controls_input(controls_ui *ui, garage_action a) {
    if (a == GA_NONE || ui->waiting) return 0;
    const int rows = row_count(ui);
    if (a == GA_UP) ui->cur = (ui->cur + rows - 1) % rows;
    if (a == GA_DOWN) ui->cur = (ui->cur + 1) % rows;
    if (a == GA_LEFT || a == GA_RIGHT || a == GA_PREV_TAB || a == GA_NEXT_TAB) {
        const int extra = ui->cur - actions(ui);   /* 0 STANDARD LAYOUT, 1 BACK, < 0 an action */
        ui->tab = !ui->tab;
        if (extra >= 0) ui->cur = actions(ui) + extra;
        else if (ui->cur >= actions(ui)) ui->cur = actions(ui) - 1;
        return 0;
    }
    if (a == GA_CANCEL) { controls_save(); return 1; }
    if (a != GA_OK) return 0;
    if (ui->cur < actions(ui)) { ui->waiting = 1; return 0; }
    if (ui->cur == actions(ui)) {   /* STANDARD LAYOUT, for this tab only */
        if (ui->tab) memcpy(g_key, k_key_default, sizeof g_key);
        else memcpy(g_bind, k_default, sizeof g_bind);
        controls_save();
        say(ui, "BACK TO THE STANDARD LAYOUT.");
        return 0;
    }
    controls_save();
    return 1;
}

/* Keyboard tab: the next key pressed. A key already in use swaps. */
static int capture_key(controls_ui *ui, const SDL_Event *e) {
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
        ui->waiting = 0; say(ui, "NOT CHANGED."); return 1;
    }
    if (e->type != SDL_EVENT_KEY_DOWN) return e->type == SDL_EVENT_KEY_UP || e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                                              e->type == SDL_EVENT_GAMEPAD_BUTTON_UP || e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    if (e->key.repeat) return 1;
    const int sc = (int)e->key.scancode;
    if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE) { ui->waiting = 0; say(ui, "NOT CHANGED."); return 1; }
    if (!key_bindable(sc)) { say(ui, "THAT KEY KEEPS ITS JOB: PICK ANOTHER."); return 1; }
    const int old = g_key[ui->cur];
    for (int a = 0; a < CK_COUNT; ++a)
        if (a != ui->cur && g_key[a] == sc) {
            g_key[a] = old;
            char line[80];
            SDL_snprintf(line, sizeof line, "SWAPPED: %s IS NOW %s.", k_key_action[a], key_name(old));
            say(ui, line);
        }
    g_key[ui->cur] = sc;
    ui->waiting = 0;
    controls_save();
    return 1;
}

int controls_capture(controls_ui *ui, const SDL_Event *e) {
    if (!ui->waiting) return 0;
    if (ui->tab) return capture_key(ui, e);
    /* Esc on the keyboard cancels. */
    if (e->type == SDL_EVENT_KEY_DOWN) {
        if (e->key.scancode == SDL_SCANCODE_ESCAPE || e->key.scancode == SDL_SCANCODE_BACKSPACE) {
            ui->waiting = 0;
            say(ui, "NOT CHANGED.");
        }
        return 1;
    }
    if (e->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->type != SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        return e->type == SDL_EVENT_GAMEPAD_BUTTON_UP;
    }
    const int code = event_input(e);
    if (code < 0) return 1;
    if (code == SDL_GAMEPAD_BUTTON_BACK) { ui->waiting = 0; say(ui, "NOT CHANGED."); return 1; }
    if (!bindable(code)) { say(ui, "START AND THE D-PAD KEEP THEIR JOBS: PICK ANOTHER."); return 1; }
    /* A button already in use swaps with this action's old one. */
    const int old = g_bind[ui->cur];
    for (int a = 0; a < CT_COUNT; ++a)
        if (a != ui->cur && g_bind[a] == code) {
            g_bind[a] = old;
            char line[80];
            SDL_snprintf(line, sizeof line, "SWAPPED: %s IS NOW %s.", k_action[a], input_name(old));
            say(ui, line);
        }
    g_bind[ui->cur] = code;
    ui->waiting = 0;
    controls_save();
    return 1;
}

void controls_draw(controls_ui *ui, float hud_w, int elapsed_ms) {
    if (ui->message_ms > 0) ui->message_ms -= elapsed_ms;
    const float lx = 24, w = 460;
    const int rows = row_count(ui), acts = actions(ui);
    const float step = ui->tab ? 20.0f : 22.0f;
    acr_hud_text(lx, 16, "CONTROLS:", HILITE);
    acr_hud_text(lx + 88, 16, ui->tab ? " CONTROLLER  [KEYBOARD]" : "[CONTROLLER]  KEYBOARD ", HILITE);
    acr_hud_rect(lx, 40, w, 60 + rows * step, 10, 14, 20, 210, 1);
    acr_hud_rect(lx, 40, w, 60 + rows * step, 70, 90, 100, 255, 0);
    acr_hud_text(lx + 16, 52, ui->tab ? "THE ARROW KEYS ALSO MOVE AND TURN (FIXED)" : "MOVE AND TURN: LEFT STICK OR D-PAD (FIXED)", DIM);
    for (int r = 0; r < rows; ++r) {
        const float y = 76 + r * step;
        const int sel = r == ui->cur;
        if (sel) acr_hud_rect(lx + 8, y - 4, w - 16, 16, 60, 70, 40, 255, 1);
        if (r < acts) {
            acr_hud_text(lx + 16, y, ui->tab ? k_key_action[r] : k_action[r], sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
            if (sel && ui->waiting) acr_hud_text(lx + 220, y, ui->tab ? "PRESS A KEY..." : "PRESS A BUTTON...", BAD);
            else acr_hud_text(lx + 220, y, ui->tab ? key_name(g_key[r]) : input_name(g_bind[r]),
                              sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
        } else {
            acr_hud_text(lx + 16, y, r == acts ? "STANDARD LAYOUT" : "BACK", sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
        }
    }
    const float fy = 76 + rows * step + 4;
    acr_hud_text(lx + 16, fy, ui->waiting ? "BACK / ESC: CANCEL"
                                          : "ENTER / A: CHANGE   LEFT / RIGHT: TAB   ESC / B: BACK", DIM);
    if (ui->message_ms > 0 && ui->message[0]) acr_hud_text(lx + 16, fy + 20, ui->message, HILITE);
    (void)hud_w;
}
