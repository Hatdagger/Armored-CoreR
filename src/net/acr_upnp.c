#include "acr_upnp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- small text helpers ---------------- */

static int copy_n(char *out, int cap, const char *s, int n) {
    if (cap <= 0 || n < 0 || n >= cap) return 0;
    memcpy(out, s, (size_t)n);
    out[n] = 0;
    return 1;
}

static int starts_nocase(const char *s, const char *prefix) {
    for (; *prefix; ++s, ++prefix)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
    return 1;
}

/* Trims spaces at both ends of [*a, *b). */
static void trim(const char **a, const char **b) {
    while (*a < *b && isspace((unsigned char)**a)) ++*a;
    while (*b > *a && isspace((unsigned char)(*b)[-1])) --*b;
}

/* The next element named `tag` (any "ns:" prefix) at or after `from`, before
 * `end` (NULL: the string's end). *open_end gets the end of the opening tag,
 * *close the start of its closing tag. */
static const char *find_elem(const char *from, const char *end, const char *tag,
                             const char **open_end, const char **close) {
    const size_t tl = strlen(tag);
    for (const char *p = from; (p = strchr(p, '<')) != NULL && (!end || p < end); ++p) {
        const char *name = p + 1;
        if (*name == '/' || *name == '?' || *name == '!') continue;
        const char *ne = name;
        while (*ne && !isspace((unsigned char)*ne) && *ne != '>' && *ne != '/') ++ne;
        const char *local = name;
        for (const char *c = name; c < ne; ++c) if (*c == ':') local = c + 1;
        if ((size_t)(ne - local) != tl || strncmp(local, tag, tl) != 0) continue;
        const char *gt = strchr(ne, '>');
        if (!gt) return NULL;
        if (gt[-1] == '/') {                     /* <tag/>: empty */
            *open_end = gt + 1;
            *close = gt + 1;
            return p;
        }
        /* The closing tag, same name with whatever prefix. */
        for (const char *q = gt + 1; (q = strstr(q, "</")) != NULL; q += 2) {
            const char *cn = q + 2, *ce = cn;
            while (*ce && *ce != '>' && !isspace((unsigned char)*ce)) ++ce;
            const char *cl = cn;
            for (const char *c = cn; c < ce; ++c) if (*c == ':') cl = c + 1;
            if ((size_t)(ce - cl) == tl && strncmp(cl, tag, tl) == 0) {
                *open_end = gt + 1;
                *close = q;
                return p;
            }
        }
        return NULL;
    }
    return NULL;
}

static int elem_text(const char *from, const char *end, const char *tag, char *out, int cap) {
    const char *a, *b;
    if (!find_elem(from, end, tag, &a, &b)) return 0;
    trim(&a, &b);
    return copy_n(out, cap, a, (int)(b - a));
}

int acr_upnp_xml_value(const char *xml, const char *tag, char *out, int cap) {
    return xml ? elem_text(xml, NULL, tag, out, cap) : 0;
}

/* ---------------- SSDP ---------------- */

int acr_upnp_msearch(char *out, int cap, const char *st) {
    const int n = snprintf(out, (size_t)cap,
                           "M-SEARCH * HTTP/1.1\r\n"
                           "HOST: 239.255.255.250:1900\r\n"
                           "MAN: \"ssdp:discover\"\r\n"
                           "MX: 2\r\n"
                           "ST: %s\r\n"
                           "\r\n", st);
    return n > 0 && n < cap ? n : 0;
}

int acr_upnp_ssdp_location(const char *reply, int len, char *url, int cap) {
    if (len < 12 || !starts_nocase(reply, "HTTP/1.") || strncmp(reply + 9, "200", 3) != 0) return 0;
    const char *end = reply + len;
    for (const char *line = reply; line < end;) {
        const char *eol = line;
        while (eol < end && *eol != '\r' && *eol != '\n') ++eol;
        if (eol - line > 9 && starts_nocase(line, "LOCATION:")) {
            const char *a = line + 9, *b = eol;
            trim(&a, &b);
            return b > a && copy_n(url, cap, a, (int)(b - a));
        }
        line = eol;
        while (line < end && (*line == '\r' || *line == '\n')) ++line;
    }
    return 0;
}

/* ---------------- URLs ---------------- */

int acr_upnp_parse_url(const char *url, char *host, int host_cap, int *port, char *path, int path_cap) {
    if (!starts_nocase(url, "http://")) return 0;
    const char *h = url + 7, *he = h;
    while (*he && *he != ':' && *he != '/') ++he;
    if (he == h || !copy_n(host, host_cap, h, (int)(he - h))) return 0;
    *port = 80;
    const char *p = he;
    if (*p == ':') {
        char *after;
        const long v = strtol(p + 1, &after, 10);
        if (after == p + 1 || v <= 0 || v > 65535) return 0;
        *port = (int)v;
        p = after;
    }
    if (!*p) return copy_n(path, path_cap, "/", 1);
    if (*p != '/') return 0;
    return copy_n(path, path_cap, p, (int)strlen(p));
}

int acr_upnp_resolve(const char *base, const char *ref, char *out, int cap) {
    if (starts_nocase(ref, "http://")) return copy_n(out, cap, ref, (int)strlen(ref));
    if (!starts_nocase(base, "http://")) return 0;
    const char *host_end = base + 7;
    while (*host_end && *host_end != '/') ++host_end;          /* "http://host:port" */
    int n;
    if (ref[0] == '/') {
        n = snprintf(out, (size_t)cap, "%.*s%s", (int)(host_end - base), base, ref);
    } else {
        /* Relative: replaces the base's last path segment. */
        const char *dir_end = strrchr(host_end, '/');
        if (!dir_end) n = snprintf(out, (size_t)cap, "%.*s/%s", (int)(host_end - base), base, ref);
        else n = snprintf(out, (size_t)cap, "%.*s%s", (int)(dir_end + 1 - base), base, ref);
    }
    return n > 0 && n < cap;
}

/* ---------------- the description ---------------- */

int acr_upnp_find_control(const char *xml, const char *desc_url, char *control, int control_cap,
                          char *service, int service_cap) {
    if (!xml) return 0;
    char base[512];
    if (!elem_text(xml, NULL, "URLBase", base, sizeof base) || !base[0])
        if (!copy_n(base, sizeof base, desc_url, (int)strlen(desc_url))) return 0;
    static const char *const k_want[] = { ":service:WANIPConnection:", ":service:WANPPPConnection:" };
    for (int w = 0; w < 2; ++w) {
        const char *a, *b;
        for (const char *p = xml; find_elem(p, NULL, "service", &a, &b); p = b) {
            char type[160], ctrl[512];
            if (!elem_text(a, b, "serviceType", type, sizeof type) || !strstr(type, k_want[w])) continue;
            if (!elem_text(a, b, "controlURL", ctrl, sizeof ctrl) || !ctrl[0]) continue;
            if (!acr_upnp_resolve(base, ctrl, control, control_cap)) return 0;
            return copy_n(service, service_cap, type, (int)strlen(type));
        }
    }
    return 0;
}

/* ---------------- HTTP ---------------- */

int acr_upnp_http_status(const char *resp, int len) {
    if (len < 12 || !starts_nocase(resp, "HTTP/1.")) return 0;
    const char *sp = memchr(resp, ' ', (size_t)len);
    if (!sp || sp + 4 > resp + len) return 0;
    const int code = atoi(sp + 1);
    return code >= 100 && code <= 599 ? code : 0;
}

char *acr_upnp_http_body(char *resp, int len, int *body_len) {
    char *end = resp + len, *body = NULL;
    for (char *p = resp; p + 3 < end; ++p)
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') { body = p + 4; break; }
    if (!body) {
        for (char *p = resp; p + 1 < end; ++p)
            if (p[0] == '\n' && p[1] == '\n') { body = p + 2; break; }
    }
    if (!body) return NULL;
    /* Chunked? (a header line "Transfer-Encoding: chunked") */
    int chunked = 0;
    for (char *line = resp; line < body;) {
        char *eol = line;
        while (eol < body && *eol != '\n') ++eol;
        if (starts_nocase(line, "Transfer-Encoding:")) {
            for (char *c = line + 18; c + 7 <= eol; ++c)
                if (starts_nocase(c, "chunked")) chunked = 1;
        }
        line = eol + 1;
    }
    if (!chunked) {
        *body_len = (int)(end - body);
        *end = 0;   /* callers leave a byte spare past len */
        return body;
    }
    char *in = body, *out = body;
    for (;;) {
        char *after;
        const long n = strtol(in, &after, 16);
        if (after == in || n < 0) break;
        char *data = strchr(after, '\n');
        if (!data) break;
        ++data;
        if (n == 0 || data + n > end) break;
        memmove(out, data, (size_t)n);
        out += n;
        in = data + n;
        while (in < end && (*in == '\r' || *in == '\n')) ++in;
    }
    *out = 0;
    *body_len = (int)(out - body);
    return body;
}

/* ---------------- SOAP ---------------- */

static int soap(char *out, int cap, const char *service, const char *action, const char *args) {
    const int n = snprintf(out, (size_t)cap,
                           "<?xml version=\"1.0\"?>\r\n"
                           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                           "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                           "<s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
                           action, service, args, action);
    return n > 0 && n < cap ? n : 0;
}

int acr_upnp_soap_external_ip(char *out, int cap, const char *service) {
    return soap(out, cap, service, "GetExternalIPAddress", "");
}

int acr_upnp_soap_add(char *out, int cap, const char *service, int ext_port, int int_port,
                      const char *client_ip, int lease_s, const char *what) {
    char args[512];
    const int n = snprintf(args, sizeof args,
                           "<NewRemoteHost></NewRemoteHost>"
                           "<NewExternalPort>%d</NewExternalPort>"
                           "<NewProtocol>UDP</NewProtocol>"
                           "<NewInternalPort>%d</NewInternalPort>"
                           "<NewInternalClient>%s</NewInternalClient>"
                           "<NewEnabled>1</NewEnabled>"
                           "<NewPortMappingDescription>%s</NewPortMappingDescription>"
                           "<NewLeaseDuration>%d</NewLeaseDuration>",
                           ext_port, int_port, client_ip, what, lease_s);
    if (n <= 0 || n >= (int)sizeof args) return 0;
    return soap(out, cap, service, "AddPortMapping", args);
}

int acr_upnp_soap_delete(char *out, int cap, const char *service, int ext_port) {
    char args[200];
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>",
             ext_port);
    return soap(out, cap, service, "DeletePortMapping", args);
}

int acr_upnp_soap_error(const char *body) {
    char v[16];
    if (!body || !acr_upnp_xml_value(body, "errorCode", v, sizeof v)) return 0;
    return atoi(v);
}

/* ---------------- addresses ---------------- */

int acr_upnp_parse_ip(const char *text, uint8_t ip[4]) {
    const char *p = text;
    for (int i = 0; i < 4; ++i) {
        if (!isdigit((unsigned char)*p)) return 0;
        char *after;
        const long v = strtol(p, &after, 10);
        if (v < 0 || v > 255 || after - p > 3) return 0;
        ip[i] = (uint8_t)v;
        p = after;
        if (i < 3) {
            if (*p != '.') return 0;
            ++p;
        }
    }
    while (isspace((unsigned char)*p)) ++p;
    return *p == 0;
}

int acr_upnp_is_public(const uint8_t ip[4]) {
    if (ip[0] == 0 || ip[0] == 10 || ip[0] == 127 || ip[0] >= 224) return 0;
    if (ip[0] == 100 && (ip[1] & 0xC0) == 64) return 0;
    if (ip[0] == 169 && ip[1] == 254) return 0;
    if (ip[0] == 172 && (ip[1] & 0xF0) == 16) return 0;
    if (ip[0] == 192 && ip[1] == 168) return 0;
    return 1;
}
