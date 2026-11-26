/* Asks the home router (UPnP Internet Gateway Device) to forward a UDP port
 * to this PC, and to stop again. Blocking calls with short timeouts: the
 * game runs them on a thread (net_link.c). Plain sockets, no SDL, so a test
 * can talk to a fake router (tests/test_upnp_io.c).
 *
 * ACR_UPNP=0 turns it off; ACR_UPNP_SSDP="host:port" sends the search there
 * instead of the multicast group (tests). */
#ifndef NET_UPNP_H
#define NET_UPNP_H

#include <stdint.h>

enum {
    NET_UPNP_OPENED = 1,       /* the port is forwarded to us */
    NET_UPNP_NO_ROUTER = 2,    /* nothing answered the search (UPnP off, or no such router) */
    NET_UPNP_REFUSED = 3,      /* a router answered but wouldn't forward a port */
    NET_UPNP_OFF = 4           /* ACR_UPNP=0 */
};

typedef struct net_upnp_map {
    int result;                /* NET_UPNP_*; 0 while not done */
    uint8_t ext_ip[4];         /* the router's own internet address (0s if it wouldn't say) */
    int ext_port, int_port;
    char local_ip[16];         /* this PC, as the router sees it */
    char control[512];         /* where the router takes its commands */
    char service[160];
    char why[160];             /* a line for the log */
} net_upnp_map;

/* Forwards UDP ext port -> this PC's int_port: the same number first, else a
 * few others. Gives up after about `budget_ms`. `cancel` (may be NULL) is
 * checked now and then; set it to give up early. */
void net_upnp_open(int int_port, int budget_ms, volatile int *cancel, net_upnp_map *m);
/* Removes the forward again (about 1.5 s at most). */
void net_upnp_close(const net_upnp_map *m);

#endif
