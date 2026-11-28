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

/* ---- the disc's own shot looks (fx_look, from acr-import) ---- */

static const fx_look *g_looks[ACR_GEN1_MAX_FIGHTERS];
static int g_look_count[ACR_GEN1_MAX_FIGHTERS];

void fx_set_looks(int fighter, const fx_look *looks, int count) {
    if (fighter < 0 || fighter >= ACR_GEN1_MAX_FIGHTERS) return;
    g_looks[fighter] = looks;
    g_look_count[fighter] = looks ? count : 0;
}

/* The weapon's look when the import has one (any family, or a hit flash
 * word: missiles have only that), else NULL. */
static const fx_look *disc_look(int owner, int slot) {
    if (owner < 0 || owner >= ACR_GEN1_MAX_FIGHTERS || !g_looks[owner] || slot < 0 || slot >= g_look_count[owner]) return NULL;
    const fx_look *l = &g_looks[owner][slot];
    return l->family >= 0 || l->hit_flash ? l : NULL;
}

/* PS1 colours: 128 is 1.0, so a vertex colour is twice the byte, capped. */
static uint8_t ps1(uint8_t c) { return (uint8_t)(c >= 128 ? 255 : c * 2); }

/* The effect atlas (VS 0x80091A28), as acr-import wrote it to
 * fx/sprites.txt: each sprite's picture, rectangle and colour. */
#define FX_ATLAS 297
#define FX_TEXC 24
typedef struct { uint8_t ok, tex; float u, v, w, h; uint8_t r, g, b; } atlas_ent;
static atlas_ent g_atlas[FX_ATLAS];
static struct { char file[24]; uint32_t tex; int w, h; } g_tc[FX_TEXC];
static int g_ntc;
static int g_atlas_ok;

static uint32_t load_png(const char *path, int *w, int *h);

static int tex_slot(const char *dir, const char *file) {
    for (int i = 0; i < g_ntc; ++i) if (!SDL_strcmp(g_tc[i].file, file)) return i;
    if (g_ntc >= FX_TEXC) return -1;
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/fx/%s", dir, file);
    /* smoke and debris keep every texel; the glows drop their dark halo */
    g_trim_dark = !(SDL_strstr(file, "fx-12") || SDL_strstr(file, "fx-05") || SDL_strstr(file, "fx-29"));
    int w = 1, h = 1;
    const uint32_t t = load_png(path, &w, &h);
    if (!t) return -1;
    SDL_strlcpy(g_tc[g_ntc].file, file, sizeof g_tc[g_ntc].file);
    g_tc[g_ntc].tex = t; g_tc[g_ntc].w = w; g_tc[g_ntc].h = h;
    return g_ntc++;
}

static void atlas_load(const char *dir) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/fx/sprites.txt", dir);
    size_t size = 0;
    char *txt = SDL_LoadFile(path, &size);
    if (!txt) return;
    for (char *line = txt; line && *line;) {
        char *next = SDL_strchr(line, '\n');
        if (next) *next++ = 0;
        unsigned i, u, v, w, h, r, g, b;
        char file[24];
        if (line[0] != '#' && SDL_sscanf(line, "%u %23s %u %u %u %u %u %u %u", &i, file, &u, &v, &w, &h, &r, &g, &b) == 9 && i < FX_ATLAS) {
            const int t = tex_slot(dir, file);
            if (t >= 0) {
                atlas_ent *a = &g_atlas[i];
                a->ok = 1; a->tex = (uint8_t)t;
                a->u = (float)u; a->v = (float)v; a->w = (float)w; a->h = (float)h;
                a->r = (uint8_t)r; a->g = (uint8_t)g; a->b = (uint8_t)b;
                g_atlas_ok = 1;
            }
        }
        line = next;
    }
    SDL_free(txt);
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
    if (!g_atlas_ok) atlas_load(import_dir);
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
    uint16_t atlas_first; /* atlas parts (sheet SH_COUNT): their frames are */
    uint8_t  atlas_count; /* atlas sprites first .. first + count - 1 */
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
    const fx_look *fl;         /* the disc's look for it (NULL: by type) */
    float tick_speed;          /* world units a Gen 1 tick (streak length = trail x this) */
    float dir[3];              /* the way it flies */
    float glow_ms;             /* B shots: time since the last trailing glow */
    uint8_t wave;              /* a blade wave (20 chips on a hit) */
} g_seen[ACR_GEN1_MAX_SHOTS];

/* Debris chips (PP 0x80083BE0 -> spawner 0x80083874; ACG1PLUS
 * mapping/weapon-effects.md 7): a hit cuts `count` small triangles off
 * the AC it hits, in its own paint; each starts at the hit point (within
 * the word's spread), flies off with a random push (sideways +-25, up
 * 15-35 a tick) and spin, and lives `life` to 1.25 x `life` ticks. The
 * game takes a random triangle of the target's model; here a random
 * triangle 12-30 across in one of its paint colours. The fall (3 a tick
 * more each tick) is a guess. */
#define FX_CHIPS 256
static struct {
    uint8_t on, rgb[3];
    float p[3], v[3], tri[3][3], axis[3], ang, spin, age, life;
} g_chip[FX_CHIPS];
static int g_chip_next;
static uint8_t g_chip_rgb[ACR_GEN1_MAX_FIGHTERS][4][3];
static int g_chip_n[ACR_GEN1_MAX_FIGHTERS];

void fx_set_chip_colours(int fighter, const uint8_t rgb[][3], int n) {
    if (fighter < 0 || fighter >= ACR_GEN1_MAX_FIGHTERS) return;
    if (n > 4) n = 4;
    g_chip_n[fighter] = n < 0 ? 0 : n;
    for (int i = 0; i < g_chip_n[fighter]; ++i) memcpy(g_chip_rgb[fighter][i], rgb[i], 3);
}

/* A streak whose shot has ended: its head stays where it stopped and the
 * tail catches up (A update 0x80088674, the life < 2 branch: the tail keeps
 * moving for the steps it waited). */
#define FX_GHOSTS 64
static struct {
    uint8_t on, rgb[3];
    uint16_t sprite;
    float head[3], dir[3], len, shrink, width;   /* shrink: units a ms */
} g_ghost[FX_GHOSTS];
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
    memset(g_chip, 0, sizeof g_chip);
    memset(g_boosting, 0, sizeof g_boosting);
    memset(g_trail, 0, sizeof g_trail);
    memset(g_rig_ok, 0, sizeof g_rig_ok);
    memset(g_seen, 0, sizeof g_seen);
    memset(g_ghost, 0, sizeof g_ghost);
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

/* The impact flash word (VS 0x8008B60C; ACG1PLUS weapon-map.md section 3):
 * bits 0-2 the kind (7 none), 3-4 frames a tick, 17-26 the size. The kinds'
 * sprites (table 0x800930DC): fireball 8-19, hot glow 20-31, spark spray
 * 32-33, flames 42-49, blue / magenta / orange lens flare 37 / 38 / 36.
 * The size is taken as the sprite's width in world units (a guess: the
 * billboard draw's scale is not traced). The light colour (bits 10-12,
 * the stage's per-area light) and the hit sound are not drawn here.
 * 1 when a flash was made. */
static const struct { uint16_t first; uint8_t count; } k_flash_kind[7] = {
    { 8, 12 }, { 20, 12 }, { 32, 2 }, { 42, 8 }, { 37, 1 }, { 38, 1 }, { 36, 1 },
};
#define FX_TICK_MS (1000.0f / 22.0f)
static int flash_word(const float p[3], uint32_t w) {
    const unsigned kind = w & 7;
    if (kind == 7 || !g_atlas_ok) return 0;
    const unsigned step = ((w >> 3) & 3) ? ((w >> 3) & 3) : 1, size = (w >> 17) & 0x3FF;
    const unsigned first = k_flash_kind[kind].first, count = k_flash_kind[kind].count;
    if (!g_atlas[first].ok) return 0;
    float ticks = (float)count / (float)step;
    if (count == 1) { ticks = (float)((w >> 5) & 31); if (ticks < 4) ticks = 4; }   /* a flare: its length field */
    part *q = spawn(SH_COUNT, (int)count, B_ADD, p, ticks * FX_TICK_MS, (float)size, (float)size, 255, 255, 255, 255);
    q->atlas_first = (uint16_t)first;
    q->atlas_count = (uint8_t)count;
    q->spin = kind >= 4 ? 0.0f : rnd() * 6.283f;
    return 1;
}
/* B shots' spark burst (0x8008B7B8, instead of a flash word): the spark
 * spray, matched by eye. */
static int spark_burst(const float p[3]) {
    if (!g_atlas_ok || !g_atlas[32].ok) return 0;
    part *q = spawn(SH_COUNT, 2, B_ADD, p, 2 * FX_TICK_MS, 150, 170, 255, 255, 255, 255);
    q->atlas_first = 32;
    q->atlas_count = 2;
    q->spin = rnd() * 6.283f;
    return 1;
}
/* A model shot's or timed shell's end (ACG1PLUS mapping/weapon-effects.md
 * 2b, 3): explosion model 0 (15 ticks, the big one: WG-PB26, AW-DC/2, the
 * timed shells) or 5 (10 ticks: WG-B2180, WR-L24, AW-XC5500, WC-01QL) as
 * a growing fireball; a timed shell bursts into three of them three ticks
 * apart; without a model, the white flare. Sizes matched to the records'
 * growth by eye. */
static void model_blast(int k, const float p[3]) {
    const fx_look *l = g_seen[k].fl;
    if (!l || l->family != FX_FAM_MODEL) return;
    if (l->timed) {
        for (int n = 0; n < 3; ++n) explosion(p, 1.9f, n * 3 * FX_TICK_MS);
        return;
    }
    if (l->blast == 0) explosion(p, 1.9f, 0);
    else if (l->blast != 255) explosion(p, l->blast == 5 ? 1.4f : 1.2f, 0);
    else if (l->flare) spawn(SH_FLARE, 1, B_ADD, p, 4 * FX_TICK_MS, (float)l->flare * 0.25f, (float)l->flare * 0.4f, 255, 255, 255, 230);
}

/* A blade hit (PP 0x80083234 from the line test 0x80076738; ACG1PLUS
 * mapping/blade.md 6): a spark burst of 1 + 6 spark-spray flashes, size
 * 300, scattered around the hit point, with no light. The scatter (+-500
 * in the game's numbers) is drawn at a quarter of that: a guess. */
static void blade_sparks(const float p[3]) {
    if (!g_atlas_ok || !g_atlas[32].ok) { hit_ac(p, 1); return; }
    for (int n = 0; n < 7; ++n) {
        float q[3] = { p[0], p[1], p[2] };
        if (n) for (int c = 0; c < 3; ++c) q[c] += (rnd() - 0.5f) * 250.0f;
        part *s = spawn(SH_COUNT, 2, B_ADD, q, 2 * FX_TICK_MS, n ? 180 : 300, n ? 220 : 340, 255, 255, 255, 255);
        s->atlas_first = 32;
        s->atlas_count = 2;
        s->spin = rnd() * 6.283f;
        s->age = -(float)(n / 2) * FX_TICK_MS * 0.5f;
    }
}

static void chips(int target, const float p[3], uint32_t word) {
    const int count = (int)((word >> 18) & 31);
    const float life = (float)((word >> 10) & 255), spread = (float)(word & 1023);
    for (int n = 0; n < count; ++n) {
        int i = -1;
        for (int t = 0; t < FX_CHIPS; ++t) {
            const int j = (g_chip_next + t) % FX_CHIPS;
            if (!g_chip[j].on) { i = j; break; }
        }
        if (i < 0) i = g_chip_next;                  /* full: the oldest goes */
        g_chip_next = (i + 1) % FX_CHIPS;
        g_chip[i].on = 1;
        for (int c = 0; c < 3; ++c) g_chip[i].p[c] = p[c] + (rnd() - 0.5f) * spread;
        g_chip[i].v[0] = (rnd() - 0.5f) * 50.0f / FX_TICK_MS;
        g_chip[i].v[2] = (rnd() - 0.5f) * 50.0f / FX_TICK_MS;
        g_chip[i].v[1] = (15.0f + rnd() * 20.0f) / FX_TICK_MS;
        const float size = 12.0f + rnd() * 18.0f;
        for (int v = 0; v < 3; ++v) {
            const float a = (float)v * 2.094f + (rnd() - 0.5f) * 1.2f, r = size * (0.4f + rnd() * 0.6f);
            g_chip[i].tri[v][0] = cosf(a) * r; g_chip[i].tri[v][1] = sinf(a) * r; g_chip[i].tri[v][2] = (rnd() - 0.5f) * size * 0.3f;
        }
        float ax[3] = { rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f };
        const float l = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        for (int c = 0; c < 3; ++c) g_chip[i].axis[c] = l > 1e-4f ? ax[c] / l : (c == 1);
        g_chip[i].ang = rnd() * 6.283f;
        g_chip[i].spin = (0.3f + rnd() * 0.5f) / FX_TICK_MS * (rnd() < 0.5f ? -1.0f : 1.0f);
        g_chip[i].age = 0;
        g_chip[i].life = (life + rnd() * life * 0.25f) * FX_TICK_MS;
        const int pn = target >= 0 && target < ACR_GEN1_MAX_FIGHTERS ? g_chip_n[target] : 0;
        static const uint8_t grey[3] = { 150, 150, 145 };
        const uint8_t *c = pn ? g_chip_rgb[target][(int)(rnd() * pn) % pn] : grey;
        memcpy(g_chip[i].rgb, c, 3);
    }
}

static int disc_hit(int k, const float p[3]) {
    const fx_look *l = g_seen[k].fl;
    if (!l) return 0;
    if (l->hit_sparks) return spark_burst(p);
    return l->hit_flash ? flash_word(p, l->hit_flash) : 0;
}

/* A streak that ended: its tail catches up with where it stopped. */
static void ghost_add(int k, const float head[3]) {
    const fx_look *l = g_seen[k].fl;
    if (!l || (l->family != FX_FAM_STREAK && l->family != FX_FAM_ENERGY)) return;
    float len = (float)(l->trail ? l->trail : 1) * g_seen[k].tick_speed;
    if ((float)g_seen[k].travelled < len) len = (float)g_seen[k].travelled;
    if (len <= 1.0f) return;
    for (int i = 0; i < FX_GHOSTS; ++i) {
        if (g_ghost[i].on) continue;
        g_ghost[i].on = 1;
        memcpy(g_ghost[i].head, head, sizeof g_ghost[i].head);
        memcpy(g_ghost[i].dir, g_seen[k].dir, sizeof g_ghost[i].dir);
        g_ghost[i].len = len;
        g_ghost[i].shrink = g_seen[k].tick_speed / FX_TICK_MS;
        g_ghost[i].width = (float)l->width * 2.0f;
        g_ghost[i].sprite = l->sprite;
        for (int c = 0; c < 3; ++c) g_ghost[i].rgb[c] = ps1(l->tint[c]);
        return;
    }
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

/* The beam is an arms-model mesh the game shows and hides every tick while
 * the swing runs (PP 0x800764C0: ground ticks 0-20, air slash 0-10; ACG1PLUS
 * mapping/blade.md 4a, 8), so it strobes at half the tick rate. Drawn on
 * the even ticks of the lit window; past it, off. */
static float beam_strobe(const acr_gen1_fighter *f) {
    const int tick = f->swing > 0 ? (f->swing - 1) * 22 / 1000 : 0;
    if (tick > (f->air_swing ? 10 : 20)) return 0.0f;
    return (tick & 1) ? 0.0f : 1.0f;
}

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
        /* A frame can be several steps long: look as far as the shot could
         * have gone since it was last drawn (two and a half Gen 1 ticks). */
        if (!g_seen[k].missile && g_seen[k].tick_speed > 0.0f) {
            const float reach = g_seen[k].tick_speed * 2.5f;
            if (reach * reach > d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
                for (int i = 0; i < 3; ++i) d[i] = g_seen[k].dir[i] * reach;
        }
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
            ghost_add(k, q);
            model_blast(k, q);
            if (g_seen[k].wave) chips(t, q, 0x00505014u);          /* waves: 20 chips (0x800858EC) */
            else if (g_seen[k].fl && g_seen[k].fl->debris) chips(t, q, g_seen[k].fl->debris);
            if (disc_hit(k, q)) return;
            if (g_seen[k].missile || (g_seen[k].look && g_seen[k].look->look == LOOK_ROCKET)) explosion(q, 1.0f, 0);
            else hit_ac(q, g_seen[k].energy);
            return;
        }
    }
    const int blast = g_seen[k].missile || (g_seen[k].look && g_seen[k].look->look == LOOK_ROCKET);
    const int out = !g_seen[k].missile && g_seen[k].travelled + g_seen[k].speed >= g_seen[k].range;
    float q[3];
    for (int i = 0; i < 3; ++i) q[i] = p[i] + st[i] * 0.5f;
    ghost_add(k, out ? p : q);
    if (g_seen[k].fl && g_seen[k].fl->timed && out) { model_blast(k, p); return; }   /* the fuse ran out */
    if (g_seen[k].fl && g_seen[k].fl->family >= 0 && out) return;     /* out of range: just gone */
    model_blast(k, blast ? p : q);
    if (disc_hit(k, blast ? p : q)) return;                           /* a wall, the floor, shot down */
    if (blast) { explosion(p, 0.8f, 0); return; }
    if (out) return;
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
            g_seen[k].fl = s->wave ? NULL : disc_look(s->owner, s->weapon);
            g_seen[k].wave = s->wave;
            g_seen[k].tick_speed = (float)(w && w->speed > 0 ? w->speed : ACR_GEN1_UNIT_SHOT);
            g_seen[k].glow_ms = 0;
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
        {
            const float l = sqrtf(st[0] * st[0] + st[1] * st[1] + st[2] * st[2]);
            if (l > 1e-4f) for (int i = 0; i < 3; ++i) g_seen[k].dir[i] = st[i] / l;
        }
        /* B shots' trailing glow (u32 +12 bits 24-25, 0x8008A700: a glow
         * left where the shot was, every tick; how long each lasts is not
         * traced: 4 ticks here). */
        const fx_look *fl = g_seen[k].fl;
        if (fl && fl->family == FX_FAM_BILLBOARD && fl->glow && fl->glow < FX_ATLAS && g_atlas[fl->glow].ok) {
            g_seen[k].glow_ms += dt;
            if (g_seen[k].glow_ms >= FX_TICK_MS) {
                g_seen[k].glow_ms = 0;
                part *q = spawn(SH_COUNT, 1, B_ADD, p, 4 * FX_TICK_MS, (float)fl->glow_size, (float)fl->glow_size * 0.5f,
                                ps1(fl->glow_tint[0]), ps1(fl->glow_tint[1]), ps1(fl->glow_tint[2]), 255);
                q->atlas_first = fl->glow;
                q->atlas_count = 1;
            }
        }
    }
    for (int i = 0; i < FX_GHOSTS; ++i)
        if (g_ghost[i].on && (g_ghost[i].len -= g_ghost[i].shrink * dt) <= 0) g_ghost[i].on = 0;

    for (int k = 0; k < acr_gen1_battle_fighters(b); ++k) {
        if (g_ap_ok && g_ap[k] > 0 && b->f[k].ap <= 0) {
            float feet[3];
            ac_feet(b, k, feet);
            ac_blast(feet);
        }
        /* A blade hit: the spark burst at the AC's middle (blade_sparks). */
        if (g_ap_ok && b->f[k].blade_hit_seq != g_bhit[k] && b->f[k].ap > 0) {
            float mid[3];
            ac_feet(b, k, mid);
            mid[1] += 106.0f;                               /* half the hit cylinder (212) */
            blade_sparks(mid);
            chips(k, mid, 10u << 18 | 20u << 10 | 20u);     /* the line test's 10 chips (0x80076738) */
        }
        g_bhit[k] = b->f[k].blade_hit_seq;
        g_ap[k] = b->f[k].ap;
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

    for (int i = 0; i < FX_CHIPS; ++i) {
        if (!g_chip[i].on) continue;
        g_chip[i].age += dt;
        if (g_chip[i].age >= g_chip[i].life) { g_chip[i].on = 0; continue; }
        g_chip[i].v[1] -= 3.0f / (FX_TICK_MS * FX_TICK_MS) * dt;
        for (int c = 0; c < 3; ++c) g_chip[i].p[c] += g_chip[i].v[c] * dt;
        g_chip[i].ang += g_chip[i].spin * dt;
    }
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

/* The same two with an atlas sprite (its own picture); 0 when the sprite
 * isn't in the import. Each drawn into the batch of its picture, which the
 * caller flushes with flush_tex. */
static int atlas_uv(int sprite, float uv[4], uint32_t *tex) {
    if (sprite < 0 || sprite >= FX_ATLAS || !g_atlas[sprite].ok) return 0;
    const atlas_ent *a = &g_atlas[sprite];
    const float tw = (float)g_tc[a->tex].w, th = (float)g_tc[a->tex].h;
    uv[0] = a->u / tw; uv[1] = a->v / th; uv[2] = (a->u + a->w) / tw; uv[3] = (a->v + a->h) / th;
    *tex = g_tc[a->tex].tex;
    return 1;
}
static void flush_tex(uint32_t tex, int blend) {
    if (!g_n) return;
    acr_render_mesh(g_v, g_n, tex, blend == B_ADD ? ACR_MESH_ADD : ACR_MESH_SEMI);
    g_n = 0;
}
static void billboard_atlas(const float p[3], float size, float spin, const float right[3], const float up[3],
                            int sprite, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    float uv[4];
    uint32_t tex;
    if (!atlas_uv(sprite, uv, &tex)) return;
    const float h = size * 0.5f, cs = cosf(spin) * h, sn = sinf(spin) * h;
    float ax[3], ay[3];
    for (int i = 0; i < 3; ++i) { ax[i] = right[i] * cs + up[i] * sn; ay[i] = up[i] * cs - right[i] * sn; }
    const float c[4][3] = {
        { p[0] - ax[0] + ay[0], p[1] - ax[1] + ay[1], p[2] - ax[2] + ay[2] },
        { p[0] + ax[0] + ay[0], p[1] + ax[1] + ay[1], p[2] + ax[2] + ay[2] },
        { p[0] + ax[0] - ay[0], p[1] + ax[1] - ay[1], p[2] + ax[2] - ay[2] },
        { p[0] - ax[0] - ay[0], p[1] - ax[1] - ay[1], p[2] - ax[2] - ay[2] },
    };
    quad(c, uv[0], uv[1], uv[2], uv[3], cr, cg, cb, ca);
}
static void streak_atlas(const float tail[3], const float head[3], float width, const float eye[3],
                         int sprite, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    float uv[4];
    uint32_t tex;
    if (!atlas_uv(sprite, uv, &tex)) return;
    float d[3] = { head[0] - tail[0], head[1] - tail[1], head[2] - tail[2] };
    const float mid[3] = { (head[0] + tail[0]) * 0.5f, (head[1] + tail[1]) * 0.5f, (head[2] + tail[2]) * 0.5f };
    float e[3] = { eye[0] - mid[0], eye[1] - mid[1], eye[2] - mid[2] }, sd[3];
    norm(d);
    norm(e);
    cross(d, e, sd);
    norm(sd);
    const float h = width * 0.5f;
    const float c[4][3] = {
        { head[0] - sd[0] * h, head[1] - sd[1] * h, head[2] - sd[2] * h },
        { head[0] + sd[0] * h, head[1] + sd[1] * h, head[2] + sd[2] * h },
        { tail[0] + sd[0] * h, tail[1] + sd[1] * h, tail[2] + sd[2] * h },
        { tail[0] - sd[0] * h, tail[1] - sd[1] * h, tail[2] - sd[2] * h },
    };
    quad(c, uv[0], uv[1], uv[2], uv[3], cr, cg, cb, ca);
}
static uint32_t atlas_tex(int sprite) {
    return sprite >= 0 && sprite < FX_ATLAS && g_atlas[sprite].ok ? g_tc[g_atlas[sprite].tex].tex : 0;
}

/* Atlas parts (hit flashes, trailing glows), one picture at a time. */
static void draw_atlas_parts(const float right[3], const float up[3]) {
    for (int t = 0; t < g_ntc; ++t) {
        for (int i = 0; i < FX_PARTS; ++i) {
            const part *q = &g_part[i];
            if (!q->on || q->sheet != SH_COUNT || q->age < 0) continue;
            const float tt = q->age / q->life;
            int frame = (int)(tt * q->atlas_count);
            if (frame >= q->atlas_count) frame = q->atlas_count - 1;
            const int sp = q->atlas_first + frame;
            if (sp >= FX_ATLAS || !g_atlas[sp].ok || g_atlas[sp].tex != t) continue;
            /* the atlas colour (128 = 1.0) times the part's own */
            const atlas_ent *a = &g_atlas[sp];
            const float k = q->atlas_count > 1 ? 1.0f : (tt > 0.6f ? (1.0f - tt) / 0.4f : 1.0f);
            billboard_atlas(q->p, q->s0 + (q->s1 - q->s0) * tt, q->spin, right, up, sp,
                            (uint8_t)(ps1(a->r) * q->r / 255), (uint8_t)(ps1(a->g) * q->g / 255),
                            (uint8_t)(ps1(a->b) * q->b / 255), (uint8_t)(q->a * k));
        }
        flush_tex(g_tc[t].tex, B_ADD);
    }
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

    /* Debris chips: solid triangles, lit from above by their facing (one
     * plain texel of the glow sheet's middle). */
    {
        const float tu = 0.5f, tv = 0.5f;
        for (int i = 0; i < FX_CHIPS; ++i) {
            if (!g_chip[i].on) continue;
            const float *ax = g_chip[i].axis, cs = cosf(g_chip[i].ang), sn = sinf(g_chip[i].ang);
            float w[3][3];
            for (int v = 0; v < 3; ++v) {            /* Rodrigues: turn the corner about the axis */
                const float *o = g_chip[i].tri[v];
                const float d = ax[0] * o[0] + ax[1] * o[1] + ax[2] * o[2];
                float cr[3];
                cross(ax, o, cr);
                for (int c = 0; c < 3; ++c) w[v][c] = g_chip[i].p[c] + o[c] * cs + cr[c] * sn + ax[c] * d * (1.0f - cs);
            }
            const float e1[3] = { w[1][0] - w[0][0], w[1][1] - w[0][1], w[1][2] - w[0][2] };
            const float e2[3] = { w[2][0] - w[0][0], w[2][1] - w[0][1], w[2][2] - w[0][2] };
            float nrm[3];
            cross(e1, e2, nrm);
            norm(nrm);
            const float shade = 0.45f + 0.55f * fabsf(nrm[1]);
            const float left = 1.0f - g_chip[i].age / g_chip[i].life;
            const uint8_t a = (uint8_t)(255 * (left < 0.25f ? left * 4.0f : 1.0f));
            const float c4[4][3] = { { w[0][0], w[0][1], w[0][2] }, { w[1][0], w[1][1], w[1][2] },
                                     { w[2][0], w[2][1], w[2][2] }, { w[2][0], w[2][1], w[2][2] } };
            quad(c4, tu, tv, tu, tv, (uint8_t)(g_chip[i].rgb[0] * shade), (uint8_t)(g_chip[i].rgb[1] * shade),
                 (uint8_t)(g_chip[i].rgb[2] * shade), a);
        }
        flush(SH_GLOW, B_SEMI);
    }

    /* Smoke first (see-through, drawn over by the glows), then the glows. */
    for (int s = 0; s < SH_COUNT; ++s) draw_parts(s, B_SEMI, right, up);
    for (int s = 0; s < SH_COUNT; ++s) draw_parts(s, B_ADD, right, up);
    draw_atlas_parts(right, up);

    /* The disc's shot looks (fx_look): A bullet and D energy streaks from
     * the head back to a tail that waits `trail` ticks at the muzzle, so
     * they grow to trail x speed and then slide (A update 0x80088674, D
     * 0x8008D584); drawn as a quad of constant screen width, half-width =
     * width x 40 / (depth / 4) pixels at the VS projection (H = 160), which
     * is `width` world units (streak draw 0x800645D8); colour = the table's
     * tint (128 = 1.0), added. B shots: the atlas sprite as a billboard at
     * the head, `width` either side (a guess, the billboard draw 0x80064C68
     * is not traced). Ended streaks shrink into where they stopped. */
    for (int t = 0; t < g_ntc; ++t) {
        const uint32_t tex = g_tc[t].tex;
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
            const acr_gen1_shot *s = &b->shot[k];
            const fx_look *l = s->active && !s->missile && g_seen[k].on ? g_seen[k].fl : NULL;
            if (!l || l->family < 0 || atlas_tex(l->sprite) != tex) continue;
            float p[3];
            shot_pos(s, p);
            const float *d = g_seen[k].dir;
            if (l->family == FX_FAM_STREAK || l->family == FX_FAM_ENERGY) {
                float len = (float)(l->trail ? l->trail : 1) * g_seen[k].tick_speed;
                if ((float)s->travelled < len) len = (float)s->travelled;
                if (len < 1.0f) len = 1.0f;
                const float tail[3] = { p[0] - d[0] * len, p[1] - d[1] * len, p[2] - d[2] * len };
                streak_atlas(tail, p, (float)l->width * 2.0f, eye, l->sprite, ps1(l->tint[0]), ps1(l->tint[1]), ps1(l->tint[2]), 255);
            } else if (l->family == FX_FAM_BILLBOARD) {
                const atlas_ent *a = &g_atlas[l->sprite];
                billboard_atlas(p, (float)l->width * 2.0f, 0, right, up, l->sprite, ps1(a->r), ps1(a->g), ps1(a->b), 255);
            }
        }
        for (int i = 0; i < FX_GHOSTS; ++i) {
            if (!g_ghost[i].on || atlas_tex(g_ghost[i].sprite) != tex) continue;
            const float *h = g_ghost[i].head, *d = g_ghost[i].dir, len = g_ghost[i].len;
            const float tail[3] = { h[0] - d[0] * len, h[1] - d[1] * len, h[2] - d[2] * len };
            streak_atlas(tail, h, g_ghost[i].width, eye, g_ghost[i].sprite, g_ghost[i].rgb[0], g_ghost[i].rgb[1], g_ghost[i].rgb[2], 255);
        }
        flush_tex(tex, B_ADD);
    }

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
            const fx_look *dl = g_seen[k].on ? g_seen[k].fl : NULL;
            if (dl && (dl->family == FX_FAM_STREAK || dl->family == FX_FAM_ENERGY || dl->family == FX_FAM_BILLBOARD) &&
                atlas_tex(dl->sprite)) continue;        /* drawn above, from the disc */
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
        if (g_seen[k].fl && g_seen[k].fl->family >= 0 && atlas_tex(g_seen[k].fl->sprite)) continue;   /* the disc's look has its own glow */
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
    /* Lit shots (model shots with a light, timed shells): the light they
     * cast in flight, as a soft glow round them (C +0x0C / +0x10). */
    {
        const rect all = { 0, 0, (float)g_tex_w[SH_GLOW], (float)g_tex_h[SH_GLOW] };
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
            const fx_look *fl = g_seen[k].on && b->shot[k].active ? g_seen[k].fl : NULL;
            if (!fl || !fl->light) continue;
            float p[3];
            shot_pos(&b->shot[k], p);
            const float sz = (float)fl->light * 0.18f * flick;
            billboard(p, sz, 0, right, up, SH_GLOW, all, ps1(fl->light_tint[0]), ps1(fl->light_tint[1]),
                      ps1(fl->light_tint[2]), 110);
        }
    }
    flush(SH_GLOW, B_ADD);

    /* The blade: a light beam out of the hand along the forearm, its core
     * white, with a fading sheet over the last ~0.1 s of the slash. */
    for (int k = 0; k < acr_gen1_battle_fighters(b); ++k) {
        const acr_gen1_fighter *f = &b->f[k];
        const float g = f->ap > 0 && has_blade(f) ? blade_glow_k(k, f->swing) * beam_strobe(f) : 0.0f;
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
        const float g = f->ap > 0 && has_blade(f) ? blade_glow_k(k, f->swing) * beam_strobe(f) : 0.0f;
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
