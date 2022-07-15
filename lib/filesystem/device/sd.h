// SD:// - Secure Digital Card File System
// https://en.wikipedia.org/wiki/SD_card
// https://github.com/arduino-libraries/SD
//

#ifndef MEATFILE_DEFINES_SDFS_H
#define MEATFILE_DEFINES_SDFS_H

#include "meat_io.h"

#include "../../include/global_defines.h"
#include "../../include/make_unique.h"

#include "../device/flash.h"
#include "fnFsSD.h"

#include "device_db.h"
#include "peoples_url_parser.h"

#include <dirent.h>
#include <string.h>

#define _filesystem fnSDFAT

/********************************************************
 * MFile
 ********************************************************/

class SDFile: public FlashFile
{
friend class SDOStream;
friend class SDIStream;

public:
    SDFile(std::string path) : FlashFile(path) {
        this->parseUrl(path);
    };
    ~SDFile() { }

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
    void closeDir() override;
};


/********************************************************
 * SDHandle
 ********************************************************/

class SDHandle : public FlashHandle 
{
public:

    SDHandle()
    {
        //Serial.println("*** Creating flash handle");
        memset(&file_h, 0, sizeof(file_h));

        dispose();
    };
    ~SDHandle() { };

};

/********************************************************
 * MStreams O
 ********************************************************/

class SDOStream: public FlashOStream {
public:
    // MStream methods
    SDOStream(std::string& path) : FlashOStream(path) {
        localPath = path;
        handle = std::make_unique<SDHandle>();
    }

protected:
    std::unique_ptr<SDHandle> handle;    
};


/********************************************************
 * MStreams I
 ********************************************************/

class SDIStream: public FlashIStream {
public:
    SDIStream(std::string& path) : FlashIStream(path) {
        localPath = path;
        handle = std::make_unique<SDHandle>();
    }

protected:
    std::unique_ptr<SDHandle> handle;
};


/********************************************************
 * MFileSystem
 ********************************************************/

class SDFileSystem: public MFileSystem 
{
private:
    MFile* getFile(std::string path) override {
        PeoplesUrlParser url;

        url.parseUrl(path);

        device_config.basepath( std::string("/sd/") );

        return new FlashFile(url.path);
    }

    bool handles(std::string name) {
        std::string pattern = "sd:";
        return mstr::equals(name, pattern, false);
    }
public:
    SDFileSystem(): MFileSystem("sd") {};
};


#endif // MEATFILE_DEFINES_SD_H
