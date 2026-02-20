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

// CSIP:/ - a scheme for handling CommodoreServer Internet Protocol
// see: https://www.commodoreserver.com/BlogEntryView.asp?EID=9D133160E7C344A398EC1F45AEF4BF32
//

#ifndef MEATLOAF_SCHEME_CSIP
#define MEATLOAF_SCHEME_CSIP

#include "meatloaf.h"
#include "meat_session.h"
#include "network/tcp.h"

#include "fnSystem.h"

#include "utils.h"
#include "string_utils.h"

#include "make_unique.h"

#include <streambuf>
#include <istream>

/********************************************************
 * Telnet buffer
 ********************************************************/

class csstreambuf : public std::streambuf {
    char* gbuf = nullptr;
    char* pbuf = nullptr;
    std::string server_host;
    uint16_t server_port;

protected:
    MeatSocket m_wifi;

public:
    csstreambuf(std::string host = "commodoreserver.com", uint16_t port = 1541)
        : server_host(host), server_port(port) {}

    ~csstreambuf() {
        close();
    }

    bool is_open() {
        return (m_wifi.isOpen());
    }

    bool open() {
        //Debug_printv("csstreambuf: open");
        if(m_wifi.isOpen())
            return true;

        int rc = m_wifi.open(server_host.c_str(), server_port);
        printf("csstreambuf: connect to %s:%d returned: %d\r\n",
               server_host.c_str(), server_port, rc);

        if(rc == 1) {
            if(gbuf == nullptr)
                gbuf = new char[512];
            if(pbuf == nullptr)
                pbuf = new char[512];
        }

        setp(pbuf, pbuf+512);

        return rc == 1;
    }

    void close() {
        printf("csstreambuf: closing\r\n");
        if(m_wifi.isOpen()) {
            m_wifi.close();
        }
        if(gbuf != nullptr)
            delete[] gbuf;
        if(pbuf != nullptr)
            delete[] pbuf;
    }

    int underflow() override {
        //Debug_printv("In underflow");
        if (!m_wifi.isOpen()) {
            Debug_printv("In connection closed");
            close();
            return std::char_traits<char>::eof();
        }
        else if (this->gptr() == this->egptr()) {
            int readCount = 0;
            int attempts = 5;
            int wait = 500;
            
            readCount = m_wifi.read((uint8_t*)gbuf, 512);

            while( readCount <= 0 && (attempts--)>0 && m_wifi.isOpen()) {
                Debug_printv("got rc: %d, retrying", readCount);
                fnSystem.delay(wait);
                wait+=100;
                readCount = m_wifi.read((uint8_t*)gbuf, 512);
            } 
            //Debug_printv("read success: %d", readCount);
            this->setg(gbuf, gbuf, gbuf + readCount);
        }
        else {
            //Debug_printv("else: %d - %d, (%d)", this->gptr(), this->egptr(), this->gbuf);
        }

        return this->gptr() == this->egptr()
            ? std::char_traits<char>::eof()
            : std::char_traits<char>::to_int_type(*this->gptr());
    };


    int overflow(int ch  = traits_type::eof()) override
    {
        //Debug_printv("in overflow");

        if (!m_wifi.isOpen()) {
            close();
            return EOF;
        }

        char* end = pptr();
        if ( ch != EOF ) {
            *end ++ = ch;
        }

        uint8_t* pBase = (uint8_t*)pbase();

        if ( m_wifi.write( pBase, end - pbase() ) == 0 ) {
            ch = EOF;
        } else if ( ch == EOF ) {
            ch = 0;
        }
        setp(pbuf, pbuf+512);
        
        return ch;
    };

    int sync() { 

        if (!m_wifi.isOpen()) {
            close();
            return 0;
        }
        if(pptr() == pbase()) {
            return 0;
        }
        else {
            //Debug_printv("in sync, written %d", pptr()-pbase());
            uint8_t* buffer = (uint8_t*)pbase();
            auto result = m_wifi.write(buffer, pptr()-pbase()); 
            setp(pbuf, pbuf+512);
            return (result != 0) ? 0 : -1;  
        }  
    };

    friend class CSIPMSession;
};

/********************************************************
 * Session manager
 ********************************************************/

class CSIPMSession : public MSession, public std::iostream {
public:
    CSIPMSession(std::string host = "commodoreserver.com", uint16_t port = 1541);
    ~CSIPMSession() override;

    // MSession interface implementation
    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    // Get the scheme for this session type
    static std::string getScheme() { return "csip"; }

    // CSIP-specific methods
    bool sendCommand(const std::string& command);
    bool traversePath(std::string path);
    bool isOK();
    std::string readLn();

    // Stream access methods for CSIPMStream
    size_t receive(uint8_t* buffer, size_t size) {
        if (buf.is_open()) {
            size_t readCount = buf.m_wifi.read(buffer, size);
            if (readCount > 0) {
                updateActivity();
            }
            return readCount;
        }
        return 0;
    }

    size_t send(const uint8_t* buffer, size_t size) {
        if (buf.is_open()) {
            size_t written = buf.m_wifi.write(buffer, size);
            if (written > 0) {
                updateActivity();
            }
            return written;
        }
        return 0;
    }

    std::string getCurrentDir() const { return currentDir; }

protected:
    csstreambuf buf;  // Must be declared first for initialization order
    std::string currentDir;

    bool establish();

    friend class CSIPMFile;
    friend class CSIPMStream;
};

/********************************************************
 * File implementations
 ********************************************************/
class CSIPMFile: public MFile {
public:
    CSIPMFile(std::string path, size_t size = 0);
    ~CSIPMFile();

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) override { return src; };
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override;

    bool exists() override;
    bool remove() override;

    // Accessor for session (used by CSIPMStream)
    std::shared_ptr<CSIPMSession> getSession() { return _session; }

protected:
    std::shared_ptr<CSIPMSession> _session;
    bool dirIsOpen = false;
    bool dirIsImage = false;
    bool dirHoldsIo = false;

    friend class CSIPMStream;
};

/********************************************************
 * Streams
 ********************************************************/

class CSIPMStream: public MStream {
public:
    CSIPMStream(std::string& path): MStream(path) {
    }
    ~CSIPMStream() override {
        close();
    }

    // MStream methods
    bool isOpen() override;
    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return false; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    bool seek(uint32_t pos) override {
        return false;
    };

protected:
    std::shared_ptr<CSIPMSession> _session;
    bool _is_open = false;
    bool _holds_io = false;
};



/********************************************************
 * FS
 ********************************************************/

class CSIPMFileSystem: public MFileSystem 
{
public:
    CSIPMFileSystem(): MFileSystem("csip") {
        isRootFS = true;
    };

    bool handles(std::string name) 
    {
        return mstr::startsWith(name, "csip:", false);
    }

    MFile* getFile(std::string path) override 
    {
        Debug_printv("path[%s]", path.c_str());
        return new CSIPMFile(path);
    }
};

#endif /* MEATLOAF_SCHEME_CSIP */
