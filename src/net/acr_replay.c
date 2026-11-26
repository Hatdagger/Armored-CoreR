#include "acr_replay.h"

#include <string.h>

/* The records, each one byte and what follows it (docs/REPLAYS.md):
 *   0x00-0x7F          that many + 1 steps with the pads as they stand
 *   0x80 + track, pad  the track's pad from the next step on (3 bytes)
 *   0xC0, hash         a check: the fight's hash after the steps so far
 *   0xFE, ...          the end: steps, hash, result, rounds, wins x 2,
 *                      then a hash of every record byte before it */
#define OP_RUN_MAX  0x7Fu
#define OP_PAD      0x80u
#define OP_CHECK    0xC0u
#define OP_END      0xFEu
#define PAD_BYTES   3u
#define PAD_MASK    0xFFFFFFu
#define END_BYTES   17u            /* the end record, whole */
#define END_SEALED  13u            /* its bytes that the last hash covers */
#define FNV_START   2166136261u
#define STEPS_MAX   0xFFFFFF00u    /* (more than two years at 60 a second) */

static uint32_t fnv(uint32_t h, const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void put16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t get16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }
static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

const char *acr_replay_error_text(int err) {
    switch (err) {
    case ACR_REPLAY_OK:           return "OK";
    case ACR_REPLAY_NOT_A_REPLAY: return "NOT A REPLAY FILE";
    case ACR_REPLAY_NEWER_FORMAT: return "MADE BY A NEWER VERSION OF THE GAME";
    case ACR_REPLAY_DAMAGED:      return "THE FILE IS DAMAGED";
    case ACR_REPLAY_OTHER_RULES:  return "MADE BY ANOTHER VERSION OF THE GAME";
    case ACR_REPLAY_OTHER_DATA:   return "MADE WITH DIFFERENT GAME DATA";
    default:                      return "UNKNOWN ERROR";
    }
}

/* ---------------- recording ---------------- */

/* Header, 32 bytes, little-endian:
 *   0  "ACRP"        4  format        5  kind          6  tracks
 *   7  pad bytes     8  rules (4)     12 data check (4)
 *   16 step ms (2)   18 check every (2)                20 seed (4)
 *   24 setup length (2)               26 0, 0
 *   28 hash of bytes 0-27 and the setup block (4)
 * then the setup block, then the records. */
int acr_replay_rec_begin(acr_replay_rec *rec, uint8_t *buf, uint32_t cap, const acr_replay_info *info,
                         const void *setup, uint32_t setup_len) {
    memset(rec, 0, sizeof *rec);
    rec->failed = 1;
    if (!buf || !info || info->kind > ACR_REPLAY_BATTLE || info->tracks < 1 || info->tracks > ACR_REPLAY_TRACKS) return 0;
    if (setup_len > ACR_REPLAY_SETUP_MAX || (setup_len && !setup) || cap < ACR_REPLAY_HEADER + setup_len) return 0;
    rec->buf = buf;
    rec->cap = cap;
    rec->info = *info;
    rec->info.rules = ACR_REPLAY_RULES;
    memset(buf, 0, ACR_REPLAY_HEADER);
    memcpy(buf, "ACRP", 4);
    buf[4] = ACR_REPLAY_FORMAT;
    buf[5] = info->kind;
    buf[6] = info->tracks;
    buf[7] = (uint8_t)PAD_BYTES;
    put32(buf + 8, ACR_REPLAY_RULES);
    put32(buf + 12, info->data_check);
    put16(buf + 16, info->step_ms);
    put16(buf + 18, info->check_every);
    put32(buf + 20, info->seed);
    put16(buf + 24, setup_len);
    if (setup_len) memcpy(buf + ACR_REPLAY_HEADER, setup, setup_len);
    put32(buf + 28, fnv(fnv(FNV_START, buf, 28), buf + ACR_REPLAY_HEADER, setup_len));
    rec->len = ACR_REPLAY_HEADER + setup_len;
    rec->stream_hash = FNV_START;
    rec->failed = 0;
    return 1;
}

/* Room for a whole record, or the replay has failed (a record is never
 * written in part). */
static uint8_t *room(acr_replay_rec *rec, uint32_t n) {
    if (rec->failed || rec->ended) return NULL;
    if (rec->cap - rec->len < n) { rec->failed = 1; return NULL; }
    uint8_t *p = rec->buf + rec->len;
    rec->len += n;
    return p;
}

static void write_run(acr_replay_rec *rec) {
    while (rec->run) {
        const uint32_t n = rec->run > OP_RUN_MAX + 1u ? OP_RUN_MAX + 1u : rec->run;
        uint8_t *o = room(rec, 1);
        if (!o) return;
        o[0] = (uint8_t)(n - 1u);
        rec->stream_hash = fnv(rec->stream_hash, o, 1);
        rec->run -= n;
    }
}

void acr_replay_rec_step(acr_replay_rec *rec, const acr_rb_pad pads[]) {
    if (rec->failed || rec->ended) return;
    if (rec->steps >= STEPS_MAX) { rec->failed = 1; return; }
    for (unsigned t = 0; t < rec->info.tracks; ++t) {
        const acr_rb_pad p = pads[t] & PAD_MASK;
        if (p == rec->pad[t]) continue;
        write_run(rec);
        uint8_t *o = room(rec, 1u + PAD_BYTES);
        if (!o) return;
        o[0] = (uint8_t)(OP_PAD + t);
        o[1] = (uint8_t)p; o[2] = (uint8_t)(p >> 8); o[3] = (uint8_t)(p >> 16);
        rec->stream_hash = fnv(rec->stream_hash, o, 1u + PAD_BYTES);
        rec->pad[t] = p;
    }
    ++rec->run;
    ++rec->steps;
}

int acr_replay_rec_wants_check(const acr_replay_rec *rec) {
    const uint32_t every = rec->info.check_every;
    return !rec->failed && !rec->ended && every && rec->steps && rec->steps % every == 0 &&
           rec->checked != rec->steps + 1u;
}

void acr_replay_rec_check(acr_replay_rec *rec, uint32_t hash) {
    write_run(rec);
    uint8_t *o = room(rec, 5);
    if (!o) return;
    o[0] = (uint8_t)OP_CHECK;
    put32(o + 1, hash);
    rec->stream_hash = fnv(rec->stream_hash, o, 5);
    rec->checked = rec->steps + 1u;
}

uint32_t acr_replay_rec_end(acr_replay_rec *rec, const acr_replay_end *end) {
    write_run(rec);
    uint8_t *o = room(rec, END_BYTES);
    if (!o) return 0;
    o[0] = (uint8_t)OP_END;
    put32(o + 1, rec->steps);
    put32(o + 5, end->hash);
    o[9] = end->result; o[10] = end->rounds; o[11] = end->wins[0]; o[12] = end->wins[1];
    rec->stream_hash = fnv(rec->stream_hash, o, END_SEALED);
    put32(o + END_SEALED, rec->stream_hash);
    rec->ended = 1;
    return rec->len;
}

void acr_replay_rec_drop(acr_replay_rec *rec) { rec->len = 0; }

uint32_t acr_replay_worst_size(int tracks, uint32_t steps, uint32_t check_every, uint32_t setup_len) {
    if (tracks < 1) tracks = 1;
    if (tracks > ACR_REPLAY_TRACKS) tracks = ACR_REPLAY_TRACKS;
    /* a step: its run byte and a pad record per track; plus the checks, the header and the end */
    const uint64_t per_step = 1u + (uint64_t)tracks * (1u + PAD_BYTES);
    uint64_t n = ACR_REPLAY_HEADER + (uint64_t)setup_len + (uint64_t)steps * per_step + END_BYTES;
    if (check_every) n += (uint64_t)(steps / check_every) * 5u;
    return n > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)n;
}

/* ---------------- recording a match ---------------- */

static void end_of_match(acr_replay_end *e, const acr_gen1_match *m) {
    memset(e, 0, sizeof *e);
    e->hash = acr_gen1_match_hash(m);
    e->result = m->round_over ? m->result : 0;
    e->rounds = m->round;
    e->wins[0] = m->wins[0];
    e->wins[1] = m->wins[1];
}

void acr_replay_rec_match(acr_replay_rec *rec, const acr_gen1_match *m, const acr_rb_pad pads[]) {
    if (rec->failed || rec->ended) return;
    if (acr_replay_rec_wants_check(rec)) acr_replay_rec_check(rec, acr_gen1_match_hash(m));
    acr_replay_rec_step(rec, pads);
}

uint32_t acr_replay_rec_end_match(acr_replay_rec *rec, const acr_gen1_match *m) {
    acr_replay_end e;
    end_of_match(&e, m);
    return acr_replay_rec_end(rec, &e);
}

/* ---------------- recording an online match ---------------- */

void acr_replay_rec_rollback(acr_replay_rec *rec, const acr_rb *rb) {
    if (rec->failed || rec->ended) return;
    /* Final: both players' pads are in (and the step has run here). */
    uint32_t final = rb->local_next < rb->remote_next ? rb->local_next : rb->remote_next;
    if (final > rb->frame) final = rb->frame;
    const uint32_t every = rec->info.check_every;
    while (rec->steps < final && !rec->failed) {
        const uint32_t f = rec->steps;
        /* The session hashes the match every ACR_RB_HASH_EVERY steps once
         * the pads before it are final: those are the replay's checks. */
        if (every == ACR_RB_HASH_EVERY && acr_replay_rec_wants_check(rec)) {
            if (rb->next_hash <= f) return;            /* not hashed yet: with the next tick */
            const uint32_t i = f / ACR_RB_HASH_EVERY % ACR_RB_HASHES;
            if (rb->hash_at[i] == f && rb->have_mine[i]) acr_replay_rec_check(rec, rb->hash_mine[i]);
            else rec->checked = f + 1u;                /* the session skipped this one: no check here */
        }
        const uint32_t s = f % ACR_RB_RING;
        if (rb->pad_for[0][s] != f + 1u || rb->pad_for[1][s] != f + 1u) { rec->failed = 1; return; }   /* called too rarely */
        const acr_rb_pad pads[ACR_REPLAY_TRACKS] = { rb->pad[0][s], rb->pad[1][s], 0, 0 };
        acr_replay_rec_step(rec, pads);
    }
}

uint32_t acr_replay_rec_end_rollback(acr_replay_rec *rec, acr_rb *rb) {
    acr_rb_settle(rb);
    acr_replay_rec_rollback(rec, rb);
    if (rec->failed || rec->ended) return 0;
    /* The match after the last recorded step: as shown, or the snapshot
     * taken before the first step that isn't final yet. */
    const uint32_t s = rec->steps;
    const acr_gen1_match *m = NULL;
    if (s == rb->frame) m = &rb->cur;
    else if (s < rb->frame && rb->frame - s < ACR_RB_RING) m = &rb->saved[s % ACR_RB_RING];
    if (!m || m->frame != s) { rec->failed = 1; return 0; }
    return acr_replay_rec_end_match(rec, m);
}

/* ---------------- playing back ---------------- */

/* Through the records from `pos` on, once: nothing unknown, the step count
 * and the last hash right. A record cut short by the end of the bytes is
 * where a replay that was never closed (or hasn't all arrived) stops. */
static int scan(const uint8_t *data, uint32_t len, uint32_t tracks, uint32_t first, uint32_t *pos_io,
                uint32_t *steps_io, acr_replay_end *end, int *complete) {
    uint32_t pos = *pos_io, steps = *steps_io;
    while (pos < len) {
        const uint32_t op = data[pos], left = len - pos;
        if (op <= OP_RUN_MAX) {
            if (steps > STEPS_MAX - (op + 1u)) return ACR_REPLAY_DAMAGED;
            steps += op + 1u;
            pos += 1u;
        } else if (op >= OP_PAD && op < OP_PAD + tracks) {
            if (left < 1u + PAD_BYTES) break;
            pos += 1u + PAD_BYTES;
        } else if (op == OP_CHECK) {
            if (left < 5u) break;
            pos += 5u;
        } else if (op == OP_END) {
            if (left < END_BYTES) break;
            if (left > END_BYTES) return ACR_REPLAY_DAMAGED;                  /* nothing comes after the end */
            if (get32(data + pos + 1) != steps) return ACR_REPLAY_DAMAGED;
            if (fnv(FNV_START, data + first, pos + END_SEALED - first) != get32(data + pos + END_SEALED))
                return ACR_REPLAY_DAMAGED;
            end->steps = steps;
            end->hash = get32(data + pos + 5);
            end->result = data[pos + 9]; end->rounds = data[pos + 10];
            end->wins[0] = data[pos + 11]; end->wins[1] = data[pos + 12];
            *complete = 1;
            break;
        } else {
            return ACR_REPLAY_DAMAGED;
        }
    }
    *pos_io = pos;
    *steps_io = steps;
    return ACR_REPLAY_OK;
}

int acr_replay_open(acr_replay_reader *rd, const uint8_t *data, uint32_t len) {
    memset(rd, 0, sizeof *rd);
    if (!data || len < ACR_REPLAY_HEADER || memcmp(data, "ACRP", 4) != 0) return ACR_REPLAY_NOT_A_REPLAY;
    if (data[4] > ACR_REPLAY_FORMAT) return ACR_REPLAY_NEWER_FORMAT;
    const uint32_t setup_len = get16(data + 24);
    if (data[4] == 0 || data[5] > ACR_REPLAY_BATTLE || data[6] < 1 || data[6] > ACR_REPLAY_TRACKS ||
        data[7] != PAD_BYTES || setup_len > ACR_REPLAY_SETUP_MAX || len - ACR_REPLAY_HEADER < setup_len)
        return ACR_REPLAY_DAMAGED;
    if (fnv(fnv(FNV_START, data, 28), data + ACR_REPLAY_HEADER, setup_len) != get32(data + 28)) return ACR_REPLAY_DAMAGED;

    const uint32_t first = ACR_REPLAY_HEADER + setup_len;
    uint32_t pos = first, steps = 0;
    acr_replay_end end;
    int complete = 0;
    memset(&end, 0, sizeof end);
    const int err = scan(data, len, data[6], first, &pos, &steps, &end, &complete);
    if (err != ACR_REPLAY_OK) return err;
    rd->data = data;
    rd->first = first;
    rd->last = pos;
    rd->info.kind = data[5];
    rd->info.tracks = data[6];
    rd->info.rules = get32(data + 8);
    rd->info.data_check = get32(data + 12);
    rd->info.step_ms = (uint16_t)get16(data + 16);
    rd->info.check_every = (uint16_t)get16(data + 18);
    rd->info.seed = get32(data + 20);
    rd->setup = data + ACR_REPLAY_HEADER;
    rd->setup_len = setup_len;
    rd->steps = steps;
    rd->complete = (uint8_t)complete;
    rd->end = end;
    acr_replay_rewind(rd);
    return ACR_REPLAY_OK;
}

int acr_replay_grow(acr_replay_reader *rd, const uint8_t *data, uint32_t len) {
    if (!rd->data || !data || len < rd->last) return ACR_REPLAY_DAMAGED;
    if (!rd->complete) {
        uint32_t pos = rd->last, steps = rd->steps;
        acr_replay_end end;
        int complete = 0;
        memset(&end, 0, sizeof end);
        const int err = scan(data, len, rd->info.tracks, rd->first, &pos, &steps, &end, &complete);
        if (err != ACR_REPLAY_OK) return err;
        rd->last = pos;
        rd->steps = steps;
        rd->complete = (uint8_t)complete;
        rd->end = end;
    }
    rd->data = data;
    rd->setup = data + ACR_REPLAY_HEADER;
    return ACR_REPLAY_OK;
}

int acr_replay_fits(const acr_replay_reader *rd, uint32_t data_check) {
    if (rd->info.rules != ACR_REPLAY_RULES) return ACR_REPLAY_OTHER_RULES;
    if (rd->info.data_check != data_check) return ACR_REPLAY_OTHER_DATA;
    return ACR_REPLAY_OK;
}

void acr_replay_rewind(acr_replay_reader *rd) {
    rd->pos = rd->first;
    rd->run = 0;
    rd->step = 0;
    rd->mismatch = 0;
    memset(rd->pad, 0, sizeof rd->pad);
}

int acr_replay_next(acr_replay_reader *rd, acr_rb_pad pads[], uint32_t *hash) {
    if (!rd->data) return ACR_REPLAY_EV_END;
    for (;;) {
        if (rd->run) {
            --rd->run;
            ++rd->step;
            for (unsigned t = 0; t < rd->info.tracks; ++t) pads[t] = rd->pad[t];
            return ACR_REPLAY_EV_STEP;
        }
        if (rd->pos >= rd->last) return ACR_REPLAY_EV_END;
        /* (acr_replay_open has been through these records: whole and known) */
        const uint32_t op = rd->data[rd->pos];
        if (op <= OP_RUN_MAX) {
            rd->run = op + 1u;
            rd->pos += 1u;
        } else if (op == OP_CHECK) {
            if (hash) *hash = get32(rd->data + rd->pos + 1);
            rd->pos += 5u;
            return ACR_REPLAY_EV_CHECK;
        } else if (op >= OP_PAD && op < OP_PAD + ACR_REPLAY_TRACKS) {
            const uint8_t *p = rd->data + rd->pos + 1;
            rd->pad[op - OP_PAD] = (acr_rb_pad)p[0] | (acr_rb_pad)p[1] << 8 | (acr_rb_pad)p[2] << 16;
            rd->pos += 1u + PAD_BYTES;
        } else {
            return ACR_REPLAY_EV_END;
        }
    }
}

int acr_replay_match_step(acr_replay_reader *rd, acr_gen1_match *m, const acr_gen1_battle *start,
                          const acr_gen1_match_rules *rules) {
    for (;;) {
        acr_rb_pad pads[ACR_REPLAY_TRACKS] = { 0 };
        uint32_t hash = 0;
        const int ev = acr_replay_next(rd, pads, &hash);
        if (ev == ACR_REPLAY_EV_CHECK) {
            if (!rd->mismatch && acr_gen1_match_hash(m) != hash) rd->mismatch = rd->step + 1u;
            continue;
        }
        if (ev == ACR_REPLAY_EV_STEP) {
            acr_gen1_input in[ACR_GEN1_MAX_FIGHTERS];
            acr_gen1_actions act[ACR_GEN1_MAX_FIGHTERS];
            memset(in, 0, sizeof in);
            memset(act, 0, sizeof act);
            for (unsigned t = 0; t < rd->info.tracks && t < ACR_GEN1_MAX_FIGHTERS; ++t) acr_rb_unpack(pads[t], &in[t], &act[t]);
            acr_gen1_match_step(m, start, rules, in, act);
            return 1;
        }
        if (rd->complete && !rd->mismatch && acr_gen1_match_hash(m) != rd->end.hash) rd->mismatch = rd->step + 1u;
        return 0;
    }
}

uint32_t acr_replay_play_match(acr_replay_reader *rd, acr_gen1_match *m, const acr_gen1_battle *start,
                               const acr_gen1_match_rules *rules) {
    acr_replay_rewind(rd);
    acr_gen1_match_init(m, start, rules);
    while (acr_replay_match_step(rd, m, start, rules)) {}
    return rd->mismatch;
}
