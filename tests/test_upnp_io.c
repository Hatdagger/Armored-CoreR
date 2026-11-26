/* The router talk (net_upnp.c) against a fake router on this PC (a child
 * process): the search, a description sent in chunks, the external address,
 * a taken port (error 718) answered with another port, a router that only
 * takes permanent forwards (725), taking the forward away again, a router
 * that refuses outright, and no router at all. POSIX only (fork). */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "net_upnp.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#define INT_PORT 41234
enum { FAKE_GOOD, FAKE_REFUSE };

static int bound(int type, int *port) {
    const int s = socket(AF_INET, type, 0);
    const int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (struct sockaddr *)&sa, sizeof sa);
    socklen_t n = sizeof sa;
    getsockname(s, (struct sockaddr *)&sa, &n);
    *port = ntohs(sa.sin_port);
    if (type == SOCK_STREAM) listen(s, 8);
    return s;
}

static void answer(int c, const char *status, const char *body, int chunked) {
    char head[256];
    if (chunked) {
        snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: text/xml\r\nTransfer-Encoding: chunked\r\n\r\n", status);
        (void)!write(c, head, strlen(head));
        const size_t n = strlen(body);
        for (size_t at = 0; at < n; at += 100) {        /* 100-byte chunks */
            const size_t k = n - at < 100 ? n - at : 100;
            char sz[16];
            snprintf(sz, sizeof sz, "%zx\r\n", k);
            (void)!write(c, sz, strlen(sz));
            (void)!write(c, body + at, k);
            (void)!write(c, "\r\n", 2);
        }
        (void)!write(c, "0\r\n\r\n", 5);
    } else {
        snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: text/xml\r\nContent-Length: %zu\r\n\r\n", status, strlen(body));
        (void)!write(c, head, strlen(head));
        (void)!write(c, body, strlen(body));
    }
}

static void fault(int c, int code) {
    char b[400];
    snprintf(b, sizeof b, "<s:Envelope><s:Body><s:Fault><detail><UPnPError><errorCode>%d</errorCode>"
                          "</UPnPError></detail></s:Fault></s:Body></s:Envelope>", code);
    answer(c, "500 Internal Server Error", b, 0);
}

static int field(const char *req, const char *tag) {
    char open[64];
    snprintf(open, sizeof open, "<%s>", tag);
    const char *p = strstr(req, open);
    return p ? atoi(p + strlen(open)) : -1;
}

/* The fake router: answers until killed; logs what it did to `log`. */
static void fake_router(int udp, int tcp, int http_port, int how, const char *log_path) {
    FILE *log = fopen(log_path, "w");
    setvbuf(log, NULL, _IONBF, 0);
    char desc[1400];
    snprintf(desc, sizeof desc,
             "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device>"
             "<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType><deviceList><device>"
             "<deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType><deviceList><device>"
             "<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
             "<serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId><controlURL>ctl/IPConn</controlURL>"
             "</service></serviceList></device></deviceList></device></deviceList></device></root>");
    for (;;) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(udp, &set);
        FD_SET(tcp, &set);
        if (select((udp > tcp ? udp : tcp) + 1, &set, NULL, NULL, NULL) <= 0) continue;
        if (FD_ISSET(udp, &set)) {
            char buf[1024];
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            const int n = (int)recvfrom(udp, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
            if (n > 0) {
                buf[n] = 0;
                if (strstr(buf, "M-SEARCH") && strstr(buf, "InternetGatewayDevice:1")) {
                    char r[300];
                    snprintf(r, sizeof r, "HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                                          "Location: http://127.0.0.1:%d/desc/rootDesc.xml\r\n\r\n", http_port);
                    sendto(udp, r, strlen(r), 0, (struct sockaddr *)&from, fl);
                    fprintf(log, "search\n");
                }
            }
        }
        if (FD_ISSET(tcp, &set)) {
            const int c = accept(tcp, NULL, NULL);
            if (c < 0) continue;
            char req[8192];
            int got = 0;
            /* Read the headers, then Content-Length more. */
            for (;;) {
                const int r = (int)read(c, req + got, sizeof req - 1 - got);
                if (r <= 0) break;
                got += r;
                req[got] = 0;
                char *end = strstr(req, "\r\n\r\n");
                if (!end) continue;
                const char *cl = strstr(req, "Content-Length: ");
                const int want = cl ? atoi(cl + 16) : 0;
                if ((int)(got - (end + 4 - req)) >= want) break;
            }
            req[got] = 0;
            if (!strncmp(req, "GET /desc/rootDesc.xml", 22)) {
                answer(c, "200 OK", desc, 1);
                fprintf(log, "desc\n");
            } else if (!strncmp(req, "POST /desc/ctl/IPConn", 21)) {
                if (strstr(req, "#GetExternalIPAddress\"")) {
                    answer(c, "200 OK", "<s:Envelope><s:Body><u:GetExternalIPAddressResponse>"
                                        "<NewExternalIPAddress>203.0.113.9</NewExternalIPAddress>"
                                        "</u:GetExternalIPAddressResponse></s:Body></s:Envelope>", 0);
                } else if (strstr(req, "#AddPortMapping\"")) {
                    const int ext = field(req, "NewExternalPort"), lease = field(req, "NewLeaseDuration");
                    fprintf(log, "add %d lease %d client %s\n", ext, lease,
                            strstr(req, "<NewInternalClient>127.0.0.1</NewInternalClient>") ? "ok" : "wrong");
                    if (how == FAKE_REFUSE) fault(c, 501);
                    else if (ext == INT_PORT) fault(c, 718);           /* taken by another PC */
                    else if (lease != 0) fault(c, 725);                /* permanent only */
                    else answer(c, "200 OK", "<s:Envelope><s:Body><u:AddPortMappingResponse/></s:Body></s:Envelope>", 0);
                } else if (strstr(req, "#DeletePortMapping\"")) {
                    fprintf(log, "delete %d\n", field(req, "NewExternalPort"));
                    answer(c, "200 OK", "<s:Envelope><s:Body><u:DeletePortMappingResponse/></s:Body></s:Envelope>", 0);
                } else {
                    fault(c, 401);
                }
            } else {
                answer(c, "404 Not Found", "", 0);
            }
            close(c);
        }
    }
}

static pid_t start(int how, const char *log_path) {
    int udp_port, http_port;
    const int udp = bound(SOCK_DGRAM, &udp_port), tcp = bound(SOCK_STREAM, &http_port);
    const pid_t pid = fork();
    if (pid == 0) { fake_router(udp, tcp, http_port, how, log_path); _exit(0); }
    close(udp);
    close(tcp);
    char at[64];
    snprintf(at, sizeof at, "127.0.0.1:%d", udp_port);
    setenv("ACR_UPNP_SSDP", at, 1);
    return pid;
}

static void stop(pid_t pid) { kill(pid, SIGTERM); waitpid(pid, NULL, 0); }

static int log_has(const char *path, const char *line) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[256];
    int found = 0;
    while (fgets(buf, sizeof buf, f)) found |= strncmp(buf, line, strlen(line)) == 0;
    fclose(f);
    return found;
}

int main(void) {
    char log_path[] = "/tmp/acr_upnp_io_XXXXXX";
    const int fd = mkstemp(log_path);
    if (fd >= 0) close(fd);

    /* A router that has our port taken and only allows permanent forwards. */
    pid_t pid = start(FAKE_GOOD, log_path);
    net_upnp_map m;
    net_upnp_open(INT_PORT, 6000, NULL, &m);
    printf("good router: %s\n", m.why);
    EXPECT(m.result == NET_UPNP_OPENED);
    EXPECT(m.ext_port != INT_PORT && m.ext_port >= 20000 && m.ext_port < 60000);
    EXPECT(m.int_port == INT_PORT);
    EXPECT(m.ext_ip[0] == 203 && m.ext_ip[1] == 0 && m.ext_ip[2] == 113 && m.ext_ip[3] == 9);
    EXPECT(!strcmp(m.local_ip, "127.0.0.1"));
    EXPECT(strstr(m.control, "http://127.0.0.1:") == m.control && strstr(m.control, "/desc/ctl/IPConn"));
    EXPECT(log_has(log_path, "search") && log_has(log_path, "desc"));
    {
        char line[64];
        snprintf(line, sizeof line, "add %d lease 7200 client ok", INT_PORT);
        EXPECT(log_has(log_path, line));
        snprintf(line, sizeof line, "add %d lease 7200", m.ext_port);
        EXPECT(log_has(log_path, line));
        snprintf(line, sizeof line, "add %d lease 0 client ok", m.ext_port);
        EXPECT(log_has(log_path, line));
        net_upnp_close(&m);
        snprintf(line, sizeof line, "delete %d", m.ext_port);
        EXPECT(log_has(log_path, line));
    }
    stop(pid);

    /* A router that says no. */
    pid = start(FAKE_REFUSE, log_path);
    net_upnp_open(INT_PORT, 6000, NULL, &m);
    printf("refusing router: %s\n", m.why);
    EXPECT(m.result == NET_UPNP_REFUSED);
    net_upnp_close(&m);   /* nothing to take away: no delete sent */
    EXPECT(!log_has(log_path, "delete"));
    stop(pid);

    /* Nobody there. */
    {
        int dead;
        const int s = bound(SOCK_DGRAM, &dead);
        close(s);
        char at[64];
        snprintf(at, sizeof at, "127.0.0.1:%d", dead);
        setenv("ACR_UPNP_SSDP", at, 1);
        net_upnp_open(INT_PORT, 1500, NULL, &m);
        printf("no router: %s\n", m.why);
        EXPECT(m.result == NET_UPNP_NO_ROUTER);
    }

    /* Turned off. */
    setenv("ACR_UPNP", "0", 1);
    net_upnp_open(INT_PORT, 1500, NULL, &m);
    EXPECT(m.result == NET_UPNP_OFF);

    remove(log_path);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("upnp_io: all good\n");
    return 0;
}
