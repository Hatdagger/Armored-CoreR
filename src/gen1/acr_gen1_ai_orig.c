/* The original Gen 1 arena AI: a port of Project Phantasma's arena brain
 * (PP arena engine, FDAT 205) with Master of Arena's extra rules as a style.
 *
 * Source: ACG1PLUS docs/research/mapping/ai.md (a static read of both
 * discs' code; nothing there has been seen running in a live game yet).
 * Section numbers below are ai.md's. Where ai.md leaves a gap, this file
 * says so: "[read]" marks details this port read itself from the PP arena
 * code (pp-work disc pack, also static), "[ours]" marks choices of this
 * port with no disc source. See docs/AI.md.
 *
 * One battle step is one game frame (Gen 1 runs 22 steps a second, the
 * same pace as the disc), so frame counts are kept as they are. Angles are
 * 4096 a turn, distances world units, weights fractions of 4096. */
#include "acr_gen1_ai.h"

#include <string.h>

/* ---- small maths (as in acr_gen1_ai.c) ---- */

static uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}
static int32_t wrap(int32_t a) { a &= 4095; return a >= 2048 ? a - 4096 : a; }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static uint16_t angle_of(int64_t s, int64_t c) {
    if (!s && !c) return 0;
    const int64_t as = s < 0 ? -s : s, ac = c < 0 ? -c : c;
    int lo = 0, hi = 1024;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if ((int64_t)acr_gen1_sin((uint16_t)mid) * ac >= (int64_t)acr_gen1_cos((uint16_t)mid) * as) hi = mid;
        else lo = mid + 1;
    }
    if (c >= 0 && s >= 0) return (uint16_t)lo;
    if (c < 0 && s >= 0) return (uint16_t)(2048 - lo);
    if (c < 0) return (uint16_t)(2048 + lo);
    return (uint16_t)((4096 - lo) & 4095);
}
static int rnd(acr_gen1_ai *ai, int below) {            /* the pilot's own random numbers */
    ai->rng = ai->rng * 1103515245u + 12345u;
    return below > 0 ? (int)(((ai->rng >> 16) & 0x7FFF) % (uint32_t)below) : 0;
}
/* The fuzzy-maths helpers (ai.md 2: raise caps at 3686, lower floors at 409). */
static uint16_t raise(uint16_t a, int32_t d) { const int32_t v = a + d; return (uint16_t)(v > 3686 ? 3686 : v); }
static uint16_t lower(uint16_t a, int32_t d) { const int32_t v = a - d; return (uint16_t)(v < 409 ? 409 : v); }
/* 0x80080EB8: 0 below a - h, rising to 4096 at a + h; 0x80080EF4 the reverse. */
static int32_t ramp_up(int32_t x, int32_t a, int32_t h) {
    const int32_t t = x - a + h;
    return t <= 0 ? 0 : h <= 0 || t >= 2 * h ? 4096 : t * 4096 / (2 * h);
}
static int32_t ramp_down(int32_t x, int32_t a, int32_t h) { return 4096 - ramp_up(x, a, h); }
static uint16_t pct(int p) { return (uint16_t)((p > 100 ? 100 : p) * 4096 / 100); }

/* ---- records (ai.md 3) ---- */

enum { B4 = 0, B5, B6, B7, B12, B13, B14 };   /* behaviour slots this port runs */

void acr_gen1_ai_orig_from_pp(acr_gen1_ai_orig_record *r, const uint8_t a[ACR_GEN1_AI_PP_RECORD_SIZE], int opponent) {
    memset(r, 0, sizeof *r);
    r->present = 1;
    r->disc = ACR_GEN1_STYLE_PP;
    r->human_plus = a[0x00];
    for (int i = 0; i < 3; ++i) { r->desire[i] = a[0x01 + i]; r->spread[i] = a[0x05 + i]; }
    r->reroll = a[0x04];
    r->unk8 = a[0x08];
    r->blade = a[0x09];
    r->gate = a[0x0A];
    r->tracking = a[0x10];
    r->delay = a[0x11];
    for (int i = 0; i < 4; ++i) memcpy(r->tune[B4 + i], a + 0x12 + 6 * i, 6);     /* behaviours 4-7 */
    for (int i = 0; i < 3; ++i) memcpy(r->tune[B12 + i], a + 0x2A + 6 * i, 6);    /* 12-14 */
    /* PP has no fields for Master of Arena's extras; if MoA style is forced
     * on a PP opponent it uses the MoA editor's defaults [ours]. */
    r->fire = 51; r->avoid = 50; r->desperate = 0;
    r->opponent = (uint8_t)(opponent < 0 ? 0 : opponent > 255 ? 255 : opponent);
    r->main_arena = 0;
}

void acr_gen1_ai_orig_from_moa(acr_gen1_ai_orig_record *r, const uint8_t a[ACR_GEN1_AI_RECORD_SIZE], int opponent,
                               int main_arena) {
    memset(r, 0, sizeof *r);
    r->present = 1;
    r->disc = ACR_GEN1_STYLE_MOA;
    r->human_plus = a[0x00];
    for (int i = 0; i < 3; ++i) { r->desire[i] = a[0x01 + i]; r->spread[i] = a[0x05 + i]; }
    r->reroll = a[0x04];
    r->unk8 = a[0x08];
    r->blade = a[0x09];
    r->gate = a[0x0A];
    r->tracking = a[0x0B];         /* MoA +0x0B (ai.md 3 table: "lock") */
    r->delay = a[0x0C];            /* MoA +0x0C (ai.md marks it "?") */
    r->fire = a[0x0D];
    r->avoid = a[0x0E];
    r->desperate = a[0x0F];
    /* MoA's behaviour tuning (+0x14..+0x23) is not laid out in ai.md. The
     * editor writes five (weight, 25) pairs at +0x14..+0x1D and six bytes
     * after; this port reads the pairs as base weight and learning blend
     * of: +0x14 hold outside reach (6), +0x16 air (5), +0x18 keep distance
     * (13), +0x1A missile dodge (7), +0x1C close in (4), and +0x1E as
     * wander (14), from which slider feeds which [ours, a guess].
     * Commitment times are Phantasma's records 0-29 (10, 10, 2, 1, -, 60, 60). */
    static const uint8_t commit[ACR_GEN1_AI_ORIG_TUNES] = { 10, 10, 2, 1, 0, 60, 60 };
    static const uint8_t at[ACR_GEN1_AI_ORIG_TUNES] = { 0x1C, 0x16, 0x14, 0x1A, 0, 0x18, 0x1E };
    for (int i = 0; i < ACR_GEN1_AI_ORIG_TUNES; ++i) {
        if (i == B12) continue;                       /* off: no stage objects (below) */
        r->tune[i][0] = a[at[i]];
        r->tune[i][1] = a[at[i] + 1];
        r->tune[i][2] = commit[i];
    }
    r->opponent = (uint8_t)(opponent < 0 ? 0 : opponent > 255 ? 255 : opponent);
    r->main_arena = (uint8_t)(main_arena != 0);
}

/* ---- start ---- */

void acr_gen1_ai_orig_init(acr_gen1_ai *ai) {
    struct acr_gen1_ai_orig *o = &ai->o;
    acr_gen1_ai_orig_record *r = &ai->prof.rec;
    if (!r->present) {
        /* No record (a preset, an AI file, a ghost): the AI editor's record
         * from the sliders, exactly as Master of Arena's editor writes it. */
        uint8_t m[ACR_GEN1_AI_RECORD_SIZE];
        acr_gen1_ai_record(&ai->prof, m);
        acr_gen1_ai_orig_from_moa(r, m, 0, 0);
    }
    const int opt = acr_gen1_ai_style_option();
    o->style = (uint8_t)(opt == ACR_GEN1_STYLE_PP || opt == ACR_GEN1_STYLE_MOA ? opt : r->disc);
    for (int i = 0; i < ACR_GEN1_AI_ORIG_TUNES; ++i) o->learned[i] = pct(r->tune[i][0]);
    for (int i = 0; i < 3; ++i) o->desire[i] = pct(r->desire[i] + (r->spread[i] ? rnd(ai, r->spread[i] + 1) : 0));
    o->blade = pct(r->blade);
    o->fire = pct(r->fire);
    o->avoid = pct(r->avoid);
    o->gate = r->gate;
    o->tracking_on = (uint8_t)(r->tracking != 0);
    o->track = (uint16_t)(r->tracking * 4096 / 600 + 512);     /* ai.md 3, +0x10 */
    o->delay = (uint8_t)(r->delay > 30 ? 30 : r->delay);
    for (int k = 0; k < ACR_GEN1_MAX_WEAPONS; ++k) o->wweight[k] = 2048;
    o->wpick = -1;
    o->cur = -1;
    o->side = (int8_t)(rnd(ai, 2) ? 1 : -1);
    o->last_ap = o->last_tap = -1;
    /* MoA's fire hold (ai.md 6): main arena only, opponents 0-10 hold 10
     * frames, the rest 20; Ex Arenas none. */
    o->hold = (uint8_t)(o->style == ACR_GEN1_STYLE_MOA && r->main_arena ? (r->opponent <= 10 ? 10 : 20) : 0);
}

/* ---- the behaviours (ai.md 4) ---- */

static int enabled(const acr_gen1_ai *ai, int i) {
    /* Behaviour 12 rides lifts and platforms (stage objects of kind 0x0A);
     * our arenas have none, and the disc switches it off for the whole
     * match on such stages, so it is always off here. Behaviours 1-3
     * (obstacles, stage objects, stage 28's drop) are not ported. */
    return i != B12 && ai->prof.rec.tune[i][0] > 0;
}

/* The AI's bearing from the target's facing (state +0x22), + = to its right. */
static int32_t rev_bearing(const acr_gen1_ai_view *v) {
    return wrap((int32_t)angle_of((int64_t)v->x - v->tx, (int64_t)v->z - v->tz) - v->tyaw);
}
/* How much the AI stands in front of the target: 4096 - 2 |bearing|
 * [read, PP 0x800823C4]. */
static int32_t in_front(const acr_gen1_ai_view *v) {
    const int32_t f = 4096 - 2 * iabs(rev_bearing(v));
    return f < 0 ? 0 : f;
}

static int32_t fit(const acr_gen1_ai *ai, const acr_gen1_ai_view *v, int i, int running) {
    const struct acr_gen1_ai_orig *o = &ai->o;
    switch (i) {
    case B4: {   /* close in / circle: the average of the ground desire and how much it is in front [read, 0x800823C4] */
        const int32_t d = o->desire[0] > o->desire[1] ? o->desire[0] : o->desire[1];
        return (d + in_front(v)) / 2;
    }
    case B5: return o->desire[2];                    /* air / strafe */
    case B6: {   /* hold outside the target's reach */
        const int32_t rev = wrap((int32_t)angle_of((int64_t)v->x - v->tx, (int64_t)v->z - v->tz) - v->tyaw);
        const int sees = iabs(rev) < 512;            /* [ours]: "the player can see it" as a 45-degree cone */
        if (sees && v->dist < v->target_range) return 0;
        return (4096 - pct(ai->prof.rec.unk8)) / 2;
    }
    case B7: return v->missiles_in ? 4096 : 0;      /* dodge missile */
    case B13: {  /* keep distance against the target's weapon, capped at 0.5 */
        if (running) return 2048;
        if (v->target_range <= 0 || v->dist >= v->target_range) return 0;
        const int32_t f = (v->target_range - v->dist) * 4096 / v->target_range;   /* [ours] the ramp's shape */
        return f > 2048 ? 2048 : f;
    }
    case B14: return running ? 2048 : 1024;          /* wander */
    default: return 0;
    }
}

static void start(acr_gen1_ai *ai, int i) {
    struct acr_gen1_ai_orig *o = &ai->o;
    o->side = (int8_t)(rnd(ai, 2) ? 1 : -1);
    if (i == B7) { o->b7_phase = 0; }
    if (i == B14) { o->wmove = (int8_t)(rnd(ai, 3) - 1); o->wstrafe = (int8_t)(rnd(ai, 3) - 1); }
}

/* The chooser, every 8 frames (phase 2): score = commitment x (base +
 * (learned - base) x blend) x fit; commitment 0.5 for the others and, for
 * the current one, from 1.0 down to 0.5 over its commitment time; whims
 * multiply the fit by the whim factor. */
static void choose(acr_gen1_ai *ai, const acr_gen1_ai_view *v) {
    struct acr_gen1_ai_orig *o = &ai->o;
    const acr_gen1_ai_orig_record *r = &ai->prof.rec;
    int best = -1;
    int64_t best_score = -1;
    for (int i = 0; i < ACR_GEN1_AI_ORIG_TUNES; ++i) {
        if (!enabled(ai, i)) continue;
        int64_t f = fit(ai, v, i, i == o->cur);
        const int whim_v = r->tune[i][3];
        if (whim_v && rnd(ai, 22 * whim_v) < 8) f = f * (r->tune[i][4] | r->tune[i][5] << 8) / 100;
        const int32_t base = pct(r->tune[i][0]), blend = pct(r->tune[i][1]);
        const int32_t w = base + (o->learned[i] - base) * blend / 4096;
        int32_t c = 2048;
        if (i == o->cur) {
            const uint32_t t = (uint32_t)r->tune[i][2] * 22u, el = o->frame - o->cur_start;
            c = t ? 4096 - (int32_t)(2048u * (el < t ? el : t) / t) : 2048;
        }
        const int64_t score = (int64_t)c * w / 4096 * f / 4096;
        if (score > best_score) { best_score = score; best = i; }
    }
    if (best != o->cur) {
        o->cur = (int8_t)best;
        o->cur_start = o->frame;
        if (best >= 0) start(ai, best);
    }
}

/* The weapon chooser, every 8 frames (phase 5): range against distance,
 * the learned weight, a bonus for the current pick for 110 frames
 * (ai.md 4; the exact mix is [ours]). */
static void choose_weapon(acr_gen1_ai *ai, const acr_gen1_ai_view *v) {
    struct acr_gen1_ai_orig *o = &ai->o;
    int best = -1;
    int64_t best_score = -1;
    for (int k = 0; k < v->weapons && k < ACR_GEN1_MAX_WEAPONS; ++k) {
        if (v->kind[k] == ACR_GEN1_BLADE || v->ammo[k] == 0) continue;
        const int32_t range = v->range[k] > 0 ? v->range[k] : 4000;
        int64_t s = (int64_t)o->wweight[k] * (v->dist <= range ? 4096 : 1024) / 4096;
        if (v->stance[k] && !v->human_plus && v->legs != ACR_GEN1_LEGS_TRACKS && v->dist < 1500) s /= 4;
        if (k == o->wpick && o->frame - o->wpick_frame < 110) s += s / 2;
        if (s > best_score) { best_score = s; best = k; }
    }
    if (best != o->wpick) { o->wpick = (int8_t)best; o->wpick_frame = o->frame; }
}

/* ---- learning (ai.md 4, "Learning") ---- */

static void got_hit(acr_gen1_ai *ai, const acr_gen1_ai_view *v, int32_t d) {
    struct acr_gen1_ai_orig *o = &ai->o;
    for (int i = 0; i < ACR_GEN1_AI_ORIG_TUNES; ++i)
        o->learned[i] = i == o->cur ? lower(o->learned[i], d / 4) : raise(o->learned[i], d / 8);
    for (int k = 0; k < v->weapons && k < ACR_GEN1_MAX_WEAPONS; ++k) {
        if (v->kind[k] == ACR_GEN1_BLADE) continue;
        if (k == v->current) o->wweight[k] = lower(o->wweight[k], v->stance[k] ? d : d / 8);
        else o->wweight[k] = raise(o->wweight[k], d / 4);
    }
}

static void hit_player(acr_gen1_ai *ai, const acr_gen1_ai_view *v, int32_t d) {
    struct acr_gen1_ai_orig *o = &ai->o;
    if (o->cur >= 0) o->learned[o->cur] = raise(o->learned[o->cur], d / 4);
    /* The disc credits the weapon whose shot landed (projectile tag); we
     * credit the blade while swinging, else the weapon in hand [ours]. */
    if (v->swinging) o->blade = raise(o->blade, d / 4);
    else if (v->current < ACR_GEN1_MAX_WEAPONS) o->wweight[v->current] = raise(o->wweight[v->current], d / 4);
}

/* ---- the frame ---- */

static void frame(acr_gen1_ai *ai, const acr_gen1_ai_view *v, acr_gen1_input *in, acr_gen1_actions *act);

/* One battle step: the brain runs once a Gen 1 tick (the disc's frame);
 * between ticks the held controls stay down and nothing new is pressed. */
void acr_gen1_ai_orig_think(acr_gen1_ai *ai, const acr_gen1_ai_view *v, acr_gen1_input *in, acr_gen1_actions *act) {
    struct acr_gen1_ai_orig *o = &ai->o;
    if (v->step_rate && v->gen1_rate && v->step_rate > v->gen1_rate) {
        o->tick_acc = (uint16_t)(o->tick_acc + v->gen1_rate);
        if (o->tick_acc < v->step_rate && o->frame) {
            *in = o->held_in;
            in->boost_tapped = 0;
            memset(act, 0, sizeof *act);
            act->fire = o->held_fire;
            if (v->ap <= 0 || !v->target_alive) { memset(in, 0, sizeof *in); act->fire = 0; }
            return;
        }
        if (o->tick_acc >= v->step_rate) o->tick_acc = (uint16_t)(o->tick_acc - v->step_rate);
    }
    frame(ai, v, in, act);
    o->held_in = *in;
    o->held_fire = act->fire;
}

static void frame(acr_gen1_ai *ai, const acr_gen1_ai_view *v, acr_gen1_input *in, acr_gen1_actions *act) {
    struct acr_gen1_ai_orig *o = &ai->o;
    const acr_gen1_ai_orig_record *r = &ai->prof.rec;
    const int moa = o->style == ACR_GEN1_STYLE_MOA;
    memset(in, 0, sizeof *in);
    memset(act, 0, sizeof *act);
    if (v->ap <= 0) { ai->prev_boost = 0; return; }
    ++o->frame;
    if (!v->target_alive) { ai->prev_boost = 0; return; }

    /* Player position history (ai.md 5 +0x028). MoA stores where the
     * player will be when the shot arrives, within twice the weapon's
     * range (ai.md 6, aiming ahead); Phantasma where it is. */
    int64_t px = v->tx, py = v->ty, pz = v->tz;
    if (moa && v->current < ACR_GEN1_MAX_WEAPONS && v->shot_speed[v->current] > 0 &&
        v->dist < 2 * (v->range[v->current] > 0 ? v->range[v->current] : 4000)) {
        const int64_t t = v->dist / v->shot_speed[v->current];   /* flight time, ticks */
        px += v->tvx * t / 64; py += v->tvy * t / 64; pz += v->tvz * t / 64;
    }
    o->at = (uint8_t)((o->at + 1) & 31);
    o->hx[o->at] = (int32_t)px; o->hy[o->at] = (int32_t)py; o->hz[o->at] = (int32_t)pz;
    if (o->filled < 32) ++o->filled;
    const int back = o->delay < o->filled ? o->delay : o->filled - 1;
    const int idx = (o->at - back) & 31;
    const int64_t dx = (int64_t)o->hx[idx] - v->x, dy = (int64_t)o->hy[idx] - v->y, dz = (int64_t)o->hz[idx] - v->z;
    const int32_t dh = (int32_t)isqrt64((uint64_t)(dx * dx + dz * dz));
    const int32_t d3 = (int32_t)isqrt64((uint64_t)(dx * dx + dy * dy + dz * dz));
    const int32_t yaw_err = wrap((int32_t)angle_of(dx, dz) - v->yaw);
    const int32_t pitch_err = wrap((int32_t)angle_of(-dy, dh)) - v->pitch;

    /* Learning, from AP changes. */
    if (o->last_ap >= 0 && v->ap < o->last_ap) got_hit(ai, v, o->last_ap - v->ap);
    if (o->last_tap >= 0 && v->target_ap < o->last_tap) hit_player(ai, v, o->last_tap - v->target_ap);
    o->last_ap = v->ap; o->last_tap = v->target_ap;

    /* MoA desperation, once a match (ai.md 6). */
    if (moa && r->desperate && !o->desperate_done && v->ap_max > 0 && v->ap < v->ap_max * r->desperate / 100) {
        o->desperate_done = 1;
        o->blade = o->fire = o->avoid = 4096;
        o->gate = 5;
        o->tracking_on = 0;
        o->lost = 0;
        o->delay = 0;
    }

    /* Desires re-rolled with chance 1/(22 v) a frame (ai.md 3 +0x04). */
    if (r->reroll && rnd(ai, 22 * r->reroll) == 0)
        for (int i = 0; i < 3; ++i) o->desire[i] = pct(r->desire[i] + (r->spread[i] ? rnd(ai, r->spread[i] + 1) : 0));

    const int phase = (int)(o->frame & 7);
    if (phase == 2 || o->cur < 0) choose(ai, v);
    if (phase == 5 || o->wpick < 0) choose_weapon(ai, v);
    /* The weapon in hand loses 1 a frame (floor 409): it keeps rotating (5b). */
    if (v->current < ACR_GEN1_MAX_WEAPONS && v->kind[v->current] != ACR_GEN1_BLADE)
        o->wweight[v->current] = lower(o->wweight[v->current], 1);

    /* Lost track (ai.md 5 +0x244) [read, PP 0x80083650..]: with tracking
     * on, it grows by the aim error (the larger of yaw and pitch) times
     * 0.375..0.625 (more far away, from 5000 to 10000 units) and falls by
     * the tracking value / 16 each frame; with tracking off it only
     * decays, 163 a frame. Capped at 3276. */
    if (o->tracking_on) {
        const int32_t far = 5000;
        const int32_t t = dh - far;
        const int32_t ramp = t <= 0 ? 0 : t < far ? t * 4096 / far : 4096;
        const int32_t g = ramp / 4 + 1536;
        const int32_t err = iabs(pitch_err) > iabs(yaw_err) ? iabs(pitch_err) : iabs(yaw_err);
        int32_t l = o->lost + g * err / 4096 - o->track / 16;
        o->lost = (uint16_t)(l < 0 ? 0 : l > 3276 ? 3276 : l);
    } else {
        o->lost = (uint16_t)(o->lost > 163 ? o->lost - 163 : 0);
    }

    /* The behaviour's action. */
    int32_t goal = yaw_err;                     /* where to turn, off the facing */
    int boost = 0;
    const int planted_w = o->wpick >= 0 && v->stance[o->wpick] && !v->human_plus && v->legs == ACR_GEN1_LEGS_TWO &&
                          v->current == o->wpick;
    /* The booster (helper 0x80081BB0, not fully read): each behaviour
     * passes how much it wants to boost (0..4096); here that is the chance
     * a tick to start a burst while the energy is over half, held until it
     * falls under a fifth [ours]. */
    int32_t boost_want = 0;
    switch (o->cur) {
    case B4: {   /* circle to the target's side and back [read, 0x80082428] */
        const int32_t rv = rev_bearing(v), a = iabs(rv);
        const int8_t away = (int8_t)(rv > 0 ? -1 : 1);          /* our strafe that takes us off its front */
        if (a < 0x100) in->strafe = away;
        else if (a < 0x200) { in->move = 1; in->strafe = away; }
        else if (a < 0x400) in->move = 1;
        else if (a < 0x500) { in->move = 1; in->strafe = (int8_t)-away; }
        else if (a < 0x700) in->strafe = (int8_t)-away;
        boost_want = 0x400;
        break;
    }
    case B5: {   /* hold the chosen weapon's range; sidestep when both face each other inside its reach [read, 0x800825A4] */
        /* The range it holds: the weapon's, guns capped at 4000, where they
         * stop firing (5b) [ours: ai.md's +0x234 is "the chosen weapon's
         * range", the cap keeps it inside its own gun rule]. */
        int32_t wr = o->wpick >= 0 && v->range[o->wpick] > 0 ? v->range[o->wpick] : 4000;
        if (o->wpick >= 0 && v->kind[o->wpick] == ACR_GEN1_GUN && wr > 3000) wr = 3000;
        in->move = (int8_t)(v->dist > wr + 300 ? 1 : v->dist < wr - 300 ? -1 : 0);
        const int32_t rv = rev_bearing(v);
        if (v->dist < v->target_range && iabs(v->bearing) < 512 && iabs(rv) < 512) in->strafe = (int8_t)(rv > 0 ? -1 : 1);
        const int32_t dyp = iabs(v->ty - v->y);
        boost_want = ramp_down(v->dist, v->target_range, v->target_range / 4) * ramp_down(dyp, 1000, 500) / 4096;
        break;
    }
    case B6: break;                                              /* only turns */
    case B7:                                                     /* [read, PP 0x80082850] */
        if (v->missiles_in) {
            const int32_t s1 = (int32_t)angle_of((int64_t)v->x - v->missile_x, (int64_t)v->z - v->missile_z);
            if (o->b7_phase == 0) {
                o->b7_side = (uint8_t)(wrap((int32_t)v->missile_yaw - s1) < 0);
                o->b7_phase = 1;
            } else if (o->b7_phase == 1 && v->missile_dist < 5000) {
                o->b7_side ^= 1;
                o->b7_phase = 2;
            }
            goal = wrap(s1 + 2048 - v->yaw);                     /* face the missile */
            in->strafe = (int8_t)(o->b7_side ? 1 : -1);         /* and move across its path */
            boost_want = v->missile_dist < 5000 ? 4096 : 0x200;
        }
        break;
    case B13: in->move = (int8_t)(v->dist < v->target_range ? -1 : 0); in->strafe = o->side; break;   /* [ours] */
    case B14:
        if ((o->frame - o->cur_start) % 22 == 21) { o->wmove = (int8_t)(rnd(ai, 3) - 1); o->wstrafe = (int8_t)(rnd(ai, 3) - 1); }
        in->move = o->wmove; in->strafe = o->wstrafe;
        break;
    default: break;
    }
    if (!o->boost_frames && boost_want && !v->drained && v->energy > v->energy_max / 2 && rnd(ai, 4096) < boost_want)
        o->boost_frames = 22;
    if (o->boost_frames) {
        --o->boost_frames;
        if (v->drained || v->energy < v->energy_max / 5 || !boost_want) o->boost_frames = 0;
        else boost = 1;
    }

    /* MoA shot dodge (ai.md 6) [the 8-frame check and 8-frame turn are ours]:
     * with chance "avoid", turn 90 degrees off the shot's line and boost. */
    if (moa && phase == 0 && v->shots_in && !o->dodge_frames && rnd(ai, 4096) < o->avoid) {
        o->dodge_frames = 8;
        o->dodge_dir = (int8_t)(rnd(ai, 2) ? 1 : -1);
    }
    if (o->dodge_frames) {
        --o->dodge_frames;
        goal = wrap((int32_t)v->shot_yaw + o->dodge_dir * 1024 - v->yaw);
        in->move = 1; in->strafe = 0;
        boost = !v->drained && v->energy > v->energy_max / 4;
    }

    /* Turning (ai.md 5b / [read] PP 0x800818E0): with chance "lost track"
     * it keeps the last turn press, or turns a random way; otherwise it
     * turns toward the goal, dead zone 16 (wider here when the AC's turn
     * carries further in one step [ours]). */
    if (rnd(ai, 4096) < o->lost) {
        in->turn = o->prev_turn ? o->prev_turn : (int8_t)(rnd(ai, 2) ? 1 : -1);
    } else {
        /* Let go early when the turn already under way would carry past
         * the goal in one tick (our turning has momentum) [ours]. */
        const int32_t rate = iabs(v->turn_rate);
        const int same_way = (goal > 0) == (v->turn_rate > 0) && v->turn_rate;
        in->turn = (int8_t)(iabs(goal) > 16 && !(same_way && iabs(goal) < rate / 2) ? (goal > 0 ? 1 : -1) : 0);
    }
    o->prev_turn = in->turn;
    /* Up / down: outside +-24 (our look steps are 41, so that is the dead zone). */
    if (pitch_err > ACR_GEN1_PITCH_RATE) in->look = 1;
    else if (pitch_err < -ACR_GEN1_PITCH_RATE) in->look = -1;

    /* Walls: never back or slide out of the arena (leaving it loses the
     * fight in our rules) [ours, as the utility brain]. */
    if (v->arena_half) {
        const int32_t margin = 700;
        const int32_t from_centre = iabs(v->x) > iabs(v->z) ? iabs(v->x) : iabs(v->z);
        if (from_centre > v->arena_half - margin) {
            if (v->centre_ahead > 0 && in->move < 0) in->move = 0;
            if (v->centre_ahead < -margin) in->move = -1;
            if (in->strafe && (v->centre_side > 0) != (in->strafe > 0) && iabs(v->centre_side) > margin / 2) {
                in->strafe = (int8_t)(v->centre_side > 0 ? 1 : -1);
                o->side = in->strafe;
            }
        }
    }
    if (boost) {
        in->boost = 1;
        if (!in->move && !in->strafe && v->state == ACR_GEN1_GROUND) in->strafe = o->side;   /* a ground boost needs a direction [ours] */
    }
    if (v->swinging) { in->move = in->strafe = 0; in->boost = 0; }

    /* Weapon change: one press, then wait for it to land. */
    if (o->switch_wait) --o->switch_wait;
    if (o->wpick >= 0 && o->wpick != v->selected && !v->volley && !o->switch_wait) {
        act->next_weapon = 1;
        o->switch_wait = 4;
    }

    /* Firing (ai.md 5b). */
    int want = 0;
    const int wi = v->current;
    if (wi < v->weapons && v->kind[wi] != ACR_GEN1_BLADE && v->ammo[wi] != 0) {
        if (planted_w) {                         /* kind 2: press into the stance, then fire on lock */
            in->move = in->strafe = 0; in->boost = 0;
            want = v->kneeling ? (v->locked || v->in_box) : v->state == ACR_GEN1_GROUND;
        } else if (v->kind[wi] == ACR_GEN1_MISSILE) {
            want = v->locks >= 1 || v->volley;
        } else if (v->ready && d3 < 4000) {      /* kind 0: inside the cone */
            int32_t cone = d3 <= 0 ? 128 : (4000 - d3) * 32 / 1000;
            if (d3 > 0) cone = cone < 16 ? 16 : cone > 32 ? 32 : cone;
            const int32_t pcone = cone > ACR_GEN1_PITCH_RATE ? cone : ACR_GEN1_PITCH_RATE;   /* [ours] */
            want = iabs(yaw_err) < cone && iabs(pitch_err) < pcone;
        }
    }
    if (moa && want && !v->volley) {
        /* Dice on every chance, a quarter of it in the first 110 frames. */
        const int32_t c = o->frame <= 110 ? o->fire / 4 : o->fire;
        if (rnd(ai, 4096) >= c) want = 0;
    }
    o->fire_q = o->fire_q << 1 | (uint32_t)(want != 0);
    act->fire = (uint8_t)(o->hold ? (o->fire_q >> o->hold & 1) : (want != 0));

    /* Blade lunge [read, PP 0x8008339C]: inside 700 units, 700 high and
     * +-45 degrees, a roll against the blade chance; press forward + blade.
     * (The disc also weighs the AC's AP against 2000: not ported.) */
    if (v->blade >= 0 && !v->swinging && !v->drained && v->energy > (int32_t)v->blade_drain &&
        iabs((int32_t)dy) < 700 && d3 < 700 && iabs(pitch_err) < 512 && iabs(yaw_err) < 512 &&
        rnd(ai, 4096) >= 4096 - o->blade) {
        act->blade = 1;
        in->move = 1;
    }
    /* Human PLUS blade at range [read, PP 0x80083510]: 1000-5000 units,
     * level within 300, +-11 degrees, every `gate` frames. */
    if (r->human_plus && o->gate && v->blade >= 0 && !v->swinging && !v->drained &&
        v->energy > (int32_t)v->blade_drain && iabs((int32_t)dy) < 300 && v->dist > 1000 && v->dist < 5000 &&
        iabs(yaw_err) < 128) {
        if (++o->gate_count >= o->gate) { act->blade = 1; o->gate_count = 0; }
    }

    in->boost_tapped = (uint8_t)(in->boost && !ai->prev_boost);
    ai->prev_boost = in->boost;
}
