// TNFS:// - Spectranet File System
// https://www.bytedelight.com/?page_id=3515
// https://github.com/FujiNetWIFI/spectranet/blob/master/tnfs/tnfs-protocol.md
//

#ifndef MEATFILE_DEFINES_TNFS_H
#define MEATFILE_DEFINES_TNFS_H

#include "meat_io.h"

#include "../../include/global_defines.h"
#include "../../include/make_unique.h"

#include "../device/flash.h"
#include "fnFsTNFS.h"

#include "device_db.h"
#include "peoples_url_parser.h"

#include <dirent.h>
#include <string.h>

#define _filesystem fnTNFS

/********************************************************
 * MFile
 ********************************************************/

class TNFSFile: public FlashFile
{
friend class TNFSOStream;
friend class TNFSIStream;

public:
    TNFSFile(std::string path) : FlashFile(path) {
        this->parseUrl(path);
    };
    ~TNFSFile() { }

    bool isDirectory() override;
    MIStream* inputStream() override ; // has to return OPENED stream
    MOStream* outputStream() override ; // has to return OPENED stream
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;
    size_t size() override ;
    bool remove() override ;
    bool rename(std::string dest);

private:
    void openDir(std::string path) override;
};


/********************************************************
 * TNFSHandle
 ********************************************************/

class TNFSHandle : public FlashHandle 
{
public:

    TNFSHandle()
    {
        //Serial.println("*** Creating flash handle");
        memset(&file_h, 0, sizeof(file_h));

        dispose();
    };
    ~TNFSHandle() { };

};

/********************************************************
 * MStreams O
 ********************************************************/

class TNFSOStream: public FlashOStream {
public:
    // MStream methods
    TNFSOStream(std::string& path) : FlashOStream(path) {
        localPath = path;
        handle = std::make_unique<TNFSHandle>();
    }

protected:
    std::unique_ptr<TNFSHandle> handle;    
};


/********************************************************
 * MStreams I
 ********************************************************/

class TNFSIStream: public FlashIStream {
public:
    TNFSIStream(std::string& path) : FlashIStream(path) {
        localPath = path;
        handle = std::make_unique<TNFSHandle>();
    }

protected:
    std::unique_ptr<TNFSHandle> handle;
};


/********************************************************
 * MFileSystem
 ********************************************************/

class TNFSFileSystem: public MFileSystem 
{
private:
    MFile* getFile(std::string path) override {
        PeoplesUrlParser url;

        url.parseUrl(path);

        if (!fnTNFS.running())
            fnTNFS.start(url.host.c_str(), TNFS_DEFAULT_PORT, url.path.c_str() , url.user.c_str(), url.pass.c_str());

        std::string basepath = fnTNFS.basepath();
        // basepath += std::string("/");
        device_config.basepath( basepath );

        return new FlashFile(url.path);
    }

    bool handles(std::string name) {
        std::string pattern = "tnfs:";
        return mstr::equals(name, pattern, false);
    }
public:
    TNFSFileSystem(): MFileSystem("tnfs") {};
};


#endif // MEATFILE_DEFINES_TNFS_H
