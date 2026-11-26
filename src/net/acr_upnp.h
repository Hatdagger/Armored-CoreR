/* UPnP port opening (Internet Gateway Device): the text side only.
 *
 * A host asks its home router to forward a UDP port to this PC, so player 2's
 * first packet gets in and the one-code join works more often. The steps:
 *   1. SSDP: an M-SEARCH to 239.255.255.250:1900; routers answer with a
 *      LOCATION (the URL of their description).
 *   2. HTTP GET the description; find the WANIPConnection (or
 *      WANPPPConnection) service and its control URL.
 *   3. SOAP: GetExternalIPAddress, AddPortMapping, later DeletePortMapping.
 *
 * No sockets in here (src/platform/sdl3/net_upnp.c does the talking), so all
 * of it is tested on plain text (tests/test_upnp.c). */
#ifndef ACR_UPNP_H
#define ACR_UPNP_H

#include <stdint.h>

#define ACR_UPNP_SSDP_PORT 1900

/* SOAP errors that change what we do next. */
enum {
    ACR_UPNP_ERR_CONFLICT = 718,          /* the external port is someone else's */
    ACR_UPNP_ERR_SAME_PORTS = 724,        /* internal and external port must match */
    ACR_UPNP_ERR_PERMANENT_ONLY = 725     /* only lease 0 (no expiry) allowed */
};

/* The M-SEARCH request for search target `st`. Returns its length (0 if it
 * doesn't fit). */
int acr_upnp_msearch(char *out, int cap, const char *st);

/* An SSDP answer: 1 and its LOCATION URL if it's a "200 OK" with one. */
int acr_upnp_ssdp_location(const char *reply, int len, char *url, int cap);

/* "http://host[:port]/path" -> parts (port 80 by default, path "/" if
 * missing). 0 if it isn't a plain http URL. */
int acr_upnp_parse_url(const char *url, char *host, int host_cap, int *port, char *path, int path_cap);

/* A URL found in a description, made absolute against `base`. */
int acr_upnp_resolve(const char *base, const char *ref, char *out, int cap);

/* The router's description: the first WANIPConnection service (any version),
 * else the first WANPPPConnection; its absolute control URL and service type.
 * URLBase in the description wins over the description's own URL. */
int acr_upnp_find_control(const char *xml, const char *desc_url, char *control, int control_cap,
                          char *service, int service_cap);

/* An HTTP response: its status code (0 if not HTTP). */
int acr_upnp_http_status(const char *resp, int len);
/* The body of an HTTP response, un-chunked in place if it was sent in
 * chunks; NULL if there's no body. *body_len gets its length. */
char *acr_upnp_http_body(char *resp, int len, int *body_len);

/* The text of the first <tag> element (any namespace prefix), trimmed. */
int acr_upnp_xml_value(const char *xml, const char *tag, char *out, int cap);

/* SOAP request bodies. `service` is the service type from the description. */
int acr_upnp_soap_external_ip(char *out, int cap, const char *service);
int acr_upnp_soap_add(char *out, int cap, const char *service, int ext_port, int int_port,
                      const char *client_ip, int lease_s, const char *what);
int acr_upnp_soap_delete(char *out, int cap, const char *service, int ext_port);

/* A SOAP fault's UPnP errorCode (0 if none). */
int acr_upnp_soap_error(const char *body);

/* "a.b.c.d" -> 4 bytes. */
int acr_upnp_parse_ip(const char *text, uint8_t ip[4]);
/* An address the internet can reach: not private (10/8, 172.16/12,
 * 192.168/16), shared carrier NAT (100.64/10), loopback, link-local, 0/8 or
 * multicast and above. A router whose own outside address is one of these
 * sits behind another router, so opening a port on it doesn't help. */
int acr_upnp_is_public(const uint8_t ip[4]);

#endif
