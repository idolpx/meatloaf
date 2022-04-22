#ifndef MEATFILE_DEFINES_FLASHFS_H
#define MEATFILE_DEFINES_FLASHFS_H

#include "meat_io.h"

#include "../../include/global_defines.h"
#include "../../include/make_unique.h"

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
friend class FlashOStream;
friend class FlashIStream;

public:
    FlashFile(std::string path) {
        parseUrl(path);
        if(!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;
    };
    ~FlashFile() {
        //Serial.printf("*** Destroying flashfile %s\n", url.c_str());
        closeDir();
    }

    //MFile* cd(std::string newDir);
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
    MIStream* createIStream(std::shared_ptr<MIStream> src);

private:
    void openDir(std::string path);
    void closeDir();
    DIR* dir;
    bool dirOpened = false;
    bool _valid;
    std::string _pattern;

    bool pathValid(std::string path);

};


/********************************************************
 * FlashHandle
 ********************************************************/

class FlashHandle {
public:
    int rc;
    FILE* lfsFile;

    FlashHandle() : rc(-255) 
    {
        //Serial.println("*** Creating flash handle");
        memset(&lfsFile, 0, sizeof(lfsFile));
    };
    ~FlashHandle();
    void obtain(std::string localPath, std::string mode);
    void dispose();

private:
    int flags;
};

/********************************************************
 * MStreams O
 ********************************************************/

class FlashOStream: public MOStream {
public:
    // MStream methods
    FlashOStream(std::string& path) {
        localPath = path;
        handle = std::make_unique<FlashHandle>();
    }
    size_t position() override;
    void close() override;
    bool open() override;
    ~FlashOStream() override {
        close();
    }

    // MOStream methods
    //size_t write(uint8_t) override;
    size_t write(const uint8_t *buf, size_t size) override;
    bool isOpen();

protected:
    std::string localPath;

    std::unique_ptr<FlashHandle> handle;    
};


/********************************************************
 * MStreams I
 ********************************************************/

class FlashIStream: public MIStream {
public:
    FlashIStream(std::string& path) {
        localPath = path;
        handle = std::make_unique<FlashHandle>();
    }
    // MStream methods
    size_t position() override;
    void close() override;
    bool open() override;
    ~FlashIStream() override {
        close();
    }

    // MIStream methods
    size_t available() override;
    size_t size() override;
    //uint8_t read() override;
    size_t read(uint8_t* buf, size_t size) override;
    bool isOpen();
    virtual bool seek(size_t pos) override;
    virtual bool seek(size_t pos, int mode) override;

protected:
    std::string localPath;

    std::unique_ptr<FlashHandle> handle;

private:
    size_t _size;
};



#endif // MEATFILE_DEFINES_FLASHFS_H
