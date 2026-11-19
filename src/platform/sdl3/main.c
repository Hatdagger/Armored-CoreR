/* PC shell (SDL3), test build: one Gen 1 AC in an empty arena.
 *
 * The AC's stats and walking animation come from the player's own
 * acr-import output (folder given on the command line, or "import" next to
 * the program). Movement runs at Gen 1's 22 steps a second; drawing is
 * smoothed between steps at the screen's own rate. ACs are drawn from the
 * import's models (pieces, skeleton, joint animations); an import without
 * models gets a stand-in made of boxes.
 *
 * It opens in the garage (assembly, parts shop, AC TEST); AC TEST is the
 * test arena with the garage's build. The garage is saved as a readable
 * file in the user's data folder (ACR_SAVE=path picks another file).
 *
 *   armored-corer [import folder | disc.cue] [--legs N] [--booster N]
 *                 [--core N] [--arms N] [--gen N] [--back N] (Master of Arena
 *                 indices: these skip the garage and go straight to the test)
 *                 [--test]     straight to the test with the starting AC
 *                 [--credits N] credits for a new save (default 0, as the game)
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
#include "../../gen1/acr_gen1_pose.h"
#include "../../gen1/acr_gen1_ai.h"
#include "demo_model.h"
#include "demo_stage.h"
#include "../../model/acr_aim.h"
#include "garage.h"
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
    acr_gen1_input in = { 0, 0, 0, 0, 0, 0, 0 };
    if (keyboard) {
    in.move = (int8_t)((k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]));
    in.turn = (int8_t)((k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]));
    in.strafe = (int8_t)(k[SDL_SCANCODE_E] - k[SDL_SCANCODE_Q]);
    in.boost = (uint8_t)(k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J]);
    {
        const int up = k[SDL_SCANCODE_PAGEUP], down = k[SDL_SCANCODE_PAGEDOWN];
        in.look = (int8_t)(up && down ? 2 : up - down);
    }
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
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) in.boost = 1;
        /* L2 / R2 tilt the view up / down; both together: back to level. */
        const int lt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000;
        const int rt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000;
        if (lt || rt) in.look = (int8_t)(lt && rt ? 2 : lt - rt);
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

/* Each player's model, reloaded when their build changes. Player 2 gets
 * its own colours (made-up numbers, 0..63 per channel) so the two differ. */
static demo_model g_model[2];
static int g_model_parts[2][SLOT_COUNT], g_model_set[2] = { -1, -1 };
static const uint8_t k_colours_p2[4][3] = { { 46, 12, 10 }, { 18, 18, 20 }, { 52, 44, 16 }, { 10, 10, 12 } };

/* Aiming (acr_aim.h): each player's aim table and angles, set from the
 * battle before drawing, and the eased joints. */
static int g_aim_table[2] = { -1, -1 };
/* Where each AC's current weapon fires from, as drawn (see draw_aimed). */
static unsigned g_muzzle_parts[2];
static struct { int ok; float p[3]; } g_muzzle[2];
static int16_t g_aim[2][3];
static struct { acr_aim_state st; Uint64 last_ns; int ok; } g_aiming[2];

static void refresh_model(int who, const char *dir, const int parts[SLOT_COUNT], int set) {
    if (g_model_set[who] == set && !memcmp(g_model_parts[who], parts, sizeof g_model_parts[who])) return;
    g_aiming[who].ok = 0;
    demo_model_free(&g_model[who]);
    demo_model_load(dir, set, parts, who ? k_colours_p2 : NULL, &g_model[who]);
    memcpy(g_model_parts[who], parts, sizeof g_model_parts[who]);
    g_model_set[who] = set;
}

/* Which animation to show and how far into it. The sim runs the walking
 * animations (their root motion moves the AC) and the jump take-off; the
 * rest are poses the game picks over the movement (acr_gen1_pose: by
 * direction in the air, boosting and sliding; turning; landing; the blade
 * swing). Shown at the game's 22 ticks a second from when each began.
 * Firing has no body animation of its own (it uses a second layer, not
 * drawn yet). */
static int g_swinging[2];                    /* set from the battle before drawing */
static acr_gen1_input g_in[2];               /* each player's latest input, for the poses */
static struct { int shown; Uint64 since; } g_pose[2];

/* A pose this animation set lacks: the nearest one it has. */
static int pose_or_fallback(const demo_model *m, int want) {
    if (m->anims.anim[want].frames) return want;
    if (want >= ACR_GEN1_SHOW_RISE && want <= ACR_GEN1_SHOW_AIR_BR && m->anims.anim[ACR_GEN1_SHOW_AIR].frames)
        return ACR_GEN1_SHOW_AIR;
    if (want > ACR_GEN1_SHOW_BOOST_FWD && want <= ACR_GEN1_SHOW_BOOST_BR && m->anims.anim[ACR_GEN1_SHOW_BOOST_FWD].frames)
        return ACR_GEN1_SHOW_BOOST_FWD;
    return ACR_GEN1_ANIM_IDLE;
}

static int shown_anim(int who, const acr_gen1_ac *ac, const demo_model *m, float *tick, int *loop) {
    *tick = (float)ac->anim_frame;
    *loop = ac->anim != ACR_GEN1_ANIM_IDLE && ac->anim != ACR_GEN1_ANIM_JUMP;
    const acr_anim *jump = &m->anims.anim[ACR_GEN1_ANIM_JUMP];
    /* the swing plays out after the lunge */
    const int blade_left = g_pose[who].shown == ACR_GEN1_SHOW_BLADE &&
        (SDL_GetTicks() - g_pose[who].since) * 22 / 1000 < m->anims.anim[ACR_GEN1_SHOW_BLADE].ticks;
    int want = acr_gen1_pose_shown(ac, g_in[who], g_swinging[who] || blade_left,
                                   jump->frames ? jump->ticks : 0);
    if (want == ac->anim) {
        /* The sim's walking advances a frame each Gen 1 tick (22 a second)
         * at any step rate; draw the frame the shown position is in (plus
         * how far into it), so the legs match the ground. */
        g_pose[who].shown = want;
        float t = ac->anim_frame ? (float)ac->anim_frame - 1.0f + acr_gen1_tick_fraction(ac) : 0.0f;
        const acr_anim *a = &m->anims.anim[want];
        if (*loop && a->frames) {
            unsigned total = 0;
            for (int f = 0; f < a->frames; ++f) total += m->anims.frame[a->first_frame + f].hold ? m->anims.frame[a->first_frame + f].hold : 1;
            t = fmodf(t, (float)total);
        }
        *tick = t;
        return want;
    }
    *loop = 0;
    want = pose_or_fallback(m, want);
    if (g_pose[who].shown != want) { g_pose[who].shown = want; g_pose[who].since = SDL_GetTicks(); }
    *tick = (float)(SDL_GetTicksNS() - g_pose[who].since * 1000000ull) * 22.0f / 1e9f;
    return want;
}

/* Animation playback: 1 = smooth (poses ease into each other between the
 * game's ticks, and animation changes cross-fade as the game does:
 * acr_gen1_pose_fade_step, mostly 8 ticks), 0 = stepped poses, no fades.
 * Only what is drawn changes; the sim, root motion and hits keep the game's
 * ticks. F5 toggles. */
static int g_smooth_anim = 1;
static struct { int anim, ok; acr_pose drawn, from; Uint64 fade_ns; uint16_t fade_step; } g_blend[2];

/* The aim over the animation (arms, waist, back units on the target), eased
 * at the game's 1/8 a tick, then the model. */
static void draw_aimed(int who, const demo_model *m, acr_pose *pose, Uint64 now, float x, float y, float z,
                       float yaw) {
    if (!g_aiming[who].ok) { acr_aim_reset(&g_aiming[who].st); g_aiming[who].ok = 1; g_aiming[who].last_ns = now; }
    const float ticks = (float)(now - g_aiming[who].last_ns) / (float)ACR_GEN1_TICK_NS;
    g_aiming[who].last_ns = now;
    acr_aim_apply(&g_aiming[who].st, &m->skel, &m->anims, g_aim_table[who], g_aim[who], ticks, pose);
    /* The game fires from the weapon's joint (VS 0x80078888): take the
     * weapon's forward-most point as drawn, aim included. */
    g_muzzle[who].ok = g_muzzle_parts[who] &&
                       demo_model_part_tip(m, pose, g_muzzle_parts[who], ACR_GEN1_MODEL_SCALE, g_muzzle[who].p) &&
                       fabsf(g_muzzle[who].p[0]) < 600 && fabsf(g_muzzle[who].p[2]) < 600 &&
                       g_muzzle[who].p[1] > -100 && g_muzzle[who].p[1] < 600;   /* else the stand-in */
    demo_model_draw_pose(m, pose, x, y, z, yaw, ACR_GEN1_MODEL_SCALE, &g_light);
}

static void draw_ac(int who, const acr_gen1_ac *ac, float x, float y, float z, float yaw, float crouch) {
    float tick;
    int loop;
    const demo_model *m = &g_model[who];
    if (!m->ok) { add_ac(v3(x, y, z), yaw, crouch, who); return; }
    const int anim = shown_anim(who, ac, m, &tick, &loop);
    acr_pose pose;
    const Uint64 now = SDL_GetTicksNS();
    if (!g_smooth_anim) {
        g_blend[who].ok = 0;
        acr_pose_play(&m->skel, &m->anims, anim, (unsigned)tick, &pose);
        draw_aimed(who, m, &pose, now, x, y, z, yaw);
        return;
    }
    acr_pose_play_smooth(&m->skel, &m->anims, anim, tick, loop, &pose);
    if (g_blend[who].ok && g_blend[who].anim != anim) {
        /* The game freezes each joint where it was drawn and moves it to
         * the new animation a step a tick (pose routine 0x80059624). */
        g_blend[who].fade_step = acr_gen1_pose_fade_step(g_blend[who].anim, anim);
        g_blend[who].from = g_blend[who].drawn;
        g_blend[who].fade_ns = g_blend[who].fade_step ? now : 0;
    }
    if (g_blend[who].ok && g_blend[who].fade_ns) {
        const float ticks = (float)(now - g_blend[who].fade_ns) / (float)ACR_GEN1_TICK_NS;
        const float t = acr_gen1_pose_fade_amount(g_blend[who].fade_step, ticks);
        if (t >= 1.0f) g_blend[who].fade_ns = 0;
        else acr_pose_mix(&m->skel, &g_blend[who].from, &pose, t, &pose);
    }
    g_blend[who].anim = anim; g_blend[who].drawn = pose; g_blend[who].ok = 1;
    draw_aimed(who, m, &pose, now, x, y, z, yaw);
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

/* Test: the target's start, ACR_DIST units ahead of player 1 (default
 * 5,000) and ACR_SIDE to their right, for measuring against the recomp. */
static int target_dist(void) { const char *e = SDL_getenv("ACR_DIST"); return e ? SDL_atoi(e) : 5000; }
static int target_side(void) { const char *e = SDL_getenv("ACR_SIDE"); return e ? SDL_atoi(e) : 0; }

static int g_direct_test = 0;   /* part arguments given: no garage */
static long g_new_credits = 0;
static int g_tick_rate = 60;   /* test: steps a second; 22 = Gen 1's own pace */
static int g_landing_anim = 0; /* test: landings last the landing animation (F3 / Back + R3) */
static const int k_tick_rates[] = { 22, 30, 45, 60 };

/* Starts the clock at the chosen rate. Faster than Gen 1, the game's rules
 * still run 22 times a second between steps, so every rate plays at the
 * game's pace; the extra steps only make the drawing smoother. */
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

/* Computer opponent (docs/AI.md): 0 = a target that stands still, else
 * the preset that flies it (ACR_GEN1_AI_*), or AI_FROM_FILE = the --ai-file
 * profile. (The library's ACR_GEN1_AI_CUSTOM is 0, the same as "still", so
 * the test build keeps its own number for the file: F4 used to stick on
 * the still target.) */
#define AI_FROM_FILE 100
static int g_ai_mode = 0;
static acr_gen1_ai g_ai[2];
static unsigned g_ai_round = 0;
static int g_autopilot = 0;              /* player 1 flown by the AI too (AI against AI) */
static acr_gen1_ai_profile g_ai_file_prof;
static int g_ai_file_ok = 0;

static int ai_preset_of(const char *s) {
    for (int k = 1; k < ACR_GEN1_AI_PRESETS; ++k)
        if (!SDL_strcasecmp(s, acr_gen1_ai_preset_name(k))) return k;
    const int n = atoi(s);
    return n >= 0 && n < ACR_GEN1_AI_PRESETS ? n : ACR_GEN1_AI_BALANCED;
}

/* The profile a mode flies: a preset, or the file's. */
static acr_gen1_ai_profile ai_profile(int mode) {
    acr_gen1_ai_profile p;
    if (mode == AI_FROM_FILE && g_ai_file_ok) return g_ai_file_prof;
    acr_gen1_ai_preset(&p, mode > 0 && mode < ACR_GEN1_AI_PRESETS ? mode : ACR_GEN1_AI_BALANCED);
    return p;
}

static void load_ai_file(const char *path) {
    size_t size = 0;
    char *text = (char *)SDL_LoadFile(path, &size);
    if (!text) { SDL_Log("AI profile %s: can't read it", path); return; }
    acr_gen1_ai_preset(&g_ai_file_prof, ACR_GEN1_AI_BALANCED);
    g_ai_file_prof.preset = ACR_GEN1_AI_CUSTOM;
    g_ai_file_ok = acr_gen1_ai_parse(&g_ai_file_prof, text);
    if (g_ai_file_ok) g_ai_mode = AI_FROM_FILE;
    SDL_free(text);
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
        if (!strcmp(argv[i], "--credits") && i + 1 < argc) { g_new_credits = atol(argv[++i]); continue; }
        if (!strcmp(argv[i], "--test")) { g_direct_test = 1; continue; }
        if (!strcmp(argv[i], "--ai") && i + 1 < argc) { g_ai_mode = ai_preset_of(argv[++i]); if (!g_ai_mode) g_ai_mode = 1; continue; }
        if (!strcmp(argv[i], "--ai-file") && i + 1 < argc) { load_ai_file(argv[++i]); continue; }
        if (!strcmp(argv[i], "--ai-vs-ai")) { g_autopilot = 1; if (!g_ai_mode) g_ai_mode = 1; continue; }
        if (slot >= 0 && i + 1 < argc) { parts[slot] = atoi(argv[++i]); g_direct_test = 1; }
        else if (argv[i][0] != '-') *dir = argv[i];
    }
    return 1;
}

/* ---------------- garage ---------------- */

/* Test builds: a new game owns every part and the shop starts empty (it
 * stocks what you sell it). A release turns this off and sets the starting
 * credits instead. */
static const int k_test_kit = 1;
static acr_gen1_hangar g_hangar;
static const char *g_import_dir = "";
/* The arena: Green Silo (stage 27) when the import has it; F7 cycles Green
 * Silo, Ocean Platform (39) and the open test floor. ACR_STAGE=NN picks
 * another stage to start on, ACR_STAGE=0 starts on the open floor. */
static demo_stage g_stage;
static int g_stage_id = 27, g_stage_on = 0;

/* Places fighter k's AC on the stage's start k (x, y down, z, facing). */
static void stage_spawn(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int k) {
    const int32_t *st = g_stage.start[k];
    acr_gen1_ac_spawn(ac, def, st[0] * 64, st[2] * 64, (uint16_t)(st[3] & 4095));
    ac->y = ac->py = ac->sy = st[1] * 64;
}
static acr_gen1_catalog g_catalog;
static char g_part_names[ACR_GEN1_CAT_COUNT][ACR_GEN1_PART_MAX][24];

static void save_path(char *out, size_t size) {
    const char *env = SDL_getenv("ACR_SAVE");
    if (env && env[0]) { SDL_strlcpy(out, env, size); return; }
    char *pref = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
    SDL_snprintf(out, size, "%sgen1-save.toml", pref ? pref : "");
    SDL_free(pref);
}

/* Loads the save, or starts a new one from the starting AC. */
static void hangar_open(const int start[SLOT_COUNT]) {
    char path[1200];
    save_path(path, sizeof path);
    char *text = SDL_LoadFile(path, NULL);
    const int ok = text && acr_gen1_save_read(&g_hangar, text);
    SDL_free(text);
    if (ok) return;
    uint8_t s[SLOT_COUNT];
    for (int i = 0; i < SLOT_COUNT; ++i) s[i] = (uint8_t)start[i];
    acr_gen1_hangar_new(&g_hangar, s, 0, (int32_t)g_new_credits);
    if (k_test_kit) acr_gen1_hangar_give_all(&g_hangar, &g_catalog);
    char cpath[1200];
    SDL_snprintf(cpath, sizeof cpath, "%s/models/colours.toml", g_import_dir);
    char *ct = SDL_LoadFile(cpath, NULL);
    const char *v = ct ? SDL_strstr(ct, "default") : NULL;
    if (v && (v = SDL_strchr(v, '[')) != NULL)
        for (int k = 0; k < 12; ++k) {
            char *end;
            const long x = SDL_strtol(v + 1, &end, 10);
            if (end == v + 1 || x < 0 || x > 63) break;
            g_hangar.colours[k / 3][k % 3] = (uint8_t)x;
            v = end;
            while (*v == ' ') ++v;
            if (*v != ',') break;
        }
    SDL_free(ct);
    SDL_strlcpy(g_hangar.ac_name, "AC", sizeof g_hangar.ac_name);
    SDL_strlcpy(g_hangar.pilot_name, "RAVEN", sizeof g_hangar.pilot_name);
}

static int hangar_save(void) {
    char path[1200], text[4096];
    save_path(path, sizeof path);
    const size_t n = acr_gen1_save_write(&g_hangar, text, sizeof text);
    return n < sizeof text && SDL_SaveFile(path, text, n);
}

static void hangar_parts(int parts[SLOT_COUNT]) {
    for (int i = 0; i < SLOT_COUNT; ++i) parts[i] = g_hangar.fitted[i];
}

/* Menu input from the controller: d-pad or stick, repeating while held. */
static garage_action pad_direction(SDL_Gamepad *pad, Uint64 now_ms) {
    static int held = 0;
    static Uint64 next_ms = 0;
    int dir = 0;
    if (pad) {
        const int ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) || ay < -20000) dir = GA_UP;
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ay > 20000) dir = GA_DOWN;
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || ax < -20000) dir = GA_LEFT;
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || ax > 20000) dir = GA_RIGHT;
    }
    if (!dir) { held = 0; return GA_NONE; }
    if (dir != held) { held = dir; next_ms = now_ms + 380; return (garage_action)dir; }
    if (now_ms >= next_ms) { next_ms = now_ms + 70; return (garage_action)dir; }
    return GA_NONE;
}

static garage_action key_action(SDL_Scancode k) {
    switch (k) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_W: return GA_UP;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: return GA_DOWN;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: return GA_LEFT;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: return GA_RIGHT;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: return GA_OK;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: return GA_CANCEL;
    case SDL_SCANCODE_Q: return GA_PREV_TAB;
    case SDL_SCANCODE_E: return GA_NEXT_TAB;
    default: return GA_NONE;
    }
}

static garage_action button_action(int b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return GA_OK;
    case SDL_GAMEPAD_BUTTON_EAST: return GA_CANCEL;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GA_PREV_TAB;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GA_NEXT_TAB;
    default: return GA_NONE;
    }
}

/* The garage's AC: turning slowly, standing, in the middle of the screen. */
static void draw_garage_ac(int pw, int ph, float hud_w, unsigned tick) {
    float hx, hy, hw, hh;
    garage_view_rect(hud_w, &hx, &hy, &hw, &hh);
    const float s = (float)ph / 480.0f;
    const int x = (int)(hx * s), y = (int)(hy * s), w = (int)(hw * s), h = (int)(hh * s);
    if (w < 8 || h < 8) return;
    (void)pw;
    float proj[16], viewm[16];
    acr_mat_perspective(proj, 40.0f * 3.14159265f / 180.0f, (float)w / (float)h, 50.0f, 40000.0f);
    const float eye[3] = { 0, 1000.0f, -2700.0f }, fwd[3] = { 0, -0.2f, 1 }, up[3] = { 0, 1, 0 };
    acr_mat_look(viewm, eye, fwd, up);
    for (int k = 0; k < 4; ++k) viewm[4 * k] = -viewm[4 * k];
    acr_render_view3d(x, y, w, h, proj, viewm, NULL);
    for (int g = -2000; g <= 2000; g += 500) {   /* the hangar floor */
        line3(v3((float)g, 0, -2000), v3((float)g, 0, 2000), 52, 70, 76);
        line3(v3(-2000, 0, (float)g), v3(2000, 0, (float)g), 52, 70, 76);
    }
    flush_scene();
    const float yaw = 3.14159265f + (float)tick * 0.004f;
    if (g_model[0].ok) demo_model_draw(&g_model[0], 0, tick, 0, 0, 0, yaw, 1.0f, &g_light);
    else { add_ac(v3(0, 0, 0), yaw, 0, 0); flush_scene(); }
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
    /* The garage: the shop's catalogue from the import, and the save (or a
     * new game with the starting AC). */
    g_import_dir = dir;
    {
        const char *want = SDL_getenv("ACR_STAGE");
        if (want) g_stage_id = SDL_atoi(want);
        if (g_stage_id > 0 && demo_stage_load(dir, g_stage_id, &g_stage)) g_stage_on = 1;
    }
    demo_catalog(dir, &g_catalog, g_part_names);
    if (k_test_kit)
        for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c)
            for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) g_catalog.part[c][i].for_sale = 0;
    hangar_open(parts);
    int mode = g_direct_test ? 1 : 0;   /* 0 garage, 1 test arena */
    garage_ui gui;
    garage_init(&gui);
    garage_data gdata = { &g_hangar, &g_catalog, g_part_names };
    if (!g_direct_test) {
        int fitted[SLOT_COUNT];
        hangar_parts(fitted);
        if (demo_load(dir, fitted, &ac_data[0], err, sizeof err)) memcpy(parts, fitted, sizeof fitted);
        else demo_load(dir, parts, &ac_data[0], err, sizeof err);
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
            memset(f_, 0, sizeof *f_);                                                    \
            f_->intercept_shot = -1; f_->swing_target = -1; f_->last_missile = -1;        \
            /* Human PLUS at full (level 9): energy rules of VS 0x80083560. */            \
            if (i_ == 0 && human_plus && !d_->plus_applied) {                             \
                acr_gen1_human_plus_energy(&d_->def.stats, 9); d_->plus_applied = 1;      \
            }                                                                             \
            f_->def = &d_->def;                                                           \
            f_->ap = f_->ap_max = d_->ap;                                                 \
            f_->shell_factor = d_->shell_factor;                                          \
            f_->energy_factor = d_->energy_factor;                                        \
            f_->weapons = (uint8_t)(i_ == 0 || two_players || g_ai_mode ? d_->weapons : 0); \
            for (int w_ = 0; w_ < d_->weapons; ++w_) {                                    \
                f_->weapon[w_] = d_->weapon[w_];                                          \
                f_->ammo[w_] = d_->weapon[w_].ammo;                                       \
            }                                                                             \
            f_->current = 0;                                                              \
            f_->legs = (uint8_t)(pp[i_][SLOT_LEGS] >= 27 ? ACR_GEN1_LEGS_TRACKS            \
                       : pp[i_][SLOT_LEGS] >= 21 ? ACR_GEN1_LEGS_FOUR : ACR_GEN1_LEGS_TWO); \
            f_->human_plus = (uint8_t)(i_ == 0 && human_plus); f_->kneeling = 0;          \
        }                                                                                 \
        if (g_stage_on) { stage_spawn(P, def, 0); stage_spawn(T, def2, 1); }               \
        else {                                                                            \
            acr_gen1_ac_spawn(P, def, 0, -2500 * 64, 0);                                  \
            acr_gen1_ac_spawn(T, def2, target_side() * 64, (target_dist() - 2500) * 64, 2048); \
        }                                                                                 \
        memset(battle.shot, 0, sizeof battle.shot);                                       \
        round_ms = ROUND_MS; round_over = 0; round_result = 0; ko_ms = 0;                 \
        for (int i_ = 0; i_ < 2; ++i_) {                                                  \
            const acr_gen1_ai_profile p_ = ai_profile(g_ai_mode);                         \
            acr_gen1_ai_init(&g_ai[i_], &p_, 0x1234u + (uint32_t)i_ * 977u + (uint32_t)(++g_ai_round) * 7919u); \
        }                                                                                 \
    } while (0)
    int human_plus = 0, two_players = SDL_getenv("ACR_TWO") != NULL;
    if (SDL_getenv("ACR_AI")) g_ai_mode = ai_preset_of(SDL_getenv("ACR_AI"));
    #define ROUND_MS 120000   /* two minutes, like Master of Arena's VS mode */
    int32_t round_ms = ROUND_MS, ko_ms = 0;
    int round_over = 0, round_result = 0;
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
    int running = 1, show_help = 1, back_held = 0, back_combo = 0, switch_pressed[2] = { 0, 0 }, blade_pressed[2] = { 0, 0 };
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
        garage_command gcmd = GC_NONE;
        int to_garage = 0;
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
            if (mode == 0) {   /* the garage takes all other input */
                garage_action ga = GA_NONE;
                if (e.type == SDL_EVENT_KEY_DOWN) ga = key_action(e.key.scancode);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) ga = button_action(e.gbutton.button);
                const garage_command c = garage_input(&gui, &gdata, ga);
                if (c != GC_NONE) gcmd = c;
                continue;
            }
            /* Number keys 1-8 work as F1-F8 (laptop F keys often do other
             * things without Fn). */
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode >= SDL_SCANCODE_1 && e.key.scancode <= SDL_SCANCODE_8)
                e.key.scancode = (SDL_Scancode)(SDL_SCANCODE_F1 + (e.key.scancode - SDL_SCANCODE_1));
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) {
                if (g_direct_test) running = 0;
                else to_garage = 1;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F1) show_help = !show_help;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F5) g_smooth_anim = !g_smooth_anim;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F3) g_landing_anim = !g_landing_anim;
            int cycle_ai = 0, toggle_autopilot = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F4) cycle_ai = 1;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F6) toggle_autopilot = 1;
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && back_held && e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) { cycle_ai = 1; back_combo = 1; }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && back_held && e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) { toggle_autopilot = 1; back_combo = 1; }
            int toggle_bounds = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F8) toggle_bounds = 1;
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && back_held && e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) { toggle_bounds = 1; back_combo = 1; }
            if (toggle_bounds && g_stage_on) {
                /* Test boundary: out-of-area boxes shrunk to just around the
                 * two starts (box 1 1,500 past them, box 2 500 wider on x and
                 * 500 narrower on z), so the warning and the loss can be
                 * tried on any stage. The stages' own boxes aren't imported
                 * yet. */
                acr_gen1_stage *cs = &g_stage.collision;
                if (cs->bounds) cs->bounds = 0;
                else {
                    const int32_t *a = g_stage.start[0], *c = g_stage.start[1];
                    const int32_t x0 = SDL_min(a[0], c[0]) - 1500, x1 = SDL_max(a[0], c[0]) + 1500;
                    const int32_t z0 = SDL_min(a[2], c[2]) - 1500, z1 = SDL_max(a[2], c[2]) + 1500;
                    const acr_gen1_stage_bound bd[3] = {
                        { { -30000, -30000, -30000 }, { 30000, 30000, 30000 }, 0 },
                        { { x0, -30000, z0 }, { x1, 30000, z1 }, 1 },
                        { { x0 - 500, -30000, z0 + 500 }, { x1 + 500, 30000, z1 - 500 }, 2 },
                    };
                    SDL_memcpy(cs->bound, bd, sizeof bd);
                    cs->bounds = 3;
                }
                SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T;
            }
            int toggle_stage = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F7) toggle_stage = 1;
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && back_held && e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) { toggle_stage = 1; back_combo = 1; }
            if (toggle_stage) {
                /* Green Silo -> Ocean Platform -> the open test floor -> Green
                 * Silo (ACR_STAGE's stage, if another, comes first). A stage
                 * the import lacks is skipped. */
                static const int k_cycle[] = { 27, 39 };
                int want_id = 0;
                if (!g_stage_on) want_id = k_cycle[0];
                else
                    for (int k = 0; k < 2; ++k)
                        if (g_stage_id == k_cycle[k]) want_id = k + 1 < 2 ? k_cycle[k + 1] : 0;
                g_stage_on = 0;
                while (want_id) {
                    if (g_stage.ok && g_stage_id == want_id) { g_stage_on = 1; break; }
                    demo_stage tmp;
                    if (demo_stage_load(dir, want_id, &tmp)) {
                        if (g_stage.ok) demo_stage_free(&g_stage);
                        g_stage = tmp;
                        g_stage_id = want_id;
                        g_stage_on = 1;
                        break;
                    }
                    want_id = want_id == k_cycle[0] ? k_cycle[1] : 0;
                }
                def->stage = def2->stage = battle.stage = g_stage_on ? &g_stage.collision : NULL;
                SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T;
            }
            if (cycle_ai) {
                /* still target -> BALANCED ... EVASIVE (-> the --ai-file profile) -> still */
                int next = g_ai_mode == AI_FROM_FILE ? 0 : g_ai_mode + 1;
                if (next >= ACR_GEN1_AI_PRESETS && next != AI_FROM_FILE) next = g_ai_file_ok ? AI_FROM_FILE : 0;
                g_ai_mode = next;
                if (!g_ai_mode) g_autopilot = 0;
                SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T;
            }
            if (toggle_autopilot) {
                g_autopilot = !g_autopilot;
                if (g_autopilot && !g_ai_mode) g_ai_mode = ACR_GEN1_AI_BALANCED;
                SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_R) {
                SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_TAB) switch_pressed[0] = 1;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_V) blade_pressed[0] = 1;
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
                if (btn == SDL_GAMEPAD_BUTTON_RIGHT_STICK && back_held) { g_landing_anim = !g_landing_anim; back_combo = 1; }
                if (btn == SDL_GAMEPAD_BUTTON_EAST && !back_held) {   /* the blade: circle / B */
                    const int who = !two_players ? 0 : pads[1] ? (player_of(pads, e.gbutton.which, 0) == 1) : 1;
                    blade_pressed[who] = 1;
                }
                if (btn == SDL_GAMEPAD_BUTTON_NORTH && !back_held) {
                    /* which player's controller? */
                    const int who = !two_players ? 0 : pads[1] ? (player_of(pads, e.gbutton.which, 0) == 1) : 1;
                    switch_pressed[who] = 1;
                }
                if (btn == SDL_GAMEPAD_BUTTON_START && back_held && !g_direct_test) { to_garage = 1; back_combo = 1; }
                else if (btn == SDL_GAMEPAD_BUTTON_START) { SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T; }
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
            if (toggle_plus) {
                human_plus = !human_plus;
                if (!human_plus && ac_data[0].plus_applied) demo_load(dir, parts, &ac_data[0], err, sizeof err);
                REFRESH_FIGHTERS();
                battle.f[0].human_plus = (uint8_t)human_plus;
            }
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
            /* From the garage, player 1's parts come from the garage only. */
            if (cycle_slot >= 0 && !(cycle_slot == SLOT_BOOSTER && pp[cycle_who][SLOT_LEGS] >= 27) &&
                (g_direct_test || cycle_who == 1)) {
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
        if (to_garage) { mode = 0; gui.screen = 0; }
        if (mode == 0) {
            garage_action dir_action = pad_direction(pads[0], SDL_GetTicks());
            /* Testing aid: ACR_KEYS="ok,down,ok,..." feeds the garage one action a frame. */
            static const char *script;
            static int script_read;
            if (!script_read) { script = SDL_getenv("ACR_KEYS"); script_read = 1; }
            if (script && *script && dir_action == GA_NONE) {
                static const char *const k_names[] = { "", "up", "down", "left", "right", "ok", "back", "lb", "rb" };
                for (int k = 1; k < 9; ++k)
                    if (!SDL_strncmp(script, k_names[k], SDL_strlen(k_names[k]))) { dir_action = (garage_action)k; break; }
                script = SDL_strchr(script, ',');
                if (script) ++script;
            }
            if (dir_action != GA_NONE) {
                const garage_command c = garage_input(&gui, &gdata, dir_action);
                if (c != GC_NONE) gcmd = c;
            }
            if (gcmd == GC_QUIT) running = 0;
            if (gcmd == GC_CHANGED || gcmd == GC_TEST) {
                int fitted[SLOT_COUNT];
                hangar_parts(fitted);
                if (demo_load(dir, fitted, &ac_data[0], err, sizeof err)) memcpy(parts, fitted, sizeof fitted);
                hangar_save();
            }
            if (gcmd == GC_TEST) {   /* AC TEST: the test arena, the target built like you */
                mode = 1;
                memcpy(parts2, parts, sizeof parts2);
                demo_load(dir, parts2, &ac_data[1], err, sizeof err);
                set_rate(&clock, def, g_tick_rate);
                set_rate(&clock, def2, g_tick_rate);
                SETUP_FIGHTERS();
                prevs[0] = *P; prevs[1] = *T;
                last = SDL_GetTicksNS();
            }
        }
        if (mode == 0) {
            static Uint64 garage_last_ms;
            const Uint64 now_ms = SDL_GetTicks();
            const int elapsed = garage_last_ms ? (int)(now_ms - garage_last_ms) : 0;
            garage_last_ms = now_ms;
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
            acr_render_begin(pw, ph, 18, 22, 28);
            refresh_model(0, dir, parts, ac_data[0].model);
            draw_garage_ac(pw, ph, 480.0f * (float)pw / (float)ph, (unsigned)(now_ms / 16));
            garage_draw(&gui, &gdata, acr_render_hud(0, 0, pw, ph), elapsed);
            goto present;
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
        in2[1] = two_players ? read_input(ctl[1], 0) : (acr_gen1_input){ 0, 0, 0, 0, 0, 0, 0 };
        /* Testing aid: ACR_HOLD="move,turn,strafe,boost[,fire[,blade_ms[,look]]]" holds those
         * inputs (and presses the blade once, blade_ms after start);
         * ACR_SHOT=file.bmp ACR_SHOT_MS=2000 saves a picture and quits. */
        const char *hold = SDL_getenv("ACR_HOLD");
        int hold_fire = 0;
        if (hold) {
            int v[7] = { 0, 0, 0, 0, 0, -1, 0 };
            static int blade_done;
            SDL_sscanf(hold, "%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
            in2[0].look = (int8_t)v[6];
            if (v[5] >= 0 && !blade_done && SDL_GetTicks() >= (Uint64)v[5]) { blade_pressed[0] = 1; blade_done = 1; }
            in2[0].move = (int8_t)v[0]; in2[0].turn = (int8_t)v[1]; in2[0].strafe = (int8_t)v[2];
            in2[0].boost = (uint8_t)v[3];
            hold_fire = v[4];
        }
        const bool *keys = SDL_GetKeyboardState(NULL);
        acr_gen1_actions act[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
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
        g_in[0] = in2[0];
        g_in[1] = in2[1];

        const Uint64 now = SDL_GetTicksNS();
        const uint32_t ticks = acr_clock_advance(&clock, now - last);
        last = now;
        battle.step_ms = (uint16_t)(1000 / (g_tick_rate > 0 ? g_tick_rate : 22));
        battle.step_rate = def->step_rate;
        battle.gen1_rate = def->gen1_rate;
        def->landing_from_anim = def2->landing_from_anim = (uint8_t)g_landing_anim;
        for (uint32_t i = 0; i < ticks && !round_over; ++i) {
            prevs[0] = *P;
            prevs[1] = *T;
            /* Two players: a two-minute round, decided as the VS engine
             * does: a knock-out settles 12 ticks after it (both down is a
             * draw); at time up the higher AP wins, equal AP is a draw. */
            if (two_players || g_ai_mode) {
                round_ms -= battle.step_ms;
                if (round_ms < 0) round_ms = 0;
                const int res = acr_gen1_battle_result(&battle, round_ms <= 0);
                if (res && (round_ms <= 0 || (ko_ms += battle.step_ms) >= 12 * 1000 / 22)) {
                    round_over = 1;
                    round_result = res;
                    break;
                }
            }
            for (int k = 0; k < 2; ++k) {
                act[k].next_weapon = (uint8_t)switch_pressed[k]; switch_pressed[k] = 0;
                act[k].blade = (uint8_t)blade_pressed[k]; blade_pressed[k] = 0;
            }
            /* The computer pilot presses its buttons like a pad, each step. */
            for (int k = 0; k < 2; ++k) {
                const int by_ai = g_ai_mode && ((k == 1 && !two_players) || (k == 0 && g_autopilot));
                if (!by_ai) continue;
                acr_gen1_ai_step(&g_ai[k], &battle, k, g_stage_on ? g_stage.half : ARENA, &in2[k], &act[k]);
                g_in[k] = in2[k];
            }
            /* The arena's floors and walls (the defs are reloaded on part changes). */
            def->stage = def2->stage = battle.stage = g_stage_on ? &g_stage.collision : NULL;
            acr_gen1_battle_step(&battle, in2, act);
            for (int k = 0; k < 2; ++k) { in2[k].boost_tapped = 0; boost_released[k] = !in2[k].boost; }
            /* the open floor's square walls (the shown position too, so drawing stops there) */
            const int32_t lim = ARENA * 64;
            for (int k = 0; k < 2 && !g_stage_on; ++k) {
                acr_gen1_ac *q = &battle.f[k].ac;
                int32_t *const xs[3] = { &q->x, &q->px, &q->sx }, *const zs[3] = { &q->z, &q->pz, &q->sz };
                for (int c = 0; c < 3; ++c) {
                    if (*xs[c] > lim) { *xs[c] = lim; q->vx = 0; }
                    if (*xs[c] < -lim) { *xs[c] = -lim; q->vx = 0; }
                    if (*zs[c] > lim) { *zs[c] = lim; q->vz = 0; }
                    if (*zs[c] < -lim) { *zs[c] = -lim; q->vz = 0; }
                }
            }
            /* The target comes back 3 seconds after it is destroyed. */
            if (!two_players && !g_ai_mode && battle.f[1].ap <= 0) {
                respawn_ms += battle.step_ms;
                if (respawn_ms >= 3000) {
                    respawn_ms = 0;
                    battle.f[1].ap = battle.f[1].ap_max;
                    if (g_stage_on) stage_spawn(T, def2, 1);
                    else acr_gen1_ac_spawn(T, def2, target_side() * 64, (target_dist() - 2500) * 64, 2048);
                    prevs[1] = *T;
                }
            }
        }

        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
        acr_render_begin(pw, ph, 18, 22, 28);
        refresh_model(0, dir, parts, ac_data[0].model);
        refresh_model(1, dir, parts2, ac_data[1].model);
        const int views = two_players ? 2 : 1;
        for (int view = 0; view < views; ++view) {
        const int vx = view * pw / views, vpw = (view + 1) * pw / views - vx;
        const acr_gen1_ac ac = battle.f[view].ac;
        const acr_gen1_ac prev = prevs[view];
        const acr_gen1_ac *const T = &battle.f[1 - view].ac;
        const acr_gen1_ac tprev = prevs[1 - view];
        /* Interpolate the shown ACs (already between Gen 1 ticks) between
         * the last two steps. */
        const float a = (float)acr_clock_alpha(&clock) / (float)ACR_FX_ONE;
        const float px = (prev.sx + (ac.sx - prev.sx) * a) / 64.0f;
        const float py = -(prev.sy + (ac.sy - prev.sy) * a) / 64.0f;
        const float pz = (prev.sz + (ac.sz - prev.sz) * a) / 64.0f;
        int dyaw = (int)ac.syaw - (int)prev.syaw;
        if (dyaw > 2048) dyaw -= 4096;
        if (dyaw < -2048) dyaw += 4096;
        const float yaw = ((float)prev.syaw + dyaw * a) * (6.2831853f / 4096.0f);

        /* The game's camera, measured in the recomp (same rig in the first
         * mission and in VS): eye 325 behind the AC and 227 up, looking at
         * a point 81 ahead and 187 up (about 6 degrees down), view centred.
         * Single player shows 41 degrees vertically on 4:3 (H 320); VS
         * keeps the same 53 degrees across on its tall half views (H 160 on
         * 160x240). So: 41 degrees vertically, widened when needed to keep
         * at least 53 degrees across. The engine's right is (cos, 0, -sin)
         * of the facing, so x is mirrored after the usual look-at. */
        float proj[16], viewm[16];
        {
            const float aspect = (float)vpw / (float)ph, half_v = 0.375f, half_h = 0.5f;
            const float t = half_h / aspect > half_v ? half_h / aspect : half_v;
            acr_mat_perspective(proj, 2.0f * atanf(t), aspect, 12.0f, 80000.0f);
        }
        {
            /* L2/R2 tilt (measured): the look-at point turns on a circle of
             * 82 around a pivot 186 up the AC's axis, up to ~66 degrees; the
             * eye sits opposite, 328 out and 7.5 degrees above level when
             * level (stretching to ~365 looking down), kept 100 above the
             * floor (measured at the up limit: 93 behind, 103 up). */
            /* The camera's swing toward the target (acr_gen1_camera: VS
             * 0x8008774C), between ticks; the chase camera clamps the
             * pitch to +-800. */
            const acr_gen1_fighter *cf = &battle.f[view];
            const float ct = acr_gen1_tick_fraction(&ac);
            const float cam_p = cf->cam_pitch_prev + (cf->cam_pitch - cf->cam_pitch_prev) * ct,
                        cam_y = cf->cam_yaw_prev + (cf->cam_yaw - cf->cam_yaw_prev) * ct;
            float th_units = (float)prev.pitch + (float)(ac.pitch - prev.pitch) * a + cam_p;
            if (th_units > ACR_GEN1_CAM_PITCH_MAX) th_units = ACR_GEN1_CAM_PITCH_MAX;
            if (th_units < -ACR_GEN1_CAM_PITCH_MAX) th_units = -ACR_GEN1_CAM_PITCH_MAX;
            const float th = th_units * (6.2831853f / 4096.0f);
            const float cyaw = yaw + cam_y * (6.2831853f / 4096.0f);
            const float fx = sinf(cyaw), fz = cosf(cyaw), pivot = 186.0f;
            const float lo = 82.0f * cosf(th), lu = pivot + 82.0f * sinf(th);
            const float phi = 0.1309f - th, down = th < 0 ? -th / 1.1519f : 0.0f;
            float eb = (328.0f + 37.0f * down) * cosf(phi), eu = (328.0f + 37.0f * down) * sinf(phi);
            if (pivot + eu < 100.0f && eu < 0) { const float t = (pivot - 100.0f) / -eu; eb *= sqrtf(t); eu *= t; }
            /* The game's chase camera eases toward its spot a quarter of the
             * way each Gen 1 tick (camera +0x0E = 2, from stat +0x3C; AC1Recomp
             * docs/mapping/stat-block.md), so it trails a fast AC and swings
             * round on turns. Kept per second at any frame rate; snaps on a
             * jump (reset, respawn). */
            static float cam_eye[2][3], cam_look[2][3];
            static int cam_ok[2];
            static Uint64 cam_ns[2];
            float eye[3] = { px - fx * eb, py + pivot + eu, pz - fz * eb };
            float look[3] = { px + fx * lo, py + lu, pz + fz * lo };
            {
                const Uint64 now_ns = SDL_GetTicksNS();
                const float dt = cam_ok[view] ? (float)(now_ns - cam_ns[view]) / 1e9f : 0.0f;
                cam_ns[view] = now_ns;
                const float ex = eye[0] - cam_eye[view][0], ey = eye[1] - cam_eye[view][1], ez = eye[2] - cam_eye[view][2];
                if (!cam_ok[view] || ex * ex + ey * ey + ez * ez > 2000.0f * 2000.0f || dt > 0.5f) {
                    for (int c = 0; c < 3; ++c) { cam_eye[view][c] = eye[c]; cam_look[view][c] = look[c]; }
                    cam_ok[view] = 1;
                } else {
                    /* The eye moves a quarter of the way a tick (camera
                     * +0x0E = 2) and the look-at point half (VS 0x80087B90). */
                    const float k = 1.0f - powf(0.75f, 22.0f * dt), kl = 1.0f - powf(0.5f, 22.0f * dt);
                    cam_eye[view][0] += ex * k; cam_eye[view][1] += ey * k; cam_eye[view][2] += ez * k;
                    for (int c = 0; c < 3; ++c) cam_look[view][c] += (look[c] - cam_look[view][c]) * kl;
                }
                if (g_stage_on) {
                    /* Above the floor under the eye (the fight's y is down). */
                    int32_t fy;
                    if (acr_gen1_stage_floor(&g_stage.collision, (int32_t)cam_eye[view][0], (int32_t)cam_eye[view][2],
                                             (int32_t)-cam_eye[view][1] - 400, &fy) &&
                        cam_eye[view][1] < (float)-fy + 100.0f)
                        cam_eye[view][1] = (float)-fy + 100.0f;
                } else if (cam_eye[view][1] < 100.0f) cam_eye[view][1] = 100.0f;
                for (int c = 0; c < 3; ++c) { eye[c] = cam_eye[view][c]; look[c] = cam_look[view][c]; }
            }
            const float fwd[3] = { look[0] - eye[0], look[1] - eye[1], look[2] - eye[2] };
            const float up[3] = { 0, 1, 0 };
            acr_mat_look(viewm, eye, fwd, up);
            for (int k = 0; k < 4; ++k) viewm[4 * k] = -viewm[4 * k];
        }
        const acr_fog fog = { 30000.0f, 60000.0f, 18, 22, 28 };
        acr_render_view3d(vx, 0, vpw, ph, proj, viewm, &fog);

        /* the arena, or the open floor's grid and walls */
        if (g_stage_on) demo_stage_draw(&g_stage, 0);
        for (int g = -ARENA; g <= ARENA && !g_stage_on; g += 2000) {
            line3(v3((float)g, 0, (float)-ARENA), v3((float)g, 0, (float)ARENA), 52, 70, 76);
            line3(v3((float)-ARENA, 0, (float)g), v3((float)ARENA, 0, (float)g), 52, 70, 76);
        }
        for (int w = 0; w < 4 && !g_stage_on; ++w) {
            const float s0 = (w & 1) ? ARENA : -ARENA;
            for (int h = 0; h <= 3000; h += 1000) {
                if (w < 2) line3(v3(s0, (float)h, -ARENA), v3(s0, (float)h, ARENA), 120, 150, 160);
                else line3(v3(-ARENA, (float)h, s0), v3(ARENA, (float)h, s0), 120, 150, 160);
            }
        }
        /* pillars for a sense of speed, then the ACs and shots */
        for (int i = -2; i <= 2 && !g_stage_on; ++i)
            for (int j = -2; j <= 2; ++j)
                if (i || j) add_box(v3(i * 7000.0f, 1500, j * 7000.0f), 0, v3(300, 1500, 300), 0.35f, 0.42f, 0.48f);
        float crouch = 0;
        if (ac.state == ACR_GEN1_CROUCH) crouch = 90;
        if (ac.state == ACR_GEN1_LANDING) crouch = 70;
        if (battle.f[view].kneeling) crouch = 150;
        flush_scene();
        for (int k = 0; k < 2; ++k) {
            g_swinging[k] = battle.f[k].swing != 0;
            g_aim_table[k] = acr_gen1_aim(&battle, k, g_aim[k]);
            /* The muzzle drawn last frame feeds the battle's shots and lock. */
            acr_gen1_fighter *fk = &battle.f[k];
            const acr_gen1_weapon *wk = fk->weapons && fk->current < fk->weapons ? &fk->weapon[fk->current] : NULL;
            g_muzzle_parts[k] = !wk || wk->kind == ACR_GEN1_BLADE ? 0
                              : wk->mount == ACR_GEN1_MOUNT_HAND ? 1u << ACR_PART_HAND
                              : wk->mount == ACR_GEN1_MOUNT_ARMS ? 1u << ACR_PART_ARMS
                              : (1u << ACR_PART_BACK_LEFT) | (1u << ACR_PART_BACK_RIGHT);
            fk->muzzle_set = (uint8_t)g_muzzle[k].ok;
            if (g_muzzle[k].ok)
                for (int c = 0; c < 3; ++c) fk->muzzle[c] = (int16_t)g_muzzle[k].p[c];
            g_in[k].crouch = battle.f[k].kneeling;   /* the cannon stance keeps its own pose */
        }
        draw_ac(view, &ac, px, py, pz, yaw, crouch);
        const float tx = (tprev.sx + (T->sx - tprev.sx) * a) / 64.0f, ty = -(tprev.sy + (T->sy - tprev.sy) * a) / 64.0f,
                    tz = (tprev.sz + (T->sz - tprev.sz) * a) / 64.0f;
        int tdyaw = (int)T->syaw - (int)tprev.syaw;
        if (tdyaw > 2048) tdyaw -= 4096;
        if (tdyaw < -2048) tdyaw += 4096;
        if (battle.f[1 - view].ap > 0)
            draw_ac(1 - view, T, tx, ty, tz, ((float)tprev.syaw + tdyaw * a) * (6.2831853f / 4096.0f), 0);
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
            const acr_gen1_shot *sh = &battle.shot[k];
            if (!sh->active) continue;
            const int missile = sh->missile;
            if (missile) add_box(v3(sh->x / 64.0f, -sh->y / 64.0f, sh->z / 64.0f), 0, v3(10, 10, 10), 1.0f, 0.55f, 0.15f);
            else add_box(v3(sh->x / 64.0f, -sh->y / 64.0f, sh->z / 64.0f), 0, v3(5, 5, 5), 1.0f, 0.95f, 0.5f);
        }
        if (battle.f[view].swing >= ACR_GEN1_BLADE_LUNGE_MS) {   /* blade arc while lunging */
            float r = 205.0f;
            for (int k = 0; k < battle.f[view].weapons; ++k)
                if (battle.f[view].weapon[k].kind == ACR_GEN1_BLADE) r = (float)battle.f[view].weapon[k].range;
            for (int k = -3; k < 3; ++k) {
                const float a0 = yaw + k * 0.25f, a1 = yaw + (k + 1) * 0.25f;
                line3(v3(px + sinf(a0) * r, py + 110, pz + cosf(a0) * r),
                      v3(px + sinf(a1) * r, py + 110, pz + cosf(a1) * r), 120, 200, 255);
            }
        }
        {   /* height marker, down to the floor under the AC */
            float ground = 0;
            int32_t fy;
            if (g_stage_on && acr_gen1_stage_floor(&g_stage.collision, (int32_t)px, (int32_t)pz, (int32_t)-py - 120, &fy))
                ground = (float)-fy;
            if (py > ground + 1.0f) line3(v3(px, ground, pz), v3(px, py, pz), 230, 160, 40);
        }
        flush_scene();
        if (g_stage_on) demo_stage_draw(&g_stage, 1);   /* see-through faces last */

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
        /* Speeds per Gen 1 tick, as the recomp measures them. */
        const int dx = (ac.x - ac.px) / 64, dz = (ac.z - ac.pz) / 64, dy = (ac.y - ac.py) / 64;
        char line[160];
        #define TEXT(x, y, s) acr_hud_text((x), (y), (s), 220, 230, 235)
        SDL_snprintf(line, sizeof line, "ENERGY %5d  %-7s  SPEED %3d  RISE %4d", (int)ac.energy,
                     k_state[ac.state], (int)sqrtf((float)(dx * dx + dz * dz)), -dy);
        TEXT(20, g_vh - 48, line);
        /* Landing check: the landing animation's length against the pause
         * the latest landing really had, and its touchdown speed (the recomp
         * measured about 20 ticks after a hop that touched down at 5,000+). */
        {
            const acr_gen1_root_track *land = ac_data[view].def.anim[ACR_GEN1_ANIM_LAND];
            char land_len[16];
            if (land && land->length) SDL_snprintf(land_len, sizeof land_len, "%d TICKS", (int)land->length);
            else SDL_snprintf(land_len, sizeof land_len, "NOT FOUND");
            SDL_snprintf(line, sizeof line, "LANDING ANIM %s  LAST LANDING %d TICKS, FALL %d  PAUSE: %s",
                         land_len, (int)ac.landing_pause, (int)ac.vy_at_touchdown,
                         g_landing_anim ? "ANIMATION" : "GUESS");
            TEXT(20, g_vh - 80, line);
        }
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
                    /* The game's sight (VS 0x800576E0): the inner lock box as
                     * an octagon, half-sizes sin(angle) and corners cut by 512,
                     * on a frame 16,384 out along the AC's facing and view tilt,
                     * seen through the camera. When the camera trails or swings
                     * toward the target, the sight drifts off the screen's
                     * centre. Locks are judged on the target's middle, so an AC
                     * whose body pokes out of the sight can stay locked. */
                    const float pt = ((float)prev.pitch + (float)(ac.pitch - prev.pitch) * a) * (6.2831853f / 4096.0f);
                    const float D = (float)ACR_GEN1_SIGHT_DEPTH, cut = (float)ACR_GEN1_SIGHT_CORNER;
                    const float sw = sinf((float)(w->inner_w ? w->inner_w : w->box_w) * (6.2831853f / 4096.0f)) * D,
                                sh = sinf((float)(w->inner_h ? w->inner_h : w->box_h) * (6.2831853f / 4096.0f)) * D;
                    const vec3 fw = v3(sinf(yaw) * cosf(pt), sinf(pt), cosf(yaw) * cosf(pt)),
                               up = v3(-sinf(yaw) * sinf(pt), cosf(pt), -cosf(yaw) * sinf(pt)),
                               rt = v3(cosf(yaw), 0, -sinf(yaw));
                    const vec3 o = v3(px, py + 186.0f, pz);
                    const float oct[8][2] = { { -sw + cut, sh }, { sw - cut, sh }, { sw, sh - cut }, { sw, -sh + cut },
                                              { sw - cut, -sh }, { -sw + cut, -sh }, { -sw, -sh + cut }, { -sw, sh - cut } };
                    float hx[8], hy[8];
                    int seen = 1;
                    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                    for (int c = 0; c < 8 && seen; ++c) {
                        const float r = oct[c][0], u = oct[c][1];
                        seen = to_hud(proj, viewm, v3(o.x + rt.x * r + up.x * u + fw.x * D, o.y + up.y * u + fw.y * D,
                                                      o.z + rt.z * r + up.z * u + fw.z * D), g_vw, &hx[c], &hy[c]);
                        if (hx[c] < x0) x0 = hx[c];
                        if (hx[c] > x1) x1 = hx[c];
                        if (hy[c] < y0) y0 = hy[c];
                        if (hy[c] > y1) y1 = hy[c];
                    }
                    float bx = g_vw * 0.5f, by = g_vh * 0.45f, bw = 0, bh = 0;
                    if (seen) {
                        bx = (x0 + x1) * 0.5f; by = (y0 + y1) * 0.5f; bw = x1 - x0; bh = y1 - y0;
                        /* The sight takes the lock's colour once the target is locked. */
                        const uint8_t cr = me->locked ? 255 : 90, cg = me->locked ? 60 : 200, cb = me->locked ? 50 : 120;
                        for (int c = 0; c < 8; ++c)
                            acr_hud_line(hx[c], hy[c], hx[(c + 1) & 7], hy[(c + 1) & 7], cr, cg, cb, 255);
                    }
                    if (w->kind == ACR_GEN1_MISSILE) {
                        /* The missile lock cursor (player block +0x20 / +0x22) and the locks held. */
                        const float cy = tanf(me->cursor_yaw * (6.2831853f / 4096.0f)),
                                    cp = tanf(me->cursor_pitch * (6.2831853f / 4096.0f));
                        const float cx = bx + cy * proj[0] * g_vw * 0.5f, cyy = by - cp * proj[5] * g_vh * 0.5f;
                        acr_hud_rect(cx - 6, cyy - 6, 12, 12, 240, 220, 60, 255, 0);
                        if (me->locks) {
                            SDL_snprintf(line, sizeof line, "LOCK x%d", (int)me->locks);
                            acr_hud_text(bx + bw * 0.5f + 6, by - bh * 0.5f, line, 255, 60, 50);
                        }
                    }
                }
                float sx, sy;
                if (tg->ap > 0 && acr_gen1_in_lock_box(&battle, view, w) &&
                    to_hud(proj, viewm, v3(tx, ty + 100, tz), g_vw, &sx, &sy)) {
                    if (me->locked) {
                        acr_hud_rect(sx - 18, sy - 18, 36, 36, 255, 60, 50, 255, 0);
                        acr_hud_text(sx - 16, sy + 22, "LOCK", 255, 60, 50);
                    } else {
                        acr_hud_rect(sx - 18, sy - 18, 36, 36, 240, 220, 60, 255, 0);
                    }
                }
            }
            if (me->human_plus) TEXT(20, g_vh - 64, "HUMAN PLUS");
            if (me->hit_flash & 4) acr_hud_text(g_vw * 0.5f - 12, g_vh * 0.45f + 40, "HIT", 255, 90, 60);
            /* Out of area: the warning outside the first inner box, then the loss. */
            {
                const int out = acr_gen1_battle_out_of_area(&battle, view);
                if (out) acr_hud_text(g_vw * 0.5f - 40, g_vh * 0.30f, out == 2 ? "OUT OF AREA" : "AREA WARNING", 255, 80, 60);
            }
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
            TEXT(20, 40, "STRAFE: LB/RB  BOOST: A (NO DIRECTION = RISE)  LOOK UP/DOWN: LT/RT, BOTH = LEVEL (PGUP/PGDN)");
            TEXT(20, 52, "FIRE: X (F)  SWITCH WEAPON: Y (TAB)  BLADE: B (V)");
            if (g_direct_test) {
                TEXT(20, 64, "BACK+X/Y/B/RB (B/G/C/L): BOOSTER/GENERATOR/BACK WEAPON/LEGS");
                TEXT(20, 76, "START/R: RESET  F1: HIDE HELP  F5: ANIM SMOOTH/STEPPED  ESC: QUIT  (KEYS 1-8 = F1-F8)");
            } else {
                TEXT(20, 64, "BACK+START (ESC): BACK TO THE GARAGE");
                TEXT(20, 76, "START/R: RESET  F1: HIDE HELP  (KEYS 1-8 WORK AS F1-F8)");
            }
            TEXT(20, 88, "BACK+A (H): HUMAN PLUS  BACK (T): STEP RATE");
            TEXT(20, 100, "BACK+LB (F2): TWO PLAYERS  BACK+R3 (F3): LANDING PAUSE GUESS/ANIMATION");
            TEXT(20, 112, "BACK+UP (F4): OPPONENT STILL / AI PRESETS  BACK+DOWN (F6): AI VS AI (WATCH)");
            if (g_stage.ok) TEXT(20, 136, g_stage.collision.bounds ? "BACK+RIGHT (F8): TEST BOUNDARY ON (1,500 PAST THE STARTS)"
                                                                   : "BACK+RIGHT (F8): TEST BOUNDARY OFF");
            TEXT(20, 124, g_stage.ok ? "BACK+LEFT (F7): GREEN SILO / OCEAN PLATFORM / OPEN TEST FLOOR"
                                     : "BACK+LEFT (F7): ARENA STAGE (RE-IMPORT THE DISC FOR STAGES)");
        }
        }
        /* Whole-screen HUD: the split line, round clock and result. */
        const float full_w = acr_render_hud(0, 0, pw, ph);
        if (two_players || g_ai_mode) {
            if (two_players) acr_hud_rect(full_w / 2.0f - 1, 0, 2, 480, 0, 0, 0, 255, 1);
            char clock_text[16];
            SDL_snprintf(clock_text, sizeof clock_text, "%d:%02d", (int)(round_ms / 60000), (int)(round_ms / 1000 % 60));
            acr_hud_text(full_w / 2.0f - 16, 464, clock_text, 240, 240, 240);   /* between the energy bars */
        }
        if (!two_players && g_ai_mode) {
            char who[64];
            SDL_snprintf(who, sizeof who, "%sOPPONENT: AI %s", g_autopilot ? "AI VS AI  " : "",
                         g_ai_mode == AI_FROM_FILE ? "FROM FILE" : acr_gen1_ai_preset_name(g_ai_mode));
            acr_hud_text(full_w / 2.0f - 90, 448, who, 255, 200, 120);
        }
        if ((two_players || g_ai_mode) && round_over) {
            const int vs_ai = !two_players;
            acr_hud_text(full_w / 2.0f - 120, 240,
                         round_result == 1 ? (vs_ai && !g_autopilot ? "YOU WIN - START TO RESET" : "PLAYER 1 WINS - START TO RESET") :
                         round_result == 2 ? (vs_ai ? "THE AI WINS - START TO RESET" : "PLAYER 2 WINS - START TO RESET")
                                           : "DRAW - START TO RESET", 255, 230, 120);
            if (round_ms <= 0) acr_hud_text(full_w / 2.0f - 40, 254, "TIME UP", 255, 230, 120);
            else if (acr_gen1_battle_out_of_area(&battle, 0) == 2 || acr_gen1_battle_out_of_area(&battle, 1) == 2)
                acr_hud_text(full_w / 2.0f - 44, 254, "OUT OF AREA", 255, 230, 120);
        }
        #undef TEXT
    present:;
        const char *shot = SDL_getenv("ACR_SHOT");
        int spw = 0, sph = 0;
        SDL_GetWindowSizeInPixels(window, &spw, &sph);
        if (shot && (SDL_GetTicksNS() - start_ns) / 1000000 >= (Uint64)SDL_atoi(SDL_getenv("ACR_SHOT_MS") ? SDL_getenv("ACR_SHOT_MS") : "2000")) {
            SDL_Surface *img = SDL_CreateSurface(spw, sph, SDL_PIXELFORMAT_RGBA32);
            uint8_t *buf = img ? malloc((size_t)spw * (size_t)sph * 4) : NULL;
            if (buf) {
                acr_render_read(spw, sph, buf);
                for (int y = 0; y < sph; ++y)      /* GL rows run bottom up */
                    memcpy((uint8_t *)img->pixels + (size_t)y * (size_t)img->pitch,
                           buf + (size_t)(sph - 1 - y) * (size_t)spw * 4, (size_t)spw * 4);
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
