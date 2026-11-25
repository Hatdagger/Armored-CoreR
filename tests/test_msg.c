/* Menu messages (acr_msg): over simulated networks (perfect; lag with
 * jitter, which reorders; heavy loss; repeats) every message arrives once,
 * whole and in order, both ways at once; big messages (a whole co-op run)
 * too; a full inbox waits instead of losing anything; an idle link still
 * sends a keep-alive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acr_msg.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static uint32_t seed = 4242;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

typedef struct { uint64_t due; int len; uint8_t b[ACR_MSG_PACKET_MAX]; } flight;
#define FLIGHT_MAX 20000
static flight wire[2][FLIGHT_MAX];   /* packets on their way to side k */
static int wire_n[2];

typedef struct { int lag, jitter, loss, dup; } net_kind;

static void put_wire(int to, const uint8_t *b, int len, uint64_t now, const net_kind *nk) {
    if ((int)(rnd() % 100) < nk->loss) return;
    for (int copies = (int)(rnd() % 100) < nk->dup ? 2 : 1; copies--;) {
        if (wire_n[to] >= FLIGHT_MAX) return;
        flight *f = &wire[to][wire_n[to]++];
        f->due = now + (uint64_t)nk->lag + (nk->jitter ? rnd() % (uint32_t)(nk->jitter + 1) : 0);
        f->len = len;
        memcpy(f->b, b, (size_t)len);
    }
}

/* Message i from side s: its length and bytes follow from (s, i). */
static int make(int s, int i, uint8_t *out) {
    uint32_t h = (uint32_t)(s * 7919 + i * 104729 + 1);
    const int len = i % 7 == 3 ? 30000 + (int)(h % 20000) : i % 5 == 0 ? 1 : 1 + (int)(h % 700);
    for (int k = 0; k < len; ++k) { h = h * 1664525u + 1013904223u; out[k] = (uint8_t)(h >> 24); }
    return len;
}

static int run(const char *name, net_kind nk, int count) {
    static acr_msg side[2];
    acr_msg_init(&side[0]);
    acr_msg_init(&side[1]);
    wire_n[0] = wire_n[1] = 0;
    int sent[2] = { 0, 0 }, got[2] = { 0, 0 }, bad = 0;
    static uint8_t want[ACR_MSG_MAX], buf[ACR_MSG_MAX], pkt[ACR_MSG_PACKET_MAX];
    uint64_t now = 1000;
    for (int step = 0; step < 200000 && (got[0] < count || got[1] < count); ++step, now += 5) {
        for (int s = 0; s < 2; ++s) {
            /* queue new messages a few at a time */
            while (sent[s] < count && sent[s] - got[1 - s] < 6) {
                const int len = make(s, sent[s], want);
                if (!acr_msg_send(&side[s], want, len)) break;
                ++sent[s];
            }
            int n;
            while ((n = acr_msg_write(&side[s], now, pkt, sizeof pkt)) > 0) put_wire(1 - s, pkt, n, now, &nk);
        }
        for (int s = 0; s < 2; ++s) {
            for (int i = 0; i < wire_n[s];) {
                if (wire[s][i].due <= now) {
                    EXPECT(acr_msg_read(&side[s], wire[s][i].b, wire[s][i].len) == 1);
                    wire[s][i] = wire[s][--wire_n[s]];   /* the order shuffles, as jitter does */
                } else ++i;
            }
            int len;
            while ((len = acr_msg_recv(&side[s], buf, sizeof buf)) != 0) {
                const int wl = make(1 - s, got[s], want);
                if (len != wl || memcmp(buf, want, (size_t)len)) ++bad;
                ++got[s];
            }
        }
    }
    printf("%-28s %4d + %4d messages, %d wrong, resent %u + %u, %.1f s simulated\n", name, got[0], got[1], bad,
           side[0].resent, side[1].resent, (double)(now - 1000) / 1000.0);
    EXPECT(got[0] == count && got[1] == count && !bad);
    EXPECT(acr_msg_idle(&side[0]) || got[1] == count);
    return failures;
}

int main(void) {
    net_kind lan = { 1, 0, 0, 0 }, wan = { 40, 30, 2, 0 }, bad = { 90, 60, 20, 5 }, awful = { 150, 100, 40, 10 };
    run("LAN", lan, 120);
    run("40 ms + jitter, 2% loss", wan, 120);
    run("90 ms, 20% loss, repeats", bad, 80);
    run("150 ms, 40% loss", awful, 30);

    /* Too long to send. */
    static acr_msg m;
    acr_msg_init(&m);
    static uint8_t big[ACR_MSG_MAX + 1];
    EXPECT(!acr_msg_send(&m, big, ACR_MSG_MAX + 1));
    EXPECT(!acr_msg_send(&m, big, 0));
    EXPECT(acr_msg_send(&m, big, ACR_MSG_MAX));
    EXPECT(!acr_msg_idle(&m));

    /* A full inbox: the sender waits, nothing is lost. */
    static acr_msg a, b;
    acr_msg_init(&a);
    acr_msg_init(&b);
    int queued = 0;
    for (int i = 0; i < 8; ++i) queued += acr_msg_send(&a, big, 30000);
    EXPECT(queued >= 3);
    uint8_t pkt[ACR_MSG_PACKET_MAX];
    uint64_t now = 10;
    for (int t = 0; t < 400; ++t, now += 50) {
        int n;
        while ((n = acr_msg_write(&a, now, pkt, sizeof pkt)) > 0) acr_msg_read(&b, pkt, n);
        while ((n = acr_msg_write(&b, now, pkt, sizeof pkt)) > 0) acr_msg_read(&a, pkt, n);
    }
    int arrived = 0;
    static uint8_t buf[ACR_MSG_MAX];
    for (int t = 0; t < 400; ++t, now += 50) {
        while (acr_msg_recv(&b, buf, sizeof buf) > 0) ++arrived;
        int n;
        while ((n = acr_msg_write(&a, now, pkt, sizeof pkt)) > 0) acr_msg_read(&b, pkt, n);
        while ((n = acr_msg_write(&b, now, pkt, sizeof pkt)) > 0) acr_msg_read(&a, pkt, n);
    }
    while (acr_msg_recv(&b, buf, sizeof buf) > 0) ++arrived;
    EXPECT(arrived == queued);
    EXPECT(acr_msg_idle(&a));

    /* Idle: a keep-alive once a second, nothing in between. */
    static acr_msg k;
    acr_msg_init(&k);
    int pkts = 0;
    for (uint64_t t = 5000; t < 10000; t += 10) {
        while (acr_msg_write(&k, t, pkt, sizeof pkt) > 0) ++pkts;
    }
    EXPECT(pkts >= 4 && pkts <= 6);
    /* A message too long for the reader's buffer is dropped, the next one comes. */
    static acr_msg x, y;
    acr_msg_init(&x);
    acr_msg_init(&y);
    acr_msg_send(&x, big, 500);
    acr_msg_send(&x, "hi", 2);
    int n;
    while ((n = acr_msg_write(&x, 100, pkt, sizeof pkt)) > 0) acr_msg_read(&y, pkt, n);
    char small[10];
    EXPECT(acr_msg_recv(&y, small, sizeof small) == -1);
    EXPECT(acr_msg_recv(&y, small, sizeof small) == 2 && !memcmp(small, "hi", 2));

    printf(failures ? "msg: %d FAILED\n" : "msg: all passed\n", failures);
    return failures ? 1 : 0;
}
