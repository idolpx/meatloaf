// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// TCP:// - Transmission Control Protocol
// https://en.wikipedia.org/wiki/Transmission_Control_Protocol
//

#ifndef MEATLOAF_SCHEME_TCP
#define MEATLOAF_SCHEME_TCP

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <fcntl.h>
#include <esp_timer.h>

#include "meatloaf.h"
#include "meat_session.h"

#include "../../include/debug.h"

//
// This is a standard "reading socket" - i.e. if you connect to a remote server
//
class MeatSocket {
    int sock = -1;
    uint8_t iecPort = 0;
    bool blocking = false;

private:
    // A connect bounded by timeout_ms. Returns 0 on success and -1 on failure
    // with errno set to something worth logging (ETIMEDOUT when the bound
    // expired), so the caller's existing failure path reads the same either way.
    //
    // The descriptor is restored to BLOCKING on every exit, success included.
    // write() calls send() with no flags, so a descriptor left O_NONBLOCK would
    // answer EAGAIN instead of blocking -- and only on dials that set a bound,
    // which is the worst shape of divergence: the default path stays correct
    // and nobody notices.
    int connectBounded(struct sockaddr_in &dest_addr, uint32_t timeout_ms)
    {
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            // Cannot go non-blocking. Dial unbounded rather than refuse to dial
            // at all -- an unbounded connect is what this did before.
            return connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        }

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err == 0)
        {
            fcntl(sock, F_SETFL, flags);
            return 0;
        }

        // A refused port answers HERE, immediately, not through select() --
        // loopback does exactly that with errno 104. Only EINPROGRESS means
        // "ask again later"; anything else is the real answer already, and
        // waiting out the bound would turn a one-second failure into a slow one
        // and replace its errno with whatever SO_ERROR reports.
        if (errno != EINPROGRESS)
        {
            int saved = errno;
            fcntl(sock, F_SETFL, flags);
            errno = saved;
            return -1;
        }

        // An ABSOLUTE deadline, because select() may return early: one call is
        // not the bound, it has to be re-armed with the time that is left.
        int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
        for (;;)
        {
            int64_t left = deadline - esp_timer_get_time();
            if (left <= 0)
            {
                fcntl(sock, F_SETFL, flags);
                errno = ETIMEDOUT;
                return -1;
            }

            struct timeval tv;
            tv.tv_sec = (time_t)(left / 1000000);
            tv.tv_usec = (suseconds_t)(left % 1000000);

            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);

            // nfds is the highest descriptor PLUS ONE. Passing sock alone never
            // reports ready, which presents as the bound always firing.
            int r = select(sock + 1, NULL, &wfds, NULL, &tv);
            if (r > 0)
            {
                int so_err = 0;
                socklen_t len = sizeof(so_err);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_err, &len) < 0)
                    so_err = errno;
                fcntl(sock, F_SETFL, flags);
                if (so_err != 0)
                {
                    errno = so_err;
                    return -1;
                }
                return 0;
            }
            if (r == 0 || errno == EINTR)
                continue;  // expired or woken early: re-arm with what is left

            int saved = errno;
            fcntl(sock, F_SETFL, flags);
            errno = saved;
            return -1;
        }
    }

public:
    MeatSocket() {};
    MeatSocket(int s, uint8_t iecp) : sock(s), iecPort(iecp) {
        // for socket created by our server
    }

    // timeout_ms bounds the connect. Zero means UNBOUNDED, which is exactly
    // what every caller got before this parameter existed -- real modems split
    // on what S7=0 should mean, and preserving the old behaviour is the reading
    // that cannot regress anything. Only the modem passes a non-zero value.
    bool open(const char *address, u16_t port, uint32_t timeout_ms = 0) {
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
        dest_addr.sin_addr.s_addr = inet_addr(address);
        //Debug_printv("dest_addr.sin_addr.s_addr=%x", dest_addr.sin_addr.s_addr);
        if (dest_addr.sin_addr.s_addr == 0xffffffff) {
            struct hostent *hp;
            hp = gethostbyname(address);
            if (hp == NULL) {
                Debug_printv("TCP Client Error: Connect to %s", address);
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
        //Debug_printv("Socket created, connecting to %s:%d (%x)", address, port, dest_addr.sin_addr.s_addr);

        int err = (timeout_ms == 0)
                      ? connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6))
                      : connectBounded(dest_addr, timeout_ms);

        if (err != 0) {
            Debug_printv("Socket unable to connect: errno %d", errno);
            // Close it here, because nothing else will. TCPMSession::disconnect()
            // returns early unless `connected` is set, and a failed connect()
            // never sets it -- so the session destructor does not clean this up
            // and every failed dial cost a descriptor permanently. Measured on a
            // freenove-esp32-s3-wroom-1: eleven refused dials exhausted all 16
            // sockets (CONFIG_LWIP_MAX_SOCKETS) and the twelfth could not create
            // one at all (errno 23), leaving the board unable to open any
            // connection until it was rebooted.
            //
            // Resetting sock matters as much as closing it: isOpen() is
            // `sock != -1`, so the object would otherwise report itself OPEN on a
            // dead descriptor. closesocket() directly rather than the member
            // close(), because shutdown() is meaningless on a connection that was
            // never established.
            closesocket(sock);
            sock = -1;
            return false;
        }
        Debug_printv("After connect for socket");

        return true;
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
        if(!isOpen()) {
            Debug_println("tcp read - NOT OPEN!\r\n");
            return -100;
        }
        //Debug_printv("tcp::read - calling recv, buff!=null:%d, buffsize=%d, blocking=%d", buffer!=nullptr, bufsize, blocking);
        int byteCount = recv(sock, buffer, bufsize, (blocking) ? 0 : MSG_DONTWAIT); 
        //Debug_printv("tcp::read - post recv");
        if(!blocking && byteCount == -1) {
            return _MEAT_NO_DATA_AVAIL;
        }

        return byteCount;
    }

    bool isOpen() {
        return sock != -1;
    }
};

/********************************************************
 * MSession - TCP Session Management
 ********************************************************/

class TCPMSession : public MSession {
public:
    TCPMSession(std::string host, uint16_t port = 0)
        : MSession("tcp://" + host + ":" + std::to_string(port), host, port)
    {
        Debug_printv("TCPMSession created for %s:%d", host.c_str(), port);
    }
    ~TCPMSession() override {
        Debug_printv("TCPMSession destroyed for %s:%d", host.c_str(), port);
        disconnect();
    }

    // Get the scheme for this session type
    static std::string getScheme() { return "tcp"; }

    bool connect() override {
        if (connected) return true;
        if (port == 0) {
            Debug_printv("TCPMSession connect failed: port is 0 for host %s", host.c_str());
            connected = false;
            return false;
        }

        if (!_socket.open(host.c_str(), port, connect_timeout_ms)) {
            Debug_printv("TCPMSession connect failed for %s:%d", host.c_str(), port);
            connected = false;
            return false;
        }

        connected = true;
        updateActivity();
        return true;
    }

    void disconnect() override {
        if (!connected) return;
        if (_socket.isOpen()) {
            _socket.close();
        }
        connected = false;
    }

    bool keep_alive() override {
        if (!connected) return false;
        if (!_socket.isOpen()) {
            connected = false;
            return false;
        }
        updateActivity();
        return true;
    }

    MeatSocket* socket() { return &_socket; }

private:
    MeatSocket _socket;
};

//
// This is a local server socket
// It waits for a connection and then opens a new "reading socket" for exclusive communication with anyone that connects to ML
//
class MeatSocketServer {
    bool isAlive = false;
    int port = 0;
    uint8_t iecPort = 0;
    TaskHandle_t *htask = nullptr;

    void start(int p) {
        port = p;
        std::string tcp_svr_name = "tcp_svr_" + std::to_string(p);
        xTaskCreatePinnedToCore(tcp_server_task, tcp_svr_name.c_str(), 4096, (void*)this, 5, htask, 0);
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
            // we'll do this here instead: add a new socket that starts one IEC port above server or currently open ports
            // if(openSocket.count < 9) {
            //  auto newSock = new MeatSocket(sock, iecPort + openSocket.count +1);
            //  openSockets.Add(newSock);
            // }
            // purgeClosedSockets();


        }

    CLEAN_UP:
        close(listen_sock);
        vTaskDelete(NULL);
    }
};




class TCPMStream: public MStream {

public:
    bool isNetwork() override { return true; };
    TCPMStream(std::string path): MStream(path) {
        //url = path;
    };

    // Bounds the connect this stream is about to make. It has to be set
    // BEFORE open(), because the session is obtained and connected inside it.
    void setConnectTimeout(uint32_t ms) override { connect_timeout_ms_ = ms; }
    ~TCPMStream() {
        close();
    };

    // MStream methods
    uint32_t size() override {
        return -1;
    }
    uint32_t available() override {
        return 0;
    }
    uint32_t position() override {
        return 0;
    }
    size_t error() override {
        return 0;
    }

    virtual bool seek(uint32_t pos) {
        return false;
    }

    void close() override {
        if (_session) {
            _session->releaseIO();
            _session->disconnect();
            _session.reset();
        }
        open_ = false;
        eof_ = false;
    }

    bool open(std::ios_base::openmode mode) override {
        // `open_`, not `isOpen()`. Once the remote has hung up isOpen() answers
        // false for good, and guarding on it here would make every later read()
        // re-obtain the session and re-connect -- an endless redial on a
        // connection the peer has finished with. open_ is true from the first
        // successful open until an explicit close(), which is exactly when
        // lazy-opening is wanted. Same reasoning as TelnetMStream::read().
        if (open_) {
            return true;
        }

        auto p = PeoplesUrlParser::parseURL(url);
        if (!p) {
            Debug_printv("TCPMStream: failed to parse URL [%s]", url.c_str());
            return false;
        }

        uint16_t tcp_port = p->getPort();
        _session = SessionBroker::obtain<TCPMSession>(p->host, tcp_port,
                                                      connect_timeout_ms_);
        if (!_session) {
            Debug_printv("TCPMStream: failed to obtain session for %s:%d", p->host.c_str(), tcp_port);
            return false;
        }

        // The session is shared per host:port, so this is set every time rather
        // than once at construction: a later dial with a different S7 must get
        // its own bound, not the one the first caller happened to leave.
        // Also set on the session object itself. obtain() already applied it to
        // a session it CREATED; this covers one it returned from the repo,
        // which is already connected now but may reconnect later.
        _session->connect_timeout_ms = connect_timeout_ms_;

        if (!_session->connect()) {
            Debug_printv("TCPMStream: failed to connect to %s:%d", p->host.c_str(), tcp_port);
            _session.reset();
            return false;
        }

        _session->acquireIO();

        open_ = true;
        eof_ = false;

        return true;
    }

    // MStream methods
    uint32_t read(uint8_t* buf, uint32_t size) override {
        if (!open_ && !open(std::ios_base::in)) {
            return 0;
        }
        if (eof_) {
            return 0;
        }

        int got = _session->socket()->read(buf, size);

        // recv(2) returning exactly 0 is an orderly shutdown by the remote --
        // genuine EOF, not "no data right now", which comes back as
        // _MEAT_NO_DATA_AVAIL instead. The local descriptor stays valid after a
        // remote FIN, so isOpen() below cannot tell on its own: this is the only
        // place the hangup is observable. Without it a dialled tcp:// connection
        // never reported NO CARRIER and ATO answered CONNECT on a dead socket.
        if (got == 0 && size > 0) {
            eof_ = true;
        }

        return (uint32_t)got;
    }
    uint32_t write(const uint8_t *buf, uint32_t size) override {
        if (!open_ && !open(std::ios_base::out)) {
            return 0;
        }
        return _session->socket()->write(buf, size);
    }

    bool isOpen() {
        return open_ && !eof_ && _session && _session->isConnected() &&
               _session->socket()->isOpen();
    }

protected:
    std::shared_ptr<TCPMSession> _session;

    // True from the first successful open() until close(); see open().
    bool open_ = false;
    // The remote performed an orderly shutdown. Set by read() and never
    // cleared except by open()/close() -- a closed TCP connection never
    // produces another byte.
    bool eof_ = false;
    // Bound for the connect open() makes. 0 = unbounded, which is what every
    // consumer that never calls setConnectTimeout() keeps getting.
    uint32_t connect_timeout_ms_ = 0;
};


/********************************************************
 * File implementations
 ********************************************************/


class TCPMFile: public MFile 
{

public:
    TCPMFile() {
        Debug_printv("C++, if you try to call this, be damned!");
    };
    TCPMFile(std::string path): MFile(path) { 
        Debug_printv("constructing tcp file from url [%s]", url.c_str());
    };
    TCPMFile(std::string path, std::string filename): MFile(path) {};
    ~TCPMFile() override {
    }

    // We are overriding getSourceStream, because obviously - TCP scheme won't be wrapped in anything
    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override {
        return createStream(mode);
    } 

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) override {
        return src;
    }

    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override
    {
        return std::make_shared<TCPMStream>(url);
    }
};



/********************************************************
 * FS
 ********************************************************/

class TCPMFileSystem: public MFileSystem 
{
public:
    TCPMFileSystem(): MFileSystem("tcp") {
        isRootFS = true;
    };

    bool handles(std::string name) {
        if ( mstr::startsWith(name, (char *)"tcp:", false) )
            return true;

        return false;
    }

    MFile* getFile(std::string path) override {
        return new TCPMFile(path);
    }
};



#endif /* MEATFILESYSTEM_SCHEME_TCP */
