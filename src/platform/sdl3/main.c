/* PC shell (SDL3), test build: one Gen 1 AC in an empty arena.
 *
 * The AC's stats and walking animation come from the player's own
 * acr-import output (folder given on the command line, or "import" next to
 * the program). Movement runs at Gen 1's 22 steps a second; drawing is
 * smoothed between steps at the screen's own rate. The AC is a stand-in
 * made of boxes until the real models are imported.
 *
 *   armored-corer [import folder | disc.cue] [--legs N] [--booster N]
 *                 [--core N] [--arms N] [--gen N] (Master of Arena indices)
 *                 [--tick N]  steps a second (default 60 for now; 22 = Gen 1)
 *
 * With no import yet it asks for the disc: drop the .cue or .bin on the
 * window (or on the program) and it imports it next to the program. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/acr_clock.h"
#include "../../core/acr_gen1.h"
#include "../../core/acr_pools.h"
#include "demo_data.h"
#include "acr_import.h"
#include "../../render/acr_render.h"
#include "../../render/acr_light.h"

/* HUD grid of the view being drawn: 480 high, width set by its shape. */
static float g_vw = 640, g_vh = 480;
#define ARENA  20000          /* half width, world units */

/* ---------------- input ---------------- */

static SDL_Gamepad *open_first_gamepad(void) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_Gamepad *pad = (ids && count > 0) ? SDL_OpenGamepad(ids[0]) : NULL;
    SDL_free(ids);
    return pad;
}

static acr_gen1_input read_input(SDL_Gamepad *pad, int keyboard) {
    const bool *k = SDL_GetKeyboardState(NULL);
    acr_gen1_input in = { 0, 0, 0, 0, 0 };
    if (keyboard) {
    in.move = (int8_t)((k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]));
    in.turn = (int8_t)((k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]));
    in.strafe = (int8_t)(k[SDL_SCANCODE_E] - k[SDL_SCANCODE_Q]);
    in.boost = (uint8_t)(k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J]);
    }
    if (pad) {
        const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        const int ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        const int rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
        const int dead = 12000;
        if (ly < -dead || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) in.move = 1;
        if (ly > dead || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) in.move = -1;
        if (lx < -dead || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) in.turn = -1;
        if (lx > dead || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) in.turn = 1;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || rx < -dead) in.strafe = -1;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) || rx > dead) in.strafe = 1;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH) ||
            SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000)
            in.boost = 1;
    }
    return in;
}

/* ---------------- the 3D view ----------------
 * Scenes are built on the CPU each frame: faces lit like the PS1 (flat,
 * per face, acr_light.h), then drawn by the renderer with depth and fog. */

typedef struct vec3 { float x, y, z; } vec3;
static vec3 v3(float x, float y, float z) { vec3 v = { x, y, z }; return v; }

#define MAX_TRIS  (4096 * 3)
#define MAX_LINES (1024 * 2)
static acr_vtx g_tris[MAX_TRIS], g_lines[MAX_LINES];
static int g_tri_count, g_line_count;
static acr_lighting g_light;

static acr_vtx vtx(vec3 p, const uint8_t c[3]) {
    acr_vtx o = { p.x, p.y, p.z, c[0], c[1], c[2], 255, 0, 0 };
    return o;
}

static void line3(vec3 a, vec3 b, uint8_t r, uint8_t g, uint8_t bl) {
    if (g_line_count + 2 > MAX_LINES) return;
    const uint8_t c[3] = { r, g, bl };
    g_lines[g_line_count++] = vtx(a, c);
    g_lines[g_line_count++] = vtx(b, c);
}

/* A box turned by yaw about y; colour 0..1 like before. */
static void add_box(vec3 centre, float yaw, vec3 half, float cr, float cg, float cb) {
    const float s = sinf(yaw), co = cosf(yaw);
    vec3 corner[8];
    for (int i = 0; i < 8; ++i) {
        const float lx = (i & 1) ? half.x : -half.x, ly = (i & 2) ? half.y : -half.y,
                    lz = (i & 4) ? half.z : -half.z;
        /* local x right, z forward -> world (forward = (sin, cos), right = (cos, -sin)) */
        corner[i] = v3(centre.x + lx * co + lz * s, centre.y + ly, centre.z - lx * s + lz * co);
    }
    static const int quads[6][4] = { {0,1,3,2}, {4,6,7,5}, {0,4,5,1}, {2,3,7,6}, {0,2,6,4}, {1,5,7,3} };
    /* local normals: -z, +z, -y, +y, -x, +x */
    static const float normal[6][3] = { {0,0,-1}, {0,0,1}, {0,-1,0}, {0,1,0}, {-1,0,0}, {1,0,0} };
    const uint8_t base[3] = { (uint8_t)(cr * 255), (uint8_t)(cg * 255), (uint8_t)(cb * 255) };
    for (int f = 0; f < 6 && g_tri_count + 6 <= MAX_TRIS; ++f) {
        const float nx = normal[f][0] * co + normal[f][2] * s, ny = normal[f][1],
                    nz = -normal[f][0] * s + normal[f][2] * co;
        uint8_t c[3];
        acr_light_color(&g_light, (int32_t)(nx * 4096), (int32_t)(ny * 4096), (int32_t)(nz * 4096),
                        base[0], base[1], base[2], c);
        const int *q = quads[f];
        g_tris[g_tri_count++] = vtx(corner[q[0]], c);
        g_tris[g_tri_count++] = vtx(corner[q[1]], c);
        g_tris[g_tri_count++] = vtx(corner[q[2]], c);
        g_tris[g_tri_count++] = vtx(corner[q[0]], c);
        g_tris[g_tri_count++] = vtx(corner[q[2]], c);
        g_tris[g_tri_count++] = vtx(corner[q[3]], c);
    }
}

static void flush_scene(void) {
    if (g_line_count) acr_render_lines(g_lines, g_line_count);
    if (g_tri_count) acr_render_tris(g_tris, g_tri_count, 0);
    g_tri_count = g_line_count = 0;
}

/* Stand-in AC, about 700 units tall, facing +z locally. */
static void add_ac(vec3 p, float yaw, float crouch, int enemy) {
    const float s = sinf(yaw), co = cosf(yaw);
    #define AT(lx, ly, lz) v3(p.x + (lx) * co + (lz) * s, p.y + (ly), p.z - (lx) * s + (lz) * co)
    #define BODY  (enemy ? 0.55f : 0.62f), (enemy ? 0.35f : 0.66f), (enemy ? 0.35f : 0.70f)
    #define TRIM  (enemy ? 0.85f : 0.95f), (enemy ? 0.2f : 0.62f), (enemy ? 0.2f : 0.15f)
    #define DARK  0.30f, 0.33f, 0.36f
    const float leg_h = 340.0f - crouch;
    add_box(AT(-120, leg_h * 0.5f, 0), yaw, v3(55, leg_h * 0.5f, 70), DARK);
    add_box(AT(120, leg_h * 0.5f, 0), yaw, v3(55, leg_h * 0.5f, 70), DARK);
    add_box(AT(0, leg_h + 140, 0), yaw, v3(200, 140, 150), BODY);           /* core */
    add_box(AT(0, leg_h + 330, 40), yaw, v3(75, 50, 80), TRIM);             /* head */
    add_box(AT(-270, leg_h + 170, 30), yaw, v3(55, 110, 70), BODY);         /* arms */
    add_box(AT(270, leg_h + 170, 30), yaw, v3(55, 110, 70), BODY);
    add_box(AT(270, leg_h + 90, 190), yaw, v3(30, 30, 140), DARK);          /* rifle */
    add_box(AT(-110, leg_h + 300, -150), yaw, v3(60, 70, 60), TRIM);        /* back units */
    add_box(AT(110, leg_h + 300, -150), yaw, v3(60, 70, 60), TRIM);
    #undef AT
    #undef BODY
    #undef TRIM
    #undef DARK
}

/* World point -> HUD grid (hud_w x 480) through proj*view; 0 if behind. */
static int to_hud(const float proj[16], const float view[16], vec3 p, float hud_w, float *hx, float *hy) {
    float e[4], c[4];
    const float in[4] = { p.x, p.y, p.z, 1 };
    for (int r = 0; r < 4; ++r) e[r] = view[r] * in[0] + view[4 + r] * in[1] + view[8 + r] * in[2] + view[12 + r] * in[3];
    for (int r = 0; r < 4; ++r) c[r] = proj[r] * e[0] + proj[4 + r] * e[1] + proj[8 + r] * e[2] + proj[12 + r] * e[3];
    if (c[3] < 50.0f) return 0;
    *hx = (c[0] / c[3] * 0.5f + 0.5f) * hud_w;
    *hy = (0.5f - c[1] / c[3] * 0.5f) * 480.0f;
    return 1;
}

/* SDL hands GL functions back as function pointers; the renderer wants void *. */
static void *gl_proc(const char *name) {
    SDL_FunctionPointer f = SDL_GL_GetProcAddress(name);
    void *p;
    memcpy(&p, &f, sizeof p);
    return p;
}

/* ---------------- main ---------------- */

/* Index of the controller with this id in pads[], or `fallback`. */
static int player_of(SDL_Gamepad *const pads[2], SDL_JoystickID id, int fallback) {
    for (int k = 0; k < 2; ++k)
        if (pads[k] && SDL_GetGamepadID(pads[k]) == id) return k;
    return fallback;
}

static int g_tick_rate = 60;   /* test: steps a second; 22 = Gen 1's own pace */
static const int k_tick_rates[] = { 22, 30, 45, 60 };

/* Starts the clock at the chosen rate. Faster than Gen 1, movement per step
 * stays as the game has it (so it runs faster), while energy and recovery
 * times are kept per second. */
static void set_rate(acr_clock *clock, acr_gen1_ac_def *def, int rate) {
    if (rate == 22 || rate <= 0) {
        acr_clock_init_ns(clock, ACR_GEN1_TICK_NS, 8);
        def->step_rate = def->gen1_rate = 0;
    } else {
        acr_clock_init(clock, (uint32_t)rate, 8);
        def->step_rate = (uint16_t)rate;
        def->gen1_rate = 22;
    }
}

/* Next part index in a slot that exists in the import (wraps around). */
static int next_part(const char *dir, int parts[SLOT_COUNT], int slot, demo_ac *data) {
    const int start = parts[slot];
    char err[256];
    for (int step = 1; step <= 40; ++step) {
        parts[slot] = (start + step) % 40;
        if (demo_load(dir, parts, data, err, sizeof err)) return 1;
    }
    parts[slot] = start;
    return demo_load(dir, parts, data, err, sizeof err);
}

static int parse_args(int argc, char **argv, const char **dir, int parts[SLOT_COUNT]) {
    /* Master of Arena's starting AC. */
    static const int k_default[SLOT_COUNT] = { 0, 0, 5, 1, 0, 1, 0, 40, 0, 0, 3 };
    memcpy(parts, k_default, sizeof k_default);
    for (int i = 1; i < argc; ++i) {
        int slot = -1;
        if (!strcmp(argv[i], "--legs")) slot = SLOT_LEGS;
        else if (!strcmp(argv[i], "--booster")) slot = SLOT_BOOSTER;
        else if (!strcmp(argv[i], "--core")) slot = SLOT_CORE;
        else if (!strcmp(argv[i], "--arms")) slot = SLOT_ARMS;
        else if (!strcmp(argv[i], "--gen")) slot = SLOT_GEN;
        else if (!strcmp(argv[i], "--back")) slot = SLOT_BACK_L;
        if (!strcmp(argv[i], "--tick") && i + 1 < argc) { g_tick_rate = atoi(argv[++i]); continue; }
        if (slot >= 0 && i + 1 < argc) parts[slot] = atoi(argv[++i]);
        else if (argv[i][0] != '-') *dir = argv[i];
    }
    return 1;
}

static int is_disc(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot && (!SDL_strcasecmp(dot, ".cue") || !SDL_strcasecmp(dot, ".bin") || !SDL_strcasecmp(dot, ".iso"));
}

/* Shows a message (several lines), then presents. */
static void show_message(SDL_Window *w, const char *text) {
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    acr_render_begin(pw, ph, 18, 22, 28);
    acr_render_hud(0, 0, pw, ph);
    float y = 60;
    for (const char *p = text; *p; ) {
        const char *nl = strchr(p, '\n');
        const size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char line[96];
        SDL_snprintf(line, sizeof line, "%.*s", (int)(n < 95 ? n : 95), p);
        acr_hud_text(24, y, line, 220, 230, 235);
        y += 14;
        p += n;
        if (*p) ++p;
    }
    SDL_GL_SwapWindow(w);
}

/* Imports a disc into dir, showing progress text. */
static int import_disc(SDL_Window *r, const char *disc, const char *dir, char *err, int err_size) {
    show_message(r, "IMPORTING YOUR DISC...\n\nTHIS TAKES A MOMENT.");
    acr_import_stats stats;
    char why[512] = "";
    if (!acr_import_disc(disc, dir, &stats, why, sizeof why)) {
        SDL_snprintf(err, err_size, "Could not import that disc:\n%s", why);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    void *block = malloc(ACR_POOLS_TOTAL_BYTES);
    acr_pools pools;
    if (!block || !acr_pools_init(&pools, block, ACR_POOLS_TOTAL_BYTES)) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;

    char default_dir[1024];
    SDL_snprintf(default_dir, sizeof default_dir, "%simport", SDL_GetBasePath() ? SDL_GetBasePath() : "");
    const char *dir = default_dir;
    int parts[SLOT_COUNT];
    parse_args(argc, argv, &dir, parts);

    /* OpenGL 2.1 at the screen's own resolution, 4x anti-aliasing when the
     * driver offers it. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const SDL_WindowFlags wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_Window *window = SDL_CreateWindow("Armored-CoreR test build", 1280, 720, wflags);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : NULL;
    if (!gl) {
        if (window) SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow("Armored-CoreR test build", 1280, 720, wflags);
        gl = window ? SDL_GL_CreateContext(window) : NULL;
    }
    if (!gl || !acr_render_init(gl_proc)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Armored-CoreR",
                                 "This computer's graphics driver does not offer OpenGL 2.1.", window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    acr_light_default(&g_light);
    SDL_Gamepad *pad = open_first_gamepad();

    /* A disc given on the command line (or dropped on the program) is
     * imported next to the program first. */
    static demo_ac ac_data[2];   /* each player's build; large, so off the stack */
    int parts2[SLOT_COUNT];
    char err[1024] = "";
    if (is_disc(dir)) {
        const char *disc = dir;
        dir = default_dir;
        import_disc(window, disc, dir, err, sizeof err);
    }
    /* No usable import yet: ask for the disc, which can be dropped on the window. */
    int loaded = err[0] ? 0 : demo_load(dir, parts, &ac_data[0], err, sizeof err);
    while (!loaded) {
        char text[1600];
        SDL_snprintf(text, sizeof text,
                     "ARMORED-CORER TEST BUILD\n\n"
                     "DROP YOUR MASTER OF ARENA DISC (.CUE OR .BIN)\n"
                     "ON THIS WINDOW TO IMPORT IT.\n\n"
                     "ESC / BACK: QUIT\n\n%s", err);
        show_message(window, text);
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) continue;
        if (e.type == SDL_EVENT_QUIT ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) ||
            (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
            SDL_Quit();
            return 0;
        }
        if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
            if (!is_disc(e.drop.data)) {
                SDL_snprintf(err, sizeof err, "That is not a .cue, .bin or .iso file.");
                continue;
            }
            dir = default_dir;
            err[0] = 0;
            if (import_disc(window, e.drop.data, dir, err, sizeof err))
                loaded = demo_load(dir, parts, &ac_data[0], err, sizeof err);
        }
    }
    /* Player 2 starts with the same build; each player can change theirs. */
    memcpy(parts2, parts, sizeof parts2);
    demo_load(dir, parts2, &ac_data[1], err, sizeof err);
    int *const pp[2] = { parts, parts2 };
    acr_gen1_ac_def *def = &ac_data[0].def;
    acr_gen1_ac_def *def2 = &ac_data[1].def;

    acr_clock clock;
    set_rate(&clock, def, g_tick_rate);
    set_rate(&clock, def2, g_tick_rate);
    /* The fight: you (0) and a target AC (1) built the same way, standing
     * ahead and facing you. It does not move or shoot yet. */
    static acr_gen1_battle battle;
    memset(&battle, 0, sizeof battle);
    acr_gen1_ac *const P = &battle.f[0].ac;
    acr_gen1_ac *const T = &battle.f[1].ac;
    #define SETUP_FIGHTERS() do {                                                         \
        for (int i_ = 0; i_ < 2; ++i_) {                                                  \
            acr_gen1_fighter *f_ = &battle.f[i_];                                         \
            demo_ac *d_ = &ac_data[i_];                                                   \
            f_->def = &d_->def;                                                           \
            f_->ap = f_->ap_max = d_->ap;                                                 \
            f_->shell_factor = d_->shell_factor;                                          \
            f_->energy_factor = d_->energy_factor;                                        \
            f_->weapons = (uint8_t)(i_ == 0 || two_players ? d_->weapons : 0);             \
            for (int w_ = 0; w_ < d_->weapons; ++w_) {                                    \
                f_->weapon[w_] = d_->weapon[w_];                                          \
                f_->ammo[w_] = d_->weapon[w_].ammo;                                       \
            }                                                                             \
            f_->current = 0; f_->cooldown_ms = 0; f_->lock_ms = 0; f_->locked = 0;        \
            f_->legs = (uint8_t)(pp[i_][SLOT_LEGS] >= 27 ? ACR_GEN1_LEGS_TRACKS            \
                       : pp[i_][SLOT_LEGS] >= 21 ? ACR_GEN1_LEGS_FOUR : ACR_GEN1_LEGS_TWO); \
            f_->human_plus = (uint8_t)(i_ == 0 && human_plus); f_->kneeling = 0;          \
        }                                                                                 \
        acr_gen1_ac_spawn(P, def, 0, -2500 * 64, 0);                                      \
        acr_gen1_ac_spawn(T, def2, 0, 2500 * 64, 2048);                                   \
        memset(battle.shot, 0, sizeof battle.shot);                                       \
        round_ms = ROUND_MS; round_over = 0;                                              \
    } while (0)
    int human_plus = 0, two_players = SDL_getenv("ACR_TWO") != NULL;
    #define ROUND_MS 120000   /* two minutes, like Master of Arena's VS mode */
    int32_t round_ms = ROUND_MS;
    int round_over = 0;
    SETUP_FIGHTERS();
    acr_gen1_ac prevs[2] = { *P, *T };
    /* After a part change: new stats and weapons, same place in the arena. */
    #define REFRESH_FIGHTERS() do {                                                      \
        const acr_gen1_ac keep_p_ = *P, keep_t_ = *T;                                     \
        const int32_t ap_t_ = battle.f[1].ap;                                             \
        SETUP_FIGHTERS();                                                                 \
        *P = keep_p_; *T = keep_t_;                                                       \
        if (ap_t_ < battle.f[1].ap) battle.f[1].ap = ap_t_;                               \
        if (P->energy > def->stats.energy_capacity) P->energy = def->stats.energy_capacity; \
        if (T->energy > def2->stats.energy_capacity) T->energy = def2->stats.energy_capacity; \
    } while (0)
    int respawn_ms = 0;

    Uint64 last = SDL_GetTicksNS();
    const Uint64 start_ns = last;
    int running = 1, show_help = 1, back_held = 0, back_combo = 0, switch_pressed[2] = { 0, 0 };
    SDL_Gamepad *pads[2] = { pad, NULL };
    {   /* a second controller, if there is one */
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        for (int i = 0; ids && i < count && !pads[1]; ++i)
            if (!pads[0] || SDL_GetGamepadID(pads[0]) != ids[i]) pads[1] = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                const int taken = (pads[0] && SDL_GetGamepadID(pads[0]) == e.gdevice.which) ||
                                  (pads[1] && SDL_GetGamepadID(pads[1]) == e.gdevice.which);
                if (!taken && !pads[0]) pads[0] = SDL_OpenGamepad(e.gdevice.which);
                else if (!taken && !pads[1]) pads[1] = SDL_OpenGamepad(e.gdevice.which);
            }
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
                for (int k = 0; k < 2; ++k)
                    if (pads[k] && SDL_GetGamepadID(pads[k]) == e.gdevice.which) { SDL_CloseGamepad(pads[k]); pads[k] = NULL; }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F1) show_help = !show_help;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_R) {
                SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_TAB) switch_pressed[0] = 1;
            int toggle_two = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F2) toggle_two = 1;
            int cycle_rate = 0, cycle_slot = -1, toggle_plus = 0, cycle_who = 0;
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const int btn = e.gbutton.button;
                cycle_who = !two_players ? 0 : pads[1] ? (player_of(pads, e.gbutton.which, 0) == 1) : 1;
                if (btn == SDL_GAMEPAD_BUTTON_BACK) { back_held = 1; back_combo = 0; }
                if (btn == SDL_GAMEPAD_BUTTON_WEST && back_held) { cycle_slot = SLOT_BOOSTER; back_combo = 1; }
                if (btn == SDL_GAMEPAD_BUTTON_NORTH && back_held) { cycle_slot = SLOT_GEN; back_combo = 1; }
                if (btn == SDL_GAMEPAD_BUTTON_EAST && back_held) { cycle_slot = SLOT_BACK_L; back_combo = 1; }
                if (btn == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && back_held) { cycle_slot = SLOT_LEGS; back_combo = 1; }
                if (btn == SDL_GAMEPAD_BUTTON_SOUTH && back_held) { toggle_plus = 1; back_combo = 1; }
                if (btn == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && back_held) { toggle_two = 1; back_combo = 1; }
                if (btn == SDL_GAMEPAD_BUTTON_NORTH && !back_held) {
                    /* which player's controller? */
                    const int who = !two_players ? 0 : pads[1] ? (player_of(pads, e.gbutton.which, 0) == 1) : 1;
                    switch_pressed[who] = 1;
                }
                if (btn == SDL_GAMEPAD_BUTTON_START) { SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T; }
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                if (!back_combo) cycle_rate = 1;
                back_held = 0;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_T) cycle_rate = 1;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_B) cycle_slot = SLOT_BOOSTER;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_G) cycle_slot = SLOT_GEN;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_C) cycle_slot = SLOT_BACK_L;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_L) cycle_slot = SLOT_LEGS;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_H) toggle_plus = 1;
            if (toggle_plus) { human_plus = !human_plus; battle.f[0].human_plus = (uint8_t)human_plus; }
            if (toggle_two) {
                two_players = !two_players;
                SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) cycle_who = 0;      /* the keyboard is player 1 */
            if (cycle_rate) {
                int idx = 0;
                const int n = (int)(sizeof k_tick_rates / sizeof k_tick_rates[0]);
                for (int i = 0; i < n; ++i) if (k_tick_rates[i] == g_tick_rate) idx = i;
                g_tick_rate = k_tick_rates[(idx + 1) % n];
                set_rate(&clock, def, g_tick_rate);
                set_rate(&clock, def2, g_tick_rate);
                last = SDL_GetTicksNS();
            }
            if (cycle_slot >= 0 && !(cycle_slot == SLOT_BOOSTER && pp[cycle_who][SLOT_LEGS] >= 27)) {
                /* Each player changes their own build; alone, the target copies yours. */
                next_part(dir, pp[cycle_who], cycle_slot, &ac_data[cycle_who]);
                if (!two_players) {
                    memcpy(parts2, parts, sizeof parts2);
                    demo_load(dir, parts2, &ac_data[1], err, sizeof err);
                }
                set_rate(&clock, def, g_tick_rate);
                set_rate(&clock, def2, g_tick_rate);
                last = SDL_GetTicksNS();
                REFRESH_FIGHTERS();
                prevs[0] = *P;
                prevs[1] = *T;
            }
        }
        /* Who uses what: one player gets the keyboard and the first controller;
         * with two players, player 2 takes the second controller (or the only
         * one, leaving player 1 the keyboard). */
        SDL_Gamepad *ctl[2] = { pads[0], NULL };
        if (two_players) {
            if (pads[1]) ctl[1] = pads[1];
            else { ctl[1] = pads[0]; ctl[0] = NULL; }
        }
        acr_gen1_input in2[2];
        in2[0] = read_input(ctl[0], 1);
        in2[1] = two_players ? read_input(ctl[1], 0) : (acr_gen1_input){ 0, 0, 0, 0, 0 };
        /* Testing aid: ACR_HOLD="move,turn,strafe,boost[,fire]" holds those inputs;
         * ACR_SHOT=file.bmp ACR_SHOT_MS=2000 saves a picture and quits. */
        const char *hold = SDL_getenv("ACR_HOLD");
        int hold_fire = 0;
        if (hold) {
            int v[5] = { 0, 0, 0, 0, 0 };
            SDL_sscanf(hold, "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
            in2[0].move = (int8_t)v[0]; in2[0].turn = (int8_t)v[1]; in2[0].strafe = (int8_t)v[2];
            in2[0].boost = (uint8_t)v[3];
            hold_fire = v[4];
        }
        const bool *keys = SDL_GetKeyboardState(NULL);
        acr_gen1_actions act[2] = { { 0, 0 }, { 0, 0 } };
        act[0].fire = (uint8_t)(hold_fire || keys[SDL_SCANCODE_F] || keys[SDL_SCANCODE_LCTRL] ||
                                (ctl[0] && !back_held && SDL_GetGamepadButton(ctl[0], SDL_GAMEPAD_BUTTON_WEST)));
        if (two_players)
            act[1].fire = (uint8_t)(ctl[1] && !back_held && SDL_GetGamepadButton(ctl[1], SDL_GAMEPAD_BUTTON_WEST));

        /* A quick release of the booster between two steps still counts. */
        static int boost_released[2];
        for (int k = 0; k < 2; ++k) {
            if (!in2[k].boost) boost_released[k] = 1;
            in2[k].boost_tapped = (uint8_t)(in2[k].boost && boost_released[k]);
        }

        const Uint64 now = SDL_GetTicksNS();
        const uint32_t ticks = acr_clock_advance(&clock, now - last);
        last = now;
        battle.step_ms = (uint16_t)(1000 / (g_tick_rate > 0 ? g_tick_rate : 22));
        battle.step_rate = def->step_rate;
        battle.gen1_rate = def->gen1_rate;
        for (uint32_t i = 0; i < ticks && !round_over; ++i) {
            prevs[0] = *P;
            prevs[1] = *T;
            /* Two players: a two-minute round; it ends when an AC is
             * destroyed or time runs out (then the larger share of AP left
             * wins). */
            if (two_players) {
                round_ms -= battle.step_ms;
                if (round_ms <= 0 || battle.f[0].ap <= 0 || battle.f[1].ap <= 0) {
                    if (round_ms < 0) round_ms = 0;
                    round_over = 1;
                    break;
                }
            }
            for (int k = 0; k < 2; ++k) { act[k].next_weapon = (uint8_t)switch_pressed[k]; switch_pressed[k] = 0; }
            acr_gen1_battle_step(&battle, in2, act);
            for (int k = 0; k < 2; ++k) { in2[k].boost_tapped = 0; boost_released[k] = !in2[k].boost; }
            /* arena walls */
            const int32_t lim = ARENA * 64;
            for (int k = 0; k < 2; ++k) {
                acr_gen1_ac *q = &battle.f[k].ac;
                if (q->x > lim) { q->x = lim; q->vx = 0; }
                if (q->x < -lim) { q->x = -lim; q->vx = 0; }
                if (q->z > lim) { q->z = lim; q->vz = 0; }
                if (q->z < -lim) { q->z = -lim; q->vz = 0; }
            }
            /* The target comes back 3 seconds after it is destroyed. */
            if (!two_players && battle.f[1].ap <= 0) {
                respawn_ms += battle.step_ms;
                if (respawn_ms >= 3000) {
                    respawn_ms = 0;
                    battle.f[1].ap = battle.f[1].ap_max;
                    acr_gen1_ac_spawn(T, def2, 0, 2500 * 64, 2048);
                    prevs[1] = *T;
                }
            }
        }

        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
        acr_render_begin(pw, ph, 18, 22, 28);
        const int views = two_players ? 2 : 1;
        for (int view = 0; view < views; ++view) {
        const int vx = view * pw / views, vpw = (view + 1) * pw / views - vx;
        const acr_gen1_ac ac = battle.f[view].ac;
        const acr_gen1_ac prev = prevs[view];
        const acr_gen1_ac *const T = &battle.f[1 - view].ac;
        const acr_gen1_ac tprev = prevs[1 - view];
        /* Interpolate between the last two steps. */
        const float a = (float)acr_clock_alpha(&clock) / (float)ACR_FX_ONE;
        const float px = (prev.x + (ac.x - prev.x) * a) / 64.0f;
        const float py = -(prev.y + (ac.y - prev.y) * a) / 64.0f;
        const float pz = (prev.z + (ac.z - prev.z) * a) / 64.0f;
        int dyaw = (int)ac.yaw - (int)prev.yaw;
        if (dyaw > 2048) dyaw -= 4096;
        if (dyaw < -2048) dyaw += 4096;
        const float yaw = ((float)prev.yaw + dyaw * a) * (6.2831853f / 4096.0f);

        /* Camera behind and above, looking slightly down. The engine's
         * right is (cos, 0, -sin) of the facing, so x is mirrored after the
         * usual look-at. The view centre sits a little below the middle
         * of the screen, as before. */
        float proj[16], viewm[16];
        acr_mat_perspective(proj, 58.0f * 3.14159265f / 180.0f, (float)vpw / (float)ph, 50.0f, 80000.0f);
        for (int k = 0; k < 4; ++k) proj[1 + 4 * k] -= 0.1f * proj[3 + 4 * k];   /* centre 5% low */
        {
            const float eye[3] = { px - sinf(yaw) * 2200.0f, py + 1200.0f, pz - cosf(yaw) * 2200.0f };
            const float fwd[3] = { sinf(yaw), -0.22f, cosf(yaw) }, up[3] = { 0, 1, 0 };
            acr_mat_look(viewm, eye, fwd, up);
            for (int k = 0; k < 4; ++k) viewm[4 * k] = -viewm[4 * k];
        }
        const acr_fog fog = { 30000.0f, 60000.0f, 18, 22, 28 };
        acr_render_view3d(vx, 0, vpw, ph, proj, viewm, &fog);

        /* floor grid and walls */
        for (int g = -ARENA; g <= ARENA; g += 2000) {
            line3(v3((float)g, 0, (float)-ARENA), v3((float)g, 0, (float)ARENA), 52, 70, 76);
            line3(v3((float)-ARENA, 0, (float)g), v3((float)ARENA, 0, (float)g), 52, 70, 76);
        }
        for (int w = 0; w < 4; ++w) {
            const float s0 = (w & 1) ? ARENA : -ARENA;
            for (int h = 0; h <= 3000; h += 1000) {
                if (w < 2) line3(v3(s0, (float)h, -ARENA), v3(s0, (float)h, ARENA), 120, 150, 160);
                else line3(v3(-ARENA, (float)h, s0), v3(ARENA, (float)h, s0), 120, 150, 160);
            }
        }
        /* pillars for a sense of speed, then the ACs and shots */
        for (int i = -2; i <= 2; ++i)
            for (int j = -2; j <= 2; ++j)
                if (i || j) add_box(v3(i * 7000.0f, 1500, j * 7000.0f), 0, v3(300, 1500, 300), 0.35f, 0.42f, 0.48f);
        float crouch = 0;
        if (ac.state == ACR_GEN1_CROUCH) crouch = 90;
        if (ac.state == ACR_GEN1_LANDING) crouch = 70;
        if (battle.f[view].kneeling) crouch = 150;
        add_ac(v3(px, py, pz), yaw, crouch, 0);
        const float tx = (tprev.x + (T->x - tprev.x) * a) / 64.0f, ty = -(tprev.y + (T->y - tprev.y) * a) / 64.0f,
                    tz = (tprev.z + (T->z - tprev.z) * a) / 64.0f;
        if (battle.f[1 - view].ap > 0) add_ac(v3(tx, ty, tz), T->yaw * (6.2831853f / 4096.0f), 0, 1);
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
            const acr_gen1_shot *sh = &battle.shot[k];
            if (!sh->active) continue;
            const int missile = battle.f[sh->owner].weapon[sh->weapon].kind == ACR_GEN1_MISSILE;
            if (missile) add_box(v3(sh->x / 64.0f, -sh->y / 64.0f, sh->z / 64.0f), 0, v3(40, 40, 40), 1.0f, 0.55f, 0.15f);
            else add_box(v3(sh->x / 64.0f, -sh->y / 64.0f, sh->z / 64.0f), 0, v3(20, 20, 20), 1.0f, 0.95f, 0.5f);
        }
        if (battle.f[view].swing) {                       /* blade arc */
            const float r = 700.0f;
            for (int k = -3; k < 3; ++k) {
                const float a0 = yaw + k * 0.25f, a1 = yaw + (k + 1) * 0.25f;
                line3(v3(px + sinf(a0) * r, py + 450, pz + cosf(a0) * r),
                      v3(px + sinf(a1) * r, py + 450, pz + cosf(a1) * r), 120, 200, 255);
            }
        }
        if (py > 1.0f) line3(v3(px, 0, pz), v3(px, py, pz), 230, 160, 40);   /* height marker */
        flush_scene();

        /* HUD, on a 480-high grid as wide as this view's shape. */
        g_vw = acr_render_hud(vx, 0, vpw, ph);
        g_vh = 480.0f;
        const float rc_y = two_players ? 130.0f : g_vh - 80.0f;   /* right-hand HUD column */
        const acr_gen1_stats *st = &ac_data[view].def.stats;
        const float e_frac = (float)ac.energy / (float)(st->energy_capacity ? st->energy_capacity : 1);
        const float red = (float)st->red_zone / (float)(st->energy_capacity ? st->energy_capacity : 1);
        acr_hud_rect(20, g_vh - 30, 300, 10, 40, 40, 40, 255, 1);
        if (ac.drained) acr_hud_rect(20, g_vh - 30, 300 * e_frac, 10, 200, 50, 40, 255, 1);
        else acr_hud_rect(20, g_vh - 30, 300 * e_frac, 10, 90, 220, 140, 255, 1);
        acr_hud_line(20 + 300 * red, g_vh - 33, 20 + 300 * red, g_vh - 17, 230, 60, 50, 255);

        static const char *const k_state[] = { "GROUND", "CROUCH", "AIR", "LANDING" };
        const int dx = (ac.x - prev.x) / 64, dz = (ac.z - prev.z) / 64, dy = (ac.y - prev.y) / 64;
        char line[160];
        #define TEXT(x, y, s) acr_hud_text((x), (y), (s), 220, 230, 235)
        SDL_snprintf(line, sizeof line, "ENERGY %5d  %-7s  SPEED %3d  RISE %4d", (int)ac.energy,
                     k_state[ac.state], (int)sqrtf((float)(dx * dx + dz * dz)), -dy);
        TEXT(20, g_vh - 48, line);
        SDL_snprintf(line, sizeof line, "LEGS %s  BOOSTER %s  GENERATOR %s", ac_data[view].names[SLOT_LEGS],
                     ac_data[view].names[SLOT_BOOSTER], ac_data[view].names[SLOT_GEN]);
        TEXT(20, 12, line);
        SDL_snprintf(line, sizeof line, "%d STEPS/S%s", g_tick_rate, g_tick_rate == 22 ? " (GEN 1)" : "");
        TEXT(g_vw - 150, rc_y + 32, line);
        /* Weapon, ammo, AP, lock box and the target's marker. */
        {
            const acr_gen1_fighter *me = &battle.f[view], *tg = &battle.f[1 - view];
            if (me->weapons) {
                const acr_gen1_weapon *w = &me->weapon[me->current];
                if (w->ammo) SDL_snprintf(line, sizeof line, "%-12s %4d", w->name, me->ammo[me->current]);
                else SDL_snprintf(line, sizeof line, "%-12s", w->name);
                TEXT(g_vw - 150, rc_y + 16, line);
                if (w->stance && !me->human_plus) {
                    const char *hint = me->legs == ACR_GEN1_LEGS_TRACKS ? NULL
                                     : me->legs == ACR_GEN1_LEGS_FOUR ? (acr_gen1_stance_ok(me) ? NULL : "LAND TO FIRE")
                                     : !me->kneeling ? "FIRE TO KNEEL" : acr_gen1_stance_ok(me) ? "KNEELING" : "KNEELING...";
                    if (hint) TEXT(g_vw - 150, rc_y, hint);
                }
                if (w->box_w) {
                    /* The box as the original sizes it: its screen is about
                     * 512 wide at 4:3, so a unit is 1.25 of our 480 grid. */
                    const float bw = w->box_w * 1.25f, bh = w->box_h * 1.25f;
                    /* The FCS box takes the lock's colour once the target is locked. */
                    if (me->locked) acr_hud_rect(g_vw * 0.5f - bw * 0.5f, g_vh * 0.45f - bh * 0.5f, bw, bh, 255, 60, 50, 255, 0);
                    else acr_hud_rect(g_vw * 0.5f - bw * 0.5f, g_vh * 0.45f - bh * 0.5f, bw, bh, 90, 200, 120, 255, 0);
                }
                float sx, sy;
                if (tg->ap > 0 && acr_gen1_in_lock_box(&battle, view, w) &&
                    to_hud(proj, viewm, v3(tx, ty + 400, tz), g_vw, &sx, &sy)) {
                    if (me->locked) {
                        acr_hud_rect(sx - 18, sy - 18, 36, 36, 255, 60, 50, 255, 0);
                        acr_hud_text(sx - 16, sy + 22, "LOCK", 255, 60, 50);
                    } else {
                        acr_hud_rect(sx - 18, sy - 18, 36, 36, 240, 220, 60, 255, 0);
                    }
                }
            }
            if (me->human_plus) TEXT(20, g_vh - 64, "HUMAN PLUS");
            SDL_snprintf(line, sizeof line, "AP %5d", (int)me->ap);
            TEXT(g_vw - 150, rc_y + 48, line);
            acr_hud_rect(g_vw - 220, 96, 200, 8, 40, 40, 40, 255, 1);
            acr_hud_rect(g_vw - 220, 96, 200.0f * (float)tg->ap / (float)(tg->ap_max ? tg->ap_max : 1), 8,
                         230, 70, 60, 255, 1);
            SDL_snprintf(line, sizeof line, tg->ap > 0 ? "TARGET AP %5d" : "TARGET DESTROYED", (int)tg->ap);
            TEXT(g_vw - 220, 108, line);
        }
        if (show_help && !two_players) {
            TEXT(20, 28, "MOVE: STICK/DPAD UP-DOWN  TURN: LEFT-RIGHT");
            TEXT(20, 40, "STRAFE: LB/RB  BOOST: A OR RT (NO DIRECTION = RISE)");
            TEXT(20, 52, "FIRE: X (F)  SWITCH WEAPON: Y (TAB)");
            TEXT(20, 64, "BACK+X/Y/B/RB (B/G/C/L): BOOSTER/GENERATOR/BACK WEAPON/LEGS");
            TEXT(20, 76, "START/R: RESET  F1: HIDE HELP  ESC: QUIT");
            TEXT(20, 88, "BACK+A (H): HUMAN PLUS  BACK (T): SPEED");
            TEXT(20, 100, "BACK+LB (F2): TWO PLAYERS");
        }
        }
        /* Whole-screen HUD: the split line, round clock and result. */
        const float full_w = acr_render_hud(0, 0, pw, ph);
        if (two_players) {
            acr_hud_rect(full_w / 2.0f - 1, 0, 2, 480, 0, 0, 0, 255, 1);
            char clock_text[16];
            SDL_snprintf(clock_text, sizeof clock_text, "%d:%02d", (int)(round_ms / 60000), (int)(round_ms / 1000 % 60));
            acr_hud_text(full_w / 2.0f - 16, 464, clock_text, 240, 240, 240);   /* between the energy bars */
        }
        if (two_players && round_over) {
            /* Winner: the AC still standing, or on time the larger share of AP. */
            const int64_t s0 = (int64_t)battle.f[0].ap * (battle.f[1].ap_max ? battle.f[1].ap_max : 1);
            const int64_t s1 = (int64_t)battle.f[1].ap * (battle.f[0].ap_max ? battle.f[0].ap_max : 1);
            acr_hud_text(full_w / 2.0f - 120, 240,
                         s0 > s1 ? "PLAYER 1 WINS - START TO RESET" :
                         s1 > s0 ? "PLAYER 2 WINS - START TO RESET" : "DRAW - START TO RESET", 255, 230, 120);
            if (round_ms <= 0) acr_hud_text(full_w / 2.0f - 40, 254, "TIME UP", 255, 230, 120);
        }
        #undef TEXT
        const char *shot = SDL_getenv("ACR_SHOT");
        if (shot && (now - start_ns) / 1000000 >= (Uint64)SDL_atoi(SDL_getenv("ACR_SHOT_MS") ? SDL_getenv("ACR_SHOT_MS") : "2000")) {
            SDL_Surface *img = SDL_CreateSurface(pw, ph, SDL_PIXELFORMAT_RGBA32);
            uint8_t *buf = img ? malloc((size_t)pw * (size_t)ph * 4) : NULL;
            if (buf) {
                acr_render_read(pw, ph, buf);
                for (int y = 0; y < ph; ++y)      /* GL rows run bottom up */
                    memcpy((uint8_t *)img->pixels + (size_t)y * (size_t)img->pitch,
                           buf + (size_t)(ph - 1 - y) * (size_t)pw * 4, (size_t)pw * 4);
                SDL_SaveBMP(img, shot);
            }
            free(buf);
            if (img) SDL_DestroySurface(img);
            running = 0;
        }
        SDL_GL_SwapWindow(window);
    }

    for (int k = 0; k < 2; ++k) if (pads[k]) SDL_CloseGamepad(pads[k]);
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(block);
    return 0;
}
