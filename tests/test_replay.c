/* Replays: a recorded fight plays back to the same result; a recording of an
 * online match (made from the final pads behind rollback) is the same on
 * both machines and equals a plain run; damaged, cut-off and foreign files
 * are told apart; and one fixed fight must keep its hash (the tripwire for
 * changes to how fights play out: see GOLDEN below). */
#include <stdio.h>
#include <string.h>

#include "acr_gen1_match.h"
#include "acr_replay.h"
#include "acr_rollback.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* The fixed fight's numbers, the same on every machine and compiler (the
 * Linux and Windows builds both run this). If a change makes fights play
 * out differently ON PURPOSE: raise ACR_REPLAY_RULES in
 * src/net/acr_replay.h (older replays are then refused, not played
 * wrongly) and put the three numbers the failing test prints in here. If it
 * was not on purpose, the change broke replays and online play between
 * builds: fix the change instead. */
#define GOLDEN_RULES   4
#define GOLDEN_HASH    0x07FA6D31u
#define GOLDEN_STREAM  0xBC861526u
#define GOLDEN_BYTES   11690u

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

static void step_with(acr_gen1_match *m, const acr_rb_pad pads[2]) {
    acr_gen1_input in[ACR_GEN1_MAX_FIGHTERS];
    acr_gen1_actions act[ACR_GEN1_MAX_FIGHTERS];
    memset(in, 0, sizeof in);
    memset(act, 0, sizeof act);
    acr_rb_unpack(pads[0], &in[0], &act[0]);
    acr_rb_unpack(pads[1], &in[1], &act[1]);
    acr_gen1_match_step(m, &start, &rules, in, act);
}

static uint32_t fnv(uint32_t h, const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
/* Makes a changed header valid again (for the tests that change one field). */
static void reseal_header(uint8_t *file) {
    const uint32_t setup_len = (uint32_t)file[24] | (uint32_t)file[25] << 8;
    put32(file + 28, fnv(fnv(2166136261u, file, 28), file + ACR_REPLAY_HEADER, setup_len));
}

#define CAP (512 * 1024)
static uint8_t file_a[CAP], file_b[CAP], scratch[CAP];
static acr_gen1_match m, m2, snap;
static const char setup_block[] = "stage=7;p1=0102030405060708;p2=1112131415161718;mode=vs";
static const acr_replay_info vs_info = { .kind = ACR_REPLAY_MATCH, .tracks = 2, .step_ms = 16,
                                         .check_every = ACR_REPLAY_CHECK_EVERY, .data_check = 0xC0FFEE01u, .seed = 77 };

/* Records `frames` steps of a plain match into out; returns the length and
 * leaves the match in m. */
static uint32_t record_plain(uint8_t *out, uint32_t cap, int frames, uint32_t pilot_seed, acr_replay_rec *rec) {
    seed = pilot_seed;
    pilot pl[2] = { { 0, 0 }, { 0, 0 } };
    EXPECT(acr_replay_rec_begin(rec, out, cap, &vs_info, setup_block, (uint32_t)sizeof setup_block));
    acr_gen1_match_init(&m, &start, &rules);
    for (int f = 0; f < frames; ++f) {
        acr_rb_pad pads[2];
        pads[0] = next_pad(&pl[0]);   /* (in this order on every compiler) */
        pads[1] = next_pad(&pl[1]);
        acr_replay_rec_match(rec, &m, pads);
        step_with(&m, pads);
    }
    return acr_replay_rec_end_match(rec, &m);
}

/* ---------------- a simulated network (as tests/test_rollback.c) ---------------- */

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
static acr_rb_pad gen[2][8192];

/* Two machines play `frames` steps over a link, each recording its own
 * replay from the final pads. cut_at > 0: from that step on nothing from B
 * reaches A any more (B's player pulled the plug). */
static void online(const char *name, int frames, int delay, const link *l, int cut_at) {
    qn = 0;
    memset(gen, 0, sizeof gen);
    acr_rb_init(&A, 0, delay, &start, &rules);
    acr_rb_init(&B, 1, delay, &start, &rules);
    acr_rb *rb[2] = { &A, &B };
    acr_replay_rec rec[2];
    uint8_t *out[2] = { file_a, file_b };
    for (int k = 0; k < 2; ++k) EXPECT(acr_replay_rec_begin(&rec[k], out[k], CAP, &vs_info, setup_block, (uint32_t)sizeof setup_block));
    pilot pl[2] = { { 0, 0 }, { 0, 0 } };
    int now = 0, skip[2] = { 0, 0 }, stalled = 0;
    for (int it = 0; it < frames * 4 && (A.frame < (uint32_t)frames || B.frame < (uint32_t)frames); ++it, now += 16) {
        deliver(rb, now);
        for (int k = 0; k < 2; ++k) {
            if (k == 1 && it < 5) continue;   /* B starts a few frames late, as a joining machine does */
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
            acr_replay_rec_rollback(&rec[k], r);
            uint8_t buf[ACR_RB_PACKET_MAX];
            const int n = acr_rb_write(r, buf, sizeof buf);
            if (n && !(cut_at && k == 1 && B.frame >= (uint32_t)cut_at)) send_packet(1 - k, buf, n, now, l);
        }
        /* After the cut A runs to the end of its window and stops: nothing more is coming. */
        stalled = cut_at && B.frame >= (uint32_t)cut_at && A.frame >= A.remote_next + ACR_RB_WINDOW ? stalled + 1 : 0;
        if (stalled > 30) break;
    }
    for (int it = 0; it < 100 && !cut_at; ++it, now += 16) {   /* the last packets land */
        deliver(rb, now);
        for (int k = 0; k < 2; ++k) {
            acr_rb_settle(rb[k]);
            acr_replay_rec_rollback(&rec[k], rb[k]);
            uint8_t buf[ACR_RB_PACKET_MAX];
            const int n = acr_rb_write(rb[k], buf, sizeof buf);
            if (n) send_packet(1 - k, buf, n, now, l);
        }
    }
    const uint32_t len_a = acr_replay_rec_end_rollback(&rec[0], &A);
    const uint32_t len_b = cut_at ? 0 : acr_replay_rec_end_rollback(&rec[1], &B);
    EXPECT(len_a > 0 && !rec[0].failed);
    acr_replay_reader rd;
    EXPECT(acr_replay_open(&rd, file_a, len_a) == ACR_REPLAY_OK);
    EXPECT(rd.complete);
    /* The plain run of the same controls, as far as the replay goes. */
    acr_gen1_match_init(&m2, &start, &rules);
    for (uint32_t f = 0; f < rd.steps; ++f) {
        const acr_rb_pad pads[2] = { gen[0][f], gen[1][f] };
        step_with(&m2, pads);
    }
    EXPECT(rd.end.hash == acr_gen1_match_hash(&m2));
    EXPECT(acr_replay_play_match(&rd, &m, &start, &rules) == 0);
    EXPECT(acr_gen1_match_hash(&m) == acr_gen1_match_hash(&m2));
    if (!cut_at) {
        EXPECT(rd.steps == (uint32_t)frames);
        EXPECT(A.desync_frame == 0 && B.desync_frame == 0);
        /* Both machines recorded the same file. */
        EXPECT(len_b == len_a && memcmp(file_a, file_b, len_a) == 0);
        EXPECT(rd.end.rounds == m2.round && rd.end.wins[0] == m2.wins[0] && rd.end.wins[1] == m2.wins[1]);
    } else {
        /* It stops where B's pads stop: behind what A's screen showed. */
        EXPECT(rd.steps < A.frame && rd.steps + ACR_RB_WINDOW >= A.frame && rd.steps + 8 >= (uint32_t)cut_at);
    }
    printf("%-30s steps %u  %u bytes  rollbacks %u/%u  checks %u\n", name, rd.steps, len_a, A.rollbacks, B.rollbacks, A.checks_ok);
}

int main(int argc, char **argv) {
    setup();
    acr_replay_rec rec;
    acr_replay_reader rd;

    /* ---- A plain match: three rounds' worth, recorded and played back. ---- */
    const int FRAMES = 4000;
    const uint32_t len = record_plain(file_a, CAP, FRAMES, 2026, &rec);
    const uint32_t h_end = acr_gen1_match_hash(&m);
    EXPECT(len > 0 && !rec.failed && rec.steps == (uint32_t)FRAMES);
    EXPECT(m.round >= 3);                                    /* round ends and restarts are in it */
    EXPECT(m.b.f[0].ammo[0] < 200 || m.b.f[1].ammo[0] < 200); /* and the fight did something */
    EXPECT(acr_replay_open(&rd, file_a, len) == ACR_REPLAY_OK);
    EXPECT(rd.complete && rd.steps == (uint32_t)FRAMES && rd.end.steps == (uint32_t)FRAMES && rd.end.hash == h_end);
    EXPECT(rd.end.rounds == m.round && rd.end.wins[0] == m.wins[0] && rd.end.wins[1] == m.wins[1]);
    EXPECT(rd.info.kind == ACR_REPLAY_MATCH && rd.info.tracks == 2 && rd.info.step_ms == 16);
    EXPECT(rd.info.check_every == ACR_REPLAY_CHECK_EVERY && rd.info.data_check == 0xC0FFEE01u && rd.info.seed == 77);
    EXPECT(rd.info.rules == ACR_REPLAY_RULES);
    EXPECT(rd.setup_len == sizeof setup_block && memcmp(rd.setup, setup_block, sizeof setup_block) == 0);
    EXPECT(acr_replay_fits(&rd, 0xC0FFEE01u) == ACR_REPLAY_OK);
    EXPECT(acr_replay_fits(&rd, 0xC0FFEE02u) == ACR_REPLAY_OTHER_DATA);
    EXPECT(acr_replay_play_match(&rd, &m2, &start, &rules) == 0);
    EXPECT(acr_gen1_match_hash(&m2) == h_end && m2.frame == (uint32_t)FRAMES);
    /* Small, even with these restless pilots (ten changes a second each).
     * Raw would be 6 bytes a step. */
    printf("plain match: %d steps (%d s) in %u bytes, %u raw; worst case %u\n", FRAMES, FRAMES * 16 / 1000, len,
           (unsigned)FRAMES * 6u, acr_replay_worst_size(2, (uint32_t)FRAMES, ACR_REPLAY_CHECK_EVERY, (uint32_t)sizeof setup_block));
    EXPECT(len < (uint32_t)FRAMES * 6u * 2u / 3u);
    EXPECT(len <= acr_replay_worst_size(2, (uint32_t)FRAMES, ACR_REPLAY_CHECK_EVERY, (uint32_t)sizeof setup_block));

    /* ---- The tripwire: this fight's numbers never change by accident. ---- */
    {
        const uint32_t stream = get32(file_a + len - 4);
        if (argc > 1 && strcmp(argv[1], "--golden") == 0) {
            printf("#define GOLDEN_RULES   %d\n#define GOLDEN_HASH    0x%08Xu\n#define GOLDEN_STREAM  0x%08Xu\n#define GOLDEN_BYTES   %uu\n",
                   ACR_REPLAY_RULES, (unsigned)h_end, (unsigned)stream, (unsigned)len);
            return 0;
        }
        if (h_end != GOLDEN_HASH || stream != GOLDEN_STREAM || len != GOLDEN_BYTES || GOLDEN_RULES != ACR_REPLAY_RULES) {
            printf("FAIL: the fixed fight no longer plays out as it did (or ACR_REPLAY_RULES changed without it).\n"
                   "  Was:  hash 0x%08X  stream 0x%08X  %u bytes  (rules %d)\n"
                   "  Now:  hash 0x%08X  stream 0x%08X  %u bytes  (rules %d)\n"
                   "  If the change to the fight's rules is meant: raise ACR_REPLAY_RULES in src/net/acr_replay.h\n"
                   "  and set GOLDEN_* in tests/test_replay.c to (test_replay --golden prints them):\n"
                   "    GOLDEN_RULES %d  GOLDEN_HASH 0x%08Xu  GOLDEN_STREAM 0x%08Xu  GOLDEN_BYTES %uu\n"
                   "  If it is not meant, replays and online play between builds are broken: fix the change.\n",
                   (unsigned)GOLDEN_HASH, (unsigned)GOLDEN_STREAM, (unsigned)GOLDEN_BYTES, GOLDEN_RULES,
                   (unsigned)h_end, (unsigned)stream, (unsigned)len, ACR_REPLAY_RULES,
                   ACR_REPLAY_RULES + (GOLDEN_RULES == ACR_REPLAY_RULES), (unsigned)h_end, (unsigned)stream, (unsigned)len);
            ++failures;
        }
    }

    /* ---- Held controls cost next to nothing: both players hold one pad
     * for the whole fight. ---- */
    {
        acr_gen1_input in = { 0 };
        acr_gen1_actions act = { 0 };
        in.move = 1; in.turn = 1; act.fire = 1;
        const acr_rb_pad hold = acr_rb_pack(&in, &act);
        const acr_rb_pad pads[2] = { hold, hold };
        EXPECT(acr_replay_rec_begin(&rec, file_b, CAP, &vs_info, NULL, 0));
        acr_gen1_match_init(&m2, &start, &rules);
        for (int f = 0; f < FRAMES; ++f) { acr_replay_rec_match(&rec, &m2, pads); step_with(&m2, pads); }
        const uint32_t quiet = acr_replay_rec_end_match(&rec, &m2);
        const uint32_t h_quiet = acr_gen1_match_hash(&m2);
        acr_replay_reader q_rd;
        EXPECT(quiet > 0 && quiet < 500);
        EXPECT(acr_replay_open(&q_rd, file_b, quiet) == ACR_REPLAY_OK && q_rd.steps == (uint32_t)FRAMES);
        EXPECT(acr_replay_play_match(&q_rd, &m2, &start, &rules) == 0 && acr_gen1_match_hash(&m2) == h_quiet);
        printf("held controls: %d steps in %u bytes\n", FRAMES, (unsigned)quiet);
    }

    /* ---- Jumping back: the reader is copied along with the match. ---- */
    {
        acr_replay_reader at;
        acr_replay_rewind(&rd);
        acr_gen1_match_init(&m2, &start, &rules);
        for (int f = 0; f < 1500; ++f) EXPECT(acr_replay_match_step(&rd, &m2, &start, &rules) == 1);
        at = rd; snap = m2;
        while (acr_replay_match_step(&rd, &m2, &start, &rules)) {}
        EXPECT(acr_gen1_match_hash(&m2) == h_end && rd.mismatch == 0);
        rd = at; m2 = snap;
        EXPECT(rd.step == 1500 && m2.frame == 1500);
        while (acr_replay_match_step(&rd, &m2, &start, &rules)) {}
        EXPECT(acr_gen1_match_hash(&m2) == h_end && rd.mismatch == 0 && rd.step == (uint32_t)FRAMES);
        EXPECT(acr_replay_match_step(&rd, &m2, &start, &rules) == 0);   /* stays at the end */
    }

    /* ---- A spectator: joins when a third of the fight is over, gets the
     * bytes so far, catches up from the first step, then follows the feed
     * in pieces of any size (cut anywhere, even inside a record). ---- */
    {
        acr_replay_reader sp;
        uint32_t have = len / 3;
        int grows = 0;
        seed = 31337;
        EXPECT(acr_replay_open(&sp, file_a, have) == ACR_REPLAY_OK && !sp.complete);
        acr_gen1_match_init(&m2, &start, &rules);
        while (acr_replay_match_step(&sp, &m2, &start, &rules)) {}
        EXPECT(m2.frame == sp.steps && sp.steps > 0 && sp.steps < (uint32_t)FRAMES && sp.mismatch == 0);
        while (have < len) {
            have += 1 + rnd() % 200;
            if (have > len) have = len;
            /* (the feed's buffer is allowed to move: here a fresh copy each time) */
            memcpy(scratch, file_a, have);
            EXPECT(acr_replay_grow(&sp, scratch, have) == ACR_REPLAY_OK);
            const uint32_t before = m2.frame;
            while (acr_replay_match_step(&sp, &m2, &start, &rules)) {}
            EXPECT(m2.frame == sp.steps && m2.frame >= before);
            ++grows;
        }
        EXPECT(sp.complete && sp.mismatch == 0 && sp.steps == (uint32_t)FRAMES && grows > 20);
        EXPECT(acr_gen1_match_hash(&m2) == h_end && sp.end.hash == h_end);
        /* a feed that goes wrong is refused and the reader stays as it was */
        EXPECT(acr_replay_open(&sp, file_a, len / 2) == ACR_REPLAY_OK);
        const uint32_t steps_before = sp.steps, last_before = sp.last;
        memcpy(scratch, file_a, len);
        scratch[len - 20] ^= 0x40;
        EXPECT(acr_replay_grow(&sp, scratch, len) == ACR_REPLAY_DAMAGED);
        EXPECT(sp.steps == steps_before && sp.last == last_before && sp.data == file_a && !sp.complete);
        EXPECT(acr_replay_grow(&sp, file_a, len) == ACR_REPLAY_OK && sp.complete);
        EXPECT(acr_replay_grow(&sp, file_a, len) == ACR_REPLAY_OK);          /* nothing new: fine */
    }

    /* ---- A fight that goes differently is noticed at the next check. ---- */
    {
        static acr_gen1_battle other;
        other = start;
        other.f[1].ap -= 1;                      /* "different data": one AP less */
        const uint32_t at = acr_replay_play_match(&rd, &m2, &other, &rules);
        EXPECT(at == (uint32_t)ACR_REPLAY_CHECK_EVERY + 1u);   /* the first check */
    }

    /* ---- Damage: any one byte changed anywhere is refused. ---- */
    {
        int refused = 0, cut = 0;
        memcpy(scratch, file_a, len);
        for (uint32_t i = 0; i < len; ++i) {
            for (int bit = 0; bit < 8; bit += 7) {
                scratch[i] ^= (uint8_t)(1u << bit);
                acr_replay_reader bad;
                const int err = acr_replay_open(&bad, scratch, len);
                if (err != ACR_REPLAY_OK) ++refused;
                else if (!bad.complete) ++cut;     /* reads as a replay that was never closed */
                else { printf("FAIL: byte %u bit %d changed and the replay still opens whole\n", (unsigned)i, bit); ++failures; }
                scratch[i] ^= (uint8_t)(1u << bit);
            }
        }
        EXPECT(refused > (int)len * 2 - 8);       /* all but the end marker's own byte */
        printf("damage: %d of %u changes refused, %d read as cut off\n", refused, (unsigned)len * 2u, cut);
    }

    /* ---- Cut off anywhere: never complete, never read past the end, and
     * what is left plays in step as far as it goes. ---- */
    {
        uint32_t last_steps = 0;
        int played = 0;
        for (uint32_t n = 0; n < len; ++n) {
            /* an exact-size copy, so reading past the end shows up under a memory checker */
            static uint8_t part[CAP];
            memcpy(part + sizeof part - n, file_a, n);
            acr_replay_reader cutrd;
            const int err = acr_replay_open(&cutrd, part + sizeof part - n, n);
            if (n < ACR_REPLAY_HEADER + sizeof setup_block) { EXPECT(err != ACR_REPLAY_OK); continue; }
            EXPECT(err == ACR_REPLAY_OK && !cutrd.complete);
            EXPECT(cutrd.steps >= last_steps && cutrd.steps <= (uint32_t)FRAMES);
            last_steps = cutrd.steps;
            if (n % 397 == 0 || n == len - 1) {
                EXPECT(acr_replay_play_match(&cutrd, &m2, &start, &rules) == 0);
                EXPECT(m2.frame == cutrd.steps);
                ++played;
            }
        }
        EXPECT(last_steps == (uint32_t)FRAMES && played > 3);
    }

    /* ---- Other builds' files. ---- */
    {
        acr_replay_reader o;
        memcpy(scratch, file_a, len);
        put32(scratch + 8, ACR_REPLAY_RULES + 1);          /* another rules version */
        reseal_header(scratch);
        EXPECT(acr_replay_open(&o, scratch, len) == ACR_REPLAY_OK);
        EXPECT(acr_replay_fits(&o, 0xC0FFEE01u) == ACR_REPLAY_OTHER_RULES);
        memcpy(scratch, file_a, len);
        scratch[4] = ACR_REPLAY_FORMAT + 1;                /* a newer file layout */
        reseal_header(scratch);
        EXPECT(acr_replay_open(&o, scratch, len) == ACR_REPLAY_NEWER_FORMAT);
        EXPECT(acr_replay_open(&o, (const uint8_t *)"ACRN not a replay at all, just some bytes", 41) == ACR_REPLAY_NOT_A_REPLAY);
        EXPECT(acr_replay_open(&o, file_a, 10) == ACR_REPLAY_NOT_A_REPLAY);
        EXPECT(acr_replay_open(&o, NULL, 0) == ACR_REPLAY_NOT_A_REPLAY);
        memcpy(scratch, file_a, len);
        scratch[len] = 0;                                  /* something after the end */
        EXPECT(acr_replay_open(&o, scratch, len + 1) == ACR_REPLAY_DAMAGED);
        for (int e = ACR_REPLAY_OK; e <= ACR_REPLAY_OTHER_DATA; ++e) EXPECT(acr_replay_error_text(e)[0] != 0);
    }

    /* ---- Saving while the fight goes on: a small buffer emptied as it
     * fills gives the same file. ---- */
    {
        static uint8_t small[160];
        uint32_t total = 0;
        seed = 2026;
        pilot pl[2] = { { 0, 0 }, { 0, 0 } };
        EXPECT(acr_replay_rec_begin(&rec, small, sizeof small, &vs_info, setup_block, (uint32_t)sizeof setup_block));
        acr_gen1_match_init(&m, &start, &rules);
        for (int f = 0; f < FRAMES; ++f) {
            acr_rb_pad pads[2];
            pads[0] = next_pad(&pl[0]);   /* (in this order on every compiler) */
            pads[1] = next_pad(&pl[1]);
            acr_replay_rec_match(&rec, &m, pads);
            step_with(&m, pads);
            if (rec.len > sizeof small / 2) { memcpy(scratch + total, small, rec.len); total += rec.len; acr_replay_rec_drop(&rec); }
        }
        const uint32_t tail = acr_replay_rec_end_match(&rec, &m);
        EXPECT(tail > 0 && !rec.failed);
        memcpy(scratch + total, small, tail); total += tail;
        EXPECT(total == len && memcmp(scratch, file_a, len) == 0);
    }

    /* ---- Out of room: the replay fails cleanly, nothing past the buffer. ---- */
    {
        static uint8_t tiny[200 + 8];
        memset(tiny, 0xAB, sizeof tiny);
        seed = 2026;
        pilot pl[2] = { { 0, 0 }, { 0, 0 } };
        EXPECT(acr_replay_rec_begin(&rec, tiny, 200, &vs_info, setup_block, (uint32_t)sizeof setup_block));
        acr_gen1_match_init(&m, &start, &rules);
        for (int f = 0; f < 2000; ++f) {
            acr_rb_pad pads[2];
            pads[0] = next_pad(&pl[0]);   /* (in this order on every compiler) */
            pads[1] = next_pad(&pl[1]);
            acr_replay_rec_match(&rec, &m, pads);
            step_with(&m, pads);
        }
        EXPECT(rec.failed && rec.len <= 200);
        EXPECT(acr_replay_rec_end_match(&rec, &m) == 0);
        for (int i = 200; i < 208; ++i) EXPECT(tiny[i] == 0xAB);
        /* and one that can't even start */
        EXPECT(!acr_replay_rec_begin(&rec, tiny, 40, &vs_info, setup_block, (uint32_t)sizeof setup_block));
        const acr_rb_pad none[2] = { 0, 0 };
        acr_replay_rec_step(&rec, none);                    /* ignored */
        EXPECT(rec.failed && acr_replay_rec_end_match(&rec, &m) == 0);
        acr_replay_info bad = vs_info;
        bad.tracks = 5;
        EXPECT(!acr_replay_rec_begin(&rec, file_b, CAP, &bad, NULL, 0));
    }

    /* ---- Any pads, four tracks, checks anywhere: they come back exactly
     * (a fight the game runs itself saves every AC's pad). ---- */
    {
        enum { N = 6000 };
        static acr_rb_pad want[N][4];
        static uint8_t check_at[N + 1];
        const acr_replay_info four = { .kind = ACR_REPLAY_BATTLE, .tracks = 4, .step_ms = 16, .check_every = 0, .data_check = 5, .seed = 9 };
        seed = 4242;
        EXPECT(acr_replay_rec_begin(&rec, file_b, CAP, &four, NULL, 0));
        acr_rb_pad cur[4] = { 0, 0, 0, 0 };
        for (int f = 0; f < N; ++f) {
            /* long quiet stretches, busy stretches, and every pad changing at once */
            const int busy = f / 500 % 3;
            for (int t = 0; t < 4; ++t)
                if (busy == 2 || (busy == 1 && rnd() % 10 == 0)) cur[t] = rnd() & 0x3FFFFu;
            check_at[f] = (uint8_t)(rnd() % 50 == 0);
            if (check_at[f]) acr_replay_rec_check(&rec, 0xABCD0000u + (uint32_t)f);
            memcpy(want[f], cur, sizeof cur);
            acr_replay_rec_step(&rec, cur);
        }
        const acr_replay_end e = { .hash = 0x12345678u, .result = 2, .rounds = 1, .wins = { 0, 1 } };
        const uint32_t n = acr_replay_rec_end(&rec, &e);
        EXPECT(n > 0 && n <= acr_replay_worst_size(4, N, 0, 0) + 5u * N / 40u);
        EXPECT(acr_replay_open(&rd, file_b, n) == ACR_REPLAY_OK);
        EXPECT(rd.complete && rd.steps == N && rd.info.tracks == 4 && rd.info.kind == ACR_REPLAY_BATTLE && rd.setup_len == 0);
        EXPECT(rd.end.hash == 0x12345678u && rd.end.result == 2 && rd.end.rounds == 1 && rd.end.wins[1] == 1);
        int f = 0, bad = 0;
        for (;;) {
            acr_rb_pad got[4];
            uint32_t h = 0;
            const int ev = acr_replay_next(&rd, got, &h);
            if (ev == ACR_REPLAY_EV_END) break;
            if (ev == ACR_REPLAY_EV_CHECK) { if (f >= N || !check_at[f] || h != 0xABCD0000u + (uint32_t)f) ++bad; check_at[f] = 0; continue; }
            if (f >= N || check_at[f] || memcmp(got, want[f], sizeof got) != 0) ++bad;
            ++f;
        }
        EXPECT(f == N && bad == 0);
        printf("four tracks: %d steps in %u bytes\n", N, (unsigned)n);
    }

    /* ---- Online: both machines' recordings, over good and bad links. ---- */
    const link lan_c = { 1, 0, 0 }, home_c = { 40, 10, 2 }, far_c = { 90, 30, 5 };
    seed = 777;
    online("online, LAN", 3000, 0, &lan_c, 0);
    online("online, 40 ms 2% loss", 3000, 0, &home_c, 0);
    online("online, 90 ms 5% loss, delay 2", 3000, 2, &far_c, 0);
    online("online, the other side drops", 3000, 1, &home_c, 1000);

    /* ---- Co-op: two players and two computer pilots, who fly inside the
     * match; only the players' pads are saved. ---- */
    start.count = 4;
    for (int i = 0; i < 4; ++i) start.f[i].team = (uint8_t)(i >= 2);
    acr_gen1_ac_spawn(&start.f[1].ac, &def, 1500 * 64, 0, 0);
    acr_gen1_ac_spawn(&start.f[2].ac, &def, 0, 2500 * 64, 2048);
    acr_gen1_ac_spawn(&start.f[3].ac, &def, 1500 * 64, 2500 * 64, 2048);
    rules.ai_mask = 0xC;
    rules.ai_seed = 99;
    acr_gen1_ai_preset(&rules.ai[2], ACR_GEN1_AI_AGGRESSOR);
    acr_gen1_ai_preset(&rules.ai[3], ACR_GEN1_AI_SNIPER);
    {
        const uint32_t n = record_plain(file_a, CAP, 3000, 555, &rec);
        const uint32_t h = acr_gen1_match_hash(&m);
        EXPECT(n > 0 && acr_replay_open(&rd, file_a, n) == ACR_REPLAY_OK && rd.info.tracks == 2);
        EXPECT(acr_replay_play_match(&rd, &m2, &start, &rules) == 0);
        EXPECT(acr_gen1_match_hash(&m2) == h);
        /* another AI seed is another fight: caught */
        rules.ai_seed = 100;
        EXPECT(acr_replay_play_match(&rd, &m2, &start, &rules) != 0);
        rules.ai_seed = 99;
        printf("co-op with 2 AI: 3000 steps in %u bytes\n", (unsigned)n);
    }
    online("online co-op, 40 ms 2% loss", 3000, 0, &home_c, 0);

    /* ---- Four players (2v2, as lockstep will run it): a pad per player. ---- */
    rules.ai_mask = 0;
    {
        const acr_replay_info four_p = { .kind = ACR_REPLAY_MATCH, .tracks = 4, .step_ms = 16,
                                         .check_every = ACR_REPLAY_CHECK_EVERY, .data_check = 0xC0FFEE01u, .seed = 77 };
        seed = 909;
        pilot pl4[4];
        memset(pl4, 0, sizeof pl4);
        EXPECT(acr_replay_rec_begin(&rec, file_a, CAP, &four_p, NULL, 0));
        acr_gen1_match_init(&m, &start, &rules);
        for (int f = 0; f < 3000; ++f) {
            acr_rb_pad pads[4];
            acr_gen1_input in[ACR_GEN1_MAX_FIGHTERS];
            acr_gen1_actions act[ACR_GEN1_MAX_FIGHTERS];
            for (int k = 0; k < 4; ++k) {
                pads[k] = next_pad(&pl4[k]);
                acr_rb_unpack(pads[k], &in[k], &act[k]);
            }
            acr_replay_rec_match(&rec, &m, pads);
            acr_gen1_match_step(&m, &start, &rules, in, act);
        }
        const uint32_t n = acr_replay_rec_end_match(&rec, &m);
        const uint32_t h = acr_gen1_match_hash(&m);
        EXPECT(n > 0 && acr_replay_open(&rd, file_a, n) == ACR_REPLAY_OK && rd.info.tracks == 4 && rd.steps == 3000);
        EXPECT(acr_replay_play_match(&rd, &m2, &start, &rules) == 0);
        EXPECT(acr_gen1_match_hash(&m2) == h);
        /* players 3 and 4 are in it: the same fight without their pads ends differently */
        acr_gen1_match_init(&m2, &start, &rules);
        acr_replay_rewind(&rd);
        for (;;) {
            acr_rb_pad pads[4];
            uint32_t hh;
            const int ev = acr_replay_next(&rd, pads, &hh);
            if (ev == ACR_REPLAY_EV_END) break;
            if (ev != ACR_REPLAY_EV_STEP) continue;
            acr_gen1_input in[ACR_GEN1_MAX_FIGHTERS];
            acr_gen1_actions act[ACR_GEN1_MAX_FIGHTERS];
            memset(in, 0, sizeof in);
            memset(act, 0, sizeof act);
            for (int k = 0; k < 2; ++k) acr_rb_unpack(pads[k], &in[k], &act[k]);
            acr_gen1_match_step(&m2, &start, &rules, in, act);
        }
        EXPECT(acr_gen1_match_hash(&m2) != h);
        printf("2v2, four players: 3000 steps in %u bytes\n", (unsigned)n);
    }

    printf(failures ? "%d FAILED\n" : "all replay tests passed\n", failures);
    return failures ? 1 : 0;
}
