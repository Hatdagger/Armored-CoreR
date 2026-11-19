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
    for (uint32_t i = 0; i < s->gmodels; ++i) {
        free(s->gmodel[i].v);
        free(s->gmodel[i].p);
    }
    free(s->gmodel);
    free(s->area);
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

/* ---- the game's own collision (collision.txt) ---- */

static const char *next_int(const char *p, const char *e, long *out, int *ok) {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
    int neg = 0;
    if (p < e && (*p == '-' || *p == '+')) { neg = *p == '-'; ++p; }
    if (p >= e || *p < '0' || *p > '9') { *ok = 0; return p; }
    long v = 0;
    while (p < e && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 4000000000L) *ok = 0; ++p; }
    *out = neg ? -v : v;
    return p;
}

int acr_gen1_stage_parse_game(const char *text, acr_gen1_stage *s) {
    /* Model numbers -> index into s->gmodel (1,024 model numbers). */
    int16_t index[1024];
    for (int i = 0; i < 1024; ++i) index[i] = -1;
    uint32_t cap_a = 0, cap_m = 0;
    int ok = 1;
    acr_gen1_stage_gmodel *cur = NULL;
    uint32_t want_v = 0, want_p = 0;
    for (const char *p = text; *p && ok; ) {
        const char *e = p;
        while (*e && *e != '\n') ++e;
        const char *q = p;
        while (q < e && (*q == ' ' || *q == '\t')) ++q;
        long v[11];
        if (e - q > 5 && !strncmp(q, "area ", 5)) {
            q += 4;
            for (int k = 0; k < 11 && ok; ++k) q = next_int(q, e, &v[k], &ok);
            if (!ok) break;
            if (s->areas == cap_a) {
                cap_a = cap_a ? cap_a * 2 : 128;
                acr_gen1_stage_area *na = realloc(s->area, cap_a * sizeof *na);
                if (!na) { ok = 0; break; }
                s->area = na;
            }
            acr_gen1_stage_area *a = &s->area[s->areas++];
            for (int k = 0; k < 3; ++k) {
                a->lo[k] = (int16_t)v[k];
                a->hi[k] = (int16_t)v[3 + k];
                a->centre[k] = (int16_t)v[6 + k];
            }
            a->quarter = (uint8_t)(v[9] & 3);
            a->model = (int16_t)v[10];          /* a model number for now; made an index below */
        } else if (e - q > 6 && !strncmp(q, "model ", 6)) {
            q += 5;
            for (int k = 0; k < 3 && ok; ++k) q = next_int(q, e, &v[k], &ok);
            if (!ok || v[0] < 0 || v[0] >= 1024 || v[1] < 0 || v[1] > 65536 || v[2] < 0 || v[2] > 65536) { ok = 0; break; }
            if (s->gmodels == cap_m) {
                cap_m = cap_m ? cap_m * 2 : 32;
                acr_gen1_stage_gmodel *nm = realloc(s->gmodel, cap_m * sizeof *nm);
                if (!nm) { ok = 0; break; }
                s->gmodel = nm;
            }
            index[v[0]] = (int16_t)s->gmodels;
            cur = &s->gmodel[s->gmodels++];
            memset(cur, 0, sizeof *cur);
            want_v = (uint32_t)v[1];
            want_p = (uint32_t)v[2];
            cur->v = calloc(want_v ? want_v : 1, sizeof *cur->v);
            cur->p = calloc(want_p ? want_p : 1, sizeof *cur->p);
            if (!cur->v || !cur->p) { ok = 0; break; }
        } else if (e - q > 2 && q[0] == 'v' && q[1] == ' ') {
            q += 1;
            for (int k = 0; k < 3 && ok; ++k) q = next_int(q, e, &v[k], &ok);
            if (!ok || !cur || cur->verts >= want_v) { ok = 0; break; }
            for (int k = 0; k < 3; ++k) cur->v[cur->verts][k] = (int16_t)v[k];
            ++cur->verts;
        } else if (e - q > 2 && q[0] == 'p' && q[1] == ' ') {
            q += 1;
            for (int k = 0; k < 9 && ok; ++k) q = next_int(q, e, &v[k], &ok);
            if (!ok || !cur || cur->polys >= want_p) { ok = 0; break; }
            acr_gen1_stage_gpoly *g = &cur->p[cur->polys++];
            g->flags = (uint16_t)v[0];
            for (int k = 0; k < 3; ++k) g->n[k] = (int16_t)v[1 + k];
            g->d = (int32_t)v[4];
            for (int k = 0; k < 4; ++k) g->c[k] = (uint16_t)v[5 + k];
        }
        p = *e ? e + 1 : e;
    }
    if (ok) {
        for (uint32_t i = 0; i < s->areas; ++i) {
            const int16_t m = s->area[i].model;
            s->area[i].model = (m >= 0 && m < 1024) ? index[m] : -1;
        }
        /* Corners must point at vertices the model has. */
        for (uint32_t i = 0; i < s->gmodels && ok; ++i)
            for (uint32_t k = 0; k < s->gmodel[i].polys && ok; ++k) {
                const acr_gen1_stage_gpoly *g = &s->gmodel[i].p[k];
                const int corners = (g->flags & 1) ? 4 : 3;
                for (int j = 0; j < corners; ++j)
                    if ((uint32_t)((g->c[j] & 0x7FF8) >> 3) >= s->gmodel[i].verts) ok = 0;
            }
    }
    if (!ok) {
        for (uint32_t i = 0; i < s->gmodels; ++i) { free(s->gmodel[i].v); free(s->gmodel[i].p); }
        free(s->gmodel);
        free(s->area);
        s->gmodel = NULL; s->gmodels = 0;
        s->area = NULL; s->areas = 0;
    }
    return ok;
}

/* 0x80074400: a quarter turn of (x, z) about y. Placing a model turns by the
 * area's quarters; going into the model turns by (-quarters) & 3. */
static void qturn(const int32_t in[3], int32_t out[3], unsigned q) {
    const int32_t x = in[0], y = in[1], z = in[2];
    switch (q & 3) {
    case 1: out[0] = -z; out[1] = y; out[2] = x; break;
    case 2: out[0] = -x; out[1] = y; out[2] = -z; break;
    case 3: out[0] = z; out[1] = y; out[2] = -x; break;
    default: out[0] = x; out[1] = y; out[2] = z; break;
    }
}

static int32_t s16w(int32_t v) { return (int16_t)(uint16_t)(uint32_t)v; }

/* 0x800739D8: do [a0, a1] and [b0, b1] meet (ends included)? */
static int ranges_meet(int32_t a0, int32_t a1, int32_t b0, int32_t b1) {
    if (a0 < b0) return a1 >= b0;
    if (b0 < a0) return b1 >= a0;
    return 1;
}

/* The box (min, max), world units, game axes, into an area's model space
 * (0x80074210): (p - centre) x 4, turned, sorted on x and z. */
static void box_to_model(const acr_gen1_stage_area *a, const int32_t lo[3], const int32_t hi[3], int32_t mlo[3],
                         int32_t mhi[3]) {
    const unsigned q = (unsigned)(-(int)a->quarter) & 3;
    int32_t p[3], t[3];
    for (int k = 0; k < 3; ++k) p[k] = s16w((lo[k] - a->centre[k]) * 4);
    qturn(p, t, q);
    for (int k = 0; k < 3; ++k) mlo[k] = s16w(t[k]);
    for (int k = 0; k < 3; ++k) p[k] = s16w((hi[k] - a->centre[k]) * 4);
    qturn(p, t, q);
    for (int k = 0; k < 3; ++k) mhi[k] = s16w(t[k]);
    for (int k = 0; k < 3; k += 2)
        if (mhi[k] < mlo[k]) { const int32_t v = mlo[k]; mlo[k] = mhi[k]; mhi[k] = v; }
}

static const int16_t *gvert(const acr_gen1_stage_gmodel *m, uint16_t c) { return m->v[(c & 0x7FF8) >> 3]; }

/* The polygon's box from the corners its flags name (0x8007386C). */
static int poly_box_meets(const acr_gen1_stage_gmodel *m, const acr_gen1_stage_gpoly *g, const int32_t lo[3],
                          const int32_t hi[3]) {
    const int corners = (g->flags & 1) ? 4 : 3;
    for (int k = 0; k < 3; ++k) {
        const unsigned imin = (g->flags >> (4 + 2 * k)) & 3, imax = (g->flags >> (10 + 2 * k)) & 3;
        if ((int)imin >= corners || (int)imax >= corners) return 0;
        if (!ranges_meet(lo[k], hi[k], gvert(m, g->c[imin])[k], gvert(m, g->c[imax])[k])) return 0;
    }
    return 1;
}

/* GTE NCLIP of three 2-D points (twice the signed area). */
static int64_t nclip(const int32_t a[2], const int32_t b[2], const int32_t c[2]) {
    return (int64_t)a[0] * b[1] + (int64_t)b[0] * c[1] + (int64_t)c[0] * a[1] - (int64_t)a[0] * c[1] -
           (int64_t)b[0] * a[1] - (int64_t)c[0] * b[1];
}

/* Is the point (u, w) inside the outline seen along the main axis (either
 * winding; 0x800732BC)? The corners in strip order: edges 0-1, 1-3, 3-2, 2-0. */
static int outline_inside(const acr_gen1_stage_gmodel *m, const acr_gen1_stage_gpoly *g, int ku, int kw, int32_t u,
                          int32_t w) {
    int32_t c[4][2];
    const int corners = (g->flags & 1) ? 4 : 3;
    for (int j = 0; j < corners; ++j) {
        c[j][0] = gvert(m, g->c[j])[ku];
        c[j][1] = gvert(m, g->c[j])[kw];
    }
    const int32_t pt[2] = { u, w };
    int64_t e[4];
    int n = 0;
    e[n++] = nclip(c[0], c[1], pt);
    e[n++] = nclip(c[2], c[0], pt);
    if (corners == 3) e[n++] = nclip(c[1], c[2], pt);
    else { e[n++] = nclip(c[1], c[3], pt); e[n++] = nclip(c[3], c[2], pt); }
    int pos = 1, neg = 1;
    for (int i = 0; i < n; ++i) { if (e[i] < 0) pos = 0; if (e[i] > 0) neg = 0; }
    return pos || neg;
}

/* One polygon against the box moving by `mot` (all model space): the game's
 * test (0x800734F8) of the four box edges along the polygon's main axis, in
 * the game's order. 1 and the push-corrected move in *out if it meets. */
static int poly_hit(const acr_gen1_stage_gmodel *m, const acr_gen1_stage_gpoly *g, const int32_t lo[3],
                    const int32_t hi[3], const int32_t mot[3], int32_t out[3]) {
    if ((int64_t)g->n[0] * mot[0] + (int64_t)g->n[1] * mot[1] + (int64_t)g->n[2] * mot[2] < 0) return 0;
    if (!poly_box_meets(m, g, lo, hi)) return 0;
    const int D = (g->flags >> 2) & 3;
    if (D > 2) return 0;
    /* The two other axes: a and b (the game's order), the outline seen as
     * (b, a) words. */
    static const int ka[3] = { 1, 0, 0 }, kb[3] = { 2, 2, 1 };
    const int A = ka[D], B = kb[D];
    const int32_t as[2] = { lo[A], hi[A] }, bs[2] = { lo[B], hi[B] };
    for (int e = 0; e < 4; ++e) {
        const int32_t a = as[e & 1], b = bs[e >> 1];
        const int32_t nd = g->n[D];
        if (!nd) return 0;
        const int32_t t = (int32_t)(-((int64_t)g->n[A] * a + (int64_t)g->n[B] * b + g->d) / nd);
        if (t < lo[D] || hi[D] < t) continue;
        if ((g->c[0] & 7) == 1 && !outline_inside(m, g, B, A, b, a)) continue;
        /* The corner: the box's far end along the main axis the way the
         * normal points (0x80073CD8). */
        int32_t corner[3];
        corner[A] = a;
        corner[B] = b;
        corner[D] = nd > 0 ? hi[D] : lo[D];
        /* Push it back to a quarter unit short of the plane (0x80073C10). */
        const int32_t dot = (int32_t)((int64_t)g->n[0] * corner[0] + (int64_t)g->n[1] * corner[1] +
                                      (int64_t)g->n[2] * corner[2]);
        const int32_t val = ((int32_t)(-(int64_t)(dot + (int64_t)g->d)) >> 10) - 16;
        for (int k = 0; k < 3; ++k) out[k] = s16w(s16w((int32_t)(((int64_t)g->n[k] * val) >> 14)) + mot[k]);
        return 1;
    }
    return 0;
}

static int32_t quarter_away(int32_t v) { return v >= 0 ? (v + 3) >> 2 : -((-v + 3) >> 2); }

int acr_gen1_stage_try(const acr_gen1_stage *s, const int32_t pos[3], int32_t step[3], int32_t half, int32_t height,
                       uint8_t mask) {
    if (!s->areas) return 0;
    /* Into the game's axes (x mirrored). */
    const int32_t gp[3] = { -pos[0], pos[1], pos[2] }, gs[3] = { -step[0], step[1], step[2] };
    int32_t dest[3], lo[3], hi[3], mot[3];
    for (int k = 0; k < 3; ++k) { dest[k] = s16w(gp[k] + gs[k]); mot[k] = s16w(gs[k] * 4); }
    lo[0] = dest[0] - half; hi[0] = dest[0] + half;
    lo[2] = dest[2] - half; hi[2] = dest[2] + half;
    lo[1] = dest[1] - height; hi[1] = dest[1];
    int found = 0;
    int32_t best = 0, best_out[3] = { 0, 0, 0 };
    unsigned best_q = 0;
    for (uint32_t ai = 0; ai < s->areas; ++ai) {
        const acr_gen1_stage_area *a = &s->area[ai];
        if (a->model < 0) continue;
        if (!ranges_meet(lo[0], hi[0], a->lo[0], a->hi[0]) || !ranges_meet(lo[1], hi[1], a->lo[1], a->hi[1]) ||
            !ranges_meet(lo[2], hi[2], a->lo[2], a->hi[2])) continue;
        const acr_gen1_stage_gmodel *m = &s->gmodel[a->model];
        int32_t mlo[3], mhi[3], mm[3], t[3];
        box_to_model(a, lo, hi, mlo, mhi);
        qturn(mot, t, (unsigned)(-(int)a->quarter) & 3);
        for (int k = 0; k < 3; ++k) mm[k] = s16w(t[k]);
        for (uint32_t pi = 0; pi < m->polys; ++pi) {
            const acr_gen1_stage_gpoly *g = &m->p[pi];
            int32_t out[3];
            if (!poly_hit(m, g, mlo, mhi, mm, out)) continue;
            /* The nearest correction among the classes the body doesn't pass
             * through (0x8007257C): smallest |x| + |y| + |z| - priority x 64. */
            if ((g->c[1] & 7) & mask) continue;
            const int32_t score = (out[0] < 0 ? -out[0] : out[0]) + (out[1] < 0 ? -out[1] : out[1]) +
                                  (out[2] < 0 ? -out[2] : out[2]) - (int32_t)(g->c[3] & 7) * 64;
            if (!found || score < best) {
                best = score;
                memcpy(best_out, out, sizeof best_out);
                best_q = a->quarter;
                found = 1;
            }
        }
    }
    if (!found) return 0;
    int32_t w[3];
    qturn(best_out, w, best_q);
    /* Back to world units, rounded away from zero (0x80078AB0), and back
     * to the engine's axes. */
    step[0] = -quarter_away(s16w(w[0]));
    step[1] = quarter_away(s16w(w[1]));
    step[2] = quarter_away(s16w(w[2]));
    return 1;
}

int acr_gen1_stage_move(const acr_gen1_stage *s, const int32_t pos[3], int32_t step[3], int32_t half, int32_t height,
                        uint8_t mask) {
    int changed = 0;
    for (int tries = 0; tries < 6; ++tries) {
        if (!acr_gen1_stage_try(s, pos, step, half, height, mask)) return changed;
        changed = 1;
    }
    step[0] = step[1] = step[2] = 0;
    return 1;
}

int acr_gen1_stage_floor_probe(const acr_gen1_stage *s, const int32_t pos[3], int32_t half, int32_t height,
                               uint8_t mask, int32_t *floor_y) {
    if (!s->areas) return 0;
    const int32_t gp[3] = { -pos[0], pos[1], pos[2] };
    int32_t lo[3], hi[3];
    lo[0] = gp[0] - half; hi[0] = gp[0] + half;
    lo[2] = gp[2] - half; hi[2] = gp[2] + half;
    lo[1] = gp[1] - height; hi[1] = gp[1] + 4096;
    int found = 0;
    int32_t best = 0;
    for (uint32_t ai = 0; ai < s->areas; ++ai) {
        const acr_gen1_stage_area *a = &s->area[ai];
        if (a->model < 0) continue;
        if (!ranges_meet(lo[0], hi[0], a->lo[0], a->hi[0]) || !ranges_meet(lo[1], hi[1], a->lo[1], a->hi[1]) ||
            !ranges_meet(lo[2], hi[2], a->lo[2], a->hi[2])) continue;
        const acr_gen1_stage_gmodel *m = &s->gmodel[a->model];
        /* 0x8007621C: the same placement, kept in 32 bits. */
        const unsigned q = (unsigned)(-(int)a->quarter) & 3;
        int32_t p[3], mlo[3], mhi[3];
        for (int k = 0; k < 3; ++k) p[k] = (lo[k] - a->centre[k]) * 4;
        qturn(p, mlo, q);
        for (int k = 0; k < 3; ++k) p[k] = (hi[k] - a->centre[k]) * 4;
        qturn(p, mhi, q);
        for (int k = 0; k < 3; k += 2)
            if (mhi[k] < mlo[k]) { const int32_t v = mlo[k]; mlo[k] = mhi[k]; mhi[k] = v; }
        for (uint32_t pi = 0; pi < m->polys; ++pi) {
            const acr_gen1_stage_gpoly *g = &m->p[pi];
            if (g->n[1] <= 0) continue;                       /* floors only */
            if ((g->c[1] & 7) & mask) continue;
            if (!poly_box_meets(m, g, mlo, mhi)) continue;
            /* The four corner columns (0x80075DD8 -> 0x80075B58). */
            for (int c = 0; c < 4; ++c) {
                const int32_t x = (c & 1) ? mhi[0] : mlo[0], z = (c & 2) ? mhi[2] : mlo[2];
                const int32_t y = (int32_t)(-((int64_t)g->n[0] * x + (int64_t)g->n[2] * z + g->d) / g->n[1]);
                if (y < s16w(mlo[1]) || s16w(mhi[1]) < y) continue;
                if ((g->c[0] & 7) == 1 && !outline_inside(m, g, 2, 0, z, x)) continue;
                const int32_t wy = (int16_t)(a->centre[1] + (y >> 2));
                if (!found || wy < best) { best = wy; found = 1; }
            }
        }
    }
    if (!found) return 0;
    if (best < pos[1]) best = pos[1];
    *floor_y = best;
    return 1;
}

/* ---- out of area ---- */

uint8_t acr_gen1_stage_area_bits(const acr_gen1_stage *s, int32_t x, int32_t y, int32_t z) {
    uint8_t bits = 0;
    for (unsigned i = 0; i < s->bounds && i < ACR_GEN1_STAGE_BOUNDS; ++i) {
        const acr_gen1_stage_bound *b = &s->bound[i];
        if (x < b->lo[0] || x > b->hi[0] || y < b->lo[1] || y > b->hi[1] || z < b->lo[2] || z > b->hi[2]) continue;
        if (b->bit < 8) bits |= (uint8_t)(1u << b->bit);
    }
    return bits;
}

int acr_gen1_stage_has_area_rule(const acr_gen1_stage *s) {
    uint8_t have = 0;
    for (unsigned i = 0; i < s->bounds && i < ACR_GEN1_STAGE_BOUNDS; ++i)
        if (s->bound[i].bit < 8) have |= (uint8_t)(1u << s->bound[i].bit);
    return (have & ACR_GEN1_AREA_INNER) != 0;
}
