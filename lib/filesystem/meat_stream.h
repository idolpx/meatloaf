#ifndef MEATFILE_STREAMS_H
#define MEATFILE_STREAMS_H

#include <Arduino.h>
#include "FS.h"
#include "../../include/make_unique.h"

/********************************************************
 * Universal streams
 ********************************************************/

class MStream 
{
public:
    virtual bool seek(uint32_t pos, SeekMode mode) = 0;
    virtual bool seek(uint32_t pos) = 0;
    virtual size_t position() = 0;
    virtual void close() = 0;
    virtual bool open() = 0;
    virtual ~MStream() = 0;
    virtual bool isOpen() = 0;
};

// template <class T>
// class MFileStream: public MStream {
//     std::unique_ptr<T> file;

// protected:
//     T* getFile() {
//         return file.get();
//     }
// public:
//     MFileStream(std::shared_ptr<T> f): file(f) {};
// };


class MOstream: public MStream {
public:
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buf, size_t size) = 0;
    virtual void flush() = 0;
};


class MIstream: public MStream {
public:
    virtual int available() = 0;
    virtual uint8_t read() = 0;
    virtual size_t read(uint8_t* buf, size_t size) = 0;
    bool pipeTo(MOstream* ostream);
};


#endif