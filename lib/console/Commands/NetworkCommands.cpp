#include "NetworkCommands.h"

#include <esp_ping.h>
#include <ping/ping.h>
#include <ping/ping_sock.h>
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/udp.h"
#include <unistd.h>
#include <esp_wifi.h>
#include <esp_crc.h>

//#include "../device/fuji.h"
#include "fnWiFi.h"

#include "string_utils.h"

#include "../../include/debug.h"

static const char* wlmode2string(wifi_mode_t mode)
{
    switch(mode) {
        case WIFI_MODE_NULL:
            return "Not initialized";
        case WIFI_MODE_AP:
            return "Accesspoint";
        case WIFI_MODE_STA:
            return "Station";
        case WIFI_MODE_APSTA:
            return "Station + Accesspoint";
        default:
            return "Unknown";
    }
}


// Ping Functions
static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    //Debug_printv("on_ping_success");
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    Serial.printf("%lu bytes from %s: icmp_seq=%d ttl=%d time=%lu ms\r\n",
           recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    //Debug_printv("on_ping_timeout");
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    Serial.printf("From %s icmp_seq=%u timeout\r\n", inet_ntoa(target_addr.u_addr.ip4), seqno);
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    //Debug_printv("on_ping_end");
    ip_addr_t target_addr;
	uint32_t transmitted;
	uint32_t received;
	uint32_t total_time_ms;
	esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
	esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
	uint32_t loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
	if (IP_IS_V4(&target_addr)) {
		Serial.printf("\r\n--- %s ping statistics", inet_ntoa(*ip_2_ip4(&target_addr)));
	} else {
		Serial.printf("\r\n--- %s ping statistics", inet6_ntoa(*ip_2_ip6(&target_addr)));
	}
	Serial.printf(" ---\r\n%lu packets transmitted, %lu received, %lu%% packet loss, time %lums\r\n",
			 transmitted, received, loss, total_time_ms);
	// delete the ping sessions, so that we clean up all resources and can create a new ping session
	// we don't have to call delete function in the callback, instead we can call delete function from other tasks
	// esp_ping_delete_session(hdl);
}

static int ping(int argc, char **argv)
{
    //By default do 5 pings
    int number_of_pings = 5;

    int opt;
    while ((opt = getopt(argc, argv, "n:")) != -1) {
        switch(opt) {
            case 'n':
                number_of_pings = atoi(optarg);
                break;
            case '?':
                Serial.printf("Unknown option: %c\r\n", optopt);
                break;
            case ':':
                Serial.printf("Missing arg for %c\r\n", optopt);
                break;

            default:
                Serial.printf("Usage: ping -n 5 [HOSTNAME]\r\n");
                Serial.printf("-n: The number of pings. 0 means infinite. Can be aborted with Ctrl+D or Ctrl+C.");
                return 1;
        }
    }

    int argind = optind;

    //Get hostname
    if (argind >= argc) {
        Serial.printf("You need to pass an hostname!\r\n");
        return EXIT_FAILURE;
    }

    char* hostname = argv[argind];

    /* convert hostname to IP address */
    ip_addr_t target_addr;
    struct addrinfo hint;
    struct addrinfo *res = NULL;
    memset(&hint, 0, sizeof(hint));
    memset(&target_addr, 0, sizeof(target_addr));
    auto result = getaddrinfo(hostname, NULL, &hint, &res);

    if (result) {
        Serial.printf("Could not resolve hostname! (getaddrinfo returned %d)\r\n", result);
        return 1;
    }

    struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
    freeaddrinfo(res);

    //Configure ping session
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.task_stack_size = 4096;
    ping_config.target_addr = target_addr;          // target IP address
    ping_config.count = number_of_pings;   // 0 means infinite ping

    /* set callback functions */
    esp_ping_callbacks_t cbs;
    cbs.on_ping_success = on_ping_success;
    cbs.on_ping_timeout = on_ping_timeout;
    cbs.on_ping_end = on_ping_end;
    //Pass a variable as pointer so the sub tasks can decrease it
    //cbs.cb_args = &number_of_pings_remaining;

    esp_ping_handle_t ping;
    esp_ping_new_session(&ping_config, &cbs, &ping);

    Serial.printf("PING %s (%s) 56(84) bytes of data.\r\n", hostname, inet_ntoa(*ip_2_ip4(&target_addr)));
    esp_ping_start(ping);

    uint16_t seqno;
    esp_ping_get_profile(ping, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    
    //Make stdin input non blocking so we can query for input AND check ping seqno

    //Wait for Ctrl+D or Ctr+C or that our task finishes
    //The async tasks decrease number_of_pings, so wait for it to get to 0
    //Wait for Ctrl+D or Ctr+C or that our task finishes
    //while((number_of_pings == 0 || seqno < number_of_pings) && c != 4 && c != 3) {
    while((seqno < number_of_pings)) {
        esp_ping_get_profile(ping, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
        //c = getc(stdin);
        sleep(1);
        //Debug_printv("number_of_pings[%d] seqno[%d]\r\n", number_of_pings, seqno);
    }

    //Reset flags, so we dont end up destroying our terminal env later, when linenoise takes over again

    //Print total statistics
    esp_ping_delete_session(ping);
    esp_ping_stop(ping);
    //Debug_printv("Done! number_of_pings[%d] seqno[%d]\r\n", number_of_pings, seqno);

    return EXIT_SUCCESS;
}


static void ipconfig_wlan()
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    Serial.printf("Mode: %s\r\n", wlmode2string(mode));

    bool is_connected = fnWiFi.connected();
    Serial.printf("Status: %s\r\n", is_connected ? "Connected" : "Disconnected");

    if (!is_connected) {
        return;
    }

    Serial.printf("\r\n");
    Serial.printf("SSID: %s\r\n", fnWiFi.get_current_ssid().c_str());
    Serial.printf("BSSID: %s\r\n", fnWiFi.get_current_bssid_str().c_str());

    Serial.printf("\r\n");
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(fnWiFi.get_adapter_handle(), &ip_info);
    Serial.printf("IP: " IPSTR "\r\n", IP2STR(&ip_info.ip));
    Serial.printf("Subnet Mask: " IPSTR "\r\n", IP2STR(&ip_info.netmask));
    Serial.printf("Gateway: " IPSTR "\r\n", IP2STR(&ip_info.gw));

    esp_ip6_addr_t ip6;
    if (esp_netif_get_ip6_linklocal(fnWiFi.get_adapter_handle(), &ip6) == ESP_OK) {
        Serial.printf("IPv6: " IPV6STR "\r\n", IPV62STR(ip6));
    }

    Serial.printf("\r\n");
    const char *hostname = nullptr;
    esp_netif_get_hostname(fnWiFi.get_adapter_handle(), &hostname);
    Serial.printf("Hostname: %s\r\n", hostname ? hostname : "");

    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(fnWiFi.get_adapter_handle(), ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
        Serial.printf("DNS1: " IPSTR "\r\n", IP2STR(&dns.ip.u_addr.ip4));
    }
    if (esp_netif_get_dns_info(fnWiFi.get_adapter_handle(), ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK) {
        Serial.printf("DNS2: " IPSTR "\r\n", IP2STR(&dns.ip.u_addr.ip4));
    }
}

static int ifconfig(int argc, char **argv)
{
    ipconfig_wlan();
    return EXIT_SUCCESS;
}

static const char *tcp_state_str(enum tcp_state s)
{
    switch (s) {
        case CLOSED:      return "CLOSED";
        case LISTEN:      return "LISTEN";
        case SYN_SENT:    return "SYN_SENT";
        case SYN_RCVD:    return "SYN_RCVD";
        case ESTABLISHED: return "ESTABLISHED";
        case FIN_WAIT_1:  return "FIN_WAIT_1";
        case FIN_WAIT_2:  return "FIN_WAIT_2";
        case CLOSE_WAIT:  return "CLOSE_WAIT";
        case CLOSING:     return "CLOSING";
        case LAST_ACK:    return "LAST_ACK";
        case TIME_WAIT:   return "TIME_WAIT";
        default:          return "UNKNOWN";
    }
}

static void fmt_addr(char *buf, size_t len, const ip_addr_t *addr, u16_t port)
{
    if (ip_addr_isany(addr))
        snprintf(buf, len, "0.0.0.0:%u", port);
    else {
        char ip[INET6_ADDRSTRLEN];
        ipaddr_ntoa_r(addr, ip, sizeof(ip));
        snprintf(buf, len, "%s:%u", ip, port);
    }
}

static int netstat(int argc, char **argv)
{
    char local[32], remote[32];

    Serial.printf("Proto  %-24s %-24s State\r\n", "Local Address", "Foreign Address");

    LOCK_TCPIP_CORE();

    for (struct tcp_pcb_listen *p = tcp_listen_pcbs.listen_pcbs; p; p = p->next) {
        fmt_addr(local, sizeof(local), &p->local_ip, p->local_port);
        Serial.printf("tcp    %-24s %-24s LISTEN\r\n", local, "*:*");
    }

    for (struct tcp_pcb *p = tcp_active_pcbs; p; p = p->next) {
        fmt_addr(local, sizeof(local), &p->local_ip, p->local_port);
        fmt_addr(remote, sizeof(remote), &p->remote_ip, p->remote_port);
        Serial.printf("tcp    %-24s %-24s %s\r\n", local, remote, tcp_state_str(p->state));
    }

    for (struct tcp_pcb *p = tcp_tw_pcbs; p; p = p->next) {
        fmt_addr(local, sizeof(local), &p->local_ip, p->local_port);
        fmt_addr(remote, sizeof(remote), &p->remote_ip, p->remote_port);
        Serial.printf("tcp    %-24s %-24s TIME_WAIT\r\n", local, remote);
    }

    for (struct udp_pcb *p = udp_pcbs; p; p = p->next) {
        fmt_addr(local, sizeof(local), &p->local_ip, p->local_port);
        if (!ip_addr_isany(&p->remote_ip) && p->remote_port != 0)
            fmt_addr(remote, sizeof(remote), &p->remote_ip, p->remote_port);
        else
            strcpy(remote, "*:*");
        Serial.printf("udp    %-24s %-24s\r\n", local, remote);
    }

    UNLOCK_TCPIP_CORE();

    return EXIT_SUCCESS;
}

static int scan(int argc, char **argv)
{
    fnWiFi.scan_networks();
    Serial.printf("Found following networks:\r\n");
    std::vector<std::string> network_names = fnWiFi.get_network_names();
    for (std::string _network_name: network_names)
    {
        uint8_t c_crc8 = esp_crc8_le(0, (uint8_t *)_network_name.c_str(), _network_name.length());
        Serial.printf("[%03d] - %s\r\n", c_crc8, _network_name.c_str());
    }
    return EXIT_SUCCESS;
}

static int connect(int argc, char **argv)
{
    if (argc == 3)
    {
        std::string network = argv[1];
        if (mstr::isNumeric(network)) {
            // Find SSID by CRC8 Number
            network = fnWiFi.get_network_name_by_crc8(std::stoi(argv[1]));
        }
        int e = ( fnWiFi.connect(network.c_str(), argv[2]) );

        if ( e == ESP_OK)
            fnWiFi.store_wifi(network, argv[2]);

        return e;
    }

    return fnWiFi.connect();
}



namespace ESP32Console::Commands
{
    const ConsoleCommand getPingCommand()
    {
        return ConsoleCommand("ping", &ping, "Ping host");
    }

    const ConsoleCommand getIfconfigCommand()
    {
        return ConsoleCommand("ifconfig", &ifconfig, "Show network interface configuration");
    }

    const ConsoleCommand getNetstatCommand()
    {
        return ConsoleCommand("netstat", &netstat, "List open sockets and connections");
    }

    const ConsoleCommand getScanCommand()
    {
        return ConsoleCommand("scan", &scan, "Scan for wifi networks");
    }

    const ConsoleCommand getConnectCommand()
    {
        return ConsoleCommand("connect", &connect, "Connect to wifi");
    }
}
