/* Fight sounds (sfx.h). Which sample plays for what comes from listening
 * to them (the user's labels) and from the sound chat's read of the game's
 * code (docs/research/gen1-sound.md); volumes, distances and repeat times
 * are set by ear for now, not read from the game. */
#include "sfx.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

/* Samples (VAB sample numbers, sfx/NN.wav). */
enum {
    S_FOOTSTEP = 1, S_EXPLOSION = 5, S_GUN_LOCKED = 10 /* sound 42 (VS 0x80080470) */, S_LASER = 7, S_BOOST_START = 9, S_LOCK = 11, S_CANNON = 12,
    S_LANDING = 13, S_MACHINE_GUN = 14, S_MISSILE = 18, S_PULSE = 20, S_RIFLE = 21, S_BLADE = 23,
    S_PERIMETER = 27, S_AP_LOW = 28, S_RED_ZONE = 29, S_DRAINED = 30, S_BOOST = 31,
    S_LOCK_NEW = 32, S_LOCK_FULL = 33, S_BIG_EXPLOSION = 34,
    S_COUNT = 39
};

#define RATE 22050
#define VOICES 32

typedef struct sample { float *pcm; int frames, loop_start; } sample;
static sample g_s[S_COUNT];

static sample g_ui[SFX_UI_COUNT];      /* menu and screen sounds */

typedef struct voice {
    const sample *s;
    double pos;
    float l, r;
    int key;              /* > 0: a held loop, found again by key */
} voice;
static voice g_v[VOICES];
static SDL_AudioStream *g_stream;
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
                    if (v->s->loop_start >= 0 && v->key) { v->pos = v->s->loop_start; p = v->s->loop_start; }
                    else { v->s = NULL; break; }
                }
                const float x = v->s->pcm[p];
                buf[2 * f] += x * v->l;
                buf[2 * f + 1] += x * v->r;
                v->pos += 1.0;
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
    const SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, RATE };
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!g_stream) return 0;
    SDL_ResumeAudioStreamDevice(g_stream);
    g_ok = 1;
    return 1;
}

void sfx_close(void) {
    if (g_stream) SDL_DestroyAudioStream(g_stream);
    g_stream = NULL;
    for (int n = 0; n < S_COUNT; ++n) { SDL_free(g_s[n].pcm); g_s[n].pcm = NULL; }
    for (int n = 0; n < SFX_UI_COUNT; ++n) { SDL_free(g_ui[n].pcm); g_ui[n].pcm = NULL; }
    g_ok = 0;
}

/* ---- starting and holding sounds (main thread) ---- */

static uint64_t g_last_start[S_COUNT];   /* ms: the game plays a sound once a frame at most */
static float g_clock_ms;

static void play(int n, float vol, float pan) {
    if (!g_ok || n <= 0 || n >= S_COUNT || !g_s[n].pcm || vol <= 0.01f) return;
    if (g_last_start[n] && g_clock_ms - (float)g_last_start[n] < 30.0f) return;
    g_last_start[n] = (uint64_t)g_clock_ms + 1;
    if (g_log) SDL_Log("sfx #%02d vol %.2f pan %+.2f", n, (double)vol, (double)pan);
    vol *= g_master;
    SDL_LockAudioStream(g_stream);
    int pick = -1;
    for (int i = 0; i < VOICES && pick < 0; ++i) if (!g_v[i].s) pick = i;
    if (pick < 0) {   /* all busy: take the one furthest along that is not a loop */
        double far = -1;
        for (int i = 0; i < VOICES; ++i) if (!g_v[i].key && g_v[i].pos > far) { far = g_v[i].pos; pick = i; }
    }
    if (pick >= 0) {
        voice *v = &g_v[pick];
        v->s = &g_s[n]; v->pos = 0; v->key = 0;
        v->l = vol * SDL_min(1.0f, 1.0f - pan);
        v->r = vol * SDL_min(1.0f, 1.0f + pan);
    }
    SDL_UnlockAudioStream(g_stream);
}

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
        v->s = &g_ui[id]; v->pos = 0; v->key = 0;
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

/* A held loop (boost): started, moved or stopped by key. */
static void hold(int key, int n, int on, float vol, float pan) {
    if (!g_ok || !g_s[n].pcm) return;
    vol *= g_master;
    SDL_LockAudioStream(g_stream);
    voice *v = NULL;
    for (int i = 0; i < VOICES && !v; ++i) if (g_v[i].s && g_v[i].key == key) v = &g_v[i];
    if (!on) { if (v) { v->s = NULL; v->key = 0; } }
    else {
        if (!v) {
            for (int i = 0; i < VOICES && !v; ++i) if (!g_v[i].s) v = &g_v[i];
            if (v) { v->s = &g_s[n]; v->pos = 0; v->key = key; if (g_log) SDL_Log("sfx #%02d loop on", n); }
        }
        if (v) { v->l = vol * SDL_min(1.0f, 1.0f - pan); v->r = vol * SDL_min(1.0f, 1.0f + pan); }
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

static struct { uint8_t on, blast; int32_t travelled; uint16_t age; float p[3]; } g_seen[ACR_GEN1_MAX_SHOTS];
static struct {
    int32_t ap;
    uint8_t boosting, boost_down, state, locked, locks, tones, swinging, red, drained, swing_frame;
} g_was[ACR_GEN1_MAX_FIGHTERS];
static int g_was_ok;
static float g_next_perimeter, g_next_ap_low, g_next_drained;
static uint64_t g_last_ns;
static float g_ear[3], g_ear_yaw;

void sfx_reset(void) {
    SDL_memset(g_seen, 0, sizeof g_seen);
    g_was_ok = 0;
    if (g_ok) {
        SDL_LockAudioStream(g_stream);
        SDL_memset(g_v, 0, sizeof g_v);
        SDL_UnlockAudioStream(g_stream);
    }
}

/* Volume and pan of a sound at p (drawn world) for the listening AC. */
static void place(const float p[3], float *vol, float *pan) {
    const float dx = p[0] - g_ear[0], dy = p[1] - g_ear[1], dz = p[2] - g_ear[2];
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    *vol = 1.0f / (1.0f + d / 2500.0f);
    /* the engine's right is (cos, 0, -sin) of the facing */
    *pan = d > 1.0f ? 0.6f * (dx * cosf(g_ear_yaw) - dz * sinf(g_ear_yaw)) / d : 0.0f;
}

static void play_at(int n, const float p[3], float vol) {
    float v, pan;
    place(p, &v, &pan);
    play(n, vol * v, pan);
}

static void ac_pos(const acr_gen1_battle *b, int k, float p[3]) {
    const acr_gen1_ac *ac = &b->f[k].ac;
    p[0] = (float)ac->sx / 64.0f; p[1] = -(float)ac->sy / 64.0f + 106.0f; p[2] = (float)ac->sz / 64.0f;
}

void sfx_update(const acr_gen1_battle *b, const sfx_listen *l, uint64_t now_ns) {
    g_fight_ns = SDL_GetTicksNS();
    float dt = g_last_ns ? (float)(now_ns - g_last_ns) / 1e6f : 0.0f;
    if (dt > 100.0f) dt = 100.0f;
    g_last_ns = now_ns;
    g_clock_ms += dt;
    if (!g_ok) return;
    const int me = l->view >= 0 && l->view < ACR_GEN1_MAX_FIGHTERS ? l->view : 0;
    ac_pos(b, me, g_ear);
    g_ear_yaw = (float)b->f[me].ac.syaw * (6.2831853f / 4096.0f);

    /* Shots: a new one fires, a rocket or missile ending is a blast. */
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        const int restarted = g_seen[k].on && s->active &&
            (s->travelled < g_seen[k].travelled || (s->missile && s->age < g_seen[k].age));
        if (g_seen[k].on && (!s->active || restarted)) {
            if (g_seen[k].blast) play_at(S_BIG_EXPLOSION, g_seen[k].p, 0.55f);
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
            play_at(n, p, 0.85f);
            /* firing an energy weapon in the red zone */
            if (owner == me && energy && g_was_ok && g_was[me].red && !l->round_over) play(S_RED_ZONE, 0.8f, 0);
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
        const int boosting = !l->round_over && f->ap > 0 && ac->boost_was_down && !ac->drained && ac->energy > 0;
        const int swinging = f->swing != 0;
        /* The swing's Gen 1 frame (+1; 0 = not swinging): the sound plays on
         * frame 8 of a ground swing, 5 of an air slash (VS 0x8007EC24 /
         * 0x8007F5C8), not on the press. */
        const int swing_frame = swinging ? SDL_min((f->swing - 1) * 22 / 1000 + 1, 250) : 0;
        const int sound_frame = 1 + (f->air_swing ? ACR_GEN1_AIR_SLASH_SOUND_TICK : ACR_GEN1_BLADE_SOUND_TICK);
        const int red = ac->energy < l->red_zone && !ac->drained;
        const acr_gen1_weapon *w = f->current < f->weapons ? &f->weapon[f->current] : NULL;
        if (g_was_ok) {
            if (f->ap < g_was[k].ap && f->ap > 0) play_at(S_EXPLOSION, p, 0.8f);         /* a hit */
            if (f->ap <= 0 && g_was[k].ap > 0) play_at(S_BIG_EXPLOSION, p, 1.0f);          /* destroyed */
            if (boosting && !g_was[k].boosting) play_at(S_BOOST_START, p, 0.8f);
            if (swing_frame >= sound_frame && g_was[k].swing_frame < sound_frame) play_at(S_BLADE, p, 0.95f);
            if (ac->state == ACR_GEN1_LANDING && g_was[k].state != ACR_GEN1_LANDING) play_at(S_LANDING, p, 0.7f);
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
                /* the energy warnings */
                if (red && !g_was[k].red) play(S_RED_ZONE, 0.8f, 0);
                else if (red && ac->boost_was_down && !g_was[k].boost_down) play(S_RED_ZONE, 0.8f, 0);
            }
        }
        {
            float v, pan;
            place(p, &v, &pan);
            hold(1 + k, S_BOOST, boosting, 0.45f * v, pan);
        }
        g_was[k].ap = f->ap;
        g_was[k].boosting = (uint8_t)boosting;
        g_was[k].boost_down = ac->boost_was_down;
        g_was[k].state = ac->state;
        g_was[k].locked = f->locked;
        g_was[k].locks = f->locks;
        g_was[k].tones = f->tones;
        g_was[k].swinging = (uint8_t)swinging;
        g_was[k].swing_frame = (uint8_t)swing_frame;
        g_was[k].red = (uint8_t)red;
        g_was[k].drained = ac->drained;
    }
    g_was_ok = 1;

    /* Repeating warnings, the listening AC's own. */
    const acr_gen1_fighter *f = &b->f[me];
    const int live = !l->round_over && f->ap > 0;
    if (live && l->out_of_area) {
        if (g_clock_ms >= g_next_perimeter) { play(S_PERIMETER, 0.9f, 0); g_next_perimeter = g_clock_ms + 900; }
    } else g_next_perimeter = 0;
    if (live && f->ap * 4 < f->ap_max) {
        if (g_clock_ms >= g_next_ap_low) { play(S_AP_LOW, 0.9f, 0); g_next_ap_low = g_clock_ms + 1500; }
    } else g_next_ap_low = 0;
    if (live && f->ac.drained) {
        if (g_clock_ms >= g_next_drained) { play(S_DRAINED, 0.8f, 0); g_next_drained = g_clock_ms + 450; }
    } else g_next_drained = 0;
    (void)S_FOOTSTEP;
}
