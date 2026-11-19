/* Gen 1 arena stages: collision polygons. See acr_gen1_stage.h. */
#include "acr_gen1_stage.h"

#include <stdlib.h>
#include <string.h>

#define FLAT_NY 2896              /* cos 45 degrees x 4096 */

static uint64_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static int64_t iabs64(int64_t v) { return v < 0 ? -v : v; }

/* ---- parsing ---- */

/* A decimal number from an OBJ line, rounded to a whole unit (half away from 0). */
static const char *parse_num(const char *p, const char *e, int32_t *out, int *ok) {
    while (p < e && (*p == ' ' || *p == '\t')) ++p;
    int neg = 0;
    if (p < e && (*p == '-' || *p == '+')) { neg = *p == '-'; ++p; }
    if (p >= e || ((*p < '0' || *p > '9') && *p != '.')) { *ok = 0; return p; }
    int64_t whole = 0;
    while (p < e && *p >= '0' && *p <= '9') { whole = whole * 10 + (*p - '0'); if (whole > 100000000) *ok = 0; ++p; }
    int half = 0;
    if (p < e && *p == '.') {
        ++p;
        if (p < e && *p >= '0' && *p <= '9') half = *p >= '5';
        while (p < e && *p >= '0' && *p <= '9') ++p;
    }
    const int64_t v = whole + half;
    *out = (int32_t)(neg ? -v : v);
    return p;
}

int acr_gen1_stage_add(acr_gen1_stage *s, const int32_t (*v)[3], int corners) {
    if (corners < 3 || corners > 4) return 1;
    if ((s->polys & 255) == 0) {
        acr_gen1_stage_poly *np = realloc(s->poly, (s->polys + 256) * sizeof *np);
        if (!np) return 0;
        s->poly = np;
    }
    acr_gen1_stage_poly *p = &s->poly[s->polys];
    memset(p, 0, sizeof *p);
    p->corners = (uint8_t)corners;
    for (int i = 0; i < corners; ++i)
        for (int k = 0; k < 3; ++k) {
            p->v[i][k] = v[i][k];
            if (!i || v[i][k] < p->lo[k]) p->lo[k] = v[i][k];
            if (!i || v[i][k] > p->hi[k]) p->hi[k] = v[i][k];
        }
    /* Normal by Newell's method (works for slightly bent quads too). */
    int64_t n[3] = { 0, 0, 0 };
    for (int i = 0; i < corners; ++i) {
        const int32_t *a = p->v[i], *b = p->v[(i + 1) % corners];
        n[0] += (int64_t)(a[1] - b[1]) * (a[2] + b[2]);
        n[1] += (int64_t)(a[2] - b[2]) * (a[0] + b[0]);
        n[2] += (int64_t)(a[0] - b[0]) * (a[1] + b[1]);
    }
    while (iabs64(n[0]) > (1 << 30) || iabs64(n[1]) > (1 << 30) || iabs64(n[2]) > (1 << 30)) {
        n[0] /= 2; n[1] /= 2; n[2] /= 2;
    }
    const uint64_t len = isqrt64((uint64_t)(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]));
    if (!len) return 1;                                   /* degenerate: skip */
    int64_t d = 0;
    for (int k = 0; k < 3; ++k) {
        p->n[k] = (int32_t)(n[k] * 4096 / (int64_t)len);
        d -= (int64_t)p->n[k] * p->v[0][k];
    }
    p->d = d;
    p->flat = (uint8_t)(p->n[1] >= FLAT_NY || p->n[1] <= -FLAT_NY);
    for (int k = 0; k < 3; ++k) {
        if (!s->polys || p->lo[k] < s->lo[k]) s->lo[k] = p->lo[k];
        if (!s->polys || p->hi[k] > s->hi[k]) s->hi[k] = p->hi[k];
    }
    ++s->polys;
    return 1;
}

int acr_gen1_stage_parse(const char *text, acr_gen1_stage *out) {
    memset(out, 0, sizeof *out);
    int32_t (*vert)[3] = NULL;
    uint32_t nv = 0, cap = 0;
    int ok = 1;
    for (const char *p = text; *p && ok; ) {
        const char *e = p;
        while (*e && *e != '\n') ++e;
        const char *s = p;
        while (s < e && (*s == ' ' || *s == '\t')) ++s;
        if (e - s > 2 && s[0] == 'v' && s[1] == ' ') {
            if (nv == cap) {
                cap = cap ? cap * 2 : 1024;
                int32_t (*nvv)[3] = realloc(vert, cap * sizeof *vert);
                if (!nvv) { ok = 0; break; }
                vert = nvv;
            }
            const char *q = s + 1;
            int32_t c[3];
            for (int k = 0; k < 3; ++k) q = parse_num(q, e, &c[k], &ok);
            /* OBJ y is up; the fight's y is down. */
            vert[nv][0] = c[0]; vert[nv][1] = -c[1]; vert[nv][2] = c[2];
            ++nv;
        } else if (e - s > 2 && s[0] == 'f' && s[1] == ' ') {
            int32_t c[4][3];
            int corners = 0;
            const char *q = s + 1;
            while (q < e && corners < 5) {
                while (q < e && (*q == ' ' || *q == '\t' || *q == '\r')) ++q;
                if (q >= e) break;
                long idx = 0;
                while (q < e && *q >= '0' && *q <= '9') idx = idx * 10 + (*q++ - '0');
                while (q < e && *q != ' ' && *q != '\t') ++q;          /* skip /vt/vn */
                if (idx < 1 || (uint32_t)idx > nv) { ok = 0; break; }
                if (corners < 4) memcpy(c[corners], vert[idx - 1], sizeof c[0]);
                ++corners;
            }
            if (ok && corners >= 3 && corners <= 4 && !acr_gen1_stage_add(out, (const int32_t (*)[3])c, corners)) ok = 0;
        }
        p = *e ? e + 1 : e;
    }
    free(vert);
    if (!ok) acr_gen1_stage_free(out);
    return ok;
}

void acr_gen1_stage_free(acr_gen1_stage *s) {
    free(s->poly);
    memset(s, 0, sizeof *s);
}

/* ---- queries ---- */

/* Is p inside the polygon, seen along its main axis? */
static int inside(const acr_gen1_stage_poly *pl, int64_t px, int64_t py, int64_t pz) {
    const int64_t ax = iabs64(pl->n[0]), ay = iabs64(pl->n[1]), az = iabs64(pl->n[2]);
    const int drop = ax >= ay && ax >= az ? 0 : ay >= az ? 1 : 2;
    const int u = drop == 0 ? 1 : 0, w = drop == 2 ? 1 : 2;
    const int64_t pu = drop == 0 ? py : px, pw = drop == 2 ? py : pz;
    int pos = 0, neg = 0;
    for (int i = 0; i < pl->corners; ++i) {
        const int32_t *a = pl->v[i], *b = pl->v[(i + 1) % pl->corners];
        const int64_t c = (int64_t)(b[u] - a[u]) * (pw - a[w]) - (int64_t)(b[w] - a[w]) * (pu - a[u]);
        if (c > 0) ++pos;
        if (c < 0) ++neg;
    }
    return !(pos && neg);
}

/* Surface y of a flat polygon at (x, z), if (x, z) is over it. */
static int surface_y(const acr_gen1_stage_poly *pl, int32_t x, int32_t z, int32_t *y) {
    if (x < pl->lo[0] || x > pl->hi[0] || z < pl->lo[2] || z > pl->hi[2]) return 0;
    const int64_t yy = -((int64_t)pl->n[0] * x + (int64_t)pl->n[2] * z + pl->d) / pl->n[1];
    if (!inside(pl, x, yy, z)) return 0;
    *y = (int32_t)yy;
    return 1;
}

int acr_gen1_stage_floor(const acr_gen1_stage *s, int32_t x, int32_t z, int32_t y_top, int32_t *floor_y) {
    int found = 0;
    for (uint32_t i = 0; i < s->polys; ++i) {
        const acr_gen1_stage_poly *pl = &s->poly[i];
        int32_t y;
        if (!pl->flat || pl->hi[1] < y_top || !surface_y(pl, x, z, &y) || y < y_top) continue;
        if (!found || y < *floor_y) { *floor_y = y; found = 1; }
    }
    return found;
}

int acr_gen1_stage_ceiling(const acr_gen1_stage *s, int32_t x, int32_t z, int32_t y_bottom, int32_t *ceiling_y) {
    int found = 0;
    for (uint32_t i = 0; i < s->polys; ++i) {
        const acr_gen1_stage_poly *pl = &s->poly[i];
        int32_t y;
        if (!pl->flat || pl->lo[1] > y_bottom || !surface_y(pl, x, z, &y) || y > y_bottom) continue;
        if (!found || y > *ceiling_y) { *ceiling_y = y; found = 1; }
    }
    return found;
}

int acr_gen1_stage_push(const acr_gen1_stage *s, int32_t *x, int32_t *z, int32_t feet_y, int32_t height,
                        int32_t radius, int32_t step) {
    int moved = 0;
    const int32_t top = feet_y - height, low = feet_y - step;
    if (low <= top) return 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < s->polys; ++i) {
            const acr_gen1_stage_poly *pl = &s->poly[i];
            if (pl->flat || pl->hi[1] < top || pl->lo[1] > low) continue;
            if (*x + radius < pl->lo[0] || *x - radius > pl->hi[0] || *z + radius < pl->lo[2] ||
                *z - radius > pl->hi[2]) continue;
            const int64_t hn = (int64_t)isqrt64((uint64_t)((int64_t)pl->n[0] * pl->n[0] + (int64_t)pl->n[2] * pl->n[2]));
            if (hn < 64) continue;
            /* Three heights on the body: just above the step, the middle, the top. */
            int64_t best = 0;
            const int32_t hs[3] = { low, (low + top) / 2, top };
            for (int h = 0; h < 3; ++h) {
                const int64_t sd = ((int64_t)pl->n[0] * *x + (int64_t)pl->n[1] * hs[h] + (int64_t)pl->n[2] * *z + pl->d) / 4096;
                if (iabs64(sd) >= radius) continue;
                const int64_t qx = *x - sd * pl->n[0] / 4096, qy = hs[h] - sd * pl->n[1] / 4096,
                              qz = *z - sd * pl->n[2] / 4096;
                if (!inside(pl, qx, qy, qz)) continue;
                /* Out to `radius` on the side the body is on. */
                const int64_t need = (sd >= 0 ? radius - sd : -radius - sd);
                if (iabs64(need) > iabs64(best)) best = need;
            }
            if (!best) continue;
            /* Moving sideways by m along the wall's level normal changes the
             * distance by m x hn / 4096. */
            const int64_t m = best * 4096 / hn + (best > 0 ? 1 : -1);
            *x += (int32_t)(m * pl->n[0] / hn);
            *z += (int32_t)(m * pl->n[2] / hn);
            moved = 1;
        }
    }
    return moved;
}

int acr_gen1_stage_segment(const acr_gen1_stage *s, const int32_t a[3], const int32_t b[3], int32_t *t) {
    int32_t lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
        lo[k] = a[k] < b[k] ? a[k] : b[k];
        hi[k] = a[k] < b[k] ? b[k] : a[k];
    }
    int found = 0;
    int32_t best = 1025;
    for (uint32_t i = 0; i < s->polys; ++i) {
        const acr_gen1_stage_poly *pl = &s->poly[i];
        if (hi[0] < pl->lo[0] || lo[0] > pl->hi[0] || hi[1] < pl->lo[1] || lo[1] > pl->hi[1] ||
            hi[2] < pl->lo[2] || lo[2] > pl->hi[2]) continue;
        const int64_t sa = (int64_t)pl->n[0] * a[0] + (int64_t)pl->n[1] * a[1] + (int64_t)pl->n[2] * a[2] + pl->d;
        const int64_t sb = (int64_t)pl->n[0] * b[0] + (int64_t)pl->n[1] * b[1] + (int64_t)pl->n[2] * b[2] + pl->d;
        if ((sa > 0 && sb > 0) || (sa < 0 && sb < 0) || sa == sb) continue;
        const int32_t tt = (int32_t)(sa * 1024 / (sa - sb));
        if (tt >= best) continue;
        const int64_t px = a[0] + (int64_t)(b[0] - a[0]) * tt / 1024, py = a[1] + (int64_t)(b[1] - a[1]) * tt / 1024,
                      pz = a[2] + (int64_t)(b[2] - a[2]) * tt / 1024;
        if (!inside(pl, px, py, pz)) continue;
        best = tt;
        found = 1;
    }
    if (found) *t = best;
    return found;
}
