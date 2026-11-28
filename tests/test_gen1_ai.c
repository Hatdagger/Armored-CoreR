/* Gen 1 computer pilot: the editor's record, profiles, and whole matches
 * flown by the AI through the real battle rules. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_ai.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_ac_def def;

static acr_gen1_weapon rifle(void) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_GUN;
    strcpy(w.name, "RIFLE");
    w.attack = 218; w.ammo = 200; w.range = 6100;
    w.lock_ms = 1; w.box_w = 240; w.box_h = 240; w.inner_w = 200; w.inner_h = 200; w.lock_value = 60;
    w.interval = 5; w.hold = 13; w.speed = 700; w.shots = 1;
    return w;
}

static acr_gen1_weapon small_missile(void) {
    acr_gen1_weapon w;
    memset(&w, 0, sizeof w);
    w.kind = ACR_GEN1_MISSILE;
    strcpy(w.name, "MISSILE");
    w.attack = 830; w.ammo = 40; w.range = 8000;
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

static void setup(acr_gen1_battle *b, int armed1) {
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
        f->ap = f->ap_max = 8384;
        f->shell_factor = 27;
        f->energy_factor = 32;
        if (i == 0 || armed1) {
            f->weapon[0] = rifle();
            f->weapon[1] = small_missile();
            f->weapon[2] = blade();
            f->weapons = 3;
            for (int k = 0; k < 3; ++k) f->ammo[k] = f->weapon[k].ammo;
        }
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

typedef struct stats { int steps, result; int64_t dist_sum; int air, drained, at_wall, shots; } stats;

/* A match: AI on side 0 (and on side 1 if p1 is given), up to `seconds`. */
static stats match(acr_gen1_battle *b, const acr_gen1_ai_profile *p0, const acr_gen1_ai_profile *p1,
                   uint32_t seed, int seconds) {
    static acr_gen1_ai ai[2];
    stats s;
    memset(&s, 0, sizeof s);
    acr_gen1_ai_init(&ai[0], p0, seed);
    if (p1) acr_gen1_ai_init(&ai[1], p1, seed * 7 + 1);
    const int total = seconds * 60;
    for (s.steps = 0; s.steps < total; ++s.steps) {
        acr_gen1_input in[2];
        acr_gen1_actions act[2];
        memset(in, 0, sizeof in);
        memset(act, 0, sizeof act);
        acr_gen1_ai_step(&ai[0], b, 0, ARENA, &in[0], &act[0]);
        if (p1) acr_gen1_ai_step(&ai[1], b, 1, ARENA, &in[1], &act[1]);
        acr_gen1_battle_step(b, in, act);
        walls(b);
        const acr_gen1_ac *a = &b->f[0].ac, *o = &b->f[1].ac;
        const int64_t dx = (o->x - a->x) / 64, dz = (o->z - a->z) / 64;
        int64_t d2 = dx * dx + dz * dz, r = 0;
        while ((r + 1) * (r + 1) <= d2) r += (r + 1) * (r + 1) * 4 <= d2 ? r + 1 : 1;
        s.dist_sum += r;
        s.air += a->state == ACR_GEN1_AIR;
        s.drained += a->drained;
        s.at_wall += a->x == ARENA * 64 || a->x == -ARENA * 64 || a->z == ARENA * 64 || a->z == -ARENA * 64;
        for (int k = 0; k < ACR_GEN1_MAX_SHOTS; ++k) s.shots += b->shot[k].active && b->shot[k].owner == 0 && b->shot[k].age == 0 && !b->shot[k].travelled;
        s.result = acr_gen1_battle_result(b, 0);
        if (s.result) break;
    }
    if (!s.result) s.result = 4 + acr_gen1_battle_result(b, 1);   /* time up: 5, 6, 7 */
    return s;
}

int main(void) {
    def.stats.energy_capacity = 28000;
    def.stats.rise_drain = 400;
    def.stats.refill_standing = 200;
    def.stats.refill_moving = 100;
    def.stats.red_zone = 8000;
    def.stats.thrust = 32; def.stats.thrust_80 = 25; def.stats.thrust_120 = 38;
    def.stats.turn_accel = 22; def.stats.turn_speed = 41; def.stats.friction = 43;
    acr_gen1_motion_from_stats(&def.stats, &def.motion);
    def.step_rate = 60; def.gen1_rate = 22;
    /* Walking tracks (1/4 world units; -z ahead, +x to the left), as an
     * import gives them, so the AI can walk as well as boost. */
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

    /* The record, as the game's editor writes it (AC1Recomp moa_card.c). */
    {
        acr_gen1_ai_profile p;
        uint8_t r[ACR_GEN1_AI_RECORD_SIZE];
        acr_gen1_ai_preset(&p, ACR_GEN1_AI_BALANCED);
        acr_gen1_ai_record(&p, r);
        EXPECT(r[0] == 0 && r[13] == 51 && r[9] == 50 && r[10] == 55 && r[11] == 51 && r[12] == 12);
        EXPECT(r[16] == 0xE8 && r[17] == 0x03 && r[18] == 0x88 && r[19] == 0x13);
        acr_gen1_ai_preset(&p, ACR_GEN1_AI_SNIPER);
        p.rebel = 5;
        acr_gen1_ai_record(&p, r);
        EXPECT(r[0] == 5 && r[13] == 96 && r[9] == 10 && r[10] == 95 && r[11] == 96 && r[12] == 1);
        EXPECT(r[14] == 50 && r[1] == 70 && r[2] == 70 && r[3] == 30);
        EXPECT(r[20] == 80 && r[22] == 20 && r[24] == 90 && r[28] == 10);
        acr_gen1_ai_preset(&p, ACR_GEN1_AI_AGGRESSOR);
        acr_gen1_ai_record(&p, r);
        EXPECT(r[11] == 96 && r[12] == 1);
        p.slider[ACR_GEN1_AI_LOCK] = 100;
        acr_gen1_ai_record(&p, r);
        EXPECT(r[11] == 0 && r[12] == 0);
    }

    /* Profiles read and written as text. */
    {
        acr_gen1_ai_profile p, q;
        acr_gen1_ai_preset(&p, ACR_GEN1_AI_BALANCED);
        EXPECT(acr_gen1_ai_parse(&p, "# a test\npreset = \"duelist\"\nrange = 20  # a bit further\nskill=80\nrebel = 3\nnonsense = 4\n"));
        EXPECT(p.preset == ACR_GEN1_AI_CUSTOM && p.slider[ACR_GEN1_AI_BLADE] == 100 &&
               p.slider[ACR_GEN1_AI_RANGE] == 20 && p.skill == 80 && p.rebel == 3);
        char text[512];
        EXPECT(acr_gen1_ai_write(&p, text, sizeof text) < (int)sizeof text);
        acr_gen1_ai_preset(&q, ACR_GEN1_AI_SNIPER);
        EXPECT(acr_gen1_ai_parse(&q, text));
        EXPECT(!memcmp(&p, &q, sizeof p));
        acr_gen1_ai_preset(&q, ACR_GEN1_AI_BALANCED);
        EXPECT(acr_gen1_ai_parse(&q, "preset = 'Evasive'"));
        EXPECT(q.preset == ACR_GEN1_AI_EVASIVE && q.slider[ACR_GEN1_AI_AVOID] == 100);
        EXPECT(!strcmp(acr_gen1_ai_preset_name(ACR_GEN1_AI_SNIPER), "SNIPER"));
        EXPECT(!strcmp(acr_gen1_ai_preset_name(99), "CUSTOM"));
    }

    static acr_gen1_battle b, b2;
    acr_gen1_ai_profile prof[ACR_GEN1_AI_PRESETS];
    /* These matches test the utility brain ("ACR UTILITY (HARD)"), which
     * the presets flew before the original brain came in; the original
     * brain has its own tests (test_gen1_ai_orig.c). */
    for (int k = 1; k < ACR_GEN1_AI_PRESETS; ++k) {
        acr_gen1_ai_preset(&prof[k], k);
        prof[k].brain = ACR_GEN1_BRAIN_UTILITY;
    }

    /* Every preset finds and beats an AC that just stands there. */
    for (int k = 1; k < ACR_GEN1_AI_PRESETS; ++k) {
        setup(&b, 0);
        const stats s = match(&b, &prof[k], NULL, 11u + (uint32_t)k, 120);
        printf("%-9s vs still target: %s after %.1f s, AP left %d, mean distance %d, air %d%%, drained %d%%\n",
               acr_gen1_ai_preset_name(k), s.result == 1 ? "won" : "no result", s.steps / 60.0,
               (int)b.f[0].ap, (int)(s.dist_sum / (s.steps + 1)), s.air * 100 / (s.steps + 1),
               s.drained * 100 / (s.steps + 1));
        EXPECT(s.result == 1);   /* a knock-out inside two minutes */
        EXPECT(s.at_wall * 10 < s.steps + 1);      /* not stuck on a wall */
    }

    /* Ranges: the sniper fights further away than the duelist. */
    {
        setup(&b, 0);
        const stats sn = match(&b, &prof[ACR_GEN1_AI_SNIPER], NULL, 5, 30);
        setup(&b, 0);
        const stats du = match(&b, &prof[ACR_GEN1_AI_DUELIST], NULL, 5, 30);
        EXPECT(sn.dist_sum / (sn.steps + 1) > du.dist_sum / (du.steps + 1) + 1000);
    }

    /* AI against AI, every preset against Balanced: a fight that ends, and
     * the same seed replays the same match. */
    for (int k = 1; k < ACR_GEN1_AI_PRESETS; ++k) {
        int wins = 0, losses = 0, kos = 0;
        int64_t dealt = 0, taken = 0;
        for (uint32_t seed = 1; seed <= 6; ++seed) {
            setup(&b, 1);
            const stats s = match(&b, &prof[k], &prof[ACR_GEN1_AI_BALANCED], seed, 120);
            const int r = s.result > 4 ? s.result - 4 : s.result;
            wins += r == 1;
            losses += r == 2;
            kos += s.result <= 3;
            dealt += b.f[1].ap_max - (b.f[1].ap > 0 ? b.f[1].ap : 0);
            taken += b.f[0].ap_max - (b.f[0].ap > 0 ? b.f[0].ap : 0);
            /* Both sides fight: each loses at least a tenth of its AP. */
            EXPECT(b.f[0].ap < b.f[0].ap_max * 9 / 10 && b.f[1].ap < b.f[1].ap_max * 9 / 10);
            if (seed == 1) {
                setup(&b2, 1);
                const stats s2 = match(&b2, &prof[k], &prof[ACR_GEN1_AI_BALANCED], seed, 120);
                EXPECT(s2.steps == s.steps && s2.result == s.result && b2.f[0].ap == b.f[0].ap &&
                       b2.f[1].ap == b.f[1].ap && b2.f[0].ac.x == b.f[0].ac.x && b2.f[1].ac.z == b.f[1].ac.z);
            }
        }
        printf("%-9s vs BALANCED, 6 two-minute rounds: %d won, %d lost (%d knock-outs), AP dealt %d, taken %d\n",
               acr_gen1_ai_preset_name(k), wins, losses, kos, (int)(dealt / 6), (int)(taken / 6));
    }

    /* A downed AI does nothing. */
    {
        setup(&b, 1);
        b.f[0].ap = 0;
        acr_gen1_ai ai;
        acr_gen1_ai_init(&ai, &prof[ACR_GEN1_AI_AGGRESSOR], 3);
        acr_gen1_input in;
        acr_gen1_actions act;
        acr_gen1_ai_step(&ai, &b, 0, ARENA, &in, &act);
        EXPECT(!in.move && !in.turn && !in.boost && !act.fire && !act.blade);
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("gen1 ai: all passed\n");
    return 0;
}
