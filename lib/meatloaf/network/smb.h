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

//
// SMB:// - Server Messagee Block Protocol
// https://en.wikipedia.org/wiki/Server_Message_Block
//

#ifndef MEATLOAF_DEVICE_SMB
#define MEATLOAF_DEVICE_SMB

#include "meatloaf.h"

#include <smb2/libsmb2.h>

#include "fnFS.h"

#include "../../../include/debug.h"

#include "make_unique.h"

#include <dirent.h>
#include <string.h>
#include <fcntl.h>

// Helper function declarations
bool parseSMBPath(const std::string& path, std::string& share, std::string& share_path);

/********************************************************
 * MFile
 ********************************************************/

class SMBMFile: public MFile
{
public:
    std::string basepath = "";
    std::string share = "";
    std::string share_path = "";
    
    SMBMFile(std::string path): MFile(path) {
        m_rootfs = true;

        // Initialize SMB context
        _smb = smb2_init_context();
        if (!_smb) {
            Debug_printv("Failed to initialize SMB context");
            m_isNull = true;
            return;
        }

        // Set SMB2 version
        smb2_set_version(_smb, SMB2_VERSION_ANY);

        // extract share from path
        parseSMBPath(this->path, share, share_path);
        Debug_printv("path[%s] share[%s] share_path[%s]", this->path.c_str(), share.c_str(), share_path.c_str());

        // Connect to server/share
        if (password.size())
            smb2_set_password(_smb, password.c_str());

        if (smb2_connect_share(_smb, host.c_str(), share.c_str(), user.c_str()) < 0) {
            Debug_printv("error[%s] host[%s] share[%s] path[%s]", smb2_get_error(_smb), host.c_str(), share.c_str(), share_path.c_str());
            m_isNull = true;
            return;
        }

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry( name );

        if (!pathValid(this->path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        Debug_printv("SMB path[%s] valid[%d]", this->path.c_str(), !m_isNull);
    };
    ~SMBMFile() {
        closeDir();
        if (_smb) {
            smb2_destroy_context(_smb);
            _smb = nullptr;
        }
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
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

    bool readEntry( std::string filename );

protected:
    bool dirOpened = false;

    struct smb2_context *_smb;
    struct smb2dir *_handle_dir;

private:
    virtual void openDir(std::string path);
    virtual void closeDir();

    bool pathValid(std::string path);

};


/********************************************************
 * SMBHandle
 ********************************************************/

class SMBHandle {
public:
    struct smb2_context *_smb = nullptr;
    struct smb2fh *_handle = nullptr;

    SMBHandle()
    {
        //Debug_printv("*** Creating SMB handle");
        _smb = nullptr;
        _handle = nullptr;
    };
    ~SMBHandle();
    void obtain(std::string localPath, int smb_mode);
    void dispose();

private:
    int flags = 0;
};


/********************************************************
 * MStream I
 ********************************************************/

class SMBMStream: public MStream {
public:
    SMBMStream(std::string& path) {
        handle = std::make_unique<SMBHandle>();
        url = path;
    }
    ~SMBMStream() override {
        close();
    }

    // MStream methods
    bool isOpen();
    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    virtual bool seek(uint32_t pos) override;
    virtual bool seek(uint32_t pos, int mode) override;    

    virtual bool seekPath(std::string path) override {
        Debug_printv( "path[%s]", path.c_str() );
        return false;
    }


protected:
    std::unique_ptr<SMBHandle> handle;
};


/********************************************************
 * MFileSystem
 ********************************************************/

class SMBMFileSystem: public MFileSystem
{
public:
    SMBMFileSystem(): MFileSystem("smb") {};

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"smb:", false) )
            return true;

        return false;
    }

    MFile* getFile(std::string path) override {
        return new SMBMFile(path);
    }
};


#endif // MEATLOAF_DEVICE_SMB
