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

// ISCSI:// - Internet Small Computer Systems Interface
// https://en.wikipedia.org/wiki/ISCSI
//
// URL format:
//   iscsi://[user[%password]@]host[:3260]/target-iqn/lun
//
// Examples:
//   iscsi://nas.local/iqn.2000-01.com.synology:nas.target-1/0
//   iscsi://user%secret@10.0.0.1:3260/iqn.2024.com.example:disk/0
//
// Directory browsing:
//   iscsi://host/               → list targets (SendTargets discovery)
//   iscsi://host/iqn.xxx/       → list LUNs for target (REPORT LUNS)
//   iscsi://host/iqn.xxx/0      → LUN 0 as a raw block-device file


#ifndef MEATLOAF_DEVICE_ISCSI
#define MEATLOAF_DEVICE_ISCSI

#include "meatloaf.h"
#include "meat_session.h"
#include "service/mdns.h"

extern "C" {
#include <iscsi.h>
#include <scsi-lowlevel.h>
}
// scsi-lowlevel.h defines static_assert as a 2-arg C89 compat macro which
// clobbers the C++11 static_assert keyword (1 or 2 args). Undef it here so
// all C++ code that follows in this TU sees the real keyword.
#ifdef static_assert
#undef static_assert
#endif

#include <vector>
#include <string>
#include <algorithm>

#include "../../../include/debug.h"

// Parse the path component of an iSCSI URL into target IQN and LUN number.
// path = ""  or "/"              → target_iqn = "",        lun_number = -1  (root)
// path = "/iqn.xxx" or "/iqn.xx/"  → target_iqn = "iqn.xxx", lun_number = -1  (target dir)
// path = "/iqn.xxx/0"            → target_iqn = "iqn.xxx", lun_number = 0   (LUN file)
bool parseISCSIPath(const std::string& path, std::string& target_iqn, int& lun_number);


/********************************************************
 * ISCSIMSession - portal-level session (discovery only)
 ********************************************************/

class ISCSIMSession : public MSession {
public:
    ISCSIMSession(std::string host, uint16_t port = 3260)
        : MSession("iscsi://" + host + ":" + std::to_string(port), host, port)
    {
        // iSCSI discovery contexts are short-lived; disable SessionBroker keep-alive
        keep_alive_interval = 0;
        Debug_printv("ISCSIMSession created for %s:%d", host.c_str(), port);
    }

    ~ISCSIMSession() override {
        Debug_printv("ISCSIMSession destroyed for %s:%d", host.c_str(), port);
        disconnect();
    }

    static std::string getScheme() { return "iscsi"; }

    bool connect() override {
        if (connected) return true;
        connected = true;  // Targets are enumerated lazily on first getTargets() call
        updateActivity();
        Debug_printv("ISCSIMSession ready for portal %s:%d", host.c_str(), port);
        return true;
    }

    void disconnect() override {
        _targets_list.clear();
        _targets_enumerated = false;
        connected = false;
        Debug_printv("ISCSIMSession disconnected from %s:%d", host.c_str(), port);
    }

    bool keep_alive() override {
        updateActivity();
        return connected;
    }

    // Return targets discovered at this portal (result is cached after first call).
    const std::vector<std::string>& getTargets() {
        if (!_targets_enumerated) {
            enumerateTargets();
        }
        return _targets_list;
    }

    // Return the portal string used by libiscsi ("host:port").
    std::string portal() const {
        return host + ":" + std::to_string(port);
    }

private:
    void enumerateTargets();

    std::vector<std::string> _targets_list;
    bool _targets_enumerated = false;
};


/********************************************************
 * ISCSIMFile
 ********************************************************/

class ISCSIMFile : public MFile {
public:
    std::string target_iqn;   // empty = root (list targets)
    int lun_number = -1;      // -1 = target directory (list LUNs)

    ISCSIMFile(std::string path) : MFile(path) {
        uint16_t iscsi_port = this->port.empty() ? 3260 : std::stoi(this->port);
        _session = SessionBroker::obtain<ISCSIMSession>(host, iscsi_port);

        if (!_session || !_session->isConnected()) {
            Debug_printv("Failed to obtain iSCSI session for %s:%d", host.c_str(), iscsi_port);
            m_isNull = true;
            return;
        }

        parseISCSIPath(this->path, target_iqn, lun_number);
        m_isNull = false;
    }

    ~ISCSIMFile() {
        closeDir();
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode = std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    time_t getLastWrite() override    { return 0; }
    time_t getCreationTime() override { return 0; }
    uint64_t getAvailableSpace() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override   { return false; }
    bool exists() override;
    bool remove() override  { return false; }
    bool rename(std::string dest) { return false; }
    bool readEntry(std::string filename);

protected:
    std::shared_ptr<ISCSIMSession> _session;
    bool  _dirOpened   = false;
    int   _dir_index   = 0;

    // Used when listing LUNs for a target
    std::vector<uint16_t> _lun_list;
    bool _luns_enumerated = false;

    void openDir(std::string apath);
    void closeDir();
    void enumerateLuns();
};


/********************************************************
 * ISCSIMStream - block-device I/O over iSCSI
 ********************************************************/

class ISCSIMStream : public MStream {
public:
    ISCSIMStream(std::string& path) : MStream(path) {
        auto parser = PeoplesUrlParser::parseURL(path);
        if (parser && parser->scheme == "iscsi") {
            uint16_t iscsi_port = parser->port.empty() ? 3260 : std::stoi(parser->port);
            _session = SessionBroker::obtain<ISCSIMSession>(parser->host, iscsi_port);
        }
    }

    ~ISCSIMStream() override {
        close();
    }

    bool isOpen()         override { return _ctx != nullptr && _connected; }
    bool isNetwork()      override { return true; }
    bool isRandomAccess() override { return true; }

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
    std::shared_ptr<ISCSIMSession> _session;
    struct iscsi_context* _ctx = nullptr;  // Dedicated per-stream connection
    int      _lun        = 0;
    uint32_t _block_size = 512;
    bool     _connected  = false;

    // Single-block read cache to avoid repeated reads for unaligned access
    std::vector<uint8_t> _block_buf;
    int64_t _buf_lba = -1;  // Which LBA is currently cached (-1 = none)

    // Read one block from the target into _block_buf
    bool readBlock(uint32_t lba);
    // Write one block worth of data from src to the target (overwrites entire block)
    bool writeBlock(uint32_t lba, const uint8_t* src);
};


/********************************************************
 * ISCSIMFileSystem
 ********************************************************/

class ISCSIMFileSystem : public MFileSystem {
public:
    ISCSIMFileSystem() : MFileSystem("iscsi") {
        isRootFS = true;
        service_type = "_iscsi._tcp";
    }

    bool handles(std::string name) override {
        return mstr::equals(name, (char*)"iscsi:", false);
    }

    MFile* getFile(std::string path) override {
        auto parser = PeoplesUrlParser::parseURL(path);
        if (parser->host.empty()) {
            path = "mdns://" + service_type;
            return new MDNSMFile(path);
        }
        return new ISCSIMFile(path);
    }
};


#endif // MEATLOAF_DEVICE_ISCSI
