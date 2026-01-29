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

// FSP:// - File Service Protocol
// https://fsp.sourceforge.net/


#ifndef MEATLOAF_NETWORK_FSP
#define MEATLOAF_NETWORK_FSP

#include "meatloaf.h"
#include "meat_session.h"

extern "C" {
#include "../../../components/fsplib/lib/fsplib.h"
}

#include "../../../include/debug.h"

#include "make_unique.h"

#include <dirent.h>
#include <string.h>
#include <mutex>



/********************************************************
 * MSession - FSP Session Management
 ********************************************************/

class FSPMSession : public MSession {
public:
    FSPMSession(std::string host, uint16_t port = 21); // Default FSP port is 21
    ~FSPMSession() override;

    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    // Get the FSP session for operations
    FSP_SESSION* getSession() { return _session.get(); }

private:
    std::unique_ptr<FSP_SESSION> _session;
    std::string _password; // FSP password (empty by default)
    std::mutex _session_mutex; // Protect session operations from concurrent access
};


/********************************************************
 * MFile
 ********************************************************/

class FSPMFile: public MFile
{
public:
    FSPMFile(std::string path);
    ~FSPMFile();

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
    std::shared_ptr<FSPMSession> _session;
    FSP_DIR* _dir_handle = nullptr;  // Directory handle for this instance

    // Helper to get FSP session from session
    FSP_SESSION* getSession() { return _session ? _session->getSession() : nullptr; }

private:
    void openDir(std::string path);
    void closeDir();
    bool pathValid(std::string path);

    int entry_index = 0;
    std::vector<FSP_RDENTRY> _dir_entries;
    size_t _current_entry = 0;
};


/********************************************************
 * MStream
 ********************************************************/

class FSPMStream: public MStream {
public:
    FSPMStream(std::string& path): MStream(path) {
    }
    ~FSPMStream() override {
        close();
    }

    // MStream methods
    bool isOpen() override;
    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return true; };

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
    std::shared_ptr<FSPMSession> _session;
    FSP_FILE* _file_handle = nullptr;

private:
    std::ios_base::openmode _mode;
    std::string _mode_str;
    void setModeString(std::ios_base::openmode mode);
};


/********************************************************
 * MFileSystem
 ********************************************************/

class FSPMFileSystem: public MFileSystem
{
public:
    FSPMFileSystem(): MFileSystem("fsp") {
        isRootFS = true;
    };

    bool handles(std::string name) {
        return mstr::equals(name, (char *)"fsp:", false);
    }

    MFile* getFile(std::string path) override {
        return new FSPMFile(path);
    }
};


#endif // MEATLOAF_NETWORK_FSP
