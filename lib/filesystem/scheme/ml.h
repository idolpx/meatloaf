// ML:// - Meatloaf Server Protocol
// 


#ifndef MEATFILE_DEFINES_FSML_H
#define MEATFILE_DEFINES_FSML_H

#include "scheme/http.h"

#include "peoples_url_parser.h"

#include <ArduinoJson.h>


/********************************************************
 * File
 ********************************************************/

class MLFile: public HttpFile {

public:
    MLFile(std::string path): HttpFile(path) {};
    ~MLFile() {};

    MIStream* inputStream() override; // file on ML server = standard HTTP file available via GET
};


/********************************************************
 * Streams
 ********************************************************/

class MLIStream: public HttpIStream {

public:
    MLIStream(std::string path) : HttpIStream(path) {};
    ~MLIStream() {};

    bool open() override;
};


/********************************************************
 * FS
 ********************************************************/

class MLFileSystem: public MFileSystem
{
    MFile* getFile(std::string path) override {
        // Debug_printv("MLFileSystem::getFile(%s)", path.c_str());
        return new MLFile(path);
    }

    bool handles(std::string name) {
        std::string pattern = "ml:";
        return mstr::startsWith(name, pattern.c_str(), false);
    }

public:
    MLFileSystem(): MFileSystem("meatloaf") {};
};


#endif