/* Lockstep netplay: 2, 3 and 4 machines talking through the host over a
 * simulated network (lag, jitter, lost and reordered packets) always show
 * the same match, equal to a plain run of the same controls; a machine that
 * stalls makes the others wait instead of going on without it; different
 * data on one machine is caught at the first check. */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_match.h"
#include "acr_lockstep.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* ---------------- the fight (as tests/test_rollback.c sets it up) ---------------- */

static acr_gen1_ac_def def;
static acr_gen1_battle start;
static acr_gen1_match_rules rules = { .round_ms = 20000, .next_round_ms = 3000, .arena_half = 20000 };

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
    if (rnd() % 25 == 0) pad |= 1u << 17;   /* switch target */
    if (rnd() % 60 == 0) pad |= 1u << 13;   /* blade */
    if (rnd() % 20 == 0) pad |= 1u << 9;    /* boost tapped */
    return pad;
}

/* ---------------- a simulated network, every packet through the host ---------------- */

#define QMAX 8192
typedef struct { int to; int at; int len; uint8_t data[ACR_LS_PACKET_MAX]; } packet;
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

static void deliver(acr_ls *ls, int now) {
    for (int i = 0; i < qn;) {
        if (q[i].at <= now) {
            acr_ls_read(&ls[q[i].to], q[i].data, q[i].len);
            q[i] = q[--qn];       /* out of order on purpose */
        } else {
            ++i;
        }
    }
}

static void send_all(acr_ls *ls, int n, int k, int now, const link *l) {
    uint8_t buf[ACR_LS_PACKET_MAX];
    for (int to = 0; to < n; ++to) {
        const int len = acr_ls_write(&ls[k], to, buf, sizeof buf);
        if (len) send_packet(to, buf, len, now, l);
    }
}

#define FMAX 8192
static acr_ls M[ACR_LS_PLAYERS];
static acr_rb_pad gen[ACR_LS_PLAYERS][FMAX];
static acr_gen1_match ref;

/* n machines play `frames` steps. corrupt: that machine (> 0) has one AP
 * less on an AC ("different data"). stall: machine 1 stops for 120 frames
 * at step 600 (a window dragged, a slow disk). */
static void play(const char *name, int n, int frames, int delay, const link *l, int corrupt, int stall) {
    qn = 0;
    memset(gen, 0, sizeof gen);
    static acr_gen1_battle other;
    other = start;
    other.f[0].ap -= 1;
    for (int k = 0; k < n; ++k) acr_ls_init(&M[k], n, k, delay, corrupt && k == corrupt ? &other : &start, &rules);
    pilot pl[ACR_LS_PLAYERS];
    memset(pl, 0, sizeof pl);
    int now = 0, skip[ACR_LS_PLAYERS] = { 0 }, stalled = 0, most_ahead = 0, iterations = 0;
    for (int it = 0; it < frames * 6; ++it, now += 16) {
        int done = 1;
        for (int k = 0; k < n; ++k) done &= M[k].frame >= (uint32_t)frames;
        if (done) break;
        ++iterations;
        deliver(M, now);
        for (int k = 0; k < n; ++k) {
            if (k && it < 3 * k) continue;                 /* guests start a few frames late, one after the other */
            acr_ls *m = &M[k];
            const int frozen = stall && k == 1 && m->frame >= 600 && stalled < 120;
            if (frozen) {
                ++stalled;                                  /* no steps, no packets */
                continue;
            }
            if (m->frame < (uint32_t)frames) {
                if (skip[k] > 0) {
                    --skip[k];
                } else if (acr_ls_should_wait(m) && rnd() % 4 == 0) {
                    skip[k] = 1;
                } else {
                    acr_rb_pad p = 0;
                    if (acr_ls_wants_pad(m)) {
                        p = next_pad(&pl[k]);
                        if (m->frame + (uint32_t)delay < FMAX) gen[k][m->frame + (uint32_t)delay] = p;
                    }
                    acr_ls_tick(m, p);
                }
            }
            send_all(M, n, k, now, l);
        }
        /* Nobody ever runs past a step someone else has no controls for. */
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b) {
                const int ahead = (int)M[a].frame - (int)M[b].frame;
                if (ahead > most_ahead) most_ahead = ahead;
            }
    }
    for (int k = 0; k < n; ++k) EXPECT(M[k].frame == (uint32_t)frames);
    EXPECT(most_ahead <= delay + 1);
    /* The plain run with the same controls. */
    acr_gen1_match_init(&ref, &start, &rules);
    for (int f = 0; f < frames; ++f) {
        acr_gen1_input in[ACR_GEN1_MAX_FIGHTERS];
        acr_gen1_actions act[ACR_GEN1_MAX_FIGHTERS];
        memset(in, 0, sizeof in);
        memset(act, 0, sizeof act);
        for (int k = 0; k < n; ++k) acr_rb_unpack(gen[k][f], &in[k], &act[k]);
        acr_gen1_match_step(&ref, &start, &rules, in, act);
    }
    const uint32_t hr = acr_gen1_match_hash(&ref);
    uint32_t waits = 0, checks = 0;
    int desyncs = 0;
    for (int k = 0; k < n; ++k) { waits += M[k].waits; checks += M[k].checks_ok; desyncs += M[k].desync_frame != 0; }
    printf("%-34s rounds %d  wins %d-%d  %d frames for %d steps  waits %u  checks %u  desyncs %d\n", name, ref.round,
           ref.wins[0], ref.wins[1], iterations, frames, waits, checks, desyncs);
    if (corrupt) {
        /* The host and the machine with the wrong data both know, from the first check. */
        EXPECT(M[0].desync_frame == ACR_LS_HASH_EVERY + 1 && M[0].desync_with == corrupt);
        EXPECT(M[corrupt].desync_frame == ACR_LS_HASH_EVERY + 1);
    } else {
        for (int k = 0; k < n; ++k) {
            EXPECT(acr_gen1_match_hash(&M[k].cur) == hr);
            EXPECT(M[k].desync_frame == 0);
            EXPECT(M[k].checks_ok > 10);
            EXPECT(M[k].packets_bad == 0);
            /* the pads a replay would save are still held right after the step */
            acr_rb_pad pads[ACR_LS_PLAYERS];
            EXPECT(acr_ls_pads(&M[k], (uint32_t)frames - 1, pads));
            for (int p = 0; p < n; ++p) EXPECT(pads[p] == gen[p][frames - 1]);
        }
        if (stall) EXPECT(iterations >= frames + 100);      /* everyone waited the stall out */
        else if (l->lag <= 16 && !l->loss) EXPECT(iterations < frames + frames / 20);   /* a LAN never drags */
    }
}

int main(void) {
    setup();
    const link lan_c = { 1, 0, 0 }, wifi_c = { 12, 10, 1 }, home_c = { 40, 10, 2 };

    /* Two machines. */
    play("2 players, LAN, delay 2", 2, 3000, 2, &lan_c, 0, 0);
    play("2 players, LAN, delay 1", 2, 3000, 1, &lan_c, 0, 0);
    play("2 players, Wi-Fi, delay 3", 2, 3000, 3, &wifi_c, 0, 0);
    play("2 players, 40 ms 2% loss, delay 6", 2, 3000, 6, &home_c, 0, 0);
    play("2 players, too little delay", 2, 1000, 1, &home_c, 0, 0);      /* slow, but still right */
    play("2 players, one stalls", 2, 3000, 2, &lan_c, 0, 1);
    play("2 players, different data", 2, 600, 2, &lan_c, 1, 0);

    /* Bad packets change nothing. */
    {
        acr_ls *a = &M[0];
        acr_ls_init(a, 2, 0, 2, &start, &rules);
        static acr_ls b, before;
        acr_ls_init(&b, 2, 1, 2, &start, &rules);
        uint8_t buf[ACR_LS_PACKET_MAX];
        acr_ls_tick(&b, 5);
        const int len = acr_ls_write(&b, 0, buf, sizeof buf);
        EXPECT(len > 0);
        EXPECT(acr_ls_write(&b, 1, buf, sizeof buf) == 0);                /* not to itself */
        EXPECT(acr_ls_write(&b, 0, buf, 64) == 0);                        /* not into too small a buffer */
        EXPECT(acr_ls_write(&b, 0, buf, sizeof buf) == len);
        memcpy(&before, a, sizeof before);
        EXPECT(acr_ls_read(a, buf, len - 1) == 0);                        /* cut short */
        uint8_t bad[ACR_LS_PACKET_MAX];
        memcpy(bad, buf, (size_t)len); bad[4] ^= 0xFF; EXPECT(acr_ls_read(a, bad, len) == 0);   /* another version */
        memcpy(bad, buf, (size_t)len); bad[6] = 0; EXPECT(acr_ls_read(a, bad, len) == 0);       /* "from" ourselves */
        memcpy(bad, buf, (size_t)len); bad[7] = 3; EXPECT(acr_ls_read(a, bad, len) == 0);       /* another match size */
        memcpy(bad, buf, (size_t)len); bad[21] = 9; EXPECT(acr_ls_read(a, bad, len) == 0);      /* sections that aren't there */
        EXPECT(a->packets_bad == 5);
        a->packets_bad = 0;
        EXPECT(memcmp(a, &before, sizeof before) == 0);
        EXPECT(acr_ls_read(a, buf, len) == 1 && a->next[1] == 3);         /* the real one: B's pad for step 2 */
        /* a guest can't send the host someone else's pads */
        memcpy(bad, buf, (size_t)len);
        bad[22 + 8] = 0;                                                  /* the section's player: 1 -> 0 */
        const uint32_t host_next = a->next[0];
        EXPECT(acr_ls_read(a, bad, len) == 1 && a->next[0] == host_next);
        /* goodbye */
        const int bye = acr_ls_write_bye(&b, buf, sizeof buf);
        EXPECT(bye > 0 && acr_ls_read(a, buf, bye) == 2 && a->gone[1]);
    }

    /* Three and four machines: every AC a player (teams of 1 v 2, then 2 v 2). */
    start.count = 3;
    start.f[0].team = 0; start.f[1].team = 1; start.f[2].team = 1;
    play("3 players, LAN, delay 2", 3, 3000, 2, &lan_c, 0, 0);
    play("3 players, Wi-Fi, delay 4", 3, 3000, 4, &wifi_c, 0, 0);
    start.count = 4;
    for (int i = 0; i < 4; ++i) start.f[i].team = (uint8_t)(i >= 2);
    acr_gen1_ac_spawn(&start.f[1].ac, &def, 1500 * 64, 0, 0);
    acr_gen1_ac_spawn(&start.f[2].ac, &def, 0, 2500 * 64, 2048);
    acr_gen1_ac_spawn(&start.f[3].ac, &def, 1500 * 64, 2500 * 64, 2048);
    play("4 players 2v2, LAN, delay 2", 4, 3000, 2, &lan_c, 0, 0);
    play("4 players 2v2, Wi-Fi, delay 4", 4, 3000, 4, &wifi_c, 0, 0);
    play("4 players 2v2, 40 ms 2%, delay 8", 4, 3000, 8, &home_c, 0, 0);
    play("4 players 2v2, one stalls", 4, 3000, 2, &lan_c, 0, 1);
    play("4 players 2v2, guest 3 differs", 4, 600, 2, &lan_c, 3, 0);

    /* Co-op: two players against two computer pilots, who fly inside the match. */
    rules.ai_mask = 0xC;
    rules.ai_seed = 99;
    acr_gen1_ai_preset(&rules.ai[2], ACR_GEN1_AI_AGGRESSOR);
    acr_gen1_ai_preset(&rules.ai[3], ACR_GEN1_AI_SNIPER);
    play("co-op, 2 players + 2 AI, LAN", 2, 3000, 2, &lan_c, 0, 0);

    printf(failures ? "%d FAILED\n" : "all lockstep tests passed\n", failures);
    return failures ? 1 : 0;
}
