#ifndef MEATFILESYSTEM_SCHEME_TCP
#define MEATFILESYSTEM_SCHEME_TCP

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "../../include/debug.h"

class MeatSocket {
    int sock = -1;
    uint8_t iecPort = 0;

public:
    MeatSocket() {};
    MeatSocket(int s) : sock(s) {
        // for socket created by our server
    }

    bool open(const char *address, u16_t port) {
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
        dest_addr.sin_addr.s_addr = inet_addr(address);
        Debug_printv("dest_addr.sin_addr.s_addr=%x", dest_addr.sin_addr.s_addr);
        if (dest_addr.sin_addr.s_addr == 0xffffffff) {
            struct hostent *hp;
            hp = gethostbyname(address);
            if (hp == NULL) {
                Debug_printv("FTP Client Error: Connect to %s", address);
                return false;
            }
            struct ip4_addr *ip4_addr;
            ip4_addr = (struct ip4_addr *)hp->h_addr;
            dest_addr.sin_addr.s_addr = ip4_addr->addr;
        }
        
        sock =	socket(AF_INET, SOCK_STREAM, IPPROTO_IP); // SCOK_STREAM = TCP/IP SOCK_DGRAM = UDP
        if (sock < 0) {
            Debug_printv("Unable to create socket: errno %d", errno);
            return false;
        }
        Debug_printv("Socket created, connecting to %s:%d", address, port);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0) {
            Debug_printv("Socket unable to connect: errno %d", errno);
            return false;
        }
    }

    void close() {
        closesocket(sock);
        shutdown(sock, 0);

        sock = -1;
    }

    size_t write(const void* buffer, size_t bufsize) {
        if(!isOpen())
            return -1;
        return send(sock, buffer, bufsize, 0);
    }

    int read(void* buffer, size_t bufsize) {
        // might work in non-blocking mode. In this mode recv returns
        // BSD_ERROR_WOULDBLOCK and then we can poll again, that's what we want
        // TODO - check what's the value of BSD_ERROR_WOULDBLOCK and if recv returns
        // error - mark this socket closed
        if(!isOpen())
            return -1;
        return recv(sock, buffer, bufsize, MSG_DONTWAIT); // MSG_DONTWAIT instead of 0 switches to non-blocking mode
    }

    bool isOpen() {
        return sock != -1;
    }
};

class MeatSocketServer {
    bool isAlive = false;
    int port = 0;

    void start(int p) {
        port = p;
        xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)this, 5, NULL);
    }

    void shutdown() {
        // openSockets.foreach { it.close() }
        // openScokets.clear()
        isAlive = false;
    }

    static void tcp_server_task(void *param)
    {
        MeatSocketServer* meatServer = (MeatSocketServer*)param;
        struct sockaddr_storage dest_addr;
        int ip_protocol = 0;
        int keepAlive = 1;
        int keepIdle = 10;
        int keepInterval = 5;
        int keepCount = 5;
        struct sockaddr_storage dest_addr;
        char addr_str[128];
        
        int port = meatServer->port;

        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(port);
        ip_protocol = IPPROTO_IP;

        int listen_sock = socket(AF_INET, SOCK_STREAM, ip_protocol);
        if (listen_sock < 0) {
            Debug_printv("Unable to create socket: errno %d", errno);
            vTaskDelete(NULL);
            return;
        }
        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        Debug_printv("Socket created");

        int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            Debug_printv("Socket unable to bind: errno %d", errno);
            Debug_printv("IPPROTO: %d", AF_INET);
            goto CLEAN_UP;
        }
        Debug_printv("Socket bound, port %d", port);

        err = listen(listen_sock, 1);
        if (err != 0) {
            Debug_printv("Error occurred during listen: errno %d", errno);
            goto CLEAN_UP;
        }

        meatServer->isAlive = true;

        while (meatServer->isAlive) {

            Debug_printv("Socket listening");

            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t addr_len = sizeof(source_addr);
            int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (sock < 0) {
                Debug_printv("Unable to accept connection: errno %d", errno);
                break;
            }

            // Set tcp keepalive option
            setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
            // Convert ip address to string
            if (source_addr.ss_family == PF_INET) {
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
            }
    #ifdef CONFIG_EXAMPLE_IPV6
            else if (source_addr.ss_family == PF_INET6) {
                inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
            }
    #endif
            Debug_printv("Socket accepted ip address: %s", addr_str);

            // do_retransmit(sock);
            //
            // we'll do this here instead:
            // auto newSock = new MeatSocket(sock);
            // openSockets.Add(newSock);
            // purgeClosedSockets();


        }

    CLEAN_UP:
        close(listen_sock);
        vTaskDelete(NULL);
    }
};

#endif /* MEATFILESYSTEM_SCHEME_TCP */
