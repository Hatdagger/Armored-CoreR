#include "hangar3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../render/acr_render.h"

/* ---- small vector helpers ---- */

typedef struct { float x, y, z; } v3f;
static v3f V(float x, float y, float z) { v3f r = { x, y, z }; return r; }
static v3f add(v3f a, v3f b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static v3f sub(v3f a, v3f b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static v3f mul(v3f a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static float dot(v3f a, v3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3f cross(v3f a, v3f b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float len(v3f a) { return sqrtf(dot(a, a)); }
static v3f norm(v3f a) { const float l = len(a); return l > 1e-6f ? mul(a, 1.0f / l) : V(0, 1, 0); }
static v3f lerp(v3f a, v3f b, float t) { return add(a, mul(sub(b, a), t)); }
static float frac(float v) { return v - floorf(v); }
/* A fixed pseudo-random number in 0..1 for `i`. */
static float hash(unsigned i) {
    i = (i ^ 61u) ^ (i >> 16);
    i *= 9u; i ^= i >> 4; i *= 0x27d4eb2du; i ^= i >> 15;
    return (float)(i & 0xffffu) / 65535.0f;
}

/* ---- solid geometry, flat-shaded by a light from above-front ---- */

#define SOLID_MAX 60000
static acr_vtx g_solid[SOLID_MAX];
static int g_ns;

typedef struct { uint8_t r, g, b; } rgb;
static rgb C(int r, int g, int b) { rgb c = { (uint8_t)r, (uint8_t)g, (uint8_t)b }; return c; }

static rgb shade(rgb c, v3f n) {
    static const v3f key = { 0.25f, 0.88f, -0.40f };
    float k = dot(n, key);
    if (k < 0) k = 0;
    float s = 0.42f + 0.62f * k;
    if (n.y < -0.5f) s = 0.30f;   /* undersides */
    const float r = c.r * s, g = c.g * s, b = c.b * s;
    return C(r > 255 ? 255 : (int)r, g > 255 ? 255 : (int)g, b > 255 ? 255 : (int)b);
}

static void put(v3f p, rgb c) {
    if (g_ns >= SOLID_MAX) return;
    acr_vtx *o = &g_solid[g_ns++];
    o->x = p.x; o->y = p.y; o->z = p.z;
    o->r = c.r; o->g = c.g; o->b = c.b; o->a = 255;
    o->u = o->v = 0;
}
static void quad(v3f a, v3f b, v3f c, v3f d, rgb col) {
    const rgb s = shade(col, norm(cross(sub(b, a), sub(d, a))));
    put(a, s); put(b, s); put(c, s);
    put(a, s); put(c, s); put(d, s);
}
/* A quad with its own colour at each end (light falling off a wall). */
static void quad2(v3f a, v3f b, v3f c, v3f d, rgb ab, rgb cd) {
    const v3f n = norm(cross(sub(b, a), sub(d, a)));
    const rgb s0 = shade(ab, n), s1 = shade(cd, n);
    put(a, s0); put(b, s0); put(c, s1);
    put(a, s0); put(c, s1); put(d, s1);
}
/* A box with centre `c` and half-axes ax, ay, az (any directions). */
static void obox(v3f c, v3f ax, v3f ay, v3f az, rgb col) {
    v3f p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = add(c, add(mul(ax, i & 1 ? 1.f : -1.f), add(mul(ay, i & 2 ? 1.f : -1.f), mul(az, i & 4 ? 1.f : -1.f))));
    quad(p[0], p[1], p[3], p[2], col);   /* -z */
    quad(p[5], p[4], p[6], p[7], col);   /* +z */
    quad(p[4], p[0], p[2], p[6], col);   /* -x */
    quad(p[1], p[5], p[7], p[3], col);   /* +x */
    quad(p[2], p[3], p[7], p[6], col);   /* +y */
    quad(p[4], p[5], p[1], p[0], col);   /* -y */
}
static void box(float x0, float y0, float z0, float x1, float y1, float z1, rgb col) {
    obox(V((x0 + x1) * .5f, (y0 + y1) * .5f, (z0 + z1) * .5f), V((x1 - x0) * .5f, 0, 0), V(0, (y1 - y0) * .5f, 0),
         V(0, 0, (z1 - z0) * .5f), col);
}
/* A square bar from a to b, `w` thick. */
static void bar(v3f a, v3f b, float w, rgb col) {
    const v3f d = sub(b, a);
    const float l = len(d);
    if (l < 1e-3f) return;
    const v3f f = mul(d, 1.0f / l);
    v3f s = cross(f, fabsf(f.y) > 0.9f ? V(1, 0, 0) : V(0, 1, 0));
    s = norm(s);
    const v3f u = cross(s, f);
    obox(lerp(a, b, 0.5f), mul(s, w * 0.5f), mul(u, w * 0.5f), mul(f, l * 0.5f), col);
}
/* A hanging cable from a to b sagging by `sag`, as bars. */
static void cable(v3f a, v3f b, float sag, float w, rgb col) {
    v3f prev = a;
    for (int i = 1; i <= 10; ++i) {
        const float t = (float)i / 10.0f;
        v3f p = lerp(a, b, t);
        p.y -= sag * 4.0f * t * (1.0f - t);
        bar(prev, p, w, col);
        prev = p;
    }
}

static void flush_solid(void) {
    if (g_ns) acr_render_mesh(g_solid, g_ns, 0, 0);
    g_ns = 0;
}

/* ---- see-through sprites (steam, sparks, glows), camera-facing ---- */

static uint32_t g_puff_tex;
static void make_puff(void) {
    if (g_puff_tex) return;
    enum { N = 64 };
    static uint8_t px[N * N * 4];
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float dx = (x + 0.5f) / N * 2 - 1, dy = (y + 0.5f) / N * 2 - 1;
            float d = sqrtf(dx * dx + dy * dy);
            /* soft round falloff with a little lumpy noise, so puffs don't look like discs */
            const float n = 0.85f + 0.15f * hash((unsigned)(x / 4 + (y / 4) * 17));
            float a = d >= 1 ? 0 : (1 - d) * (1 - d) * n;
            uint8_t *p = &px[(y * N + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (uint8_t)(a * 255);
        }
    g_puff_tex = acr_render_texture(px, N, N, 0);
}

typedef struct { v3f p; float size, rot; uint8_t r, g, b, a; int add; float dist; } sprite;
#define SPRITES_MAX 900
static sprite g_sp[SPRITES_MAX];
static int g_nsp;
static void spr(v3f p, float size, float rot, rgb c, int alpha, int additive) {
    if (g_nsp >= SPRITES_MAX || alpha <= 0) return;
    sprite *s = &g_sp[g_nsp++];
    s->p = p; s->size = size; s->rot = rot;
    s->r = c.r; s->g = c.g; s->b = c.b; s->a = (uint8_t)(alpha > 255 ? 255 : alpha);
    s->add = additive;
}
static int far_first(const void *a, const void *b) {
    const float da = ((const sprite *)a)->dist, db = ((const sprite *)b)->dist;
    return da < db ? 1 : da > db ? -1 : 0;
}
static void flush_sprites(const hangar3d_view *v) {
    const v3f eye = V(v->eye[0], v->eye[1], v->eye[2]);
    const v3f R = V(v->right[0], v->right[1], v->right[2]), U = V(v->up[0], v->up[1], v->up[2]);
    for (int i = 0; i < g_nsp; ++i) g_sp[i].dist = len(sub(g_sp[i].p, eye));
    qsort(g_sp, (size_t)g_nsp, sizeof g_sp[0], far_first);
    static acr_vtx q[6];
    for (int i = 0; i < g_nsp; ++i) {
        const sprite *s = &g_sp[i];
        const float c = cosf(s->rot) * s->size, sn = sinf(s->rot) * s->size;
        const v3f rx = add(mul(R, c), mul(U, sn)), uy = add(mul(U, c), mul(R, -sn));
        const v3f k[4] = { sub(sub(s->p, rx), uy), sub(add(s->p, rx), uy), add(add(s->p, rx), uy), add(sub(s->p, rx), uy) };
        const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
        for (int j = 0; j < 6; ++j) {
            acr_vtx *o = &q[j];
            o->x = k[idx[j]].x; o->y = k[idx[j]].y; o->z = k[idx[j]].z;
            o->r = s->r; o->g = s->g; o->b = s->b; o->a = s->a;
            o->u = uv[idx[j]][0]; o->v = uv[idx[j]][1];
        }
        acr_render_mesh(q, 6, g_puff_tex, s->add ? ACR_MESH_ADD : ACR_MESH_SEMI);
    }
    g_nsp = 0;
}

/* ---- the camera ---- */

void hangar3d_camera(float H, float W, float eye[3], float target[3], hangar3d_view *v) {
    (void)W;
    /* low, off to the AC's right, looking up at it */
    const v3f e = V(0.30f * H, 0.09f * H, -2.05f * H), t = V(0.0f, 0.66f * H, 0);
    eye[0] = e.x; eye[1] = e.y; eye[2] = e.z;
    target[0] = t.x; target[1] = t.y; target[2] = t.z;
    const v3f f = norm(sub(t, e));
    const v3f r = norm(cross(V(0, 1, 0), f));
    const v3f u = cross(f, r);
    for (int i = 0; i < 3; ++i) v->eye[i] = eye[i];
    v->right[0] = r.x; v->right[1] = r.y; v->right[2] = r.z;
    v->up[0] = u.x; v->up[1] = u.y; v->up[2] = u.z;
}

/* ---- the maintenance arm: mounted on the right tower, its tool going
 * from one spot on the AC to the next, welding at each ---- */

#define ARM_MOVE 1.3f
#define ARM_WELD 2.4f
static void arm_targets(float H, float W, v3f out[4]) {
    /* on the AC's outer right side, so the arm never crosses its front */
    out[0] = V(0.90f * W, 0.82f * H, -0.10f * H);   /* shoulder */
    out[1] = V(0.55f * W, 0.32f * H, -0.12f * H);   /* knee */
    out[2] = V(0.98f * W, 0.60f * H, -0.12f * H);   /* forearm */
    out[3] = V(0.62f * W, 0.48f * H, -0.10f * H);   /* hip */
}
/* Where the tool is and whether it is welding (0..1 into the weld, or -1). */
static v3f arm_tip(float H, float W, float t, float *weld) {
    v3f tg[4];
    arm_targets(H, W, tg);
    const float cycle = ARM_MOVE + ARM_WELD;
    const int k = (int)floorf(t / cycle);
    const float u = t - (float)k * cycle;
    const v3f from = tg[((k % 4) + 4) % 4], to = tg[(((k + 1) % 4) + 4) % 4];
    /* back off the surface while travelling */
    const v3f away = V(0.16f * H, 0.04f * H, -0.06f * H);
    if (u < ARM_MOVE) {
        float s = u / ARM_MOVE;
        s = s * s * (3 - 2 * s);
        *weld = -1;
        return add(lerp(from, to, s), mul(away, sinf(s * 3.14159265f)));
    }
    *weld = (u - ARM_MOVE) / ARM_WELD;
    /* the torch traces a short seam */
    return add(to, V(0, sinf(*weld * 6.2831853f) * 0.03f * H, 0));
}
/* Two-bone reach from base to tip; the elbow bends up and out. */
static v3f arm_elbow(v3f base, v3f tip, float l1, float l2) {
    v3f d = sub(tip, base);
    float dist = len(d);
    if (dist > l1 + l2 - 1) dist = l1 + l2 - 1;
    if (dist < 1) dist = 1;
    const v3f f = norm(d);
    const float a = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist);
    const float h = sqrtf(fmaxf(l1 * l1 - a * a, 0));
    v3f pole = V(0.4f, 1, 0.2f);
    pole = norm(sub(pole, mul(f, dot(pole, f))));
    return add(add(base, mul(f, a)), mul(pole, h));
}

static const rgb STEEL = { 96, 100, 108 }, DARK = { 48, 50, 56 }, YEL = { 214, 168, 40 }, BLK = { 30, 30, 32 };

/* One gantry tower with its clamp, walkway and railings, on side `sx`
 * (+1 right, -1 left), reaching the AC's side at `reach`. */
static void tower(float H, float sx, float reach, int full) {
    const float tx = sx * (reach + 0.38f * H);
    const float w = 0.07f * H;
    /* four legs and cross bracing */
    for (int i = 0; i < 4; ++i) {
        const float x = tx + (i & 1 ? w : -w), z = (i & 2 ? 0.16f : -0.16f) * H;
        box(x - 0.012f * H, 0, z - 0.012f * H, x + 0.012f * H, 1.22f * H, z + 0.012f * H, STEEL);
    }
    for (int lvl = 0; lvl < 6; ++lvl) {
        const float y0 = lvl * 0.2f * H, y1 = y0 + 0.2f * H;
        bar(V(tx - w, y0, -0.16f * H), V(tx + w, y1, -0.16f * H), 0.008f * H, DARK);
        bar(V(tx + w, y0, -0.16f * H), V(tx - w, y1, -0.16f * H), 0.008f * H, DARK);
        bar(V(tx - sx * w, y0, -0.16f * H), V(tx - sx * w, y1, 0.16f * H), 0.008f * H, DARK);
    }
    box(tx - w * 1.4f, 1.20f * H, -0.2f * H, tx + w * 1.4f, 1.26f * H, 0.2f * H, STEEL);   /* cap */
    if (!full) return;
    /* the clamp: a beam to the AC's shoulder with a pad pressing on it */
    const float cy = 0.80f * H, inner = sx * (reach + 0.015f * H);
    box(fminf(tx, inner), cy - 0.035f * H, -0.05f * H, fmaxf(tx, inner), cy + 0.035f * H, 0.05f * H, C(120, 110, 70));
    box(inner - sx * 0.03f * H, cy - 0.08f * H, -0.08f * H, inner, cy + 0.08f * H, 0.08f * H, DARK);
    for (int k = 0; k < 4; ++k) {   /* hazard bands on the beam */
        const float x0 = tx + (inner - tx) * (0.15f + k * 0.2f);
        box(fminf(x0, x0 + sx * 0.04f * H), cy + 0.034f * H, -0.051f * H, fmaxf(x0, x0 + sx * 0.04f * H), cy + 0.037f * H, 0.051f * H, YEL);
    }
    /* the walkway at chest height, grated, with a railing */
    const float wy = 0.60f * H, wi = sx * (reach + 0.04f * H);
    box(fminf(tx, wi), wy - 0.015f * H, -0.30f * H, fmaxf(tx, wi), wy, 0.30f * H, C(70, 72, 78));
    for (int g = 0; g < 10; ++g) {
        const float z = (-0.30f + g * 0.06f) * H;
        box(fminf(tx, wi), wy, z, fmaxf(tx, wi), wy + 0.003f * H, z + 0.006f * H, DARK);
    }
    for (int p = 0; p < 4; ++p) {
        const float x = tx + (wi - tx) * (float)p / 3.0f;
        bar(V(x, wy, -0.30f * H), V(x, wy + 0.11f * H, -0.30f * H), 0.008f * H, YEL);
    }
    bar(V(tx, wy + 0.11f * H, -0.30f * H), V(wi, wy + 0.11f * H, -0.30f * H), 0.008f * H, YEL);
    bar(V(tx, wy + 0.06f * H, -0.30f * H), V(wi, wy + 0.06f * H, -0.30f * H), 0.006f * H, YEL);
    /* a stair down the tower's back */
    for (int s = 0; s < 8; ++s) {
        const float y = wy - (float)(s + 1) * wy / 8.0f, z = 0.18f * H + (float)s * 0.03f * H;
        box(tx - w, y, z, tx + w, y + 0.01f * H, z + 0.04f * H, DARK);
    }
}

/* Crates in a stack. */
static void crates(float x, float z, float s, int n, float turn) {
    const float c = cosf(turn), sn = sinf(turn);
    for (int i = 0; i < n; ++i) {
        const float ox = (i % 2) * s * 1.05f - s * 0.5f, oy = (float)(i / 2) * s, oz = (i % 3 == 2) * s * 0.2f;
        const v3f ctr = V(x + ox * c - oz * sn, oy + s * 0.5f, z + ox * sn + oz * c);
        obox(ctr, V(c * s * 0.5f, 0, sn * s * 0.5f), V(0, s * 0.5f, 0), V(-sn * s * 0.5f, 0, c * s * 0.5f),
             i % 3 == 1 ? C(70, 84, 64) : C(98, 82, 54));
        obox(add(ctr, V(0, s * 0.18f, 0)), V(c * s * 0.51f, 0, sn * s * 0.51f), V(0, s * 0.03f, 0),
             V(-sn * s * 0.51f, 0, c * s * 0.51f), DARK);
    }
}

void hangar3d_solid(float H, float W, unsigned ms) {
    const float t = (float)ms / 1000.0f;
    const float reach = W * 1.02f + 0.02f * H;   /* just outside the AC's sides */
    /* the floor: big concrete slabs, darker further off (fog does the rest) */
    for (int iz = -3; iz < 12; ++iz)
        for (int ix = -6; ix < 6; ++ix) {
            const float s = 0.75f * H;
            const float x0 = ix * s, z0 = iz * s;
            const int v = (int)(hash((unsigned)(ix * 31 + iz * 7 + 100)) * 10);
            const rgb c = C(58 + v, 60 + v, 63 + v);
            quad(V(x0, 0, z0), V(x0, 0, z0 + s * 0.985f), V(x0 + s * 0.985f, 0, z0 + s * 0.985f), V(x0 + s * 0.985f, 0, z0), c);
        }
    /* the bay's hazard border and the lit pad under the AC */
    const float bx = reach + 0.55f * H, bz0 = -0.55f * H, bz1 = 0.5f * H;
    quad(V(-bx, 2, bz0), V(-bx, 2, bz1), V(bx, 2, bz1), V(bx, 2, bz0), C(72, 74, 78));
    for (int i = 0; i < 40; ++i) {   /* front and back edges: diagonal stripes */
        const float x0 = -bx + (2 * bx) * (float)i / 40.0f, x1 = x0 + (2 * bx) / 40.0f;
        const rgb c = i & 1 ? BLK : YEL;
        quad(V(x0, 3, bz0 - 0.06f * H), V(x0 + 0.02f * H, 3, bz0), V(x1 + 0.02f * H, 3, bz0), V(x1, 3, bz0 - 0.06f * H), c);
        quad(V(x0, 3, bz1), V(x0 + 0.02f * H, 3, bz1 + 0.06f * H), V(x1 + 0.02f * H, 3, bz1 + 0.06f * H), V(x1, 3, bz1), c);
    }
    /* floor vents by the feet (the steam comes up through them) */
    for (int sx = -1; sx <= 1; sx += 2) {
        const float vx = sx * 0.55f * W;
        box(vx - 0.09f * H, 0, -0.32f * H, vx + 0.09f * H, 4, -0.20f * H, C(30, 31, 34));
        for (int g = 0; g < 6; ++g)
            box(vx - 0.085f * H + g * 0.034f * H, 4, -0.315f * H, vx - 0.075f * H + g * 0.034f * H, 7, -0.205f * H, C(84, 86, 90));
    }

    /* the walls: sides with pillars and lamp housings, back wall with a bay door */
    const float side = 3.6f * H, back = 7.5f * H, roof = 3.4f * H, front = -3.0f * H;
    quad2(V(-side, 0, front), V(-side, 0, back), V(-side, roof, back), V(-side, roof, front), C(62, 64, 70), C(40, 42, 48));
    quad2(V(side, 0, back), V(side, 0, front), V(side, roof, front), V(side, roof, back), C(62, 64, 70), C(40, 42, 48));
    quad2(V(-side, 0, back), V(side, 0, back), V(side, roof, back), V(-side, roof, back), C(58, 60, 66), C(36, 38, 44));
    quad(V(-side, roof, front), V(-side, roof, back), V(side, roof, back), V(side, roof, front), C(26, 27, 30));
    for (int s = -1; s <= 1; s += 2)
        for (int i = 0; i < 7; ++i) {   /* pillars marching into the distance */
            const float z = front + 0.4f * H + i * 1.5f * H, x = s * side;
            box(x - 0.12f * H, 0, z - 0.12f * H, x + 0.12f * H, roof, z + 0.12f * H, C(74, 76, 82));
            box(x - s * 0.16f * H - 0.04f * H, 0, z - 0.14f * H, x - s * 0.16f * H + 0.04f * H, 0.35f * H, z + 0.14f * H, YEL);
            box(x - s * 0.2f * H - 0.04f * H, 2.2f * H, z - 0.08f * H, x - s * 0.12f * H, 2.32f * H, z + 0.08f * H, DARK);
            /* wall panels between pillars */
            for (int r = 0; r < 4; ++r)
                box(x - 0.02f * H, 0.5f * H + r * 0.7f * H, z + 0.2f * H, x + 0.02f * H, 0.52f * H + r * 0.7f * H, z + 1.3f * H, C(50, 52, 58));
        }
    /* the bay door: slats, a frame, warning stripes */
    box(-1.7f * H, 0, back - 0.1f * H, 1.7f * H, 2.3f * H, back - 0.02f * H, C(80, 82, 86));
    for (int i = 0; i < 18; ++i)
        box(-1.68f * H, i * 0.125f * H, back - 0.13f * H, 1.68f * H, i * 0.125f * H + 0.02f * H, back - 0.1f * H, C(56, 58, 62));
    box(-1.85f * H, 0, back - 0.2f * H, -1.7f * H, 2.45f * H, back, YEL);
    box(1.7f * H, 0, back - 0.2f * H, 1.85f * H, 2.45f * H, back, YEL);
    box(-1.85f * H, 2.3f * H, back - 0.2f * H, 1.85f * H, 2.45f * H, back, YEL);
    /* roof trusses crossing over, with a crane rail */
    for (int i = 0; i < 8; ++i) {
        const float z = front + i * 1.4f * H;
        box(-side, roof - 0.12f * H, z - 0.04f * H, side, roof - 0.04f * H, z + 0.04f * H, C(54, 56, 60));
        for (int k = -5; k < 5; ++k)
            bar(V(k * 0.7f * H, roof - 0.12f * H, z), V((k + 1) * 0.7f * H, roof - 0.4f * H, z), 0.025f * H, C(48, 50, 54));
        box(-side, roof - 0.42f * H, z - 0.03f * H, side, roof - 0.38f * H, z + 0.03f * H, C(54, 56, 60));
    }
    box(-0.05f * H, roof - 0.5f * H, front, 0.05f * H, roof - 0.42f * H, back, C(70, 66, 44));

    /* background: an empty gantry and parts on the floor, deeper in */
    for (int s = -1; s <= 1; s += 2) {
        const float gx = s * 2.3f * H, gz = 3.2f * H;
        box(gx - 0.5f * H, 0, gz - 0.25f * H, gx + 0.5f * H, 0.03f * H, gz + 0.25f * H, C(70, 70, 60));
        for (int k = -1; k <= 1; k += 2)
            box(gx + k * 0.55f * H - 0.05f * H, 0, gz - 0.05f * H, gx + k * 0.55f * H + 0.05f * H, 1.25f * H, gz + 0.05f * H, STEEL);
        box(gx - 0.6f * H, 1.2f * H, gz - 0.06f * H, gx + 0.6f * H, 1.28f * H, gz + 0.06f * H, STEEL);
    }
    crates(-2.6f * H, 1.0f * H, 0.32f * H, 5, 0.3f);
    crates(2.7f * H, 5.0f * H, 0.36f * H, 6, -0.2f);
    crates(-1.4f * H, 5.6f * H, 0.3f * H, 3, 0.8f);
    /* a tool cart by the right tower */
    {
        const float cx = reach + 0.62f * H, cz = 0.75f * H;
        box(cx - 0.14f * H, 0.03f * H, cz - 0.09f * H, cx + 0.14f * H, 0.28f * H, cz + 0.09f * H, C(150, 40, 34));
        for (int d = 0; d < 3; ++d)
            box(cx - 0.13f * H, 0.07f * H + d * 0.07f * H, cz - 0.092f * H, cx + 0.13f * H, 0.075f * H + d * 0.07f * H, cz - 0.09f * H, DARK);
        for (int k = 0; k < 4; ++k)
            box(cx + (k & 1 ? 0.1f : -0.12f) * H, 0, cz + (k & 2 ? 0.06f : -0.08f) * H, cx + (k & 1 ? 0.12f : -0.1f) * H,
                0.03f * H, cz + (k & 2 ? 0.08f : -0.06f) * H, BLK);
    }
    /* drums */
    for (int i = 0; i < 3; ++i) {
        const float x = -reach - 0.9f * H + i * 0.18f * H, z = 0.9f * H + (i == 1) * 0.15f * H;
        box(x - 0.08f * H, 0, z - 0.08f * H, x + 0.08f * H, 0.24f * H, z + 0.08f * H, i == 1 ? C(60, 80, 110) : C(120, 90, 40));
    }

    /* the gantry around the AC: two towers, the top beam, clamps, walkways */
    tower(H, 1, reach, 1);
    tower(H, -1, reach, 1);
    const float topx = reach + 0.38f * H + 0.1f * H;
    box(-topx, 1.22f * H, -0.1f * H, topx, 1.32f * H, 0.1f * H, C(110, 104, 70));
    box(-topx, 1.32f * H, -0.02f * H, topx, 1.34f * H, 0.02f * H, YEL);
    /* the umbilical cables from the top beam into the AC's back */
    for (int i = 0; i < 4; ++i) {
        const float x0 = (-0.55f + i * 0.36f) * topx, x1 = (-0.3f + i * 0.2f) * W;
        const float sway = sinf(t * 0.6f + i) * 0.01f * H;
        cable(V(x0, 1.24f * H, 0.08f * H), V(x1 + sway, 0.70f * H, 0.24f * H), 0.12f * H + i * 0.02f * H, 0.022f * H,
              i == 2 ? C(150, 120, 40) : C(34, 34, 38));
        box(x1 - 0.03f * H, 0.66f * H, 0.20f * H, x1 + 0.03f * H, 0.74f * H, 0.27f * H, DARK);   /* the plug */
    }

    /* the maintenance arm on the right tower */
    {
        const v3f base = V(reach + 0.30f * H, 1.12f * H, 0.10f * H);
        float weld;
        const v3f tip = arm_tip(H, W, t, &weld);
        const v3f tool = add(tip, V(0.11f * H, 0.03f * H, -0.02f * H));   /* the wrist sits out to the side of the torch */
        const float l1 = 0.46f * H, l2 = 0.42f * H;
        const v3f elbow = arm_elbow(base, tool, l1, l2);
        box(base.x - 0.045f * H, base.y - 0.045f * H, base.z - 0.045f * H, base.x + 0.045f * H, base.y + 0.045f * H, base.z + 0.045f * H, C(150, 116, 40));
        bar(base, elbow, 0.042f * H, C(176, 140, 50));
        obox(elbow, V(0.032f * H, 0, 0), V(0, 0.032f * H, 0), V(0, 0, 0.032f * H), DARK);
        bar(elbow, tool, 0.034f * H, C(176, 140, 50));
        obox(tool, V(0.024f * H, 0, 0), V(0, 0.024f * H, 0), V(0, 0, 0.024f * H), DARK);
        bar(tool, tip, 0.012f * H, C(150, 150, 160));   /* the torch */
        cable(base, lerp(elbow, tool, 0.5f), 0.04f * H, 0.01f * H, BLK);
    }
    /* the scanner arm hanging from the roof on the left */
    {
        const float sw = sinf(t * 0.45f);
        const v3f base = V(-reach - 0.1f * H, 1.9f * H, -0.35f * H);
        const v3f head = V(-0.55f * W - 0.15f * H, 1.05f * H + sw * 0.08f * H, -0.42f * H);
        const v3f elbow = arm_elbow(base, head, 0.5f * H, 0.5f * H);
        bar(V(base.x, roof - 0.4f * H, base.z), base, 0.05f * H, STEEL);
        bar(base, elbow, 0.05f * H, C(90, 96, 104));
        bar(elbow, head, 0.045f * H, C(90, 96, 104));
        obox(head, V(0.05f * H, 0, 0), V(0, 0.035f * H, 0), V(0, 0, 0.05f * H), DARK);
    }
    /* the beacons' housings on the tower caps */
    for (int s = -1; s <= 1; s += 2) {
        const float x = s * (reach + 0.38f * H);
        box(x - 0.03f * H, 1.26f * H, -0.03f * H, x + 0.03f * H, 1.31f * H, 0.03f * H, C(90, 30, 24));
    }
    flush_solid();
}

void hangar3d_fx(float H, float W, unsigned ms, const hangar3d_view *v) {
    make_puff();
    const float t = (float)ms / 1000.0f;
    const float reach = W * 1.02f + 0.02f * H;
    const float roof = 3.4f * H, side = 3.6f * H, front = -3.0f * H;
    /* light cones from the roof onto the AC and through the hall's haze */
    /* (behind the AC, so it rims it with light rather than fogging it over) */
    for (int i = 0; i < 12; ++i) {
        const float y = roof - 0.5f * H - i * 0.2f * H;
        spr(V(0.0f, y, 0.45f * H), 0.15f * H + i * 0.05f * H, 0, C(255, 232, 190), 10, 1);
    }
    spr(V(0, 0.02f * H, 0.1f * H), 0.9f * H, 0, C(255, 230, 190), 22, 1);   /* the pool of light it leaves on the pad */
    for (int s = -1; s <= 1; s += 2)
        for (int i = 0; i < 7; ++i) {   /* the wall lamps' glow, fading into the distance */
            const float z = front + 0.4f * H + i * 1.5f * H;
            const v3f p = V(s * (side - 0.2f * H), 2.18f * H, z);
            spr(p, 0.12f * H, 0, C(255, 190, 110), 200, 1);
            spr(p, 0.45f * H, 0, C(255, 170, 90), 50, 1);
            spr(V(p.x, 0.02f * H, z), 0.6f * H, 0, C(255, 170, 90), 14, 1);   /* its pool on the floor */
        }
    /* the beacons: a rotating red flash */
    for (int s = -1; s <= 1; s += 2) {
        const float ph = frac(t / 1.4f + (s > 0 ? 0.5f : 0));
        const float on = ph < 0.5f ? sinf(ph * 2 * 3.14159265f) : 0;
        const v3f p = V(s * (reach + 0.38f * H), 1.33f * H, 0);
        spr(p, 0.05f * H, 0, C(255, 70, 50), (int)(255 * (0.3f + 0.7f * on)), 1);
        spr(p, 0.3f * H, 0, C(255, 50, 40), (int)(90 * on), 1);
    }
    /* the scanner's beam: a red line sweeping over the AC's left side */
    {
        const float sw = sinf(t * 0.45f);
        const v3f head = V(-0.55f * W - 0.15f * H, 1.05f * H + sw * 0.08f * H, -0.42f * H);
        const float y = 0.5f * H + 0.45f * H * sinf(t * 1.7f);
        const v3f hit = V(-0.55f * W, y, -0.22f * H);
        for (int i = 0; i <= 40; ++i) spr(lerp(head, hit, (float)i / 40.0f), 0.008f * H, 0, C(255, 40, 30), 90, 1);
        for (int i = -16; i <= 16; ++i)   /* the line it paints across the armour */
            spr(add(hit, V(i * 0.012f * H, 0, -0.01f * H)), 0.012f * H, 0, C(255, 60, 40), 110, 1);
        spr(head, 0.04f * H, 0, C(255, 80, 60), 200, 1);
    }
    /* welding: a white-hot point, a flickering glow on the AC and a spray
     * of sparks that fall and bounce */
    {
        float weld;
        const v3f tip = arm_tip(H, W, t, &weld);
        if (weld >= 0) {
            const float fl = 0.7f + 0.3f * hash((unsigned)(ms / 40));
            spr(tip, 0.03f * H * fl, 0, C(255, 255, 240), 255, 1);
            spr(tip, 0.16f * H * fl, 0, C(130, 170, 255), 90, 1);
            spr(tip, 0.4f * H, 0, C(255, 180, 90), (int)(40 * fl), 1);
            for (int i = 0; i < 60; ++i) {
                const float life = 0.6f + 0.5f * hash(i * 3u);
                const float age = frac(t / life + hash(i * 7u)) * life;
                const float born = t - age;   /* sparks only from while welding */
                const float cyc = ARM_MOVE + ARM_WELD, ub = born - floorf(born / cyc) * cyc;
                if (ub < ARM_MOVE) continue;
                const v3f vel = V((hash(i * 11u) - 0.3f) * 0.9f * H, (hash(i * 13u) * 0.6f + 0.1f) * H, (hash(i * 17u) - 0.8f) * 0.7f * H);
                v3f p = add(tip, add(mul(vel, age), V(0, -1.6f * H * age * age, 0)));
                if (p.y < 0) p.y = -p.y * 0.3f;   /* a bounce off the floor */
                const float k = 1 - age / life;
                spr(p, 0.008f * H, 0, C(255, 200 + (int)(55 * k), (int)(120 * k)), (int)(255 * k), 1);
                const v3f p2 = sub(p, mul(vel, 0.02f));   /* a short streak */
                spr(p2, 0.006f * H, 0, C(255, 150, 60), (int)(160 * k), 1);
            }
        }
    }
    /* steam: soft puffs rising out of the floor vents in bursts, curling
     * round the AC's legs, and a slow vent from its back */
    for (int sx = -1; sx <= 1; sx += 2) {
        const v3f src = V(sx * 0.55f * W, 0.02f * H, -0.26f * H);
        for (int i = 0; i < 70; ++i) {
            const float life = 3.2f + 1.2f * hash(i * 5u + (sx > 0));
            const float ph = frac(t / life + hash(i * 19u + (sx > 0 ? 7u : 0)));
            const float age = ph * life;
            /* bursts: the vent breathes every ~5 s */
            const float born = t - age;
            const float breath = 0.35f + 0.65f * powf(0.5f + 0.5f * sinf(born * 1.25f + (sx > 0 ? 1.7f : 0)), 3);
            const float a = hash(i * 23u) * 6.2831853f;
            const float spread = 0.05f * H + age * 0.06f * H;
            v3f p = add(src, V(cosf(a) * spread, age * (0.22f + 0.1f * hash(i * 29u)) * H, sinf(a) * spread * 0.6f));
            p.x += sinf(age * 1.3f + i) * 0.04f * H - sx * age * 0.015f * H;   /* drawn in towards the AC */
            p.z -= age * 0.04f * H;
            const float fade = (ph < 0.12f ? ph / 0.12f : 1) * (1 - ph) * (1 - ph);
            const int g = 200 + (int)(30 * hash(i * 31u));
            spr(p, (0.07f + 0.22f * ph) * H, hash(i * 37u) * 6.28f + age * 0.4f, C(g, g + 4, g + 10), (int)(95 * fade * breath), 0);
        }
    }
    {
        const v3f src = V(0, 0.82f * H, 0.28f * H);
        for (int i = 0; i < 30; ++i) {
            const float life = 4.0f, ph = frac(t / life + hash(i * 41u)), age = ph * life;
            v3f p = add(src, V(sinf(age + i) * 0.05f * H, age * 0.16f * H, age * 0.03f * H));
            const float fade = (ph < 0.15f ? ph / 0.15f : 1) * (1 - ph);
            spr(p, (0.06f + 0.18f * ph) * H, hash(i * 43u) * 6.28f, C(190, 196, 205), (int)(90 * fade), 0);
        }
    }
    /* haze drifting low over the floor, catching the light */
    for (int i = 0; i < 18; ++i) {
        const float x = (hash(i * 47u) - 0.5f) * 4.0f * H + sinf(t * 0.07f + i) * 0.3f * H;
        const float z = hash(i * 53u) * 5.0f * H - 0.5f * H;
        spr(V(x, 0.12f * H, z), 0.7f * H, hash(i * 59u) * 6.28f + t * 0.02f, C(120, 124, 132), 26, 0);
    }
    flush_sprites(v);
}
