#ifndef MEATFILESYSTEM_SCHEME_TCP
#define MEATFILESYSTEM_SCHEME_TCP

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "../../include/debug.h"

class MeatSocketClient {
    int sock = -1;

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
        if(!isOpen())
            return -1;
        return recv(sock, buffer, bufsize, MSG_DONTWAIT); // MSG_DONTWAIT instead of 0 switches to non-blocking mode
    }

    bool isOpen() {
        return sock != -1;
    }
};

class MeatSocketServer {

};

#endif /* MEATFILESYSTEM_SCHEME_TCP */
