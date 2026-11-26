#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L   /* getaddrinfo, clock_gettime under strict C11 */
#endif
#include "net_upnp.h"
#include "acr_upnp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET sock_t;
#  define BAD_SOCK INVALID_SOCKET
#  define close_sock closesocket
static int in_progress(void) { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
static uint64_t now_ms(void) { return GetTickCount64(); }
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <time.h>
#  include <unistd.h>
typedef int sock_t;
#  define BAD_SOCK (-1)
#  define close_sock close
static int in_progress(void) { return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN; }
static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)(t.tv_nsec / 1000000);
}
#endif

#define WHAT "Armored-CoreR"
#define LEASE_S 7200       /* 2 hours: a crashed game's forward goes away by itself */
#define RESP_MAX 65536

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

static void non_blocking(sock_t s) {
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static int cancelled(volatile int *cancel) { return cancel && *cancel; }

/* Waits until s can be read (or written) or the deadline passes. */
static int wait_sock(sock_t s, int for_write, uint64_t deadline) {
    const uint64_t t = now_ms();
    if (t >= deadline) return 0;
    const uint64_t left = deadline - t;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    struct timeval tv;
    tv.tv_sec = (long)(left / 1000);
    tv.tv_usec = (long)(left % 1000) * 1000;
    return select((int)s + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &tv) > 0;
}

static int lookup(const char *host, int port, struct sockaddr_in *sa) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;
    memcpy(sa, res->ai_addr, sizeof *sa);
    sa->sin_port = htons((uint16_t)port);
    freeaddrinfo(res);
    return 1;
}

/* ---------------- SSDP: who's the router? ---------------- */

static int search(int budget_ms, volatile int *cancel, char loc[][512], int max) {
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BAD_SOCK) return 0;
    non_blocking(s);
#ifdef _WIN32
    const DWORD ttl = 2;      /* Windows wants a whole DWORD here */
#else
    const unsigned char ttl = 2;
#endif
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof ttl);
    struct sockaddr_in to;
    const char *over = getenv("ACR_UPNP_SSDP");
    int ok = 0;
    if (over && over[0]) {
        char host[128];
        snprintf(host, sizeof host, "%s", over);
        char *colon = strrchr(host, ':');
        int port = ACR_UPNP_SSDP_PORT;
        if (colon) { *colon = 0; port = atoi(colon + 1); }
        ok = lookup(host, port, &to);
    } else {
        memset(&to, 0, sizeof to);
        to.sin_family = AF_INET;
        to.sin_port = htons(ACR_UPNP_SSDP_PORT);
        inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
        ok = 1;
    }
    if (!ok) { close_sock(s); return 0; }
    static const char *const k_st[] = { "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                                        "urn:schemas-upnp-org:service:WANIPConnection:1",
                                        "urn:schemas-upnp-org:service:WANPPPConnection:1" };
    const uint64_t start = now_ms();
    uint64_t deadline = start + (uint64_t)budget_ms, last_send = 0;
    int n = 0, sends = 0;
    while (!cancelled(cancel) && now_ms() < deadline) {
        if (sends < 2 && now_ms() - last_send >= 700) {    /* twice: one lost packet shouldn't hide the router */
            for (int i = 0; i < 4; ++i) {
                char req[300];
                const int len = acr_upnp_msearch(req, sizeof req, k_st[i]);
                sendto(s, req, len, 0, (const struct sockaddr *)&to, sizeof to);
            }
            last_send = now_ms();
            ++sends;
        }
        uint64_t slice = now_ms() + 100;
        if (slice > deadline) slice = deadline;
        if (!wait_sock(s, 0, slice)) continue;
        char buf[2048];
        const int len = (int)recv(s, buf, sizeof buf - 1, 0);
        if (len <= 0) continue;
        buf[len] = 0;
        char url[512];
        if (!acr_upnp_ssdp_location(buf, len, url, sizeof url)) continue;
        int seen = 0;
        for (int i = 0; i < n; ++i) seen |= !strcmp(loc[i], url);
        if (seen || n >= max) continue;
        snprintf(loc[n++], 512, "%s", url);
        /* Once one answered, the others (if any) get a moment more. */
        if (deadline > now_ms() + 400) deadline = now_ms() + 400;
    }
    close_sock(s);
    return n;
}

/* ---------------- HTTP ---------------- */

/* One request; the whole response into resp (NUL-ended). Returns its length,
 * or -1. local_ip (may be NULL) gets this PC's address on that connection. */
static int http(const char *url, const char *soap_action, const char *service, const char *body,
                char *resp, int cap, int timeout_ms, char *local_ip, int local_cap) {
    char host[256], path[512];
    int port;
    struct sockaddr_in sa;
    if (!acr_upnp_parse_url(url, host, sizeof host, &port, path, sizeof path) || !lookup(host, port, &sa)) return -1;
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK) return -1;
    non_blocking(s);
    const uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
    if (connect(s, (const struct sockaddr *)&sa, sizeof sa) != 0) {
        if (!in_progress() || !wait_sock(s, 1, deadline)) { close_sock(s); return -1; }
        int err = 0;
        socklen_t el = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &el);
        if (err) { close_sock(s); return -1; }
    }
    if (local_ip) {
        struct sockaddr_in me;
        socklen_t ml = sizeof me;
        local_ip[0] = 0;
        if (getsockname(s, (struct sockaddr *)&me, &ml) == 0) inet_ntop(AF_INET, &me.sin_addr, local_ip, (socklen_t)local_cap);
    }
    char head[1024];
    int hn;
    if (soap_action) {
        hn = snprintf(head, sizeof head,
                      "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
                      "SOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                      path, host, port, service, soap_action, (int)strlen(body));
    } else {
        hn = snprintf(head, sizeof head, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", path, host, port);
    }
    const char *parts[2] = { head, soap_action ? body : "" };
    const int lens[2] = { hn, soap_action ? (int)strlen(body) : 0 };
    for (int p = 0; p < 2; ++p) {
        for (int at = 0; at < lens[p];) {
            if (!wait_sock(s, 1, deadline)) { close_sock(s); return -1; }
            const int w = (int)send(s, parts[p] + at, lens[p] - at, 0);
            if (w <= 0) { close_sock(s); return -1; }
            at += w;
        }
    }
    int got = 0;
    while (got < cap - 1 && wait_sock(s, 0, deadline)) {
        const int r = (int)recv(s, resp + got, cap - 1 - got, 0);
        if (r <= 0) break;                  /* the router closed: done */
        got += r;
    }
    close_sock(s);
    resp[got] = 0;
    return got > 0 ? got : -1;
}

/* A SOAP call: the HTTP status (0: no answer); *soap_err the UPnP error code. */
static int call(const net_upnp_map *m, const char *action, const char *body, char *resp, int cap,
                int timeout_ms, int *soap_err, char **out_body) {
    *soap_err = 0;
    const int n = http(m->control, action, m->service, body, resp, cap, timeout_ms, NULL, 0);
    if (n <= 0) return 0;
    const int status = acr_upnp_http_status(resp, n);
    int bl = 0;
    char *b = acr_upnp_http_body(resp, n, &bl);
    if (status != 200) *soap_err = acr_upnp_soap_error(b);
    if (out_body) *out_body = b;
    return status;
}

/* ---------------- open / close ---------------- */

void net_upnp_open(int int_port, int budget_ms, volatile int *cancel, net_upnp_map *m) {
    memset(m, 0, sizeof *m);
    m->int_port = int_port;
    const char *off = getenv("ACR_UPNP");
    if (off && off[0] == '0') { m->result = NET_UPNP_OFF; snprintf(m->why, sizeof m->why, "turned off (ACR_UPNP=0)"); return; }
    if (!sockets_up()) { m->result = NET_UPNP_NO_ROUTER; snprintf(m->why, sizeof m->why, "no network"); return; }
    const uint64_t deadline = now_ms() + (uint64_t)budget_ms;
    char loc[4][512];
    const int found = search(budget_ms < 2500 ? budget_ms : 2500, cancel, loc, 4);
    if (!found) {
        m->result = NET_UPNP_NO_ROUTER;
        snprintf(m->why, sizeof m->why, "no UPnP router answered");
        return;
    }
    char *resp = (char *)malloc(RESP_MAX);
    if (!resp) { m->result = NET_UPNP_REFUSED; return; }
    m->result = NET_UPNP_REFUSED;
    snprintf(m->why, sizeof m->why, "the router's description had no port-forwarding service");
    for (int i = 0; i < found && !cancelled(cancel) && now_ms() < deadline; ++i) {
        const int left = (int)(deadline - now_ms());
        const int n = http(loc[i], NULL, NULL, NULL, resp, RESP_MAX, left < 2000 ? left : 2000, m->local_ip, sizeof m->local_ip);
        if (n <= 0 || acr_upnp_http_status(resp, n) != 200) {
            snprintf(m->why, sizeof m->why, "no description from %.100s", loc[i]);
            continue;
        }
        int bl;
        const char *xml = acr_upnp_http_body(resp, n, &bl);
        if (!xml || !m->local_ip[0] ||
            !acr_upnp_find_control(xml, loc[i], m->control, sizeof m->control, m->service, sizeof m->service))
            continue;

        char body[1400];
        int err;
        char *rb = NULL;
        /* The router's internet address (optional: some won't say until online). */
        acr_upnp_soap_external_ip(body, sizeof body, m->service);
        if (call(m, "GetExternalIPAddress", body, resp, RESP_MAX, 1500, &err, &rb) == 200) {
            char ip[64];
            if (acr_upnp_xml_value(rb, "NewExternalIPAddress", ip, sizeof ip)) acr_upnp_parse_ip(ip, m->ext_ip);
        }
        /* Our own port number first; if another PC has it, a few others. */
        int tries[4] = { int_port, 0, 0, 0 };
        uint32_t seed = (uint32_t)now_ms() ^ (uint32_t)int_port * 2654435761u;
        for (int t = 1; t < 4; ++t) { seed = seed * 1103515245u + 12345u; tries[t] = 20000 + (int)((seed >> 8) % 40000); }
        for (int t = 0; t < 4 && !cancelled(cancel) && now_ms() < deadline; ++t) {
            int lease = LEASE_S;
            for (int again = 0; again < 2; ++again) {
                acr_upnp_soap_add(body, sizeof body, m->service, tries[t], int_port, m->local_ip, lease, WHAT);
                const int st = call(m, "AddPortMapping", body, resp, RESP_MAX, 2000, &err, NULL);
                if (st == 200) {
                    m->ext_port = tries[t];
                    m->result = NET_UPNP_OPENED;
                    snprintf(m->why, sizeof m->why, "UDP %d -> %s:%d (lease %d s)", tries[t], m->local_ip, int_port, lease);
                    free(resp);
                    return;
                }
                if (err == ACR_UPNP_ERR_PERMANENT_ONLY && lease) { lease = 0; continue; }
                break;
            }
            snprintf(m->why, sizeof m->why, "the router refused port %d (error %d)", tries[t], err);
            if (err == ACR_UPNP_ERR_SAME_PORTS) break;      /* other numbers won't do either */
            if (err != ACR_UPNP_ERR_CONFLICT) break;        /* only a taken port is worth another try */
        }
        break;   /* this router answered; don't ask a second one */
    }
    free(resp);
}

void net_upnp_close(const net_upnp_map *m) {
    if (m->result != NET_UPNP_OPENED) return;
    char body[600];
    char *resp = (char *)malloc(4096);
    if (!resp) return;
    int err;
    acr_upnp_soap_delete(body, sizeof body, m->service, m->ext_port);
    call(m, "DeletePortMapping", body, resp, 4096, 1500, &err, NULL);
    free(resp);
}
