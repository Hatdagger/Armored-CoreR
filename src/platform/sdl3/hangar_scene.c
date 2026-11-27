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

void hangar_ac_rect(float hud_w, float *x, float *y, float *w, float *h) {
    origin(hud_w);
    *x = X(510);
    *y = Y(100);
    *w = 300 * 0.86f * K;
    *h = 440 * 0.86f * K;
}

/* ---- the console ---- */

/* A filled polygon (convex, in order), one colour. */
static void poly(const float *xy, int n, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t c[4] = { r, g, b, a };
    for (int i = 1; i + 1 < n; ++i) {
        const acr_vtx t[3] = { hv(xy[0], xy[1], c), hv(xy[i * 2], xy[i * 2 + 1], c), hv(xy[i * 2 + 2], xy[i * 2 + 3], c) };
        acr_render_tris(t, 3, 0);
    }
}
/* A box with its corners cut at `k`. */
static void chamfer(float x, float y, float w, float h, float k, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const float p[16] = { x + k, y, x + w - k, y, x + w, y + k, x + w, y + h - k, x + w - k, y + h, x + k, y + h, x, y + h - k, x, y + k };
    poly(p, 8, r, g, b, a);
}
static void chamfer_edge(float x, float y, float w, float h, float k, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const float p[16] = { x + k, y, x + w - k, y, x + w, y + k, x + w, y + h - k, x + w - k, y + h, x + k, y + h, x, y + h - k, x, y + k };
    for (int i = 0; i < 8; ++i) acr_hud_line(p[i * 2], p[i * 2 + 1], p[(i * 2 + 2) % 16], p[(i * 2 + 3) % 16], r, g, b, a);
}

/* The pilot's terminal: a desk running across the bottom of the screen, a
 * heavy CRT monitor on it (housing with depth, bezel, label, power light,
 * knobs), a keyboard in front, a mug and papers, and the screen's green
 * light falling on the desk. Sitting at it, the hangar is beyond. */
void hangar_console(float hud_w, const char *ac_name, long coam, float *cx, float *cy, float *cw, float *ch) {
    origin(hud_w);
    const float mid = hud_w * 0.5f;
    /* the screen; the menu is laid out on it */
    const float sw = 600, sh = 166;
    const float sx = mid - sw * 0.5f, sy = 268;
    const float hx = sx - 20, hy = sy - 22, hw = sw + 40, hh = sh + 48;   /* the monitor's front */

    /* the desk: its far edge behind the monitor, running out of the screen */
    const float dy = 420;
    {
        const float p[8] = { -10, dy, hud_w + 10, dy, hud_w + 10, 480, -10, 480 };
        poly(p, 4, 0x2a, 0x2b, 0x2f, 255);
        const uint8_t t0[4] = { 0x3c, 0x3d, 0x42, 255 }, t1[4] = { 0x22, 0x23, 0x27, 255 };
        hs_quad(-10, dy, hud_w + 20, 60, t0, t1);
        acr_hud_line(-10, dy, hud_w + 10, dy, 0x5a, 0x5c, 0x62, 255);   /* the desk's lit far edge */
        acr_hud_line(-10, dy + 1, hud_w + 10, dy + 1, 0x14, 0x15, 0x17, 255);
        hs_puff(mid, dy + 26, 260, 90, 255, 170, 26);   /* the screen's glow on the desk */
    }
    /* papers on the left: a few sheets at angles */
    {
        const float px = mid - sw * 0.5f - 95, py = 438;
        const float a[8] = { px, py, px + 70, py - 6, px + 78, py + 30, px + 6, py + 36 };
        poly(a, 4, 0x9a, 0x9a, 0x90, 255);
        const float b2[8] = { px + 14, py + 4, px + 86, py + 2, px + 88, py + 38, px + 14, py + 40 };
        poly(b2, 4, 0xb4, 0xb3, 0xa8, 255);
        for (int i = 0; i < 6; ++i) acr_hud_line(px + 22, py + 10 + i * 4.5f, px + 74 - (i % 3) * 8, py + 9 + i * 4.5f, 0x60, 0x60, 0x5a, 255);
        acr_hud_text_size(px + 22, py + 33, "ORDERS", 4, 0x80, 0x30, 0x28, 255);
    }
    /* a mug on the right, steam curling off it */
    {
        const float mx = mid + sw * 0.5f + 70, my = 434;
        hs_puff(mx + 8, my + 30, 22, 0, 0, 0, 90);   /* its shadow */
        acr_hud_rect(mx - 2, my, 22, 26, 0x6c, 0x2a, 0x24, 255, 1);
        acr_hud_rect(mx - 2, my, 7, 26, 0x8a, 0x3a, 0x30, 255, 1);
        acr_hud_rect(mx - 2, my - 2, 22, 4, 0x3a, 0x20, 0x1a, 255, 1);
        acr_hud_rect(mx + 20, my + 6, 7, 12, 0x6c, 0x2a, 0x24, 255, 0);
        acr_hud_rect(mx + 2, my + 10, 14, 3, 0xd6, 0xa8, 0x30, 255, 1);   /* a yellow band */
    }
    /* the monitor: its sides going back (depth), the housing, the bezel */
    {
        const float back = 34;   /* how far the case reaches back, drawn as narrowing sides */
        const float l[8] = { hx, hy + 6, hx + back * 0.7f, hy - 14, hx + back * 0.7f, hy + hh - 30, hx, hy + hh };
        poly(l, 4, 0x2c, 0x2e, 0x33, 255);
        const float r[8] = { hx + hw, hy + 6, hx + hw - back * 0.7f, hy - 14, hx + hw - back * 0.7f, hy + hh - 30, hx + hw, hy + hh };
        poly(r, 4, 0x2c, 0x2e, 0x33, 255);
        const float top[8] = { hx + back * 0.7f, hy - 14, hx + hw - back * 0.7f, hy - 14, hx + hw - 6, hy + 2, hx + 6, hy + 2 };
        poly(top, 4, 0x4a, 0x4c, 0x52, 255);
        for (int i = 0; i < 18; ++i)   /* vent slots on top */
            acr_hud_line(mid - 120 + i * 14, hy - 8, mid - 116 + i * 14, hy - 2, 0x24, 0x25, 0x29, 255);
        /* the stand under it */
        acr_hud_rect(mid - 70, hy + hh - 4, 140, dy + 10 - (hy + hh - 4), 0x26, 0x27, 0x2b, 255, 1);
        const float foot[8] = { mid - 110, dy + 8, mid + 110, dy + 8, mid + 120, dy + 18, mid - 120, dy + 18 };
        poly(foot, 4, 0x33, 0x34, 0x39, 255);
        /* the front of the housing, lit from above */
        chamfer(hx, hy, hw, hh, 10, 0x40, 0x42, 0x48, 255);
        const uint8_t hl0[4] = { 0x5c, 0x5e, 0x66, 255 }, hl1[4] = { 0x40, 0x42, 0x48, 0 };
        hs_quad(hx + 10, hy + 1, hw - 20, 14, hl0, hl1);
        chamfer_edge(hx, hy, hw, hh, 10, 0x66, 0x69, 0x70, 255);
        /* the recess round the glass */
        chamfer(sx - 7, sy - 7, sw + 14, sh + 14, 8, 0x14, 0x15, 0x18, 255);
        chamfer_edge(sx - 7, sy - 7, sw + 14, sh + 14, 8, 0x2a, 0x2c, 0x30, 255);
        /* screws in the corners */
        const float sc[4][2] = { { hx + 9, hy + 9 }, { hx + hw - 9, hy + 9 }, { hx + 9, hy + hh - 9 }, { hx + hw - 9, hy + hh - 9 } };
        for (int i = 0; i < 4; ++i) {
            hs_puff(sc[i][0], sc[i][1], 3, 0x20, 0x20, 0x24, 255);
            acr_hud_line(sc[i][0] - 2, sc[i][1], sc[i][0] + 2, sc[i][1], 0x70, 0x72, 0x78, 255);
        }
        /* the label strip above the glass: the header line */
        acr_hud_text_size(sx + 4, hy + 6, "RAVEN NET // HANGAR", 6, 0x9d, 0xa1, 0xa9, 255);
        char line[64];
        int n = 0;
        const char *p = ac_name && ac_name[0] ? ac_name : "AC";
        for (const char *q = "AC: "; *q && n < 60; ++q) line[n++] = *q;
        for (; *p && n < 60; ++p) line[n++] = *p;
        line[n] = 0;
        acr_hud_text_size(mid - (float)n * 3.0f, hy + 6, line, 6, 0x9d, 0xa1, 0xa9, 255);
        /* COAM with thousands commas */
        char num[24], out[40] = "COAM ";
        long v = coam < 0 ? -coam : coam;
        int nn = 0;
        do { num[nn++] = (char)('0' + v % 10); v /= 10; } while (v && nn < 20);
        int o = 5;
        if (coam < 0) out[o++] = '-';
        for (int i = nn - 1; i >= 0; --i) {
            out[o++] = num[i];
            if (i && i % 3 == 0) out[o++] = ',';
        }
        out[o] = 0;
        acr_hud_text_size(sx + sw - 4 - (float)o * 6.0f, hy + 6, out, 6, coam < 0 ? 0xe0 : 0x9d, coam < 0 ? 0x50 : 0xa1,
                          coam < 0 ? 0x40 : 0xa9, 255);
        /* the chin: brand, power light, two knobs, a speaker grille */
        const float cy2 = sy + sh + 9;
        acr_hud_text_size(sx + 4, cy2 + 3, "MURAKUMO TX-9", 5, 0x7a, 0x7d, 0x84, 255);
        hs_puff(sx + sw - 8, cy2 + 6, 6, 80, 255, 140, 120);
        hs_puff(sx + sw - 8, cy2 + 6, 2.5f, 160, 255, 190, 255);
        for (int k = 0; k < 2; ++k) {
            const float kx = sx + sw - 40 - k * 18;
            hs_puff(kx, cy2 + 6, 6, 0x1c, 0x1d, 0x20, 255);
            acr_hud_line(kx, cy2 + 6, kx + 3, cy2 + 2, 0x80, 0x82, 0x88, 255);
        }
        for (int i = 0; i < 10; ++i) acr_hud_line(mid - 40 + i * 8, cy2 + 2, mid - 40 + i * 8, cy2 + 10, 0x24, 0x25, 0x29, 255);
    }
    /* the keyboard on the desk, coming towards you out of the screen */
    {
        const float ky = 452, kw0 = 330, kw1 = 420;
        const float kb[8] = { mid - kw0 * 0.5f, ky, mid + kw0 * 0.5f, ky, mid + kw1 * 0.5f, 492, mid - kw1 * 0.5f, 492 };
        poly(kb, 4, 0x34, 0x35, 0x3a, 255);
        acr_hud_line(kb[0], kb[1], kb[2], kb[3], 0x60, 0x62, 0x68, 255);
        for (int row = 0; row < 4; ++row) {
            const float t0 = (float)row / 4.0f, t1 = (float)(row + 1) / 4.0f;
            const float y0 = ky + 3 + t0 * 36, y1 = ky + 3 + t1 * 36 - 2;
            const float w0 = kw0 - 16 + (kw1 - kw0) * t0, w1 = kw0 - 16 + (kw1 - kw0) * t1;
            const int keys = 14;
            for (int k = 0; k < keys; ++k) {
                const float a0 = (float)k / keys, a1 = (float)(k + 1) / keys - 0.012f;
                const float q[8] = { mid - w0 * 0.5f + w0 * a0, y0, mid - w0 * 0.5f + w0 * a1, y0,
                                     mid - w1 * 0.5f + w1 * a1, y1, mid - w1 * 0.5f + w1 * a0, y1 };
                const int lit = (row == 0 && (k == 0 || k == 13));
                poly(q, 4, lit ? 0x6a : 0x52, lit ? 0x3a : 0x54, lit ? 0x30 : 0x5a, 255);
                acr_hud_line(q[0], q[1], q[2], q[3], 0x72, 0x74, 0x7a, 255);
            }
        }
    }
    *cx = sx;
    *cy = sy;
    *cw = sw;
    *ch = sh;
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
