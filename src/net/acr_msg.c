#include "acr_msg.h"

#include <string.h>

enum { T_DATA = 1, T_ACK = 2 };

static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

void acr_msg_init(acr_msg *m) { memset(m, 0, sizeof *m); }

int acr_msg_send(acr_msg *m, const void *data, int len) {
    if (len < 1 || len > ACR_MSG_MAX) return 0;
    const int chunks = (len + ACR_MSG_CHUNK - 1) / ACR_MSG_CHUNK;
    if (m->out_next - m->out_acked + (uint32_t)chunks > ACR_MSG_SEND_RING) return 0;
    const uint8_t *p = (const uint8_t *)data;
    for (int i = 0; i < chunks; ++i) {
        const uint32_t n = m->out_next++;
        acr_msg_chunk *c = &m->out[n % ACR_MSG_SEND_RING];
        const int at = i * ACR_MSG_CHUNK, left = len - at;
        c->len = (uint8_t)(left > ACR_MSG_CHUNK ? ACR_MSG_CHUNK : left);
        c->last = (uint8_t)(i == chunks - 1);
        if (c->len) memcpy(c->data, p + at, c->len);
        m->sent_at[n % ACR_MSG_SEND_RING] = 0;
    }
    return 1;
}

/* ---- the inbox: a ring of [4-byte length][bytes] ---- */

static void inbox_put(acr_msg *m, const uint8_t *src, int n) {
    for (int i = 0; i < n; ++i) m->inbox[(m->inbox_head + m->inbox_len + i) % ACR_MSG_INBOX] = src[i];
    m->inbox_len += n;
}

static void inbox_get(acr_msg *m, uint8_t *dst, int n) {
    for (int i = 0; i < n; ++i) {
        if (dst) dst[i] = m->inbox[m->inbox_head];
        m->inbox_head = (m->inbox_head + 1) % ACR_MSG_INBOX;
    }
    m->inbox_len -= n;
}

int acr_msg_recv(acr_msg *m, void *buf, int cap) {
    if (m->inbox_len < 4) return 0;
    uint8_t h[4];
    inbox_get(m, h, 4);
    const int len = (int)get32(h);
    if (len > cap) { inbox_get(m, NULL, len); return -1; }
    inbox_get(m, (uint8_t *)buf, len);
    return len;
}

/* Hands over the chunks that are now in order; whole messages go to the inbox. */
static void deliver(acr_msg *m) {
    while (m->in_have[m->in_next % ACR_MSG_RECV_RING]) {
        const acr_msg_chunk *c = &m->in[m->in_next % ACR_MSG_RECV_RING];
        if (c->last && !m->part_bad && m->inbox_len + 4 + m->part_len + c->len > ACR_MSG_INBOX)
            return;                       /* the inbox is full: wait (the chunk comes again) */
        if (m->part_len + c->len > ACR_MSG_MAX) m->part_bad = 1;
        if (!m->part_bad) { memcpy(m->part + m->part_len, c->data, c->len); m->part_len += c->len; }
        if (c->last) {
            if (!m->part_bad) {
                uint8_t h[4];
                put32(h, (uint32_t)m->part_len);
                inbox_put(m, h, 4);
                inbox_put(m, m->part, m->part_len);
            } else {
                ++m->dropped;
            }
            m->part_len = 0;
            m->part_bad = 0;
        }
        m->in_have[m->in_next % ACR_MSG_RECV_RING] = 0;
        ++m->in_next;
    }
}

int acr_msg_read(acr_msg *m, const uint8_t *pkt, int len) {
    if (len < 9 || pkt[0] != 'A' || pkt[1] != 'C' || pkt[2] != 'R' || pkt[3] != 'M') return 0;
    const uint32_t ack = get32(pkt + 5);
    if (ack > m->out_acked && ack <= m->out_next) m->out_acked = ack;
    if (pkt[4] != T_DATA) return 1;
    if (len < 15) return 1;
    const uint32_t seq = get32(pkt + 9);
    const int last = pkt[13], n = pkt[14];
    if (n > ACR_MSG_CHUNK || 15 + n > len) return 1;
    m->need_ack = 1;
    if (seq < m->in_next || seq >= m->in_next + ACR_MSG_RECV_RING) return 1;   /* old, or too far ahead */
    acr_msg_chunk *c = &m->in[seq % ACR_MSG_RECV_RING];
    c->len = (uint8_t)n;
    c->last = (uint8_t)(last != 0);
    memcpy(c->data, pkt + 15, (size_t)n);
    m->in_have[seq % ACR_MSG_RECV_RING] = 1;
    deliver(m);
    return 1;
}

static int header(acr_msg *m, uint8_t *pkt, int type) {
    pkt[0] = 'A'; pkt[1] = 'C'; pkt[2] = 'R'; pkt[3] = 'M'; pkt[4] = (uint8_t)type;
    put32(pkt + 5, m->in_next);
    return 9;
}

int acr_msg_write(acr_msg *m, uint64_t now_ms, uint8_t *pkt, int cap) {
    if (cap < ACR_MSG_PACKET_MAX) return 0;
    deliver(m);                           /* room may have opened in the inbox */
    const uint64_t stamp = now_ms + 1;    /* 0 means "not sent yet" */
    uint32_t end = m->out_acked + ACR_MSG_WINDOW;
    if (end > m->out_next) end = m->out_next;
    for (uint32_t n = m->out_acked; n < end; ++n) {
        uint64_t *at = &m->sent_at[n % ACR_MSG_SEND_RING];
        if (*at && stamp - *at < ACR_MSG_RESEND_MS) continue;
        if (*at) ++m->resent;
        *at = stamp;
        const acr_msg_chunk *c = &m->out[n % ACR_MSG_SEND_RING];
        int k = header(m, pkt, T_DATA);
        put32(pkt + k, n); k += 4;
        pkt[k++] = c->last;
        pkt[k++] = c->len;
        memcpy(pkt + k, c->data, c->len);
        k += c->len;
        m->need_ack = 0;
        m->last_out_ms = now_ms;
        return k;
    }
    if (m->need_ack || now_ms - m->last_out_ms >= ACR_MSG_KEEPALIVE_MS) {
        m->need_ack = 0;
        m->last_out_ms = now_ms;
        return header(m, pkt, T_ACK);
    }
    return 0;
}

int acr_msg_idle(const acr_msg *m) { return m->out_acked == m->out_next; }
