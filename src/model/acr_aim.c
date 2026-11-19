#include "acr_aim.h"

#include <math.h>
#include <string.h>

enum { FRESH = -2 };   /* no frame drawn yet: the first aim is set at once (0x80059868) */

void acr_aim_reset(acr_aim_state *st) {
    memset(st, 0, sizeof *st);
    st->table = FRESH;
}

int acr_aim_clamp(int rest, int min, int max, int add) {
    const int v = add + rest;
    int below = (v - min) & 0xFFF;
    const int range = (max - min) & 0xFFF;
    if (range >= below) return below + min - rest;             /* inside */
    if (below > 0x800) below = 0x1000 - below;                  /* distances to each end */
    int above = (v - max) & 0xFFF;
    if (above > 0x800) above = 0x1000 - above;
    return below < above ? min - rest : max - rest;
}

void acr_aim_targets(const acr_aim_table *t, const int16_t aim[3], const int16_t parent[3],
                     int16_t out[ACR_AIM_MAX_JOINTS][3]) {
    int delta[3];
    for (int a = 0; a < 3; ++a)
        delta[a] = acr_aim_clamp(t->rest[a], t->min[a] + parent[a], t->max[a] + parent[a], aim[a]);
    for (int k = 0; k < t->count; ++k)
        for (int a = 0; a < 3; ++a) {
            const int w = delta[a] < 0 ? t->joint[k].neg[a] : t->joint[k].pos[a];
            out[k][a] = (int16_t)(t->joint[k].rest[a] + ((delta[a] * w) >> 12));
        }
}

static float wrap(float d) {
    d = fmodf(d + 2048.0f, 4096.0f);
    if (d < 0.0f) d += 4096.0f;
    return d - 2048.0f;
}

/* The pose as drawn now: animation, with the eased joints in place. */
static void drawn(const acr_aim_state *st, const acr_skeleton *skel, const acr_pose *anim, acr_pose *out) {
    *out = *anim;
    for (int i = 0; i < skel->count; ++i)
        if (st->easing[i]) {
            for (int a = 0; a < 3; ++a) out->rot[i][a] = (int16_t)lroundf(st->ang[i][a]);
            out->own_rot[i] = st->own[i];
        }
}

static void angles_of(const acr_skeleton *skel, const acr_pose *p, int node, int16_t out[3]) {
    float w[ACR_SKEL_MAX_NODES][9];
    if (node < 0) { out[0] = out[1] = out[2] = 0; return; }
    acr_pose_world_rot(skel, p, w);
    acr_rot_angles(w[node], out);
}

void acr_aim_apply(acr_aim_state *st, const acr_skeleton *skel, const acr_anim_set *set, int table,
                   const int16_t aim[3], float ticks, acr_pose *pose) {
    if (table < 0 || table >= ACR_AIM_MAX || !set->aim[table].count) table = -1;
    const int fresh = st->table == FRESH;
    if (fresh) st->table = -1;
    acr_pose now;
    if (table != st->table) {
        drawn(st, skel, pose, &now);
        /* The old table's joints go back to the animation, easing. */
        if (st->table >= 0) {
            const acr_aim_table *old = &set->aim[st->table];
            for (int k = 0; k < old->count; ++k) {
                const int n = old->joint[k].node;
                st->aimed[n] = 0;
                if (st->own[n]) {
                    /* its angles turned back into its parent's space (0x800599AC) */
                    int16_t pa[3];
                    angles_of(skel, &now, skel->node[n].parent, pa);
                    for (int a = 0; a < 3; ++a) st->ang[n][a] -= pa[a];
                    st->own[n] = 0;
                }
            }
        }
        /* The new table's joints start from where they are drawn. */
        if (table >= 0) {
            const acr_aim_table *t = &set->aim[table];
            for (int k = 0; k < t->count; ++k) {
                const int n = t->joint[k].node;
                if (!st->easing[n])
                    for (int a = 0; a < 3; ++a) st->ang[n][a] = pose->rot[n][a];
                st->easing[n] = st->aimed[n] = 1;
            }
            const int first = t->joint[0].node;
            int16_t own[3];
            angles_of(skel, &now, first, own);         /* its drawn rotation, as its own (0x80059AB4) */
            for (int a = 0; a < 3; ++a) st->ang[first][a] = own[a];
            st->own[first] = 1;
        }
        st->table = table;
    }
    int16_t target[ACR_AIM_MAX_JOINTS][3];
    if (table >= 0) {
        const acr_aim_table *t = &set->aim[table];
        drawn(st, skel, pose, &now);
        int16_t pa[3];
        angles_of(skel, &now, skel->node[t->joint[0].node].parent, pa);
        acr_aim_targets(t, aim, pa, target);
    }
    const float f = ticks > 0.0f ? 1.0f - powf(7.0f / 8.0f, ticks) : 0.0f;
    for (int n = 0; n < skel->count; ++n) {
        if (!st->easing[n]) continue;
        float want[3];
        int k = -1;
        if (st->aimed[n] && table >= 0)
            for (int j = 0; j < set->aim[table].count; ++j)
                if (set->aim[table].joint[j].node == n) { k = j; break; }
        for (int a = 0; a < 3; ++a) want[a] = k >= 0 ? (float)target[k][a] : (float)pose->rot[n][a];
        int settled = 1;
        for (int a = 0; a < 3; ++a) {
            const float d = wrap(want[a] - st->ang[n][a]);
            if (fresh || fabsf(d) < 64.0f) st->ang[n][a] = want[a];   /* snaps within 64 */
            else { st->ang[n][a] += d * f; settled = 0; }
            if (fabsf(st->ang[n][a]) > 4096.0f) st->ang[n][a] = fmodf(st->ang[n][a], 4096.0f);
        }
        if (settled && k < 0) st->easing[n] = 0;                         /* back on the animation */
        if (st->easing[n]) {
            for (int a = 0; a < 3; ++a) pose->rot[n][a] = (int16_t)lroundf(st->ang[n][a]);
            pose->own_rot[n] = st->own[n];
        }
    }
}
