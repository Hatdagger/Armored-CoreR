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
            if (ok && v[8] == 1 && out->breakables < ACR_GEN1_MAX_BREAKABLES) {
                acr_gen1_breakable *k = &out->breakable[out->breakables++];
                memset(k, 0, sizeof *k);
                k->area = (int16_t)v[0];
                k->game_area = (int16_t)v[1];
                for (int j = 0; j < 3; ++j) k->centre[j] = (int16_t)v[2 + j];
                k->size_xz = (int16_t)v[5];
                k->size_y = (int16_t)v[6];
                k->hp = v[7] > 0 ? (int32_t)v[7] : 1;
                k->message = (uint8_t)v[9];
                k->target = (int16_t)v[10];
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
        /* The box: centre at its foot (y down), size_y up. */
        if (dx < -r || dx > r || dz < -r || dz > r) continue;
        if (p[1] > k->centre[1] + reach || p[1] < k->centre[1] - k->size_y - reach) continue;
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
