#include "acr_gen1_match.h"

#include <string.h>

/* A knock-out (or leaving the area) settles 12 Gen 1 ticks after it. */
#define SETTLE_MS (12 * 1000 / 22)

/* Fresh pilots for a round: seeded from the rules and the round number. */
static void start_ai(acr_gen1_match *m, const acr_gen1_match_rules *r) {
    for (int k = 0; k < ACR_GEN1_MAX_FIGHTERS; ++k) {
        if (!(r->ai_mask >> k & 1)) { memset(&m->ai[k], 0, sizeof m->ai[k]); continue; }
        acr_gen1_ai_init(&m->ai[k], &r->ai[k], r->ai_seed + 0x1234u + (uint32_t)k * 977u + (uint32_t)m->round * 7919u);
    }
}

void acr_gen1_match_init(acr_gen1_match *m, const acr_gen1_battle *start, const acr_gen1_match_rules *r) {
    memset(m, 0, sizeof *m);
    m->b = *start;
    /* Each round starts on a Gen 1 tick boundary, whatever the caller's fight
     * was doing before (a phase left over from earlier play differs between
     * PCs and would put the two machines' 22-a-second ticks on different steps). */
    m->b.tick_phase = 0;
    m->round_ms = r->round_ms;
    m->round = 1;
    acr_gen1_battle_retarget(&m->b);
    start_ai(m, r);
}

static void next_round(acr_gen1_match *m, const acr_gen1_battle *start, const acr_gen1_match_rules *r) {
    m->b = *start;
    m->b.tick_phase = 0;
    acr_gen1_battle_retarget(&m->b);
    m->round_ms = r->round_ms;
    m->ko_ms = m->over_ms = 0;
    m->round_over = m->result = m->time_up = 0;
    if (m->round < 255) ++m->round;
    start_ai(m, r);
}

/* The open floor's square walls (the shown position too, so drawing stops there). */
static void floor_walls(acr_gen1_match *m, int32_t half) {
    if (m->b.stage || half <= 0) return;
    const int32_t lim = half * 64;
    for (int k = 0; k < acr_gen1_battle_fighters(&m->b); ++k) {
        acr_gen1_ac *q = &m->b.f[k].ac;
        int32_t *const xs[3] = { &q->x, &q->px, &q->sx }, *const zs[3] = { &q->z, &q->pz, &q->sz };
        for (int c = 0; c < 3; ++c) {
            if (*xs[c] > lim) { *xs[c] = lim; q->vx = 0; }
            if (*xs[c] < -lim) { *xs[c] = -lim; q->vx = 0; }
            if (*zs[c] > lim) { *zs[c] = lim; q->vz = 0; }
            if (*zs[c] < -lim) { *zs[c] = -lim; q->vz = 0; }
        }
    }
}

void acr_gen1_match_step(acr_gen1_match *m, const acr_gen1_battle *start, const acr_gen1_match_rules *r,
                         const acr_gen1_input in[], const acr_gen1_actions act[]) {
    const int32_t step = m->b.step_ms;
    const int n = acr_gen1_battle_fighters(&m->b);
    ++m->frame;
    if (m->round_over) {
        /* Controls let go: the ACs drop to the ground where they are. */
        m->over_ms += step;
        if (r->next_round_ms > 0 && m->over_ms >= r->next_round_ms) {
            next_round(m, start, r);
            return;
        }
        static const acr_gen1_input no_in[ACR_GEN1_MAX_FIGHTERS] = { { 0 } };
        static const acr_gen1_actions no_act[ACR_GEN1_MAX_FIGHTERS] = { { 0 } };
        acr_gen1_battle_step(&m->b, no_in, no_act);
        floor_walls(m, r->arena_half);
        return;
    }
    if (r->round_ms > 0) {
        m->round_ms -= step;
        if (m->round_ms < 0) m->round_ms = 0;
    }
    const int time_up = r->round_ms > 0 && m->round_ms <= 0;
    const int res = acr_gen1_battle_result(&m->b, time_up);
    if (!res) m->ko_ms = 0;      /* back in the area before it settled */
    if (res && (time_up || (m->ko_ms += step) >= SETTLE_MS)) {
        m->round_over = 1;
        m->result = (uint8_t)res;
        m->time_up = (uint8_t)time_up;
        if (res == 1 || res == 2) if (m->wins[res - 1] < 255) ++m->wins[res - 1];
        return;
    }
    /* Players' controls, and the computer pilots' for their ACs. */
    acr_gen1_input use_in[ACR_GEN1_MAX_FIGHTERS];
    acr_gen1_actions use_act[ACR_GEN1_MAX_FIGHTERS];
    memset(use_in, 0, sizeof use_in);
    memset(use_act, 0, sizeof use_act);
    const int32_t half = r->ai_half ? r->ai_half : r->arena_half;
    for (int k = 0; k < n; ++k) {
        if (r->ai_mask >> k & 1) acr_gen1_ai_step(&m->ai[k], &m->b, k, half, &use_in[k], &use_act[k]);
        else if (k < 2 || !r->ai_mask) { use_in[k] = in[k]; use_act[k] = act[k]; }
    }
    memcpy(m->shown_in, use_in, sizeof m->shown_in);
    acr_gen1_battle_step(&m->b, use_in, use_act);
    floor_walls(m, r->arena_half);
}

/* FNV-1a over 32-bit values. */
static uint32_t mix(uint32_t h, int32_t v) {
    const uint32_t u = (uint32_t)v;
    for (int i = 0; i < 4; ++i) { h ^= (u >> (i * 8)) & 0xFFu; h *= 16777619u; }
    return h;
}

uint32_t acr_gen1_match_hash(const acr_gen1_match *m) {
#define H(v) (h = mix(h, (int32_t)(v)))
    uint32_t h = 2166136261u;
    H(m->frame); H(m->round_ms); H(m->ko_ms); H(m->over_ms);
    H(m->round_over); H(m->result); H(m->time_up); H(m->round); H(m->wins[0]); H(m->wins[1]);
    const acr_gen1_battle *b = &m->b;
    H(b->rng); H(b->tick_phase);
    H(b->count); H(b->targeted);
    for (int i = 0; i < acr_gen1_battle_fighters(b); ++i) {
        const acr_gen1_fighter *f = &b->f[i];
        const acr_gen1_ac *a = &f->ac;
        H(a->x); H(a->y); H(a->z); H(a->vx); H(a->vy); H(a->vz);
        H(a->yaw); H(a->turn_rate); H(a->pitch_fine); H(a->pitch); H(a->coast); H(a->energy);
        H(a->drained); H(a->boost_was_down); H(a->boost_released); H(a->state); H(a->timer);
        H(a->vy_at_touchdown); H(a->landing_pause); H(a->anim); H(a->anim_frame); H(a->span_left);
        H(a->phase);
        for (int s = 0; s < 3; ++s) {
            const acr_gen1_stepper *st = &a->step[s];
            H(st->step); H(st->step_alt); H(st->counter); H(st->dec); H(st->frames);
        }
        H(f->ap); H(f->current); H(f->locked); H(f->locks); H(f->lock_count);
        for (int w = 0; w < ACR_GEN1_MAX_WEAPONS; ++w) H(f->ammo[w]);
        H(f->cooldown); H(f->hold_left); H(f->lock_wait); H(f->lock_lead); H(f->lead_yaw); H(f->lead_pitch);
        H(f->cursor_yaw); H(f->cursor_pitch); H(f->volley_ticks);
        H(f->swing); H(f->swing_hits); H(f->lunge_done); H(f->swing_target); H(f->lunge_x); H(f->lunge_z);
        H(f->kneeling); H(f->human_plus); H(f->kneel_ms);
        H(f->intercept_burst); H(f->intercept_shot); H(f->hit_flash); H(f->last_missile);
        H(f->cam_pitch); H(f->cam_yaw); H(f->team); H(f->target);
        const acr_gen1_ai *ai = &m->ai[i];
        H(ai->rng); H(ai->think_ms); H(ai->move); H(ai->strafe); H(ai->strafe_ms); H(ai->boost);
        H(ai->boost_ms); H(ai->hop); H(ai->fire); H(ai->want_locks); H(ai->want_weapon); H(ai->switch_ms);
        H(ai->blade_ms); H(ai->plant_ms); H(ai->aim_error); H(ai->last_ap); H(ai->hurt_ms); H(ai->prev_boost);
    }
    for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
        const acr_gen1_shot *s = &b->shot[k];
        if (!s->active) continue;
        H(k); H(s->owner); H(s->weapon); H(s->missile); H(s->flags);
        H(s->x); H(s->y); H(s->z); H(s->vx); H(s->vy); H(s->vz);
        H(s->travelled); H(s->yaw); H(s->pitch); H(s->age); H(s->ignition); H(s->split); H(s->target);
        H(s->attack);
    }
    H(b->units);
    for (int u = 0; u < b->units; ++u) {
        const acr_gen1_unit *un = &b->unit[u];
        H(un->active); H(un->x); H(un->z); H(un->yaw); H(un->ap); H(un->state); H(un->state_ticks);
        H(un->burst); H(un->gun_yaw); H(un->gun_pitch); H(un->down); H(un->target);
    }
#undef H
    return h;
}
