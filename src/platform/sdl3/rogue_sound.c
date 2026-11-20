/* Rogue Arena's own sounds (rogue_sound.h), from AC1Recomp
 * (games/master-of-arena/moa_chime.c and moa_sfx.c, release 0.8.4.4.2):
 * the victory sting and the type tick are made here, not taken from the
 * disc; the VS count-in shots and the shutters' slam are the game's rifle
 * (sound 21) and sound 1, reshaped. Every one gets the shared "90s CD-era"
 * finish (a little drive, a dull top, a dark hall echo baked in), so they
 * sit with the game's own (user, 2026-11-18: one consistent style, early
 * polygon / CD era, not cartoony). Mono float PCM at 22050 Hz. */
#include "rogue_sound.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sfx.h"

#define RATE        22050
#define MOA_SFX_RATE RATE
#define TAU         6.2831853f

struct moa_sfx_room {
    float wet;     /* echo level (0.25 typical) */
    float size;    /* hall size (1.0 = the VS sounds' hall) */
    float damp;    /* echo darkness, Hz (3800 typical) */
    float drive;   /* 0 = clean, 1.5 = hot sample */
    float top;     /* top-end roll-off, Hz (0 = none) */
};

/* ---- the shared finish (moa_sfx.c) ---- */

static const struct moa_sfx_room moa_sfx_hall = { 0.28f, 1.0f, 3800.0f, 1.3f, 8500.0f };

static void moa_sfx_lowpass(float *x, int n, float hz)
{
    /* RBJ biquad, Q 0.707 */
    if (hz <= 0.0f || hz >= 0.49f * MOA_SFX_RATE) return;
    const float w = 6.2831853f * hz / MOA_SFX_RATE, c = cosf(w), al = sinf(w) / (2.0f * 0.7071f);
    const float a0 = 1.0f + al;
    const float b0 = (1.0f - c) / 2.0f / a0, b1 = (1.0f - c) / a0, b2 = b0;
    const float a1 = -2.0f * c / a0, a2 = (1.0f - al) / a0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int i = 0; i < n; i++) {
        const float y = b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x[i]; y2 = y1; y1 = y;
        x[i] = y;
    }
}

/* Comb + all-pass hall (Schroeder / Freeverb style), darkened in the loop. */
static void hall(const float *in, float *out, int n, const struct moa_sfx_room *r)
{
    static const int k_comb[6] = { 1117, 1188, 1277, 1356, 1422, 1491 };
    static const float k_fb[6] = { 0.84f, 0.83f, 0.82f, 0.81f, 0.80f, 0.79f };
    static const int k_ap[4] = { 225, 556, 441, 341 };
    const float d = expf(-6.2831853f * r->damp / MOA_SFX_RATE);    /* one-pole in the loop */
    memset(out, 0, sizeof(float) * (size_t)n);
    for (int c = 0; c < 6; c++) {
        const int len = (int)((float)k_comb[c] / 2.0f * r->size) + 1;     /* tuned at 44.1k */
        float *buf = (float *)calloc((size_t)len, sizeof *buf);
        if (!buf) continue;
        float lp = 0.0f;
        int p = 0;
        for (int i = 0; i < n; i++) {
            const float y = buf[p];
            lp = y * (1.0f - d) + lp * d;
            buf[p] = in[i] + k_fb[c] * lp;
            out[i] += y;
            if (++p == len) p = 0;
        }
        free(buf);
    }
    for (int a = 0; a < 4; a++) {
        const int len = k_ap[a] / 2 + 1;
        float *buf = (float *)calloc((size_t)len, sizeof *buf);
        if (!buf) continue;
        int p = 0;
        for (int i = 0; i < n; i++) {
            const float b = buf[p];
            const float y = -0.5f * out[i] + b;
            buf[p] = out[i] + 0.5f * b;
            out[i] = y;
            if (++p == len) p = 0;
        }
        free(buf);
    }
    for (int i = 0; i < n; i++) out[i] *= 0.10f;
}

static float *moa_sfx_finish(const float *in, int n, float tail_s, const struct moa_sfx_room *r, int *n_out)
{
    const int m = n + (int)(tail_s * MOA_SFX_RATE);
    float *x = (float *)calloc((size_t)m, sizeof *x);
    float *w = (float *)calloc((size_t)m, sizeof *w);
    if (!x || !w) { free(x); free(w); return NULL; }
    float top = 0.0f;
    for (int i = 0; i < n; i++) if (fabsf(in[i]) > top) top = fabsf(in[i]);
    const float k = top > 0.0f ? 1.0f / top : 0.0f;
    for (int i = 0; i < n; i++) {
        float v = in[i] * k;
        if (r->drive > 0.0f) v = tanhf(v * r->drive) / tanhf(r->drive);
        x[i] = v;
    }
    moa_sfx_lowpass(x, n, r->top);
    hall(x, w, m, r);
    for (int i = 0; i < m; i++) x[i] += r->wet * w[i];
    /* no click at the very end */
    const int f = m < 400 ? m : 400;
    for (int i = 0; i < f; i++) x[m - 1 - i] *= (float)i / (float)f;
    free(w);
    *n_out = m;
    return x;
}

/* ---- VS count-in ------------------------------------------------------------- */
static float *moa_sfx_count_shot(const float *src, int n, int last, int *n_out)
{
    /* The last shot of the count plays a little lower and duller; the
     * others are the game's sound as it is. */
    const float pitch = last ? 0.94f : 1.0f;
    const int m = (int)((float)n / pitch);
    float *x = (float *)calloc((size_t)m + 1u, sizeof *x);
    if (!x) return NULL;
    for (int i = 0; i < m; i++) {                        /* read slower = lower */
        const float p = (float)i * pitch;
        const int k = (int)p;
        const float f = p - (float)k;
        const float a = src[k < n ? k : n - 1], b = src[k + 1 < n ? k + 1 : n - 1];
        x[i] = a + (b - a) * f;
    }
    const int fo = m < 300 ? m : 300;                   /* no click where the sample stops */
    for (int i = 0; i < fo; i++) x[m - 1 - i] *= (float)i / (float)fo;
    if (last) moa_sfx_lowpass(x, m, 7400.0f);
    float *f = moa_sfx_finish(x, m, 1.0f, &moa_sfx_hall, n_out);
    free(x);
    return f;
}


/* ---- the victory sting and the type tick (moa_chime.c) ---- */

static uint32_t s_noise = 0x1234567u;
static float noise(void)
{
    s_noise = s_noise * 1664525u + 1013904223u;
    return (float)(int32_t)s_noise / 2147483648.0f;
}

/* ---- guitar ------------------------------------------------------------------- */
/* One plucked string into `o` (clean signal; the amp comes after).
 * damp: 0.5 = open and ringing .. lower = palm muted. len_s: how long it may
 * sound. vib: vibrato depth in semitones (after 0.25 s). */
static void pluck_bend(float *o, int n, float at, float hz, float amp, float damp, float len_s, float vib, float bend)
{
    const int s0 = (int)(at * RATE);
    int d = (int)((float)RATE / hz);
    if (d < 2) d = 2;
    float *line = (float *)calloc((size_t)d + 2u, sizeof *line);
    if (!line) return;
    float lp = 0.0f;
    for (int i = 0; i < d; i++) { lp += 0.6f * (noise() - lp); line[i] = lp; }  /* a softer pick */
    const int end = s0 + (int)(len_s * RATE) < n ? s0 + (int)(len_s * RATE) : n;
    float pos = 0.0f, prev = 0.0f;
    for (int i = s0; i < end; i++) {
        const float t = (float)(i - s0) / RATE;
        /* vibrato by reading the line slightly faster or slower */
        float v = vib > 0.0f && t > 0.25f ? vib * sinf(TAU * 5.5f * t) * fminf(1.0f, (t - 0.25f) * 3.0f) : 0.0f;
        if (bend > 0.0f && t < 0.07f) v -= bend * (1.0f - t / 0.07f);
        const float step = powf(2.0f, v / 12.0f);
        const int k = (int)pos % d, k1 = (k + 1) % d;
        const float cur = line[k];
        const float nxt = (cur + line[k1]) * damp + prev * (1.0f - 2.0f * damp);
        prev = cur;
        line[k] = nxt * 0.999f;
        const float fade = end - i < 400 ? (float)(end - i) / 400.0f : 1.0f;
        o[i] += amp * cur * fade;
        pos += step;
        if (pos >= (float)d * 1000.0f) pos -= (float)d * 1000.0f;
    }
    free(line);
}

static void pluck(float *o, int n, float at, float hz, float amp, float damp, float len_s, float vib)
{
    pluck_bend(o, n, at, hz, amp, damp, len_s, vib, 0.0f);
}

/* A power chord (root, fifth, octave), two takes a hair apart and detuned. */
static void power_chord(float *o, int n, float at, float root_hz, float damp, float len_s, float vib)
{
    static const float k_int[3] = { 1.0f, 1.4983f, 2.0f };
    for (int take = 0; take < 2; take++)
        for (int k = 0; k < 3; k++)
            pluck(o, n, at + 0.004f * (float)take + 0.002f * (float)k, root_hz * k_int[k] * (take ? 1.004f : 0.997f),
                  0.5f, damp, len_s, vib);
}

/* Overdrive and a speaker cabinet over the whole guitar track. */
static void amp_cab(float *g, int n)
{
    float hp = 0.0f, hpx = 0.0f, l1 = 0.0f, l2 = 0.0f, l3 = 0.0f;
    for (int i = 0; i < n; i++) {
        float x = g[i];
        const float y = x - hpx + 0.995f * hp;              /* low cut before the drive */
        hpx = x; hp = y;
        x = tanhf(y * 9.0f);                                 /* hard drive */
        x = 0.7f * x + 0.3f * tanhf(x * 3.0f);
        l1 += 0.42f * (x - l1);                              /* cabinet: ~3.5 kHz roll-off */
        l2 += 0.42f * (l1 - l2);
        l3 += 0.55f * (l2 - l3);
        g[i] = l3;
    }
}

/* ---- drums and machines -------------------------------------------------------- */
static void kick(float *o, int n, float at, float gain)
{
    const int s0 = (int)(at * RATE);
    float ph = 0.0f;
    for (int i = s0; i < n; i++) {
        const float t = (float)(i - s0) / RATE;
        if (t > 0.35f) break;
        ph += (50.0f + 120.0f * expf(-t * 35.0f)) / RATE;
        o[i] += gain * (sinf(TAU * ph) * expf(-t * 11.0f) + 0.3f * noise() * expf(-t * 300.0f));
    }
}

static void crash(float *o, int n, float at, float gain)
{
    const int s0 = (int)(at * RATE);
    float a = 0.0f;
    for (int i = s0; i < n; i++) {
        const float t = (float)(i - s0) / RATE;
        if (t > 1.6f) break;
        const float w = noise();
        a += 0.25f * (w - a);
        o[i] += gain * (w - a) * expf(-t * 3.6f) * (t < 0.003f ? t / 0.003f : 1.0f);
    }
}

static void snare(float *o, int n, float at, float gain)
{
    const int s0 = (int)(at * RATE);
    float ph = 0.0f, a = 0.0f;
    for (int i = s0; i < n; i++) {
        const float t = (float)(i - s0) / RATE;
        if (t > 0.25f) break;
        ph += (190.0f + 60.0f * expf(-t * 60.0f)) / RATE;
        const float w = noise();
        a += 0.35f * (w - a);
        o[i] += gain * (0.55f * sinf(TAU * ph) * expf(-t * 30.0f) + 0.8f * (w - 0.5f * a) * expf(-t * 18.0f));
    }
}

/* A hydraulic hiss with a clunk in front: the machine settling. */
static void hiss(float *o, int n, float at, float len, float gain)
{
    const int s0 = (int)(at * RATE);
    float a = 0.0f, b = 0.0f;
    for (int i = s0; i < n; i++) {
        const float t = (float)(i - s0) / RATE;
        if (t > len) break;
        const float w = noise();
        a += 0.6f * (w - a);
        b += 0.6f * (a - b);
        const float env = (t < 0.01f ? t / 0.01f : 1.0f) * expf(-t * 4.0f / len);
        o[i] += gain * (w - b) * env;
        o[i] += gain * 1.6f * sinf(TAU * 70.0f * t) * expf(-t * 40.0f);
    }
}

/* Metal: inharmonic clang partials and a click. */
static void clank(float *o, int n, float at, float gain)
{
    static const float k_part[5] = { 1.0f, 1.52f, 2.39f, 3.17f, 4.41f };
    const int s0 = (int)(at * RATE);
    for (int i = s0; i < n; i++) {
        const float t = (float)(i - s0) / RATE;
        if (t > 0.9f) break;
        float v = 0.5f * noise() * expf(-t * 120.0f);
        for (int k = 0; k < 5; k++)
            v += 0.2f * sinf(TAU * 420.0f * k_part[k] * t + (float)k) * expf(-t * (8.0f + 4.0f * (float)k));
        o[i] += gain * v;
    }
}

/* The pinch harmonic: a high squeal sliding up a step, with wide vibrato. */
static void squeal(float *o, int n, float at, float hz, float gain)
{
    const int s0 = (int)(at * RATE);
    float ph = 0.0f;
    for (int i = s0; i < n; i++) {
        const float t = (float)(i - s0) / RATE;
        if (t > 1.0f) break;
        const float slide = 1.0f + 0.1225f * fminf(1.0f, t / 0.12f);            /* up a whole step */
        const float vib = 1.0f + 0.03f * sinf(TAU * 7.0f * t) * fminf(1.0f, t * 4.0f);
        ph += hz * slide * vib / RATE;
        const float env = (t < 0.01f ? t / 0.01f : 1.0f) * expf(-t * 2.8f);
        o[i] += gain * env * tanhf(4.0f * (sinf(TAU * ph) + 0.4f * sinf(2.0f * TAU * ph)));
    }
}


/* The sting as PCM (also used by the offline preview). About 1.85 s, quick:
 * a hit on E to open, a fast rising run on the lead guitar over chugs and a
 * snare build, then at 0.72 s (when YOU WIN lands) a big E power chord with a
 * lead note bent up into the octave, crash, kick and anvil clank; it fades out
 * fast and the machine settles with a hydraulic hiss. */
#define STING_HIT 0.72f
static float *moa_chime_sting_pcm(int *n_out)
{
    const int n = (int)(1.85f * RATE);
    float *g = (float *)calloc((size_t)n, sizeof *g);       /* guitars */
    float *o = (float *)calloc((size_t)n, sizeof *o);       /* the mix */
    if (!g || !o) { free(g); free(o); return NULL; }
    s_noise = 0x1234567u;
    const float E2 = 82.41f;
    /* the opening hit and the chugs under the run */
    power_chord(g, n, 0.00f, E2, 0.45f, 0.16f, 0.0f);
    for (int k = 1; k < 8; k++) power_chord(g, n, 0.08f * (float)k + 0.08f, E2, 0.36f, 0.06f, 0.0f);
    /* the run: E minor pentatonic up two octaves, double-tracked */
    static const float k_run[8] = { 196.00f, 220.00f, 246.94f, 293.66f, 329.63f, 392.00f, 440.00f, 493.88f };
    for (int k = 0; k < 8; k++)
        for (int take = 0; take < 2; take++)
            pluck(g, n, 0.08f + 0.08f * (float)k + 0.003f * (float)take, k_run[k] * (take ? 1.003f : 0.998f),
                  0.55f, 0.47f, 0.085f, 0.0f);
    /* the big one */
    power_chord(g, n, STING_HIT, E2, 0.4985f, 1.1f, 0.25f);
    for (int take = 0; take < 2; take++)
        pluck_bend(g, n, STING_HIT + 0.003f * (float)take, 659.26f * (take ? 1.003f : 0.998f), 0.6f, 0.4985f, 1.1f,
                   0.4f, 2.0f);
    squeal(g, n, STING_HIT + 0.01f, 1318.5f, 0.035f);
    amp_cab(g, n);
    for (int i = 0; i < n; i++) o[i] = 0.85f * g[i];
    kick(o, n, 0.00f, 1.0f);
    clank(o, n, 0.00f, 0.5f);
    crash(o, n, 0.00f, 0.12f);
    kick(o, n, 0.24f, 0.55f);
    kick(o, n, 0.40f, 0.55f);
    for (int k = 0; k < 6; k++) snare(o, n, 0.44f + 0.05f * (float)k, 0.18f + 0.07f * (float)k);   /* the build */
    kick(o, n, STING_HIT, 1.0f);
    snare(o, n, STING_HIT, 0.5f);
    clank(o, n, STING_HIT, 0.6f);
    crash(o, n, STING_HIT, 0.24f);
    hiss(o, n, 1.30f, 0.5f, 0.10f);
    /* starts strong, fades out fast after the hit */
    for (int i = (int)(1.05f * RATE); i < n; i++) {
        const float u = (float)(i - (int)(1.05f * RATE)) / (float)(n - (int)(1.05f * RATE));
        o[i] *= (1.0f - u) * (1.0f - u);
    }
    free(g);
    *n_out = n;
    return o;
}

static float *moa_chime_tick_pcm(int *n_out)
{
    const int n = (int)(0.045f * RATE);
    float *o = (float *)calloc((size_t)n, sizeof *o);
    if (!o) return NULL;
    float lp = 0.0f;
    for (int i = 0; i < n; i++) {
        const float t = (float)i / RATE;
        lp += 0.5f * (noise() - lp);
        o[i] = (0.7f * (noise() - lp) + 0.6f * sinf(TAU * 2200.0f * t)) * expf(-t * 160.0f) +
               0.5f * sinf(TAU * 180.0f * t) * expf(-t * 90.0f);
    }
    *n_out = n;
    return o;
}

/* The sounds as they are encoded: with the shared finish (moa_sfx.c). The
 * sting keeps 0.9 s of hall after it; the tick only a short room. */
static float *moa_chime_sting_finished(int *n_out)
{
    int n = 0;
    float *o = moa_chime_sting_pcm(&n);
    if (!o) return NULL;
    float *f = moa_sfx_finish(o, n, 0.9f, &moa_sfx_hall, n_out);
    free(o);
    return f;
}

static float *moa_chime_tick_finished(int *n_out)
{
    static const struct moa_sfx_room k_tick_room = { 0.12f, 0.6f, 3800.0f, 1.3f, 8500.0f };
    int n = 0;
    float *o = moa_chime_tick_pcm(&n);
    if (!o) return NULL;
    float *f = moa_sfx_finish(o, n, 0.12f, &k_tick_room, n_out);
    free(o);
    return f;
}

/* ---- building them ---- */

/* Scaled so the loudest point is `peak` (full scale 1), as AC1Recomp's
 * encode does (29000 / 32767 at voice level 0x3000 / 0x3FFF; the tick
 * 14000). */
static void level(float *x, int n, float peak) {
    float top = 0.0f;
    for (int i = 0; i < n; ++i) if (fabsf(x[i]) > top) top = fabsf(x[i]);
    if (top <= 0.0f) return;
    const float k = peak / top;
    for (int i = 0; i < n; ++i) x[i] *= k;
}

#define LOUD  (29000.0f / 32767.0f * 0.75f)
#define QUIET (14000.0f / 32767.0f * 0.75f)

int rogue_sound_build(void) {
    static int done;
    if (done) return 1;
    int ok = 0, n = 0;
    float *x;
    /* The VS sounds: the game's rifle (21) for the count's shots, sound 1
     * for the shutters' slam (moa_vs_sound.c). */
    int ns = 0, nd = 0;
    const float *shot = sfx_sample(21, &ns), *door = sfx_sample(1, &nd);
    if (shot && ns > 0) {
        for (int last = 0; last < 2; ++last)
            if ((x = moa_sfx_count_shot(shot, ns, last, &n)) != NULL) {
                level(x, n, LOUD);
                sfx_set_ui(last ? SFX_VS_SHOT_LAST : SFX_VS_SHOT, x, n);
                ++ok;
            }
    }
    if (door && nd > 0) {
        static const struct moa_sfx_room k_door_room = { 0.28f, 1.0f, 3800.0f, 1.3f, 8500.0f };
        if ((x = moa_sfx_finish(door, nd, 1.0f, &k_door_room, &n)) != NULL) {
            level(x, n, LOUD);
            sfx_set_ui(SFX_VS_DOOR, x, n);
            ++ok;
        }
    }
    if ((x = moa_chime_sting_finished(&n)) != NULL) { level(x, n, LOUD); sfx_set_ui(SFX_WIN_STING, x, n); ++ok; }
    if ((x = moa_chime_tick_finished(&n)) != NULL) { level(x, n, QUIET); sfx_set_ui(SFX_WIN_TICK, x, n); ++ok; }
    done = 1;
    return ok;
}
