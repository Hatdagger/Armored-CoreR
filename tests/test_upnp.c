/* UPnP text side: SSDP answers, URLs, router descriptions (namespaces,
 * URLBase, relative control URLs, PPP-only routers), HTTP bodies sent in
 * chunks, SOAP requests and faults, and which addresses count as internet
 * ones. The descriptions are trimmed copies of the shapes real routers send
 * (miniupnpd, a Fritz!Box-style PPP router, an ISP box with URLBase). */
#include <stdio.h>
#include <string.h>

#include "acr_upnp.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const char k_miniupnpd[] =
    "<?xml version=\"1.0\"?>\n<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
    "<specVersion><major>1</major><minor>0</minor></specVersion>"
    "<device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
    "<serviceList><service><serviceType>urn:schemas-microsoft-com:service:OSInfo:1</serviceType>"
    "<controlURL>/ctl/OSInfo</controlURL></service></serviceList>"
    "<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>"
    "<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType>"
    "<controlURL>/ctl/CmnIfCfg</controlURL></service></serviceList>"
    "<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>"
    "<serviceList><service>\n  <serviceType> urn:schemas-upnp-org:service:WANIPConnection:1 </serviceType>\n"
    "  <serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>\n  <controlURL>/ctl/IPConn</controlURL>\n"
    "</service></serviceList></device></deviceList></device></deviceList></device></root>";

static const char k_ppp_relative[] =
    "<root><device><serviceList>"
    "<service><serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
    "<controlURL>upnp/control/WANPPPConn1</controlURL></service>"
    "</serviceList></device></root>";

static const char k_urlbase_prefixed[] =
    "<ns0:root xmlns:ns0=\"urn:schemas-upnp-org:device-1-0\"><ns0:URLBase>http://192.168.0.1:5000</ns0:URLBase>"
    "<ns0:device><ns0:serviceList>"
    "<ns0:service><ns0:serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</ns0:serviceType>"
    "<ns0:controlURL>/ppp</ns0:controlURL></ns0:service>"
    "<ns0:service><ns0:serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</ns0:serviceType>"
    "<ns0:controlURL>/ip2</ns0:controlURL></ns0:service>"
    "</ns0:serviceList></ns0:device></ns0:root>";

int main(void) {
    char a[512], b[512];
    int port;

    /* SSDP */
    EXPECT(acr_upnp_msearch(a, sizeof a, "urn:schemas-upnp-org:device:InternetGatewayDevice:1") > 0);
    EXPECT(strstr(a, "M-SEARCH * HTTP/1.1\r\n") == a && strstr(a, "MAN: \"ssdp:discover\"") && strstr(a, "\r\n\r\n"));
    EXPECT(acr_upnp_msearch(a, 20, "x") == 0);
    {
        const char r[] = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:x\r\n"
                         "location:   http://192.168.1.1:5431/dyndev/uuid:0000e068\r\nSERVER: Linux UPnP/1.0\r\n\r\n";
        EXPECT(acr_upnp_ssdp_location(r, (int)strlen(r), a, sizeof a) && !strcmp(a, "http://192.168.1.1:5431/dyndev/uuid:0000e068"));
        const char notify[] = "NOTIFY * HTTP/1.1\r\nLOCATION: http://x/\r\n\r\n";
        EXPECT(!acr_upnp_ssdp_location(notify, (int)strlen(notify), a, sizeof a));
        const char bad[] = "HTTP/1.1 404 Not Found\r\nLOCATION: http://x/\r\n\r\n";
        EXPECT(!acr_upnp_ssdp_location(bad, (int)strlen(bad), a, sizeof a));
        const char none[] = "HTTP/1.1 200 OK\r\nST: urn:x\r\n\r\n";
        EXPECT(!acr_upnp_ssdp_location(none, (int)strlen(none), a, sizeof a));
    }

    /* URLs */
    EXPECT(acr_upnp_parse_url("http://192.168.1.1:5431/dyndev/x", a, sizeof a, &port, b, sizeof b) &&
           !strcmp(a, "192.168.1.1") && port == 5431 && !strcmp(b, "/dyndev/x"));
    EXPECT(acr_upnp_parse_url("http://router.lan", a, sizeof a, &port, b, sizeof b) && port == 80 && !strcmp(b, "/"));
    EXPECT(!acr_upnp_parse_url("https://x/", a, sizeof a, &port, b, sizeof b));
    EXPECT(!acr_upnp_parse_url("http://x:0/", a, sizeof a, &port, b, sizeof b));
    EXPECT(!acr_upnp_parse_url("http://x:99999/", a, sizeof a, &port, b, sizeof b));
    EXPECT(acr_upnp_resolve("http://10.0.0.1:80/desc/root.xml", "/ctl/x", a, sizeof a) && !strcmp(a, "http://10.0.0.1:80/ctl/x"));
    EXPECT(acr_upnp_resolve("http://10.0.0.1:80/desc/root.xml", "ctl/x", a, sizeof a) && !strcmp(a, "http://10.0.0.1:80/desc/ctl/x"));
    EXPECT(acr_upnp_resolve("http://10.0.0.1:80", "ctl/x", a, sizeof a) && !strcmp(a, "http://10.0.0.1:80/ctl/x"));
    EXPECT(acr_upnp_resolve("http://10.0.0.1/", "http://10.0.0.2:9/c", a, sizeof a) && !strcmp(a, "http://10.0.0.2:9/c"));

    /* Descriptions */
    EXPECT(acr_upnp_find_control(k_miniupnpd, "http://192.168.1.1:5000/rootDesc.xml", a, sizeof a, b, sizeof b));
    EXPECT(!strcmp(a, "http://192.168.1.1:5000/ctl/IPConn"));
    EXPECT(!strcmp(b, "urn:schemas-upnp-org:service:WANIPConnection:1"));
    EXPECT(acr_upnp_find_control(k_ppp_relative, "http://192.168.178.1:49000/igddesc.xml", a, sizeof a, b, sizeof b));
    EXPECT(!strcmp(a, "http://192.168.178.1:49000/upnp/control/WANPPPConn1"));
    EXPECT(!strcmp(b, "urn:schemas-upnp-org:service:WANPPPConnection:1"));
    /* IP connection preferred over PPP even when listed second; URLBase wins. */
    EXPECT(acr_upnp_find_control(k_urlbase_prefixed, "http://192.168.0.1:80/x.xml", a, sizeof a, b, sizeof b));
    EXPECT(!strcmp(a, "http://192.168.0.1:5000/ip2"));
    EXPECT(!strcmp(b, "urn:schemas-upnp-org:service:WANIPConnection:2"));
    EXPECT(!acr_upnp_find_control("<root><device/></root>", "http://x/", a, sizeof a, b, sizeof b));

    /* HTTP bodies */
    {
        char r[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
        int n = 0;
        char *body = acr_upnp_http_body(r, (int)strlen(r), &n);
        EXPECT(acr_upnp_http_status(r, (int)strlen(r)) == 200);
        EXPECT(body && n == 5 && !strcmp(body, "hello"));
    }
    {
        char r[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n<roo\r\n6;x=y\r\nt>a</r\r\n4\r\noot>\r\n0\r\n\r\n";
        int n = 0;
        char *body = acr_upnp_http_body(r, (int)strlen(r), &n);
        EXPECT(body && !strcmp(body, "<root>a</root>") && n == 14);
    }
    EXPECT(acr_upnp_http_status("garbage here", 12) == 0);

    /* SOAP */
    char soap[1400];
    EXPECT(acr_upnp_soap_add(soap, sizeof soap, "urn:schemas-upnp-org:service:WANIPConnection:1", 7777, 7777, "192.168.1.20", 7200, "Armored-CoreR"));
    EXPECT(strstr(soap, "<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"));
    EXPECT(strstr(soap, "<NewExternalPort>7777</NewExternalPort>") && strstr(soap, "<NewProtocol>UDP</NewProtocol>"));
    EXPECT(strstr(soap, "<NewInternalClient>192.168.1.20</NewInternalClient>") && strstr(soap, "<NewLeaseDuration>7200</NewLeaseDuration>"));
    EXPECT(strstr(soap, "</u:AddPortMapping></s:Body></s:Envelope>"));
    EXPECT(acr_upnp_soap_add(a, 100, "urn:x", 1, 1, "1.2.3.4", 0, "x") == 0);   /* too small: refused, not cut */
    EXPECT(acr_upnp_soap_delete(a, sizeof a, "urn:x", 30001) && strstr(a, "<NewExternalPort>30001</NewExternalPort>"));
    EXPECT(acr_upnp_soap_external_ip(a, sizeof a, "urn:x") && strstr(a, "<u:GetExternalIPAddress xmlns:u=\"urn:x\"></u:GetExternalIPAddress>"));
    {
        const char fault[] = "<s:Envelope><s:Body><s:Fault><faultcode>s:Client</faultcode><detail>"
                             "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>718</errorCode>"
                             "<errorDescription>ConflictInMappingEntry</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>";
        EXPECT(acr_upnp_soap_error(fault) == ACR_UPNP_ERR_CONFLICT);
        EXPECT(acr_upnp_soap_error("<ok/>") == 0);
        const char ext[] = "<s:Envelope><s:Body><u:GetExternalIPAddressResponse xmlns:u=\"urn:x\">"
                           "<NewExternalIPAddress>203.0.113.9</NewExternalIPAddress></u:GetExternalIPAddressResponse></s:Body></s:Envelope>";
        EXPECT(acr_upnp_xml_value(ext, "NewExternalIPAddress", a, sizeof a) && !strcmp(a, "203.0.113.9"));
        EXPECT(acr_upnp_xml_value("<m:NewExternalIPAddress/>", "NewExternalIPAddress", a, sizeof a) && !a[0]);
    }

    /* Addresses */
    {
        uint8_t ip[4];
        EXPECT(acr_upnp_parse_ip("203.0.113.9", ip) && ip[0] == 203 && ip[3] == 9 && acr_upnp_is_public(ip));
        EXPECT(!acr_upnp_parse_ip("1.2.3", ip) && !acr_upnp_parse_ip("1.2.3.256", ip) && !acr_upnp_parse_ip("", ip));
        static const char *const k_not[] = { "10.1.2.3", "172.16.0.1", "172.31.255.1", "192.168.1.1", "100.64.0.1",
                                             "100.127.0.1", "127.0.0.1", "169.254.3.3", "0.0.0.0", "224.0.0.1" };
        for (int i = 0; i < 10; ++i) EXPECT(acr_upnp_parse_ip(k_not[i], ip) && !acr_upnp_is_public(ip));
        static const char *const k_yes[] = { "172.15.0.1", "172.32.0.1", "100.63.0.1", "100.128.0.1", "8.8.8.8" };
        for (int i = 0; i < 5; ++i) EXPECT(acr_upnp_parse_ip(k_yes[i], ip) && acr_upnp_is_public(ip));
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("upnp: all good\n");
    return 0;
}
