#ifndef MEATFILE_DEFINES_FSLITTLE_H
#define MEATFILE_DEFINES_FSLITTLE_H

#include "meat_io.h"

#include "../../include/global_defines.h"
#include "../../include/make_unique.h"

#include "esp_littlefs.h"

#include <string.h>



/********************************************************
 * MFileSystem
 ********************************************************/

class LittleFileSystem: public MFileSystem 
{
    MFile* getFile(std::string path) override;

public:
    LittleFileSystem() : MFileSystem("littleFS") {};

    bool handles(std::string path);

    static lfs_t lfsStruct;
};



/********************************************************
 * MFile
 ********************************************************/

class LittleFile: public MFile
{
friend class LittleOStream;
friend class LittleIStream;

public:
    LittleFile(std::string path) {
        parseUrl(path);
        if(!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;
    };
    ~LittleFile() {
        //Serial.printf("*** Destroying littlefile %s\n", url.c_str());
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
    lfs_dir_t dir;
    bool dirOpened = false;
    bool _valid;
    std::string _pattern;

    bool pathValid(std::string path);

};


/********************************************************
 * LittleHandle
 ********************************************************/

class LittleHandle {
public:
    int rc;
    lfs_file_t lfsFile;

    LittleHandle() : rc(-255) 
    {
        //Serial.println("*** Creating little handle");
        memset(&lfsFile, 0, sizeof(lfsFile));
    };
    ~LittleHandle();
    void obtain(int flags, std::string localPath);
    void dispose();

private:
    int flags;
};

/********************************************************
 * MStreams O
 ********************************************************/

class LittleOStream: public MOStream {
public:
    // MStream methods
    LittleOStream(std::string& path) {
        localPath = path;
        handle = std::make_unique<LittleHandle>();
    }
    size_t position() override;
    void close() override;
    bool open() override;
    ~LittleOStream() override {
        close();
    }

    // MOStream methods
    //size_t write(uint8_t) override;
    size_t write(const uint8_t *buf, size_t size) override;
    bool isOpen();

protected:
    std::string localPath;

    std::unique_ptr<LittleHandle> handle;    
};


/********************************************************
 * MStreams I
 ********************************************************/

class LittleIStream: public MIStream {
public:
    LittleIStream(std::string& path) {
        localPath = path;
        handle = std::make_unique<LittleHandle>();
    }
    // MStream methods
    size_t position() override;
    void close() override;
    bool open() override;
    ~LittleIStream() override {
        close();
    }

    // MIStream methods
    size_t available() override;
    size_t size() override;
    //uint8_t read() override;
    size_t read(uint8_t* buf, size_t size) override;
    bool isOpen();
    virtual bool seek(size_t pos) override;
    virtual bool seek(size_t pos, SeekMode mode) override;

protected:
    std::string localPath;

    std::unique_ptr<LittleHandle> handle;    
};



#endif
