/* Menu messages over the network: whole messages (up to ACR_MSG_MAX bytes)
 * that arrive complete, once each and in the order they were sent, over a
 * link that loses, repeats and reorders packets (UDP). Rogue Arena co-op
 * uses it for everything outside fights (the run, choices, ready); fights
 * use rollback (acr_rollback.h).
 *
 * How: a message is cut into chunks of up to ACR_MSG_CHUNK bytes, each
 * with a running number. A chunk is sent again every ACR_MSG_RESEND_MS
 * until the other side says it has every chunk before some number (a
 * cumulative ack, which rides on every packet). At most ACR_MSG_WINDOW
 * chunks are in flight at once. When there is nothing to send, an ack goes
 * out once a second anyway, so the other side knows we're still here.
 *
 * Packets start "ACRM". No sockets or clocks in here: the caller moves
 * the packets and passes the time. */
#ifndef ACR_MSG_H
#define ACR_MSG_H

#include <stdint.h>

#define ACR_MSG_CHUNK      200
#define ACR_MSG_WINDOW     48         /* chunks in flight */
#define ACR_MSG_SEND_RING  512        /* chunks waiting or in flight (about 100 KB) */
#define ACR_MSG_RECV_RING  64         /* chunks that may arrive ahead of a missing one */
#define ACR_MSG_INBOX      (128 * 1024)
#define ACR_MSG_MAX        (64 * 1024)
#define ACR_MSG_RESEND_MS  250
#define ACR_MSG_KEEPALIVE_MS 1000
#define ACR_MSG_PACKET_MAX (4 + 1 + 4 + 4 + 1 + 1 + ACR_MSG_CHUNK)

typedef struct acr_msg_chunk {
    uint8_t len, last;               /* last: the message's final chunk */
    uint8_t data[ACR_MSG_CHUNK];
} acr_msg_chunk;

typedef struct acr_msg {
    /* sending */
    acr_msg_chunk out[ACR_MSG_SEND_RING];
    uint64_t sent_at[ACR_MSG_SEND_RING];   /* 0 = not sent yet */
    uint32_t out_next;               /* the next chunk number to hand out */
    uint32_t out_acked;              /* the other side has every chunk before this */
    uint32_t out_scan;               /* where acr_msg_write looks next */
    /* receiving */
    acr_msg_chunk in[ACR_MSG_RECV_RING];
    uint8_t  in_have[ACR_MSG_RECV_RING];
    uint32_t in_next;                /* the first chunk we haven't got */
    uint8_t  part[ACR_MSG_MAX];      /* the message being put together */
    int      part_len, part_bad;     /* part_bad: it was too long, dropped */
    uint8_t  inbox[ACR_MSG_INBOX];   /* whole messages: 4-byte length, then the bytes */
    int      inbox_head, inbox_len;
    int      need_ack;
    uint64_t last_out_ms;
    /* stats */
    uint32_t resent, dropped;
} acr_msg;

void acr_msg_init(acr_msg *m);

/* Queues a message (1 to ACR_MSG_MAX bytes). 1 = queued; 0 = empty, too
 * long or no room (try later). */
int acr_msg_send(acr_msg *m, const void *data, int len);
/* The next whole message: its length (copied into buf), 0 when none.
 * A message longer than cap is dropped and -1 returned. */
int acr_msg_recv(acr_msg *m, void *buf, int cap);

/* A packet from the other side: 1 when it was ours ("ACRM"), else 0. */
int acr_msg_read(acr_msg *m, const uint8_t *pkt, int len);
/* The next packet to send now (its length; 0 when nothing is due). Call
 * until it returns 0, sending each one. */
int acr_msg_write(acr_msg *m, uint64_t now_ms, uint8_t *pkt, int cap);

/* Everything queued has reached the other side. */
int acr_msg_idle(const acr_msg *m);

#endif
