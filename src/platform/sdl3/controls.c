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

void controls_defaults(void) { memcpy(g_bind, k_default, sizeof g_bind); }

void controls_load(void) {
    char path[1200];
    file_path(path, sizeof path);
    char *t = SDL_LoadFile(path, NULL);
    if (!t) return;
    for (int a = 0; a < CT_COUNT; ++a) {
        const size_t n = SDL_strlen(k_key[a]);
        for (const char *p = t; p && *p; ) {
            if (!SDL_strncmp(p, k_key[a], n) && (p[n] == ' ' || p[n] == '=')) {
                const char *v = p + n;
                while (*v == ' ' || *v == '=') ++v;
                const int code = SDL_atoi(v);
                if (bindable(code)) g_bind[a] = code;
                break;
            }
            p = SDL_strchr(p, '\n');
            if (p) ++p;
        }
    }
    SDL_free(t);
}

int controls_save(void) {
    char path[1200], text[1024];
    file_path(path, sizeof path);
    int n = SDL_snprintf(text, sizeof text,
                         "# Armored-CoreR controller mapping (the CONTROLS page writes this).\n"
                         "# Numbers are SDL gamepad buttons; 100 = left trigger, 101 = right trigger.\n");
    for (int a = 0; a < CT_COUNT && n < (int)sizeof text - 64; ++a)
        n += SDL_snprintf(text + n, sizeof text - (size_t)n, "%s = %d   # %s\n", k_key[a], g_bind[a], input_name(g_bind[a]));
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

#define ROW_RESET CT_COUNT
#define ROW_BACK  (CT_COUNT + 1)
#define ROWS      (CT_COUNT + 2)

static void say(controls_ui *ui, const char *t) {
    SDL_strlcpy(ui->message, t, sizeof ui->message);
    ui->message_ms = 2500;
}

void controls_open(controls_ui *ui) {
    memset(ui, 0, sizeof *ui);
}

int controls_input(controls_ui *ui, garage_action a) {
    if (a == GA_NONE || ui->waiting) return 0;
    if (a == GA_UP) ui->cur = (ui->cur + ROWS - 1) % ROWS;
    if (a == GA_DOWN) ui->cur = (ui->cur + 1) % ROWS;
    if (a == GA_CANCEL) { controls_save(); return 1; }
    if (a != GA_OK) return 0;
    if (ui->cur < CT_COUNT) { ui->waiting = 1; return 0; }
    if (ui->cur == ROW_RESET) { controls_defaults(); controls_save(); say(ui, "BACK TO THE STANDARD LAYOUT."); return 0; }
    controls_save();
    return 1;
}

int controls_capture(controls_ui *ui, const SDL_Event *e) {
    if (!ui->waiting) return 0;
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
    acr_hud_text(lx, 16, "CONTROLS: CONTROLLER", HILITE);
    acr_hud_rect(lx, 40, w, 60 + ROWS * 22.0f, 10, 14, 20, 210, 1);
    acr_hud_rect(lx, 40, w, 60 + ROWS * 22.0f, 70, 90, 100, 255, 0);
    acr_hud_text(lx + 16, 52, "MOVE AND TURN: LEFT STICK OR D-PAD (FIXED)", DIM);
    for (int r = 0; r < ROWS; ++r) {
        const float y = 76 + r * 22.0f;
        const int sel = r == ui->cur;
        if (sel) acr_hud_rect(lx + 8, y - 4, w - 16, 16, 60, 70, 40, 255, 1);
        if (r < CT_COUNT) {
            acr_hud_text(lx + 16, y, k_action[r], sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
            if (sel && ui->waiting) acr_hud_text(lx + 220, y, "PRESS A BUTTON...", BAD);
            else acr_hud_text(lx + 220, y, input_name(g_bind[r]), sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
        } else {
            acr_hud_text(lx + 16, y, r == ROW_RESET ? "STANDARD LAYOUT" : "BACK", sel ? 255 : 220, sel ? 220 : 230, sel ? 120 : 235);
        }
    }
    const float fy = 76 + ROWS * 22.0f + 4;
    acr_hud_text(lx + 16, fy, ui->waiting ? "BACK / ESC: CANCEL" : "ENTER / A: CHANGE    ESC / B: BACK (SAVED)", DIM);
    if (ui->message_ms > 0 && ui->message[0]) acr_hud_text(lx + 16, fy + 20, ui->message, HILITE);
    (void)hud_w;
}
