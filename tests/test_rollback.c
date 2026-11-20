/* Rollback netplay: snapshots replay exactly, and two sessions talking over
 * a simulated network (lag, jitter, lost and reordered packets) end every
 * match in step with each other and with a plain run of the same controls. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_match.h"
#include "acr_rollback.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static acr_gen1_ac_def def;
static acr_gen1_battle start;
static acr_gen1_match_rules rules = { .round_ms = 20000, .next_round_ms = 3000, .arena_half = 20000 };   /* short rounds: round ends get tested too */

static uint32_t seed = 12345;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static void setup(void) {
    memset(&def, 0, sizeof def);
    def.stats.energy_capacity = 28000;
    def.stats.thrust = 32; def.stats.thrust_80 = 25; def.stats.thrust_120 = 38;
    def.stats.turn_accel = 22; def.stats.turn_speed = 41;
    acr_gen1_motion_from_stats(&def.stats, &def.motion);
    def.step_rate = 60; def.gen1_rate = 22;
    memset(&start, 0, sizeof start);
    start.step_ms = 16; start.step_rate = 60; start.gen1_rate = 22;
    for (int i = 0; i < ACR_GEN1_MAX_FIGHTERS; ++i) {
        acr_gen1_fighter *f = &start.f[i];
        f->def = &def;
        f->intercept_shot = -1; f->swing_target = -1; f->last_missile = -1;
        acr_gen1_ac_spawn(&f->ac, &def, (i & 2) ? 1500 * 64 : 0, (i & 1) ? 2500 * 64 : 0, (i & 1) ? 2048 : 0);
        f->ap = f->ap_max = 4000;
        f->shell_factor = 27; f->energy_factor = 32;
        acr_gen1_weapon *w = f->weapon;
        memset(w, 0, sizeof f->weapon);
        w[0].kind = ACR_GEN1_GUN; strcpy(w[0].name, "RIFLE");
        w[0].attack = 218; w[0].ammo = 200; w[0].range = 6100;
        w[0].lock_ms = 1; w[0].box_w = 240; w[0].box_h = 240; w[0].inner_w = 200; w[0].inner_h = 200; w[0].lock_value = 60;
        w[0].interval = 5; w[0].hold = 13; w[0].speed = 700; w[0].shots = 1;
        w[1].kind = ACR_GEN1_MISSILE; strcpy(w[1].name, "MISSILE");
        w[1].attack = 830; w[1].ammo = 40; w[1].range = 8000;
        w[1].box_w = 200; w[1].box_h = 200; w[1].inner_w = 160; w[1].inner_h = 160;
        w[1].interval = 10; w[1].lock_value = 128; w[1].lock_ticks = 16; w[1].max_locks = 4;
        w[1].volley_a = 1; w[1].ignition = 1;
        w[1].flight = (acr_gen1_flight){ 50, 60, 80, 350, 110, 1024, 0 };
        w[2].kind = ACR_GEN1_BLADE; strcpy(w[2].name, "LS-2001");
        w[2].attack = 738; w[2].energy = 1; w[2].range = ACR_GEN1_BLADE_REACH; w[2].charge_drain = 2050;
        f->weapons = 3;
        for (int k = 0; k < 3; ++k) f->ammo[k] = w[k].ammo;
    }
}

/* A player who holds controls for a while, with the odd quick press. */
typedef struct { acr_rb_pad held; int left; } pilot;
static acr_rb_pad next_pad(pilot *p) {
    if (p->left-- <= 0) {
        acr_gen1_input in = { 0 };
        acr_gen1_actions act = { 0 };
        in.move = (int8_t)((int)(rnd() % 3) - 1);
        in.turn = (int8_t)((int)(rnd() % 3) - 1);
        in.strafe = (int8_t)((int)(rnd() % 3) - 1);
        in.boost = (uint8_t)(rnd() % 3 == 0);
        in.look = (int8_t)(rnd() % 8 == 0 ? (int)(rnd() % 4) - 1 : 0);
        act.fire = (uint8_t)(rnd() % 2);
        p->held = acr_rb_pack(&in, &act);
        p->left = 1 + (int)(rnd() % 40);
    }
    acr_rb_pad pad = p->held;
    if (rnd() % 30 == 0) pad |= 1u << 12;   /* next weapon */
    if (rnd() % 60 == 0) pad |= 1u << 13;   /* blade */
    if (rnd() % 20 == 0) pad |= 1u << 9;    /* boost tapped */
    return pad;
}

/* ---------------- a simulated network ---------------- */

#define QMAX 4096
typedef struct { int to; int at; int len; uint8_t data[ACR_RB_PACKET_MAX]; } packet;
static packet q[QMAX];
static int qn;
typedef struct { int lag, jitter, loss; } link;

static void send_packet(int to, const uint8_t *d, int len, int now, const link *l) {
    if (l->loss && (int)(rnd() % 100) < l->loss) return;
    if (qn >= QMAX) return;
    packet *p = &q[qn++];
    p->to = to;
    p->at = now + l->lag + (l->jitter ? (int)(rnd() % (unsigned)(l->jitter + 1)) : 0);
    p->len = len;
    memcpy(p->data, d, (size_t)len);
}

static void deliver(acr_rb *rb[2], int now) {
    for (int i = 0; i < qn;) {
        if (q[i].at <= now) {
            acr_rb_read(rb[q[i].to], q[i].data, q[i].len);
            q[i] = q[--qn];       /* out of order on purpose */
        } else {
            ++i;
        }
    }
}

static acr_rb A, B;
static acr_rb_pad gen[2][4096];
static acr_gen1_match ref;

/* Plays `frames` steps between two machines and checks they agree. */
static void play(const char *name, int frames, int delay, const link *l, int corrupt_at) {
    qn = 0;
    memset(gen, 0, sizeof gen);
    acr_rb_init(&A, 0, delay, &start, &rules);
    static acr_gen1_battle other;
    other = start;
    other.f[0].ap -= 1;          /* "different data" on the second machine */
    acr_rb_init(&B, 1, delay, corrupt_at ? &other : &start, &rules);
    acr_rb *rb[2] = { &A, &B };
    pilot pl[2] = { { 0, 0 }, { 0, 0 } };
    int now = 0, skip[2] = { 0, 0 };
    /* B starts a few frames late, as a joining machine does. */
    for (int it = 0; it < frames * 4 && (A.frame < (uint32_t)frames || B.frame < (uint32_t)frames); ++it, now += 16) {
        deliver(rb, now);
        for (int k = 0; k < 2; ++k) {
            if (k == 1 && it < 5) continue;
            acr_rb *r = rb[k];
            if (r->frame >= (uint32_t)frames) {
                acr_rb_settle(r);
            } else if (skip[k] > 0) {
                --skip[k];
            } else if (acr_rb_should_wait(r) && rnd() % 4 == 0) {
                skip[k] = 1;
            } else {
                const uint32_t f = r->frame + (uint32_t)delay;
                const acr_rb_pad p = next_pad(&pl[k]);
                if (acr_rb_tick(r, p)) gen[k][f] = p;
                else pl[k].left++;   /* (the same pad again next frame) */
            }
            uint8_t buf[ACR_RB_PACKET_MAX];
            const int n = acr_rb_write(r, buf, sizeof buf);
            if (n) send_packet(1 - k, buf, n, now, l);
        }
    }
    /* Let the last packets land and the corrections settle. */
    for (int it = 0; it < 100; ++it, now += 16) {
        deliver(rb, now);
        for (int k = 0; k < 2; ++k) {
            acr_rb_settle(rb[k]);
            uint8_t buf[ACR_RB_PACKET_MAX];
            const int n = acr_rb_write(rb[k], buf, sizeof buf);
            if (n) send_packet(1 - k, buf, n, now, l);
        }
    }
    EXPECT(A.frame == (uint32_t)frames && B.frame == (uint32_t)frames);
    /* The plain run with the same controls. */
    acr_gen1_match_init(&ref, &start, &rules);
    for (int f = 0; f < frames; ++f) {
        acr_gen1_input in[2];
        acr_gen1_actions act[2];
        acr_rb_unpack(gen[0][f], &in[0], &act[0]);
        acr_rb_unpack(gen[1][f], &in[1], &act[1]);
        acr_gen1_match_step(&ref, &start, &rules, in, act);
    }
    const uint32_t ha = acr_gen1_match_hash(&A.cur), hb = acr_gen1_match_hash(&B.cur), hr = acr_gen1_match_hash(&ref);
    printf("%-26s rounds %d  wins %d-%d  rollbacks %u/%u (deepest %u)  waits %u/%u  checks %u/%u  desync %u/%u\n",
           name, ref.round, ref.wins[0], ref.wins[1], A.rollbacks, B.rollbacks, A.deepest > B.deepest ? A.deepest : B.deepest,
           A.waits, B.waits, A.checks_ok, B.checks_ok, A.desync_frame, B.desync_frame);
    if (corrupt_at) {
        EXPECT(A.desync_frame != 0 && B.desync_frame != 0);
    } else {
        EXPECT(ha == hr);
        EXPECT(hb == hr);
        EXPECT(A.desync_frame == 0 && B.desync_frame == 0);
        EXPECT(A.checks_ok > 10 && B.checks_ok > 10);
        if (l->lag > 16 * (delay + 1)) EXPECT(A.rollbacks > 0 && B.rollbacks > 0);
    }
}

int main(void) {
    setup();
    const link lan_c = { 1, 0, 0 }, home_c = { 40, 10, 2 }, far_c = { 90, 30, 5 };

    /* Pads survive packing. */
    for (int i = 0; i < 2000; ++i) {
        pilot p = { 0, 0 };
        const acr_rb_pad a = next_pad(&p);
        acr_gen1_input in;
        acr_gen1_actions act;
        acr_rb_unpack(a, &in, &act);
        EXPECT(acr_rb_pack(&in, &act) == a);
    }
    {
        const acr_gen1_input in = { 1, -1, 0, 1, 1, 2, 0 };
        const acr_gen1_actions act = { 1, 1, 1 };
        acr_gen1_input o;
        acr_gen1_actions oa;
        acr_rb_unpack(acr_rb_pack(&in, &act), &o, &oa);
        EXPECT(o.move == 1 && o.turn == -1 && o.strafe == 0 && o.boost && o.boost_tapped && o.look == 2);
        EXPECT(oa.fire && oa.next_weapon && oa.blade);
        acr_rb_unpack(acr_rb_held(acr_rb_pack(&in, &act)), &o, &oa);
        EXPECT(o.boost && !o.boost_tapped && oa.fire && !oa.next_weapon && !oa.blade);
    }

    /* A snapshot replays exactly: copy the match, run on, go back, run the
     * same controls again. */
    {
        static acr_gen1_match m, snap;
        static acr_rb_pad pads[2][1200];
        pilot pl[2] = { { 0, 0 }, { 0, 0 } };
        for (int f = 0; f < 1200; ++f) { pads[0][f] = next_pad(&pl[0]); pads[1][f] = next_pad(&pl[1]); }
        acr_gen1_match_init(&m, &start, &rules);
        uint32_t h_mid = 0;
        for (int f = 0; f < 1200; ++f) {
            if (f == 400) { snap = m; h_mid = acr_gen1_match_hash(&m); }
            acr_gen1_input in[2];
            acr_gen1_actions act[2];
            acr_rb_unpack(pads[0][f], &in[0], &act[0]);
            acr_rb_unpack(pads[1][f], &in[1], &act[1]);
            acr_gen1_match_step(&m, &start, &rules, in, act);
        }
        const uint32_t h_end = acr_gen1_match_hash(&m);
        m = snap;
        EXPECT(acr_gen1_match_hash(&m) == h_mid);
        for (int f = 400; f < 1200; ++f) {
            acr_gen1_input in[2];
            acr_gen1_actions act[2];
            acr_rb_unpack(pads[0][f], &in[0], &act[0]);
            acr_rb_unpack(pads[1][f], &in[1], &act[1]);
            acr_gen1_match_step(&m, &start, &rules, in, act);
        }
        EXPECT(acr_gen1_match_hash(&m) == h_end);
        EXPECT(h_mid != h_end);
        /* The fight did something: shots fired, AP lost. */
        EXPECT(m.b.f[0].ammo[0] < 200 || m.b.f[1].ammo[0] < 200);
    }

    /* Co-op: the two players (team 0) against two AI ACs, which run inside
     * the match on both machines. */
    start.count = 4;
    for (int i = 0; i < 4; ++i) start.f[i].team = (uint8_t)(i >= 2);
    /* players 0 and 1 side by side at z 0; the AIs across at z 2500 */
    acr_gen1_ac_spawn(&start.f[1].ac, &def, 1500 * 64, 0, 0);
    acr_gen1_ac_spawn(&start.f[2].ac, &def, 0, 2500 * 64, 2048);
    acr_gen1_ac_spawn(&start.f[3].ac, &def, 1500 * 64, 2500 * 64, 2048);
    rules.ai_mask = 0xC;
    rules.ai_seed = 99;
    acr_gen1_ai_preset(&rules.ai[2], ACR_GEN1_AI_AGGRESSOR);
    acr_gen1_ai_preset(&rules.ai[3], ACR_GEN1_AI_SNIPER);
    {
        /* The AI really flies: it moves and shoots. */
        static acr_gen1_match m;
        acr_gen1_match_init(&m, &start, &rules);
        const acr_gen1_input in[2] = { { 0 } };
        const acr_gen1_actions act[2] = { { 0 } };
        const int32_t z0 = m.b.f[2].ac.z;
        for (int f = 0; f < 600; ++f) acr_gen1_match_step(&m, &start, &rules, in, act);
        EXPECT(m.b.f[2].ac.z != z0);
        EXPECT(m.b.f[2].ammo[0] < 200 || m.b.f[2].ammo[1] < 40 || m.b.f[3].ammo[0] < 200 || m.b.f[3].ammo[1] < 40);
        EXPECT(m.b.f[0].ap < m.b.f[0].ap_max || m.b.f[1].ap < m.b.f[1].ap_max);
    }
    play("co-op vs 2 AI, LAN", 3000, 0, &lan_c, 0);
    play("co-op vs 2 AI, 90 ms 5%", 3000, 2, &far_c, 0);
    rules.ai_mask = 0x4;                     /* two players against one AI */
    start.count = 3;
    play("co-op vs 1 AI, 40 ms 2%", 3000, 1, &home_c, 0);
    play("co-op, different data", 1500, 1, &home_c, 1);

    /* Matches over different networks: in step, and the same as a plain run. */
    setup();                                 /* back to 1v1 */
    rules.ai_mask = 0;
    const link lan = { 1, 0, 0 }, home = { 40, 10, 2 }, far = { 90, 30, 5 }, awful = { 120, 60, 20 };
    play("LAN, no delay", 3000, 0, &lan, 0);
    play("LAN, delay 1", 3000, 1, &lan, 0);
    play("40 ms + jitter, 2% loss", 3000, 1, &home, 0);
    play("90 ms + jitter, 5% loss", 3000, 2, &far, 0);
    play("120 ms, 20% loss", 3000, 2, &awful, 0);
    play("different data on purpose", 1500, 1, &home, 1);

    if (failures) printf("%d failure(s)\n", failures);
    else printf("rollback: all passed\n");
    return failures ? 1 : 0;
}
