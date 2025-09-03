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

#ifndef MEATLOAF_DEVICE_FTP
#define MEATLOAF_DEVICE_FTP

#include "meatloaf.h"

#include "fnFS.h"
#include "fnFsFTP.h"
#include "fnFTP.h"

#include "../../../include/debug.h"

#include "make_unique.h"

#include <dirent.h>
#include <string.h>


/********************************************************
 * MFile
 ********************************************************/

class FTPMFile: public MFile
{

public:
    std::string basepath = "";
    
    FTPMFile(std::string path): MFile(path) {

        Debug_printv("path[%s]", path.c_str());
        Debug_printv("host[%s]", host.c_str());
        Debug_printv("port[%s]", port.c_str());
        Debug_printv("user[%s]", user.c_str());
        Debug_printv("password[%s]", password.c_str());
        if (!_fsFTP->start(host.c_str()))
        {
            Debug_printv("Failed to mount %s:%s", host.c_str(), port.c_str());
            m_isNull = true;
            return;
        }

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry( name );

        if (!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        m_rootfs = true;
        //Debug_printv("basepath[%s] path[%s] valid[%d]", basepath.c_str(), this->path.c_str(), m_isNull);
    };
    ~FTPMFile() {
        Debug_printv("*** Destroying FTPMFile [%s]", url.c_str());
        closeDir();
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;

    bool remove() override ;
    bool rename(std::string dest);


    bool readEntry( std::string filename );

protected:
    DIR* dir;
    bool dirOpened = false;

private:
    FileSystem *_fs = nullptr;

    virtual void openDir(std::string path);
    virtual void closeDir();

    bool _valid;
    std::string _pattern;

    bool pathValid(std::string path);
    std::unique_ptr<FileSystemFTP> _fsFTP;
};


/********************************************************
 * FTPHandle
 ********************************************************/

class FTPHandle {
public:
    //int rc;
    FILE* file_h = nullptr;

    FTPHandle() 
    {
        //Debug_printv("*** Creating flash handle");
        memset(&file_h, 0, sizeof(file_h));
    };
    ~FTPHandle();
    void obtain(std::string localPath, std::string mode);
    void dispose();

private:
    int flags = 0;
};


/********************************************************
 * MStream I
 ********************************************************/

class FTPMStream: public MStream {
public:
    FTPMStream(std::string& path) {
        localPath = path;
        handle = std::make_unique<FTPHandle>();
        url = path;
    }
    ~FTPMStream() override {
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
    std::string localPath;

    std::unique_ptr<FTPHandle> handle;
};


/********************************************************
 * MFileSystem
 ********************************************************/

class FTPMFileSystem: public MFileSystem 
{
public:
    FTPMFileSystem(): MFileSystem("tnfs") {};

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"tnfs:", false) )
            return true;

        return false;
    }

    MFile* getFile(std::string path) override {
        return new FTPMFile(path);
    }
};


#endif // MEATLOAF_DEVICE_FTP
