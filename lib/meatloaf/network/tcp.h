// TCP raw socket support for Meatloaf
#ifndef MEATLOAF_SCHEME_TCP
#define MEATLOAF_SCHEME_TCP

#include "meatloaf.h"
#include "meat_session.h"

#include <string>
#include <memory>

#include "../../../include/debug.h"

class TCPMSession : public MSession {
public:
    TCPMSession(std::string host, uint16_t port = 0);
    ~TCPMSession() override;

    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    // socket operations
    int send(const uint8_t* buf, size_t len);
    int recv(uint8_t* buf, size_t len);
    size_t available();
    void setBlocking(bool b);
    bool listen(uint16_t port, int backlog = 4);
    bool isListening() const { return _listen_sock >= 0; }
    int acceptClient();

    int sockfd() const { return _sock; }

private:
    int _sock = -1;
    int _listen_sock = -1;
    bool _blocking = false;
    uint32_t _last_activity = 0;
};


// TCP MFile / Stream declarations

class TCPMStream;
class TCPMFile : public MFile {
public:
    TCPMFile(std::string path);
    ~TCPMFile() override;

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) override;
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;
    bool exists() override;
};

class TCPMStream : public MStream {
public:
    TCPMStream(std::string path);
    ~TCPMStream() override;

    bool isOpen() override;
    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;
    bool seek(uint32_t pos) override { return false; }

private:
    std::shared_ptr<TCPMSession> _session = nullptr;
    bool _opened = false;
};

class TCPMFileSystem : public MFileSystem {
public:
    TCPMFileSystem(): MFileSystem("tcp") {
        isRootFS = false;
    }
    bool handles(std::string name) override;
    MFile* getFile(std::string path) override;
};

#endif /* MEATLOAF_SCHEME_TCP */
