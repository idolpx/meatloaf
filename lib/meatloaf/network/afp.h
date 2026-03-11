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

// AFP:// - Apple Filing Protocol
// https://en.wikipedia.org/wiki/Apple_Filing_Protocol
//
// https://github.com/idolpx/afpfs-ng
// https://github.com/joaogsleite/docker-afp
//

#ifndef MEATLOAF_DEVICE_AFP
#define MEATLOAF_DEVICE_AFP

#include "meatloaf.h"
#include "meat_session.h"
#include "service/mdns.h"

extern "C" {
#include <afp.h>
#include <midlevel.h>
#include <libafpclient.h>
}

#include <map>
#include <pthread.h>

#include "fnFS.h"
#include "../../../include/debug.h"
#include "make_unique.h"

#include <dirent.h>
#include <string.h>
#include <fcntl.h>

// AFP default port (548)
#define AFP_DEFAULT_PORT 548

// Seconds between AFP epoch (Jan 1 2000) and Unix epoch (Jan 1 1970)
#define AFP_EPOCH_OFFSET 946684800

// Helper function declarations
bool parseAFPPath(const std::string& path, std::string& volume_name, std::string& file_path);


/********************************************************
 * AFPMSession - AFP Session Management
 *
 * Manages a connection to one AFP server (host:port).
 * Caches mounted volumes by name so re-accessing the same
 * volume does not trigger a second FPOpenVol.
 *
 * keep_alive_interval = 0:  AFP FPZzzzz keepalive is
 * handled by afp_zzzzz() if needed; we disable SessionBroker
 * keep-alive to avoid concurrent socket access.
 ********************************************************/

class AFPMSession : public MSession {
public:
    AFPMSession(std::string host, uint16_t port = AFP_DEFAULT_PORT)
        : MSession("afp://" + host + ":" + std::to_string(port), host, port)
    {
        keep_alive_interval = 0;
        Debug_printv("AFPMSession created for %s:%d", host.c_str(), port);
    }
    ~AFPMSession() override {
        Debug_printv("AFPMSession destroyed for %s:%d", host.c_str(), port);
        disconnect();
    }

    static std::string getScheme() { return "afp"; }

    void setCredentials(const std::string& user, const std::string& password) {
        _user = user;
        _password = password;
    }

    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    struct afp_server* getServer() { return _server; }

    // Get or open (mount) a volume by name; returns nullptr on failure.
    struct afp_volume* getVolume(const std::string& volume_name);

    // Cached, sorted list of volume names available on this server.
    const std::vector<std::string>& getVolumes();

private:
    void enumerateVolumes();

    std::string _user;
    std::string _password;
    struct afp_server* _server = nullptr;
    std::map<std::string, struct afp_volume*> _mounted_volumes;
    std::vector<std::string> _volumes_list;
    bool _volumes_enumerated = false;

    // One-time global AFP library initialisation (loop thread is file-local in afp.cpp).
    static bool _afp_initialized;
};


/********************************************************
 * AFPMFile - AFP File / Directory Object
 ********************************************************/

class AFPMFile : public MFile
{
public:
    std::string volume_name = "";
    std::string file_path   = "";   // absolute path within volume, e.g. "/Games/Elite.prg"

    AFPMFile(std::string path) : MFile(path) {
        uint16_t afp_port = port.empty() ? AFP_DEFAULT_PORT : std::stoi(port);
        _session = SessionBroker::obtain<AFPMSession>(host, afp_port);

        if (!_session || !_session->isConnected()) {
            Debug_printv("Failed to obtain AFP session for %s:%d", host.c_str(), afp_port);
            m_isNull = true;
            return;
        }

        if (!user.empty() || !password.empty())
            _session->setCredentials(user, password);

        parseAFPPath(this->path, volume_name, file_path);

        if (!volume_name.empty()) {
            _volume = _session->getVolume(volume_name);
            if (!_volume) {
                Debug_printv("Failed to get AFP volume: %s", volume_name.c_str());
                m_isNull = true;
                return;
            }
        }

        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry(name);

        m_isNull = !pathValid(this->path.c_str());
    }

    ~AFPMFile() {
        closeDir();
        _volume = nullptr;
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode = std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    time_t getLastWrite() override;
    time_t getCreationTime() override;
    uint64_t getAvailableSpace() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override;
    bool exists() override;
    bool remove() override;
    bool rename(std::string dest);
    bool readEntry(std::string filename);

protected:
    bool dirOpened = false;

    std::shared_ptr<AFPMSession> _session;
    struct afp_volume* _volume = nullptr;

    // Directory listing — linked list returned by ml_readdir.
    struct afp_file_info* _dir_base = nullptr;
    struct afp_file_info* _dir_iter = nullptr;

    struct afp_volume* getAFP() { return _volume; }

private:
    void openDir(std::string path);
    void closeDir();
    bool pathValid(std::string path);

    int entry_index = 0;                        // used when enumerating volumes
    static std::vector<std::string> volumes;    // volume name list during root enumeration
};


/********************************************************
 * AFPHandle - Utility handle wrapper (mirrors NFSHandle)
 ********************************************************/

class AFPHandle {
public:
    AFPHandle() : _session(nullptr), _fp(nullptr) {}
    ~AFPHandle();
    void obtain(std::string localPath, int flags);
    void dispose();

    struct afp_volume* getVolume();

private:
    std::shared_ptr<AFPMSession> _session;
    struct afp_file_info* _fp = nullptr;
    std::string _volume_name;
    std::string _file_path;
};


/********************************************************
 * AFPMStream - AFP File Stream
 ********************************************************/

class AFPMStream : public MStream {
public:
    AFPMStream(std::string& path) : MStream(path) {
        auto parser = PeoplesUrlParser::parseURL(path);
        if (parser && parser->scheme == "afp") {
            uint16_t afp_port = parser->port.empty() ? AFP_DEFAULT_PORT : std::stoi(parser->port);
            _session = SessionBroker::obtain<AFPMSession>(parser->host, afp_port);
        }
    }
    ~AFPMStream() override {
        close();
        _volume = nullptr;
    }

    bool isOpen() override;
    bool isNetwork() override { return true; }

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t* buf, uint32_t size) override;

    virtual bool seek(uint32_t pos) override;
    virtual bool seek(uint32_t pos, int mode) override;

    virtual bool seekPath(std::string path) override {
        Debug_printv("path[%s]", path.c_str());
        return false;
    }

protected:
    std::shared_ptr<AFPMSession> _session;
    struct afp_volume* _volume   = nullptr;
    struct afp_file_info* _fp    = nullptr;   // open fork
    std::string _volume_name;
    std::string _file_path;
    int _eof = 0;

    struct afp_volume* getAFP() { return _volume; }
};


/********************************************************
 * AFPMFileSystem - AFP Filesystem Factory
 ********************************************************/

class AFPMFileSystem : public MFileSystem
{
public:
    AFPMFileSystem() : MFileSystem("afp") {
        isRootFS = true;
        service_type = "_afpovertcp._tcp";
    }

    bool handles(std::string name) {
        return mstr::equals(name, (char*)"afp:", false);
    }

    MFile* getFile(std::string path) override {
        auto parser = PeoplesUrlParser::parseURL(path);
        if (parser->host.empty()) {
            path = "mdns://" + service_type;
            return new MDNSMFile(path);
        }
        return new AFPMFile(path);
    }
};


#endif // MEATLOAF_DEVICE_AFP
