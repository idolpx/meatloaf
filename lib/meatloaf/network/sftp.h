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

// SFTP:// - SSH File Transfer Protocol
// https://en.wikipedia.org/wiki/SSH_File_Transfer_Protocol
// NOTE: Requires libssh with SFTP support compiled in

#ifndef MEATLOAF_NETWORK_SFTP
#define MEATLOAF_NETWORK_SFTP

#include "meatloaf.h"
#include "meat_session.h"

extern "C" {
#include <libssh/libssh.h>
#include <libssh/sftp.h>
}

#include "../../../include/debug.h"

#include <memory>
#include <string>

// Only compile SFTP support if libssh has it available
#ifdef WITH_SFTP

/********************************************************
 * MSession - SFTP Session Management
 ********************************************************/

class SFTPMSession : public MSession {
public:
    SFTPMSession(std::string host, uint16_t port = 22);
    ~SFTPMSession() override;

    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    // Get the SSH session handle
    ssh_session getSSHSession() { return ssh_handle; }
    
    // Get the SFTP session handle
    sftp_session getSFTPSession() { return sftp_handle; }

    // Authentication methods
    void setCredentials(const std::string& username, const std::string& password);
    void setPrivateKey(const std::string& keypath);

private:
    ssh_session ssh_handle = nullptr;
    sftp_session sftp_handle = nullptr;
    
    std::string username;
    std::string password;
    std::string private_key_path;
    
    bool authenticatePassword();
    bool authenticatePublicKey();
};


/********************************************************
 * MFile - SFTP File
 ********************************************************/

class SFTPMFile: public MFile
{
public:
    SFTPMFile(std::string path);
    ~SFTPMFile();

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

protected:
    bool dirOpened = false;
    std::shared_ptr<SFTPMSession> _session;
    sftp_dir _dir_handle = nullptr;

    // Helper to get SFTP session from session
    sftp_session getSFTPSession() { return _session ? _session->getSFTPSession() : nullptr; }

private:
    void openDir(std::string path);
    void closeDir();
    bool pathValid(std::string path);
    
    sftp_attributes current_attrs = nullptr;
};


/********************************************************
 * MStream - SFTP Stream
 ********************************************************/

class SFTPMStream: public MStream {
public:
    SFTPMStream(std::string& path): MStream(path) {
    }
    ~SFTPMStream() override {
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
    std::shared_ptr<SFTPMSession> _session;
    sftp_file _file_handle = nullptr;

private:
    uint32_t mapOpenMode(std::ios_base::openmode mode);
    mode_t mapFileMode(std::ios_base::openmode mode);
};


/********************************************************
 * MFileSystem - SFTP Filesystem
 ********************************************************/

class SFTPMFileSystem: public MFileSystem
{
public:
    SFTPMFileSystem(): MFileSystem("sftp") {
        isRootFS = true;
    };

    bool handles(std::string name) {
        return mstr::equals(name, (char *)"sftp:", false);
    }

    MFile* getFile(std::string path) override {
        return new SFTPMFile(path);
    }
};

#endif // WITH_SFTP

#endif // MEATLOAF_NETWORK_SFTP
