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

// TNFS:// - Trivial Network File System
// https://github.com/FujiNetWIFI/fujinet-platformio/wiki/SIO-Command-$DC-Open-TNFS-Directory

#ifndef MEATLOAF_NETWORK_TNFS
#define MEATLOAF_NETWORK_TNFS

#include "meatloaf.h"
#include "meat_session.h"

#include "tnfslib.h"
#include "tnfslibMountInfo.h"

#include "../../../include/debug.h"

#include "make_unique.h"

#include <dirent.h>
#include <string.h>


/********************************************************
 * MSession - TNFS Session Management
 ********************************************************/

class TNFSMSession : public MSession {
public:
    TNFSMSession(std::string host, uint16_t port = TNFS_DEFAULT_PORT);
    ~TNFSMSession() override;

    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    // Get the scheme for this session type
    static std::string getScheme() { return "tnfs"; }

    // Get the mount info for TNFS operations
    tnfsMountInfo* getMountInfo() { return _mountinfo.get(); }

private:
    std::unique_ptr<tnfsMountInfo> _mountinfo;
};


/********************************************************
 * MFile
 ********************************************************/

class TNFSMFile: public MFile
{
public:
    TNFSMFile(std::string path);
    ~TNFSMFile();

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) override;
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
    bool rename(std::string dest) override;

    bool readEntry(std::string filename);

protected:
    bool dirOpened = false;
    std::shared_ptr<TNFSMSession> _session;
    uint16_t _dir_handle = 0xFFFF;  // Directory handle for this instance

    // Helper to get mount info from session
    tnfsMountInfo* getMountInfo() { return _session ? _session->getMountInfo() : nullptr; }

private:
    void openDir(std::string path);
    void closeDir();
    bool pathValid(std::string path);

    int entry_index = 0;
};


/********************************************************
 * TNFSHandle
 ********************************************************/

class TNFSHandle {
public:
    std::unique_ptr<tnfsMountInfo> _mountinfo;
    int16_t _handle = TNFS_INVALID_HANDLE;

    TNFSHandle();
    ~TNFSHandle();
    void obtain(std::string url, std::ios_base::openmode mode);
    void dispose();

private:
    uint16_t mapOpenMode(std::ios_base::openmode mode);
};


/********************************************************
 * MStream
 ********************************************************/

class TNFSMStream: public MStream {
public:
    TNFSMStream(std::string& path): MStream(path) {
    }
    ~TNFSMStream() override {
        close();
    }

    // MStream methods
    bool isOpen() override;
    // bool isBrowsable() override { return false; };
    // bool isRandomAccess() override { return false; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    bool seek(uint32_t pos) override;
    bool seek(uint32_t pos, int mode) override;

    bool seekPath(std::string path) override {
        Debug_printv("path[%s]", path.c_str());
        return false;
    }

protected:
    std::shared_ptr<TNFSMSession> _session;
    int16_t _handle = TNFS_INVALID_HANDLE;

private:
    uint16_t mapOpenMode(std::ios_base::openmode mode);
};


/********************************************************
 * MFileSystem
 ********************************************************/

class TNFSMFileSystem: public MFileSystem
{
public:
    TNFSMFileSystem(): MFileSystem("tnfs") {
        isRootFS = true;
    };

    bool handles(std::string name) {
        return mstr::equals(name, (char *)"tnfs:", false);
    }

    MFile* getFile(std::string path) override {
        return new TNFSMFile(path);
    }
};


#endif // MEATLOAF_NETWORK_TNFS
