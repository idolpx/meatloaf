#include "tcp.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include "compat_inet.h"


// --- TCPMSession ---
TCPMSession::TCPMSession(std::string host, uint16_t port)
    : MSession(std::string("tcp://") + host + ":" + std::to_string(port), host, port)
{
}

TCPMSession::~TCPMSession()
{
    disconnect();
}

bool TCPMSession::connect()
{
    // If host is empty, act as a listening server for the configured port
    if (host.empty()) {
        return listen(port);
    }

    struct addrinfo hints;
    struct addrinfo *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string portstr = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portstr.c_str(), &hints, &res);
    if (rc != 0) {
        Debug_printv("TCP connect getaddrinfo failed: %d", rc);
        return false;
    }

    int s = -1;
    struct addrinfo *p;
    for (p = res; p != NULL; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) continue;

        // set close-on-exec
        int flags = fcntl(s, F_GETFD, 0);
        if (flags >= 0) fcntl(s, F_SETFD, flags | FD_CLOEXEC);

        if (::connect(s, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }

        close(s);
        s = -1;
    }

    freeaddrinfo(res);

    if (s < 0) {
        Debug_printv("TCP connect failed for %s:%d", host.c_str(), port);
        return false;
    }

    _sock = s;
    connected = true;
    updateActivity();
    return true;
}

void TCPMSession::disconnect()
{
    if (_sock >= 0) {
        close(_sock);
        _sock = -1;
    }
    if (_listen_sock >= 0) {
        close(_listen_sock);
        _listen_sock = -1;
    }
    connected = false;
}

bool TCPMSession::keep_alive()
{
    return connected;
}

int TCPMSession::send(const uint8_t* buf, size_t len)
{
    if (_sock < 0) return -1;
    ssize_t r = ::send(_sock, buf, len, 0);
    if (r > 0) updateActivity();
    return (int)r;
}

int TCPMSession::recv(uint8_t* buf, size_t len)
{
    // If we're a listening session, try to accept a client first
    if (_listen_sock >= 0 && _sock < 0) {
        int clientfd = acceptClient();
        if (clientfd > 0) {
            _sock = clientfd;
            connected = true;
        }
    }

    if (_sock < 0) return -1;
    ssize_t r = ::recv(_sock, buf, len, 0);
    if (r > 0) updateActivity();
    return (int)r;
}

size_t TCPMSession::available()
{
    if (_sock < 0) return 0;
#if defined(__APPLE__) || defined(__MACH__)
    int count = 0;
    if (ioctl(_sock, FIONREAD, &count) < 0) return 0;
    return (size_t)count;
#else
    int count = 0;
    if (ioctl(_sock, FIONREAD, &count) < 0) return 0;
    return (size_t)count;
#endif
}

void TCPMSession::setBlocking(bool b)
{
    if (_sock < 0) {
        _blocking = b;
        return;
    }
    int flags = fcntl(_sock, F_GETFL, 0);
    if (flags < 0) return;
    if (b)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    fcntl(_sock, F_SETFL, flags);
    _blocking = b;
}

bool TCPMSession::listen(uint16_t port, int backlog)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        Debug_printv("TCP listen socket failed: %d", compat_getsockerr());
        return false;
    }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        Debug_printv("TCP bind failed: %d", compat_getsockerr());
        close(s);
        return false;
    }

    if (::listen(s, backlog) < 0) {
        Debug_printv("TCP listen failed: %d", compat_getsockerr());
        close(s);
        return false;
    }

    // set non-blocking accept by default to avoid blocking the IEC thread
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    _listen_sock = s;
    connected = true;
    updateActivity();
    return true;
}

int TCPMSession::acceptClient()
{
    if (_listen_sock < 0) return -1;
    struct sockaddr_in clientaddr;
    socklen_t addrlen = sizeof(clientaddr);
    int clientfd = accept(_listen_sock, (struct sockaddr *)&clientaddr, &addrlen);
    if (clientfd < 0) {
        int err = compat_getsockerr();
        if (err == EWOULDBLOCK || err == EAGAIN) return -1;
        Debug_printv("TCP accept failed: %d", err);
        return -1;
    }

    // Set close-on-exec
    int flags = fcntl(clientfd, F_GETFD, 0);
    if (flags >= 0) fcntl(clientfd, F_SETFD, flags | FD_CLOEXEC);

    // Set blocking mode according to current preference
    flags = fcntl(clientfd, F_GETFL, 0);
    if (!_blocking) flags |= O_NONBLOCK; else flags &= ~O_NONBLOCK;
    fcntl(clientfd, F_SETFL, flags);

    return clientfd;
}



// --- TCPMFile ---
TCPMFile::TCPMFile(std::string path): MFile(path)
{
}

TCPMFile::~TCPMFile()
{
}

std::shared_ptr<MStream> TCPMFile::getSourceStream(std::ios_base::openmode mode)
{
    return createStream(mode);
}

std::shared_ptr<MStream> TCPMFile::getDecodedStream(std::shared_ptr<MStream> src)
{
    return src;
}

std::shared_ptr<MStream> TCPMFile::createStream(std::ios_base::openmode mode)
{
    auto s = std::make_shared<TCPMStream>(url);
    s->mode = mode;
    return s;
}

bool TCPMFile::exists()
{
    return true;
}



// --- TCPMStream ---
TCPMStream::TCPMStream(std::string path): MStream(path)
{
}

TCPMStream::~TCPMStream()
{
    close();
}

bool TCPMStream::isOpen()
{
    return _opened;
}

bool TCPMStream::open(std::ios_base::openmode mode)
{
    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser) return false;

    uint16_t p = parser->port.empty() ? 23 : std::stoi(parser->port);
    _session = SessionBroker::obtain<TCPMSession>(parser->host, p);
    if (!_session || !_session->isConnected()) {
        _opened = false;
        return false;
    }
    // Honor the requested open mode: if the caller opened for output
    // (write) we make the socket blocking so writes behave synchronously.
    // If opened read-only, default to non-blocking to avoid freezing the C64.
    bool wants_write = (mode & std::ios_base::out) != 0;
    _session->setBlocking(wants_write);
    _opened = true;
    return true;
}

void TCPMStream::close()
{
    if (_session) {
        // do not forcibly disconnect session here; keep broker life-cycle
        _session.reset();
    }
    _opened = false;
}

uint32_t TCPMStream::read(uint8_t* buf, uint32_t size)
{
    if (!_session) return 0;
    int r = _session->recv(buf, size);
    if (r < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            // non-blocking, no data
            _error = 0;
            return 0;
        }
        _error = 1;
        return 0;
    }

    if (r == 0) {
        // remote closed
        _error = 0;
        return 0;
    }

    return (uint32_t)r;
}

uint32_t TCPMStream::write(const uint8_t *buf, uint32_t size)
{
    if (!_session) return 0;
    int r = _session->send(buf, size);
    if (r < 0) {
        _error = 1;
        return 0;
    }
    return (uint32_t)r;
}



// --- TCPMFileSystem ---
bool TCPMFileSystem::handles(std::string name)
{
    if (mstr::startsWith(name, "tcp:", false) || mstr::startsWith(name, "tcp://", false))
        return true;
    return false;
}

MFile* TCPMFileSystem::getFile(std::string path)
{
    return new TCPMFile(path);
}
