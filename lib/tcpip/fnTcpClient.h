/* Modified version of ESP-Arduino WiFiClient.cpp/h */

#ifndef _FN_TCPCLIENT_H_
#define _FN_TCPCLIENT_H_

#include <memory>
#include <string>

#include "compat_inet.h"

class fnTcpClientSocketHandle;

class fnTcpClient
{
protected:
    std::string _rxBuffer;
    std::shared_ptr<fnTcpClientSocketHandle> _clientSocketHandle;
    bool _connected = false;

public:
    fnTcpClient() {};
    fnTcpClient(int fd);
    ~fnTcpClient();

    void stop();

    int connect(const char *host, uint16_t port, int32_t timeout = -1);
    int connect(in_addr_t addr, uint16_t port, int32_t timeout = -1);

    size_t write(uint8_t data);
    size_t write(const uint8_t *buf, size_t size);
    size_t write(const char *buff);
    size_t write(const std::string str);

    int read();
    int read(uint8_t *buf, size_t size);
    int read_until(char terminator, char *buf, size_t size);

    void updateFIFO();
    size_t available();
    int peek();
    void flush();
    uint8_t connected();

    operator bool() { return connected(); }

    int setSocketOption(int option, char* value, size_t len);
    int setOption(int option, int *value);
    int getOption(int option, int *value);
    int setTimeout(uint32_t seconds);
    int setNoDelay(bool nodelay);
    bool getNoDelay();

    in_addr_t remoteIP() const;
    in_addr_t remoteIP(int fd) const;
    uint16_t remotePort() const;
    uint16_t remotePort(int fd) const;
    in_addr_t localIP() const;
    in_addr_t localIP(int fd) const;
    uint16_t localPort() const;
    uint16_t localPort(int fd) const;

    int fd() const;

    // Give up ownership of the socket WITHOUT closing it. The caller becomes
    // responsible for closing the returned descriptor. Used to hand a
    // connected socket to a layer that owns it from then on (esp-tls closes
    // the descriptor itself in esp_tls_conn_destroy(), so leaving it with the
    // handle as well would double-close it). The socket handle is a shared_ptr,
    // so only detach a socket no other fnTcpClient holds a copy of - the other
    // holder would go on believing it owns a descriptor someone else closes.
    int detach();
};

#endif // _FN_TCPCLIENT_H_
