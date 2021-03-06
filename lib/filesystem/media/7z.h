#ifndef MEATFILE_DEFINES_7ZIP_H
#define MEATFILE_DEFINES_7ZIP_H

#include "meat_io.h"


/********************************************************
 * Streams implementations
 ********************************************************/

class SevenZipIStream: MIstream {
public:
    
    SevenZipIStream(MIstream* srcStream): srcStr(srcStream) {
        // this stream must be able to return a stream of
        // UNPACKED file contents
        // additionaly it has to implement getNextEntry()
        // which skips data in the stream to next file in zip
    }
    // MStream methods
    bool seek(uint32_t pos, SeekMode mode) override;
    bool seek(uint32_t pos) override;
    size_t position() override;
    void close() override;
    bool open() override;
    ~SevenZipIStream() {
        close();
    }

    // MIstream methods
    int available() override;
    uint8_t read() override;
    size_t read(uint8_t* buf, size_t size) override;
    bool isOpen() override;

protected:
    MStream* srcStr;

};


/********************************************************
 * Files implementations
 ********************************************************/

class SevenZipFile: public MFile
{
public:
    SevenZipFile(std::string path) : MFile(path) {};
    MIstream* createIStream(MIstream* src) override;

    bool isDirectory() override;
    MIstream* inputStream() override ; // has to return OPENED stream
    MOstream* outputStream() override ; // has to return OPENED stream
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;
    size_t size() override ;
    bool remove() override ;
    bool rename(const char* dest);


    bool isBrowsable() override {
        return true;
    }

    // Browsable methods
    MFile* getNextEntry() override; // skips the stream until the beginnin of next file

};



/********************************************************
 * FS implementations
 ********************************************************/

class SevenZipFileSystem: public MFileSystem 
{
    MFile* getFile(std::string path) {
        return new SevenZipFile(path);
    };


public:
    SevenZipFileSystem(): MFileSystem("7z"){}

    bool handles(std::string fileName) {
        return fileName.rfind(".7z") == fileName.length()-4;
    }

private:
};



#endif