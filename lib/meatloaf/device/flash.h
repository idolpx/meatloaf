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

#ifndef TEST_NATIVE
#ifndef MEATLOAF_DEVICE_FLASH
#define MEATLOAF_DEVICE_FLASH

#include "meatloaf.h"

#include "make_unique.h"

#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

#include "../../include/debug.h"


/********************************************************
 * MFile
 ********************************************************/

class FlashMFile: public MFile
{

public:
    std::string basepath = "";
    
    FlashMFile(std::string path): MFile(path) {
        // parseUrl( path );

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry( name );

        if (!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        isWritable = true;

        if(isDirectory()) {
            size = 0;
        }
        else {
            struct stat info;
            stat( path.c_str(), &info);
            size = info.st_size;
        }

        //Debug_printv("basepath[%s] path[%s] valid[%d]", basepath.c_str(), this->path.c_str(), m_isNull);
    };
    ~FlashMFile() {
        //printf("*** Destroying flashfile %s\r\n", url.c_str());
        closeDir();
    }

    //MFile* cd(std::string newDir);
    bool isDirectory() override;
    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override;
    bool rmDir() override;
    bool exists() override;
    bool remove() override;
    bool rename(std::string dest) override;

    time_t getLastWrite() override;
    time_t getCreationTime() override;

    bool readEntry( std::string filename );

protected:
    DIR* dir;
    bool dirOpened = false;

    virtual void openDir(std::string path);
    virtual void closeDir();

    virtual bool pathValid(std::string path);
    bool _valid;
    std::string _pattern;

private:
};


/********************************************************
 * FlashHandle
 ********************************************************/

class FlashHandle {
public:
    //int rc;
    FILE* file_h = nullptr;

    FlashHandle() 
    {
        //Debug_printv("*** Creating flash handle");
        memset(&file_h, 0, sizeof(file_h));
    };
    ~FlashHandle();
    void obtain(std::string path, std::string mode);
    void dispose();

private:
    int flags = 0;
};


/********************************************************
 * MStream I
 ********************************************************/

class FlashMStream: public MStream {
public:
    FlashMStream(std::string& path, std::ios_base::openmode m): MStream(path) {
        mode = m;
        handle = std::make_unique<FlashHandle>();
        //url = path;
        //Debug_printv("url[%s] mode[%d]", url.c_str(), mode);
    }
    ~FlashMStream() override {
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

    virtual bool seek(uint32_t pos) override;

    virtual bool seekPath(std::string path) override {
        Debug_printv( "path[%s]", path.c_str() );
        return false;
    }


protected:
    std::unique_ptr<FlashHandle> handle;
};

/********************************************************
 * MFileSystem
 ********************************************************/

class FlashMFileSystem: public MFileSystem 
{
public:
    FlashMFileSystem() : MFileSystem("flash") {
        isRootFS = true;
    };

    bool handles(std::string path) override
    {
        return true; // fallback fs, so it must be last on FS list
    }

    MFile* getFile(std::string path) override
    {
        //Debug_printv("path[%s]", path.c_str());
        return new FlashMFile(path);
    }
};


#endif // MEATLOAF_DEVICE_FLASH
#endif // TEST_NATIVE
