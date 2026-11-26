#include "acr_lockstep.h"

#include <string.h>

#define PAD_BYTES 3
#define VERSION   1
enum { PKT_INPUT = 1, PKT_BYE = 2 };
/* Packet, little-endian:
 *   0  "ACRL"   4 version   5 type   6 sender (player)   7 players
 *   8  sender's step (4)    12 its latest hash's step (4)   16 that hash (4)
 *   20 how far ahead of us it thinks it is (1, signed)      21 sections
 *   22 its first missing step of each player (4 each)
 * then per section: player (1), first step (4), count (1), count pads. */
#define HEADER 22

static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

static int has_pad(const acr_ls *ls, int p, uint32_t f) { return ls->pad_for[p][f % ACR_LS_RING] == f + 1; }

static void set_pad(acr_ls *ls, int p, uint32_t f, acr_rb_pad v) {
    ls->pad[p][f % ACR_LS_RING] = v;
    ls->pad_for[p][f % ACR_LS_RING] = f + 1;
}

/* Do we exchange packets with machine q? (Guests only with the host.) */
static int linked(const acr_ls *ls, int q) {
    return q >= 0 && q < ls->players && q != ls->local && (ls->local == 0 || q == 0);
}

void acr_ls_init(acr_ls *ls, int players, int local, int delay, const acr_gen1_battle *start,
                 const acr_gen1_match_rules *rules) {
    memset(ls, 0, sizeof *ls);
    if (players < 2) players = 2;
    if (players > ACR_LS_PLAYERS) players = ACR_LS_PLAYERS;
    if (local < 0 || local >= players) local = 0;
    if (delay < 1) delay = 1;
    if (delay > ACR_LS_MAX_DELAY) delay = ACR_LS_MAX_DELAY;
    ls->players = players;
    ls->local = local;
    ls->delay = delay;
    ls->start = start;
    ls->rules = *rules;
    acr_gen1_match_init(&ls->cur, start, rules);
    /* The delayed first steps have no controls from anyone: nothing pressed. */
    for (int p = 0; p < players; ++p) {
        for (uint32_t f = 0; f < (uint32_t)delay; ++f) set_pad(ls, p, f, 0);
        ls->next[p] = (uint32_t)delay;
        for (int q = 0; q < players; ++q) ls->peer_next[q][p] = (uint32_t)delay;
    }
    ls->next_hash = ACR_LS_HASH_EVERY;
}

/* ---------------- desync checks ---------------- */

static void compare(acr_ls *ls, int q, int i) {
    if (!ls->have_mine[i] || !ls->have_theirs[q][i] || ls->checked[q][i]) return;
    ls->checked[q][i] = 1;
    if (ls->hash_mine[i] == ls->hash_theirs[q][i]) {
        ++ls->checks_ok;
    } else if (!ls->desync_frame) {
        ls->desync_frame = ls->hash_at[i] + 1;
        ls->desync_with = (uint8_t)q;
    }
}

/* A check slot for step c (a newer step takes the slot over; an older one
 * than the slot holds is ignored: returns -1). */
static int check_slot(acr_ls *ls, uint32_t c) {
    const int i = (int)(c / ACR_LS_HASH_EVERY % ACR_LS_HASHES);
    if (ls->hash_at[i] == c) return i;
    if (ls->hash_at[i] > c) return -1;
    ls->hash_at[i] = c;
    ls->have_mine[i] = 0;
    for (int q = 0; q < ACR_LS_PLAYERS; ++q) ls->have_theirs[q][i] = ls->checked[q][i] = 0;
    return i;
}

/* ---------------- steps ---------------- */

int acr_ls_wants_pad(const acr_ls *ls) { return ls->next[ls->local] <= ls->frame + (uint32_t)ls->delay; }

int acr_ls_tick(acr_ls *ls, acr_rb_pad local_pad) {
    /* Our controls now are the ones for `delay` steps on: taken once per
     * step, when we first get to it, and sent out while we wait. */
    const uint32_t mine = ls->frame + (uint32_t)ls->delay;
    if (ls->next[ls->local] <= mine) {
        set_pad(ls, ls->local, mine, local_pad);
        ls->next[ls->local] = mine + 1;
    }
    ls->waiting_for = 0;
    for (int p = 0; p < ls->players; ++p)
        if (!has_pad(ls, p, ls->frame)) ls->waiting_for |= (uint8_t)(1u << p);
    if (ls->waiting_for) { ++ls->waits; return 0; }

    acr_gen1_input in[ACR_GEN1_MAX_FIGHTERS];
    acr_gen1_actions act[ACR_GEN1_MAX_FIGHTERS];
    memset(in, 0, sizeof in);
    memset(act, 0, sizeof act);
    for (int p = 0; p < ls->players; ++p) acr_rb_unpack(ls->pad[p][ls->frame % ACR_LS_RING], &in[p], &act[p]);
    acr_gen1_match_step(&ls->cur, ls->start, &ls->rules, in, act);
    ++ls->frame;

    if (ls->frame == ls->next_hash) {
        const uint32_t h = acr_gen1_match_hash(&ls->cur);
        const int i = check_slot(ls, ls->frame);
        if (i >= 0) {
            ls->hash_mine[i] = h;
            ls->have_mine[i] = 1;
            for (int q = 0; q < ls->players; ++q) compare(ls, q, i);
        }
        ls->out_hash_frame = ls->frame;
        ls->out_hash = h;
        ls->next_hash += ACR_LS_HASH_EVERY;
    }
    return 1;
}

int acr_ls_should_wait(const acr_ls *ls) {
    int32_t most = 0;
    for (int q = 0; q < ls->players; ++q) {
        if (!linked(ls, q) || ls->gone[q]) continue;
        const int32_t diff = (ls->local_adv256[q] - ls->remote_adv256[q]) / 2;
        if (diff > most) most = diff;
    }
    return most >= 256 ? most / 256 : 0;
}

int acr_ls_pads(const acr_ls *ls, uint32_t f, acr_rb_pad pads[]) {
    for (int p = 0; p < ls->players; ++p) {
        if (!has_pad(ls, p, f)) return 0;
        pads[p] = ls->pad[p][f % ACR_LS_RING];
    }
    return 1;
}

/* ---------------- packets ---------------- */

static void header(const acr_ls *ls, uint8_t *b, int type) {
    memset(b, 0, HEADER);
    b[0] = 'A'; b[1] = 'C'; b[2] = 'R'; b[3] = 'L';
    b[4] = VERSION;
    b[5] = (uint8_t)type;
    b[6] = (uint8_t)ls->local;
    b[7] = (uint8_t)ls->players;
}

int acr_ls_write_bye(const acr_ls *ls, uint8_t *buf, int cap) {
    if (cap < HEADER) return 0;
    header(ls, buf, PKT_BYE);
    return HEADER;
}

int acr_ls_write(acr_ls *ls, int to, uint8_t *buf, int cap) {
    if (!linked(ls, to) || cap < ACR_LS_PACKET_MAX) return 0;
    header(ls, buf, PKT_INPUT);
    put32(buf + 8, ls->frame);
    put32(buf + 12, ls->out_hash_frame);
    put32(buf + 16, ls->out_hash);
    int32_t adv = (int32_t)ls->frame - (int32_t)ls->peer_frame[to];
    if (adv > 127) adv = 127;
    if (adv < -127) adv = -127;
    buf[20] = (uint8_t)(int8_t)adv;
    int n = HEADER, sections = 0;
    for (int p = 0; p < ls->players; ++p, n += 4) put32(buf + n, ls->next[p]);
    /* A guest sends its own pads; the host sends everyone's but the
     * receiver's own. Oldest first, from what the receiver said it lacks. */
    for (int p = 0; p < ls->players; ++p) {
        if (p == to || (ls->local != 0 && p != ls->local)) continue;
        const uint32_t first = ls->peer_next[to][p];
        if (first >= ls->next[p]) continue;
        uint32_t count = ls->next[p] - first;
        if (count > ACR_LS_PADS_MAX) count = ACR_LS_PADS_MAX;
        uint32_t held = 0;
        while (held < count && has_pad(ls, p, first + held)) ++held;
        if (!held) continue;
        buf[n++] = (uint8_t)p;
        put32(buf + n, first); n += 4;
        buf[n++] = (uint8_t)held;
        for (uint32_t k = 0; k < held; ++k) {
            const acr_rb_pad v = ls->pad[p][(first + k) % ACR_LS_RING];
            buf[n++] = (uint8_t)v; buf[n++] = (uint8_t)(v >> 8); buf[n++] = (uint8_t)(v >> 16);
        }
        ++sections;
    }
    buf[21] = (uint8_t)sections;
    return n;
}

int acr_ls_read(acr_ls *ls, const uint8_t *buf, int len) {
    if (len < HEADER || buf[0] != 'A' || buf[1] != 'C' || buf[2] != 'R' || buf[3] != 'L' || buf[4] != VERSION ||
        buf[7] != ls->players || !linked(ls, buf[6])) {
        ++ls->packets_bad;
        return 0;
    }
    const int q = buf[6];
    if (buf[5] == PKT_BYE) { ls->gone[q] = 1; return 2; }
    if (buf[5] != PKT_INPUT || len < HEADER + ls->players * 4) { ++ls->packets_bad; return 0; }
    /* The sections must fit before anything is taken from the packet. */
    {
        int n = HEADER + ls->players * 4;
        for (int s = 0; s < buf[21]; ++s) {
            if (len - n < 6 || buf[n] >= ls->players || buf[n + 5] > ACR_LS_PADS_MAX || len - n - 6 < buf[n + 5] * PAD_BYTES) {
                ++ls->packets_bad;
                return 0;
            }
            n += 6 + buf[n + 5] * PAD_BYTES;
        }
    }
    ++ls->packets_in;

    const uint32_t their_frame = get32(buf + 8);
    const int32_t adv = (int8_t)buf[20];
    if (their_frame > ls->peer_frame[q]) ls->peer_frame[q] = their_frame;
    /* Frame advantage, averaged over about 8 packets. */
    ls->remote_adv256[q] += (adv * 256 - ls->remote_adv256[q]) / 8;
    const int32_t local_adv = (int32_t)ls->frame - (int32_t)ls->peer_frame[q];
    ls->local_adv256[q] += (local_adv * 256 - ls->local_adv256[q]) / 8;

    /* Their desync check (it may be for a step we haven't reached yet). */
    const uint32_t hf = get32(buf + 12);
    if (hf) {
        const int i = check_slot(ls, hf);
        if (i >= 0 && !ls->have_theirs[q][i]) {
            ls->hash_theirs[q][i] = get32(buf + 16);
            ls->have_theirs[q][i] = 1;
            compare(ls, q, i);
        }
    }

    /* What they have (never more than exists). */
    int n = HEADER;
    for (int p = 0; p < ls->players; ++p, n += 4) {
        const uint32_t v = get32(buf + n);
        if (v > ls->peer_next[q][p] && v <= ls->next[p] + ACR_LS_RING) ls->peer_next[q][p] = v;
    }
    /* Their pads: a guest's own, or from the host anyone's but ours. */
    for (int s = 0; s < buf[21]; ++s) {
        const int p = buf[n];
        const uint32_t first = get32(buf + n + 1), count = buf[n + 5];
        const uint8_t *d = buf + n + 6;
        n += 6 + (int)count * PAD_BYTES;
        if (p == ls->local || (q != 0 && p != q)) continue;
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t f = first + k;
            if (f < ls->next[p] || has_pad(ls, p, f)) continue;
            if (f >= ls->frame + ACR_LS_AHEAD) break;
            set_pad(ls, p, f, (acr_rb_pad)d[k * PAD_BYTES] | (acr_rb_pad)d[k * PAD_BYTES + 1] << 8 |
                                  (acr_rb_pad)d[k * PAD_BYTES + 2] << 16);
        }
        while (has_pad(ls, p, ls->next[p])) ++ls->next[p];
    }
    return 1;
}
