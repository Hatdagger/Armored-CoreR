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
 * Netplay, 1v1 with rollback (each PC uses its own garage build):
 *                 --host [PORT]        host a match as player 1 (port 7777)
 *                 --join ADDR[:PORT]   join the host as player 2
 *                 --host-online        host over the internet: shows a mission briefing
 *                 --join-brief "TEXT"  join over the internet with player 1's briefing
 *                 --readback "TEXT"    (host, testing) enter player 2's read-back at once
 *                 --guest-port N       (testing) the joiner's own UDP port
 *                 [--stage N]          host: stage number (27 Green Silo, 39 Ocean
 *                                      Platform, 0 the open test floor)
 *                 [--net-delay N]      host: input delay in steps (default 1)
 *                 [--net-lag MS] [--net-jitter MS] [--net-loss PCT]
 *                                      testing: fake a bad connection (each way)
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
#include "demo_units.h"
#include "../../model/acr_aim.h"
#include "garage.h"
#include "rogue.h"
#include "arena.h"
#include "missions.h"
#include "controls.h"
#include "settings.h"
#include "battle_hud.h"
#include "fx.h"
#include "sfx.h"

/* HUD framing: the PS1's 4:3 layout in the middle (default) or spread to a
 * wide screen's edges (F9 / --hud-wide). */
static int g_hud_frame = BATTLE_HUD_FRAME_43;
static int g_hud_frame_arg;   /* --hud-wide given: it wins over SETTINGS > VIDEO */
#include "net_link.h"
#include "net_menu.h"
#include "net_brief_ui.h"
#include "../../net/acr_rollback.h"

/* Netplay (--host / --join): a 1v1 over the network with rollback
 * (src/net/acr_rollback.h). Each PC shows its own player full screen. */
static int g_net;                         /* 1 while in an online match */
static int g_net_cmd;                     /* --host (1) or --join (2) on the command line */
static int g_net_host;                    /* this PC hosts (player 1) */
static int g_net_port = NET_PORT_DEFAULT;
static const char *g_net_join;            /* the host's address */
static int g_net_delay = 1;               /* input delay in steps (host decides) */
static int g_net_lag, g_net_jitter, g_net_loss;   /* testing: fake bad connection */
static int g_test_boundary;               /* AC TEST: F8's test boundary in place of the stage's own boxes */
static int g_net_stage = -1;              /* host: --stage N (-1: ACR_STAGE or Green Silo) */
static int g_net_coop;                    /* host: --coop N, co-op against N AI ACs (0 = VS) */
static int g_net_online;                  /* --host-online / --join-brief: over the internet with briefing codes */
static const char *g_net_brief;           /* --join-brief "TEXT": player 1's briefing */
static const char *g_net_readback;
static int g_net_guest_port;              /* --guest-port N: the guest's own port (testing) */        /* --readback "TEXT": host enters player 2's read-back (testing) */
static int g_part_args;                   /* parts were given on the command line */
#include "acr_import.h"
#include "disc_pick.h"
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
    acr_gen1_input in = { 0 };
    if (keyboard) {
    /* The keys as mapped on the CONTROLS page (controls.c). */
    in.move = (int8_t)(controls_key_held(CK_FORWARD) - controls_key_held(CK_BACKWARD));
    in.turn = (int8_t)(controls_key_held(CK_TURN_R) - controls_key_held(CK_TURN_L));
    in.strafe = (int8_t)(controls_key_held(CK_STRAFE_R) - controls_key_held(CK_STRAFE_L));
    in.boost = (uint8_t)controls_key_held(CK_BOOST);
    {
        const int up = controls_key_held(CK_LOOK_UP), down = controls_key_held(CK_LOOK_DOWN);
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
        /* The buttons as mapped on the CONTROLS page (controls.c). */
        if (controls_held(pad, CT_STRAFE_L) || rx < -dead) in.strafe = -1;
        if (controls_held(pad, CT_STRAFE_R) || rx > dead) in.strafe = 1;
        if (controls_held(pad, CT_BOOST)) in.boost = 1;
        /* Tilt the view up / down (L2 / R2 as standard); both together: back to level. */
        const int lt = controls_held(pad, CT_LOOK_UP);
        const int rt = controls_held(pad, CT_LOOK_DOWN);
        if (lt || rt) in.look = (int8_t)(lt && rt ? 2 : lt - rt);
    }
    return in;
}

/* Modern controls (the SP-MODERN option): a twin-stick controller is
 * required, laid out after Armored Core VI's Type A, with Gen 1's weapons
 * (one selected at a time, switched with triangle):
 *   left stick   move and strafe        right stick  turn and tilt the view
 *   cross        jump; hold to rise while the boost toggle is on
 *   L1           boost toggle (the only ground boost) square  quick boost
 *   R2           fire the selected weapon            L2      blade
 *   triangle     switch weapon                       R3      hard lock
 *   d-pad down   view back to level
 * The keyboard does nothing for a modern pilot. Presses (circle, square,
 * R3, L2) come from the events / modern_poll, not from here. */
static acr_gen1_input read_modern(SDL_Gamepad *pad, int cruise) {
    acr_gen1_input in = { 0 };
    if (!pad) return in;
    const int dead = 12000;
    const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    const int rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX), ry = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
    in.move = (int8_t)(ly < -dead ? 1 : ly > dead ? -1 : 0);
    in.strafe = (int8_t)(lx < -dead ? -1 : lx > dead ? 1 : 0);
    in.turn = (int8_t)(rx < -dead ? -1 : rx > dead ? 1 : 0);
    in.look = (int8_t)(ry < -dead ? 1 : ry > dead ? -1 : 0);
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) in.look = 2;
    in.boost = (uint8_t)SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
    in.cruise = (uint8_t)(cruise != 0);
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
static demo_model g_model[ACR_GEN1_MAX_FIGHTERS];
static int g_model_parts[ACR_GEN1_MAX_FIGHTERS][SLOT_COUNT], g_model_set[ACR_GEN1_MAX_FIGHTERS] = { -1, -1, -1, -1 };
static const uint8_t k_colours_p2[4][3] = { { 46, 12, 10 }, { 18, 18, 20 }, { 52, 44, 16 }, { 10, 10, 12 } };
/* Co-op's computer ACs (fighters 2 and 3): dark grey and red, made-up too. */
static const uint8_t k_colours_ai[4][3] = { { 22, 22, 24 }, { 40, 8, 8 }, { 34, 34, 36 }, { 8, 8, 10 } };

/* Aiming (acr_aim.h): each player's aim table and angles, set from the
 * battle before drawing, and the eased joints. */
static int g_aim_table[ACR_GEN1_MAX_FIGHTERS] = { -1, -1, -1, -1 };
/* Where each AC's current weapon fires from, as drawn (see draw_aimed). */
static unsigned g_muzzle_parts[ACR_GEN1_MAX_FIGHTERS];
static struct { int ok; float p[3]; } g_muzzle[ACR_GEN1_MAX_FIGHTERS];
static int16_t g_aim[ACR_GEN1_MAX_FIGHTERS][3];
static struct { acr_aim_state st; Uint64 last_ns; int ok; } g_aiming[ACR_GEN1_MAX_FIGHTERS];

/* Rogue Arena paints both ACs (the run's colours, the opponent's own). */
static int g_paint_on[ACR_GEN1_MAX_FIGHTERS];
static uint8_t g_paint[ACR_GEN1_MAX_FIGHTERS][4][3], g_model_paint[ACR_GEN1_MAX_FIGHTERS][4][3];
static int g_model_paint_on[ACR_GEN1_MAX_FIGHTERS];
/* The player's own AC (fighter 0 when nothing else paints it): CHANGE
 * COLOR's per-part paint from the garage. */
static acr_gen1_hangar g_hangar;
static uint8_t g_model_hangar_paint[4][4][3];
static int g_model_hangar_painted;
static int g_hangar_fighter;   /* which fighter flies the garage's AC (online: your own player) */

static void refresh_model(int who, const char *dir, const int parts[SLOT_COUNT], int set) {
    const int hangar_paint = who == g_hangar_fighter && !g_paint_on[who] && !g_part_args;
    if (g_model_set[who] == set && !memcmp(g_model_parts[who], parts, sizeof g_model_parts[who]) &&
        g_model_paint_on[who] == g_paint_on[who] && (!g_paint_on[who] || !memcmp(g_model_paint[who], g_paint[who], sizeof g_paint[who])) &&
        (who != g_hangar_fighter || (g_model_hangar_painted == hangar_paint &&
                      (!hangar_paint || !memcmp(g_model_hangar_paint, g_hangar.paint, sizeof g_model_hangar_paint)))))
        return;
    g_aiming[who].ok = 0;
    demo_model_free(&g_model[who]);
    g_model_paint_on[who] = g_paint_on[who];
    memcpy(g_model_paint[who], g_paint[who], sizeof g_paint[who]);
    if (who == g_hangar_fighter) { g_model_hangar_painted = hangar_paint; memcpy(g_model_hangar_paint, g_hangar.paint, sizeof g_model_hangar_paint); }
    if (hangar_paint) demo_model_load_paint(dir, set, parts, (const uint8_t (*)[4][3])g_hangar.paint, &g_model[who]);
    else demo_model_load(dir, set, parts, g_paint_on[who] ? (const uint8_t (*)[3])g_paint[who]
                                          : who >= 2 ? k_colours_ai : who ? k_colours_p2 : NULL, &g_model[who]);
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
static int g_swinging[ACR_GEN1_MAX_FIGHTERS];      /* set from the battle before drawing */
static acr_gen1_input g_in[ACR_GEN1_MAX_FIGHTERS];   /* each AC's latest input, for the poses */
static struct { int shown; Uint64 since; } g_pose[ACR_GEN1_MAX_FIGHTERS];

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
static struct { int anim, ok; acr_pose drawn, from; Uint64 fade_ns; uint16_t fade_step; } g_blend[ACR_GEN1_MAX_FIGHTERS];

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
#define AI_ROGUE 101                     /* the Rogue Arena opponent's own pilot */

/* The screen the game is on. 2-5 are the network screens (net_menu.c,
 * net_brief_ui.c): 2 the NETWORK menu, 3 a match's status, 4 its end,
 * 5 the mission-briefing editor. */
#define MODE_GARAGE     0
#define MODE_FIGHT      1
#define MODE_NET_FIRST  2
#define MODE_NET_LAST   5
#define MODE_ROGUE      10
#define MODE_CONTROLS   11
#define MODE_ARENA      12
#define MODE_MISSIONS   13
#define MODE_SETTINGS   14
static int g_ai_mode = 0;
static acr_gen1_ai_profile g_rogue_prof;
static int g_plus2 = 0;                  /* the opponent's Human PLUS level (Rogue Arena) */
static int g_rogue_fight = 0;            /* a menu-run fight is on: 1 Rogue Arena, 2 the Arena, 3 a mission */
static int32_t g_round_start_ms = 120000; /* a round's length (the Arena's timer can change it) */
static int g_round_clock = 1;            /* 0 = no time limit (the Arena's NONE) */
/* The fight the menus set up (Rogue Arena's or the Arena's). */
static const rogue_fight *menu_fight(void) {
    return g_rogue_fight == 3 ? missions_fight_info() : g_rogue_fight == 2 ? arena_fight_info() : rogue_fight_info();
}
static acr_gen1_ai g_ai[ACR_GEN1_MAX_FIGHTERS];
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
    if (mode == AI_ROGUE) return g_rogue_prof;
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
        if (!strcmp(argv[i], "--hud-wide")) { g_hud_frame = BATTLE_HUD_FRAME_WIDE; g_hud_frame_arg = 1; continue; }
        if (!strcmp(argv[i], "--ai") && i + 1 < argc) { g_ai_mode = ai_preset_of(argv[++i]); if (!g_ai_mode) g_ai_mode = 1; continue; }
        if (!strcmp(argv[i], "--ai-file") && i + 1 < argc) { load_ai_file(argv[++i]); continue; }
        if (!strcmp(argv[i], "--ai-vs-ai")) { g_autopilot = 1; if (!g_ai_mode) g_ai_mode = 1; continue; }
        if (!strcmp(argv[i], "--host")) {
            g_net_cmd = 1; g_direct_test = 1;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') g_net_port = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--join") && i + 1 < argc) { g_net_cmd = 2; g_net_join = argv[++i]; g_direct_test = 1; continue; }
        if (!strcmp(argv[i], "--host-online")) { g_net_cmd = 1; g_net_online = 1; g_direct_test = 1; continue; }
        if (!strcmp(argv[i], "--join-brief") && i + 1 < argc) { g_net_cmd = 2; g_net_online = 1; g_net_brief = argv[++i]; g_direct_test = 1; continue; }
        if (!strcmp(argv[i], "--guest-port") && i + 1 < argc) { g_net_guest_port = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--readback") && i + 1 < argc) { g_net_readback = argv[++i]; continue; }
        if (!strcmp(argv[i], "--net-delay") && i + 1 < argc) { g_net_delay = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--coop") && i + 1 < argc) { g_net_coop = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--net-lag") && i + 1 < argc) { g_net_lag = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--net-jitter") && i + 1 < argc) { g_net_jitter = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--net-loss") && i + 1 < argc) { g_net_loss = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--stage") && i + 1 < argc) { g_net_stage = atoi(argv[++i]); continue; }
        if (slot >= 0 && i + 1 < argc) { parts[slot] = atoi(argv[++i]); g_direct_test = 1; g_part_args = 1; }
        else if (argv[i][0] != '-') *dir = argv[i];
    }
    return 1;
}

/* ---------------- garage ---------------- */

/* Test builds: a new game owns every part and the shop starts empty (it
 * stocks what you sell it). A release turns this off and sets the starting
 * credits instead. */
static const int k_test_kit = 1;
static uint8_t g_paint_default[4][3];   /* the disc's default colours (models/colours.toml): CHANGE COLOR's DEFAULT */
/* Each player's optional parts (bit n = option n): player 1's from the
 * garage, player 2's a copy of them offline (the same build), each
 * player's own online. */
static uint16_t g_options[2];
static int modern_of(int who) { return (g_options[who] & ACR_GEN1_OPTION_MODERN) != 0; }
static const char *g_import_dir = "";
/* The arena: Green Silo (stage 27) when the import has it; F7 cycles Green
 * Silo, Ocean Platform (39), mission map 11 (the four-pillar hall, the P4
 * Qualifier test), mission map 6 (the sea platform, with its units) and the
 * open test floor. ACR_STAGE=NN picks any stage
 * 0-40 to start on (0-19 are the mission maps); ACR_STAGE=open (or -1)
 * starts on the open floor. */
static demo_stage g_stage;
static int g_stage_id = 27, g_stage_on = 0;
/* On a mission map (0-19), its mission units where the map's objects stand
 * (drawn only: their mission code isn't read yet). Loaded when first drawn. */
static demo_units g_units = { .map = -1 };
/* Armored-CoreR missions: units borrowed from another map (P4's drones). */
static demo_units g_mission_units = { .map = -1 };
/* A mission's radio line on screen (text only for now) and until when. */
static char g_radio[96];
static Uint64 g_radio_until;
static void radio_say(const char *t) {
    if (!t) return;
    SDL_strlcpy(g_radio, t, sizeof g_radio);
    g_radio_until = SDL_GetTicks() + 5000;
    SDL_Log("radio: %s", t);
}
/* An Armored-CoreR mission's start (acr_gen1_missions.h spots). */
static void spot_spawn(acr_gen1_ac *ac, const acr_gen1_ac_def *def, const acr_gen1_mission_spot *s) {
    acr_gen1_ac_spawn(ac, def, s->x * 64, s->z * 64, (uint16_t)(s->facing & 4095));
    ac->y = ac->py = ac->sy = s->y * 64;
}

/* Places fighter k's AC on the stage's start k (x, y down, z, facing). */
static void stage_spawn(acr_gen1_ac *ac, const acr_gen1_ac_def *def, int k) {
    const int32_t *st = g_stage.start[k];
    acr_gen1_ac_spawn(ac, def, st[0] * 64, st[2] * 64, (uint16_t)(st[3] & 4095));
    ac->y = ac->py = ac->sy = st[1] * 64;
}

/* Co-op starts: each team at its side's start (the stage's starts 0 and
 * 1, or the open floor's), teammates 500 either side of it. */
static void coop_spawn(acr_gen1_battle *b) {
    const int n = acr_gen1_battle_fighters(b);
    for (int team = 0; team < 2; ++team) {
        int32_t x, y = 0, z;
        uint16_t yaw;
        if (g_stage_on) { const int32_t *st = g_stage.start[team]; x = st[0]; y = st[1]; z = st[2]; yaw = (uint16_t)(st[3] & 4095); }
        else { x = 0; z = team ? 2500 : -2500; yaw = team ? 2048 : 0; }
        int members[ACR_GEN1_MAX_FIGHTERS], m = 0;
        for (int i = 0; i < n; ++i) if (acr_gen1_battle_team(b, i) == team) members[m++] = i;
        for (int k = 0; k < m; ++k) {
            const int32_t side = m == 1 ? 0 : k ? 500 : -500;     /* along the start's right: (cos, -sin) */
            const int32_t dx = (int32_t)((int64_t)side * acr_gen1_cos(yaw) / 4096),
                          dz = (int32_t)((int64_t)-side * acr_gen1_sin(yaw) / 4096);
            acr_gen1_fighter *f = &b->f[members[k]];
            acr_gen1_ac_spawn(&f->ac, f->def, (x + dx) * 64, (z + dz) * 64, yaw);
            f->ac.y = f->ac.py = f->ac.sy = y * 64;
        }
    }
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
    /* The disc's default colours (CHANGE COLOR's DEFAULT, a new game's paint). */
    char cpath[1200];
    SDL_snprintf(cpath, sizeof cpath, "%s/models/colours.toml", g_import_dir);
    char *ct = SDL_LoadFile(cpath, NULL);
    const char *v = ct ? SDL_strstr(ct, "default") : NULL;
    if (v && (v = SDL_strchr(v, '[')) != NULL)
        for (int k = 0; k < 12; ++k) {
            char *end;
            const long x = SDL_strtol(v + 1, &end, 10);
            if (end == v + 1 || x < 0 || x > 63) break;
            g_paint_default[k / 3][k % 3] = (uint8_t)x;
            v = end;
            while (*v == ' ') ++v;
            if (*v != ',') break;
        }
    SDL_free(ct);
    char *text = SDL_LoadFile(path, NULL);
    const int ok = text && acr_gen1_save_read(&g_hangar, text);
    if (ok) acr_gen1_mission_record_read(missions_record(), text);   /* the save's [missions] */
    SDL_free(text);
    if (ok) return;
    uint8_t s[SLOT_COUNT];
    for (int i = 0; i < SLOT_COUNT; ++i) s[i] = (uint8_t)start[i];
    acr_gen1_hangar_new(&g_hangar, s, 0, (int32_t)g_new_credits);
    if (k_test_kit) acr_gen1_hangar_give_all(&g_hangar, &g_catalog);
    memcpy(g_hangar.colours, g_paint_default, sizeof g_hangar.colours);
    acr_gen1_paint_fill(g_hangar.paint, (const uint8_t (*)[3])g_paint_default);   /* CHANGE COLOR starts on the default */
    SDL_strlcpy(g_hangar.ac_name, "AC", sizeof g_hangar.ac_name);
    SDL_strlcpy(g_hangar.pilot_name, "RAVEN", sizeof g_hangar.pilot_name);
}

static int hangar_save(void) {
    char path[1200];
    static char text[32768];   /* the garage, its paint and the missions record */
    save_path(path, sizeof path);
    size_t n = acr_gen1_save_write(&g_hangar, text, sizeof text);
    if (n < sizeof text) n += acr_gen1_mission_record_write(missions_record(), text + n, sizeof text - n);
    return n < sizeof text && SDL_SaveFile(path, text, n);
}

/* SYSTEM > LOAD: the saved garage again, if there is one. */
static int hangar_load(void) {
    char path[1200];
    save_path(path, sizeof path);
    char *text = SDL_LoadFile(path, NULL);
    acr_gen1_hangar h;
    const int ok = text && acr_gen1_save_read(&h, text);
    if (ok) acr_gen1_mission_record_read(missions_record(), text);
    SDL_free(text);
    if (ok) g_hangar = h;
    return ok;
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
    case SDL_SCANCODE_TAB: return GA_INFO;
    default: return GA_NONE;
    }
}

static garage_action button_action(int b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return GA_OK;
    case SDL_GAMEPAD_BUTTON_EAST: return GA_CANCEL;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GA_PREV_TAB;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GA_NEXT_TAB;
    case SDL_GAMEPAD_BUTTON_NORTH: return GA_INFO;
    default: return GA_NONE;
    }
}

/* OPTIONAL PARTS: the highlighted option's own model turning in the middle
 * of the screen (models/menu/option/NN, loaded once each). */
static void draw_option_model(int bit, int ph, float hx, float hy, float hw, float hh, unsigned tick) {
    static demo_object objs[ACR_GEN1_OPTION_MAX];
    static uint8_t tried[ACR_GEN1_OPTION_MAX];
    if (bit < 0 || bit >= ACR_GEN1_OPTION_MAX) return;
    if (!tried[bit]) { tried[bit] = 1; demo_object_load(g_import_dir, "option", bit, &objs[bit]); }
    if (!objs[bit].ok) return;
    const float s = (float)ph / 480.0f;
    const int x = (int)(hx * s), y = (int)(hy * s), w = (int)(hw * s), h = (int)(hh * s);
    if (w < 8 || h < 8) return;
    float proj[16], viewm[16];
    acr_mat_perspective(proj, 30.0f * 3.14159265f / 180.0f, (float)w / (float)h, 50.0f, 40000.0f);
    const float eye[3] = { 0, 0, -4000.0f }, fwd[3] = { 0, 0, 1 }, up[3] = { 0, 1, 0 };
    acr_mat_look(viewm, eye, fwd, up);
    for (int k = 0; k < 4; ++k) viewm[4 * k] = -viewm[4 * k];
    acr_render_view3d(x, y, w, h, proj, viewm, NULL);
    demo_object_draw(&objs[bit], 1500.0f, (float)tick * 0.02f, 0.35f, &g_light);
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

/* Rogue Arena's VS and victory screens (rogue_ac_drawer): an AC drawn into a
 * HUD box, standing and framed from the top of its head down `frac` of its
 * height, or on its back. The models are kept for the screens' ACs (the
 * last few builds asked for). */
#define PORTRAITS 10
static struct {
    int used;
    unsigned age;
    int parts[SLOT_COUNT];
    uint8_t colours[4][3];
    demo_model m;
    float mn[3], mx[3];
    int boxed;
} g_portrait[PORTRAITS];
static unsigned g_portrait_age;
static int g_screen_pw, g_screen_ph;

static void rogue_draw_ac(const int parts[SLOT_COUNT], const uint8_t colours[4][3], float x, float y, float w, float h,
                          float frac, float yaw, int lying, float shade) {
    int k = -1, oldest = 0;
    for (int i = 0; i < PORTRAITS && k < 0; ++i)
        if (g_portrait[i].used && !memcmp(g_portrait[i].parts, parts, sizeof g_portrait[i].parts) &&
            !memcmp(g_portrait[i].colours, colours, sizeof g_portrait[i].colours))
            k = i;
    if (k < 0) {
        for (int i = 1; i < PORTRAITS; ++i)
            if (!g_portrait[i].used || (g_portrait[oldest].used && g_portrait[i].age < g_portrait[oldest].age)) oldest = i;
        k = oldest;
        if (g_portrait[k].used) demo_model_free(&g_portrait[k].m);
        memset(&g_portrait[k], 0, sizeof g_portrait[k]);
        g_portrait[k].used = 1;
        memcpy(g_portrait[k].parts, parts, sizeof g_portrait[k].parts);
        memcpy(g_portrait[k].colours, colours, sizeof g_portrait[k].colours);
        const int set = demo_pick_model(g_import_dir, parts);
        if (set >= 0) demo_model_load(g_import_dir, set, parts, colours, &g_portrait[k].m);
        g_portrait[k].boxed = demo_model_bounds(&g_portrait[k].m, 0, 0, g_portrait[k].mn, g_portrait[k].mx);
    }
    g_portrait[k].age = ++g_portrait_age;
    if (!g_portrait[k].m.ok || !g_portrait[k].boxed || g_screen_ph < 1) return;
    const float s = (float)g_screen_ph / 480.0f;
    const int px = (int)(x * s), py = (int)(y * s), pw = (int)(w * s), ph = (int)(h * s);
    if (pw < 4 || ph < 4) return;
    const float *mn = g_portrait[k].mn, *mx = g_portrait[k].mx;
    const float tall = mx[1] - mn[1], wide = mx[0] - mn[0] > mx[2] - mn[2] ? mx[0] - mn[0] : mx[2] - mn[2];
    const float fov = 30.0f * 3.14159265f / 180.0f, aspect = (float)pw / (float)ph;
    float proj[16], viewm[16], eye[3], fwd[3] = { 0, 0, 1 }, up[3] = { 0, 1, 0 };
    if (!lying) {
        /* the top `frac` of it, a little room over the head */
        const float shown = tall * frac * 1.04f, cy = mx[1] + tall * 0.03f - shown * 0.5f;
        const float dist = shown * 0.5f / SDL_tanf(fov * 0.5f) + wide * 0.5f;
        eye[0] = 0; eye[1] = cy; eye[2] = -dist;
    } else {
        /* on its back: the view rolled a quarter turn, so it lies across the box */
        const float across = tall * 1.06f / aspect;
        const float dist = (across > wide ? across : wide) * 0.5f / SDL_tanf(fov * 0.5f) + wide * 0.5f;
        eye[0] = 0; eye[1] = (mn[1] + mx[1]) * 0.5f; eye[2] = -dist;
        up[0] = 1; up[1] = 0;
    }
    acr_mat_perspective(proj, fov, aspect, 20.0f, 40000.0f);
    acr_mat_look(viewm, eye, fwd, up);
    for (int i = 0; i < 4; ++i) viewm[4 * i] = -viewm[4 * i];
    acr_render_view3d(px, py, pw, ph, proj, viewm, NULL);
    acr_lighting l = g_light;
    if (shade < 1.0f) {
        l.ambient_r = (uint8_t)(l.ambient_r * shade); l.ambient_g = (uint8_t)(l.ambient_g * shade); l.ambient_b = (uint8_t)(l.ambient_b * shade);
        for (int i = 0; i < l.count && i < 3; ++i) {
            l.light[i].r = (uint8_t)(l.light[i].r * shade); l.light[i].g = (uint8_t)(l.light[i].g * shade); l.light[i].b = (uint8_t)(l.light[i].b * shade);
        }
    }
    demo_model_draw(&g_portrait[k].m, 0, 0, 0, 0, 0, 3.14159265f + yaw, 1.0f, &l);
    acr_render_hud(0, 0, g_screen_pw, g_screen_ph);
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

/* Testing aid: ACR_KEYS="ok,down,ok,..." feeds the menus (garage, network,
 * Rogue Arena) one action a frame; "wait" lets half a second pass. */
static garage_action script_action(void) {
    static const char *script;
    static int script_read;
    static Uint64 wait_until;
    if (!script_read) { script = SDL_getenv("ACR_KEYS"); script_read = 1; }
    if (!script || !*script || SDL_GetTicks() < wait_until) return GA_NONE;
    garage_action a = GA_NONE;
    static const char *const k_names[] = { "", "up", "down", "left", "right", "ok", "back", "lb", "rb", "info" };
    if (!SDL_strncmp(script, "wait", 4)) wait_until = SDL_GetTicks() + 500;
    else
        for (int k = 1; k < 10; ++k)
            if (!SDL_strncmp(script, k_names[k], SDL_strlen(k_names[k]))) { a = (garage_action)k; break; }
    script = SDL_strchr(script, ',');
    if (script) ++script;
    return a;
}

/* Opens the network link to host (join == NULL) or join a match with this
 * player's build. NULL with the reason in err. */
static net_link *net_open(const char *dir, const char *join, int online, int stage, int delay, int coop_ai, int ai_preset,
                          const int parts[SLOT_COUNT], char *err, int err_size) {
    if (stage > 0 && !net_stage_check(dir, stage)) stage = 0;   /* not in this import: the open floor */
    net_setup ns;
    SDL_zero(ns);
    ns.host = join == NULL;
    ns.port = g_net_port;
    ns.join = join;
    ns.import_dir = dir;
    for (int i = 0; i < SLOT_COUNT; ++i) ns.parts[i] = parts[i];
    ns.parts[SLOT_COUNT] = g_part_args ? 0 : g_hangar.options;   /* the option parts (SP-MODERN) */
    ns.ac_name = g_hangar.ac_name;
    ns.stage = stage;
    ns.delay = delay < 0 ? 0 : delay > ACR_RB_MAX_DELAY ? ACR_RB_MAX_DELAY : delay;
    ns.coop_ai = coop_ai;
    ns.ai_preset = ai_preset > 0 && ai_preset < ACR_GEN1_AI_PRESETS ? ai_preset : ACR_GEN1_AI_BALANCED;
    ns.lag_ms = g_net_lag;
    ns.jitter_ms = g_net_jitter;
    ns.loss_pct = g_net_loss;
    ns.online = online;
    ns.guest_port = g_net_guest_port;
    g_net_host = ns.host;
    return net_link_open(&ns, err, err_size);
}

int main(int argc, char **argv) {
    void *block = malloc(ACR_POOLS_TOTAL_BYTES);
    acr_pools pools;
    if (!block || !acr_pools_init(&pools, block, ACR_POOLS_TOTAL_BYTES)) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;

    char default_dir[1024];
#ifdef SDL_PLATFORM_ANDROID
    /* Android: the import lives in the app's own storage, with the saves. */
    char *pref_dir = SDL_GetPrefPath("Hatdagger", "Armored-CoreR");
    SDL_snprintf(default_dir, sizeof default_dir, "%simport", pref_dir ? pref_dir : "");
    SDL_free(pref_dir);
#else
    SDL_snprintf(default_dir, sizeof default_dir, "%simport", SDL_GetBasePath() ? SDL_GetBasePath() : "");
#endif
    const char *dir = default_dir;
    int parts[SLOT_COUNT];
    parse_args(argc, argv, &dir, parts);

    /* OpenGL 2.1 at the screen's own resolution, 4x anti-aliasing when the
     * driver offers it (the GLES build: OpenGL ES 3.0, else 2.0). */
#ifdef ACR_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef SDL_PLATFORM_ANDROID
    const SDL_WindowFlags wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    const SDL_WindowFlags wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    /* SETTINGS > VIDEO picks the window's size and fullscreen; ACR_WINDOW=WxH
     * wins over it (screenshots at other shapes). */
    settings_load();
    int win_w = 1280, win_h = 720;
    settings_window_size(g_settings.window, &win_w, &win_h);
    if (!g_hud_frame_arg) g_hud_frame = g_settings.hud_wide ? BATTLE_HUD_FRAME_WIDE : BATTLE_HUD_FRAME_43;
    g_smooth_anim = g_settings.smooth_anim;
    const int window_env = SDL_getenv("ACR_WINDOW") != NULL;
    if (window_env && SDL_sscanf(SDL_getenv("ACR_WINDOW"), "%dx%d", &win_w, &win_h) != 2) { win_w = 1280; win_h = 720; }
    SDL_Window *window = SDL_CreateWindow("Armored-CoreR test build", win_w, win_h, wflags);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : NULL;
    if (!gl) {
        if (window) SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow("Armored-CoreR test build", win_w, win_h, wflags);
        gl = window ? SDL_GL_CreateContext(window) : NULL;
    }
#ifdef ACR_GLES
    if (!gl) {
        if (window) SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        window = SDL_CreateWindow("Armored-CoreR test build", win_w, win_h, wflags);
        gl = window ? SDL_GL_CreateContext(window) : NULL;
    }
#endif
    if (!gl || !acr_render_init(gl_proc)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Armored-CoreR",
#ifdef ACR_GLES
                                 "This device's graphics driver does not offer OpenGL ES 2.0.", window);
#else
                                 "This computer's graphics driver does not offer OpenGL 2.1.", window);
#endif
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    if (g_settings.fullscreen && !window_env) SDL_SetWindowFullscreen(window, true);
    acr_light_default(&g_light);
    SDL_Gamepad *pad = open_first_gamepad();

    /* A disc given on the command line (or dropped on the program) is
     * imported next to the program first. */
    static demo_ac ac_data[ACR_GEN1_MAX_FIGHTERS];   /* each AC's build; large, so off the stack */
    static int parts_ai[2][SLOT_COUNT];             /* co-op: the computer ACs' builds (fighters 2, 3) */
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
#ifdef SDL_PLATFORM_ANDROID
                     "PRESS A (OR TAP THE SCREEN) TO CHOOSE YOUR\n"
                     "MASTER OF ARENA DISC IMAGE (THE .BIN FILE).\n"
                     "IT IS IMPORTED INTO THE APP (ABOUT 1 GB).\n\n"
                     "BACK: QUIT\n\n%s", err);
#else
                     "DROP YOUR MASTER OF ARENA DISC (.CUE OR .BIN)\n"
                     "ON THIS WINDOW TO IMPORT IT,\n"
                     "OR PRESS A / ENTER TO CHOOSE IT.\n\n"
                     "ESC / BACK: QUIT\n\n%s", err);
#endif
        show_message(window, text);
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) continue;
        /* the system's file picker: A, Enter or a tap opens it */
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
        if ((e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) ||
            (e.type == SDL_EVENT_KEY_DOWN && (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_SPACE)) ||
            e.type == SDL_EVENT_FINGER_UP || (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.which != SDL_TOUCH_MOUSEID)) {
            disc_pick_open(window);
            continue;
        }
        char picked[2048];
        const int pick = disc_pick_poll(picked, sizeof picked);
        if (pick == 1) {
            disc_pick_file keep;
            char local[1200];
            err[0] = 0;
            if (disc_pick_local(picked, &keep, local, sizeof local, err, sizeof err)) {
                dir = default_dir;
                if (import_disc(window, local, dir, err, sizeof err))
                    loaded = demo_load(dir, parts, &ac_data[0], err, sizeof err);
                disc_pick_close(&keep);
            }
            continue;
        }
        if (e.type == SDL_EVENT_QUIT ||
            (e.type == SDL_EVENT_KEY_DOWN && (e.key.scancode == SDL_SCANCODE_ESCAPE || e.key.scancode == SDL_SCANCODE_AC_BACK)) ||
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
        if (want) g_stage_id = SDL_strcmp(want, "open") == 0 ? -1 : SDL_atoi(want);
        if (g_stage_id >= 0 && demo_stage_load(dir, g_stage_id, &g_stage)) g_stage_on = 1;
    }
    demo_catalog(dir, &g_catalog, g_part_names);
    acr_gen1_catalog_add_own_parts(&g_catalog, g_part_names[ACR_GEN1_CAT_OPTION][ACR_GEN1_OPTION_MODERN_BIT]);
    if (k_test_kit)
        for (int c = 0; c < ACR_GEN1_CAT_COUNT; ++c)
            for (int i = 0; i < ACR_GEN1_PART_MAX; ++i) g_catalog.part[c][i].for_sale = 0;
    hangar_open(parts);
    /* SP-MODERN is there from the start, older saves included. */
    if (!g_hangar.owned[ACR_GEN1_CAT_OPTION][ACR_GEN1_OPTION_MODERN_BIT])
        g_hangar.owned[ACR_GEN1_CAT_OPTION][ACR_GEN1_OPTION_MODERN_BIT] = 1;
    g_options[0] = g_options[1] = g_part_args ? 0 : g_hangar.options;
    acr_import_refresh(dir);          /* older imports: the menu sounds, the emblems, the mission units */
    sfx_open(dir);                    /* the menus' clicks too */
    sfx_set_volume((float)g_settings.fx_volume / 10.0f, (float)g_settings.menu_volume / 10.0f);
    rogue_init(dir, &g_catalog, g_part_names);
    rogue_set_ac_drawer(rogue_draw_ac);
    arena_init(dir, &g_hangar, &g_catalog, g_part_names);
    arena_set_ac_drawer(rogue_draw_ac);
    missions_init(dir, &g_hangar, &g_catalog);
    controls_load();
    controls_ui cui;
    controls_open(&cui);
    settings_ui sui;
    settings_open(&sui, 0);
    int mode = g_direct_test ? 1 : 0;   /* MODE_GARAGE, MODE_FIGHT, the network screens, MODE_ROGUE, MODE_CONTROLS */
    garage_ui gui;
    garage_init(&gui);
    static demo_part_specs specs;     /* the disc's type names and shop texts */
    demo_part_specs_load(dir, &specs);
    const garage_data gdata_main = { &g_hangar, &g_catalog, g_part_names, 0, &specs,
                                     (const uint8_t (*)[3])g_paint_default };
    garage_data gdata = gdata_main;
    if (!g_direct_test) {
        int fitted[SLOT_COUNT];
        hangar_parts(fitted);
        if (demo_load(dir, fitted, &ac_data[0], err, sizeof err)) memcpy(parts, fitted, sizeof fitted);
        else demo_load(dir, parts, &ac_data[0], err, sizeof err);
    }
    /* Netplay: the NETWORK screen (2), connecting (3) and why it failed (4);
     * --host / --join start at connecting. The match itself is built
     * once connected (see "the match starts" below). */
    static net_link *net = NULL;
    static net_match_info net_info;
    static net_menu nmenu;
    static net_finder *finder = NULL;
    static char net_status[1600];
    /* Over the internet (briefing codes): the editor is mode 5. */
    static brief_edit bedit;
    static int net_online;                    /* the open link uses briefing codes */
    static int bedit_host;                    /* the editor is player 1 entering a read-back */
    static int net_readback;                  /* player 1 has entered player 2's read-back */
    static Uint64 net_open_ms;                /* when the link opened */
    static char brief_msg[200];
    static Uint64 brief_msg_until;
    static char brief_join[64];
    _Static_assert(SLOT_COUNT + 1 == NET_PARTS, "the network sends every part slot and the options");
    net_menu_init(&nmenu);
    if (g_net_cmd == 2 && g_net_brief) {
        acr_brief b;
        net_addr a;
        if (acr_brief_parse(g_net_brief, &b) && acr_brief_decode(&b, ACR_BRIEF_OFFER, a.ip, &a.port)) {
            SDL_snprintf(brief_join, sizeof brief_join, "%d.%d.%d.%d:%d", a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port);
            g_net_join = brief_join;
        } else {
            SDL_Log("netplay: --join-brief: that isn't a mission briefing");
            g_net_join = "";
        }
    }
    if (g_net_cmd) {
        int own[SLOT_COUNT];
        memcpy(own, parts, sizeof own);
        if (!g_part_args) hangar_parts(own);
        const int stage = g_net_stage >= 0 ? g_net_stage : g_stage_id >= 0 ? g_stage_id : 27;
        net = net_open(dir, g_net_cmd == 2 ? g_net_join : NULL, g_net_online, stage, g_net_delay, g_net_coop,
                       g_ai_mode > 0 && g_ai_mode < ACR_GEN1_AI_PRESETS ? g_ai_mode : ACR_GEN1_AI_BALANCED, own,
                       net_status, sizeof net_status);
        mode = net ? 3 : 4;
        net_online = g_net_online;
        net_open_ms = SDL_GetTicks();
        net_readback = 0;
        if (net && g_net_readback) {
            acr_brief b;
            net_addr a;
            if (acr_brief_parse(g_net_readback, &b) && acr_brief_decode(&b, ACR_BRIEF_REPLY, a.ip, &a.port)) {
                net_link_punch(net, &a);
                net_readback = 1;
            } else {
                SDL_Log("netplay: --readback: that isn't a read-back");
            }
        }
    }
    /* Player 2 starts with the same build; each player can change theirs. */
    memcpy(parts2, parts, sizeof parts2);
    demo_load(dir, parts2, &ac_data[1], err, sizeof err);
    int *const pp[ACR_GEN1_MAX_FIGHTERS] = { parts, parts2, parts_ai[0], parts_ai[1] };
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
    /* Fighter i_ from its demo_ac (parts loaded), at full AP; not placed. */
    #define SETUP_FIGHTER(i_) do {                                                        \
        acr_gen1_fighter *f_ = &battle.f[i_];                                         \
        demo_ac *d_ = &ac_data[i_];                                                   \
        memset(f_, 0, sizeof *f_);                                                    \
        f_->intercept_shot = -1; f_->swing_target = -1; f_->last_missile = -1;        \
        /* Human PLUS at full (level 9): energy rules of VS 0x80083560. */            \
        /* With SP-MODERN only the no-kneel benefit applies (level 0 here). */         \
        if ((i_ == 0 ? human_plus : g_plus2) && !d_->plus_applied &&                  \
            acr_gen1_human_plus_level(i_ == 0 ? 9 : (unsigned)g_plus2, i_ == 0 ? g_options[0] : 0)) { \
            acr_gen1_human_plus_energy(&d_->def.stats, i_ == 0 ? 9 : g_plus2);        \
            d_->plus_applied = 1;                                                     \
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
        f_->human_plus = (uint8_t)(i_ == 0 ? human_plus : i_ == 1 && g_plus2 > 0); f_->kneeling = 0; \
        f_->team = (uint8_t)(i_ >= 2);   /* co-op: players 0, 1 against the AI ACs */ \
        f_->target = -1;                                                              \
        /* SP-MODERN: pilots only (the AI flies the original controls). */           \
        f_->modern = (uint8_t)(i_ < 2 && !(i_ == 0 && g_autopilot) &&                 \
                               !(i_ == 1 && g_ai_mode && !g_net && !two_players) && modern_of(i_)); \
        d_->def.cruise_pct = (uint8_t)(f_->legs == ACR_GEN1_LEGS_TWO ? ACR_GEN1_CRUISE_PCT \
                                       : ACR_GEN1_CRUISE_HEAVY_PCT);                    \
    } while (0)
    #define SETUP_FIGHTERS() do {                                                         \
        for (int i_ = 0; i_ < acr_gen1_battle_fighters(&battle); ++i_) SETUP_FIGHTER(i_); \
        battle.targeted = 0;                                                              \
        if (battle.count > 2) coop_spawn(&battle);                                        \
        else if (g_stage_on) { stage_spawn(P, def, 0); stage_spawn(T, def2, 1); }         \
        else {                                                                            \
            acr_gen1_ac_spawn(P, def, 0, -2500 * 64, 0);                                  \
            acr_gen1_ac_spawn(T, def2, target_side() * 64, (target_dist() - 2500) * 64, 2048); \
        }                                                                                 \
        /* Mission maps: their units join the fight (team 1, with the target AC; \
         * map 12's friendly units team 0, with the player). */                 \
        demo_units_join(&g_units, g_import_dir, g_stage_on && !g_rogue_fight ? g_stage_id : -1, &battle,  \
                        g_stage_on ? &g_stage.collision : NULL);                          \
        /* Armored-CoreR missions: the variant's rule, and P4 Alternate layout's          \
         * starts and drones (acr_gen1_missions.h). */                                    \
        g_mission_units.objects = 0;                                                      \
        if (g_rogue_fight == 3) {                                                         \
            acr_gen1_mission_start_own(&battle, missions_rule());                         \
            /* Testing aid: ACR_MISSION_FOE_HURT=1 starts the opponent below half  \
             * its AP (P4 Altered: the challenger comes at once). */              \
            if (SDL_getenv("ACR_MISSION_FOE_HURT")) battle.f[1].ap = battle.f[1].ap_max * 49 / 100; \
            if (missions_rule() == ACR_GEN1_OWN_P4_ALTERNATE && g_stage_on) {             \
                spot_spawn(P, def, &acr_gen1_p4_alt_start[0]);                            \
                spot_spawn(T, def2, &acr_gen1_p4_alt_start[1]);                           \
                demo_units_borrow(&g_mission_units, g_import_dir, ACR_GEN1_P4_DRONE_MAP,  \
                                  ACR_GEN1_P4_DRONE_RECORD, acr_gen1_p4_alt_drone, 2, &battle,\
                                  &g_stage.collision);                                    \
            }                                                                             \
        }                                                                                 \
        /* Testing aids: ACR_UNITS_DOWN=1 starts with the enemy units in the    \
         * fight destroyed, so a map's script moves on (map 6: the deck opens); \
         * ACR_FRIENDS_DOWN=1 the friendly ones (map 12: the mission fails). */ \
        for (int u_ = 0; u_ < battle.units; ++u_)                                         \
            if (battle.unit[u_].active &&                                                 \
                SDL_getenv(battle.unit[u_].team & 1 ? "ACR_UNITS_DOWN" : "ACR_FRIENDS_DOWN")) \
                battle.unit[u_].ap = 0, battle.unit[u_].down = 1;                         \
        acr_gen1_battle_retarget(&battle);                                                \
        memset(battle.shot, 0, sizeof battle.shot);                                       \
        fx_reset();                                                                       \
        sfx_reset();                                                                      \
        round_ms = g_round_start_ms; round_over = 0; round_result = 0; ko_ms = 0;         \
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
    /* Netplay: the match as set up is every round's start; the rollback
     * session runs it from here. */
    static acr_gen1_battle net_start;
    static acr_rb net_rb;
    int net_skip_cool = 0, net_lost = 0;
    unsigned net_skips = 0;
    Uint64 net_wait_since = 0;                /* when the match started waiting for the other player */
    acr_gen1_ac prevs[ACR_GEN1_MAX_FIGHTERS] = { *P, *T, *P, *T };
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
    Uint64 give_up_until = 0;              /* Rogue Arena: a second Esc before this gives up */
    Uint64 rogue_over_at = 0;              /* Rogue Arena: when the round ended */
    int running = 1, show_help = 0, back_held = 0, back_combo = 0, switch_pressed[2] = { 0, 0 }, blade_pressed[2] = { 0, 0 },
        target_pressed[2] = { 0, 0 };   /* switch target: Back/Select tap, X */
    /* Modern controls, per player slot here (0 = this PC's player online). */
    int cruise_on[2] = { 0, 0 }, quick_pressed[2] = { 0, 0 }, lock_pressed[2] = { 0, 0 }, l2_was[2] = { 0, 0 };
    SDL_Gamepad *pads[2] = { pad, NULL };
    {   /* a second controller, if there is one */
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        for (int i = 0; ids && i < count && !pads[1]; ++i)
            if (!pads[0] || SDL_GetGamepadID(pads[0]) != ids[i]) pads[1] = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
    while (running) {
        if (!g_net) {      /* offline: player 2 is built like player 1 */
            g_options[0] = g_part_args ? 0 : g_hangar.options;
            g_options[1] = g_rogue_fight == 3 ? missions_foe_options() : g_options[0];   /* a mission's own opponent */
        }
        SDL_Event e;
        garage_command gcmd = GC_NONE;
        int to_garage = 0, rogue_done = 0, rogue_give_up = 0;
        rogue_command rcmd = RC_NONE;
        arena_command acmd = AC_NONE;
        missions_command mcmd = MC_NONE;
        garage_action net_action = GA_NONE;   /* the network screens' input this frame */
        int net_action_keys = 0;
        int net_copy = 0, net_paste = 0;      /* copy / paste a briefing code this frame */
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
            if (mode == MODE_SETTINGS) {   /* SETTINGS > VIDEO / AUDIO */
                garage_action ga = GA_NONE;
                if (e.type == SDL_EVENT_KEY_DOWN) ga = key_action(e.key.scancode);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) ga = button_action(e.gbutton.button);
                if (settings_input(&sui, ga)) { mode = 0; gui.screen = 0; }
                continue;
            }
            if (mode == MODE_CONTROLS) {   /* the CONTROLS page: a button to map, or the menu */
                if (controls_capture(&cui, &e)) continue;
                garage_action ga = GA_NONE;
                if (e.type == SDL_EVENT_KEY_DOWN) ga = key_action(e.key.scancode);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) ga = button_action(e.gbutton.button);
                if (controls_input(&cui, ga)) { mode = 0; gui.screen = 0; }
                continue;
            }
            if (mode == MODE_ARENA) {   /* the Arena's screens take all other input */
                garage_action ga = GA_NONE;
                if (e.type == SDL_EVENT_KEY_DOWN) ga = key_action(e.key.scancode);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) ga = button_action(e.gbutton.button);
                const arena_command ac = arena_input(ga);
                if (ac != AC_NONE) acmd = ac;
                continue;
            }
            if (mode == MODE_MISSIONS) {   /* the MISSIONS screens take all other input */
                garage_action ga = GA_NONE;
                if (e.type == SDL_EVENT_KEY_DOWN) ga = key_action(e.key.scancode);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) ga = button_action(e.gbutton.button);
                const missions_command mc = missions_input(ga);
                if (mc != MC_NONE) mcmd = mc;
                continue;
            }
            if (mode == MODE_ROGUE) {   /* Rogue Arena's screens take all other input */
                garage_action ga = GA_NONE;
                if (e.type == SDL_EVENT_KEY_DOWN) ga = key_action(e.key.scancode);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) ga = button_action(e.gbutton.button);
                const rogue_command rc = rogue_input(ga);
                if (rc != RC_NONE) rcmd = rc;
                continue;
            }
            if (mode == 1 && g_rogue_fight) {
                /* A Rogue Arena fight: no test keys. Esc (twice) or Back +
                 * Start gives up, which counts as a loss; after the round,
                 * Enter / A goes on. */
                const int esc = e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE;
                const int pad_quit = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && back_held && e.gbutton.button == SDL_GAMEPAD_BUTTON_START;
                if (esc || pad_quit) {
                    if (round_over) rogue_done = 1;
                    else if (SDL_GetTicks() < give_up_until) rogue_give_up = 1;
                    else give_up_until = SDL_GetTicks() + 2000;
                    if (pad_quit) back_combo = 1;
                    continue;
                }
                if (round_over && ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_RETURN) ||
                                   (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH))) {
                    rogue_done = 1;
                    continue;
                }
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    const SDL_Scancode k = e.key.scancode;
                    const int test_key = (k >= SDL_SCANCODE_F2 && k <= SDL_SCANCODE_F8 && k != SDL_SCANCODE_F5) ||
                                         (k >= SDL_SCANCODE_2 && k <= SDL_SCANCODE_8 && k != SDL_SCANCODE_5) ||
                                         k == SDL_SCANCODE_R || k == SDL_SCANCODE_B || k == SDL_SCANCODE_G || k == SDL_SCANCODE_C ||
                                         k == SDL_SCANCODE_L || k == SDL_SCANCODE_H || k == SDL_SCANCODE_T;
                    if (test_key) continue;
                }
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    const int b = e.gbutton.button;
                    if (b == SDL_GAMEPAD_BUTTON_BACK) { back_held = 1; back_combo = 1; continue; }
                    if (back_held || b == SDL_GAMEPAD_BUTTON_START) continue;
                }
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) { back_held = 0; continue; }
            }
            if (mode >= MODE_NET_FIRST && mode <= MODE_NET_LAST) {   /* the network screens */
                garage_action ga = GA_NONE;
                int from_keys = 0;
                if (mode == 2 && e.type == SDL_EVENT_TEXT_INPUT) net_menu_text(&nmenu, e.text.text);
                if (mode == 5 && e.type == SDL_EVENT_TEXT_INPUT) brief_edit_text(&bedit, e.text.text, SDL_GetTicks());
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    from_keys = 1;
                    const SDL_Scancode k = e.key.scancode;
                    const int ctrl = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
                    if (k == SDL_SCANCODE_V && ctrl) net_paste = 1;
                    if (k == SDL_SCANCODE_C && (ctrl || mode == 3)) net_copy = 1;
                    if (mode == 5) {
                        /* the briefing editor: letters jump in the lists */
                        if (k == SDL_SCANCODE_UP) ga = GA_UP;
                        else if (k == SDL_SCANCODE_DOWN) ga = GA_DOWN;
                        else if (k == SDL_SCANCODE_LEFT || k == SDL_SCANCODE_PAGEUP) ga = GA_PREV_TAB;
                        else if (k == SDL_SCANCODE_RIGHT || k == SDL_SCANCODE_PAGEDOWN) ga = GA_NEXT_TAB;
                        else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) ga = GA_OK;
                        else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_BACKSPACE) ga = GA_CANCEL;
                    } else if (mode == 2 && net_menu_typing(&nmenu)) {
                        /* typing the address: letters are text, not moves */
                        if (k == SDL_SCANCODE_BACKSPACE) net_menu_text(&nmenu, "\b");
                        else if (k == SDL_SCANCODE_UP) ga = GA_UP;
                        else if (k == SDL_SCANCODE_DOWN) ga = GA_DOWN;
                        else if (k == SDL_SCANCODE_LEFT) ga = GA_LEFT;
                        else if (k == SDL_SCANCODE_RIGHT) ga = GA_RIGHT;
                        else if (k == SDL_SCANCODE_RETURN) ga = GA_OK;
                        else if (k == SDL_SCANCODE_ESCAPE) ga = GA_CANCEL;
                    } else {
                        ga = key_action(k);
                    }
                }
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    ga = button_action(e.gbutton.button);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) ga = GA_CANCEL;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST && mode == 3) net_copy = 1;
                }
                if (ga != GA_NONE) net_action = ga, net_action_keys = from_keys;
                continue;
            }
            if (mode == 0) {   /* the garage takes all other input */
                garage_action ga = GA_NONE;
                if (e.type == SDL_EVENT_KEY_DOWN) ga = key_action(e.key.scancode);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) ga = button_action(e.gbutton.button);
                const garage_command c = garage_input(&gui, &gdata, ga);
                if (c != GC_NONE) gcmd = c;
                continue;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) {
                if (g_direct_test) running = 0;
                else to_garage = 1;
            }
            if (g_net) {
                /* Online: the gameplay controls only (the keyboard's fixed
                 * keys and the mapped pad buttons), plus Esc / Back + Start
                 * to leave. Every test key (F1-F9, the number keys, the
                 * Back + button combinations) is off. */
                if (controls_key_pressed(&e, CK_SWITCH)) switch_pressed[0] = 1;
                if (controls_key_pressed(&e, CK_BLADE)) blade_pressed[0] = 1;
                if (controls_key_pressed(&e, CK_TARGET)) target_pressed[0] = 1;
                if (modern_of(net_info.local)) {          /* SP-MODERN: its own fixed layout */
                    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && !back_held) {
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) switch_pressed[0] = 1;
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) cruise_on[0] = !cruise_on[0];
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) quick_pressed[0] = 1;
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) lock_pressed[0] = 1;
                    }
                } else {
                    if (controls_pressed(&e, CT_SWITCH)) switch_pressed[0] = 1;
                    if (controls_pressed(&e, CT_BLADE)) blade_pressed[0] = 1;
                }
                /* Back/Select: a tap switches target; Back + Start leaves. */
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) { back_held = 1; back_combo = 0; }
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_START && back_held) {
                    back_combo = 1;
                    if (g_direct_test) running = 0;
                    else to_garage = 1;
                }
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                    if (!back_combo) target_pressed[0] = 1;
                    back_held = 0;
                }
                continue;
            }
            /* Number keys 1-8 work as F1-F8 (laptop F keys often do other
             * things without Fn). */
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode >= SDL_SCANCODE_1 && e.key.scancode <= SDL_SCANCODE_8)
                e.key.scancode = (SDL_Scancode)(SDL_SCANCODE_F1 + (e.key.scancode - SDL_SCANCODE_1));
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F1) show_help = !show_help;
            /* F5 / F9 change the same settings as SETTINGS > VIDEO (and save them). */
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F5) {
                g_smooth_anim = !g_smooth_anim;
                g_settings.smooth_anim = g_smooth_anim;
                settings_save();
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && (e.key.scancode == SDL_SCANCODE_F9 || e.key.scancode == SDL_SCANCODE_9)) {
                g_hud_frame = !g_hud_frame;
                g_settings.hud_wide = g_hud_frame == BATTLE_HUD_FRAME_WIDE;
                settings_save();
            }
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
                 * tried on any stage, Green Silo too (it has no boxes of its
                 * own). Pressed again: back to the stage's own boxes. */
                acr_gen1_stage *cs = &g_stage.collision;
                if (g_test_boundary) {
                    g_test_boundary = 0;
                    SDL_memcpy(cs->bound, g_stage.own_bound, sizeof cs->bound);
                    cs->bounds = g_stage.own_bounds;
                } else {
                    g_test_boundary = 1;
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
                /* Green Silo -> Ocean Platform -> mission map 11 (four-pillar
                 * hall) -> mission map 6 (sea platform, its units drawn) ->
                 * the open test floor -> Green Silo (ACR_STAGE's
                 * stage, if another, goes to the open floor next). A stage
                 * the import lacks is skipped. */
                static const int k_cycle[] = { 27, 39, 11, 6 };
                enum { CYCLE = (int)(sizeof k_cycle / sizeof k_cycle[0]) };
                int want_id = -1, at = CYCLE;
                if (!g_stage_on) { want_id = k_cycle[0]; at = 0; }
                else
                    for (int k = 0; k < CYCLE; ++k)
                        if (g_stage_id == k_cycle[k]) { at = k + 1; want_id = at < CYCLE ? k_cycle[at] : -1; }
                g_stage_on = 0;
                while (want_id >= 0) {
                    if (g_stage.ok && g_stage_id == want_id) { g_stage_on = 1; break; }
                    demo_stage tmp;
                    if (demo_stage_load(dir, want_id, &tmp)) {
                        if (g_stage.ok) demo_stage_free(&g_stage);
                        g_stage = tmp; g_test_boundary = 0;
                        g_stage_id = want_id;
                        g_stage_on = 1;
                        break;
                    }
                    ++at;
                    want_id = at < CYCLE ? k_cycle[at] : -1;
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
            if (controls_key_pressed(&e, CK_SWITCH)) switch_pressed[0] = 1;
            if (controls_key_pressed(&e, CK_BLADE)) blade_pressed[0] = 1;
            int toggle_two = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F2) toggle_two = 1;
            int cycle_rate = 0, cycle_slot = -1, toggle_plus = 0, cycle_who = 0;
            /* The blade and the weapon switch, as mapped (CONTROLS page);
             * a trigger can be one too. Which player's controller? */
            if (!back_held && (controls_pressed(&e, CT_BLADE) || controls_pressed(&e, CT_SWITCH))) {
                const int who = !two_players ? 0 : pads[1] ? (player_of(pads, controls_event_pad(&e), 0) == 1) : 1;
                if (!modern_of(who)) {             /* SP-MODERN keeps its own layout */
                    if (controls_pressed(&e, CT_BLADE)) blade_pressed[who] = 1;
                    if (controls_pressed(&e, CT_SWITCH)) switch_pressed[who] = 1;
                }
            }
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
                const int pad_who = !two_players ? 0 : pads[1] ? (player_of(pads, e.gbutton.which, 0) == 1) : 1;
                if (modern_of(pad_who) && !back_held) {   /* SP-MODERN: its own fixed layout */
                    if (btn == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) cruise_on[pad_who] = !cruise_on[pad_who];   /* boost toggle */
                    if (btn == SDL_GAMEPAD_BUTTON_WEST) quick_pressed[pad_who] = 1;                  /* quick boost */
                    if (btn == SDL_GAMEPAD_BUTTON_RIGHT_STICK) lock_pressed[pad_who] = 1;            /* hard lock */
                    if (btn == SDL_GAMEPAD_BUTTON_NORTH) switch_pressed[pad_who] = 1;                /* switch weapon */
                }
                if (btn == SDL_GAMEPAD_BUTTON_START && back_held && !g_direct_test) { to_garage = 1; back_combo = 1; }
                else if (btn == SDL_GAMEPAD_BUTTON_START) { SETUP_FIGHTERS(); prevs[0] = *P; prevs[1] = *T; }
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                /* A tap on its own switches target (the step rate is on T). */
                if (!back_combo) target_pressed[!two_players ? 0 : pads[1] ? (player_of(pads, e.gbutton.which, 0) == 1) : 1] = 1;
                back_held = 0;
            }
            if (controls_key_pressed(&e, CK_TARGET)) target_pressed[0] = 1;
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
        if (to_garage && g_net) {
            /* Leaving an online match: back to your own build, offline. */
            net_link_close(net);
            net = NULL;
            g_net = 0;
            g_hangar_fighter = 0;
            battle.count = 0;                           /* back to the 1v1 test arena */
            two_players = SDL_getenv("ACR_TWO") != NULL;
            hangar_parts(parts);
            demo_load(dir, parts, &ac_data[0], err, sizeof err);
            memcpy(parts2, parts, sizeof parts2);
            demo_load(dir, parts2, &ac_data[1], err, sizeof err);
            set_rate(&clock, def, g_tick_rate);
            set_rate(&clock, def2, g_tick_rate);
            SETUP_FIGHTERS();
            prevs[0] = *P; prevs[1] = *T;
        }
        if (to_garage) { mode = 0; gui.screen = 0; }
        gdata.have_pad = pads[0] != NULL || pads[1] != NULL;
        /* ---- Rogue Arena (rogue.c): its screens, its fights ---- */
        #define LOAD_PLAYER_BUILD(src_parts) do {                                                \
            int lpb_[SLOT_COUNT];                                                                \
            memcpy(lpb_, (src_parts), sizeof lpb_);                                              \
            if (demo_load(dir, lpb_, &ac_data[0], err, sizeof err)) memcpy(parts, lpb_, sizeof lpb_); \
        } while (0)
        if (mode == 1 && g_rogue_fight) {
            if (round_over && !rogue_over_at) rogue_over_at = SDL_GetTicks();
            if (round_over && SDL_GetTicks() - rogue_over_at > 4000) rogue_done = 1;
            /* Testing aid: ACR_ROGUE_AUTOWIN=1 counts every menu fight (Rogue
             * Arena, Arena, missions) as won a moment after it starts
             * (headless screen tests); =lose as lost. ACR_AUTOEND_MS sets the
             * moment (default 1500). */
            static Uint64 autowin_at;
            if (!autowin_at) autowin_at = SDL_GetTicks();
            const char *auto_ = SDL_getenv("ACR_ROGUE_AUTOWIN");
            const Uint64 auto_ms_ = SDL_getenv("ACR_AUTOEND_MS") ? (Uint64)SDL_atoi(SDL_getenv("ACR_AUTOEND_MS")) : 1500;
            const int autowin = auto_ && SDL_GetTicks() - autowin_at > auto_ms_;
            if (rogue_give_up || rogue_done || autowin) {
                autowin_at = 0;
                const int res_ = autowin ? (SDL_strcmp(auto_, "lose") ? 1 : 2) : rogue_give_up || !round_over ? 2 : round_result;
                const int was_arena_ = g_rogue_fight == 2, was_mission_ = g_rogue_fight == 3;
                if (was_mission_) {
                    /* The income and expense report: the player's AP and the
                     * rounds fired are what the bill is figured from. */
                    if (missions_fight_over(res_, battle.f[0].ap, battle.f[0].ap_max,
                                            acr_gen1_mission_ammo_cost(&battle.f[0]),
                                            (battle.mission.flags & ACR_GEN1_MISSION_BONUS) != 0))
                        hangar_save();
                } else if (was_arena_) { if (arena_fight_over(res_)) hangar_save(); }
                else rogue_fight_over(res_);
                g_rogue_fight = 0;
                g_round_start_ms = ROUND_MS; g_round_clock = 1;
                g_ai_mode = 0; g_autopilot = 0; g_plus2 = 0; human_plus = 0;
                g_paint_on[0] = g_paint_on[1] = 0;
                rogue_over_at = 0;
                mode = was_mission_ ? MODE_MISSIONS : was_arena_ ? MODE_ARENA : MODE_ROGUE;
                if (was_arena_ || was_mission_) {   /* the garage's own AC again (paint off) */
                    int f_[SLOT_COUNT];
                    hangar_parts(f_);
                    LOAD_PLAYER_BUILD(f_);
                }
            }
        }
        if (mode == MODE_ARENA) {
            garage_action d = pad_direction(pads[0], SDL_GetTicks());
            if (d == GA_NONE) d = script_action();
            if (d != GA_NONE) { const arena_command c = arena_input(d); if (c != AC_NONE) acmd = c; }
            if (acmd == AC_LEAVE) { mode = 0; gui.screen = 0; }
            if (acmd == AC_FIGHT || arena_vs_due()) {
                /* After the VS screen: the garage's AC against the opponent,
                 * flown by its own pilot, on the stage and with the timer
                 * picked on the ARENA screen. */
                const rogue_fight *af = arena_fight_info();
                LOAD_PLAYER_BUILD(af->parts[0]);
                memcpy(parts2, af->parts[1], sizeof parts2);
                demo_load(dir, parts2, &ac_data[1], err, sizeof err);
                /* the opponent's own colours; the player's AC keeps its CHANGE COLOR paint */
                g_paint_on[0] = 0;
                g_paint_on[1] = 1; memcpy(g_paint[1], af->colours[1], sizeof g_paint[1]);
                g_rogue_prof = af->ai;
                g_ai_mode = AI_ROGUE; g_autopilot = 0; two_players = 0;
                human_plus = 0; g_plus2 = af->plus[1];
                if (!(g_stage.ok && g_stage_id == af->stage)) {
                    demo_stage tmp;
                    if (demo_stage_load(dir, af->stage, &tmp)) {
                        if (g_stage.ok) demo_stage_free(&g_stage);
                        g_stage = tmp; g_test_boundary = 0;
                        g_stage_id = af->stage;
                    }
                }
                g_stage_on = g_stage.ok && g_stage_id == af->stage;
                def->stage = def2->stage = battle.stage = g_stage_on ? &g_stage.collision : NULL;
                const int32_t limit_ = arena_round_ms();
                g_round_clock = limit_ > 0;
                g_round_start_ms = limit_ > 0 ? limit_ : ROUND_MS;
                g_rogue_fight = 2;
                set_rate(&clock, def, g_tick_rate);
                set_rate(&clock, def2, g_tick_rate);
                SETUP_FIGHTERS();
                prevs[0] = *P; prevs[1] = *T;
                last = SDL_GetTicksNS();
                rogue_over_at = give_up_until = 0;
                mode = 1;
            }
        }
        if (mode == MODE_MISSIONS) {
            garage_action d = pad_direction(pads[0], SDL_GetTicks());
            if (d == GA_NONE) d = script_action();
            if (d != GA_NONE) { const missions_command c = missions_input(d); if (c != MC_NONE) mcmd = c; }
            if (mcmd == MC_LEAVE) { mode = 0; gui.screen = 0; }
            if (mcmd == MC_SORTIE) {
                /* The garage's AC on the mission map against the mission's
                 * opponent (P4: the Nest examiner, the user's card build,
                 * flown by the balanced AI), with the mission's time limit. */
                const rogue_fight *mf = missions_fight_info();
                LOAD_PLAYER_BUILD(mf->parts[0]);
                memcpy(parts2, mf->parts[1], sizeof parts2);
                demo_load(dir, parts2, &ac_data[1], err, sizeof err);
                for (int k = 0; k < 2; ++k) { g_paint_on[k] = 1; memcpy(g_paint[k], mf->colours[k], sizeof g_paint[k]); }
                g_ai_mode = ACR_GEN1_AI_BALANCED; g_autopilot = 0; two_players = 0;
                human_plus = 0; g_plus2 = 0;
                if (!(g_stage.ok && g_stage_id == mf->stage)) {
                    demo_stage tmp;
                    if (demo_stage_load(dir, mf->stage, &tmp)) {
                        if (g_stage.ok) demo_stage_free(&g_stage);
                        g_stage = tmp;
                        g_stage_id = mf->stage;
                    }
                }
                g_stage_on = g_stage.ok && g_stage_id == mf->stage;
                def->stage = def2->stage = battle.stage = g_stage_on ? &g_stage.collision : NULL;
                const int32_t limit_ = missions_round_ms();
                g_round_clock = limit_ > 0;
                g_round_start_ms = limit_ > 0 ? limit_ : ROUND_MS;
                g_rogue_fight = 3;
                set_rate(&clock, def, g_tick_rate);
                set_rate(&clock, def2, g_tick_rate);
                SETUP_FIGHTERS();
                prevs[0] = *P; prevs[1] = *T;
                g_radio_until = 0;
                radio_say(missions_radio(0));
                SDL_Log("missions: sortie, variant %d (%s)", missions_variant(),
                        acr_gen1_mission_variant_name(missions_variant()));
                last = SDL_GetTicksNS();
                rogue_over_at = give_up_until = 0;
                mode = 1;
            }
        }
        if (mode == MODE_ROGUE) {
            garage_action d = pad_direction(pads[0], SDL_GetTicks());
            if (d == GA_NONE) d = script_action();
            if (d != GA_NONE) { const rogue_command c = rogue_input(d); if (c != RC_NONE) rcmd = c; }
            if (rcmd == RC_LEAVE) {            /* back to the main garage and its AC */
                mode = 0; gui.screen = 0; gui.rogue = 0; gdata = gdata_main;
                int f_[SLOT_COUNT];
                hangar_parts(f_);
                LOAD_PLAYER_BUILD(f_);
                g_paint_on[0] = 0;
            }
            if (rcmd == RC_GARAGE) {           /* the run's garage: assembly only */
                mode = 0; gui.screen = 0; gui.menu = 0; gui.rogue = 1; gdata = rogue_garage_data();
                int f_[SLOT_COUNT];
                if (rogue_player_ac(f_, g_paint[0])) { g_paint_on[0] = 1; LOAD_PLAYER_BUILD(f_); }
            }
            if (rcmd == RC_FIGHT || rogue_vs_due()) {
                /* The run's AC against the next opponent, flown by its own
                 * pilot (its AI sliders and Human PLUS), on the stage picked
                 * at the hub; two minutes, as in the arena. */
                const rogue_fight *rf = rogue_fight_info();
                LOAD_PLAYER_BUILD(rf->parts[0]);
                memcpy(parts2, rf->parts[1], sizeof parts2);
                demo_load(dir, parts2, &ac_data[1], err, sizeof err);
                for (int k = 0; k < 2; ++k) { g_paint_on[k] = 1; memcpy(g_paint[k], rf->colours[k], sizeof g_paint[k]); }
                g_rogue_prof = rf->ai;
                g_ai_mode = AI_ROGUE; g_autopilot = 0; two_players = 0;
                human_plus = rf->plus[0] > 0; g_plus2 = rf->plus[1];
                if (!(g_stage.ok && g_stage_id == rf->stage)) {
                    demo_stage tmp;
                    if (demo_stage_load(dir, rf->stage, &tmp)) {
                        if (g_stage.ok) demo_stage_free(&g_stage);
                        g_stage = tmp; g_test_boundary = 0;
                        g_stage_id = rf->stage;
                    }
                }
                g_stage_on = g_stage.ok && g_stage_id == rf->stage;
                def->stage = def2->stage = battle.stage = g_stage_on ? &g_stage.collision : NULL;
                set_rate(&clock, def, g_tick_rate);
                set_rate(&clock, def2, g_tick_rate);
                SETUP_FIGHTERS();
                prevs[0] = *P; prevs[1] = *T;
                last = SDL_GetTicksNS();
                g_rogue_fight = 1;
                rogue_over_at = give_up_until = 0;
                mode = 1;
            }
        }
        if (mode == MODE_CONTROLS) {
            if (!cui.waiting) {
                garage_action d = pad_direction(pads[0], SDL_GetTicks());
                if (d == GA_NONE) d = script_action();
                if (controls_input(&cui, d)) { mode = 0; gui.screen = 0; }
            }
        }
        if (mode == MODE_SETTINGS) {
            garage_action d = pad_direction(pads[0], SDL_GetTicks());
            if (d == GA_NONE) d = script_action();
            if (settings_input(&sui, d)) mode = 0;
            if (sui.apply) {   /* put the changed settings into effect */
                sui.apply = 0;
                if (!window_env) {
                    SDL_SetWindowFullscreen(window, g_settings.fullscreen != 0);
                    if (!g_settings.fullscreen) {
                        int ww, wh;
                        settings_window_size(g_settings.window, &ww, &wh);
                        SDL_SetWindowSize(window, ww, wh);
                    }
                }
                g_hud_frame = g_settings.hud_wide ? BATTLE_HUD_FRAME_WIDE : BATTLE_HUD_FRAME_43;
                g_smooth_anim = g_settings.smooth_anim;
                sfx_set_volume((float)g_settings.fx_volume / 10.0f, (float)g_settings.menu_volume / 10.0f);
                if (sui.page == 1) sfx_ui(SFX_UI_CURSOR);   /* hear the new menu volume */
            }
        }
        if (mode == MODE_SETTINGS) {
            static Uint64 settings_last_ms;
            const Uint64 now_ms = SDL_GetTicks();
            const int elapsed = settings_last_ms ? (int)(now_ms - settings_last_ms) : 0;
            settings_last_ms = now_ms;
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
            acr_render_begin(pw, ph, 18, 22, 28);
            settings_draw(&sui, acr_render_hud(0, 0, pw, ph), elapsed);
            goto present;
        }
        if (mode == MODE_CONTROLS) {
            static Uint64 controls_last_ms;
            const Uint64 now_ms = SDL_GetTicks();
            const int elapsed = controls_last_ms ? (int)(now_ms - controls_last_ms) : 0;
            controls_last_ms = now_ms;
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
            acr_render_begin(pw, ph, 18, 22, 28);
            controls_draw(&cui, acr_render_hud(0, 0, pw, ph), elapsed);
            goto present;
        }
        if (mode == MODE_ARENA) {
            static Uint64 arena_last_ms;
            const Uint64 now_ms = SDL_GetTicks();
            const int elapsed = arena_last_ms ? (int)(now_ms - arena_last_ms) : 0;
            arena_last_ms = now_ms;
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
            acr_render_begin(pw, ph, 8, 10, 14);
            g_screen_pw = pw; g_screen_ph = ph;
            arena_draw(acr_render_hud(0, 0, pw, ph), elapsed);
            goto present;
        }
        if (mode == MODE_MISSIONS) {
            static Uint64 missions_last_ms;
            const Uint64 now_ms = SDL_GetTicks();
            const int elapsed = missions_last_ms ? (int)(now_ms - missions_last_ms) : 0;
            missions_last_ms = now_ms;
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
            acr_render_begin(pw, ph, 8, 10, 14);
            g_screen_pw = pw; g_screen_ph = ph;
            missions_draw(acr_render_hud(0, 0, pw, ph), elapsed);
            goto present;
        }
        if (mode == MODE_ROGUE) {
            static Uint64 rogue_last_ms;
            const Uint64 now_ms = SDL_GetTicks();
            const int elapsed = rogue_last_ms ? (int)(now_ms - rogue_last_ms) : 0;
            rogue_last_ms = now_ms;
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
            acr_render_begin(pw, ph, 8, 10, 14);
            g_screen_pw = pw; g_screen_ph = ph;
            rogue_draw(acr_render_hud(0, 0, pw, ph), elapsed);
            goto present;
        }
        if (mode == 0) {
            garage_action dir_action = pad_direction(pads[0], SDL_GetTicks());
            if (dir_action == GA_NONE) dir_action = script_action();
            if (dir_action != GA_NONE) {
                const garage_command c = garage_input(&gui, &gdata, dir_action);
                if (c != GC_NONE) gcmd = c;
            }
            if (gui.rogue) {                   /* the Rogue Arena run's garage */
                if (gcmd == GC_CHANGED) {
                    int f_[SLOT_COUNT];
                    rogue_player_ac(f_, g_paint[0]);
                    LOAD_PLAYER_BUILD(f_);
                }
                if (gcmd == GC_QUIT) { rogue_garage_done(); gui.rogue = 0; gdata = gdata_main; mode = MODE_ROGUE; }
                gcmd = GC_NONE;
            }
            if (gcmd == GC_ROGUE) { rogue_open(g_hangar.pilot_name, g_hangar.ac_name); mode = MODE_ROGUE; }
            if (gcmd == GC_ARENA) { arena_open(); mode = MODE_ARENA; }
            if (gcmd == GC_MISSIONS) { missions_open(); mode = MODE_MISSIONS; }
            if (gcmd == GC_CONTROLS) { controls_open(&cui); mode = MODE_CONTROLS; }
            if (gcmd == GC_VIDEO || gcmd == GC_AUDIO) { settings_open(&sui, gcmd == GC_AUDIO); mode = MODE_SETTINGS; }
            if (gcmd == GC_QUIT) running = 0;
            if (gcmd == GC_NETWORK) {
                mode = 2;
                nmenu.message_ms = 0;
                for (int i = 0; i < NET_MENU_STAGES; ++i) {
                    const int id = i == 0 ? 27 : i == 1 ? 39 : 0;
                    nmenu.stage_ok[i] = id == 0 || net_stage_check(dir, id) != 0;
                }
                if (!nmenu.stage_ok[nmenu.stage_pick]) nmenu.stage_pick = 2;
                SDL_StartTextInput(window);
            }
            if (gcmd == GC_SAVE) garage_say(&gui, hangar_save() ? "SAVED." : "COULD NOT SAVE.");
            if (gcmd == GC_LOAD) {
                if (hangar_load()) {
                    int fitted[SLOT_COUNT];
                    hangar_parts(fitted);
                    if (demo_load(dir, fitted, &ac_data[0], err, sizeof err)) memcpy(parts, fitted, sizeof fitted);
                    garage_say(&gui, "LOADED.");
                } else garage_say(&gui, "NO SAVE TO LOAD.");
            }
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
        if (mode >= MODE_NET_FIRST && mode <= MODE_NET_LAST) {
            const Uint64 now_ms = SDL_GetTicks();
            static Uint64 net_last_ms;
            const int elapsed = net_last_ms ? (int)(now_ms - net_last_ms) : 0;
            net_last_ms = now_ms;
            if (net_action == GA_NONE && mode == 2) net_action = pad_direction(pads[0], now_ms);
            if (net_action == GA_NONE) net_action = script_action();
            if (mode == 2) {
                if (!finder) finder = net_finder_open(dir);
                nmenu.found_n = net_finder_poll(finder, now_ms, nmenu.found, NET_MENU_FOUND);
                char join_to[256];
                const net_menu_command c = net_menu_input(&nmenu, net_action, net_action_keys, join_to, sizeof join_to);
                if (c == NM_BACK) { mode = 0; gui.screen = 0; SDL_StopTextInput(window); }
                if (c == NM_JOIN_BRIEF) {
                    brief_edit_start(&bedit, ACR_BRIEF_OFFER);
                    bedit_host = 0;
                    mode = 5;
                }
                if (c == NM_HOST || c == NM_JOIN || c == NM_HOST_ONLINE) {
                    int own[SLOT_COUNT];
                    hangar_parts(own);
                    net_status[0] = 0;
                    net_online = c == NM_HOST_ONLINE;
                    net = net_open(dir, c == NM_JOIN ? join_to : NULL, net_online, net_menu_stage(&nmenu), nmenu.delay,
                                   nmenu.coop_ai, nmenu.ai_preset, own, net_status, sizeof net_status);
                    net_open_ms = now_ms;
                    net_readback = 0;
                    brief_msg[0] = 0;
                    mode = net ? 3 : 4;
                }
            } else if (mode == 5) {
                /* Entering a briefing (player 2) or a read-back (player 1). */
                net_addr to;
                const brief_result r = brief_edit_input(&bedit, net_action, &to);
                if (net_paste || r == BE_PASTE) {
                    char *clip = SDL_GetClipboardText();
                    brief_edit_paste(&bedit, clip);
                    SDL_free(clip);
                }
                if (r == BE_CANCEL) mode = bedit_host ? 3 : 2;
                if (r == BE_DONE && bedit_host) {
                    net_link_punch(net, &to);
                    net_readback = 1;
                    mode = 3;
                } else if (r == BE_DONE) {
                    int own[SLOT_COUNT];
                    hangar_parts(own);
                    SDL_snprintf(brief_join, sizeof brief_join, "%d.%d.%d.%d:%d", to.ip[0], to.ip[1], to.ip[2], to.ip[3], to.port);
                    net_status[0] = 0;
                    net_online = 1;
                    net = net_open(dir, brief_join, 1, 0, 0, 0, 0, own, net_status, sizeof net_status);
                    net_open_ms = now_ms;
                    brief_msg[0] = 0;
                    mode = net ? 3 : 4;
                }
            }
            if (mode == 3 && net && net_online) {
                /* Copying a code, and player 1 opening the read-back editor. */
                net_addr me;
                const int stun = net_link_public(net, &me);
                if (net_copy) {
                    if (stun == 1 && (g_net_host || now_ms - net_open_ms >= BRIEF_TRY_MS + BRIEF_URGENT_MS)) {
                        char text[300];
                        brief_copy_text(&me, g_net_host ? ACR_BRIEF_OFFER : ACR_BRIEF_REPLY, text, sizeof text);
                        SDL_SetClipboardText(text);
                        SDL_Log("netplay: copied \"%s\"", text);
                        SDL_strlcpy(brief_msg, "COPIED TO THE CLIPBOARD.", sizeof brief_msg);
                    } else {
                        SDL_strlcpy(brief_msg, "NOTHING TO COPY YET.", sizeof brief_msg);
                    }
                    brief_msg_until = now_ms + 3000;
                }
                if (g_net_host && stun == 1 && net_action == GA_OK) {
                    brief_edit_start(&bedit, ACR_BRIEF_REPLY);
                    bedit_host = 1;
                    mode = 5;
                    net_action = GA_NONE;
                }
                if (now_ms > brief_msg_until) brief_msg[0] = 0;
                static net_link *logged;              /* the code is logged once per link (for support) */
                if (stun == 1 && logged != net && (g_net_host || now_ms - net_open_ms >= BRIEF_TRY_MS)) {
                    char text[300];
                    brief_copy_text(&me, g_net_host ? ACR_BRIEF_OFFER : ACR_BRIEF_REPLY, text, sizeof text);
                    SDL_Log("netplay: %s", text);
                    logged = net;
                }
            }
            if (mode == 5 && bedit_host && net) {
                /* Player 1 keeps answering while typing the read-back: player 2
                 * may get through on their own. */
                const int st = net_link_connect(net, now_ms, &net_info, net_status, sizeof net_status);
                if (st != NET_CONNECTING) mode = 3;     /* the mode 3 branch below takes it from here next frame */
            } else if (mode == 3) {
                if (net_action == GA_CANCEL) {
                    net_link_close(net);
                    net = NULL;
                    if (g_direct_test) running = 0;
                    mode = 2;
                } else {
                    const int st = net_link_connect(net, now_ms, &net_info, net_status, sizeof net_status);
                    if (st == NET_FAILED) {
                        SDL_Log("netplay: %s", net_link_error(net));
                        SDL_snprintf(net_status, sizeof net_status, "%s", net_link_error(net));
                        net_link_close(net);
                        net = NULL;
                        mode = 4;
                    } else if (st == NET_STARTED) {
                        /* The match starts: both PCs build the same one. Player 1
                         * is the host's garage build, player 2 the guest's, on the
                         * host's stage. */
                        SDL_Log("netplay: match started, you are player %d (%s vs %s), stage %d, input delay %d",
                                net_info.local + 1, net_info.name[0], net_info.name[1], net_info.stage, net_info.delay);
                        for (int i = 0; i < SLOT_COUNT; ++i) { parts[i] = net_info.parts[0][i]; parts2[i] = net_info.parts[1][i]; }
                        g_options[0] = (uint16_t)net_info.parts[0][SLOT_COUNT];
                        g_options[1] = (uint16_t)net_info.parts[1][SLOT_COUNT];
                        demo_load(dir, parts, &ac_data[0], err, sizeof err);
                        demo_load(dir, parts2, &ac_data[1], err, sizeof err);
                        /* Co-op: the computer ACs fly copies of the players'
                         * builds (the first P1's, the second P2's), so both PCs
                         * have them already. */
                        const int coop = net_info.coop_ai > 0;
                        battle.count = (uint8_t)(coop ? 2 + net_info.coop_ai : 0);
                        for (int k = 0; k < net_info.coop_ai && k < 2; ++k) {
                            memcpy(parts_ai[k], k ? parts2 : parts, sizeof parts_ai[k]);
                            demo_load(dir, parts_ai[k], &ac_data[2 + k], err, sizeof err);
                        }
                        if (coop) SDL_Log("netplay: co-op against %d AI (%s)", net_info.coop_ai, acr_gen1_ai_preset_name(net_info.ai_preset));
                        if (net_info.stage > 0 && !(g_stage.ok && g_stage_id == net_info.stage)) {
                            demo_stage tmp;
                            if (demo_stage_load(dir, net_info.stage, &tmp)) {
                                if (g_stage.ok) demo_stage_free(&g_stage);
                                g_stage = tmp; g_test_boundary = 0;
                                g_stage_id = net_info.stage;
                            }
                        }
                        g_stage_on = net_info.stage > 0 && g_stage.ok && g_stage_id == net_info.stage;
                        g_tick_rate = 60;          /* netplay always runs 60 steps a second */
                        g_ai_mode = 0;
                        g_autopilot = 0;
                        human_plus = 0;
                        g_plus2 = 0;               /* left over from Rogue Arena: would give P2 Human PLUS on this PC only */
                        g_landing_anim = 0;        /* a local test toggle: both PCs must run the same rules */
                        show_help = 0;             /* test readouts off: no test keys online */
                        two_players = 1;
                        g_net = 1;
                        g_hangar_fighter = net_info.local;
                        for (int k = 0; k < acr_gen1_battle_fighters(&battle); ++k) {
                            set_rate(&clock, &ac_data[k].def, g_tick_rate);
                            /* Everything the fight reads is fixed before the first snapshot. */
                            ac_data[k].def.stage = g_stage_on ? &g_stage.collision : NULL;
                            ac_data[k].def.landing_from_anim = (uint8_t)g_landing_anim;
                        }
                        battle.stage = g_stage_on ? &g_stage.collision : NULL;
                        battle.step_ms = (uint16_t)(1000 / g_tick_rate);
                        battle.step_rate = def->step_rate;
                        battle.gen1_rate = def->gen1_rate;
                        SETUP_FIGHTERS();
                        net_start = battle;
                        net_start.rng = net_info.seed;
                        net_start.tick_phase = 0;  /* left over from earlier play, different on each PC */
                        acr_gen1_match_rules rules;
                        SDL_zero(rules);
                        rules.round_ms = ROUND_MS;
                        rules.next_round_ms = 5000;
                        rules.arena_half = g_stage_on ? 0 : ARENA;
                        if (coop) {
                            rules.ai_half = g_stage_on ? g_stage.half : ARENA;
                            rules.ai_seed = net_info.seed;
                            for (int k = 2; k < battle.count; ++k) {
                                rules.ai_mask |= (uint8_t)(1u << k);
                                acr_gen1_ai_preset(&rules.ai[k], net_info.ai_preset);
                            }
                        }
                        acr_rb_init(&net_rb, net_info.local, net_info.delay, &net_start, &rules);
                        battle = net_rb.cur.b;
                        for (int k = 0; k < ACR_GEN1_MAX_FIGHTERS; ++k) prevs[k] = battle.f[k].ac;
                        net_skip_cool = 0; net_lost = 0; net_skips = 0; net_wait_since = 0;
                        switch_pressed[0] = switch_pressed[1] = blade_pressed[0] = blade_pressed[1] = 0;
                        target_pressed[0] = target_pressed[1] = 0;
                        back_held = 0;
                        net_finder_close(finder);
                        finder = NULL;
                        SDL_StopTextInput(window);
                        mode = 1;
                        last = SDL_GetTicksNS();
                        net_link_touch(net, SDL_GetTicks());   /* the stage load took a while */
                        continue;
                    }
                }
            } else if (mode == 4) {
                if (net_action == GA_OK || net_action == GA_CANCEL) {
                    if (g_direct_test) running = 0;
                    mode = 2;
                }
            }
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (pw < 1 || ph < 1) { SDL_Delay(10); continue; }
            acr_render_begin(pw, ph, 18, 22, 28);
            const float hud_w = acr_render_hud(0, 0, pw, ph);
            if (mode == 2) net_menu_draw(&nmenu, hud_w, elapsed);
            else if (mode == 5) brief_edit_draw(&bedit, hud_w, elapsed);
            else if (mode == 3 && net && net_online) {
                net_addr me;
                const int stun = net_link_public(net, &me);
                if (g_net_host) brief_draw_host(hud_w, stun, &me, net_readback, (int)((now_ms - net_open_ms) / 1000), brief_msg, net_link_router(net, NULL));
                else brief_draw_guest(hud_w, stun, &me, now_ms - net_open_ms, brief_msg);
            }
            else if (mode == 3) net_menu_draw_status(hud_w, "NETWORK MATCH", net_status);
            else {
                char text[1800];
                SDL_snprintf(text, sizeof text, "%s\n\nA / ENTER: BACK", net_status);
                net_menu_draw_status(hud_w, "COULD NOT START THE MATCH", text);
            }
            goto present;
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
            {
                const float hud_w_ = 480.0f * (float)pw / (float)ph;
                float ox, oy, ow, oh;
                const int obit = garage_option_model(&gui, &gdata, hud_w_, &ox, &oy, &ow, &oh);
                if (obit >= 0) draw_option_model(obit, ph, ox, oy, ow, oh, (unsigned)(now_ms / 16));
                else draw_garage_ac(pw, ph, hud_w_, (unsigned)(now_ms / 16));
            }
            garage_draw(&gui, &gdata, acr_render_hud(0, 0, pw, ph), elapsed);
            goto present;
        }
        /* Who uses what: one player gets the keyboard and the first controller;
         * with two players, player 2 takes the second controller (or the only
         * one, leaving player 1 the keyboard). */
        SDL_Gamepad *ctl[2] = { pads[0], NULL };
        if (two_players && !g_net) {
            if (pads[1]) ctl[1] = pads[1];
            else { ctl[1] = pads[0]; ctl[0] = NULL; }
        }
        acr_gen1_input in2[2];
        /* Online this PC's player is slot 0 here, whichever player it is. */
        const int mod[2] = { g_net ? modern_of(net_info.local) : modern_of(0), !g_net && modern_of(1) };
        in2[0] = mod[0] ? read_modern(ctl[0], cruise_on[0]) : read_input(ctl[0], 1);
        in2[1] = !two_players ? (acr_gen1_input){ 0 } : mod[1] ? read_modern(ctl[1], cruise_on[1]) : read_input(ctl[1], 0);
        for (int k = 0; k < 2; ++k) {
            if (!mod[k]) { cruise_on[k] = 0; quick_pressed[k] = lock_pressed[k] = 0; continue; }
            /* Running dry switches the boost toggle off (press circle again after). */
            if (battle.f[g_net ? net_info.local : k].ac.drained) cruise_on[k] = 0;
            in2[k].cruise = (uint8_t)cruise_on[k];
            in2[k].quick = (uint8_t)quick_pressed[k];
            /* L2: the blade, on the press. */
            const int l2 = ctl[k] && SDL_GetGamepadAxis(ctl[k], SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000;
            if (l2 && !l2_was[k] && !back_held) blade_pressed[k] = 1;
            l2_was[k] = l2;
        }
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
        acr_gen1_actions act[2] = { { 0 }, { 0 } };
        act[0].fire = (uint8_t)(hold_fire || (!mod[0] && controls_key_held(CK_FIRE)) ||
                                (ctl[0] && !back_held && (mod[0] ? SDL_GetGamepadAxis(ctl[0], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000
                                                                 : controls_held(ctl[0], CT_FIRE))));
        if (two_players)
            act[1].fire = (uint8_t)(ctl[1] && !back_held && (mod[1] ? SDL_GetGamepadAxis(ctl[1], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000
                                                                    : controls_held(ctl[1], CT_FIRE)));

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
        if (g_net) {
            /* Online: rollback runs the match (acr_rb_tick: our controls now,
             * the other player's as they arrive, going back to fix guesses). */
            const Uint64 now_ms = SDL_GetTicks();
            uint8_t pkt[ACR_RB_PACKET_MAX];
            int len;
            while ((len = net_link_recv(net, pkt, sizeof pkt, now_ms)) > 0)
                if (acr_rb_read(&net_rb, pkt, len) == 2) net_lost = 2;    /* they left */
            /* Nothing heard for 10 s: shown as lost, but the match goes on if they come back. */
            net_lost = net_lost == 2 ? 2 : net_link_quiet_ms(net, now_ms) > 10000;
            const int me = net_info.local;
            acr_gen1_ac before[ACR_GEN1_MAX_FIGHTERS];
            for (int k = 0; k < ACR_GEN1_MAX_FIGHTERS; ++k) before[k] = battle.f[k].ac;
            int ran = 0;
            for (uint32_t i = 0; i < ticks && !net_lost; ++i) {
                /* The PC that runs ahead skips a step now and then. */
                if (net_skip_cool > 0) --net_skip_cool;
                else if (acr_rb_should_wait(&net_rb)) { net_skip_cool = 10; ++net_skips; continue; }
                acr_gen1_actions a = act[0];
                a.next_weapon = (uint8_t)switch_pressed[0];
                a.blade = (uint8_t)blade_pressed[0];
                a.switch_target = (uint8_t)target_pressed[0];
                a.hard_lock = (uint8_t)lock_pressed[0];
                in2[0].quick = (uint8_t)quick_pressed[0];
                if (!acr_rb_tick(&net_rb, acr_rb_pack(&in2[0], &a))) {   /* waiting for them */
                    if (!net_wait_since) net_wait_since = now_ms;
                    break;
                }
                net_wait_since = 0;
                ++ran;
                switch_pressed[0] = blade_pressed[0] = target_pressed[0] = lock_pressed[0] = quick_pressed[0] = 0;
                in2[0].boost_tapped = 0;
                boost_released[0] = !in2[0].boost;
            }
            acr_rb_settle(&net_rb);
            if (!net_lost) {
                const int n = acr_rb_write(&net_rb, pkt, sizeof pkt);
                if (n) net_link_send(net, pkt, n, now_ms);
            }
            net_link_pump(net, now_ms);
            battle = net_rb.cur.b;
            if (ran) for (int k = 0; k < ACR_GEN1_MAX_FIGHTERS; ++k) prevs[k] = before[k];
            round_ms = net_rb.cur.round_ms;
            round_over = net_rb.cur.round_over;
            round_result = net_rb.cur.result;
            if (net_rb.frame > 0) {     /* the poses follow the controls the match used */
                const uint32_t f = net_rb.frame - 1;
                acr_gen1_actions unused;
                acr_rb_unpack(net_rb.pad[me][f % ACR_RB_RING], &g_in[me], &unused);
                acr_rb_unpack(net_rb.used[f % ACR_RB_RING], &g_in[1 - me], &unused);
                for (int k = 2; k < acr_gen1_battle_fighters(&battle); ++k) g_in[k] = net_rb.cur.shown_in[k];
            }
        } else
        for (uint32_t i = 0; i < ticks; ++i) {
            prevs[0] = *P;
            prevs[1] = *T;
            /* Two players: a two-minute round, decided as the VS engine
             * does: a knock-out settles 12 ticks after it (both down is a
             * draw); at time up the higher AP wins, equal AP is a draw. */
            if (round_over) {
                /* Round over: the game keeps running with every pilot's
                 * controls let go (AI included), so the ACs drop to the
                 * ground where they are. */
            } else if (two_players || g_ai_mode) {
                if (g_round_clock) round_ms -= battle.step_ms;
                if (round_ms < 0) round_ms = 0;
                const int res = acr_gen1_battle_result(&battle, round_ms <= 0);
                if (!res) ko_ms = 0;      /* back in the area before it settled */
                if (res && (round_ms <= 0 || (ko_ms += battle.step_ms) >= 12 * 1000 / 22)) {
                    round_over = 1;
                    round_result = res;
                    break;
                }
            } else if (battle.units) {
                /* A mission map in AC TEST: no clock; won once every enemy
                 * target (the target AC and the units in the fight) is
                 * down, lost when the player is down or out of the area. */
                const int res = acr_gen1_battle_result(&battle, 0);
                if (!res) ko_ms = 0;
                if (res && (ko_ms += battle.step_ms) >= 12 * 1000 / 22) {
                    round_over = 1;
                    round_result = res;
                    break;
                }
            } else if (g_stage_on && acr_gen1_stage_has_area_rule(&g_stage.collision)) {
                /* Against the still target there is no clock, but leaving
                 * the area (outside both inner boxes) still loses. */
                const int o0 = acr_gen1_battle_out_of_area(&battle, 0) == 2,
                          o1 = acr_gen1_battle_out_of_area(&battle, 1) == 2;
                const int res = o0 || o1 ? (o0 && o1 ? 3 : o0 ? 2 : 1) : 0;
                if (!res) ko_ms = 0;
                if (res && (ko_ms += battle.step_ms) >= 12 * 1000 / 22) {
                    round_over = 1;
                    round_result = res;
                    break;
                }
            }
            for (int k = 0; k < 2; ++k) {
                act[k].next_weapon = (uint8_t)switch_pressed[k]; switch_pressed[k] = 0;
                act[k].blade = (uint8_t)blade_pressed[k]; blade_pressed[k] = 0;
                act[k].switch_target = (uint8_t)target_pressed[k]; target_pressed[k] = 0;
                act[k].hard_lock = (uint8_t)lock_pressed[k]; lock_pressed[k] = 0;
                in2[k].quick = (uint8_t)quick_pressed[k]; quick_pressed[k] = 0;
            }
            /* The computer pilot presses its buttons like a pad, each step. */
            for (int k = 0; k < 2; ++k) {
                const int by_ai = g_ai_mode && ((k == 1 && !two_players) || (k == 0 && g_autopilot));
                if (!by_ai) continue;
                acr_gen1_ai_step(&g_ai[k], &battle, k, g_stage_on ? g_stage.half : ARENA, &in2[k], &act[k]);
                g_in[k] = in2[k];
            }
            if (round_over) {
                SDL_memset(in2, 0, sizeof in2);
                SDL_memset(act, 0, sizeof act);
            }
            /* The arena's floors and walls (the defs are reloaded on part changes). */
            def->stage = def2->stage = battle.stage = g_stage_on ? &g_stage.collision : NULL;
            acr_gen1_battle_step(&battle, in2, act);
            if (battle.mission.flags & ACR_GEN1_MISSION_SWAP) {
                /* P4 Altered circumstances: the examiner withdraws and the
                 * challenger (an arena AC, flown by its own AI) drops in
                 * from above on the east side; the clock gets its extra
                 * time and the Nest says to go on. */
                if (missions_challenger()) {
                    const rogue_fight *cf = missions_fight_info();
                    memcpy(parts2, cf->parts[1], sizeof parts2);
                    demo_load(dir, parts2, &ac_data[1], err, sizeof err);
                    g_paint_on[1] = 1;
                    memcpy(g_paint[1], cf->colours[1], sizeof g_paint[1]);
                    g_rogue_prof = cf->ai;
                    g_ai_mode = AI_ROGUE;
                    g_plus2 = cf->plus[1];
                    g_options[1] = missions_foe_options();
                    SETUP_FIGHTER(1);
                    acr_gen1_mission_spot drop = acr_gen1_p4_challenger_drop;
                    drop.y -= ACR_GEN1_P4_DROP_HEIGHT;
                    spot_spawn(T, def2, &drop);
                    prevs[1] = *T;
                    const acr_gen1_ai_profile p_ = ai_profile(g_ai_mode);
                    acr_gen1_ai_init(&g_ai[1], &p_, 0x5678u + (uint32_t)(++g_ai_round) * 7919u);
                    battle.targeted = 0;
                    acr_gen1_battle_retarget(&battle);
                } else
                    SDL_Log("missions: the challenger isn't in this import (re-import your disc); the examiner stays");
                round_ms += missions_extra_ms();
                acr_gen1_mission_challenger_in(&battle);
                radio_say(missions_radio(1));
            }
            if (battle.mission.map) {      /* the map's script: log each step it takes */
                static acr_gen1_mission last_m;
                const acr_gen1_mission *m_ = &battle.mission;
                if (m_->phase != last_m.phase || m_->result != last_m.result || (m_->flags & ~last_m.flags) ||
                    (m_->end_ticks && !last_m.end_ticks)) {
                    int up = 0;
                    for (int u_ = 0; u_ < battle.units; ++u_) up += battle.unit[u_].active && !battle.unit[u_].down;
                    SDL_Log("mission %02d script: step %d, flags %d, ending in %d ticks%s (%d units in the fight)",
                            m_->map - 1, m_->phase, m_->flags, m_->end_ticks,
                            m_->result == ACR_GEN1_MISSION_FAILED ? ", FAILED" : m_->result ? ", DONE" : "", up);
                }
                last_m = *m_;
            }
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
        for (int k = 2; k < acr_gen1_battle_fighters(&battle); ++k) refresh_model(k, dir, parts_ai[k - 2], ac_data[k].model);
        const int nf = acr_gen1_battle_fighters(&battle);
        const int views = two_players && !g_net ? 2 : 1;
        {   /* weapon effects: once a frame, watching the fight from outside */
            static int fx_tried;
            if (!fx_tried) { fx_tried = 1; fx_load(g_import_dir); }
            for (int k = 0; k < nf; ++k) fx_set_weapons(k, (const char (*)[24])ac_data[k].weapon_type, ac_data[k].weapons);
            fx_update(&battle, SDL_GetTicksNS());
        }
        {   /* fight sounds: the same, heard from your own AC (player 1 on a shared screen) */
            static int sfx_tried;
            if (!sfx_tried) { sfx_tried = 1; sfx_open(g_import_dir); }
            const int ear = g_net ? net_info.local : 0;
            for (int k = 0; k < nf; ++k) sfx_set_weapons(k, (const char (*)[24])ac_data[k].weapon_type, ac_data[k].weapons);
            sfx_listen sl;
            SDL_zero(sl);
            sl.view = ear;
            sl.red_zone = ac_data[ear].def.stats.red_zone;
            sl.out_of_area = acr_gen1_battle_out_of_area(&battle, ear) != 0;
            sl.round_over = round_over;
            sfx_update(&battle, &sl, SDL_GetTicksNS());
        }
        for (int vi = 0; vi < views; ++vi) {
        const int view = g_net ? net_info.local : vi;   /* online: your own player, full screen */
        const int vx = vi * pw / views, vpw = (vi + 1) * pw / views - vx;
        const acr_gen1_ac ac = battle.f[view].ac;
        const acr_gen1_ac prev = prevs[view];
        /* The AC this view's pilot is aimed at (its target; the other AC in 1v1). */
        const int tgi = acr_gen1_battle_target(&battle, view) >= 0 ? acr_gen1_battle_target(&battle, view) : 1 - view;
        const acr_gen1_ac *const T = &battle.f[tgi].ac;
        const acr_gen1_ac tprev = prevs[tgi];
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
        float sky_eye[3] = { 0, 0, 0 };
        float cam_fwd[3] = { 0, 0, 1 };
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
                        cam_y = cf->cam_yaw_prev + (float)(((cf->cam_yaw - cf->cam_yaw_prev + 2048) & 4095) - 2048) * ct;
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
                for (int c = 0; c < 3; ++c) { eye[c] = cam_eye[view][c]; look[c] = cam_look[view][c]; sky_eye[c] = eye[c]; }
            }
            const float fwd[3] = { look[0] - eye[0], look[1] - eye[1], look[2] - eye[2] };
            const float up[3] = { 0, 1, 0 };
            acr_mat_look(viewm, eye, fwd, up);
            for (int c = 0; c < 3; ++c) cam_fwd[c] = fwd[c];
            for (int k = 0; k < 4; ++k) viewm[4 * k] = -viewm[4 * k];
        }
        acr_fog fog = { 30000.0f, 60000.0f, 18, 22, 28 };
        if (g_stage_on) demo_stage_fog(&g_stage, &fog);   /* our backdrop's horizon haze */
        acr_render_view3d(vx, 0, vpw, ph, proj, viewm, &fog);

        /* the arena (its sky first, around the eye), or the open floor's grid and walls */
        if (g_stage_on) demo_stage_draw_sky(&g_stage, sky_eye, (float)SDL_GetTicks() / 1000.0f);
        if (g_stage_on) demo_stage_draw(&g_stage, 0);
        if (g_stage_on && g_rogue_fight == 3 && g_mission_units.objects && battle.units)   /* a mission's borrowed units */
            demo_units_draw(&g_mission_units, &battle, (float)SDL_GetTicksNS() / (float)ACR_GEN1_TICK_NS, &g_light);
        if (g_stage_on && g_stage_id >= 0 && g_stage_id < 20 && !g_rogue_fight) {   /* menu fights: no mission units */
            if (g_units.map != g_stage_id) {
                demo_units_free(&g_units);
                demo_units_load(g_import_dir, g_stage_id, &g_units);
            }
            demo_units_draw(&g_units, battle.units ? &battle : NULL, (float)SDL_GetTicksNS() / (float)ACR_GEN1_TICK_NS, &g_light);
        }
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
        for (int k = 0; k < nf; ++k) {
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
            if (battle.f[k].modern) {                /* modern: the stick as the AC took it */
                g_in[k].move = battle.f[k].used.move;
                g_in[k].strafe = battle.f[k].used.strafe;
                g_in[k].turn = battle.f[k].used.turn;
            }
        }
        draw_ac(view, &ac, px, py, pz, yaw, crouch);
        const float tx = (tprev.sx + (T->sx - tprev.sx) * a) / 64.0f, ty = -(tprev.sy + (T->sy - tprev.sy) * a) / 64.0f,
                    tz = (tprev.sz + (T->sz - tprev.sz) * a) / 64.0f;
        /* Every other AC still standing, between the last two steps. */
        float ox[ACR_GEN1_MAX_FIGHTERS], oy[ACR_GEN1_MAX_FIGHTERS], oz[ACR_GEN1_MAX_FIGHTERS];
        for (int j = 0; j < nf; ++j) {
            const acr_gen1_ac *o = &battle.f[j].ac, op = prevs[j];
            ox[j] = (op.sx + (o->sx - op.sx) * a) / 64.0f;
            oy[j] = -(op.sy + (o->sy - op.sy) * a) / 64.0f;
            oz[j] = (op.sz + (o->sz - op.sz) * a) / 64.0f;
            if (j == view || battle.f[j].ap <= 0) continue;
            int odyaw = (int)o->syaw - (int)op.syaw;
            if (odyaw > 2048) odyaw -= 4096;
            if (odyaw < -2048) odyaw += 4096;
            draw_ac(j, o, ox[j], oy[j], oz[j], ((float)op.syaw + odyaw * a) * (6.2831853f / 4096.0f),
                    battle.f[j].kneeling ? 150.0f : 0.0f);
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
        fx_draw(&battle, sky_eye, cam_fwd);   /* shots, flashes, smoke, blasts */

        /* HUD, on a 480-high grid as wide as this view's shape. */
        g_vw = acr_render_hud(vx, 0, vpw, ph);
        g_vh = 480.0f;
        const float rc_y = two_players ? 130.0f : g_vh - 80.0f;   /* right-hand column of the test readouts */
        const acr_gen1_stats *st = &ac_data[view].def.stats;
        {
            static int hud_tried;
            if (!hud_tried) { hud_tried = 1; battle_hud_load(g_import_dir); }
        }
        char line[160];
        #define TEXT(x, y, s) acr_hud_text((x), (y), (s), 220, 230, 235)
        if (show_help) {
            /* Test readouts (F1), under the help. */
            static const char *const k_state[] = { "GROUND", "CROUCH", "AIR", "LANDING" };
            /* Speeds per Gen 1 tick, as the recomp measures them. */
            const int dx = (ac.x - ac.px) / 64, dz = (ac.z - ac.pz) / 64, dy = (ac.y - ac.py) / 64;
            acr_hud_rect(0, g_vh - 100, g_vw, 100, 0, 0, 0, 140, 1);
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
            TEXT(20, g_vh - 64, line);
            SDL_snprintf(line, sizeof line, "%d STEPS/S%s  HUD %s (F9)%s", g_tick_rate, g_tick_rate == 22 ? " (GEN 1)" : "",
                         g_hud_frame ? "WIDE" : "4:3", battle.f[view].human_plus ? "  HUMAN PLUS" : "");
            TEXT(20, g_vh - 32, line);
        }
        /* Weapon, ammo, AP, lock box and the target's marker. */
        {
            const acr_gen1_fighter *me = &battle.f[view], *tg = &battle.f[tgi];
            /* A mission unit aimed at: its marker, AP and radar dot instead. */
            const int tany = acr_gen1_battle_target_any(&battle, view);
            const acr_gen1_unit *tun = tany >= ACR_GEN1_MAX_FIGHTERS ? &battle.unit[tany - ACR_GEN1_MAX_FIGHTERS] : NULL;
            const int tg_alive = tun ? tun->ap > 0 && !tun->down : tg->ap > 0;
            float mk[3] = { tx, ty + 100, tz };
            if (tun) { mk[0] = (float)tun->x / 64.0f; mk[1] = -(float)tun->y / 64.0f + tun->height * 0.5f; mk[2] = (float)tun->z / 64.0f; }
            if (me->weapons) {
                const acr_gen1_weapon *w = &me->weapon[me->current];
                if (w->stance && !me->human_plus) {
                    const char *hint = me->legs == ACR_GEN1_LEGS_TRACKS ? NULL
                                     : me->legs == ACR_GEN1_LEGS_FOUR ? (acr_gen1_stance_ok(me) ? NULL : "LAND TO FIRE")
                                     : !me->kneeling ? "FIRE TO KNEEL" : acr_gen1_stance_ok(me) ? "KNEELING" : "KNEELING...";
                    if (hint) battle_hud_text(g_vw, g_hud_frame, 1, 312.0f - 4.0f * (float)SDL_strlen(hint), 160, hint, 0,
                                              255, 255, 0);
                }
                if (me->modern) {
                    const char *mh = !ctl[g_net ? 0 : view] ? "CONNECT A TWIN-STICK CONTROLLER"
                                   : me->hard_lock ? "HARD LOCK" : NULL;
                    if (mh) battle_hud_text(g_vw, g_hud_frame, 1, 312.0f - 4.0f * (float)SDL_strlen(mh), 172, mh, 0, 255, 160, 60);
                }
                if (w->box_w && (!me->modern || me->hard_lock)) {   /* modern: only with hard lock */
                    /* The game's sight (VS 0x800576E0): the inner lock box as
                     * an octagon, half-sizes sin(angle) and corners cut by 512,
                     * on a frame 16,384 out along the AC's facing and view tilt,
                     * seen through the camera. When the camera trails or swings
                     * toward the target, the sight drifts off the screen's
                     * centre. Locks are judged on the target's middle, so an AC
                     * whose body pokes out of the sight can stay locked. */
                    float pt = ((float)prev.pitch + (float)(ac.pitch - prev.pitch) * a) * (6.2831853f / 4096.0f);
                    float syaw = yaw;
                    float box_scale = 1.0f;
                    if (me->modern && me->hist_n) {   /* modern controls: the smaller sight, lagging the facing */
                        syaw = (float)me->sight_yaw * (6.2831853f / 4096.0f);
                        pt = (float)me->sight_pitch * (6.2831853f / 4096.0f);
                        box_scale = (float)(battle.modern.box_pct ? battle.modern.box_pct : ACR_GEN1_MODERN_BOX_PCT) / 100.0f;
                    }
                    const float D = (float)ACR_GEN1_SIGHT_DEPTH, cut = (float)ACR_GEN1_SIGHT_CORNER;
                    const float sw = sinf((float)(w->inner_w ? w->inner_w : w->box_w) * box_scale * (6.2831853f / 4096.0f)) * D,
                                sh = sinf((float)(w->inner_h ? w->inner_h : w->box_h) * box_scale * (6.2831853f / 4096.0f)) * D;
                    const vec3 fw = v3(sinf(syaw) * cosf(pt), sinf(pt), cosf(syaw) * cosf(pt)),
                               up = v3(-sinf(syaw) * sinf(pt), cosf(pt), -cosf(syaw) * sinf(pt)),
                               rt = v3(cosf(syaw), 0, -sinf(syaw));
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
                        const uint8_t cr = me->locked ? 255 : 64, cg = me->locked ? 60 : 144, cb = me->locked ? 50 : 56;
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
                            float ox, oy, ux, uy;   /* the game's 4 x 5 letters, at this view's HUD scale */
                            battle_hud_point(g_vw, g_hud_frame, 0, 0, 0, &ox, &oy);
                            battle_hud_point(g_vw, g_hud_frame, 0, 1, 1, &ux, &uy);
                            battle_hud_text(g_vw, g_hud_frame, 0, (bx + bw * 0.5f - ox) / (ux - ox) + 3, (by - bh * 0.5f) / uy, line, 0,
                                            255, 60, 50);
                        }
                    }
                }
                float sx, sy;
                if (tg_alive && acr_gen1_in_lock_box(&battle, view, w) &&
                    to_hud(proj, viewm, v3(mk[0], mk[1], mk[2]), g_vw, &sx, &sy)) {
                    if (me->locked) {
                        acr_hud_rect(sx - 18, sy - 18, 36, 36, 255, 60, 50, 255, 0);
                        {
                            /* The game's 6 x 7 letters, above the marker; at
                             * this view's HUD scale. */
                            float ox, oy, ux, uy;
                            battle_hud_point(g_vw, g_hud_frame, 0, 0, 0, &ox, &oy);
                            battle_hud_point(g_vw, g_hud_frame, 0, 1, 1, &ux, &uy);
                            battle_hud_text(g_vw, g_hud_frame, 0, (sx - ox) / (ux - ox) - 12, (sy - 18) / uy - 10, "LOCK", 1,
                                            255, 60, 50);
                        }
                    } else {
                        acr_hud_rect(sx - 18, sy - 18, 36, 36, 240, 220, 60, 255, 0);
                    }
                }
            }
            (void)rc_y;
            /* Co-op: your teammate's name over their AC. */
            for (int j = 0; j < nf && battle.count; ++j) {
                float sx, sy;
                if (j == view || battle.f[j].ap <= 0 ||
                    acr_gen1_battle_team(&battle, j) != acr_gen1_battle_team(&battle, view) ||
                    !to_hud(proj, viewm, v3(ox[j], oy[j] + 260, oz[j]), g_vw, &sx, &sy)) continue;
                const char *nm = g_net && j < 2 ? net_info.name[j] : "ALLY";
                acr_hud_text(sx - 4.0f * (float)SDL_strlen(nm), sy - 10, nm, 120, 230, 150);
            }
            /* The game's HUD (battle_hud.c). */
            {
                battle_hud_state hs;
                SDL_zero(hs);
                hs.ap = me->ap;
                hs.energy = ac.energy;
                hs.energy_max = st->energy_capacity;
                hs.red_zone = st->red_zone;
                hs.drained = ac.drained;
                hs.enemy_ap = tun ? (tg_alive ? tun->ap : 0) : tg->ap > 0 ? tg->ap : 0;
                hs.heading = ac.syaw;
                for (int k = 0; k < me->weapons && hs.weapons < BATTLE_HUD_MAX_WEAPONS; ++k) {
                    const acr_gen1_weapon *wk = &me->weapon[k];
                    if (wk->kind == ACR_GEN1_BLADE && me->weapons > 1) continue;   /* the blade has its own button */
                    if (k == me->current) hs.current = hs.weapons;
                    hs.weapon[hs.weapons].name = ac_data[view].weapon_type[k][0] ? ac_data[view].weapon_type[k] : wk->name;
                    hs.weapon[hs.weapons].ammo = me->ammo[k];
                    hs.weapon[hs.weapons].ammo_max = wk->ammo;
                    ++hs.weapons;
                }
                hs.radar_range = ac_data[view].radar_range;
                hs.enemy_dx = tun ? (tun->x - ac.sx) / 64 : (T->sx - ac.sx) / 64;
                hs.enemy_dz = tun ? (tun->z - ac.sz) / 64 : (T->sz - ac.sz) / 64;
                hs.enemy_on_radar = tg_alive;
                /* Co-op: every other AC on the radar too, teammates in their own colour. */
                for (int j = 0; j < nf && hs.others < BATTLE_HUD_MAX_OTHERS; ++j) {
                    if (j == view || (j == tgi && !tun) || battle.f[j].ap <= 0) continue;
                    hs.other[hs.others].dx = (battle.f[j].ac.sx - ac.sx) / 64;
                    hs.other[hs.others].dz = (battle.f[j].ac.sz - ac.sz) / 64;
                    hs.other[hs.others].ally = acr_gen1_battle_team(&battle, j) == acr_gen1_battle_team(&battle, view);
                    ++hs.others;
                }
                for (int u = 0; u < battle.units && hs.others < BATTLE_HUD_MAX_OTHERS; ++u) {   /* mission units */
                    const acr_gen1_unit *un = &battle.unit[u];
                    if (!un->active || un->down || un == tun) continue;
                    hs.other[hs.others].dx = (un->x - ac.sx) / 64;
                    hs.other[hs.others].dz = (un->z - ac.sz) / 64;
                    hs.other[hs.others].ally = (un->team & 1) == acr_gen1_battle_team(&battle, view);
                    ++hs.others;
                }
                /* HIT: this AC landed one; DAMAGED: it took one. ARMOR LOW
                 * below a quarter of its AP (threshold not read from the game
                 * yet); CHARGING while the energy refills after running dry. */
                if (me->hit_flash & 4) hs.messages |= BATTLE_HUD_MSG_HIT;
                for (int j = 0; j < nf; ++j)     /* an enemy aimed at us landed a hit */
                    if (acr_gen1_battle_team(&battle, j) != acr_gen1_battle_team(&battle, view) &&
                        acr_gen1_battle_target(&battle, j) == view && (battle.f[j].hit_flash & 4))
                        hs.messages |= BATTLE_HUD_MSG_DAMAGED;
                if (acr_gen1_battle_out_of_area(&battle, view) && !round_over) hs.messages |= BATTLE_HUD_MSG_LEAVING_AREA;
                if (me->ap > 0 && me->ap * 4 < me->ap_max) hs.messages |= BATTLE_HUD_MSG_ARMOR_LOW;
                if (ac.drained) hs.messages |= BATTLE_HUD_MSG_CHARGING;
                hs.recording = 0;   /* no replays yet: REC shows once fights are recorded */
                hs.ac_name = g_net ? net_info.name[view] : view == 0 || !two_players ? g_hangar.ac_name : "";
                hs.time_ms = (two_players || g_ai_mode) && g_round_clock ? (int)round_ms : -1;
                battle_hud_draw(&hs, g_vw, g_hud_frame);
            }
            /* Round over: each player's own result in their own view. */
            if (round_over) {
                const int won = round_result == 1 + acr_gen1_battle_team(&battle, view), draw = round_result == 3;
                battle_hud_word(g_vw, g_hud_frame, draw ? BATTLE_HUD_DRAW : won ? BATTLE_HUD_WIN : BATTLE_HUD_LOSE, 70,
                                draw ? 240 : won ? 120 : 255, draw ? 240 : won ? 230 : 80, draw ? 240 : won ? 255 : 60);
            }
        }
        if (show_help && !two_players) {
            acr_hud_rect(12, 22, g_vw - 24, 128, 0, 0, 0, 160, 1);
            TEXT(20, 28, "MOVE: STICK/DPAD UP-DOWN  TURN: LEFT-RIGHT");
            TEXT(20, 40, "STRAFE: LB/RB  BOOST: A (NO DIRECTION = RISE)  LOOK UP/DOWN: LT/RT, BOTH = LEVEL (PGUP/PGDN)");
            TEXT(20, 52, "FIRE: X (F)  SWITCH WEAPON: Y (TAB)  BLADE: B (V)  SWITCH TARGET: BACK TAP (X)");
            if (g_direct_test) {
                TEXT(20, 64, "BACK+X/Y/B/RB (B/G/C/L): BOOSTER/GENERATOR/BACK WEAPON/LEGS");
                TEXT(20, 76, "START/R: RESET  F1: HIDE HELP  F5: ANIM SMOOTH/STEPPED  F9: HUD 4:3/WIDE  ESC: QUIT");
            } else {
                TEXT(20, 64, "BACK+START (ESC): BACK TO THE GARAGE");
                TEXT(20, 76, "START/R: RESET  F1: HIDE HELP  F9: HUD 4:3/WIDE  (KEYS 1-9 = F1-F9)");
            }
            TEXT(20, 88, "BACK+A (H): HUMAN PLUS  (T): STEP RATE");
            TEXT(20, 100, "BACK+LB (F2): TWO PLAYERS  BACK+R3 (F3): LANDING PAUSE GUESS/ANIMATION");
            TEXT(20, 112, "BACK+UP (F4): OPPONENT STILL / AI PRESETS  BACK+DOWN (F6): AI VS AI (WATCH)");
            if (g_stage.ok) TEXT(20, 136, g_test_boundary ? "BACK+RIGHT (F8): TEST BOUNDARY ON (1,500 PAST THE STARTS)"
                                          : g_stage.own_bounds ? "BACK+RIGHT (F8): TEST BOUNDARY OFF (THE STAGE'S OWN AREA)"
                                                               : "BACK+RIGHT (F8): TEST BOUNDARY OFF (NO OUT OF AREA HERE)");
            TEXT(20, 124, g_stage.ok ? "BACK+LEFT (F7): GREEN SILO / OCEAN PLATFORM / MISSION MAPS 11, 6 / OPEN FLOOR"
                                     : "BACK+LEFT (F7): ARENA STAGE (RE-IMPORT THE DISC FOR STAGES)");
        }
        }
        /* Whole-screen HUD: the split line and the result. */
        const float full_w = acr_render_hud(0, 0, pw, ph);
        if (two_players || g_ai_mode) {
            if (two_players && !g_net) acr_hud_rect(full_w / 2.0f - 1, 0, 2, 480, 0, 0, 0, 255, 1);
        }
        if (!two_players && g_ai_mode) {
            char who[64];
            if (g_rogue_fight) SDL_snprintf(who, sizeof who, "%s", menu_fight()->name[1]);
            else SDL_snprintf(who, sizeof who, "%sOPPONENT: AI %s", g_autopilot ? "AI VS AI  " : "",
                              g_ai_mode == AI_FROM_FILE ? "FROM FILE" : acr_gen1_ai_preset_name(g_ai_mode));
            battle_hud_text(full_w, g_hud_frame, 0, 160.0f - 2.0f * (float)SDL_strlen(who), 42, who, 0, 255, 200, 120);
        }
        if (g_rogue_fight == 3 && !round_over && SDL_GetTicks() < g_radio_until)   /* a mission's radio */
            battle_hud_text(full_w, g_hud_frame, 0, 160.0f - 2.0f * (float)SDL_strlen(g_radio), 196, g_radio, 0, 140, 230, 255);
        if (g_rogue_fight && !round_over && SDL_GetTicks() < give_up_until)
            battle_hud_text(full_w, g_hud_frame, 0, 160.0f - 2.0f * 29.0f, 180, "ESC AGAIN: GIVE UP (A LOSS)", 0, 255, 90, 70);
        if (round_over) {
            /* The result itself is in each player's view; here, how it ended. */
            const int time_up = round_ms <= 0 && (two_players || g_ai_mode);
            int out = 0;
            for (int j = 0; j < acr_gen1_battle_fighters(&battle); ++j) out |= acr_gen1_battle_out_of_area(&battle, j) == 2;
            /* A map's script decides by its objective: the game's own words. */
            const char *why = battle.mission.result == ACR_GEN1_MISSION_FAILED ? "MISSION FAILED"
                            : battle.mission.result == ACR_GEN1_MISSION_DONE   ? "MISSION ACCOMPLISHED"
                            : out ? "OUT OF AREA" : "KNOCKED OUT";
            if (time_up) battle_hud_word(full_w, g_hud_frame, BATTLE_HUD_TIME_UP, 140, 255, 230, 120);
            else battle_hud_text(full_w, g_hud_frame, 0, 160.0f - 3.0f * (float)SDL_strlen(why), 140, why, 1, 255, 230, 120);
            {
                char next[48];
                if (g_net) SDL_snprintf(next, sizeof next, "ROUND %d  %d - %d", (int)net_rb.cur.round, (int)net_rb.cur.wins[0],
                                        (int)net_rb.cur.wins[1]);
                else SDL_snprintf(next, sizeof next, g_rogue_fight ? "ENTER TO GO ON" : "START TO RESET");
                battle_hud_text(full_w, g_hud_frame, 0, 160.0f - 2.0f * (float)SDL_strlen(next), 152 + (time_up ? 54 : 0), next, 0,
                                255, 230, 120);
            }
        }
        if (g_net) {
            /* Online: who is who, and how the connection is doing. */
            char line[160];
            SDL_snprintf(line, sizeof line, "ONLINE  P%d %s  PING %d MS  DELAY %d  FIXES %u (DEEPEST %u)",
                         net_info.local + 1, g_net_host ? "HOST" : "GUEST", net_link_ping_ms(net), net_info.delay,
                         net_rb.rollbacks, net_rb.deepest);
            if (show_help) battle_hud_text(full_w, g_hud_frame, 0, 4, 4, line, 0, 160, 200, 255);
            const char *alert = NULL;
            if (net_lost == 2) alert = "THE OTHER PLAYER LEFT  (ESC: QUIT)";
            else if (net_lost) alert = "CONNECTION LOST  (ESC: QUIT)";
            else if (acr_rb_desync(&net_rb)) alert = "OUT OF STEP WITH THE OTHER PC (DESYNC)";
            else if (net_link_quiet_ms(net, SDL_GetTicks()) > 1000 || (net_wait_since && SDL_GetTicks() - net_wait_since > 330))
                alert = "WAITING FOR THE OTHER PLAYER...";
            if (alert) battle_hud_text(full_w, g_hud_frame, 0, 160.0f - 2.0f * (float)SDL_strlen(alert), 100, alert, 0, 255, 90, 70);
            if (acr_rb_desync(&net_rb)) {
                SDL_snprintf(line, sizeof line, "FIRST DIFFERENT AT STEP %u", acr_rb_desync(&net_rb) - 1);
                battle_hud_text(full_w, g_hud_frame, 0, 160.0f - 2.0f * (float)SDL_strlen(line), 110, line, 0, 255, 90, 70);
            }
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

    if (net && g_net) {
        SDL_Log("netplay: ended at step %u, %u fixes (deepest %u steps), %u checks in step, desync at %u, ping %d ms, "
                "%u waits, %u skips, adv %d/%d",
                net_rb.frame, net_rb.rollbacks, net_rb.deepest, net_rb.checks_ok, acr_rb_desync(&net_rb),
                net_link_ping_ms(net), net_rb.waits, net_skips, net_rb.local_adv256 / 256, net_rb.remote_adv256 / 256);
    }
    net_link_close(net);
    net_finder_close(finder);
    for (int k = 0; k < 2; ++k) if (pads[k]) SDL_CloseGamepad(pads[k]);
    sfx_close();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(block);
    return 0;
}
