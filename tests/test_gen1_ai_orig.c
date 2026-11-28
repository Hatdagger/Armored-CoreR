/* The original Gen 1 arena AI (acr_gen1_ai_orig.c): its records, the
 * brain and style switches, determinism, and a ladder test against a
 * scripted baseline player.
 *
 * No game data: the sample records below are made up, shaped like the two
 * families ACG1PLUS ai.md section 3 / 3a describes (ranks 1-30: desires
 * 40/75/75, ranks 1-20 react at once and never lose track, 21-30 react 10
 * frames late with tracking 60; ranks 31-49: desires 70/25/25 with spread
 * 30, re-roll 4, delay 30). Their behaviour tuning and the sample ACs are
 * invented too. The ladder writes its numbers to the file named by the
 * first argument (default ai-ladder-results.txt). */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_ai.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_ac_def def;

static acr_gen1_weapon rifle(uint16_t attack) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_GUN;
    strcpy(w.name, "RIFLE");
    w.attack = attack; w.ammo = 300; w.range = 6100;
    w.lock_ms = 1; w.box_w = 240; w.box_h = 240; w.inner_w = 200; w.inner_h = 200; w.lock_value = 60;
    w.interval = 5; w.hold = 13; w.speed = 700; w.shots = 1;
    return w;
}
static acr_gen1_weapon missile(void) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_MISSILE;
    strcpy(w.name, "MISSILE");
    w.attack = 830; w.ammo = 30; w.range = 8000;
    w.box_w = 200; w.box_h = 200; w.inner_w = 160; w.inner_h = 160;
    w.interval = 10; w.lock_value = 128; w.lock_ticks = (256 - 128) / 8; w.max_locks = 4;
    w.volley_a = 1; w.ignition = 1;
    w.flight = (acr_gen1_flight){ 50, 60, 80, 350, 110, 1024, 0 };
    return w;
}
static acr_gen1_weapon blade(void) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_BLADE;
    strcpy(w.name, "LS-2001");
    w.attack = 738; w.energy = 1; w.range = ACR_GEN1_BLADE_REACH; w.charge_drain = 2050;
    return w;
}

#define ARENA 6000

/* Side 0 = the baseline player, side 1 = the AI. `tier` 0..5 strengthens
 * the AI's sample AC the way the disc's builds get stronger up the ladder
 * (made up: AP and rifle attack). */
static void setup(acr_gen1_battle *b, int tier) {
    memset(b, 0, sizeof *b);
    b->step_ms = 1000 / 60;
    b->step_rate = 60;
    b->gen1_rate = 22;
    for (int i = 0; i < 2; ++i) {
        acr_gen1_fighter *f = &b->f[i];
        f->def = &def;
        f->intercept_shot = -1;
        f->swing_target = -1;
        acr_gen1_ac_spawn(&f->ac, &def, 0, i ? 2500 * 64 : -2500 * 64, i ? 2048 : 0);
        f->ap = f->ap_max = i ? 6000 + 900 * tier : 8384;
        f->shell_factor = 27;
        f->energy_factor = 32;
        f->weapon[0] = rifle((uint16_t)(i ? 170 + 26 * tier : 218));
        f->weapon[1] = missile();
        f->weapon[2] = blade();
        f->weapons = 3;
        for (int k = 0; k < 3; ++k) f->ammo[k] = f->weapon[k].ammo;
    }
}

static void walls(acr_gen1_battle *b) {
    const int32_t lim = ARENA * 64;
    for (int k = 0; k < 2; ++k) {
        acr_gen1_ac *q = &b->f[k].ac;
        int32_t *const xs[3] = { &q->x, &q->px, &q->sx }, *const zs[3] = { &q->z, &q->pz, &q->sz };
        for (int c = 0; c < 3; ++c) {
            if (*xs[c] > lim) { *xs[c] = lim; q->vx = 0; }
            if (*xs[c] < -lim) { *xs[c] = -lim; q->vx = 0; }
            if (*zs[c] > lim) { *zs[c] = lim; q->vz = 0; }
            if (*zs[c] < -lim) { *zs[c] = -lim; q->vz = 0; }
        }
    }
}

/* ---- the baseline player: average aim, side-to-side strafing, sensible
 * weapon use. It strafes one way then the other every 1.5-2.5 s, keeps 1500-3000 away,
 * fires the rifle on 8 of 10 chances with the target in its box, missiles
 * at two locks when far, the blade when close and ahead. Aim: where the AI
 * was 0.15 s ago, +-4 degrees. ---- */
typedef struct baseline {
    uint32_t rng;
    int32_t hist[16];       /* bearings, newest at `at` */
    int at, n;
    int strafe, strafe_ms;
    int want, switch_ms, blade_ms, step;
} baseline;

static int brnd(baseline *p, int below) {
    p->rng = p->rng * 1664525u + 1013904223u;
    return below > 0 ? (int)((p->rng >> 8) % (uint32_t)below) : 0;
}

static void baseline_step(baseline *p, const acr_gen1_battle *b, acr_gen1_input *in, acr_gen1_actions *act) {
    acr_gen1_ai_view v;
    acr_gen1_ai_look(b, 0, ARENA, &v);
    memset(in, 0, sizeof *in);
    memset(act, 0, sizeof *act);
    ++p->step;
    if (v.ap <= 0 || !v.target_alive) return;
    p->at = (p->at + 1) & 15;
    p->hist[p->at] = v.bearing;
    if (p->n < 16) ++p->n;
    const int lag = p->n > 9 ? 9 : p->n - 1;            /* 0.15 s */
    int32_t aim = p->hist[(p->at - lag) & 15] + (brnd(p, 91) - 45);
    if (aim > 24) in->turn = 1;
    else if (aim < -24) in->turn = -1;
    const int32_t tilt = v.elevation - v.pitch;
    if (tilt > ACR_GEN1_PITCH_RATE) in->look = 1;
    else if (tilt < -ACR_GEN1_PITCH_RATE) in->look = -1;
    p->strafe_ms -= b->step_ms;
    if (p->strafe_ms <= 0) { p->strafe = p->strafe > 0 ? -1 : 1; p->strafe_ms = 1500 + brnd(p, 1000); }
    in->strafe = (int8_t)p->strafe;
    in->move = (int8_t)(v.dist > 3000 ? 1 : v.dist < 1500 ? -1 : 0);
    if (v.arena_half) {
        const int32_t far = v.x < 0 ? -v.x : v.x, farz = v.z < 0 ? -v.z : v.z;
        if ((far > farz ? far : farz) > ARENA - 800) {
            in->move = 1;
            in->strafe = (int8_t)(v.centre_side > 0 ? 1 : -1);
            p->strafe = in->strafe;
        }
    }
    /* weapons: missiles past 2500 when it has a lock, the rifle otherwise */
    p->want = v.dist > 2500 && v.ammo[1] ? 1 : 0;
    if (p->switch_ms > 0) p->switch_ms -= b->step_ms;
    if (v.selected != p->want && !v.volley && p->switch_ms <= 0) { act->next_weapon = 1; p->switch_ms = 300; }
    if (v.current == 0) act->fire = (uint8_t)(v.in_box && v.ready && brnd(p, 10) < 8);   /* not every chance */
    else if (v.current == 1) act->fire = (uint8_t)(v.locks >= 2 || v.volley);
    if (p->blade_ms > 0) p->blade_ms -= b->step_ms;
    if (v.dist < 600 && (v.bearing < 300 && v.bearing > -300) && !v.swinging && p->blade_ms <= 0 &&
        v.energy > (int32_t)v.blade_drain + 2000) {
        act->blade = 1;
        p->blade_ms = 1500;
    }
}

/* ---- sample records ---- */

/* A made-up Phantasma-shaped record for opponent number n (rank n + 1). */
static void sample_pp(uint8_t r[ACR_GEN1_AI_PP_RECORD_SIZE], int n) {
    memset(r, 0, ACR_GEN1_AI_PP_RECORD_SIZE);
    static const uint8_t tune_a[4][6] = {      /* behaviours 4-7 */
        { 55, 70, 9, 0, 0, 0 }, { 70, 30, 9, 0, 0, 0 }, { 45, 90, 2, 0, 0, 0 }, { 100, 0, 1, 0, 0, 0 } };
    static const uint8_t tune_b[4][6] = {
        { 70, 45, 9, 25, 180, 0 }, { 70, 45, 9, 25, 180, 0 }, { 45, 90, 2, 0, 0, 0 }, { 100, 0, 1, 0, 0, 0 } };
    static const uint8_t tune_hi[3][6] = { { 0, 0, 0, 0, 0, 0 }, { 45, 30, 55, 30, 0x80, 1 }, { 45, 30, 55, 12, 0x80, 1 } };
    if (n < 30) {
        r[0] = (uint8_t)(n < 9 ? 9 : 0);
        r[1] = 40; r[2] = 75; r[3] = (uint8_t)(n == 0 ? 85 : 75);
        r[8] = 80;
        r[9] = (uint8_t)(n < 20 ? 100 - n * 3 : 60);
        r[0x0A] = (uint8_t)(n < 9 ? 14 : 0);
        r[0x10] = (uint8_t)(n < 20 ? 0 : 60);
        r[0x11] = (uint8_t)(n < 20 ? 0 : 10);
        for (int i = 0; i < 4; ++i) memcpy(r + 0x12 + 6 * i, tune_a[i], 6);
        if (n >= 20) for (int i = 0; i < 3; ++i) memcpy(r + 0x2A + 6 * i, tune_hi[i], 6);
    } else {
        r[1] = 70; r[2] = 25; r[3] = 25; r[4] = 4; r[6] = 30; r[7] = 30;
        r[8] = 100;
        r[9] = (uint8_t)(n % 3 ? 0 : 90);
        r[0x10] = (uint8_t)(10 + n % 4 * 10);
        r[0x11] = 30;
        for (int i = 0; i < 4; ++i) memcpy(r + 0x12 + 6 * i, tune_b[i], 6);
        for (int i = 0; i < 3; ++i) memcpy(r + 0x2A + 6 * i, tune_hi[i], 6);
    }
}

typedef struct result { int won_ai, won_base, draw, steps; int ai_shots, ai_hits, base_shots, base_hits; } result;

static result fight(int rank, int style, int brain, uint32_t seed, int seconds) {
    static acr_gen1_battle b;
    static acr_gen1_ai ai;
    result out;
    memset(&out, 0, sizeof out);
    const int n = rank - 1;
    int tier = rank >= 45 ? 0 : rank >= 35 ? 1 : rank >= 25 ? 2 : rank >= 15 ? 3 : rank >= 5 ? 4 : 5;
    setup(&b, tier);
    acr_gen1_ai_profile p;
    acr_gen1_ai_preset(&p, ACR_GEN1_AI_BALANCED);
    p.preset = ACR_GEN1_AI_CUSTOM;
    uint8_t rec[ACR_GEN1_AI_PP_RECORD_SIZE];
    sample_pp(rec, n);
    acr_gen1_ai_orig_from_pp(&p.rec, rec, n);
    p.rebel = rec[0];
    p.brain = (uint8_t)brain;
    acr_gen1_ai_set_options(ACR_GEN1_BRAIN_ORIGINAL, style);
    acr_gen1_ai_init(&ai, &p, seed);
    baseline base;
    memset(&base, 0, sizeof base);
    base.rng = seed * 2654435761u + 7;
    base.strafe = 1;
    int32_t last_ap[2] = { b.f[0].ap, b.f[1].ap };
    uint8_t was[ACR_GEN1_MAX_SHOTS];
    memset(was, 0, sizeof was);
    const int total = seconds * 60;
    int r = 0;
    for (out.steps = 0; out.steps < total; ++out.steps) {
        acr_gen1_input in[2];
        acr_gen1_actions act[2];
        baseline_step(&base, &b, &in[0], &act[0]);
        acr_gen1_ai_step(&ai, &b, 1, ARENA, &in[1], &act[1]);
        acr_gen1_battle_step(&b, in, act);
        walls(&b);
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) {
            const acr_gen1_shot *s = &b.shot[k];
            if (s->active && !was[k]) {
                if (s->owner == 0) ++out.base_shots;
                if (s->owner == 1) ++out.ai_shots;
            }
            was[k] = s->active;
        }
        if (b.f[0].ap < last_ap[0]) ++out.ai_hits;
        if (b.f[1].ap < last_ap[1]) ++out.base_hits;
        last_ap[0] = b.f[0].ap; last_ap[1] = b.f[1].ap;
        r = acr_gen1_battle_result(&b, 0);
        if (r) break;
    }
    if (!r) r = acr_gen1_battle_result(&b, 1);
    out.won_base = r == 1; out.won_ai = r == 2; out.draw = r == 3;
    acr_gen1_ai_set_options(ACR_GEN1_BRAIN_ORIGINAL, ACR_GEN1_STYLE_NATIVE);
    return out;
}

static void init_def(void) {
    def.stats.energy_capacity = 28000;
    def.stats.rise_drain = 400;
    def.stats.refill_standing = 200;
    def.stats.refill_moving = 100;
    def.stats.red_zone = 8000;
    def.stats.thrust = 32; def.stats.thrust_80 = 25; def.stats.thrust_120 = 38;
    def.stats.turn_accel = 22; def.stats.turn_speed = 41; def.stats.friction = 43;
    acr_gen1_motion_from_stats(&def.stats, &def.motion);
    def.step_rate = 60; def.gen1_rate = 22;
    static const acr_gen1_root_key k_fwd[] = { { 2, 0, 0, -248 }, { 4, 0, 0, -548 } };
    static const acr_gen1_root_key k_back[] = { { 4, 0, 0, 400 } };
    static const acr_gen1_root_key k_left[] = { { 4, 400, 0, 0 } };
    static const acr_gen1_root_key k_right[] = { { 4, -400, 0, 0 } };
    static const acr_gen1_root_key k_jump[] = { { 7, 0, 0, 0 }, { 9, 0, -400, 0 } };
    static const acr_gen1_root_track t_fwd = { 4, 2, k_fwd }, t_back = { 4, 1, k_back },
        t_left = { 4, 1, k_left }, t_right = { 4, 1, k_right }, t_jump = { 9, 2, k_jump };
    def.anim[ACR_GEN1_ANIM_WALK] = &t_fwd;
    def.anim[ACR_GEN1_ANIM_BACK] = &t_back;
    def.anim[ACR_GEN1_ANIM_STRAFE_L] = &t_left;
    def.anim[ACR_GEN1_ANIM_STRAFE_R] = &t_right;
    def.anim[ACR_GEN1_ANIM_JUMP] = &t_jump;
    def.landing_steps = 20;
}

int main(int argc, char **argv) {
    init_def();

    /* Records: the Phantasma layout (ai.md 3). */
    {
        uint8_t a[ACR_GEN1_AI_PP_RECORD_SIZE];
        for (int i = 0; i < ACR_GEN1_AI_PP_RECORD_SIZE; ++i) a[i] = (uint8_t)(i + 1);
        acr_gen1_ai_orig_record r;
        acr_gen1_ai_orig_from_pp(&r, a, 12);
        EXPECT(r.present && r.disc == ACR_GEN1_STYLE_PP && r.opponent == 12 && !r.main_arena);
        EXPECT(r.human_plus == 1 && r.desire[0] == 2 && r.desire[2] == 4 && r.reroll == 5 && r.spread[2] == 8);
        EXPECT(r.unk8 == 9 && r.blade == 10 && r.gate == 11 && r.tracking == 0x11 && r.delay == 0x12);
        EXPECT(r.tune[0][0] == 0x13 && r.tune[3][5] == 0x13 + 23 && r.tune[4][0] == 0x2B && r.tune[6][5] == 0x2B + 17);
    }
    /* Master of Arena's 36 bytes, from the editor's own record. */
    {
        acr_gen1_ai_profile p;
        acr_gen1_ai_preset(&p, ACR_GEN1_AI_SNIPER);
        uint8_t m[ACR_GEN1_AI_RECORD_SIZE];
        acr_gen1_ai_record(&p, m);
        acr_gen1_ai_orig_record r;
        acr_gen1_ai_orig_from_moa(&r, m, 3, 1);
        EXPECT(r.disc == ACR_GEN1_STYLE_MOA && r.fire == 96 && r.avoid == 50 && r.tracking == 96 && r.delay == 1);
        EXPECT(r.desire[2] == 30 && r.blade == 10 && r.main_arena && r.opponent == 3);
        EXPECT(r.tune[0][0] == m[0x1C] && r.tune[1][0] == m[0x16] && r.tune[3][0] == m[0x1A]);
    }
    /* Styles and brains resolve at init; the fire hold goes to Master of
     * Arena's main arena only (10 frames for opponents 0-10, 20 after). */
    {
        acr_gen1_ai ai;
        acr_gen1_ai_profile p;
        acr_gen1_ai_preset(&p, ACR_GEN1_AI_BALANCED);
        acr_gen1_ai_init(&ai, &p, 1);
        EXPECT(ai.brain == ACR_GEN1_BRAIN_ORIGINAL && ai.o.style == ACR_GEN1_STYLE_MOA && ai.o.hold == 0);
        uint8_t m[ACR_GEN1_AI_RECORD_SIZE];
        acr_gen1_ai_record(&p, m);
        acr_gen1_ai_orig_from_moa(&p.rec, m, 10, 1);
        acr_gen1_ai_init(&ai, &p, 1);
        EXPECT(ai.o.hold == 10);
        acr_gen1_ai_orig_from_moa(&p.rec, m, 11, 1);
        acr_gen1_ai_init(&ai, &p, 1);
        EXPECT(ai.o.hold == 20);
        acr_gen1_ai_set_options(ACR_GEN1_BRAIN_ORIGINAL, ACR_GEN1_STYLE_PP);
        acr_gen1_ai_init(&ai, &p, 1);
        EXPECT(ai.o.style == ACR_GEN1_STYLE_PP && ai.o.hold == 0);
        acr_gen1_ai_set_options(ACR_GEN1_BRAIN_UTILITY, ACR_GEN1_STYLE_NATIVE);
        acr_gen1_ai_init(&ai, &p, 1);
        EXPECT(ai.brain == ACR_GEN1_BRAIN_UTILITY);
        p.brain = ACR_GEN1_BRAIN_ORIGINAL;                 /* a fight that names its brain keeps it */
        acr_gen1_ai_init(&ai, &p, 1);
        EXPECT(ai.brain == ACR_GEN1_BRAIN_ORIGINAL);
        acr_gen1_ai_set_options(ACR_GEN1_BRAIN_ORIGINAL, ACR_GEN1_STYLE_NATIVE);
        EXPECT(!strcmp(acr_gen1_ai_brain_name(ACR_GEN1_BRAIN_UTILITY), "ACR UTILITY (HARD)"));
    }

    /* Determinism: the same seed replays the same match, and a copy taken
     * mid-fight (what rollback saves) carries on exactly the same. */
    {
        static acr_gen1_battle b1, b2;
        static acr_gen1_ai a1, a2;
        for (int style = ACR_GEN1_STYLE_PP; style <= ACR_GEN1_STYLE_MOA; ++style) {
            const result r1 = fight(30, style, ACR_GEN1_BRAIN_DEFAULT, 77, 60);
            const result r2 = fight(30, style, ACR_GEN1_BRAIN_DEFAULT, 77, 60);
            EXPECT(r1.steps == r2.steps && r1.ai_shots == r2.ai_shots && r1.ai_hits == r2.ai_hits &&
                   r1.base_hits == r2.base_hits && r1.won_ai == r2.won_ai);
        }
        setup(&b1, 2);
        acr_gen1_ai_profile p;
        acr_gen1_ai_preset(&p, ACR_GEN1_AI_AGGRESSOR);
        acr_gen1_ai_init(&a1, &p, 5);
        baseline q1, q2;
        memset(&q1, 0, sizeof q1);
        q1.rng = 9; q1.strafe = 1;
        int same = 1;
        for (int s = 0; s < 60 * 50; ++s) {
            if (s == 60 * 20) { b2 = b1; a2 = a1; q2 = q1; }
            acr_gen1_input in[2], in2[2];
            acr_gen1_actions act[2], act2[2];
            baseline_step(&q1, &b1, &in[0], &act[0]);
            acr_gen1_ai_step(&a1, &b1, 1, ARENA, &in[1], &act[1]);
            acr_gen1_battle_step(&b1, in, act);
            walls(&b1);
            if (s >= 60 * 20) {
                baseline_step(&q2, &b2, &in2[0], &act2[0]);
                acr_gen1_ai_step(&a2, &b2, 1, ARENA, &in2[1], &act2[1]);
                acr_gen1_battle_step(&b2, in2, act2);
                walls(&b2);
                if (memcmp(&in[1], &in2[1], sizeof in[1]) || b1.f[1].ac.x != b2.f[1].ac.x || b1.f[0].ap != b2.f[0].ap) same = 0;
            }
        }
        EXPECT(same);
    }

    /* A downed original-brain AI does nothing. */
    {
        static acr_gen1_battle b;
        setup(&b, 0);
        b.f[1].ap = 0;
        acr_gen1_ai ai;
        acr_gen1_ai_profile p;
        acr_gen1_ai_preset(&p, ACR_GEN1_AI_AGGRESSOR);
        acr_gen1_ai_init(&ai, &p, 3);
        acr_gen1_input in;
        acr_gen1_actions act;
        acr_gen1_ai_step(&ai, &b, 1, ARENA, &in, &act);
        EXPECT(!in.move && !in.turn && !in.boost && !act.fire && !act.blade);
    }

    /* The ladder: ranks 49, 40, 30, 20, 10 and 1 against the baseline, in
     * Phantasma style (the PP opponents' own), over several seeds. Low
     * ranks should lose most fights, the middle be roughly even, the top
     * win most. Fight length is logged, never tested. */
    {
        const char *path = argc > 1 ? argv[1] : "ai-ladder-results.txt";
        FILE *out = fopen(path, "w");
        static const int ranks[6] = { 49, 40, 30, 20, 10, 1 };
        enum { SEEDS = 10 };
        int ai_wins[3][6];
        if (out) fprintf(out, "# Original brain vs the scripted baseline (tests/test_gen1_ai_orig.c), %d seeds, 120 s fights\n"
                              "# style rank  ai_won base_won draw  mean_s  ai_shots ai_hits  base_shots base_hits\n", SEEDS);
        /* rows: PP style, MoA style, and the utility brain for comparison */
        for (int st = 0; st < 3; ++st) {
            const int style = st == 1 ? ACR_GEN1_STYLE_MOA : ACR_GEN1_STYLE_PP;
            const int brain = st == 2 ? ACR_GEN1_BRAIN_UTILITY : ACR_GEN1_BRAIN_DEFAULT;
            for (int i = 0; i < 6; ++i) {
                int w = 0, l = 0, d = 0;
                long steps = 0, as = 0, ah = 0, bs = 0, bh = 0;
                for (uint32_t seed = 1; seed <= SEEDS; ++seed) {
                    const result r = fight(ranks[i], style, brain, seed * 101u, 120);
                    w += r.won_ai; l += r.won_base; d += r.draw;
                    steps += r.steps; as += r.ai_shots; ah += r.ai_hits; bs += r.base_shots; bh += r.base_hits;
                }
                ai_wins[st][i] = w;
                char line[200];
                snprintf(line, sizeof line, "%-5s %4d  %6d %8d %4d  %6.1f  %8ld %7ld  %10ld %9ld\n",
                         st == 2 ? "UTIL" : st ? "MOA" : "PP", ranks[i], w, l, d, steps / (double)SEEDS / 60.0, as, ah, bs, bh);
                printf("%s", line);
                if (out) fputs(line, out);
            }
        }
        if (out) fclose(out);
        /* Phantasma style (the ladder the complaint was about). */
        EXPECT(ai_wins[0][0] <= SEEDS * 4 / 10);          /* rank 49 loses most */
        EXPECT(ai_wins[0][1] <= SEEDS * 5 / 10);          /* rank 40 */
        EXPECT(ai_wins[0][3] >= SEEDS * 2 / 10 && ai_wins[0][3] <= SEEDS * 8 / 10);   /* rank 20 about even */
        EXPECT(ai_wins[0][5] >= SEEDS * 6 / 10);          /* rank 1 wins most */
        EXPECT(ai_wins[0][5] > ai_wins[0][0]);
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 ai (original brain): all passed\n");
    return 0;
}
