#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L   /* getaddrinfo under strict C11 */
#endif
#include "net_link.h"

#include <SDL3/SDL.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET sock_t;
#  define BAD_SOCK INVALID_SOCKET
#  define close_sock closesocket
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
typedef int sock_t;
#  define BAD_SOCK (-1)
#  define close_sock close
#endif

#define VERSION 2   /* 2: co-op (mode and AI in WELCOME and the beacon) */
enum { H_HELLO = 1, H_WELCOME = 2, H_REFUSE = 3, H_READY = 4, H_PING = 5, H_PONG = 6, H_PUNCH = 7 };
enum { R_DATA = 1, R_VERSION = 2, R_BUSY = 3, R_STAGE = 4 };

#define HELD_MAX 512
#define PKT_MAX  256

typedef struct held { uint64_t due; int len; uint8_t data[PKT_MAX]; } held;

struct net_link {
    sock_t s;
    int host;
    net_addr peer;
    int have_peer;
    int state;
    char error[400];
    const char *import_dir;
    uint32_t data_hash;
    int parts[NET_PARTS];
    char name[NET_NAME + 1];
    int stage, delay, port_shown, coop_ai, ai_preset;
    char join[256];
    uint64_t start_ms, last_send_ms, last_rx_ms, last_ping_ms, last_beacon_ms;
    int got_match;               /* guest: the host's match packets have started */
    int ping_ms;
    net_match_info info;
    int refused_once;            /* host: a player with other data tried */
    /* fake bad connection */
    int lag_ms, jitter_ms, loss_pct;
    uint32_t rng;
    held held[HELD_MAX];
    int held_n;
    /* over the internet */
    int online;
    net_addr stun[4];
    int stun_n, stun_state;      /* stun_state: 0 asking, 1 answered, -1 nobody answered */
    uint8_t stun_tid[12];
    uint64_t stun_last_ms, stun_start_ms;
    net_addr public_addr;
    net_addr punch;
    int have_punch;
    uint64_t last_punch_ms;
    int strict;                  /* testing: act like a strict router (ACR_NET_STRICT) */
};

/* ---------------- sockets ---------------- */

static int sockets_up(void) {
#ifdef _WIN32
    static int done;
    if (!done) {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return 0;
        done = 1;
    }
#endif
    return 1;
}

static void to_sa(const net_addr *a, struct sockaddr_in *sa) {
    memset(sa, 0, sizeof *sa);
    sa->sin_family = AF_INET;
    sa->sin_port = htons(a->port);
    memcpy(&sa->sin_addr, a->ip, 4);
}

static void from_sa(const struct sockaddr_in *sa, net_addr *a) {
    memcpy(a->ip, &sa->sin_addr, 4);
    a->port = ntohs(sa->sin_port);
}

static int same_addr(const net_addr *a, const net_addr *b) { return !memcmp(a->ip, b->ip, 4) && a->port == b->port; }

/* "host[:port]" -> address (IPv4). */
static int resolve(const char *text, int default_port, net_addr *out) {
    char host[256];
    SDL_strlcpy(host, text, sizeof host);
    int port = default_port;
    char *colon = SDL_strrchr(host, ':');
    if (colon) { *colon = 0; port = SDL_atoi(colon + 1); }
    if (port <= 0 || port > 65535 || !host[0]) return 0;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;
    from_sa((const struct sockaddr_in *)res->ai_addr, out);
    out->port = (uint16_t)port;
    freeaddrinfo(res);
    return 1;
}

static void raw_send(net_link *l, const net_addr *to, const void *data, int len) {
    struct sockaddr_in sa;
    to_sa(to, &sa);
    sendto(l->s, (const char *)data, len, 0, (const struct sockaddr *)&sa, sizeof sa);
}

static uint32_t rnd(net_link *l) { l->rng = l->rng * 1103515245u + 12345u; return l->rng >> 8; }

/* Sends now, or holds it for the fake lag. */
static void send_to(net_link *l, const net_addr *to, const void *data, int len, uint64_t now_ms) {
    if (len <= 0 || len > PKT_MAX) return;
    if (l->loss_pct && (int)(rnd(l) % 100) < l->loss_pct) return;
    if (!l->lag_ms && !l->jitter_ms) { raw_send(l, to, data, len); return; }
    if (l->held_n >= HELD_MAX) return;
    held *h = &l->held[l->held_n++];
    h->due = now_ms + (uint64_t)l->lag_ms + (l->jitter_ms ? rnd(l) % (uint32_t)(l->jitter_ms + 1) : 0);
    h->len = len;
    memcpy(h->data, data, (size_t)len);
    (void)to;   /* held packets only ever go to the other player */
}

void net_link_pump(net_link *l, uint64_t now_ms) {
    for (int i = 0; i < l->held_n;) {
        if (l->held[i].due <= now_ms && l->have_peer) {
            raw_send(l, &l->peer, l->held[i].data, l->held[i].len);
            l->held[i] = l->held[--l->held_n];     /* jitter can reorder, as on a real network */
        } else {
            ++i;
        }
    }
}

static int raw_recv(net_link *l, net_addr *from, uint8_t *buf, int cap) {
    struct sockaddr_in sa;
    socklen_t n = sizeof sa;
    const int len = (int)recvfrom(l->s, (char *)buf, cap, 0, (struct sockaddr *)&sa, &n);
    if (len <= 0) return 0;
    from_sa(&sa, from);
    return len;
}

static int hs_header(uint8_t *b, int type);

/* ---------------- STUN (RFC 5389): our address as the internet sees it ---------------- */

#define STUN_COOKIE 0x2112A442u

static void stun_servers(net_link *l) {
    const char *over = SDL_getenv("ACR_STUN");
    static const char *const k_servers[] = { "stun.l.google.com:19302", "stun.cloudflare.com:3478",
                                             "stun1.l.google.com:19302", "stun.nextcloud.com:443" };
    if (over && over[0]) {
        if (resolve(over, 3478, &l->stun[0])) l->stun_n = 1;
        return;
    }
    for (int i = 0; i < 4 && l->stun_n < 4; ++i)
        if (resolve(k_servers[i], 3478, &l->stun[l->stun_n])) ++l->stun_n;
}

static void stun_ask(net_link *l) {
    uint8_t b[20] = { 0x00, 0x01, 0, 0, 0x21, 0x12, 0xA4, 0x42 };   /* binding request, no attributes */
    memcpy(b + 8, l->stun_tid, 12);
    for (int i = 0; i < l->stun_n; ++i) raw_send(l, &l->stun[i], b, sizeof b);
}

static int is_stun(const uint8_t *b, int len) {
    return len >= 20 && b[4] == 0x21 && b[5] == 0x12 && b[6] == 0xA4 && b[7] == 0x42 && (b[0] & 0xC0) == 0;
}

/* A binding answer: the (XOR-)MAPPED-ADDRESS in it. */
static void stun_read(net_link *l, const uint8_t *b, int len) {
    if (l->stun_state == 1 || b[0] != 0x01 || b[1] != 0x01 || memcmp(b + 8, l->stun_tid, 12) != 0) return;
    int at = 20;
    const int end = 20 + (b[2] << 8 | b[3]);
    while (at + 4 <= len && at + 4 <= end) {
        const int type = b[at] << 8 | b[at + 1], alen = b[at + 2] << 8 | b[at + 3];
        const uint8_t *v = b + at + 4;
        if (at + 4 + alen > len) break;
        if ((type == 0x0020 || type == 0x0001) && alen >= 8 && v[1] == 0x01) {
            uint16_t port = (uint16_t)(v[2] << 8 | v[3]);
            uint8_t ip[4] = { v[4], v[5], v[6], v[7] };
            if (type == 0x0020) {
                port ^= (uint16_t)(STUN_COOKIE >> 16);
                for (int i = 0; i < 4; ++i) ip[i] ^= (uint8_t)(STUN_COOKIE >> (24 - 8 * i));
            }
            memcpy(l->public_addr.ip, ip, 4);
            l->public_addr.port = port;
            l->stun_state = 1;
            if (type == 0x0020) return;      /* the XOR form is the reliable one */
        }
        at += 4 + ((alen + 3) & ~3);
    }
}

int net_link_public(const net_link *l, net_addr *out) {
    if (l->stun_state == 1) *out = l->public_addr;
    return l->stun_state;
}

void net_link_punch(net_link *l, const net_addr *to) {
    l->punch = *to;
    l->have_punch = 1;
    l->last_punch_ms = 0;
}

/* Online upkeep while connecting: ask STUN until it answers, and punch. */
static void online_upkeep(net_link *l, uint64_t now_ms) {
    if (!l->online) return;
    if (l->stun_state == 0) {
        if (!l->stun_start_ms) l->stun_start_ms = now_ms;
        if (!l->stun_n || now_ms - l->stun_start_ms > 10000) {
            l->stun_state = -1;
        } else if (now_ms - l->stun_last_ms >= 500) {
            stun_ask(l);
            l->stun_last_ms = now_ms;
        }
    }
    if (l->host && l->have_punch && !l->have_peer && now_ms - l->last_punch_ms >= 200) {
        uint8_t b[16];
        const int n = hs_header(b, H_PUNCH);
        raw_send(l, &l->punch, b, n);
        l->last_punch_ms = now_ms;
    }
}

net_link *net_link_open(const net_setup *s, char *err, int err_size) {
    if (!sockets_up()) { SDL_snprintf(err, (size_t)err_size, "The network could not be started."); return NULL; }
    net_link *l = (net_link *)SDL_calloc(1, sizeof *l);
    if (!l) return NULL;
    l->host = s->host;
    l->import_dir = s->import_dir;
    for (int i = 0; i < NET_PARTS; ++i) l->parts[i] = s->parts[i];
    SDL_strlcpy(l->name, s->ac_name ? s->ac_name : "", sizeof l->name);
    l->stage = s->stage;
    l->delay = s->delay;
    l->coop_ai = s->coop_ai < 0 ? 0 : s->coop_ai > 2 ? 2 : s->coop_ai;
    l->ai_preset = s->ai_preset;
    l->port_shown = s->port;
    l->lag_ms = s->lag_ms; l->jitter_ms = s->jitter_ms; l->loss_pct = s->loss_pct;
    l->rng = (uint32_t)SDL_GetTicksNS() | 1u;
    l->data_hash = net_data_check(s->import_dir);
    l->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (l->s == BAD_SOCK) { SDL_snprintf(err, (size_t)err_size, "Could not open a network socket."); SDL_free(l); return NULL; }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(l->s, FIONBIO, &nb);
    /* A "port unreachable" from the other side would otherwise break the socket. */
    BOOL no = FALSE;
    DWORD got = 0;
    WSAIoctl(l->s, _WSAIOW(IOC_VENDOR, 12), &no, sizeof no, NULL, 0, &got, NULL, NULL);
#else
    fcntl(l->s, F_SETFL, fcntl(l->s, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (s->host) {
        const int yes = 1;
        setsockopt(l->s, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof yes);   /* LAN beacons */
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((uint16_t)(s->host ? s->port : s->guest_port));
    if (bind(l->s, (const struct sockaddr *)&sa, sizeof sa) != 0) {
        SDL_snprintf(err, (size_t)err_size, "Could not use port %d (is another copy already hosting on it?).", s->port);
        close_sock(l->s);
        SDL_free(l);
        return NULL;
    }
    if (!s->host) {
        SDL_strlcpy(l->join, s->join ? s->join : "", sizeof l->join);
        if (!resolve(l->join, NET_PORT_DEFAULT, &l->peer)) {
            SDL_snprintf(err, (size_t)err_size, "Can't understand the address \"%s\". Use the host's address, like 192.168.1.20 or 192.168.1.20:7777.", l->join);
            close_sock(l->s);
            SDL_free(l);
            return NULL;
        }
        l->have_peer = 1;
    }
    l->online = s->online;
    if (l->online) {
        for (int i = 0; i < 12; ++i) l->stun_tid[i] = (uint8_t)rnd(l);
        stun_servers(l);
        l->strict = SDL_getenv("ACR_NET_STRICT") != NULL;
    }
    l->state = NET_CONNECTING;
    return l;
}

void net_link_close(net_link *l) {
    if (!l) return;
    if (l->have_peer) {
        /* The match packet "goodbye" (acr_rollback), sent a few times. */
        uint8_t bye[28] = { 'A', 'C', 'R', 'N', 1, 2 };
        for (int i = 0; i < 3; ++i) raw_send(l, &l->peer, bye, sizeof bye);
    }
    close_sock(l->s);
    SDL_free(l);
}

const char *net_link_error(const net_link *l) { return l->error; }

/* ---------------- handshake ---------------- */

static void put16(uint8_t *b, int v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static int get16(const uint8_t *b) { return b[0] | b[1] << 8; }
static void put32(uint8_t *b, uint32_t v) { put16(b, (int)(v & 0xFFFF)); put16(b + 2, (int)(v >> 16)); }
static uint32_t get32(const uint8_t *b) { return (uint32_t)get16(b) | (uint32_t)get16(b + 2) << 16; }

static int hs_header(uint8_t *b, int type) {
    b[0] = 'A'; b[1] = 'C'; b[2] = 'R'; b[3] = 'H'; b[4] = VERSION; b[5] = (uint8_t)type;
    return 6;
}
static int is_hs(const uint8_t *b, int len) { return len >= 6 && b[0] == 'A' && b[1] == 'C' && b[2] == 'R' && b[3] == 'H'; }
static int is_match(const uint8_t *b, int len) { return len >= 6 && b[0] == 'A' && b[1] == 'C' && b[2] == 'R' && b[3] == 'N'; }

static void send_simple(net_link *l, const net_addr *to, int type, int arg, uint64_t now_ms) {
    uint8_t b[16];
    int n = hs_header(b, type);
    b[n++] = (uint8_t)arg;
    send_to(l, to, b, n, now_ms);
    if (type == H_REFUSE) raw_send(l, to, b, n);   /* refusals go at once (the asker may not be the peer) */
}

static void send_hello(net_link *l, uint64_t now_ms) {
    uint8_t b[64];
    int n = hs_header(b, H_HELLO);
    put32(b + n, l->data_hash); n += 4;
    for (int i = 0; i < NET_PARTS; ++i) { put16(b + n, l->parts[i]); n += 2; }
    memcpy(b + n, l->name, NET_NAME); n += NET_NAME;
    send_to(l, &l->peer, b, n, now_ms);
}

static void send_welcome(net_link *l, uint64_t now_ms) {
    uint8_t b[128];
    int n = hs_header(b, H_WELCOME);
    put32(b + n, l->data_hash); n += 4;
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < NET_PARTS; ++i) { put16(b + n, l->info.parts[p][i]); n += 2; }
    b[n++] = (uint8_t)l->info.stage;
    b[n++] = (uint8_t)l->info.delay;
    put32(b + n, l->info.seed); n += 4;
    put32(b + n, l->info.stage ? net_stage_check(l->import_dir, l->info.stage) : 0); n += 4;
    for (int p = 0; p < 2; ++p) { memcpy(b + n, l->info.name[p], NET_NAME); n += NET_NAME; }
    b[n++] = (uint8_t)l->info.coop_ai;
    b[n++] = (uint8_t)l->info.ai_preset;
    send_to(l, &l->peer, b, n, now_ms);
}

static const char *refusal(int why) {
    switch (why) {
    case R_DATA: return "The two games were imported from different disc data (parts or animations differ).\n"
                        "Import the same disc on both PCs (or copy one import folder to the other PC).";
    case R_VERSION: return "The two copies of Armored-CoreR are different versions.\nUse the same build on both PCs.";
    case R_BUSY: return "The host is already in a match with someone else.";
    case R_STAGE: return "The stage's files differ between the two PCs.\nImport the same disc on both PCs.";
    default: return "The other player refused the match.";
    }
}

/* Handles a handshake packet that arrives during the match (pings, repeats). */
static void late_handshake(net_link *l, const net_addr *from, const uint8_t *b, int len, uint64_t now_ms) {
    if (b[4] != VERSION) return;
    const int type = b[5];
    if (!l->have_peer || !same_addr(from, &l->peer)) {
        if (type == H_HELLO) send_simple(l, from, H_REFUSE, R_BUSY, now_ms);
        return;
    }
    if (type == H_PING && len >= 10) {
        uint8_t r[16];
        int n = hs_header(r, H_PONG);
        memcpy(r + n, b + 6, 4); n += 4;
        send_to(l, from, r, n, now_ms);
    } else if (type == H_PONG && len >= 10) {
        const uint32_t sent = get32(b + 6), now32 = (uint32_t)now_ms;
        const int rtt = (int)(now32 - sent);
        if (rtt >= 0 && rtt < 10000) l->ping_ms = l->ping_ms ? (l->ping_ms * 3 + rtt) / 4 : rtt;
    } else if (type == H_HELLO && l->host) {
        send_welcome(l, now_ms);      /* our WELCOME was lost */
    }
}

int net_link_connect(net_link *l, uint64_t now_ms, net_match_info *info, char *status, int status_size) {
    if (!l->start_ms) {
        l->start_ms = now_ms;
        l->last_rx_ms = now_ms;
        if (l->host) {
            l->info.local = 0;
            for (int i = 0; i < NET_PARTS; ++i) l->info.parts[0][i] = l->parts[i];
            SDL_strlcpy(l->info.name[0], l->name, sizeof l->info.name[0]);
            l->info.stage = l->stage;
            l->info.delay = l->delay;
            l->info.coop_ai = l->coop_ai;
            l->info.ai_preset = l->ai_preset;
            l->info.seed = (uint32_t)(SDL_GetTicksNS() * 2654435761u) | 1u;
        }
    }
    net_link_pump(l, now_ms);
    if (l->state != NET_CONNECTING) {
        if (l->state == NET_STARTED) *info = l->info;
        return l->state;
    }
    const uint64_t waited = now_ms - l->start_ms;

    uint8_t b[PKT_MAX];
    net_addr from;
    int len;
    online_upkeep(l, now_ms);
    while ((len = raw_recv(l, &from, b, sizeof b)) > 0) {
        if (is_stun(b, len)) { stun_read(l, b, len); continue; }
        if (l->online && l->host && l->strict && !(l->have_punch && same_addr(&from, &l->punch))) continue;
        if (l->host && is_match(b, len) && l->have_peer && same_addr(&from, &l->peer)) {
            l->state = NET_STARTED;     /* their READY was lost; their match packets mean the same */
            break;
        }
        if (!is_hs(b, len)) continue;
        const int type = b[5];
        if (b[4] != VERSION) {
            if (l->host && type == H_HELLO) send_simple(l, &from, H_REFUSE, R_VERSION, now_ms);
            if (!l->host && type == H_REFUSE) {
                SDL_snprintf(l->error, sizeof l->error, "%s", refusal(R_VERSION));
                l->state = NET_FAILED;
            }
            continue;
        }
        if (l->host) {
            if (type == H_HELLO && len >= 6 + 4 + NET_PARTS * 2 + NET_NAME) {
                if (l->have_peer && !same_addr(&from, &l->peer)) { send_simple(l, &from, H_REFUSE, R_BUSY, now_ms); continue; }
                if (get32(b + 6) != l->data_hash) {
                    send_simple(l, &from, H_REFUSE, R_DATA, now_ms);
                    l->refused_once = 1;
                    continue;
                }
                l->peer = from;
                l->have_peer = 1;
                l->last_rx_ms = now_ms;
                for (int i = 0; i < NET_PARTS; ++i) l->info.parts[1][i] = get16(b + 10 + i * 2);
                memcpy(l->info.name[1], b + 10 + NET_PARTS * 2, NET_NAME);
                l->info.name[1][NET_NAME] = 0;
                send_welcome(l, now_ms);
            } else if (type == H_READY && l->have_peer && same_addr(&from, &l->peer)) {
                l->state = NET_STARTED;
                break;
            } else if (type == H_REFUSE && l->have_peer && same_addr(&from, &l->peer)) {
                SDL_snprintf(l->error, sizeof l->error, "%s", refusal(len > 6 ? b[6] : 0));
                l->have_peer = 0;        /* keep hosting */
                l->refused_once = 1;
            } else {
                late_handshake(l, &from, b, len, now_ms);
            }
        } else {
            if (!same_addr(&from, &l->peer)) continue;
            l->last_rx_ms = now_ms;
            if (type == H_REFUSE) {
                SDL_snprintf(l->error, sizeof l->error, "%s", refusal(len > 6 ? b[6] : 0));
                l->state = NET_FAILED;
            } else if (type == H_WELCOME && len >= 6 + 4 + NET_PARTS * 4 + 2 + 8 + NET_NAME * 2 + 2) {
                int n = 6;
                if (get32(b + n) != l->data_hash) {
                    SDL_snprintf(l->error, sizeof l->error, "%s", refusal(R_DATA));
                    l->state = NET_FAILED;
                    break;
                }
                n += 4;
                for (int p = 0; p < 2; ++p)
                    for (int i = 0; i < NET_PARTS; ++i) { l->info.parts[p][i] = get16(b + n); n += 2; }
                l->info.stage = b[n++];
                l->info.delay = b[n++];
                l->info.seed = get32(b + n); n += 4;
                const uint32_t stage_hash = get32(b + n); n += 4;
                for (int p = 0; p < 2; ++p) { memcpy(l->info.name[p], b + n, NET_NAME); l->info.name[p][NET_NAME] = 0; n += NET_NAME; }
                l->info.coop_ai = b[n] > 2 ? 2 : b[n]; ++n;
                l->info.ai_preset = b[n++];
                l->info.local = 1;
                if (l->info.stage && net_stage_check(l->import_dir, l->info.stage) != stage_hash) {
                    send_simple(l, &l->peer, H_REFUSE, R_STAGE, now_ms);
                    SDL_snprintf(l->error, sizeof l->error, "%s", refusal(R_STAGE));
                    l->state = NET_FAILED;
                    break;
                }
                send_simple(l, &l->peer, H_READY, 0, now_ms);
                l->last_send_ms = now_ms;
                l->state = NET_STARTED;
                break;
            }
        }
    }
    if (l->state == NET_STARTED) {
        l->last_rx_ms = now_ms;
        *info = l->info;
        return NET_STARTED;
    }
    if (l->state == NET_FAILED) return NET_FAILED;

    if (l->host) {
        if (!l->have_peer && (!l->last_beacon_ms || now_ms - l->last_beacon_ms >= 1000)) {
            /* Tell finders on the network (and on this PC) we're here. */
            uint8_t bb[64];
            int n = 0;
            bb[n++] = 'A'; bb[n++] = 'C'; bb[n++] = 'R'; bb[n++] = 'B'; bb[n++] = VERSION;
            put16(bb + n, l->port_shown); n += 2;
            put32(bb + n, l->data_hash); n += 4;
            bb[n++] = (uint8_t)l->stage;
            memcpy(bb + n, l->name, NET_NAME); n += NET_NAME;
            put32(bb + n, l->info.seed); n += 4;      /* tells copies of the same beacon apart from other hosts */
            bb[n++] = (uint8_t)l->coop_ai;
            static const net_addr everyone = { { 255, 255, 255, 255 }, NET_BEACON_PORT },
                                  here = { { 127, 0, 0, 1 }, NET_BEACON_PORT };
            raw_send(l, &everyone, bb, n);
            raw_send(l, &here, bb, n);
            l->last_beacon_ms = now_ms;
        }
        char addr[64];
        net_local_address(addr, sizeof addr);
        SDL_snprintf(status, (size_t)status_size,
                     "HOSTING A MATCH (PLAYER 1)\n\n"
                     "TELL PLAYER 2 TO JOIN:  %s:%d\n"
                     "(SAME PC: 127.0.0.1:%d)\n\n"
                     "WAITING FOR PLAYER 2...  %d S\n"
                     "(PLAYER 2 CAN ALSO PICK THIS GAME UNDER GARAGE > NETWORK)\n\n%s%s\n\nB / ESC: CANCEL",
                     addr[0] ? addr : "(this PC's address)", l->port_shown,
                     l->port_shown, (int)(waited / 1000),
                     l->refused_once ? "A PLAYER TRIED TO JOIN BUT COULD NOT:\n" : "", l->refused_once ? (l->error[0] ? l->error : refusal(R_DATA)) : "");
    } else {
        if (now_ms - l->last_send_ms >= 250) { send_hello(l, now_ms); l->last_send_ms = now_ms; }
        if (l->online && waited > 300000) {
            SDL_snprintf(l->error, sizeof l->error,
                         "The host never answered the briefing.\n\nCheck the briefing with the host. If the host already entered your\n"
                         "contract, your two routers may block each other: try direct IP\n(the host forwards UDP port %d) or LAN.", NET_PORT_DEFAULT);
            l->state = NET_FAILED;
            return NET_FAILED;
        }
        if (!l->online && waited > 30000) {
            SDL_snprintf(l->error, sizeof l->error,
                         "No answer from %s.\n\nCheck that the host has started (--host), that the address is right,\n"
                         "and that the host's firewall lets Armored-CoreR use the network.", l->join);
            l->state = NET_FAILED;
            return NET_FAILED;
        }
        SDL_snprintf(status, (size_t)status_size, "JOINING A MATCH (PLAYER 2)\n\nCONNECTING TO %s...  %d S\n\nB / ESC: CANCEL",
                     l->join, (int)(waited / 1000));
    }
    return NET_CONNECTING;
}

/* ---------------- the match ---------------- */

void net_link_send(net_link *l, const void *data, int len, uint64_t now_ms) {
    if (!l->have_peer) return;
    send_to(l, &l->peer, data, len, now_ms);
    if (!l->host && !l->got_match && now_ms - l->last_send_ms >= 100) {
        send_simple(l, &l->peer, H_READY, 0, now_ms);     /* until the host's match packets come */
        l->last_send_ms = now_ms;
    }
    if (now_ms - l->last_ping_ms >= 500) {
        uint8_t b[16];
        int n = hs_header(b, H_PING);
        put32(b + n, (uint32_t)now_ms); n += 4;
        send_to(l, &l->peer, b, n, now_ms);
        l->last_ping_ms = now_ms;
    }
    net_link_pump(l, now_ms);
}

int net_link_recv(net_link *l, void *buf, int cap, uint64_t now_ms) {
    uint8_t b[PKT_MAX];
    net_addr from;
    int len;
    while ((len = raw_recv(l, &from, b, sizeof b)) > 0) {
        if (is_hs(b, len)) {
            if (l->have_peer && same_addr(&from, &l->peer)) l->last_rx_ms = now_ms;
            late_handshake(l, &from, b, len, now_ms);
            continue;
        }
        if (!is_match(b, len) || !l->have_peer || !same_addr(&from, &l->peer) || len > cap) continue;
        l->last_rx_ms = now_ms;
        l->got_match = 1;
        memcpy(buf, b, (size_t)len);
        return len;
    }
    return 0;
}

void net_link_touch(net_link *l, uint64_t now_ms) { l->last_rx_ms = now_ms; }
uint64_t net_link_quiet_ms(const net_link *l, uint64_t now_ms) { return now_ms > l->last_rx_ms ? now_ms - l->last_rx_ms : 0; }
int net_link_ping_ms(const net_link *l) { return l->ping_ms; }

void net_local_address(char *out, int size) {
    out[0] = 0;
    if (!sockets_up()) return;
    /* The address the system would use to reach the internet (nothing is sent). */
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BAD_SOCK) return;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    sa.sin_addr.s_addr = htonl(0x08080808u);
    if (connect(s, (const struct sockaddr *)&sa, sizeof sa) == 0) {
        struct sockaddr_in me;
        socklen_t n = sizeof me;
        if (getsockname(s, (struct sockaddr *)&me, &n) == 0 && me.sin_addr.s_addr != 0) {
            const uint8_t *ip = (const uint8_t *)&me.sin_addr;
            SDL_snprintf(out, (size_t)size, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        }
    }
    close_sock(s);
}

/* ---------------- LAN discovery ---------------- */

#define FOUND_MAX 16
struct net_finder {
    sock_t s;
    uint32_t data_hash;
    net_found found[FOUND_MAX];
    int n;
};

net_finder *net_finder_open(const char *import_dir) {
    if (!sockets_up()) return NULL;
    net_finder *f = (net_finder *)SDL_calloc(1, sizeof *f);
    if (!f) return NULL;
    f->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (f->s == BAD_SOCK) { SDL_free(f); return NULL; }
    const int yes = 1;
    /* Several copies on one PC can all listen. */
    setsockopt(f->s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    setsockopt(f->s, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof yes);
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(f->s, FIONBIO, &nb);
#else
    fcntl(f->s, F_SETFL, fcntl(f->s, F_GETFL, 0) | O_NONBLOCK);
#endif
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(NET_BEACON_PORT);
    if (bind(f->s, (const struct sockaddr *)&sa, sizeof sa) != 0) { close_sock(f->s); SDL_free(f); return NULL; }
    f->data_hash = net_data_check(import_dir);
    return f;
}

void net_finder_close(net_finder *f) {
    if (!f) return;
    close_sock(f->s);
    SDL_free(f);
}

int net_finder_poll(net_finder *f, uint64_t now_ms, net_found *out, int max) {
    if (!f) return 0;
    uint8_t b[128];
    struct sockaddr_in sa;
    for (;;) {
        socklen_t sl = sizeof sa;
        const int len = (int)recvfrom(f->s, (char *)b, sizeof b, 0, (struct sockaddr *)&sa, &sl);
        if (len <= 0) break;
        if (len < 5 + 2 + 4 + 1 + NET_NAME + 4 || b[0] != 'A' || b[1] != 'C' || b[2] != 'R' || b[3] != 'B' || b[4] != VERSION) continue;
        net_addr from;
        from_sa(&sa, &from);
        const int port = get16(b + 5);
        const uint32_t id = get32(b + 12 + NET_NAME);
        const int here = from.ip[0] == 127;
        char addr[64];
        SDL_snprintf(addr, sizeof addr, "%d.%d.%d.%d:%d", from.ip[0], from.ip[1], from.ip[2], from.ip[3], port);
        int i = 0;
        while (i < f->n && f->found[i].id != id) ++i;
        if (i == f->n) {
            if (f->n == FOUND_MAX) continue;
            ++f->n;
            f->found[i].this_pc = 0;
        }
        net_found *g = &f->found[i];
        g->id = id;
        /* Heard on this PC's own address too: it's on this PC, use 127.0.0.1. */
        if (here) g->this_pc = 1;
        if (here || !g->this_pc) SDL_strlcpy(g->addr, addr, sizeof g->addr);
        g->same_data = get32(b + 7) == f->data_hash;
        g->stage = b[11];
        g->coop_ai = len >= 12 + NET_NAME + 5 ? b[16 + NET_NAME] : 0;
        memcpy(g->name, b + 12, NET_NAME);
        g->name[NET_NAME] = 0;
        g->seen_ms = now_ms;
    }
    /* Forget hosts gone quiet (started a match or closed). */
    for (int i = 0; i < f->n;)
        if (now_ms - f->found[i].seen_ms > 3000) f->found[i] = f->found[--f->n];
        else ++i;
    int n = 0;
    for (int i = 0; i < f->n && n < max; ++i) out[n++] = f->found[i];
    return n;
}

/* ---------------- data check ---------------- */

typedef struct path_list { char **p; int n, cap; const char *root; } path_list;

static SDL_EnumerationResult SDLCALL collect(void *ud, const char *dirname, const char *fname) {
    path_list *pl = (path_list *)ud;
    char full[1200];
    SDL_snprintf(full, sizeof full, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(full, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_EnumerateDirectory(full, collect, ud);
    } else if (info.type == SDL_PATHTYPE_FILE) {
        if (pl->n == pl->cap) {
            pl->cap = pl->cap ? pl->cap * 2 : 256;
            pl->p = (char **)SDL_realloc(pl->p, sizeof(char *) * (size_t)pl->cap);
        }
        pl->p[pl->n++] = SDL_strdup(full);
    }
    return SDL_ENUM_CONTINUE;
}

static int by_name(const void *a, const void *b) { return SDL_strcmp(*(char *const *)a, *(char *const *)b); }

static uint32_t fnv(uint32_t h, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* Hashes every file under dir/sub (names relative to dir, and contents). */
static uint32_t hash_tree(uint32_t h, const char *dir, const char *sub) {
    char root[1100];
    SDL_snprintf(root, sizeof root, "%s/%s", dir, sub);
    path_list pl = { NULL, 0, 0, root };
    SDL_EnumerateDirectory(root, collect, &pl);
    if (pl.n) SDL_qsort(pl.p, (size_t)pl.n, sizeof(char *), by_name);
    const size_t skip = SDL_strlen(dir);
    for (int i = 0; i < pl.n; ++i) {
        const char *rel = pl.p[i] + skip;
        while (*rel == '/' || *rel == '\\') ++rel;
        char norm[1200];
        SDL_strlcpy(norm, rel, sizeof norm);
        for (char *c = norm; *c; ++c) if (*c == '\\') *c = '/';
        h = fnv(h, norm, SDL_strlen(norm) + 1);
        size_t size = 0;
        void *data = SDL_LoadFile(pl.p[i], &size);
        if (data) {
            /* Text files: line endings may differ between PCs that copied the import. */
            uint8_t *d = (uint8_t *)data;
            size_t w = 0;
            for (size_t r = 0; r < size; ++r) if (d[r] != '\r') d[w++] = d[r];
            h = fnv(h, d, w);
            SDL_free(data);
        }
        SDL_free(pl.p[i]);
    }
    SDL_free(pl.p);
    return h;
}

uint32_t net_data_check(const char *import_dir) {
    uint32_t h = 2166136261u;
    h = hash_tree(h, import_dir, "parts");
    h = hash_tree(h, import_dir, "motion");
    return h;
}

uint32_t net_stage_check(const char *import_dir, int stage) {
    char sub[32];
    SDL_snprintf(sub, sizeof sub, "stages/%02d", stage);
    char probe[1200];
    SDL_snprintf(probe, sizeof probe, "%s/%s/collision.obj", import_dir, sub);
    if (!SDL_GetPathInfo(probe, NULL)) return 0;
    return hash_tree(2166136261u, import_dir, sub);
}
