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

// FTP:// - File Transfer Protocol
//
// https://en.wikipedia.org/wiki/File_Transfer_Protocol
// https://www.ietf.org/archive/id/draft-bryan-ftp-range-01.html
//

#ifndef MEATLOAF_DEVICE_FTP
#define MEATLOAF_DEVICE_FTP

#include "meatloaf.h"
#include "meat_session.h"

#include <dirent.h>
#include <string.h>

#include "../../../include/debug.h"
#include "../../FileSystem/fnFile.h"
#include "../../FileSystem/fnFsFTP.h"

#include "make_unique.h"

/********************************************************
 * MSession - FTP Session Management
 ********************************************************/

// Build the host key a session is filed under. Credentials are embedded so
// they reach the FTPMSession constructor before connect() runs, and so that
// two users of the same server get two sessions rather than sharing one.
// Same shape as SMBMFile's session_host.
inline std::string ftpSessionHost(const std::string &user,
                                  const std::string &password,
                                  const std::string &host)
{
    if (user.empty()) return host;
    std::string h = user;
    if (!password.empty()) h += ":" + password;
    return h + "@" + host;
}

class FTPMSession : public MSession {
   public:
    FTPMSession(std::string host, uint16_t port = 21)
        : FTPMSession("ftp", std::move(host), port) {}

    // The scheme must be spelled here exactly as SessionBroker::obtain()
    // spells it, or the session is created under one name and filed under
    // another.
    FTPMSession(const char *scheme, std::string host, uint16_t port)
        : MSession(std::string(scheme) + "://" + host + ":" + std::to_string(port), host, port)
    {
        // Split user[:pass]@ back off the key, leaving `host` the bare host.
        size_t at = host.rfind('@');
        if (at != std::string::npos) {
            std::string creds = host.substr(0, at);
            this->host = host.substr(at + 1);
            size_t colon = creds.find(':');
            if (colon != std::string::npos) {
                _user = creds.substr(0, colon);
                _password = creds.substr(colon + 1);
            } else {
                _user = creds;
            }
        }
        Debug_printv("FTPMSession created for %s:%d (user: %s)",
                     this->host.c_str(), port,
                     _user.empty() ? "anonymous" : _user.c_str());
    }
    ~FTPMSession() override {
        Debug_printv("FTPMSession destroyed for %s:%d", host.c_str(), port);
        disconnect();
    }

    // Get the scheme for this session type
    static std::string getScheme() { return "ftp"; }

    // The scheme this session's URLs carry. FTPSMSession overrides it, and
    // FileSystemFTP::start() reads it back off the base URL to decide whether
    // to negotiate TLS up front.
    virtual const char *urlScheme() const { return "ftp"; }

    bool connect() override {
        if (connected) return true;
        _fs = std::make_unique<FileSystemFTP>();
        // Build base URL
        std::string base = std::string(urlScheme()) + "://" + host;
        if (port != 21) base += ":" + std::to_string(port);

        // nullptr, not "", when there are no credentials - start() picks its
        // anonymous defaults on a null pointer and would otherwise send an
        // empty USER.
        if (!_fs->start(base.c_str(),
                        _user.empty() ? nullptr : _user.c_str(),
                        _password.empty() ? nullptr : _password.c_str())) {
            Debug_printv("Failed to start FTP filesystem for %s:%d",
                         host.c_str(), port);
            connected = false;
            return false;
        }
        connected = true;
        updateActivity();
        return true;
    }

    void disconnect() override {
        if (!connected) return;
        if (_fs) {
            _fs->dir_close();
        }
        _fs.reset();
        connected = false;
    }

    bool keep_alive() override {
        if (!connected || !_fs) return false;
        // Send NOOP command as lightweight keep-alive
        bool res = _fs->keep_alive();
        updateActivity();
        return res;
    }

    FileSystemFTP* fs() { return _fs.get(); }

   private:
    std::unique_ptr<FileSystemFTP> _fs;
    std::string _user;
    std::string _password;
};

/**
 * An ftps:// session. Same client, but TLS is negotiated on the FIRST attempt
 * rather than only after the server refuses to talk in the clear.
 *
 * It is a separate class purely so SessionBroker keys it separately -
 * obtain<T>() builds the key from T::getScheme(). Explicit FTPS uses the same
 * port 21 as plain FTP, so the port cannot distinguish them the way it does
 * for http/https, and a server that accepts BOTH must not hand an ftps://
 * caller a plaintext session that an ftp:// caller opened first.
 */
class FTPSMSession : public FTPMSession {
   public:
    FTPSMSession(std::string host, uint16_t port = 21)
        : FTPMSession(std::move(host), port) {}

    static std::string getScheme() { return "ftps"; }
    const char *urlScheme() const override { return "ftps"; }
};

/********************************************************
 * MFile
 ********************************************************/

class FTPMFile : public MFile {
   public:
    std::string basepath = "";

    FTPMFile(std::string path) : MFile(path) {
        // Obtain or create FTP session via SessionBroker
        uint16_t ftp_port = port.empty() ? 21 : std::stoi(port);
        std::string session_host = ftpSessionHost(user, password, host);
        _session = (scheme == "ftps")
                       ? SessionBroker::obtain<FTPSMSession>(session_host, ftp_port)
                       : SessionBroker::obtain<FTPMSession>(session_host, ftp_port);

        if (!_session || !_session->isConnected()) {
            Debug_printv("Failed to obtain FTP session for %s:%d", host.c_str(), ftp_port);
            m_isNull = true;
            return;
        }

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry(name);

        // if (!pathValid(path.c_str()))
        //     m_isNull = true;
        // else
            m_isNull = false;
    };
    ~FTPMFile() {
        //Debug_printv("*** Destroying FTPMFile [%s]", url.c_str());
        //closeDir();
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode = std::ios_base::in) override;  // has to return OPENED stream
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    time_t getLastWrite() override;
    time_t getCreationTime() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override;
    bool exists() override;

    bool remove() override;
    bool rename(std::string dest);

    bool readEntry(std::string filename);

   protected:
    bool dirOpened = false;
    std::shared_ptr<FTPMSession> _session;

    // Helper to get FTP filesystem from session
    FileSystemFTP* getFS() { return _session ? _session->fs() : nullptr; }

   private:
    virtual void openDir(std::string path);
    virtual void closeDir();

    //bool pathValid(std::string path);
};

/********************************************************
 * MStream I
 ********************************************************/

// forward declare impl
struct FTPMStream_impl_access;

class FTPMStream : public MStream {
   public:
    FTPMStream(std::string& path) : MStream(path), _impl(nullptr) {}
    ~FTPMStream() override { close(); }

    // MStream methods
    bool isOpen() override;
    // bool isBrowsable() override { return false; };
    // bool isRandomAccess() override { return false; };
    bool isNetwork() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t* buf, uint32_t size) override;

    bool seek(uint32_t pos) override;
    bool seek(uint32_t pos, int mode) override;

    bool seekPath(std::string path) override {
        Debug_printv("path[%s]", path.c_str());
        return false;
    }

   protected:
    std::shared_ptr<FTPMSession> _session;
    FTPMStream_impl_access* _impl;
};

/********************************************************
 * MFileSystem
 ********************************************************/

class FTPMFileSystem : public MFileSystem {
   public:
    FTPMFileSystem() : MFileSystem("ftp") { isRootFS = true; };

    bool handles(std::string name) {
        if (mstr::equals(name, (char*)"ftp:", false)) return true;
        // Explicit FTPS (RFC 4217) - same client, same port 21, TLS negotiated
        // before login rather than only when the server insists.
        if (mstr::equals(name, (char*)"ftps:", false)) return true;

        return false;
    }

    MFile* getFile(std::string path) override { return new FTPMFile(path); }
};

#endif  // MEATLOAF_DEVICE_FTP
