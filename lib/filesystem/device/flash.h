#ifndef MEATFILE_DEFINES_FLASHFS_H
#define MEATFILE_DEFINES_FLASHFS_H

#include "meat_io.h"

#ifdef FLASH_SPIFFS
#include "esp_spiffs.h"
#endif

#include "../../include/global_defines.h"
#include "../../include/make_unique.h"

#include "device_db.h"

#include <dirent.h>
#include <string.h>


/********************************************************
 * MFileSystem
 ********************************************************/

class FlashFileSystem: public MFileSystem 
{
    bool handles(std::string path);
    
public:
    FlashFileSystem() : MFileSystem("FlashFS") {};
    MFile* getFile(std::string path) override;

};



/********************************************************
 * MFile
 ********************************************************/

class FlashFile: public MFile
{
friend class FlashIStream;

public:
    std::string basepath = "";
    
    FlashFile(std::string path) {
        basepath = device_config.basepath();

        parseUrl(path);
        if(!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        //Debug_printv("basepath[%s] path[%s]", basepath.c_str(), this->path.c_str());
    };
    ~FlashFile() {
        //Serial.printf("*** Destroying flashfile %s\n", url.c_str());
        closeDir();
    }

    //MFile* cd(std::string newDir);
    bool isDirectory() override;
    MStream* meatStream() override ; // has to return OPENED stream
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;
    size_t size() override ;
    bool remove() override ;
    bool rename(std::string dest);
    MStream* createIStream(std::shared_ptr<MStream> src);

protected:
    DIR* dir;
    bool dirOpened = false;

private:
    virtual void openDir(std::string path);
    virtual void closeDir();

    bool _valid;
    std::string _pattern;

    bool pathValid(std::string path);

};


/********************************************************
 * FlashHandle
 ********************************************************/

class FlashHandle {
public:
    //int rc;
    FILE* file_h;

    FlashHandle() 
    {
        //Debug_printv("*** Creating flash handle");
        memset(&file_h, 0, sizeof(file_h));
    };
    ~FlashHandle();
    void obtain(std::string localPath, std::string mode);
    void dispose();

private:
    int flags;
};


/********************************************************
 * MStream I
 ********************************************************/

class FlashIStream: public MStream {
public:
    FlashIStream(std::string& path) {
        localPath = path;
        handle = std::make_unique<FlashHandle>();
    }
    ~FlashIStream() override {
        close();
    }

    // MStream methods
    size_t available() override;
    size_t size() override;    
    size_t position() override;

    virtual bool seek(size_t pos) override;
    virtual bool seek(size_t pos, int mode) override;    

    void close() override;
    bool open() override;

    // MStream methods
    //uint8_t read() override;
    size_t read(uint8_t* buf, size_t size) override;
    size_t write(const uint8_t *buf, size_t size) override;

    bool isOpen();

protected:
    std::string localPath;

    std::unique_ptr<FlashHandle> handle;

private:
    size_t _size;
};



#endif // MEATFILE_DEFINES_FLASHFS_H
