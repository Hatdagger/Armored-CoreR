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

#define VIEW_W 640
#define VIEW_H 480
#define ARENA  20000          /* half width, world units */

/* ---------------- input ---------------- */

static SDL_Gamepad *open_first_gamepad(void) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_Gamepad *pad = (ids && count > 0) ? SDL_OpenGamepad(ids[0]) : NULL;
    SDL_free(ids);
    return pad;
}

static acr_gen1_input read_input(SDL_Gamepad *pad) {
    const bool *k = SDL_GetKeyboardState(NULL);
    acr_gen1_input in = { 0, 0, 0, 0 };
    in.move = (int8_t)((k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]));
    in.turn = (int8_t)((k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]));
    in.strafe = (int8_t)(k[SDL_SCANCODE_E] - k[SDL_SCANCODE_Q]);
    in.boost = (uint8_t)(k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J]);
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

/* ---------------- a tiny 3D view ---------------- */

typedef struct vec3 { float x, y, z; } vec3;
typedef struct camera { vec3 pos, right, up, fwd; float focal; } camera;

static vec3 v3(float x, float y, float z) { vec3 v = { x, y, z }; return v; }
static vec3 vsub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static float vdot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/* World (y up) -> camera space; z is depth. */
static vec3 to_cam(const camera *c, vec3 p) {
    const vec3 d = vsub(p, c->pos);
    return v3(vdot(d, c->right), vdot(d, c->up), vdot(d, c->fwd));
}
static SDL_FPoint project(const camera *c, vec3 q) {
    SDL_FPoint s = { VIEW_W * 0.5f + q.x * c->focal / q.z, VIEW_H * 0.55f - q.y * c->focal / q.z };
    return s;
}

static void line3(SDL_Renderer *r, const camera *c, vec3 a, vec3 b) {
    vec3 qa = to_cam(c, a), qb = to_cam(c, b);
    const float near = 50.0f;
    if (qa.z < near && qb.z < near) return;
    if (qa.z < near || qb.z < near) {              /* clip at the near plane */
        const float t = (near - qa.z) / (qb.z - qa.z);
        const vec3 m = v3(qa.x + (qb.x - qa.x) * t, qa.y + (qb.y - qa.y) * t, near);
        if (qa.z < near) qa = m; else qb = m;
    }
    const SDL_FPoint pa = project(c, qa), pb = project(c, qb);
    SDL_RenderLine(r, pa.x, pa.y, pb.x, pb.y);
}

/* Boxes are drawn as sorted faces (painter's order), shaded by facing. */
#define MAX_FACES 512
typedef struct face { SDL_Vertex v[4]; float depth; } face;
static face g_faces[MAX_FACES];
static int g_face_count;

static void add_box(const camera *c, vec3 centre, float yaw, vec3 half, SDL_FColor col) {
    const float s = sinf(yaw), co = cosf(yaw);
    vec3 corner[8];
    for (int i = 0; i < 8; ++i) {
        const float lx = (i & 1) ? half.x : -half.x, ly = (i & 2) ? half.y : -half.y,
                    lz = (i & 4) ? half.z : -half.z;
        /* local x right, z forward -> world (forward = (sin, cos), right = (cos, -sin)) */
        corner[i] = v3(centre.x + lx * co + lz * s, centre.y + ly, centre.z - lx * s + lz * co);
    }
    static const int quads[6][4] = { {0,1,3,2}, {4,6,7,5}, {0,4,5,1}, {2,3,7,6}, {0,2,6,4}, {1,5,7,3} };
    static const float shade[6] = { 0.55f, 0.85f, 0.45f, 1.0f, 0.7f, 0.75f };
    for (int f = 0; f < 6 && g_face_count < MAX_FACES; ++f) {
        face *fc = &g_faces[g_face_count];
        float depth = 0;
        int ok = 1;
        vec3 q[4];
        for (int k = 0; k < 4; ++k) {
            q[k] = to_cam(c, corner[quads[f][k]]);
            if (q[k].z < 50.0f) ok = 0;
            depth += q[k].z;
        }
        if (!ok) continue;
        for (int k = 0; k < 4; ++k) {
            fc->v[k].position = project(c, q[k]);
            fc->v[k].color = (SDL_FColor){ col.r * shade[f], col.g * shade[f], col.b * shade[f], 1.0f };
            fc->v[k].tex_coord = (SDL_FPoint){ 0, 0 };
        }
        fc->depth = depth;
        ++g_face_count;
    }
}

static int face_cmp(const void *a, const void *b) {
    const float da = ((const face *)a)->depth, db = ((const face *)b)->depth;
    return da < db ? 1 : da > db ? -1 : 0;
}

static void flush_faces(SDL_Renderer *r) {
    qsort(g_faces, (size_t)g_face_count, sizeof g_faces[0], face_cmp);
    static SDL_Vertex verts[MAX_FACES * 6];
    int n = 0;
    for (int i = 0; i < g_face_count; ++i) {
        const SDL_Vertex *v = g_faces[i].v;
        verts[n++] = v[0]; verts[n++] = v[1]; verts[n++] = v[2];
        verts[n++] = v[0]; verts[n++] = v[2]; verts[n++] = v[3];
    }
    if (n) SDL_RenderGeometry(r, NULL, verts, n, NULL, 0);
    g_face_count = 0;
}

/* Stand-in AC, about 700 units tall, facing +z locally. */
static void add_ac(const camera *c, vec3 p, float yaw, float crouch) {
    const SDL_FColor body = { 0.62f, 0.66f, 0.70f, 1 }, trim = { 0.95f, 0.62f, 0.15f, 1 },
                     dark = { 0.30f, 0.33f, 0.36f, 1 };
    const float s = sinf(yaw), co = cosf(yaw);
    #define AT(lx, ly, lz) v3(p.x + (lx) * co + (lz) * s, p.y + (ly), p.z - (lx) * s + (lz) * co)
    const float leg_h = 340.0f - crouch;
    add_box(c, AT(-120, leg_h * 0.5f, 0), yaw, v3(55, leg_h * 0.5f, 70), dark);
    add_box(c, AT(120, leg_h * 0.5f, 0), yaw, v3(55, leg_h * 0.5f, 70), dark);
    add_box(c, AT(0, leg_h + 140, 0), yaw, v3(200, 140, 150), body);           /* core */
    add_box(c, AT(0, leg_h + 330, 40), yaw, v3(75, 50, 80), trim);             /* head */
    add_box(c, AT(-270, leg_h + 170, 30), yaw, v3(55, 110, 70), body);         /* arms */
    add_box(c, AT(270, leg_h + 170, 30), yaw, v3(55, 110, 70), body);
    add_box(c, AT(270, leg_h + 90, 190), yaw, v3(30, 30, 140), dark);          /* rifle */
    add_box(c, AT(-110, leg_h + 300, -150), yaw, v3(60, 70, 60), trim);        /* back units */
    add_box(c, AT(110, leg_h + 300, -150), yaw, v3(60, 70, 60), trim);
    #undef AT
}

/* ---------------- main ---------------- */

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
        if (slot >= 0 && i + 1 < argc) parts[slot] = atoi(argv[++i]);
        else if (argv[i][0] != '-') *dir = argv[i];
    }
    return 1;
}

static int is_disc(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot && (!SDL_strcasecmp(dot, ".cue") || !SDL_strcasecmp(dot, ".bin") || !SDL_strcasecmp(dot, ".iso"));
}

/* Shows a message (several lines) centred, then presents. */
static void show_message(SDL_Renderer *r, const char *text) {
    SDL_SetRenderDrawColor(r, 18, 22, 28, 255);
    SDL_RenderClear(r);
    SDL_SetRenderDrawColor(r, 220, 230, 235, 255);
    float y = 60;
    for (const char *p = text; *p; ) {
        const char *nl = strchr(p, '\n');
        const size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char line[96];
        SDL_snprintf(line, sizeof line, "%.*s", (int)(n < 95 ? n : 95), p);
        SDL_RenderDebugText(r, 24, y, line);
        y += 14;
        p += n;
        if (*p) ++p;
    }
    SDL_RenderPresent(r);
}

/* Imports a disc into dir, showing progress text. */
static int import_disc(SDL_Renderer *r, const char *disc, const char *dir, char *err, int err_size) {
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

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer("Armored-CoreR test build", 1280, 960, SDL_WINDOW_RESIZABLE,
                                     &window, &renderer)) {
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderLogicalPresentation(renderer, VIEW_W, VIEW_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Gamepad *pad = open_first_gamepad();

    /* A disc given on the command line (or dropped on the program) is
     * imported next to the program first. */
    static demo_ac ac_data;      /* large: keep it off the stack */
    char err[1024] = "";
    if (is_disc(dir)) {
        const char *disc = dir;
        dir = default_dir;
        import_disc(renderer, disc, dir, err, sizeof err);
    }
    /* No usable import yet: ask for the disc, which can be dropped on the window. */
    int loaded = err[0] ? 0 : demo_load(dir, parts, &ac_data, err, sizeof err);
    while (!loaded) {
        char text[1600];
        SDL_snprintf(text, sizeof text,
                     "ARMORED-CORER TEST BUILD\n\n"
                     "DROP YOUR MASTER OF ARENA DISC (.CUE OR .BIN)\n"
                     "ON THIS WINDOW TO IMPORT IT.\n\n"
                     "ESC / BACK: QUIT\n\n%s", err);
        show_message(renderer, text);
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
            if (import_disc(renderer, e.drop.data, dir, err, sizeof err))
                loaded = demo_load(dir, parts, &ac_data, err, sizeof err);
        }
    }
    const acr_gen1_ac_def *def = &ac_data.def;

    acr_clock clock;
    acr_clock_init_ns(&clock, ACR_GEN1_TICK_NS, 8);   /* Gen 1: 22 steps a second */
    acr_gen1_ac ac, prev;
    acr_gen1_ac_spawn(&ac, def, 0, 0, 0);
    prev = ac;

    Uint64 last = SDL_GetTicksNS();
    const Uint64 start_ns = last;
    int running = 1, show_help = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
                SDL_CloseGamepad(pad);
                pad = NULL;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F1) show_help = !show_help;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_R) {
                acr_gen1_ac_spawn(&ac, def, 0, 0, 0); prev = ac;
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) show_help = !show_help;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) { acr_gen1_ac_spawn(&ac, def, 0, 0, 0); prev = ac; }
            }
        }
        acr_gen1_input in = read_input(pad);
        /* Testing aid: ACR_HOLD="move,turn,strafe,boost" (e.g. "1,0,0,1") holds
         * those inputs; ACR_SHOT=file.bmp ACR_SHOT_MS=2000 saves a picture and quits. */
        const char *hold = SDL_getenv("ACR_HOLD");
        if (hold) {
            int v[4] = { 0, 0, 0, 0 };
            SDL_sscanf(hold, "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]);
            in.move = (int8_t)v[0]; in.turn = (int8_t)v[1]; in.strafe = (int8_t)v[2]; in.boost = (uint8_t)v[3];
        }

        const Uint64 now = SDL_GetTicksNS();
        const uint32_t ticks = acr_clock_advance(&clock, now - last);
        last = now;
        for (uint32_t i = 0; i < ticks; ++i) {
            prev = ac;
            acr_gen1_ac_step(&ac, def, in);
            /* arena walls */
            const int32_t lim = ARENA * 64;
            if (ac.x > lim) { ac.x = lim; ac.vx = 0; }
            if (ac.x < -lim) { ac.x = -lim; ac.vx = 0; }
            if (ac.z > lim) { ac.z = lim; ac.vz = 0; }
            if (ac.z < -lim) { ac.z = -lim; ac.vz = 0; }
        }

        /* Interpolate between the last two steps. */
        const float a = (float)acr_clock_alpha(&clock) / (float)ACR_FX_ONE;
        const float px = (prev.x + (ac.x - prev.x) * a) / 64.0f;
        const float py = -(prev.y + (ac.y - prev.y) * a) / 64.0f;
        const float pz = (prev.z + (ac.z - prev.z) * a) / 64.0f;
        int dyaw = (int)ac.yaw - (int)prev.yaw;
        if (dyaw > 2048) dyaw -= 4096;
        if (dyaw < -2048) dyaw += 4096;
        const float yaw = ((float)prev.yaw + dyaw * a) * (6.2831853f / 4096.0f);

        camera cam;
        cam.fwd = v3(sinf(yaw), -0.22f, cosf(yaw));
        const float fl = sqrtf(vdot(cam.fwd, cam.fwd));
        cam.fwd = v3(cam.fwd.x / fl, cam.fwd.y / fl, cam.fwd.z / fl);
        cam.right = v3(cosf(yaw), 0, -sinf(yaw));
        cam.up = v3(cam.fwd.y * cam.right.z - cam.fwd.z * cam.right.y,
                    cam.fwd.z * cam.right.x - cam.fwd.x * cam.right.z,
                    cam.fwd.x * cam.right.y - cam.fwd.y * cam.right.x);
        if (cam.up.y < 0) cam.up = v3(-cam.up.x, -cam.up.y, -cam.up.z);
        cam.pos = v3(px - sinf(yaw) * 2200.0f, py + 1200.0f, pz - cosf(yaw) * 2200.0f);
        cam.focal = VIEW_H * 0.9f;

        SDL_SetRenderDrawColor(renderer, 18, 22, 28, 255);
        SDL_RenderClear(renderer);
        /* floor grid and walls */
        SDL_SetRenderDrawColor(renderer, 52, 70, 76, 255);
        for (int g = -ARENA; g <= ARENA; g += 2000) {
            line3(renderer, &cam, v3((float)g, 0, (float)-ARENA), v3((float)g, 0, (float)ARENA));
            line3(renderer, &cam, v3((float)-ARENA, 0, (float)g), v3((float)ARENA, 0, (float)g));
        }
        SDL_SetRenderDrawColor(renderer, 120, 150, 160, 255);
        for (int w = 0; w < 4; ++w) {
            const float s0 = (w & 1) ? ARENA : -ARENA;
            for (int h = 0; h <= 3000; h += 1000) {
                if (w < 2) line3(renderer, &cam, v3(s0, (float)h, -ARENA), v3(s0, (float)h, ARENA));
                else line3(renderer, &cam, v3(-ARENA, (float)h, s0), v3(ARENA, (float)h, s0));
            }
        }
        /* pillars for a sense of speed, the AC's shadow, then the AC */
        for (int i = -2; i <= 2; ++i)
            for (int j = -2; j <= 2; ++j)
                if (i || j) add_box(&cam, v3(i * 7000.0f, 1500, j * 7000.0f), 0, v3(300, 1500, 300),
                                    (SDL_FColor){ 0.35f, 0.42f, 0.48f, 1 });
        float crouch = 0;
        if (ac.state == ACR_GEN1_CROUCH) crouch = 90;
        if (ac.state == ACR_GEN1_LANDING) crouch = 70;
        add_ac(&cam, v3(px, py, pz), yaw, crouch);
        flush_faces(renderer);
        if (py > 1.0f) {                                   /* height marker */
            SDL_SetRenderDrawColor(renderer, 230, 160, 40, 255);
            line3(renderer, &cam, v3(px, 0, pz), v3(px, py, pz));
        }

        /* HUD */
        const acr_gen1_stats *st = &def->stats;
        const float e_frac = (float)ac.energy / (float)(st->energy_capacity ? st->energy_capacity : 1);
        const float red = (float)st->red_zone / (float)(st->energy_capacity ? st->energy_capacity : 1);
        SDL_FRect bar = { 20, VIEW_H - 30, 300, 10 };
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderFillRect(renderer, &bar);
        bar.w = 300 * e_frac;
        if (ac.drained) SDL_SetRenderDrawColor(renderer, 200, 50, 40, 255);
        else SDL_SetRenderDrawColor(renderer, 90, 220, 140, 255);
        SDL_RenderFillRect(renderer, &bar);
        SDL_SetRenderDrawColor(renderer, 230, 60, 50, 255);
        SDL_RenderLine(renderer, 20 + 300 * red, VIEW_H - 33, 20 + 300 * red, VIEW_H - 17);

        static const char *const k_state[] = { "GROUND", "CROUCH", "AIR", "LANDING" };
        const int dx = (ac.x - prev.x) / 64, dz = (ac.z - prev.z) / 64, dy = (ac.y - prev.y) / 64;
        char line[160];
        SDL_SetRenderDrawColor(renderer, 220, 230, 235, 255);
        SDL_snprintf(line, sizeof line, "ENERGY %5d  %-7s  SPEED %3d  RISE %4d", (int)ac.energy,
                     k_state[ac.state], (int)sqrtf((float)(dx * dx + dz * dz)), -dy);
        SDL_RenderDebugText(renderer, 20, VIEW_H - 48, line);
        SDL_snprintf(line, sizeof line, "LEGS %s  BOOSTER %s  SET %d", ac_data.names[SLOT_LEGS],
                     ac_data.names[SLOT_BOOSTER], ac_data.model);
        SDL_RenderDebugText(renderer, 20, 12, line);
        if (show_help) {
            SDL_RenderDebugText(renderer, 20, 28, "MOVE: STICK/DPAD UP-DOWN  TURN: LEFT-RIGHT");
            SDL_RenderDebugText(renderer, 20, 40, "STRAFE: LB/RB  BOOST: A OR RT (NO DIRECTION = RISE)");
            SDL_RenderDebugText(renderer, 20, 52, "START/R: RESET  BACK/F1: HIDE HELP  ESC: QUIT");
        }
        const char *shot = SDL_getenv("ACR_SHOT");
        if (shot && (now - start_ns) / 1000000 >= (Uint64)SDL_atoi(SDL_getenv("ACR_SHOT_MS") ? SDL_getenv("ACR_SHOT_MS") : "2000")) {
            SDL_Surface *img = SDL_RenderReadPixels(renderer, NULL);
            if (img) { SDL_SaveBMP(img, shot); SDL_DestroySurface(img); }
            running = 0;
        }
        SDL_RenderPresent(renderer);
    }

    if (pad) SDL_CloseGamepad(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(block);
    return 0;
}
