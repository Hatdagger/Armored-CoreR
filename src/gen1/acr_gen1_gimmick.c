/* Gen 1 moving map parts (doors). See acr_gen1_gimmick.h. */
#include "acr_gen1_gimmick.h"

#include <stdlib.h>
#include <string.h>

static const char *num(const char *p, const char *e, long *out, int *ok) {
    while (p < e && (*p == ' ' || *p == '\t')) ++p;
    char *end;
    *out = strtol(p, &end, 10);
    if (end == p || end > e) *ok = 0;
    return end;
}

void acr_gen1_gimmicks_parse(const char *text, acr_gen1_gimmicks *out) {
    memset(out, 0, sizeof *out);
    for (const char *p = text; p && *p; ) {
        const char *e = p;
        while (*e && *e != '\n') ++e;
        if (e - p > 10 && !strncmp(p, "breakable ", 10)) {
            long v[11];
            int ok = 1;
            const char *q = p + 10;
            for (int k = 0; k < 11 && ok; ++k) q = num(q, e, &v[k], &ok);
            if (ok && (v[8] == 0 || v[8] == 1) && out->breakables < ACR_GEN1_MAX_BREAKABLES) {
                acr_gen1_breakable *k = &out->breakable[out->breakables++];
                memset(k, 0, sizeof *k);
                k->area = (int16_t)v[0];
                k->game_area = (int16_t)v[1];
                for (int j = 0; j < 3; ++j) k->centre[j] = (int16_t)v[2 + j];
                k->size_xz = (int16_t)v[5];
                k->size_y = (int16_t)v[6];
                k->hp = v[7] > 0 ? (int32_t)v[7] : 1;
                k->message = (uint8_t)(v[8] ? v[9] : 0);
                k->target = (int16_t)v[10];
                long t = 0, bo = 0;
                int ok2 = 1;
                q = num(q, e, &t, &ok2);
                if (ok2) q = num(q, e, &bo, &ok2);
                k->top = (int16_t)(ok2 ? t : k->centre[1] - k->size_y);
                k->bottom = (int16_t)(ok2 ? bo : k->centre[1]);
            }
        } else if (e - p > 5 && !strncmp(p, "lift ", 5)) {
            long v[18];
            int ok = 1;
            const char *q = p + 5;
            for (int k = 0; k < 18 && ok; ++k) q = num(q, e, &v[k], &ok);
            if (ok && out->lifts < ACR_GEN1_MAX_LIFTS) {
                acr_gen1_lift *l = &out->lift[out->lifts++];
                memset(l, 0, sizeof *l);
                l->area = (int16_t)v[0];
                l->game_area = (int16_t)v[1];
                for (int j = 0; j < 3; ++j) l->centre[j] = (int16_t)v[2 + j];
                l->size_xz = (int16_t)v[5];
                l->size_y = (int16_t)v[6];
                l->speed = (uint16_t)v[7];
                l->ya = (int16_t)v[8];
                l->yb = (int16_t)v[9];
                l->way = (uint8_t)(v[10] & 1);
                l->xa = (int16_t)v[11];
                l->za = (int16_t)v[12];
                l->xb = (int16_t)v[13];
                l->zb = (int16_t)v[14];
                l->sound_move = (uint8_t)v[15];
                l->sound_stop = (uint8_t)v[16];
                l->sound_flip = (uint8_t)v[17];
            }
        } else if (e - p > 5 && !strncmp(p, "ship ", 5)) {
            long v[20];
            int ok = 1;
            const char *q = p + 5;
            for (int k = 0; k < 20 && ok; ++k) q = num(q, e, &v[k], &ok);
            if (ok && out->ships < ACR_GEN1_MAX_SHIPS) {
                acr_gen1_ship *sh = &out->ship[out->ships++];
                memset(sh, 0, sizeof *sh);
                sh->area = (int16_t)v[0];
                sh->game_area = (int16_t)v[1];
                for (int j = 0; j < 3; ++j) {
                    sh->centre[j] = (int16_t)v[2 + j];
                    sh->size[j] = (int16_t)v[5 + j];
                    sh->lo[j] = (int16_t)v[8 + j];
                    sh->hi[j] = (int16_t)v[11 + j];
                    sh->vel[j] = (int16_t)v[14 + j];                       /* 0x800A4F6C: as written */
                    sh->top[j] = (int16_t)(v[14 + j] < 0 ? -v[14 + j] : v[14 + j]);
                    sh->acc[j] = (int16_t)v[17 + j];
                }
            }
        } else if (e - p > 7 && !strncmp(p, "button ", 7)) {
            long v[9];
            int ok = 1;
            const char *q = p + 7;
            for (int k = 0; k < 9 && ok; ++k) q = num(q, e, &v[k], &ok);
            if (ok && out->buttons < ACR_GEN1_MAX_BUTTONS) {
                acr_gen1_button *b = &out->button[out->buttons++];
                memset(b, 0, sizeof *b);
                b->area = (int16_t)v[0];
                b->game_area = (int16_t)v[1];
                for (int j = 0; j < 3; ++j) b->centre[j] = (int16_t)v[2 + j];
                b->size_xz = (int16_t)v[5];
                b->size_y = (int16_t)v[6];
                b->lift = (int16_t)v[7];
                b->sound = (uint8_t)v[8];
            }
        } else if (e - p > 5 && !strncmp(p, "door ", 5)) {
            long v[17];
            int ok = 1;
            const char *q = p + 5;
            for (int k = 0; k < 17 && ok; ++k) q = num(q, e, &v[k], &ok);
            if (ok && out->doors < ACR_GEN1_MAX_DOORS) {
                acr_gen1_door *d = &out->door[out->doors++];
                memset(d, 0, sizeof *d);
                d->area = (int16_t)v[0];
                d->game_area = (int16_t)v[1];
                d->quarter = (uint8_t)(v[2] & 3);
                for (int k = 0; k < 3; ++k) d->centre[k] = (int16_t)v[3 + k];
                d->size_xz = (int16_t)v[6];
                d->size_y = (int16_t)v[7];
                d->speed = (uint16_t)v[8];
                d->distance = (int16_t)v[9];
                d->radio = (uint8_t)v[10];
                d->axis = (uint8_t)v[11];
                d->sound_open = (uint8_t)v[12];
                d->sound_close = (uint8_t)v[13];
                d->stays_open = (uint8_t)v[14];
                d->opens_near = (uint8_t)v[15];
                const int n = (int)v[16];
                for (int k = 0; k < ACR_GEN1_DOOR_PARTNERS; ++k) d->partner[k] = -1;
                for (int k = 0; k < n; ++k) {
                    long a = -1;
                    int ok2 = 1;
                    q = num(q, e, &a, &ok2);
                    if (!ok2) break;
                    if (d->partners < ACR_GEN1_DOOR_PARTNERS) d->partner[d->partners++] = (int16_t)a;
                }
                /* The set-up (0x800A2600): ticks = distance / speed, and the
                 * step spread over them (0x800135DC). */
                if (d->speed) {
                    const int t = (uint16_t)d->distance / d->speed;
                    d->ticks = (uint8_t)(t > 255 ? 255 : t);
                }
                d->reopen = 0;   /* data +4 / speed: 0 on every door read */
                if (d->ticks) {
                    const int dist = d->distance;
                    d->step_lo = (int16_t)(dist / d->ticks);
                    d->step_hi = (int16_t)(d->step_lo + (dist < 0 ? -1 : 1));
                    d->acc = (int16_t)((d->ticks + 1) >> 1);
                    const int r = dist % d->ticks;
                    d->rem = (int16_t)(r < 0 ? -r : r);
                }
            }
        }
        p = *e ? e + 1 : e;
    }
}

static int box_touch(const int16_t c[3], const int32_t off[3], int32_t sxz, int32_t sy, const int32_t p[3],
                     int32_t radius, int32_t height);

int acr_gen1_door_find(const acr_gen1_gimmicks *g, int game_area) {
    for (int i = 0; i < g->doors; ++i)
        if (g->door[i].game_area == game_area) return i;
    return -1;
}

static int by_area(const acr_gen1_gimmicks *g, int area) {
    for (int i = 0; i < g->doors; ++i)
        if (g->door[i].area == area) return i;
    return -1;
}

void acr_gen1_door_set_state(acr_gen1_gimmicks *g, int i, int s) {
    if (i < 0 || i >= g->doors) return;
    acr_gen1_door *d = &g->door[i];
    d->state = (uint8_t)s;
    if (s == 4) d->radio = 0;
    for (int k = 0; k < d->partners; ++k) {
        const int j = by_area(g, d->partner[k]);
        if (j >= 0 && j != i) g->door[j].state = (uint8_t)s;
    }
}

void acr_gen1_door_unlock(acr_gen1_gimmicks *g, int i) { acr_gen1_door_set_state(g, i, 4); }

int acr_gen1_door_near(const acr_gen1_door *d, const int32_t p[3], int32_t radius, int32_t height) {
    const int32_t r = d->size_xz + radius;
    const int32_t dx = p[0] - d->centre[0], dz = p[2] - d->centre[2];
    if (dx < -r || dx > r || dz < -r || dz > r) return 0;
    const int32_t cy = d->centre[1], py = p[1];
    if (cy < py) { if (cy < py - height) return 0; }
    else if (py < cy - d->size_y) return 0;
    return (int64_t)dx * dx + (int64_t)dz * dz <= (int64_t)r * r;
}

int acr_gen1_gimmicks_hit(acr_gen1_gimmicks *g, const int32_t p[3], int32_t reach, int32_t damage) {
    int any = 0;
    for (int i = 0; i < g->breakables; ++i) {
        acr_gen1_breakable *k = &g->breakable[i];
        if (k->hp <= 0) continue;
        const int32_t r = k->size_xz / 2 + reach;
        const int32_t dx = p[0] - k->centre[0], dz = p[2] - k->centre[2];
        /* The box: its record's top and bottom (y down). */
        if (dx < -r || dx > r || dz < -r || dz > r) continue;
        if (p[1] > k->bottom + reach || p[1] < k->top - reach) continue;
        any = 1;
        k->hp -= damage > 0 ? damage : 1;
        if (k->hp > 0) continue;
        k->hp = 0;
        /* Broken (0x800A2708): the area goes; stand-in: put far below. */
        k->offset[1] = 30000;
        const int d = acr_gen1_door_find(g, k->target);
        if (d >= 0) {
            if (k->message == 2) acr_gen1_door_unlock(g, d);
            else if (k->message == 1) acr_gen1_door_set_state(g, d, 1);
        }
    }
    return any;
}

void acr_gen1_gimmicks_blade(acr_gen1_gimmicks *g, const int32_t player[3], int32_t radius, int32_t height,
                             int32_t reach, acr_gen1_gimmick_events *ev) {
    const int32_t p[3] = { player[0], player[1] + reach, player[2] };
    for (int i = 0; i < g->doors; ++i) {
        acr_gen1_door *d = &g->door[i];
        if (!d->ticks || d->state >= 4) continue;
        if (!acr_gen1_door_near(d, p, radius + reach, height + 2 * reach)) continue;
        if (d->radio) { if (ev) ev->radio = d->radio; }
        else if (d->count < d->ticks && d->state != 1 && d->state != 3) acr_gen1_door_set_state(g, i, 1);
    }
    /* 0x8100 to a lift or its button: a flip. */
    for (int i = 0; i < g->buttons; ++i) {
        const acr_gen1_button *b = &g->button[i];
        const int32_t z[3] = { 0, 0, 0 };
        if (box_touch(b->centre, z, b->size_xz, b->size_y, p, radius + reach, height + 2 * reach)) {
            const int j = acr_gen1_lift_find(g, b->lift);
            if (j >= 0) acr_gen1_lift_flip(&g->lift[j]);
        }
    }
    for (int i = 0; i < g->lifts; ++i)
        if (box_touch(g->lift[i].centre, g->lift[i].offset, g->lift[i].size_xz, g->lift[i].size_y, p, radius + reach,
                      height + 2 * reach))
            acr_gen1_lift_flip(&g->lift[i]);
}

/* One step of the move (0x800A217C states 1-4 and 10): 1 while moving, 2
 * when it has just shut. */
static int door_move(acr_gen1_door *d, int32_t out[3]) {
    const int s = d->state;
    if (s != 1 && s != 2 && s != 4 && s != 10) {
        /* 3: open, waiting for the player to leave (0x800A244C). */
        return s == 3 ? 3 : 0;
    }
    const int back = s == 2 || s == 10;
    d->acc = (int16_t)(d->acc - d->rem);
    int16_t step;
    if (d->acc > 0) step = d->step_lo;
    else { d->acc = (int16_t)(d->acc + d->ticks); step = d->step_hi; }
    out[0] = out[1] = out[2] = 0;
    if (d->axis < 4) {
        /* Sideways: (0, 0, step) turned by axis + the area's quarter turns
         * (+2 going back) (0x80013C18). */
        const int q = (d->axis + d->quarter + (back ? 2 : 0)) & 3;
        if (q == 0) out[2] = step;
        else if (q == 1) out[0] = -step;
        else if (q == 2) out[2] = -step;
        else out[0] = step;
    } else {
        int dir = d->axis == 4 ? -1 : 1;   /* 4: up (y down) */
        if (back) dir = -dir;
        out[1] = dir * step;
    }
    for (int k = 0; k < 3; ++k) d->offset[k] += out[k];
    if (!back) {
        if (++d->count >= d->ticks) d->state = (uint8_t)(s + 2);
        return 1;
    }
    if (d->count) --d->count;
    if (d->count == 0) {
        /* Shut: back exactly where it started. */
        d->state = 0;
        for (int k = 0; k < 3; ++k) { out[k] -= d->offset[k]; d->offset[k] = 0; }
        return 2;
    }
    return 1;
}

void acr_gen1_gimmicks_tick(acr_gen1_gimmicks *g, const int32_t player[3], int32_t radius, int32_t height,
                            int32_t (*move)[3], acr_gen1_gimmick_events *ev) {
    if (ev) memset(ev, 0, sizeof *ev);
    for (int i = 0; i < g->doors; ++i) {
        acr_gen1_door *d = &g->door[i];
        move[i][0] = move[i][1] = move[i][2] = 0;
        if (!d->ticks) continue;
        const int near = acr_gen1_door_near(d, player, radius, height);
        if (d->opens_near) {
            /* 0x800A2498: opens when the player comes near. */
            if (d->state != 1 && near && d->count < d->ticks) acr_gen1_door_set_state(g, i, 1);
        } else if (near && !d->was_near && d->state < 4) {
            /* Message 0x8100 (the player runs into it): a locked door plays
             * its radio line; otherwise it opens. */
            if (d->radio) { if (ev) ev->radio = d->radio; }
            else if (d->count < d->ticks) acr_gen1_door_set_state(g, i, 1);
        }
        d->was_near = (uint8_t)near;
        const int r = door_move(d, move[i]);
        if (r == 3) {
            if (!d->stays_open && !near) acr_gen1_door_set_state(g, i, 2);
        } else if (d->state == 2 && d->count >= d->reopen && !d->stays_open && near) {
            /* Closing with the player back in the way: opens again. */
            acr_gen1_door_set_state(g, i, 1);
        }
        if (ev && r == 1 && d->count == 1 && d->sound_open && (d->state == 1 || d->state == 4)) {
            ev->sound = d->sound_open;
            ev->sound_area = d->area;
        }
        if (ev && r == 2 && d->sound_close) {
            ev->sound = d->sound_close;
            ev->sound_area = d->area;
        }
    }
}

/* ---- lifts ---- */

int acr_gen1_lift_find(const acr_gen1_gimmicks *g, int game_area) {
    for (int i = 0; i < g->lifts; ++i)
        if (g->lift[i].game_area == game_area) return i;
    return -1;
}

static int32_t lift_y(const acr_gen1_lift *l) { return l->centre[1] + l->offset[1]; }

void acr_gen1_lift_flip(acr_gen1_lift *l) {
    if (l->vel != 0) {
        const int32_t y = lift_y(l);
        const int32_t gone = l->way ? y - l->ya : l->yb - y;
        if ((int32_t)l->speed * 17 >= gone / 2) return;
    }
    l->way ^= 1;
    l->blocked = 0;
}

/* Touching: the player's body against the box (centre at its foot, y
 * down), a little slack so standing on top counts. Stand-in for the
 * game's contact message: the collision code that sends it isn't read. */
#define LIFT_SLACK 32
static int box_touch(const int16_t c[3], const int32_t off[3], int32_t sxz, int32_t sy, const int32_t p[3],
                     int32_t radius, int32_t height) {
    const int32_t r = sxz / 2 + radius + LIFT_SLACK;
    const int32_t dx = p[0] - (c[0] + off[0]), dz = p[2] - (c[2] + off[2]);
    if (dx < -r || dx > r || dz < -r || dz > r) return 0;
    const int32_t cy = c[1] + off[1];
    return p[1] >= cy - sy - LIFT_SLACK && p[1] - height <= cy + LIFT_SLACK;
}

int acr_gen1_lift_under(const acr_gen1_lift *l, const int32_t p[3], int32_t radius) {
    const int32_t r = l->size_xz / 2 + radius;
    const int32_t dx = p[0] - (l->centre[0] + l->offset[0]), dz = p[2] - (l->centre[2] + l->offset[2]);
    if (dx < -r || dx > r || dz < -r || dz > r) return 0;
    const int32_t top = lift_y(l) - l->size_y;
    return p[1] >= top - 64 && p[1] <= top + 64;
}

/* The tick 0x800A1F44 for one lift: its step this tick into out[]. */
static void lift_move(acr_gen1_lift *l, int32_t out[3]) {
    out[0] = out[1] = out[2] = 0;
    const int32_t y = lift_y(l);
    const int32_t left = l->way ? l->yb - y : y - l->ya;
    int32_t sp = l->way ? l->vel : -l->vel;
    const int32_t acc = l->speed, top = (int32_t)l->speed * 32, brake = (int32_t)l->speed * 17;
    int32_t step;
    if (left < brake && (sp -= acc) <= 0) {
        if (left == 0) { l->step = 0; return; }
        const int32_t a = left < 4 ? left : 4;
        step = l->way ? a : -a;
    } else {
        if (left >= brake) { sp += acc; if (sp > top) sp = top; }
        l->vel = (int16_t)(l->way ? sp : -sp);
        step = (int16_t)l->vel >> 5;
    }
    l->step = (int16_t)step;
    out[1] = step;
    const int32_t dy = l->yb - l->ya;
    if (dy) {
        const int32_t ny = y + step - l->ya;
        if (l->xa != l->xb) out[0] = l->xa + ny * (l->xb - l->xa) / dy - (l->centre[0] + l->offset[0]);
        if (l->za != l->zb) out[2] = l->za + ny * (l->zb - l->za) / dy - (l->centre[2] + l->offset[2]);
    }
    for (int k = 0; k < 3; ++k) l->offset[k] += out[k];
}

void acr_gen1_lifts_tick(acr_gen1_gimmicks *g, const int32_t player[3], int32_t radius, int32_t height,
                         int32_t (*move)[3]) {
    for (int i = 0; i < g->buttons; ++i) {
        acr_gen1_button *b = &g->button[i];
        const int32_t z[3] = { 0, 0, 0 };
        const int near = box_touch(b->centre, z, b->size_xz, b->size_y, player, radius, height);
        if (near && !b->was_near) {
            const int j = acr_gen1_lift_find(g, b->lift);
            if (j >= 0) acr_gen1_lift_flip(&g->lift[j]);
        }
        b->was_near = (uint8_t)near;
    }
    for (int i = 0; i < g->lifts; ++i) {
        acr_gen1_lift *l = &g->lift[i];
        const int near = box_touch(l->centre, l->offset, l->size_xz, l->size_y, player, radius, height);
        if (near && !l->was_near) acr_gen1_lift_flip(l);   /* 0x8100 from the player */
        l->was_near = (uint8_t)near;
        lift_move(l, move[i]);
    }
}

/* ---- ships ---- */

void acr_gen1_ships_tick(acr_gen1_gimmicks *g, int32_t (*move)[3]) {
    for (int i = 0; i < g->ships; ++i) {
        acr_gen1_ship *sh = &g->ship[i];
        for (int k = 0; k < 3; ++k) {
            /* 0x800A49BC: speed up, held within the top speed. */
            int32_t v = (int16_t)(sh->vel[k] + sh->acc[k]);
            if (sh->acc[k] > 0) { if (sh->top[k] < v) v = sh->top[k]; }
            else if (v < -sh->top[k]) v = -sh->top[k];
            sh->vel[k] = (int16_t)v;
            move[i][k] = v;
            sh->offset[k] += v;
            /* 0x800A4B68: past the range's end, the speed-up turns round. */
            const int32_t c = sh->centre[k] + sh->offset[k];
            if (sh->acc[k] > 0 ? sh->hi[k] < c : c < sh->lo[k]) sh->acc[k] = (int16_t)-sh->acc[k];
        }
    }
}

int acr_gen1_ship_over(const acr_gen1_ship *sh, const int32_t p[3], int32_t radius) {
    const int32_t dx = p[0] - (sh->centre[0] + sh->offset[0]), dz = p[2] - (sh->centre[2] + sh->offset[2]);
    const int32_t rx = sh->size[0] / 2 + radius, rz = sh->size[2] / 2 + radius;
    return dx >= -rx && dx <= rx && dz >= -rz && dz <= rz;
}

int acr_gen1_breakable_find(const acr_gen1_gimmicks *g, int game_area) {
    for (int i = 0; i < g->breakables; ++i)
        if (g->breakable[i].game_area == game_area) return i;
    return -1;
}
