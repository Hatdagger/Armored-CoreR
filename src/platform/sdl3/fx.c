/* Weapon effects (fx.h). The pictures are Master of Arena's own (RTIM 61,
 * docs/research/gen1-effects.md); how they are timed, sized and combined
 * here is matched by eye to the original for now, not mapped from its code
 * yet. Sizes are world units (an AC stands about 212 tall), times ms. */
#include "fx.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "../../model/acr_png.h"
#include "../../render/acr_render.h"

/* ---- the pictures: fx-NN.png, NN their place in RTIM 61 ---- */

enum {
    SH_FLASH,      /* 00: hot glow, 4 frames growing: muzzle and hit flash */
    SH_BALL_GROW,  /* 03: fireball, 4 frames growing */
    SH_BALL_FADE,  /* 04: fireball breaking up, 4 frames */
    SH_DEBRIS,     /* 05: smoke and debris thinning out, 4 frames */
    SH_FIRE,       /* 06: flames, 4 frames (loop) */
    SH_SPARKS,     /* 08: spark spray, 2 frames */
    SH_FLARE,      /* 11: lens flare */
    SH_SMOKE,      /* 12: smoke cloud */
    SH_SHOTS,      /* 15: shot sprites (grey, tinted when drawn) */
    SH_GLOW,       /* 24: round glow (energy) */
    SH_COUNT
};
static const uint8_t k_sheet_file[SH_COUNT] = { 0, 3, 4, 5, 6, 8, 11, 12, 15, 24 };
static uint32_t g_tex[SH_COUNT];
static int g_tex_w[SH_COUNT], g_tex_h[SH_COUNT];

/* Pieces of the shot sheet (fx-15, 256 x 64), in texels. */
typedef struct { float u, v, w, h; } rect;
static const rect k_tracer = { 0, 0, 32, 64 };      /* glowing capsule, long axis down */
static const rect k_head = { 32, 0, 16, 36 };       /* small bright blob */
static const rect k_plume = { 128, 0, 32, 62 };     /* rocket flame, nozzle at the top */
static const rect k_beam = { 160, 0, 32, 64 };      /* wide capsule: energy shots */

/* Glows are added to the screen; their faint dithered halo (a few levels
 * of red at the cell's edges) only shows as a square on dark backgrounds,
 * so those texels are dropped. Smoke and debris keep every texel. */
static int g_trim_dark;

/* How a weapon's shots look, by its type (the HUD name). Rifles fire a
 * round-fronted blob with a narrower tail; machine guns a thin streak.
 * The game picks these per weapon from tables not mapped yet (the part's
 * model_id); these are matched by eye until they are. */
enum { LOOK_STREAK, LOOK_BLOB, LOOK_ENERGY, LOOK_ROCKET };
typedef struct look {
    const char *type;
    uint8_t look, r, g, b;
    float width, length;
} look;
static const look k_looks[] = {
    { "RIFLE",            LOOK_BLOB,   200, 130, 255, 46, 80 },
    { "SNIPER RIFLE",     LOOK_BLOB,   210, 150, 255, 30, 90 },
    { "HAND GUN",         LOOK_BLOB,   210, 140, 255, 40, 60 },
    { "MACHINE GUN",      LOOK_STREAK, 255, 205, 120, 16, 0 },
    { "CHAIN GUN",        LOOK_STREAK, 255, 205, 120, 20, 0 },
    { "BAZOOKA",          LOOK_ROCKET, 255, 170,  90, 30, 70 },
    { "ROCKET",           LOOK_ROCKET, 255, 170,  90, 30, 70 },
    { "SMALL ROCKET",     LOOK_ROCKET, 255, 170,  90, 26, 60 },
    { "LARGE ROCKET",     LOOK_ROCKET, 255, 170,  90, 38, 90 },
    { "SPECIAL ROCKET",   LOOK_ROCKET, 255, 170,  90, 30, 70 },
    { "GRENADE LAUNCHER", LOOK_ROCKET, 255, 170,  90, 34, 60 },
};
static const look k_look_shell = { "", LOOK_STREAK, 255, 205, 120, 16, 0 };
static const look k_look_energy = { "", LOOK_ENERGY, 120, 190, 255, 26, 0 };

static const char (*g_types[ACR_GEN1_MAX_FIGHTERS])[24];
static int g_type_count[ACR_GEN1_MAX_FIGHTERS];

void fx_set_weapons(int fighter, const char (*types)[24], int count) {
    if (fighter < 0 || fighter >= ACR_GEN1_MAX_FIGHTERS) return;
    g_types[fighter] = types;
    g_type_count[fighter] = count;
}

static const look *look_of(int owner, int slot, int energy) {
    if (energy) return &k_look_energy;
    if (owner >= 0 && owner < ACR_GEN1_MAX_FIGHTERS && g_types[owner] && slot < g_type_count[owner])
        for (size_t i = 0; i < sizeof k_looks / sizeof k_looks[0]; ++i)
            if (!SDL_strcmp(k_looks[i].type, g_types[owner][slot])) return &k_looks[i];
    return &k_look_shell;
}

static uint32_t load_png(const char *path, int *w, int *h) {
    size_t size = 0;
    uint8_t *data = SDL_LoadFile(path, &size);
    if (!data) return 0;
    acr_image img;
    const int ok = acr_png_decode(data, size, &img);
    SDL_free(data);
    if (!ok) return 0;
    uint32_t tex = 0;
    uint8_t *rgba = SDL_malloc((size_t)img.w * img.h * 4);
    if (rgba) {
        if (img.indexed) for (int i = 0; i < img.w * img.h; ++i) SDL_memcpy(rgba + 4 * i, img.palette[img.pixels[i]], 4);
        else SDL_memcpy(rgba, img.pixels, (size_t)img.w * img.h * 4);
        if (g_trim_dark)
            for (int i = 0; i < img.w * img.h; ++i) {
                const uint8_t *c = rgba + 4 * i;
                const int m = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
                if (m < 72) rgba[4 * i + 3] = 0;
            }
        tex = acr_render_texture(rgba, img.w, img.h, 1);
        *w = img.w;
        *h = img.h;
        SDL_free(rgba);
    }
    acr_image_free(&img);
    return tex;
}

int fx_load(const char *import_dir) {
    int found = 1;
    for (int i = 0; i < SH_COUNT; ++i) {
        if (g_tex[i]) continue;
        char path[1024];
        SDL_snprintf(path, sizeof path, "%s/fx/fx-%02u.png", import_dir, (unsigned)k_sheet_file[i]);
        g_trim_dark = i != SH_DEBRIS && i != SH_SMOKE;
        g_tex[i] = load_png(path, &g_tex_w[i], &g_tex_h[i]);
        if (!g_tex[i]) { g_tex_w[i] = g_tex_h[i] = 1; found = 0; }
    }
    return found;
}

/* ---- effects in flight ---- */

enum { B_ADD, B_SEMI };

typedef struct part {
    uint8_t on, sheet, frames, blend, ground;   /* ground: the picture stands on p (fire) */
    uint8_t r, g, b, a;
    float p[3], v[3];
    float age, life;      /* age < 0: waiting to start */
    float s0, s1;         /* size at the start and the end */
    float loop_ms;        /* > 0: frames cycle this fast instead of playing once */
    float spin;           /* turn of the picture (smoke, fireballs) */
    float rise;           /* units a ms upward (smoke) */
} part;

#define FX_PARTS 1536   /* missiles' white trails are many small dots */
static part g_part[FX_PARTS];
static int g_next;

static struct {
    uint8_t on, missile, energy, owner;
    const look *look;
    int32_t travelled, range, speed;
    uint16_t age;
    float p[3], step[3];       /* where it is, and how far it goes a step */
    float trail[3];            /* where the missile's last smoke puff went */
} g_seen[ACR_GEN1_MAX_SHOTS];
static int32_t g_ap[ACR_GEN1_MAX_FIGHTERS];
static uint8_t g_bhit[ACR_GEN1_MAX_FIGHTERS];   /* last blade_hit_seq seen */
static int g_ap_ok;
static fx_rig g_rig[ACR_GEN1_MAX_FIGHTERS];
static uint8_t g_rig_ok[ACR_GEN1_MAX_FIGHTERS];
static uint8_t g_boosting[ACR_GEN1_MAX_FIGHTERS];
static float g_boost_ms[ACR_GEN1_MAX_FIGHTERS];   /* how long the boost has burned (ignition flare) */
/* The blade's slash trail: the beam's last few spots, newest first. */
#define FX_TRAIL 10
static struct { int n; float t[FX_TRAIL]; float base[FX_TRAIL][3], end[FX_TRAIL][3]; } g_trail[ACR_GEN1_MAX_FIGHTERS];
static uint8_t g_unit_down[ACR_GEN1_MAX_UNITS];   /* each unit's down counter last seen */
static uint64_t g_last_ns;
static float g_now_ms;          /* a running clock for flicker */
static uint32_t g_rng = 0x9E3779B9u;

static float rnd(void) {          /* 0..1, presentation only (not the fight's) */
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return (float)(g_rng >> 8) / 16777216.0f;
}

static part *spawn(int sheet, int frames, int blend, const float p[3], float life, float s0, float s1,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    part *q = NULL;
    for (int i = 0; i < FX_PARTS && !q; ++i) {
        part *c = &g_part[(g_next + i) % FX_PARTS];
        if (!c->on) { q = c; g_next = (g_next + i + 1) % FX_PARTS; }
    }
    if (!q) { q = &g_part[g_next]; g_next = (g_next + 1) % FX_PARTS; }   /* full: reuse the oldest slot */
    memset(q, 0, sizeof *q);
    q->on = 1; q->sheet = (uint8_t)sheet; q->frames = (uint8_t)frames; q->blend = (uint8_t)blend;
    memcpy(q->p, p, sizeof q->p);
    q->life = life; q->s0 = s0; q->s1 = s1;
    q->r = r; q->g = g; q->b = b; q->a = a;
    return q;
}

void fx_set_rig(int fighter, const fx_rig *rig) {
    if (fighter < 0 || fighter >= ACR_GEN1_MAX_FIGHTERS || !rig) return;
    g_rig[fighter] = *rig;
    g_rig_ok[fighter] = 1;
}

static struct { int lit, out, have, at; float lat; } g_slash[ACR_GEN1_MAX_FIGHTERS];

void fx_reset(void) {
    for (int k = 0; k < ACR_GEN1_MAX_FIGHTERS; ++k) { g_slash[k].lit = g_slash[k].out = -1; g_slash[k].have = 0; }
    memset(g_part, 0, sizeof g_part);
    memset(g_boosting, 0, sizeof g_boosting);
    memset(g_trail, 0, sizeof g_trail);
    memset(g_rig_ok, 0, sizeof g_rig_ok);
    memset(g_seen, 0, sizeof g_seen);
    memset(g_unit_down, 0, sizeof g_unit_down);
    g_ap_ok = 0;
}

/* ---- what each event looks like ---- */

static void muzzle(const float p[3], int energy, int missile) {
    if (missile) {
        spawn(SH_FLASH, 4, B_ADD, p, 120, 45, 90, 255, 200, 140, 255);
        part *s = spawn(SH_SMOKE, 1, B_SEMI, p, 700, 40, 110, 170, 170, 170, 120);
        s->spin = rnd() * 6.283f; s->rise = 0.04f;
    } else if (energy) {
        spawn(SH_GLOW, 1, B_ADD, p, 110, 80, 40, 140, 200, 255, 255);
    } else {
        part *f = spawn(SH_FLASH, 4, B_ADD, p, 70, 30, 45, 255, 225, 170, 255);
        f->spin = rnd() * 6.283f;
    }
}

static void hit_ac(const float p[3], int energy) {
    if (energy) {
        spawn(SH_GLOW, 1, B_ADD, p, 200, 50, 150, 130, 190, 255, 255);
        spawn(SH_FLARE, 1, B_ADD, p, 140, 140, 90, 150, 190, 255, 200);
    } else {
        part *s = spawn(SH_SPARKS, 2, B_ADD, p, 120, 90, 110, 255, 235, 190, 255);
        s->spin = rnd() * 6.283f;
        spawn(SH_FLASH, 4, B_ADD, p, 90, 50, 80, 255, 220, 160, 255);
    }
}

static void hit_wall(const float p[3], int energy) {
    if (energy) {
        spawn(SH_GLOW, 1, B_ADD, p, 140, 30, 90, 130, 190, 255, 220);
        return;
    }
    part *s = spawn(SH_SPARKS, 2, B_ADD, p, 100, 50, 60, 255, 220, 160, 255);
    s->spin = rnd() * 6.283f;
    part *d = spawn(SH_SMOKE, 1, B_SEMI, p, 380, 20, 60, 150, 145, 135, 110);
    d->spin = rnd() * 6.283f; d->rise = 0.03f;
}

/* A fireball: grow, break up, thin out to smoke. `scale` 1 = a missile. */
static void explosion(const float p[3], float scale, float delay) {
    part *q = spawn(SH_BALL_GROW, 4, B_ADD, p, 110, 90 * scale, 150 * scale, 255, 255, 255, 255);
    q->age = -delay; q->spin = rnd() * 6.283f;
    q = spawn(SH_BALL_FADE, 4, B_ADD, p, 240, 160 * scale, 190 * scale, 255, 255, 255, 255);
    q->age = -delay - 100; q->spin = rnd() * 6.283f;
    q = spawn(SH_DEBRIS, 4, B_SEMI, p, 450, 170 * scale, 200 * scale, 255, 255, 255, 220);
    q->age = -delay - 260; q->spin = rnd() * 6.283f; q->rise = 0.02f;
    q = spawn(SH_SMOKE, 1, B_SEMI, p, 1100, 80 * scale, 220 * scale, 120, 115, 110, 100);
    q->age = -delay - 200; q->spin = rnd() * 6.283f; q->rise = 0.05f;
}

/* An AC's AP reaching zero: a flare, a chain of blasts over its body, then
 * flames where it stands. */
static void ac_blast(const float feet[3]) {
    float c[3] = { feet[0], feet[1] + 110, feet[2] };
    spawn(SH_FLARE, 1, B_ADD, c, 380, 300, 700, 255, 230, 200, 255);
    for (int i = 0; i < 7; ++i) {
        const float a = rnd() * 6.283f, r = rnd() * 90;
        const float q[3] = { feet[0] + cosf(a) * r, feet[1] + 30 + rnd() * 180, feet[2] + sinf(a) * r };
        explosion(q, 1.6f + rnd() * 0.9f, i * 140.0f + rnd() * 80);
    }
    for (int i = 0; i < 2; ++i) {
        const float q[3] = { feet[0] + (rnd() - 0.5f) * 80, feet[1], feet[2] + (rnd() - 0.5f) * 80 };
        part *f = spawn(SH_FIRE, 4, B_ADD, q, 3200, 200, 160, 255, 255, 255, 255);
        f->age = -500 - i * 150.0f; f->loop_ms = 70; f->ground = 1;
    }
}


/* ---- boosting and the blade (matched by eye, like the rest) ---- */

/* Burning the booster this step: the button (or the modern toggle with a
 * direction, or a quick-boost burst) with energy to spend. */
static int boosting(const acr_gen1_fighter *f) {
    const acr_gen1_ac *ac = &f->ac;
    if (f->ap <= 0) return 0;
    if (ac->quick_ticks) return 1;
    if (ac->drained || ac->energy <= 0 || ac->state == ACR_GEN1_LANDING || f->used.crouch) return 0;
    return f->used.boost || (f->used.cruise && (f->used.move || f->used.strafe));
}

/* The rig as drawn, or a stand-in spot from the AC's place and facing. */
static void rig_of(const acr_gen1_battle *b, int k, fx_rig *r) {
    if (g_rig_ok[k]) { *r = g_rig[k]; return; }
    const acr_gen1_ac *ac = &b->f[k].ac;
    const float yaw = (float)ac->syaw * (6.2831853f / 4096.0f), fx = sinf(yaw), fz = cosf(yaw);
    const float rx = cosf(yaw), rz = -sinf(yaw);
    const float x = (float)ac->sx / 64.0f, y = -(float)ac->sy / 64.0f, z = (float)ac->sz / 64.0f;
    memset(r, 0, sizeof *r);
    r->nozzles = 1;
    r->nozzle[0][0] = x - fx * 45; r->nozzle[0][1] = y + 140; r->nozzle[0][2] = z - fz * 45;
    r->back[0] = -fx; r->back[2] = -fz;
    r->blade_ok = 1;
    r->blade_root[0] = x - rx * 55; r->blade_root[1] = y + 150; r->blade_root[2] = z - rz * 55;
    r->blade_tip[0] = x - rx * 55 + fx * 70; r->blade_tip[1] = y + 110; r->blade_tip[2] = z - rz * 55 + fz * 70;
}

/* A boost lighting: a hot flash at each nozzle, and dust off the floor
 * when it starts on the ground. */
static void boost_ignite(const fx_rig *r, int on_ground, const float feet[3]) {
    for (int n = 0; n < r->nozzles; ++n) {
        part *f = spawn(SH_FLASH, 4, B_ADD, r->nozzle[n], 120, 30, 75, 255, 215, 150, 255);
        f->spin = rnd() * 6.283f;
    }
    if (on_ground)
        for (int i = 0; i < 2; ++i) {
            const float q[3] = { feet[0] + r->back[0] * (40 + i * 40) + (rnd() - 0.5f) * 30, feet[1] + 15,
                                 feet[2] + r->back[2] * (40 + i * 40) + (rnd() - 0.5f) * 30 };
            part *d = spawn(SH_SMOKE, 1, B_SEMI, q, 520, 40, 120, 165, 160, 150, 90);
            d->spin = rnd() * 6.283f; d->rise = 0.03f;
        }
}

/* The beam follows the arm (user, 2026-11-27), measured on the posed
 * model, shoulder to hand: it lights on the slash itself, once the arm
 * points more ahead than up or down and is sweeping left (10+ across in
 * 30 ms), not during
 * the wind-up with the arm raised; it goes out once the arm drops after the
 * slash (up < -40), fading over 150 ms. The air slash is shorter (16 ticks
 * against 26) and is followed the same way. lit / out: the swing's ms it
 * lit / started to go out at, -1 not yet; lat: the hand's last spot across
 * the AC (+ right). */
/* (g_slash: declared above fx_reset) */

/* How strongly the blade's beam shows `ms` into a swing, 0..1: lights over
 * 60 ms from `lit`, goes out over 150 ms from `out`, gone by 1,150 ms. */
static float blade_glow_from(int ms, int lit, int out) {
    if (ms <= 0 || lit < 0 || ms < lit || ms >= 1150) return 0.0f;
    float g = ms - lit < 60 ? (float)(ms - lit) / 60.0f : 1.0f;
    if (out >= 0) { const float f = 1.0f - (float)(ms - out) / 150.0f; g *= f > 0 ? f : 0; }
    if (ms > 900) g *= (float)(1150 - ms) / 250.0f;
    return g;
}
static float blade_glow_k(int k, int ms) { return blade_glow_from(ms, g_slash[k].lit, g_slash[k].out); }

/* The beam's reach as drawn, from the blade's reach in the fight. */
static float blade_len(const acr_gen1_fighter *f) {
    float r = 205.0f;
    for (int k = 0; k < f->weapons; ++k)
        if (f->weapon[k].kind == ACR_GEN1_BLADE) r = (float)f->weapon[k].range;
    r *= 0.75f;
    return r < 120.0f ? 120.0f : r > 280.0f ? 280.0f : r;
}

static int has_blade(const acr_gen1_fighter *f) {
    for (int k = 0; k < f->weapons; ++k)
        if (f->weapon[k].kind == ACR_GEN1_BLADE) return 1;
    return 0;
}

static void blade_line(const acr_gen1_battle *b, int k, const fx_rig *r, float base[3], float end[3]) {
    float d[3] = { r->blade_tip[0] - r->blade_root[0], r->blade_tip[1] - r->blade_root[1], r->blade_tip[2] - r->blade_root[2] };
    const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l > 1e-3f) for (int i = 0; i < 3; ++i) d[i] /= l;
    const float len = blade_len(&b->f[k]);
    for (int i = 0; i < 3; ++i) { base[i] = r->blade_tip[i]; end[i] = r->blade_tip[i] + d[i] * len; }
}

/* ---- watching the fight ---- */

static void shot_pos(const acr_gen1_shot *s, float p[3]) {
    p[0] = (float)s->x / 64.0f; p[1] = -(float)s->y / 64.0f; p[2] = (float)s->z / 64.0f;
}

static void ac_feet(const acr_gen1_battle *b, int k, float p[3]) {
    const acr_gen1_ac *ac = &b->f[k].ac;
    p[0] = (float)ac->sx / 64.0f; p[1] = -(float)ac->sy / 64.0f; p[2] = (float)ac->sz / 64.0f;
}

/* Where a shot that just ended stopped, and on what. */
static void shot_ended(const acr_gen1_battle *b, int k) {
    const float *p = g_seen[k].p, *st = g_seen[k].step;
    /* The shot may have moved one more step before it ended: look along
     * the way it was going for an AC (the hit cylinder: radius 75, 212 tall). */
    for (int t = 0; t < acr_gen1_battle_fighters(b); ++t) {
        if (t == g_seen[k].owner || (g_ap_ok && g_ap[t] <= 0)) continue;
        float feet[3];
        ac_feet(b, t, feet);
        const float c[3] = { feet[0], feet[1] + 106, feet[2] };
        float a[3], d[3];
        for (int i = 0; i < 3; ++i) { a[i] = p[i] - st[i] * 0.5f; d[i] = st[i] * 2.0f; }
        const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        float u = dd > 0 ? ((c[0] - a[0]) * d[0] + (c[1] - a[1]) * d[1] + (c[2] - a[2]) * d[2]) / dd : 0;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
        float q[3];
        for (int i = 0; i < 3; ++i) q[i] = a[i] + d[i] * u;
        const float hx = q[0] - c[0], hz = q[2] - c[2], hy = q[1] - c[1];
        if (hx * hx + hz * hz < 140.0f * 140.0f && hy > -150.0f && hy < 170.0f) {
            /* on the AC's skin facing the shot, not inside it */
            const float l = sqrtf(hx * hx + hz * hz);
            if (l > 60.0f) { q[0] = c[0] + hx * 60.0f / l; q[2] = c[2] + hz * 60.0f / l; }
            if (g_seen[k].missile || (g_seen[k].look && g_seen[k].look->look == LOOK_ROCKET)) explosion(q, 1.0f, 0);
            else hit_ac(q, g_seen[k].energy);
            return;
        }
    }
    const int blast = g_seen[k].missile || (g_seen[k].look && g_seen[k].look->look == LOOK_ROCKET);
    if (blast) { explosion(p, 0.8f, 0); return; }     /* a wall, the floor, shot down */
    if (g_seen[k].travelled + g_seen[k].speed >= g_seen[k].range) return;   /* out of range: just gone */
    float q[3];
    for (int i = 0; i < 3; ++i) q[i] = p[i] + st[i] * 0.5f;
    hit_wall(q, g_seen[k].energy);
}

/* Behind a missile: a thin, clear white line (user, 2026-11-26: not
 * smoke), dots of glow close together that thin out and fade where they
 * are. Rockets keep their smoke puffs. */
static void missile_trail(int k, float dt, int smoke) {
    (void)dt;
    float *t = g_seen[k].trail, *p = g_seen[k].p;
    const float dx = p[0] - t[0], dy = p[1] - t[1], dz = p[2] - t[2];
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    const float gap = smoke ? 45.0f : 20.0f;
    int n = (int)(len / gap);
    if (n > (smoke ? 8 : 24)) n = smoke ? 8 : 24;
    for (int i = 1; i <= n; ++i) {
        const float u = (float)i / (float)(n);
        const float q[3] = { t[0] + dx * u, t[1] + dy * u, t[2] + dz * u };
        if (smoke) {
            part *s = spawn(SH_SMOKE, 1, B_SEMI, q, 650 + rnd() * 200, 22, 75, 185, 185, 185, 130);
            s->spin = rnd() * 6.283f; s->rise = 0.035f;
        } else {
            spawn(SH_GLOW, 1, B_ADD, q, 300, 26, 8, 255, 255, 255, 210);
        }
    }
    if (n) memcpy(t, p, sizeof g_seen[k].trail);
}

void fx_update(const acr_gen1_battle *b, uint64_t now_ns) {
    float dt = g_last_ns ? (float)(now_ns - g_last_ns) / 1e6f : 0.0f;
    if (dt > 100.0f) dt = 100.0f;
    g_last_ns = now_ns;
    g_now_ms += dt;

    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        const int restarted = g_seen[k].on && s->active &&
            (s->missile != g_seen[k].missile || s->travelled < g_seen[k].travelled ||
             (s->missile && s->age < g_seen[k].age));
        if (g_seen[k].on && (!s->active || restarted)) {
            shot_ended(b, k);
            g_seen[k].on = 0;
        }
        if (!s->active) continue;
        float p[3];
        shot_pos(s, p);
        const acr_gen1_fighter *o = &b->f[s->owner < ACR_GEN1_MAX_FIGHTERS ? s->owner : 0];
        const acr_gen1_weapon *w = s->weapon < o->weapons ? &o->weapon[s->weapon] : NULL;
        float st[3] = { (float)s->vx / 64.0f, -(float)s->vy / 64.0f, (float)s->vz / 64.0f };
        if (!g_seen[k].on) {
            g_seen[k].on = 1;
            g_seen[k].missile = s->missile;
            g_seen[k].energy = (uint8_t)(w && w->energy);
            g_seen[k].owner = s->owner;
            g_seen[k].look = look_of(s->owner, s->weapon, g_seen[k].energy);
            g_seen[k].range = w ? w->range : 0;
            g_seen[k].speed = (int32_t)sqrtf(st[0] * st[0] + st[1] * st[1] + st[2] * st[2]);
            /* It has usually moved a step already: the flash goes where it left. */
            float m[3];
            const float back = s->missile ? 0.0f : 1.0f;
            for (int i = 0; i < 3; ++i) m[i] = p[i] - st[i] * back;
            muzzle(m, g_seen[k].energy, s->missile);
            memcpy(g_seen[k].trail, p, sizeof g_seen[k].trail);
        }
        g_seen[k].travelled = s->travelled;
        g_seen[k].age = s->age;
        memcpy(g_seen[k].p, p, sizeof g_seen[k].p);
        if (!s->missile && g_seen[k].look && g_seen[k].look->look == LOOK_ROCKET) missile_trail(k, dt, 1);
        if (s->missile) {
            /* missiles: the heading's step for the hit test */
            const float len = sqrtf(st[0] * st[0] + st[1] * st[1] + st[2] * st[2]);
            if (len > 0) for (int i = 0; i < 3; ++i) st[i] = st[i] / len * 60.0f;
            missile_trail(k, dt, 0);
        }
        memcpy(g_seen[k].step, st, sizeof g_seen[k].step);
    }

    for (int k = 0; k < acr_gen1_battle_fighters(b); ++k) {
        if (g_ap_ok && g_ap[k] > 0 && b->f[k].ap <= 0) {
            float feet[3];
            ac_feet(b, k, feet);
            ac_blast(feet);
        }
        /* A blade hit: the hit dispatcher's flash (VS 0x8008821C ->
         * 0x8008C164) at the AC's middle. Blades are energy hits (flags
         * 0x20), so the energy look; the exact sprite is not mapped yet. */
        if (g_ap_ok && b->f[k].blade_hit_seq != g_bhit[k] && b->f[k].ap > 0) {
            float mid[3];
            ac_feet(b, k, mid);
            mid[1] += 106.0f;                               /* half the hit cylinder (212) */
            hit_ac(mid, 1);
        }
        g_bhit[k] = b->f[k].blade_hit_seq;
        g_ap[k] = b->f[k].ap;
        {   /* Burning (Gen 1 burn, VS 0x8007D038 spawns flames and chips on the
             * AC every step it burns): short flames over the body, a few
             * sparks, the odd wisp of smoke. Matched by eye. */
            static float burn_ms[ACR_GEN1_MAX_FIGHTERS];
            if (b->f[k].burn_steps && b->f[k].ap > 0 && !b->over) {
                burn_ms[k] += dt;
                while (burn_ms[k] >= 45.0f) {              /* about one Gen 1 step */
                    burn_ms[k] -= 45.0f;
                    float feet[3];
                    ac_feet(b, k, feet);
                    const float a = rnd() * 6.283f, r = 20 + rnd() * 50;
                    const float q[3] = { feet[0] + cosf(a) * r, feet[1] + 20 + rnd() * 190, feet[2] + sinf(a) * r };
                    part *f = spawn(SH_FIRE, 4, B_ADD, q, 360, 70 + rnd() * 40, 40, 255, 220, 170, 255);
                    f->loop_ms = 70; f->rise = 0.12f;
                    if (rnd() < 0.35f) {
                        part *c = spawn(SH_SPARKS, 2, B_ADD, q, 160, 40, 60, 255, 200, 120, 255);
                        c->spin = rnd() * 6.283f;
                    }
                    if (rnd() < 0.15f) {
                        part *m = spawn(SH_SMOKE, 1, B_SEMI, q, 800, 40, 130, 90, 85, 80, 90);
                        m->spin = rnd() * 6.283f; m->rise = 0.08f;
                    }
                }
            } else burn_ms[k] = 0.0f;
        }
        {   /* the booster lighting */
            const int on = boosting(&b->f[k]);
            if (on && !g_boosting[k] && g_ap_ok) {
                fx_rig r;
                float feet[3];
                rig_of(b, k, &r);
                ac_feet(b, k, feet);
                boost_ignite(&r, b->f[k].ac.state != ACR_GEN1_AIR, feet);
            }
            g_boost_ms[k] = on ? (g_boosting[k] ? g_boost_ms[k] + dt : 0.0f) : 0.0f;
            g_boosting[k] = (uint8_t)on;
        }
        {   /* the beam follows the arm: lit on the slash, out once it drops */
            const int ms = b->f[k].swing;
            if (!ms) { g_slash[k].lit = g_slash[k].out = -1; g_slash[k].have = 0; }
            else if (g_slash[k].out < 0) {
                fx_rig r;
                rig_of(b, k, &r);
                if (!r.blade_ok) { if (g_slash[k].lit < 0) g_slash[k].lit = ms; }   /* no model to follow: as before */
                else {
                    const float right[2] = { -r.back[2], r.back[0] };
                    const float dx = r.blade_tip[0] - r.blade_root[0], dz = r.blade_tip[2] - r.blade_root[2];
                    const float lat = dx * right[0] + dz * right[1], ahead = -(dx * r.back[0] + dz * r.back[2]);
                    const float up = r.blade_tip[1] - r.blade_root[1];
                    /* sweeping: the hand moved left since the last sample,
                     * 30 ms or more ago (frame to frame is too little to see) */
                    const int due = !g_slash[k].have || ms - g_slash[k].at >= 30;
                    if (g_slash[k].lit < 0) {
                        const int sweeping = g_slash[k].have && due && lat < g_slash[k].lat - 10.0f;
                        if ((sweeping && ahead > fabsf(up)) || ms >= 700) g_slash[k].lit = ms;   /* 700: never seen: don't lose it */
                    } else if (up < -40.0f && ms - g_slash[k].lit >= 60) g_slash[k].out = ms;
                    if (due) { g_slash[k].lat = lat; g_slash[k].at = ms; g_slash[k].have = 1; }
                }
            }
        }
        {   /* the blade's trail: where the beam was, a spot every 12 ms */
            const int ms = g_slash[k].lit >= 0 && blade_glow_k(k, b->f[k].swing) > 0 ? b->f[k].swing : 0;
            if (!ms || !has_blade(&b->f[k]) || b->f[k].ap <= 0) g_trail[k].n = 0;
            else {
                for (int i = 0; i < g_trail[k].n; ++i) g_trail[k].t[i] += dt;
                if (!g_trail[k].n || g_trail[k].t[0] >= 12.0f) {
                    fx_rig r;
                    rig_of(b, k, &r);
                    if (r.blade_ok) {
                        if (g_trail[k].n < FX_TRAIL) g_trail[k].n++;
                        memmove(&g_trail[k].t[1], &g_trail[k].t[0], sizeof(float) * (FX_TRAIL - 1));
                        memmove(&g_trail[k].base[1], &g_trail[k].base[0], sizeof(float) * 3 * (FX_TRAIL - 1));
                        memmove(&g_trail[k].end[1], &g_trail[k].end[0], sizeof(float) * 3 * (FX_TRAIL - 1));
                        g_trail[k].t[0] = 0.0f;
                        blade_line(b, k, &r, g_trail[k].base[0], g_trail[k].end[0]);
                    }
                }
                while (g_trail[k].n && g_trail[k].t[g_trail[k].n - 1] > 110.0f) g_trail[k].n--;
            }
        }
    }
    /* Mission units: a blast sized to the body the tick one is destroyed. */
    for (int u = 0; u < b->units; ++u) {
        const acr_gen1_unit *un = &b->unit[u];
        if (un->down && !g_unit_down[u] && g_ap_ok) {
            const float c[3] = { (float)un->x / 64.0f, -(float)un->y / 64.0f + un->height * 0.5f, (float)un->z / 64.0f };
            const float sz = (float)(un->half > un->height ? un->half : un->height) / 150.0f;
            explosion(c, sz < 0.6f ? 0.6f : sz > 3.0f ? 3.0f : sz, 0);
            explosion(c, sz * 0.6f < 0.5f ? 0.5f : sz * 0.6f, 0.15f);
        }
        g_unit_down[u] = un->down;
    }
    g_ap_ok = 1;

    for (int i = 0; i < FX_PARTS; ++i) {
        part *q = &g_part[i];
        if (!q->on) continue;
        q->age += dt;
        if (q->age >= q->life) { q->on = 0; continue; }
        if (q->age < 0) continue;
        for (int c = 0; c < 3; ++c) q->p[c] += q->v[c] * dt;
        q->p[1] += q->rise * dt;
    }
}

/* ---- drawing ---- */

#define FX_VERTS ((FX_PARTS + ACR_GEN1_MAX_SHOTS * 3) * 6)
static acr_vtx g_v[FX_VERTS];
static int g_n;

static void quad(const float c[4][3], float u0, float v0, float u1, float v1,
                 uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (g_n + 6 > FX_VERTS) return;
    const float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
    static const int k_order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i) {
        const int j = k_order[i];
        acr_vtx *v = &g_v[g_n++];
        v->x = c[j][0]; v->y = c[j][1]; v->z = c[j][2];
        v->r = r; v->g = g; v->b = b; v->a = a;
        v->u = uv[j][0]; v->v = uv[j][1];
    }
}

static void flush(int sheet, int blend) {
    if (!g_n) return;
    acr_render_mesh(g_v, g_n, g_tex[sheet], blend == B_ADD ? ACR_MESH_ADD : ACR_MESH_SEMI);
    g_n = 0;
}

static void norm(float v[3]) {
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

/* A picture facing the camera, turned by `spin`. */
static void billboard(const float p[3], float size, float spin, const float right[3], const float up[3],
                      int sheet, rect r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    const float h = size * 0.5f, cs = cosf(spin) * h, sn = sinf(spin) * h;
    float ax[3], ay[3];
    for (int i = 0; i < 3; ++i) { ax[i] = right[i] * cs + up[i] * sn; ay[i] = up[i] * cs - right[i] * sn; }
    const float c[4][3] = {
        { p[0] - ax[0] + ay[0], p[1] - ax[1] + ay[1], p[2] - ax[2] + ay[2] },
        { p[0] + ax[0] + ay[0], p[1] + ax[1] + ay[1], p[2] + ax[2] + ay[2] },
        { p[0] + ax[0] - ay[0], p[1] + ax[1] - ay[1], p[2] + ax[2] - ay[2] },
        { p[0] - ax[0] - ay[0], p[1] - ax[1] - ay[1], p[2] - ax[2] - ay[2] },
    };
    const float tw = (float)g_tex_w[sheet], th = (float)g_tex_h[sheet];
    quad(c, r.u / tw, r.v / th, (r.u + r.w) / tw, (r.v + r.h) / th, cr, cg, cb, ca);
}

/* A picture stretched from `tail` to `head`, turned to face the eye; the
 * picture's top goes at the head. */
static void streak(const float tail[3], const float head[3], float width, const float eye[3],
                   int sheet, rect r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    float d[3] = { head[0] - tail[0], head[1] - tail[1], head[2] - tail[2] };
    const float mid[3] = { (head[0] + tail[0]) * 0.5f, (head[1] + tail[1]) * 0.5f, (head[2] + tail[2]) * 0.5f };
    float e[3] = { eye[0] - mid[0], eye[1] - mid[1], eye[2] - mid[2] }, s[3];
    norm(d);
    norm(e);
    cross(d, e, s);
    norm(s);
    const float h = width * 0.5f;
    const float c[4][3] = {
        { head[0] - s[0] * h, head[1] - s[1] * h, head[2] - s[2] * h },
        { head[0] + s[0] * h, head[1] + s[1] * h, head[2] + s[2] * h },
        { tail[0] + s[0] * h, tail[1] + s[1] * h, tail[2] + s[2] * h },
        { tail[0] - s[0] * h, tail[1] - s[1] * h, tail[2] - s[2] * h },
    };
    const float tw = (float)g_tex_w[sheet], th = (float)g_tex_h[sheet];
    quad(c, r.u / tw, r.v / th, (r.u + r.w) / tw, (r.v + r.h) / th, cr, cg, cb, ca);
}

static void draw_parts(int sheet, int blend, const float right[3], const float up[3]) {
    for (int i = 0; i < FX_PARTS; ++i) {
        const part *q = &g_part[i];
        if (!q->on || q->sheet != sheet || q->blend != blend || q->age < 0) continue;
        const float t = q->age / q->life;
        int frame = q->loop_ms > 0 ? (int)(q->age / q->loop_ms) % q->frames : (int)(t * q->frames);
        if (frame >= q->frames) frame = q->frames - 1;
        const float cell = (float)g_tex_w[sheet] / (float)q->frames;
        const rect r = { cell * (float)frame, 0, cell, (float)g_tex_h[sheet] };
        const float size = q->s0 + (q->s1 - q->s0) * t;
        /* fade over the last part of the life (smoke over all of it) */
        float k = blend == B_SEMI ? 1.0f - t : (t > 0.7f ? (1.0f - t) / 0.3f : 1.0f);
        if (q->loop_ms > 0 && t < 0.1f) k = t / 0.1f;
        float p[3] = { q->p[0], q->p[1], q->p[2] };
        if (q->ground) p[1] += size * 0.45f;
        billboard(p, size, q->spin, right, up, sheet, r, q->r, q->g, q->b, (uint8_t)(q->a * k));
    }
    flush(sheet, blend);
}

/* Placeholder colours (see fx.h). Matched by a piece of the name, first
 * match wins. */
typedef struct { const char *key; uint8_t rgb[3]; } fx_colour;
static const fx_colour k_flame_colours[] = {
    { "B-VR", { 120, 170, 255 } },     /* guess: the high-thrust boosters burn blue */
    { "B-T2", { 255, 110, 70 } },
    { "B-T0", { 255, 200, 90 } },
    { "B-P35", { 255, 140, 60 } },
    { "B-P32", { 255, 165, 80 } },
};
static const fx_colour k_beam_colours[] = {
    { "MOONLIGHT", { 110, 150, 255 } },  /* guess: deep blue-violet */
    { "3303", { 255, 120, 110 } },
    { "2006", { 120, 255, 170 } },
    { "2001", { 130, 195, 255 } },
};
static int colour_of(const fx_colour *t, int n, const char *name, uint8_t out[3]) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < n; ++i)
        if (SDL_strcasestr(name, t[i].key)) { SDL_memcpy(out, t[i].rgb, 3); return 1; }
    return 0;
}
void fx_part_colour(const char *booster, const char *blade, uint8_t flame[3], uint8_t beam[3]) {
    static const uint8_t k_flame[3] = { 255, 165, 80 }, k_beam[3] = { 130, 195, 255 };
    if (!colour_of(k_flame_colours, (int)SDL_arraysize(k_flame_colours), booster, flame)) SDL_memcpy(flame, k_flame, 3);
    if (!colour_of(k_beam_colours, (int)SDL_arraysize(k_beam_colours), blade, beam)) SDL_memcpy(beam, k_beam, 3);
}
/* An unset rig colour (all 0) is the default. */
static void rig_colour(const uint8_t c[3], uint8_t r, uint8_t g, uint8_t b, uint8_t out[3]) {
    if (c[0] | c[1] | c[2]) SDL_memcpy(out, c, 3); else { out[0] = r; out[1] = g; out[2] = b; }
}
/* The hot core: the colour washed most of the way to white. */
static void whiter(const uint8_t c[3], float k, uint8_t out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = (uint8_t)(c[i] + (255 - c[i]) * k);
}

void fx_draw(const acr_gen1_battle *b, const float eye[3], const float fwd[3]) {
    float f[3] = { fwd[0], fwd[1], fwd[2] }, right[3], up[3];
    const float world_up[3] = { 0, 1, 0 };
    norm(f);
    cross(f, world_up, right);
    norm(right);
    cross(right, f, up);
    norm(up);
    g_n = 0;

    /* Smoke first (see-through, drawn over by the glows), then the glows. */
    for (int s = 0; s < SH_COUNT; ++s) draw_parts(s, B_SEMI, right, up);
    for (int s = 0; s < SH_COUNT; ++s) draw_parts(s, B_ADD, right, up);

    /* The shots themselves. */
    const float flick = 0.85f + 0.15f * sinf(g_now_ms * 0.09f);
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        if (!s->active) continue;
        float p[3];
        shot_pos(s, p);
        float d[3] = { (float)s->vx, -(float)s->vy, (float)s->vz };
        const float speed = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / 64.0f;
        norm(d);
        if (s->missile) {
            /* the rocket flame behind, nozzle at the missile */
            const float len = 70.0f * flick;
            const float tail[3] = { p[0] - d[0] * len, p[1] - d[1] * len, p[2] - d[2] * len };
            streak(tail, p, 30.0f, eye, SH_SHOTS, k_plume, 255, 170, 90, 255);
        } else {
            const look *lk = g_seen[k].on && g_seen[k].look ? g_seen[k].look : &k_look_shell;
            if (lk->look == LOOK_BLOB || lk->look == LOOK_ROCKET) {
                /* round front at the shot, tail narrowing behind it */
                float len = lk->length;
                if ((float)s->travelled < len) len = (float)s->travelled > 25.0f ? (float)s->travelled : 25.0f;
                const float tail[3] = { p[0] - d[0] * len, p[1] - d[1] * len, p[2] - d[2] * len };
                const float head[3] = { p[0] + d[0] * lk->width * 0.3f, p[1] + d[1] * lk->width * 0.3f, p[2] + d[2] * lk->width * 0.3f };
                streak(tail, head, lk->width, eye, SH_SHOTS, k_plume, lk->r, lk->g, lk->b, 255);
                continue;
            }
            float len = speed * 1.6f;
            if (len < 60.0f) len = 60.0f;
            if (len > 280.0f) len = 280.0f;
            if ((float)s->travelled < len) len = (float)s->travelled > 20.0f ? (float)s->travelled : 20.0f;
            const float tail[3] = { p[0] - d[0] * len, p[1] - d[1] * len, p[2] - d[2] * len };
            if (lk->look == LOOK_ENERGY) streak(tail, p, lk->width, eye, SH_SHOTS, k_beam, lk->r, lk->g, lk->b, 255);
            else streak(tail, p, lk->width, eye, SH_SHOTS, k_tracer, lk->r, lk->g, lk->b, 255);
        }
    }
    flush(SH_SHOTS, B_ADD);
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        if (!s->active) continue;
        float p[3];
        shot_pos(s, p);
        if (s->missile) billboard(p, 26.0f * flick, 0, right, up, SH_SHOTS, k_head, 255, 240, 200, 255);
    }
    flush(SH_SHOTS, B_ADD);
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        if (!s->active || s->missile || !(g_seen[k].on && g_seen[k].energy)) continue;
        float p[3];
        shot_pos(s, p);
        const rect all = { 0, 0, (float)g_tex_w[SH_GLOW], (float)g_tex_h[SH_GLOW] };
        billboard(p, 50.0f * flick, 0, right, up, SH_GLOW, all, 140, 200, 255, 255);
    }
    flush(SH_GLOW, B_ADD);

        /* Booster flames: a flickering plume out of each nozzle, backward and
     * a little down, longer for a moment as it lights; a hot glow on the
     * nozzle. */
    for (int k = 0; k < acr_gen1_battle_fighters(b); ++k) {
        if (!g_boosting[k]) continue;
        fx_rig r;
        rig_of(b, k, &r);
        const float fl = 0.8f + 0.2f * sinf(g_now_ms * 0.11f + k * 1.7f) + 0.08f * (rnd() - 0.5f);
        const float lit = g_boost_ms[k] < 120.0f ? 1.0f + 0.6f * (1.0f - g_boost_ms[k] / 120.0f) : 1.0f;
        float d[3] = { r.back[0], -0.35f, r.back[2] };
        norm(d);
        for (int n = 0; n < r.nozzles; ++n) {
            const float *p = r.nozzle[n];
            const float len = 62.0f * fl * lit, core = 30.0f * fl * lit;
            const float tail[3] = { p[0] + d[0] * len, p[1] + d[1] * len, p[2] + d[2] * len };
            const float tail2[3] = { p[0] + d[0] * core, p[1] + d[1] * core, p[2] + d[2] * core };
            uint8_t c[3], w[3];
            rig_colour(r.flame, 255, 165, 80, c);
            whiter(c, 0.75f, w);
            streak(tail, p, 30.0f, eye, SH_SHOTS, k_plume, c[0], c[1], c[2], 230);
            streak(tail2, p, 16.0f, eye, SH_SHOTS, k_plume, w[0], w[1], w[2], 255);
        }
    }
    flush(SH_SHOTS, B_ADD);
    for (int k = 0; k < acr_gen1_battle_fighters(b); ++k) {
        if (!g_boosting[k]) continue;
        fx_rig r;
        rig_of(b, k, &r);
        const rect all = { 0, 0, (float)g_tex_w[SH_GLOW], (float)g_tex_h[SH_GLOW] };
        uint8_t c[3], w[3];
        rig_colour(r.flame, 255, 165, 80, c);
        whiter(c, 0.35f, w);
        for (int n = 0; n < r.nozzles; ++n)
            billboard(r.nozzle[n], 34.0f * flick, 0, right, up, SH_GLOW, all, w[0], w[1], w[2], 220);
    }
    flush(SH_GLOW, B_ADD);

    /* The blade: a light beam out of the hand along the forearm, its core
     * white, with a fading sheet over the last ~0.1 s of the slash. */
    for (int k = 0; k < acr_gen1_battle_fighters(b); ++k) {
        const acr_gen1_fighter *f = &b->f[k];
        const float g = f->ap > 0 && has_blade(f) ? blade_glow_k(k, f->swing) : 0.0f;
        if (g <= 0.0f) continue;
        fx_rig r;
        rig_of(b, k, &r);
        if (!r.blade_ok) continue;
        float base[3], end[3];
        blade_line(b, k, &r, base, end);
        uint8_t bc[3], bw[3];
        rig_colour(r.beam, 130, 195, 255, bc);
        whiter(bc, 0.8f, bw);
        const uint8_t a = (uint8_t)(255 * g);
        /* the trail: quads between the beam's past spots */
        const float tw = (float)g_tex_w[SH_SHOTS], th = (float)g_tex_h[SH_SHOTS];
        const float u0 = (k_beam.u + k_beam.w * 0.3f) / tw, u1 = (k_beam.u + k_beam.w * 0.7f) / tw;
        for (int i = 0; i + 1 < g_trail[k].n; ++i) {
            const float fade = 1.0f - g_trail[k].t[i + 1] / 110.0f;
            if (fade <= 0) continue;
            const float c[4][3] = {
                { g_trail[k].end[i][0], g_trail[k].end[i][1], g_trail[k].end[i][2] },
                { g_trail[k].end[i + 1][0], g_trail[k].end[i + 1][1], g_trail[k].end[i + 1][2] },
                { g_trail[k].base[i + 1][0], g_trail[k].base[i + 1][1], g_trail[k].base[i + 1][2] },
                { g_trail[k].base[i][0], g_trail[k].base[i][1], g_trail[k].base[i][2] },
            };
            quad(c, u0, 0, u1, k_beam.h / th, bc[0], bc[1], bc[2], (uint8_t)(150 * fade * g));
        }
        const float fl = 0.9f + 0.1f * sinf(g_now_ms * 0.15f + k);
        streak(base, end, 30.0f * fl, eye, SH_SHOTS, k_beam, bc[0], bc[1], bc[2], a);
        streak(base, end, 12.0f, eye, SH_SHOTS, k_beam, bw[0], bw[1], bw[2], a);
    }
    flush(SH_SHOTS, B_ADD);
    for (int k = 0; k < acr_gen1_battle_fighters(b); ++k) {
        const acr_gen1_fighter *f = &b->f[k];
        const float g = f->ap > 0 && has_blade(f) ? blade_glow_k(k, f->swing) : 0.0f;
        if (g <= 0.0f) continue;
        fx_rig r;
        rig_of(b, k, &r);
        if (!r.blade_ok) continue;
        const rect all = { 0, 0, (float)g_tex_w[SH_GLOW], (float)g_tex_h[SH_GLOW] };
        uint8_t bc[3], bw[3];
        rig_colour(r.beam, 130, 195, 255, bc);
        whiter(bc, 0.15f, bw);
        billboard(r.blade_tip, 46.0f * flick, 0, right, up, SH_GLOW, all, bw[0], bw[1], bw[2], (uint8_t)(230 * g));
    }
    flush(SH_GLOW, B_ADD);
}
