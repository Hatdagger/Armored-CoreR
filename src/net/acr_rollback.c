#include "acr_rollback.h"

#include <string.h>

#define NONE 0xFFFFFFFFu
#define EDGE_BITS ((1u << 9) | (1u << 12) | (1u << 13) | (1u << 14))

enum { PKT_INPUT = 1, PKT_BYE = 2 };
#define VERSION 1
#define HEADER 28

/* ---------------- pads ---------------- */

static unsigned tri(int v) { return v > 0 ? 2u : v < 0 ? 0u : 1u; }
static int8_t untri(unsigned v) { return (int8_t)(v == 2 ? 1 : v == 0 ? -1 : 0); }

acr_rb_pad acr_rb_pack(const acr_gen1_input *in, const acr_gen1_actions *act) {
    unsigned look = in->look >= 2 ? 3u : (unsigned)(in->look + 1);   /* -1, 0, 1, 2 -> 0..3 */
    unsigned p = tri(in->move) | tri(in->turn) << 2 | tri(in->strafe) << 4 | look << 6 |
                 (in->boost ? 1u : 0u) << 8 | (in->boost_tapped ? 1u : 0u) << 9 | (in->crouch ? 1u : 0u) << 10 |
                 (act->fire ? 1u : 0u) << 11 | (act->next_weapon ? 1u : 0u) << 12 | (act->blade ? 1u : 0u) << 13 |
                 (act->switch_target ? 1u : 0u) << 14;
    return (acr_rb_pad)p;
}

void acr_rb_unpack(acr_rb_pad p, acr_gen1_input *in, acr_gen1_actions *act) {
    memset(in, 0, sizeof *in);
    memset(act, 0, sizeof *act);
    in->move = untri(p & 3u);
    in->turn = untri(p >> 2 & 3u);
    in->strafe = untri(p >> 4 & 3u);
    in->look = (int8_t)((int)(p >> 6 & 3u) - 1);
    in->boost = (uint8_t)(p >> 8 & 1u);
    in->boost_tapped = (uint8_t)(p >> 9 & 1u);
    in->crouch = (uint8_t)(p >> 10 & 1u);
    act->fire = (uint8_t)(p >> 11 & 1u);
    act->next_weapon = (uint8_t)(p >> 12 & 1u);
    act->blade = (uint8_t)(p >> 13 & 1u);
    act->switch_target = (uint8_t)(p >> 14 & 1u);
}

acr_rb_pad acr_rb_held(acr_rb_pad p) { return (acr_rb_pad)(p & ~EDGE_BITS); }

/* ---------------- bytes ---------------- */

static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

/* ---------------- session ---------------- */

static int has_pad(const acr_rb *rb, int who, uint32_t f) { return rb->pad_for[who][f % ACR_RB_RING] == f + 1; }

static void set_pad(acr_rb *rb, int who, uint32_t f, acr_rb_pad p) {
    rb->pad[who][f % ACR_RB_RING] = p;
    rb->pad_for[who][f % ACR_RB_RING] = f + 1;
}

void acr_rb_init(acr_rb *rb, int local, int delay, const acr_gen1_battle *start, const acr_gen1_match_rules *rules) {
    memset(rb, 0, sizeof *rb);
    if (delay < 0) delay = 0;
    if (delay > ACR_RB_MAX_DELAY) delay = ACR_RB_MAX_DELAY;
    rb->local = local ? 1 : 0;
    rb->delay = delay;
    rb->start = start;
    rb->rules = *rules;
    acr_gen1_match_init(&rb->cur, start, rules);
    /* The delayed first steps have no controls on either side: nothing pressed. */
    for (uint32_t f = 0; f < (uint32_t)delay; ++f) { set_pad(rb, 0, f, 0); set_pad(rb, 1, f, 0); }
    rb->local_next = rb->remote_next = (uint32_t)delay;
    rb->rollback_from = NONE;
    rb->next_hash = ACR_RB_HASH_EVERY;
}

/* The remote pad for step f: theirs if it has come, else the guess. */
static acr_rb_pad remote_pad(const acr_rb *rb, uint32_t f) {
    const int them = 1 - rb->local;
    if (has_pad(rb, them, f)) return rb->pad[them][f % ACR_RB_RING];
    if (rb->remote_next == 0) return 0;
    return acr_rb_held(rb->pad[them][(rb->remote_next - 1) % ACR_RB_RING]);
}

static void run_step(acr_rb *rb, uint32_t f) {
    rb->saved[f % ACR_RB_RING] = rb->cur;
    const acr_rb_pad mine = rb->pad[rb->local][f % ACR_RB_RING];
    const acr_rb_pad theirs = remote_pad(rb, f);
    rb->used[f % ACR_RB_RING] = theirs;
    acr_gen1_input in[2];
    acr_gen1_actions act[2];
    acr_rb_unpack(mine, &in[rb->local], &act[rb->local]);
    acr_rb_unpack(theirs, &in[1 - rb->local], &act[1 - rb->local]);
    acr_gen1_match_step(&rb->cur, rb->start, &rb->rules, in, act);
}

/* Go back to the earliest wrong guess and run the steps again. */
static void resolve(acr_rb *rb) {
    if (rb->rollback_from == NONE) return;
    const uint32_t from = rb->rollback_from;
    rb->rollback_from = NONE;
    if (from >= rb->frame || rb->frame - from >= ACR_RB_RING) return;   /* cannot happen: the window is smaller */
    rb->cur = rb->saved[from % ACR_RB_RING];
    for (uint32_t f = from; f < rb->frame; ++f) run_step(rb, f);
    ++rb->rollbacks;
    rb->resimulated += rb->frame - from;
    if (rb->frame - from > rb->deepest) rb->deepest = rb->frame - from;
}

static void compare(acr_rb *rb, int i) {
    if (!rb->have_mine[i] || !rb->have_theirs[i] || rb->checked[i]) return;
    rb->checked[i] = 1;
    if (rb->hash_mine[i] == rb->hash_theirs[i]) ++rb->checks_ok;
    else if (!rb->desync_frame) rb->desync_frame = rb->hash_at[i] + 1;
}

/* A check slot for step c (a newer step takes the slot over; an older one
 * than the slot holds is ignored: returns -1). */
static int check_slot(acr_rb *rb, uint32_t c) {
    const int i = (int)(c / ACR_RB_HASH_EVERY % ACR_RB_HASHES);
    if (rb->hash_at[i] == c) return i;
    if (rb->hash_at[i] > c) return -1;
    rb->hash_at[i] = c;
    rb->have_mine[i] = rb->have_theirs[i] = rb->checked[i] = 0;
    return i;
}

/* Hash the match at each check step once both players' controls before it
 * are final. */
static void take_hashes(acr_rb *rb) {
    while (rb->next_hash <= rb->frame && rb->next_hash <= rb->remote_next) {
        const uint32_t c = rb->next_hash;
        if (rb->frame - c >= ACR_RB_RING) { rb->next_hash += ACR_RB_HASH_EVERY; continue; }
        const acr_gen1_match *m = c == rb->frame ? &rb->cur : &rb->saved[c % ACR_RB_RING];
        const uint32_t h = acr_gen1_match_hash(m);
        const int i = check_slot(rb, c);
        if (i >= 0) {
            rb->hash_mine[i] = h;
            rb->have_mine[i] = 1;
            compare(rb, i);
        }
        rb->out_hash_frame = c;
        rb->out_hash = h;
        rb->next_hash += ACR_RB_HASH_EVERY;
    }
}

int acr_rb_tick(acr_rb *rb, acr_rb_pad local_pad) {
    resolve(rb);
    take_hashes(rb);
    if (rb->frame >= rb->remote_next + ACR_RB_WINDOW) { ++rb->waits; return 0; }
    set_pad(rb, rb->local, rb->frame + (uint32_t)rb->delay, local_pad);
    rb->local_next = rb->frame + (uint32_t)rb->delay + 1;
    run_step(rb, rb->frame);
    ++rb->frame;
    take_hashes(rb);
    return 1;
}

void acr_rb_settle(acr_rb *rb) {
    resolve(rb);
    take_hashes(rb);
}

int acr_rb_should_wait(const acr_rb *rb) {
    const int32_t diff = (rb->local_adv256 - rb->remote_adv256) / 2;
    return diff >= 256 ? diff / 256 : 0;
}

/* ---------------- packets ---------------- */

static int header(uint8_t *b, int type) {
    b[0] = 'A'; b[1] = 'C'; b[2] = 'R'; b[3] = 'N';
    b[4] = VERSION;
    b[5] = (uint8_t)type;
    return 6;
}

int acr_rb_write_bye(uint8_t *buf, int cap) {
    if (cap < HEADER) return 0;
    memset(buf, 0, HEADER);
    header(buf, PKT_BYE);
    return HEADER;
}

int acr_rb_write(acr_rb *rb, uint8_t *buf, int cap) {
    uint32_t first = rb->remote_ack;
    if (first + ACR_RB_PADS_MAX < rb->local_next) first = rb->local_next - ACR_RB_PADS_MAX;
    const uint32_t count = rb->local_next > first ? rb->local_next - first : 0;
    if (cap < HEADER + (int)count * 2) return 0;
    header(buf, PKT_INPUT);
    put32(buf + 6, rb->frame);
    put32(buf + 10, rb->remote_next);
    int32_t adv = (int32_t)rb->frame - (int32_t)rb->remote_frame;
    if (adv > 127) adv = 127;
    if (adv < -127) adv = -127;
    buf[14] = (uint8_t)(int8_t)adv;
    put32(buf + 15, rb->out_hash_frame);
    put32(buf + 19, rb->out_hash);
    put32(buf + 23, first);
    buf[27] = (uint8_t)count;
    for (uint32_t k = 0; k < count; ++k) {
        const acr_rb_pad p = rb->pad[rb->local][(first + k) % ACR_RB_RING];
        buf[HEADER + k * 2] = (uint8_t)p;
        buf[HEADER + k * 2 + 1] = (uint8_t)(p >> 8);
    }
    return HEADER + (int)count * 2;
}

int acr_rb_read(acr_rb *rb, const uint8_t *buf, int len) {
    if (len < HEADER || buf[0] != 'A' || buf[1] != 'C' || buf[2] != 'R' || buf[3] != 'N' || buf[4] != VERSION) {
        ++rb->packets_bad;
        return 0;
    }
    if (buf[5] == PKT_BYE) return 2;
    if (buf[5] != PKT_INPUT) { ++rb->packets_bad; return 0; }
    const uint32_t count = buf[27];
    if (len < HEADER + (int)count * 2 || count > ACR_RB_PADS_MAX) { ++rb->packets_bad; return 0; }
    ++rb->packets_in;
    const uint32_t their_frame = get32(buf + 6), ack = get32(buf + 10);
    const int32_t adv = (int8_t)buf[14];
    if (their_frame > rb->remote_frame) rb->remote_frame = their_frame;
    if (ack > rb->remote_ack && ack <= rb->local_next) rb->remote_ack = ack;
    /* Frame advantage, averaged over about 8 packets. */
    rb->remote_adv256 += (adv * 256 - rb->remote_adv256) / 8;
    const int32_t local_adv = (int32_t)rb->frame - (int32_t)rb->remote_frame;
    rb->local_adv256 += (local_adv * 256 - rb->local_adv256) / 8;

    /* Their desync check. */
    const uint32_t hf = get32(buf + 15);
    if (hf) {
        const int i = check_slot(rb, hf);
        if (i >= 0 && !rb->have_theirs[i]) {
            rb->hash_theirs[i] = get32(buf + 19);
            rb->have_theirs[i] = 1;
            compare(rb, i);
        }
    }

    /* Their pads: keep the new ones; a pad for a step already run that
     * differs from the guess means going back to it. */
    const int them = 1 - rb->local;
    const uint32_t first = get32(buf + 23);
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t f = first + k;
        if (f < rb->remote_next || has_pad(rb, them, f)) continue;
        if (f >= rb->remote_next + ACR_RB_RING - 1) break;
        const acr_rb_pad p = (acr_rb_pad)(buf[HEADER + k * 2] | buf[HEADER + k * 2 + 1] << 8);
        set_pad(rb, them, f, p);
        if (f < rb->frame && rb->used[f % ACR_RB_RING] != p && (rb->rollback_from == NONE || f < rb->rollback_from))
            rb->rollback_from = f;
    }
    /* Later guesses change with the newest pad: go back to the first guessed
     * step whose guess is now different too. */
    const uint32_t old_next = rb->remote_next;
    while (has_pad(rb, them, rb->remote_next)) ++rb->remote_next;
    if (rb->remote_next != old_next) {
        const acr_rb_pad guess = acr_rb_held(rb->pad[them][(rb->remote_next - 1) % ACR_RB_RING]);
        for (uint32_t f = rb->remote_next; f < rb->frame; ++f)
            if (rb->used[f % ACR_RB_RING] != guess) {
                if (rb->rollback_from == NONE || f < rb->rollback_from) rb->rollback_from = f;
                break;
            }
    }
    return 1;
}
