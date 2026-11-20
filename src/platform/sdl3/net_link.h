/* PC network link for netplay: a UDP socket, the connect handshake and the
 * "same game data?" check. The match itself (rollback) is acr_rollback.
 *
 * Handshake (packets start "ACRH"):
 *   guest -> host  HELLO    data check, guest's parts, AC name
 *   host  -> guest WELCOME  data check, both players' parts, stage, input
 *                           delay, random seed, AC names, mode (VS, or
 *                           co-op against 1 or 2 AI ACs) and the AI's style
 *                  REFUSE   with a reason (other game data, version, full)
 *   guest -> host  READY    stage loaded and its data matches
 * Each side repeats its packet until it hears the next one, so lost packets
 * only cost time. The guest starts the match on WELCOME; the host on READY
 * (or the guest's first match packet).
 *
 * Testing: fake lag (each way), jitter and packet loss on what this side
 * sends, so two copies on one PC can feel a bad connection; and
 * ACR_NET_STRICT=1 makes an online host act like a strict router (nothing
 * gets in from an address it hasn't sent to), to test read-backs. */
#ifndef NET_LINK_H
#define NET_LINK_H

#include <stdint.h>

#define NET_PORT_DEFAULT 7777
#define NET_PARTS 11
#define NET_NAME 20

typedef struct net_addr { uint8_t ip[4]; uint16_t port; } net_addr;

enum { NET_CONNECTING, NET_STARTED, NET_FAILED };

typedef struct net_link net_link;

typedef struct net_setup {
    int host;                     /* 1 host (player 1), 0 join (player 2) */
    int port;                     /* host: port to listen on */
    const char *join;             /* guest: "address[:port]" */
    const char *import_dir;       /* for the data check */
    int parts[NET_PARTS];         /* this player's build */
    const char *ac_name;
    int stage;                    /* host: the stage (0 = the open test floor) */
    int delay;                    /* host: input delay in steps */
    int coop_ai;                  /* host: 0 VS (1v1); 1 or 2: co-op against that many AI ACs */
    int ai_preset;                /* host: the AI's style (ACR_GEN1_AI_*) */
    int lag_ms, jitter_ms, loss_pct;   /* testing: fake bad connection on what we send */
    int guest_port;               /* guest: bind this port (0: any; testing) */
    int online;                   /* over the internet with briefing codes: find our internet
                                     address (STUN); the guest waits longer for the host */
} net_setup;

typedef struct net_match_info {
    int parts[2][NET_PARTS];      /* player 1's (host) and player 2's builds */
    char name[2][NET_NAME + 1];
    int stage, delay;
    int coop_ai, ai_preset;       /* see net_setup */
    uint32_t seed;
    int local;                    /* our player: 0 or 1 */
} net_match_info;

/* Opens the socket. NULL with a message in err on failure. */
net_link *net_link_open(const net_setup *s, char *err, int err_size);
void net_link_close(net_link *l);     /* says goodbye to the other side first */

/* Call each frame while connecting: returns NET_CONNECTING, NET_STARTED
 * (info filled in) or NET_FAILED (message in net_link_error). `status` gets
 * a line to show while waiting. stage_check(stage) returns the data check
 * of that stage's files on this machine (0 if it's missing). */
int net_link_connect(net_link *l, uint64_t now_ms, net_match_info *info, char *status, int status_size);
const char *net_link_error(const net_link *l);

/* During the match: send a match packet / receive the next one (length, 0
 * when none). Packets from anyone but the other player are dropped. */
void net_link_send(net_link *l, const void *data, int len, uint64_t now_ms);
int  net_link_recv(net_link *l, void *buf, int cap, uint64_t now_ms);
/* Lets held (fake-lag) packets go when their time comes. */
void net_link_pump(net_link *l, uint64_t now_ms);
/* Starts the quiet clock again (after loading the match, say). */
void net_link_touch(net_link *l, uint64_t now_ms);
/* Milliseconds since anything came from the other player. */
uint64_t net_link_quiet_ms(const net_link *l, uint64_t now_ms);
/* Round trip in ms, from the handshake and match pings (0 = not known yet). */
int net_link_ping_ms(const net_link *l);

/* Over the internet (briefing codes, acr_brief.h). The link asks public
 * STUN servers (ACR_STUN="host:port" overrides them, for tests) which
 * internet address and port this link's socket shows to the world.
 * Returns 1 once known, -1 if no server answered, 0 while asking. */
int net_link_public(const net_link *l, net_addr *out);
/* Host: the joiner's address from their reply. The host sends to it a few
 * times a second until they're connected, so the host's router lets the
 * joiner's packets in (both sides "punch" a hole at once). */
void net_link_punch(net_link *l, const net_addr *to);

/* LAN discovery: a host waiting for player 2 announces itself once a
 * second on the local network (UDP broadcast to port NET_BEACON_PORT, and
 * to this PC itself); the finder lists the hosts it hears. */
#define NET_BEACON_PORT 7778
typedef struct net_found {
    char addr[64];            /* "192.168.1.20:7777", ready for --join */
    char name[NET_NAME + 1];  /* the host's AC */
    int stage;
    int coop_ai;              /* the host's mode: 0 VS, else co-op against that many AI */
    int same_data;            /* its game data matches ours */
    int this_pc;              /* the host is another copy on this PC */
    uint32_t id;
    uint64_t seen_ms;
} net_found;
typedef struct net_finder net_finder;
net_finder *net_finder_open(const char *import_dir);
void net_finder_close(net_finder *f);
/* Hosts heard in the last 3 seconds (at most max); returns the count. */
int net_finder_poll(net_finder *f, uint64_t now_ms, net_found *out, int max);

/* This PC's address on the local network ("192.168.1.20"), for the host to
 * tell the other player; "" if unknown. */
void net_local_address(char *out, int size);

/* The data check: a hash of the import's part and motion files (what both
 * machines' fights are built from), and of a stage's files. */
uint32_t net_data_check(const char *import_dir);
uint32_t net_stage_check(const char *import_dir, int stage);

#endif
