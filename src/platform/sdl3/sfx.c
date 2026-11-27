/* Fight sounds (sfx.h). Which sample plays for what comes from listening
 * to them (the user's labels) and from the game's code
 * (docs/research/gen1-sound.md, docs/feel/sound-hud.md): the movement
 * sounds, their volumes (0-127, as the game's), pitches, distances and the
 * voice priorities are the VS engine's (FDAT 209); the warnings' timing is
 * battle_warn.c. Shot volumes are still set by ear. */
#include "sfx.h"
#include "battle_warn.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

/* Samples (VAB sample numbers, sfx/NN.wav). */
enum {
    S_FOOTSTEP = 1, S_QUAD_STEP = 2 /* sound 18 */, S_TREADS = 4 /* sound 16, loops */, S_EXPLOSION = 5, S_GUN_LOCKED = 10 /* sound 42 (VS 0x80080470) */, S_LASER = 7, S_BOOST_START = 9, S_LOCK = 11, S_CANNON = 12,
    S_LANDING = 13, S_MACHINE_GUN = 14, S_MISSILE = 18, S_PULSE = 20, S_RIFLE = 21, S_BLADE = 23,
    S_PERIMETER = 27, S_AP_LOW = 28, S_RED_ZONE = 29, S_DRAINED = 30, S_BOOST = 31,
    S_LOCK_NEW = 32, S_LOCK_FULL = 33, S_BIG_EXPLOSION = 34, S_LEG_HUM = 37 /* sound 13, loops */,
    S_COUNT = 39
};
/* The hard landing's thud is sound 5: the cannon's sample, #12. */
#define S_HARD_LANDING S_CANNON

/* Each sample's priority: the weight of the sound id that plays it (FDAT
 * 205's id table, record +8). When every voice is busy a new sound takes
 * the lowest-weight one below its own weight (VS 0x80053154); the two
 * area / AP alarms (5,000) beat everything. 0 = 1,000. */
static const uint16_t k_weight[S_COUNT] = {
    [S_FOOTSTEP] = 200, [S_EXPLOSION] = 500, [S_BOOST_START] = 900, [S_MACHINE_GUN] = 200,
    [S_MISSILE] = 200, [19] = 200, [S_RIFLE] = 200, [S_BOOST] = 200, [S_BIG_EXPLOSION] = 600,
    [S_PERIMETER] = 5000, [S_AP_LOW] = 5000,
};
static int weight_of(int n) { return n > 0 && n < S_COUNT && k_weight[n] ? k_weight[n] : 1000; }

#define RATE 22050
#define VOICES 32      /* the game has 22 for the fight; the rest are for the menus' own sounds */

typedef struct sample { float *pcm; int frames, loop_start; } sample;
static sample g_s[S_COUNT];

static sample g_ui[SFX_UI_COUNT];      /* menu and screen sounds */

typedef struct voice {
    const sample *s;
    double pos, rate;     /* rate: samples a frame (1 = 22050 Hz; the game's pitch offsets move it) */
    float l, r;
    int key;              /* > 0: a held loop, found again by key */
    int weight;           /* priority (k_weight) */
    int n;                /* fight bank sample, 0 for menu sounds */
} voice;
static voice g_v[VOICES];
static SDL_AudioStream *g_stream;
static SDL_AudioDeviceID g_dev;
static float g_master = 0.7f;   /* fight sounds */
static float g_menu = 0.7f;     /* menu and screen sounds */
static int g_env_volume;        /* ACR_SFX_VOLUME given: it wins over the settings */
static int g_log, g_tried, g_ok;
/* Last time the fight was heard (sfx_update). Held loops (boost) only live
 * while a fight is updating them: once the fight stops calling in (menus,
 * results, a screen opened mid-round) the mixer lets them go, so a boost
 * held as the fight ended can't keep playing over the menus. */
static uint64_t g_fight_ns;
#define LOOP_TIMEOUT_NS 200000000ull

/* ---- the mixer (the audio thread; the stream's lock guards g_v) ---- */

static void SDLCALL feed(void *user, SDL_AudioStream *stream, int additional, int total) {
    (void)user; (void)total;
    float buf[2 * 512];
    if (SDL_GetTicksNS() - g_fight_ns > LOOP_TIMEOUT_NS)
        for (int i = 0; i < VOICES; ++i) if (g_v[i].key) { g_v[i].s = NULL; g_v[i].key = 0; }
    while (additional > 0) {
        const int frames = SDL_min(additional / 8 + 1, 512);
        SDL_memset(buf, 0, sizeof(float) * 2 * (size_t)frames);
        for (int i = 0; i < VOICES; ++i) {
            voice *v = &g_v[i];
            if (!v->s) continue;
            for (int f = 0; f < frames; ++f) {
                int p = (int)v->pos;
                if (p >= v->s->frames) {
                    if (!(v->s->loop_start >= 0 && v->key)) { v->s = NULL; break; }
                    v->pos = (double)v->s->loop_start + (v->pos - (double)v->s->frames);
                    p = (int)v->pos;
                    if (p < v->s->loop_start || p >= v->s->frames) { v->pos = v->s->loop_start; p = v->s->loop_start; }
                }
                const float x = v->s->pcm[p];
                buf[2 * f] += x * v->l;
                buf[2 * f + 1] += x * v->r;
                v->pos += v->rate > 0 ? v->rate : 1.0;
            }
        }
        for (int f = 0; f < 2 * frames; ++f) buf[f] = SDL_clamp(buf[f], -1.0f, 1.0f);
        SDL_PutAudioStreamData(stream, buf, (int)(sizeof(float) * 2 * (size_t)frames));
        additional -= 8 * frames;
    }
}

static int load_wav(int n, const char *dir) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/sfx/%02d.wav", dir, n);
    SDL_AudioSpec spec;
    Uint8 *data = NULL;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path, &spec, &data, &len)) return 0;
    if (spec.format != SDL_AUDIO_S16LE || spec.channels != 1) { SDL_free(data); return 0; }
    const int frames = (int)(len / 2);
    float *pcm = SDL_malloc(sizeof(float) * (size_t)(frames ? frames : 1));
    if (!pcm) { SDL_free(data); return 0; }
    const Sint16 *s = (const Sint16 *)data;
    for (int i = 0; i < frames; ++i) pcm[i] = (float)s[i] / 32768.0f;
    SDL_free(data);
    g_s[n].pcm = pcm;
    g_s[n].frames = frames;
    g_s[n].loop_start = -1;
    return 1;
}

static int load_named(const char *dir, const char *name, sample *out) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/sfx/%s.wav", dir, name);
    SDL_AudioSpec spec;
    Uint8 *data = NULL;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path, &spec, &data, &len)) return 0;
    if (spec.format != SDL_AUDIO_S16LE || spec.channels != 1 || len < 2) { SDL_free(data); return 0; }
    const int frames = (int)(len / 2);
    float *pcm = SDL_malloc(sizeof(float) * (size_t)frames);
    if (!pcm) { SDL_free(data); return 0; }
    const Sint16 *s = (const Sint16 *)data;
    for (int i = 0; i < frames; ++i) pcm[i] = (float)s[i] / 32768.0f;
    SDL_free(data);
    out->pcm = pcm;
    out->frames = frames;
    out->loop_start = -1;
    return 1;
}

/* sounds.toml: the loop points (n = .. then loop_start = ..). */
static void load_loops(const char *dir) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/sfx/sounds.toml", dir);
    size_t size = 0;
    char *t = SDL_LoadFile(path, &size);
    if (!t) return;
    int n = -1;
    for (char *line = t; line && *line;) {
        char *next = SDL_strchr(line, '\n');
        if (next) *next++ = 0;
        int v;
        if (SDL_sscanf(line, "n = %d", &v) == 1) n = v;
        else if (SDL_sscanf(line, "loop_start = %d", &v) == 1 && n > 0 && n < S_COUNT && g_s[n].pcm)
            g_s[n].loop_start = v < g_s[n].frames ? v : -1;
        line = next;
    }
    SDL_free(t);
}

int sfx_open(const char *import_dir) {
    if (g_tried) return g_ok;
    g_tried = 1;
    const char *e = SDL_getenv("ACR_SFX_VOLUME");
    if (e) { g_master = g_menu = SDL_clamp(SDL_atoi(e), 0, 100) / 100.0f; g_env_volume = 1; }
    g_log = SDL_getenv("ACR_SFX_LOG") != NULL;
    if (g_env_volume && g_master <= 0) return 0;   /* ACR_SFX_VOLUME=0: no audio at all */
    int any = 0;
    for (int n = 1; n < S_COUNT; ++n) any |= load_wav(n, import_dir);
    static const char *const k_menu[4] = { "menu-back", "menu-cursor", "menu-ok", "menu-equip" };
    for (int k = 0; k < 4; ++k) any |= load_named(import_dir, k_menu[k], &g_ui[k]);
    if (!any) return 0;
    load_loops(import_dir);
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
#ifdef SDL_PLATFORM_ANDROID
    /* Android: a roomier buffer, so the mixer never runs dry (crackles). */
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
#endif
    /* The mix is 22,050 Hz (the game's); the device stays at its own rate
     * (48,000 on most phones) and SDL converts. Asking the device for
     * 22,050 itself gave a crunchy, underrunning path on Android. */
    const SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, RATE };
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!dev) return 0;
    g_stream = SDL_CreateAudioStream(&spec, NULL);
    if (!g_stream) { SDL_CloseAudioDevice(dev); return 0; }
    if (!SDL_SetAudioStreamGetCallback(g_stream, feed, NULL) || !SDL_BindAudioStream(dev, g_stream)) {
        SDL_DestroyAudioStream(g_stream); g_stream = NULL; SDL_CloseAudioDevice(dev); return 0;
    }
    g_dev = dev;
    g_ok = 1;
    return 1;
}

void sfx_close(void) {
    if (g_stream) SDL_DestroyAudioStream(g_stream);
    g_stream = NULL;
    if (g_dev) SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    for (int n = 0; n < S_COUNT; ++n) { SDL_free(g_s[n].pcm); g_s[n].pcm = NULL; }
    for (int n = 0; n < SFX_UI_COUNT; ++n) { SDL_free(g_ui[n].pcm); g_ui[n].pcm = NULL; }
    g_ok = 0;
}

/* ---- starting and holding sounds (main thread) ---- */

static uint64_t g_last_start[S_COUNT];   /* ms: the game plays a sound once a frame at most */
static float g_clock_ms;

/* The game's pitch offset (sixteenths of a semitone, record note + offset /
 * 16) as a playback rate. */
static double pitch_rate(int sixteenths) { return sixteenths ? pow(2.0, (double)sixteenths / (16.0 * 12.0)) : 1.0; }

/* A voice for a new sound of weight w (VS 0x80053154): a free one; else one
 * already playing the same sample (it restarts); else the lowest-weight
 * one that is not held, if lower than w; else none (the sound is dropped).
 * Under the stream's lock. */
static int pick_voice(int n, int w) {
    for (int i = 0; i < VOICES; ++i) if (!g_v[i].s) return i;
    for (int i = 0; i < VOICES; ++i) if (n && g_v[i].n == n && !g_v[i].key) return i;
    int pick = -1, low = w;
    for (int i = 0; i < VOICES; ++i)
        if (!g_v[i].key && g_v[i].weight < low) { low = g_v[i].weight; pick = i; }
    return pick;
}

static void play_ex(int n, float vol, float pan, int sixteenths, int weight) {
    if (!g_ok || n <= 0 || n >= S_COUNT || !g_s[n].pcm || vol <= 0.01f) return;
    if (g_last_start[n] && g_clock_ms - (float)g_last_start[n] < 30.0f) return;   /* once a frame (0x80045178) */
    g_last_start[n] = (uint64_t)g_clock_ms + 1;
    if (g_log) SDL_Log("sfx #%02d vol %.2f pan %+.2f pitch %+d", n, (double)vol, (double)pan, sixteenths);
    vol *= g_master;
    SDL_LockAudioStream(g_stream);
    const int pick = pick_voice(n, weight);
    if (pick >= 0) {
        voice *v = &g_v[pick];
        v->s = &g_s[n]; v->pos = 0; v->key = 0; v->n = n; v->weight = weight;
        v->rate = pitch_rate(sixteenths);
        v->l = vol * SDL_min(1.0f, 1.0f - pan);
        v->r = vol * SDL_min(1.0f, 1.0f + pan);
    }
    SDL_UnlockAudioStream(g_stream);
}

static void play(int n, float vol, float pan) { play_ex(n, vol, pan, 0, weight_of(n)); }

/* ---- menu and screen sounds ---- */

void sfx_set_ui(int id, float *pcm, int frames) {
    if (id < 0 || id >= SFX_UI_COUNT) { SDL_free(pcm); return; }
    if (g_stream) SDL_LockAudioStream(g_stream);
    for (int i = 0; i < VOICES; ++i) if (g_v[i].s == &g_ui[id]) g_v[i].s = NULL;
    SDL_free(g_ui[id].pcm);
    g_ui[id].pcm = pcm;
    g_ui[id].frames = pcm ? frames : 0;
    g_ui[id].loop_start = -1;
    if (g_stream) SDL_UnlockAudioStream(g_stream);
}

const float *sfx_sample(int n, int *frames) {
    if (n <= 0 || n >= S_COUNT || !g_s[n].pcm) return NULL;
    *frames = g_s[n].frames;
    return g_s[n].pcm;
}

void sfx_set_volume(float fight, float menu) {
    if (g_env_volume) return;
    g_master = SDL_clamp(fight, 0.0f, 1.0f);
    g_menu = SDL_clamp(menu, 0.0f, 1.0f);
}

void sfx_ui(int id) {
    if (!g_ok || id < 0 || id >= SFX_UI_COUNT || !g_ui[id].pcm) return;
    if (g_log) SDL_Log("sfx ui %d", id);
    const float vol = g_menu;
    SDL_LockAudioStream(g_stream);
    int pick = -1;
    for (int i = 0; i < VOICES && pick < 0; ++i) if (g_v[i].s == &g_ui[id] && id <= SFX_UI_EQUIP) pick = i;   /* a click restarts */
    for (int i = 0; i < VOICES && pick < 0; ++i) if (!g_v[i].s) pick = i;
    if (pick < 0) {
        double far = -1;
        for (int i = 0; i < VOICES; ++i) if (!g_v[i].key && g_v[i].pos > far) { far = g_v[i].pos; pick = i; }
    }
    if (pick >= 0) {
        voice *v = &g_v[pick];
        v->s = &g_ui[id]; v->pos = 0; v->key = 0; v->n = 0; v->weight = 1000; v->rate = 1.0;
        v->l = v->r = vol;
    }
    SDL_UnlockAudioStream(g_stream);
}

void sfx_ui_stop(int id) {
    if (!g_ok || id < 0 || id >= SFX_UI_COUNT) return;
    SDL_LockAudioStream(g_stream);
    for (int i = 0; i < VOICES; ++i) if (g_v[i].s == &g_ui[id]) g_v[i].s = NULL;
    SDL_UnlockAudioStream(g_stream);
}

/* A held sound (boost, turning, the drive): started, moved or stopped by
 * key, as the game's play calls with a handle (VS 0x80053284: a running
 * voice with that handle only gets its new volume and pitch). Held voices
 * are never taken for another sound. */
static void hold_ex(int key, int n, int on, float vol, float pan, int sixteenths) {
    if (!g_ok || n <= 0 || n >= S_COUNT || !g_s[n].pcm) return;
    vol *= g_master;
    SDL_LockAudioStream(g_stream);
    voice *v = NULL;
    for (int i = 0; i < VOICES && !v; ++i) if (g_v[i].s && g_v[i].key == key) v = &g_v[i];
    if (!on) { if (v) { v->s = NULL; v->key = 0; } }
    else {
        if (!v) {
            const int pick = pick_voice(0, weight_of(n));
            if (pick >= 0) {
                v = &g_v[pick];
                v->s = &g_s[n]; v->pos = 0; v->key = key; v->n = n; v->weight = weight_of(n);
                if (g_log) SDL_Log("sfx #%02d loop on", n);
            }
        }
        if (v) {
            v->rate = pitch_rate(sixteenths);
            v->l = vol * SDL_min(1.0f, 1.0f - pan);
            v->r = vol * SDL_min(1.0f, 1.0f + pan);
        }
    }
    SDL_UnlockAudioStream(g_stream);
}

/* ---- watching the fight ---- */

static const char (*g_types[ACR_GEN1_MAX_FIGHTERS])[24];
static int g_type_count[ACR_GEN1_MAX_FIGHTERS];

void sfx_set_weapons(int fighter, const char (*types)[24], int count) {
    if (fighter < 0 || fighter >= ACR_GEN1_MAX_FIGHTERS) return;
    g_types[fighter] = types;
    g_type_count[fighter] = count;
}

static int has(const char *s, const char *word) { return s && SDL_strstr(s, word) != NULL; }

/* A shot's firing sound and whether it ends in a blast. The game picks the
 * sound by weapon (AC1Recomp mapping/weapon-map.md, "Fire snd" sound ids),
 * which the labels (project doc sound-labels.md) turn into samples:
 *   id 4 -> #21 (rifle), 48 -> #15 (machine gun), 50 -> #20 (pulse),
 *   51 -> #07 (laser), 53 -> #22 (heavy guns, rockets, flame),
 *   0 -> #12 (cannon), 2 -> #18 (missile launch).
 * The type alone doesn't settle every weapon (machine guns, laser and
 * plasma cannons, grenade launchers split), so those go by name first; any
 * other weapon (Phantasma's own) by its type, as the game's majority. */
enum { S_GUN_HEAVY = 22, S_MG = 15 };
static const struct { const char *name; uint8_t sample; } k_fire_by_name[] = {
    { "WG-AR1000", S_RIFLE }, { "WA-Finger", S_RIFLE },               /* machine guns on id 4 */
    { "WC-GN230", S_CANNON }, { "AW-DC/2", S_CANNON },                /* grenade launchers on id 0 */
    { "WX-C/4", S_CANNON }, { "WX-ED2", S_CANNON },                   /* two-slot energy cannons, id 0 */
    { "AW-XC65", S_LASER }, { "WC-XC8000", S_LASER },                 /* id 51 */
    { "WC-01QL", S_GUN_HEAVY }, { "WC-IR24", S_GUN_HEAVY }, { "AW-XC5500", S_GUN_HEAVY },   /* id 53 */
    { "WG-RF/E", S_LASER },
};

static int fire_sound(int owner, int slot, const char *nm, int energy, int missile, int *blast) {
    const char *t = owner >= 0 && owner < ACR_GEN1_MAX_FIGHTERS && g_types[owner] && slot < g_type_count[owner] ? g_types[owner][slot] : NULL;
    *blast = missile || has(t, "ROCKET") || has(t, "BAZOOKA") || has(t, "GRENADE");
    if (missile) return S_MISSILE;
    if (nm && nm[0])
        for (size_t k = 0; k < sizeof k_fire_by_name / sizeof k_fire_by_name[0]; ++k)
            if (!SDL_strcmp(nm, k_fire_by_name[k].name)) return k_fire_by_name[k].sample;
    if (has(t, "DISPEN")) return has(t, "MINE") ? S_GUN_HEAVY : S_RIFLE;   /* bombs, plasma dispenser: id 4 */
    if (has(t, "PULSE")) return S_PULSE;
    if (has(t, "LASER") || has(t, "PLASMA RIFLE")) return S_LASER;
    if (has(t, "ROCKET") || has(t, "BAZOOKA") || has(t, "FLAME") || has(t, "GRENADE") ||
        has(t, "CANNON") || has(t, "PLASMA")) return S_GUN_HEAVY;
    if (has(t, "MACHINE")) return S_MG;
    if (energy) return S_LASER;
    return S_RIFLE;   /* rifles, hand guns, chain, gatling, slug and linear guns: id 4 */
}


/* ---- movement sounds (VS engine, FDAT 209) ----
 *
 * Each animation set has a 20-byte sound record (0x80091340 + set x 20,
 * pointed to by player block +0x44; the same table in all five fight
 * engines): five (frame, sound, volume, 0) entries. Only three records
 * exist, one per leg kind:
 *
 *   legs (humanoid, reverse joint)  (4,  3, 50) (11, 3, 50) (2, 5, 80) (1, 13, 60) (1, 13, 60)
 *   four legs                       (4, 18, 50) (11, 3, 50)    same
 *   caterpillars                    (4, 16, 50) (11, 3, 50)    same    (1, 13, 60) (1, 16, 60)
 *
 * (humanoid sets differ only in the step frames: 3 / 9 or 4 / 14 on some.)
 * Entries 0-1 are the walk cycle's two steps, attached to every walk,
 * strafe and back-step animation (0x8007A9E0 with count 2); entry 2 the
 * hard landing's thud (frame 2 of animation 36, 0x80054ADC); entry 3 the
 * jump take-off (frame 1 of animation 22, 0x8007AD7C); byte 1 of the record
 * is also the drive sound (0x80087108) and byte 0x11 the turning sound
 * (0x8007E1DC / 0x8007E2DC / 0x8007E3E0). */

#define WALK_CYCLE    16      /* frames of the default walk animation (2) */
#define STEP_1_FRAME  4       /* the record's step frames; the event fires a frame after (0x8007DF14 compares frame + 1) */
#define STEP_2_FRAME  11
#define TICK_MS       (1000.0f / 22.0f)

enum { LEG_TWO, LEG_FOUR, LEG_TREADS };
static int leg_kind(const acr_gen1_fighter *f) {
    if (!f->def) return LEG_TWO;
    if (f->def->no_jump) return LEG_TREADS;
    return f->def->drive_walk ? LEG_FOUR : LEG_TWO;
}

static struct { uint8_t on, blast; int32_t travelled; uint16_t age; float p[3]; } g_seen[ACR_GEN1_MAX_SHOTS];
static struct {
    int32_t ap;
    uint8_t boosting, state, locked, locks, tones, swinging, swing_frame;
    int32_t sx, sz;          /* shown position last update (footsteps by distance) */
    float walk;              /* walk cycle phase, 0..1 */
    float drive;             /* the drive sound's speed (AC +0x116): toward the ground speed */
    float hard_land_ms;      /* > 0: the hard landing's thud is due at this clock */
    uint8_t anim;            /* the sim's animation and frame last update (footsteps) */
    uint16_t anim_frame;
} g_was[ACR_GEN1_MAX_FIGHTERS];
static int g_was_ok;
static uint64_t g_last_ns;
static float g_ear[3], g_ear_yaw;

void sfx_reset(void) {
    SDL_memset(g_seen, 0, sizeof g_seen);
    SDL_memset(g_was, 0, sizeof g_was);
    g_was_ok = 0;
    if (g_ok) {
        SDL_LockAudioStream(g_stream);
        SDL_memset(g_v, 0, sizeof g_v);
        SDL_UnlockAudioStream(g_stream);
    }
}

/* Volume (0..1) and pan of a sound at p (drawn world) for the listening AC,
 * by the game's 3D rule (VS 0x80052D9C / 0x80052C88): silent at `cut` or
 * further, else volume x (fade - d) / fade, nothing under 30 of 127. The
 * game measures from the camera, which rides about 650 behind the AC. */
#define CUT_NEAR 8000.0f
#define FADE_NEAR 10000.0f
#define CUT_FAR 12000.0f      /* explosions (ids 6, 7) carry half as far again */
#define FADE_FAR 14000.0f
static void place3d(const float p[3], float gvol, float cut, float fade, float *vol, float *pan) {
    const float dx = p[0] - g_ear[0], dy = p[1] - g_ear[1], dz = p[2] - g_ear[2];
    const float flat = sqrtf(dx * dx + dy * dy + dz * dz);
    const float d = sqrtf(flat * flat + 650.0f * 650.0f);
    float v = d >= cut ? 0.0f : gvol * (fade - d) / fade;
    if (v < 30.0f) v = 0.0f;
    if (v > 127.0f) v = 127.0f;
    *vol = v / 127.0f;
    /* the engine's right is (cos, 0, -sin) of the facing */
    *pan = flat > 1.0f ? 0.6f * (dx * cosf(g_ear_yaw) - dz * sinf(g_ear_yaw)) / flat : 0.0f;
}

/* At p, game volume 0-127, pitch offset in 1/16 semitones. */
static void play_at_ex(int n, const float p[3], float gvol, int sixteenths, int weight, int far) {
    float v, pan;
    place3d(p, gvol, far ? CUT_FAR : CUT_NEAR, far ? FADE_FAR : FADE_NEAR, &v, &pan);
    play_ex(n, v, pan, sixteenths, weight);
}
static void play_at(int n, const float p[3], float gvol) { play_at_ex(n, p, gvol, 0, weight_of(n), 0); }

static void hold_at(int key, int n, int on, const float p[3], float gvol, int sixteenths) {
    float v = 0, pan = 0;
    if (on) place3d(p, gvol, CUT_NEAR, FADE_NEAR, &v, &pan);
    hold_ex(key, n, on && v > 0.0f, v, pan, sixteenths);
}

static void ac_pos(const acr_gen1_battle *b, int k, float p[3]) {
    const acr_gen1_ac *ac = &b->f[k].ac;
    p[0] = (float)ac->sx / 64.0f; p[1] = -(float)ac->sy / 64.0f + 106.0f; p[2] = (float)ac->sz / 64.0f;
}

/* Held-sound keys, one set per fighter. */
enum { KEY_BOOST = 1, KEY_TURN = 1 + ACR_GEN1_MAX_FIGHTERS, KEY_DRIVE = 1 + 2 * ACR_GEN1_MAX_FIGHTERS };

void sfx_update(const acr_gen1_battle *b, const sfx_listen *l, uint64_t now_ns) {
    g_fight_ns = SDL_GetTicksNS();
    float dt = g_last_ns ? (float)(now_ns - g_last_ns) / 1e6f : 0.0f;
    if (dt > 100.0f) dt = 100.0f;
    g_last_ns = now_ns;
    g_clock_ms += dt;
    if (!g_ok) return;
    const float ticks = dt / TICK_MS;
    const int me = l->view >= 0 && l->view < ACR_GEN1_MAX_FIGHTERS ? l->view : 0;
    ac_pos(b, me, g_ear);
    g_ear_yaw = (float)b->f[me].ac.syaw * (6.2831853f / 4096.0f);

    /* Shots: a new one fires, a rocket or missile ending is a blast. */
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        const int restarted = g_seen[k].on && s->active &&
            (s->travelled < g_seen[k].travelled || (s->missile && s->age < g_seen[k].age));
        if (g_seen[k].on && (!s->active || restarted)) {
            if (g_seen[k].blast) play_at_ex(S_BIG_EXPLOSION, g_seen[k].p, 70, 0, weight_of(S_BIG_EXPLOSION), 1);
            g_seen[k].on = 0;
        }
        if (!s->active) continue;
        const float p[3] = { (float)s->x / 64.0f, -(float)s->y / 64.0f, (float)s->z / 64.0f };
        if (!g_seen[k].on) {
            const int owner = s->owner < ACR_GEN1_MAX_FIGHTERS ? s->owner : 0;
            const acr_gen1_fighter *o = &b->f[owner];
            const int energy = s->weapon < o->weapons && o->weapon[s->weapon].energy;
            int blast = 0;
            const int n = fire_sound(owner, s->weapon, owner < ACR_GEN1_MAX_FIGHTERS && s->weapon < ACR_GEN1_MAX_WEAPONS ? b->f[owner].weapon[s->weapon].name : NULL, energy, s->missile, &blast);
            g_seen[k].on = 1;
            g_seen[k].blast = (uint8_t)blast;
            play_at(n, p, 110);
        }
        g_seen[k].travelled = s->travelled;
        g_seen[k].age = s->age;
        SDL_memcpy(g_seen[k].p, p, sizeof p);
    }

    for (int k = 0; k < acr_gen1_battle_fighters(b); ++k) {
        const acr_gen1_fighter *f = &b->f[k];
        const acr_gen1_ac *ac = &f->ac;
        float p[3];
        ac_pos(b, k, p);
        const int alive = !l->round_over && f->ap > 0;
        const int legs = leg_kind(f);
        const int swinging = f->swing != 0;
        /* The swing's Gen 1 frame (+1; 0 = not swinging): the sound plays on
         * frame 8 of a ground swing, 5 of an air slash (VS 0x8007EC24 /
         * 0x8007F5C8), not on the press. */
        const int swing_frame = swinging ? SDL_min((f->swing - 1) * 22 / 1000 + 1, 250) : 0;
        const int sound_frame = 1 + (f->air_swing ? ACR_GEN1_AIR_SLASH_SOUND_TICK : ACR_GEN1_BLADE_SOUND_TICK);
        const int boost_held = alive && ac->boost_was_down && !ac->drained && ac->energy > 0;
        /* The swing's lunge on frames 3-11 runs the boost routine too
         * (0x8007EBD0 calls 0x8007E698), so it sounds like a boost. */
        const int lunge = alive && swinging && swing_frame >= 4 && swing_frame <= 12;
        const int boosting = boost_held || lunge;
        const int on_ground = ac->state == ACR_GEN1_GROUND;
        const acr_gen1_weapon *w = f->current < f->weapons ? &f->weapon[f->current] : NULL;
        const float ddx = (float)(ac->sx - g_was[k].sx), ddz = (float)(ac->sz - g_was[k].sz);
        const float moved = g_was_ok ? sqrtf(ddx * ddx + ddz * ddz) : 0.0f;
        if (g_was_ok) {
            if (f->ap < g_was[k].ap && f->ap > 0) play_at_ex(S_EXPLOSION, p, 90, 0, weight_of(S_EXPLOSION), 1);  /* a hit (0x800930A0) */
            if (f->ap <= 0 && g_was[k].ap > 0) play_at_ex(S_BIG_EXPLOSION, p, 127, 0, weight_of(S_BIG_EXPLOSION), 1);
            /* Boost (0x8007E698): the first tick plays sound 1 (#09) a
             * fourth lower (-80 / 16 semitones), at 80; from then on the
             * loop below. Nothing plays when it stops: the loop just ends. */
            if (boosting && !g_was[k].boosting) play_at_ex(S_BOOST_START, p, 80, -80, weight_of(S_BOOST_START), 0);
            if (swing_frame >= sound_frame && g_was[k].swing_frame < sound_frame) play_at(S_BLADE, p, 110);
            /* Jump take-off: sound 13 (#37) at 60 on frame 1 of animation 22. */
            if (alive && ac->state == ACR_GEN1_CROUCH && g_was[k].state != ACR_GEN1_CROUCH) play_at(S_LEG_HUM, p, 60);
            /* Landing: soft (state 0x60, 0x80084834) plays sound 14 (#13)
             * at 60 on touching down; hard (0x61, 0x80084C20) plays sound 5
             * (#12, a deep thud) at 80 on frame 2 of the kneel. */
            if (ac->state == ACR_GEN1_LANDING && g_was[k].state != ACR_GEN1_LANDING) {
                if (ac->landing_soft) play_at(S_LANDING, p, 60);
                else g_was[k].hard_land_ms = g_clock_ms + 2.0f * TICK_MS;
            }
            if (k == me && !l->round_over) {
                /* lock beeps: the listening AC's own */
                /* (docs/feel/lockon.md "Sounds"): the build-up beep is #11
                 * (sounds 46 / 47); missiles: a new lock #32 (43), the last
                 * free one #33 (44, the cap is the FCS / weapon limit, the
                 * ammo and 6); guns: locked plays sound 42, sample #10. */
                if (f->tones != g_was[k].tones && !(w && w->kind == ACR_GEN1_MISSILE && f->locks > g_was[k].locks))
                    play(S_LOCK, 0.4f, 0);
                if (w && w->kind == ACR_GEN1_MISSILE) {
                    unsigned cap = w->max_locks ? w->max_locks : 1;
                    if (w->ammo && cap > f->ammo[f->current]) cap = f->ammo[f->current];
                    if (cap > ACR_GEN1_MAX_LOCKS) cap = ACR_GEN1_MAX_LOCKS;
                    if (f->locks > g_was[k].locks)
                        play(f->locks >= cap ? S_LOCK_FULL : S_LOCK_NEW, 0.6f, 0);
                } else if (f->locked && !g_was[k].locked) play(S_GUN_LOCKED, 0.5f, 0);
            }
        }
        if (g_was[k].hard_land_ms > 0 && g_clock_ms >= g_was[k].hard_land_ms) {
            g_was[k].hard_land_ms = 0;
            if (alive) play_at_ex(S_HARD_LANDING, p, 80, 0, 900, 0);
        }

        /* Footsteps: the walk cycle's two steps (record entries 0-1, volume
         * 50), on the walk animation's own frames now that feel/animations
         * is in: the game fires an entry when the frame counter (+0x76)
         * reaches its frame + 1, so frames 5 and 12 of the walk loops (2, 4,
         * 6, 9, 12, 15, 17, 19); the start animations have none. Legs: #01
         * twice; four legs #02 then #01; caterpillars #04 (their tread
         * sound) then #01. Four legs and caterpillars walk in a single pose
         * (docs/feel/animations.md), and a set may have no walk track: those
         * step by distance walked, one 16-frame cycle per 16 ticks at the
         * walking pace. */
        const int walking = alive && on_ground && !boosting;
        const int loop_anim = ac->anim == 2 || ac->anim == 4 || ac->anim == 6 || ac->anim == 9 || ac->anim == 12 ||
                              ac->anim == 15 || ac->anim == 17 || ac->anim == 19;
        const int step1 = legs == LEG_FOUR ? S_QUAD_STEP : legs == LEG_TREADS ? S_TREADS : S_FOOTSTEP;
        const int e1 = STEP_1_FRAME + 1, e2 = STEP_2_FRAME + 1;
        if (walking && loop_anim && f->def && f->def->anim[ac->anim] && f->def->anim[ac->anim]->length > e2) {
            const int now = ac->anim_frame;
            const int was = g_was_ok && g_was[k].anim == ac->anim ? g_was[k].anim_frame : -1;
            if (now != was) {
                for (int e = 0; e < 2; ++e) {
                    const int at = e ? e2 : e1;
                    const int hit = was < 0 ? now >= at && now - 1 < at
                                  : now > was ? was < at && at <= now
                                  : at > was || at <= now;   /* the loop wrapped */
                    if (hit) play_at(e ? S_FOOTSTEP : step1, p, 50);
                }
            }
            g_was[k].walk = 0.0f;
        } else if (walking && moved > 0.5f && f->def) {
            const float stride = (float)acr_gen1_walk_pace(f->def) * (float)WALK_CYCLE;
            const float was = g_was[k].walk;
            const float now = was + (stride > 1.0f ? moved / stride : 0.0f);
            const float f1 = (float)e1 / WALK_CYCLE, f2 = (float)e2 / WALK_CYCLE;
            for (float base = 0.0f; base < now; base += 1.0f) {
                if (was < base + f1 && now >= base + f1) play_at(step1, p, 50);
                if (was < base + f2 && now >= base + f2) play_at(S_FOOTSTEP, p, 50);
            }
            g_was[k].walk = now - floorf(now);
        } else g_was[k].walk = 0.0f;   /* walking starts over from the start animation */

        /* Turning (record byte 0x11, at 40, held while the turn handlers
         * run): legs #37, caterpillars #04. */
        hold_at(KEY_TURN + k, legs == LEG_TREADS ? S_TREADS : S_LEG_HUM, alive && ac->turn_rate != 0, p, 40, 0);

        /* The drive (0x80087108, every tick, record byte 1): volume 50 +
         * speed / 128, pitch up by speed / 128 sixteenths, where the speed
         * (AC +0x116) climbs 512 a tick toward the ground boost's speed
         * (0x800858E4) and falls 256 a tick. Four legs (#02) and
         * caterpillars (#04) drone the whole fight; the two-legged sound
         * is #01, a one-shot, so it can't hold and is left out. */
        {
            const int ground_drive = alive && on_ground && (boosting || (legs != LEG_TWO && moved > 0.5f));
            const float target = ground_drive ? sqrtf((float)ac->vx * (float)ac->vx + (float)ac->vz * (float)ac->vz) : 0.0f;
            float d = g_was[k].drive;
            if (d < target) d = SDL_min(target, d + 512.0f * ticks);
            else d = SDL_max(target, d - 256.0f * ticks);
            g_was[k].drive = d;
            const int up = (int)d >> 7;
            hold_at(KEY_DRIVE + k, legs == LEG_FOUR ? S_QUAD_STEP : S_TREADS, alive && legs != LEG_TWO, p, (float)(50 + up), up);
        }

        /* The boost loop: sound 11 (#31) at 80, three semitones down (-48). */
        hold_at(KEY_BOOST + k, S_BOOST, boosting, p, 80, -48);

        g_was[k].ap = f->ap;
        g_was[k].boosting = (uint8_t)boosting;
        g_was[k].state = ac->state;
        g_was[k].locked = f->locked;
        g_was[k].locks = f->locks;
        g_was[k].tones = f->tones;
        g_was[k].swinging = (uint8_t)swinging;
        g_was[k].swing_frame = (uint8_t)swing_frame;
        g_was[k].sx = ac->sx;
        g_was[k].sz = ac->sz;
        g_was[k].anim = ac->anim;
        g_was[k].anim_frame = ac->anim_frame;
    }
    g_was_ok = 1;

    /* Warnings, the listening AC's own (battle_warn.c times them): area and
     * low AP at 90 (weight 5,000: they always get a voice), the red zone at
     * 100, over-drain at 90. */
    if (!l->round_over && b->f[me].ap > 0) {
        if (l->beeps & BATTLE_WARN_BEEP_PERIMETER) play(S_PERIMETER, 90.0f / 127.0f, 0);
        if (l->beeps & BATTLE_WARN_BEEP_AP_LOW) play(S_AP_LOW, 90.0f / 127.0f, 0);
        if (l->beeps & BATTLE_WARN_BEEP_RED_ZONE) play(S_RED_ZONE, 100.0f / 127.0f, 0);
        if (l->beeps & BATTLE_WARN_BEEP_DRAINED) play(S_DRAINED, 90.0f / 127.0f, 0);
    }
}
