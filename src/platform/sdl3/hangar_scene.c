#include "hangar_scene.h"

#include <math.h>

#include "../../render/acr_render.h"

/* The mockup is 1280x720; the HUD is 480 high. */
#define K (480.0f / 720.0f)
static float g_ox;   /* left edge of the mockup's 1280 on this HUD */
static void origin(float hud_w) { g_ox = (hud_w - 1280.0f * K) * 0.5f; }
static float X(float x) { return g_ox + x * K; }
static float Y(float y) { return y * K; }

static acr_vtx hv(float x, float y, const uint8_t c[4]) {
    acr_vtx v = { x, y, 0, c[0], c[1], c[2], c[3], 0, 0 };
    return v;
}

void hs_quad(float x, float y, float w, float h, const uint8_t top[4], const uint8_t bottom[4]) {
    const acr_vtx q[6] = { hv(x, y, top), hv(x + w, y, top), hv(x + w, y + h, bottom),
                           hv(x, y, top), hv(x + w, y + h, bottom), hv(x, y + h, bottom) };
    acr_render_tris(q, 6, 0);
}

static void fill(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    acr_hud_rect(x, y, w, h, r, g, b, a, 1);
}

/* A box turned by `ang` about (px, py); (x0, y0)-(x1, y1) relative to it. */
static void turned_box(float px, float py, float ang, float x0, float y0, float x1, float y1,
                       const uint8_t c[4], const uint8_t edge[4]) {
    const float s = sinf(ang), co = cosf(ang);
    const float cx[4] = { x0, x1, x1, x0 }, cy[4] = { y0, y0, y1, y1 };
    acr_vtx v[4];
    for (int i = 0; i < 4; ++i) v[i] = hv(px + cx[i] * co - cy[i] * s, py + cx[i] * s + cy[i] * co, c);
    const acr_vtx t[6] = { v[0], v[1], v[2], v[0], v[2], v[3] };
    acr_render_tris(t, 6, 0);
    for (int i = 0; i < 4; ++i)
        acr_hud_line(v[i].x, v[i].y, v[(i + 1) % 4].x, v[(i + 1) % 4].y, edge[0], edge[1], edge[2], edge[3]);
}

/* A thick straight line (the roof struts). */
static void thick(float x0, float y0, float x1, float y1, float wdt, const uint8_t c[4]) {
    const float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l <= 0) return;
    const float nx = -dy / l * wdt * 0.5f, ny = dx / l * wdt * 0.5f;
    const acr_vtx t[6] = { hv(x0 + nx, y0 + ny, c), hv(x1 + nx, y1 + ny, c), hv(x1 - nx, y1 - ny, c),
                           hv(x0 + nx, y0 + ny, c), hv(x1 - nx, y1 - ny, c), hv(x0 - nx, y0 - ny, c) };
    acr_render_tris(t, 6, 0);
}

void hs_puff(float x, float y, float radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    enum { N = 20 };
    const uint8_t mid[4] = { r, g, b, a }, rim[4] = { r, g, b, 0 };
    acr_vtx t[N * 3];
    for (int i = 0; i < N; ++i) {
        const float a0 = (float)i / N * 6.2831853f, a1 = (float)(i + 1) / N * 6.2831853f;
        t[i * 3] = hv(x, y, mid);
        t[i * 3 + 1] = hv(x + cosf(a0) * radius, y + sinf(a0) * radius, rim);
        t[i * 3 + 2] = hv(x + cosf(a1) * radius, y + sinf(a1) * radius, rim);
    }
    acr_render_tris(t, N * 3, 0);
}

/* ---- behind the AC ---- */

void hangar_back(float hud_w) {
    origin(hud_w);
    /* the hangar's air: #26272c down to #141518 (70%), then #0c0c0e */
    const uint8_t c0[4] = { 0x26, 0x27, 0x2c, 255 }, c1[4] = { 0x14, 0x15, 0x18, 255 }, c2[4] = { 0x0c, 0x0c, 0x0e, 255 };
    hs_quad(0, 0, hud_w, 336, c0, c1);
    hs_quad(0, 336, hud_w, 144, c1, c2);
    /* pillars and the roof beam (full width on wide screens) */
    fill(X(80), 0, 70 * K, 480, 0x1d, 0x1e, 0x22, 255);
    acr_hud_rect(X(80), -2, 70 * K, 484, 0x34, 0x36, 0x3c, 255, 0);
    fill(X(1130), 0, 70 * K, 480, 0x1d, 0x1e, 0x22, 255);
    acr_hud_rect(X(1130), -2, 70 * K, 484, 0x34, 0x36, 0x3c, 255, 0);
    fill(0, Y(60), hud_w, 34 * K, 0x1d, 0x1e, 0x22, 255);
    acr_hud_line(0, Y(60), hud_w, Y(60), 0x34, 0x36, 0x3c, 255);
    acr_hud_line(0, Y(94), hud_w, Y(94), 0x34, 0x36, 0x3c, 255);
    const uint8_t strut[4] = { 0x2c, 0x2e, 0x33, 255 };
    thick(X(150), Y(94), X(420), Y(380), 10 * K, strut);
    thick(X(1130), Y(94), X(860), Y(380), 10 * K, strut);
    /* pipes */
    static const float pipes[][8] = {
        { 1000, 94, 1000, 314, 1060, 314, 1060, 614 },
        { 1012, 94, 1012, 302, 1072, 302, 1072, 614 },
        { 260, 94, 260, 254, 200, 254, 200, 614 },
    };
    for (int p = 0; p < 3; ++p)
        for (int i = 0; i < 3; ++i)
            acr_hud_line(X(pipes[p][i * 2]), Y(pipes[p][i * 2 + 1]), X(pipes[p][i * 2 + 2]), Y(pipes[p][i * 2 + 3]),
                         0x3a, 0x3c, 0x42, 255);
    /* floor, the gantry's platform and its tread marks */
    fill(0, Y(560), hud_w, 160 * K, 0x10, 0x11, 0x13, 255);
    fill(X(420), Y(560), 440 * K, 10 * K, 0x2a, 0x2b, 0x30, 255);
    const uint8_t tread[4] = { 0x1a, 0x1b, 0x1e, 255 };
    for (int i = 0; i < 6; ++i) thick(X(500 + i * 60), Y(566), X(514 + i * 60), Y(560), 6 * K, tread);
    /* the gantry clamps over the AC */
    fill(X(470), Y(94), 340 * K, 18 * K, 0x3b, 0x3d, 0x43, 255);
    fill(X(500), Y(112), 20 * K, 60 * K, 0x2e, 0x30, 0x35, 255);
    fill(X(760), Y(112), 20 * K, 60 * K, 0x2e, 0x30, 0x35, 255);
}

void hangar_ac_rect(float hud_w, float *x, float *y, float *w, float *h) {
    origin(hud_w);
    *x = X(510);
    *y = Y(100);
    *w = 300 * 0.86f * K;
    *h = 440 * 0.86f * K;
}

/* ---- in front of the AC ---- */

static float frac(float v) { return v - floorf(v); }

/* CSS ease-in-out between keyframes 0% -> 50% -> 100% (a, b, a). */
static float swing(float t, float period, float a, float b) {
    float u = frac(t / period) * 2.0f;
    if (u > 1.0f) u = 2.0f - u;
    u = u * u * (3.0f - 2.0f * u);
    return (a + (b - a) * u) * 3.14159265f / 180.0f;
}

void hangar_fx(float hud_w, unsigned ms) {
    origin(hud_w);
    const float t = (float)ms / 1000.0f;
    /* steam: 3.2 s, rising 120 px while it grows from 0.5x to 2.4x of 40 px,
     * fading in to 0.55 by 20% and out by the end */
    static const float puffs[][3] = {
        { 540, 320, 0.0f }, { 560, 320, 1.1f }, { 732, 320, 0.6f }, { 660, 540, 1.8f }, { 1050, 340, 2.3f } };
    for (int i = 0; i < 5; ++i) {
        const float u = frac((t - puffs[i][2]) / 3.2f);
        const float u2 = 1.0f - (1.0f - u) * (1.0f - u);   /* ease-out */
        const float op = u < 0.2f ? u / 0.2f * 0.55f : 0.55f * (1.0f - (u - 0.2f) / 0.8f);
        const float scale = 0.5f + 1.9f * u2;
        hs_puff(X(puffs[i][0]), Y(puffs[i][1] - 120.0f * u2), 20.0f * scale * K, 220, 225, 235, (uint8_t)(op * 255.0f));
    }
    /* maintenance arm 1: hinged at its left end, -14 to 18 degrees over 6 s,
     * a clamp and a weld spark at its tip */
    const uint8_t arm[4] = { 0x3d, 0x40, 0x47, 255 }, edge[4] = { 0x5a, 0x5d, 0x64, 255 }, head[4] = { 0x2a, 0x2c, 0x31, 255 };
    const float a1 = swing(t, 6.0f, -14, 18);
    const float px1 = X(330), py1 = Y(126);
    turned_box(px1, py1, a1, 0, -6 * K, 170 * K, 6 * K, arm, edge);
    turned_box(px1, py1, a1, 162 * K, -14 * K, 184 * K, 14 * K, head, edge);
    const float sp = frac(t / 2.4f);
    if (sp > 0.85f) {
        const float on = sp < 0.88f ? (sp - 0.85f) / 0.03f : 1.0f - (sp - 0.88f) / 0.12f;
        const float sx = px1 + cosf(a1) * 190 * K, sy = py1 + sinf(a1) * 190 * K;
        hs_puff(sx, sy, 12 * K, 255, 179, 71, (uint8_t)(on * 160));
        hs_puff(sx, sy, 4 * K, 255, 210, 122, (uint8_t)(on * 255));
    }
    /* arm 2: hinged at its right end, 10 to -22 degrees over 7 s */
    const float a2 = swing(t, 7.0f, 10, -22);
    const float px2 = X(1110), py2 = Y(206);
    turned_box(px2, py2, a2, -150 * K, -6 * K, 0, 6 * K, arm, edge);
    turned_box(px2, py2, a2, -164 * K, -14 * K, -142 * K, 14 * K, head, edge);
    /* the warning lights: on for 60% of 1.4 s, down to 0.15 by 70% */
    for (int i = 0; i < 2; ++i) {
        const float u = frac((t - (i ? 0.7f : 0.0f)) / 1.4f);
        const float op = u < 0.6f ? 1.0f : u < 0.7f ? 1.0f - 0.85f * (u - 0.6f) / 0.1f : 0.15f;
        const float lx = X(i ? 119 : 1157), ly = Y(117);
        hs_puff(lx, ly, 18 * K, 224, 65, 47, (uint8_t)(op * 110));
        hs_puff(lx, ly, 7 * K, 224, 65, 47, (uint8_t)(op * 255));
        hs_puff(lx, ly, 4 * K, 255, 140, 120, (uint8_t)(op * 255));
    }
}

/* ---- the console ---- */

void hangar_console(float hud_w, const char *ac_name, long coam, float *cx, float *cy, float *cw, float *ch) {
    origin(hud_w);
    const float x = X(150), y = Y(390), w = 980 * K, h = 480 - Y(390);
    /* its shadow on the hangar */
    const uint8_t top[4] = { 0x3a, 0x3b, 0x40, 255 }, bot[4] = { 0x24, 0x25, 0x2a, 255 };
    hs_quad(x, y, w, h, top, bot);
    acr_hud_rect(x, y, w, h + 4, 0x55, 0x57, 0x5d, 255, 0);
    char line[64];
    const float ty = Y(390 + 18);
    acr_hud_text_size(x + 26 * K, ty, "RAVEN NET // HANGAR", 7, 0x8d, 0x91, 0x99, 255);
    {
        int n = 0;
        const char *p = ac_name && ac_name[0] ? ac_name : "AC";
        for (const char *s = "AC: "; *s && n < 60; ++s) line[n++] = *s;
        for (; *p && n < 60; ++p) line[n++] = *p;
        line[n] = 0;
        acr_hud_text_size(x + w * 0.5f - (float)n * 3.5f, ty, line, 7, 0x8d, 0x91, 0x99, 255);
    }
    {
        /* COAM with thousands commas, as the mockup's "COAM 248,500" */
        char num[24], out[40] = "COAM ";
        long v = coam < 0 ? -coam : coam;
        int n = 0;
        do { num[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 20);
        int o = 5;
        if (coam < 0) out[o++] = '-';
        for (int i = n - 1; i >= 0; --i) {
            out[o++] = num[i];
            if (i && i % 3 == 0) out[o++] = ',';
        }
        out[o] = 0;
        acr_hud_text_size(x + w - 26 * K - (float)o * 7.0f, ty, out, 7, coam < 0 ? 0xe0 : 0x8d, coam < 0 ? 0x50 : 0x91,
                          coam < 0 ? 0x40 : 0x99, 255);
    }
    *cx = x + 26 * K;
    *cy = Y(390 + 18 + 13 + 8);
    *cw = w - 52 * K;
    *ch = 270 * K;
}

void hangar_crt_back(float cx, float cy, float cw, float ch) {
    fill(cx, cy, cw, ch, 0x0d, 0x1a, 0x17, 255);
    /* the tube's inner glow: brighter at the edges */
    const uint8_t g0[4] = { 60, 200, 140, 40 }, g1[4] = { 60, 200, 140, 0 };
    hs_quad(cx, cy, cw, 14, g0, g1);
    hs_quad(cx, cy + ch - 14, cw, 14, g1, g0);
}

void hangar_crt_front(float cx, float cy, float cw, float ch, unsigned ms) {
    /* the sweep: 30 px band from -10% to 110% over 4 s */
    const float u = (float)(ms % 4000u) / 4000.0f;
    const float band = 30 * K, by = cy - ch * 0.1f + ch * 1.2f * u - band * 0.5f;
    const float y0 = by < cy ? cy : by, y1 = by + band > cy + ch ? cy + ch : by + band;
    if (y1 > y0) {
        const float mid = by + band * 0.5f;
        const uint8_t clear[4] = { 120, 255, 190, 0 }, lit[4] = { 120, 255, 190, 20 };
        if (mid > y0) hs_quad(cx, y0, cw, (mid < y1 ? mid : y1) - y0, clear, lit);
        if (mid < y1) hs_quad(cx, mid > y0 ? mid : y0, cw, y1 - (mid > y0 ? mid : y0), lit, clear);
    }
    /* scanlines: a dark line every 3 px (mockup px) */
    for (float y = cy + 1; y < cy + ch; y += 3 * K) acr_hud_line(cx, y, cx + cw, y, 0, 0, 0, 64);
    acr_hud_rect(cx, cy, cw, ch, 0x12, 0x14, 0x14, 255, 0);
}

/* ---- icons ---- */

/* Polylines on 24x24; -1 starts a new line, -2 a circle (cx, cy, r), -9 ends. */
static const float k_icons[10][48] = {
    { -2, 12, 12, 4, -1, 12, 2, 12, 5, -1, 12, 19, 12, 22, -1, 2, 12, 5, 12, -1, 19, 12, 22, 12, -9 },        /* SYSTEM */
    { 5, 4, 16, 4, 19, 7, 19, 20, 5, 20, 5, 4, -1, 8, 4, 8, 9, 15, 9, 15, 4, -9 },                         /* DATA */
    { 5, 5, 19, 19, -1, 19, 5, 5, 19, -1, 3, 12, 7, 12, -1, 17, 12, 21, 12, -9 },                           /* VERSUS */
    { 4, 20, 12, 4, 20, 20, 4, 20, -1, 8, 14, 16, 14, -9 },                                                 /* ARENA */
    { 12, 3, 15, 8, 20, 8, 16, 12, 18, 18, 12, 15, 6, 18, 8, 12, 4, 8, 9, 8, 12, 3, -9 },                   /* GARAGE */
    { 4, 6, 20, 6, 20, 18, 4, 18, 4, 6, -1, 4, 10, 20, 10, -1, 9, 6, 9, 18, -9 },                           /* MISSION */
    { 6, 12, 18, 12, -1, 12, 6, 12, 18, -9 },                                                               /* sub marks */
    { 5, 5, 19, 5, 19, 19, 5, 19, 5, 5, -9 },
    { 12, 4, 20, 12, 12, 20, 4, 12, 12, 4, -9 },
    { 6, 18, 18, 6, -1, 8, 6, 18, 6, 18, 16, -9 },
};

void hangar_icon(int id, float x, float y, float size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (id >= 6) id = 6 + (id - 6) % 4;
    if (id < 0 || id >= 10) return;
    const float s = size / 24.0f;
    const float *p = k_icons[id];
    float lx = 0, ly = 0;
    int have = 0;
    for (int i = 0; i < 48 && p[i] != -9;) {
        if (p[i] == -1) { have = 0; ++i; continue; }
        if (p[i] == -2) {
            const float cx = x + p[i + 1] * s, cy = y + p[i + 2] * s, rr = p[i + 3] * s;
            for (int k = 0; k < 16; ++k) {
                const float a0 = (float)k / 16 * 6.2831853f, a1 = (float)(k + 1) / 16 * 6.2831853f;
                acr_hud_line(cx + cosf(a0) * rr, cy + sinf(a0) * rr, cx + cosf(a1) * rr, cy + sinf(a1) * rr, r, g, b, a);
            }
            i += 4;
            have = 0;
            continue;
        }
        const float px = x + p[i] * s, py = y + p[i + 1] * s;
        if (have) acr_hud_line(lx, ly, px, py, r, g, b, a);
        lx = px; ly = py; have = 1;
        i += 2;
    }
}
