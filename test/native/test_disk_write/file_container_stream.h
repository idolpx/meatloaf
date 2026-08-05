#ifndef TEST_FILE_CONTAINER_STREAM
#define TEST_FILE_CONTAINER_STREAM

#include <cstdio>
#include <string>
#include <vector>
#include "meatloaf.h"

// A bottom MStream backed by a real file on disk, so images the tests produce
// can be handed straight to c1541 without conversion.
class FileContainerStream : public MStream
{
public:
    // initial_size > 0 creates (or truncates) the file zero-filled to that
    // length; initial_size == 0 opens an existing file and adopts its size.
    FileContainerStream(const std::string& path, uint32_t initial_size = 0)
        : MStream(path), m_path(path)
    {
        if (initial_size > 0)
        {
            m_fp = fopen(path.c_str(), "w+b");
            if (m_fp != nullptr)
            {
                std::vector<uint8_t> zeros(initial_size, 0);
                fwrite(zeros.data(), 1, initial_size, m_fp);
                fflush(m_fp);
                _size = initial_size;
            }
        }
        else
        {
            m_fp = fopen(path.c_str(), "r+b");
            if (m_fp != nullptr)
            {
                fseek(m_fp, 0, SEEK_END);
                _size = (uint32_t)ftell(m_fp);
            }
        }
        _position = 0;
        if (m_fp != nullptr)
            fseek(m_fp, 0, SEEK_SET);
    }

    ~FileContainerStream() override { close(); }

    bool isOpen() override { return m_fp != nullptr; }
    bool isRandomAccess() override { return true; }

    bool open(std::ios_base::openmode mode) override { (void)mode; return isOpen(); }

    void close() override
    {
        if (m_fp != nullptr) { fclose(m_fp); m_fp = nullptr; }
    }

    uint32_t read(uint8_t* buf, uint32_t size) override
    {
        if (m_fp == nullptr) return 0;
        uint32_t n = (uint32_t)fread(buf, 1, size, m_fp);
        _position += n;
        return n;
    }

    uint32_t write(const uint8_t* buf, uint32_t size) override
    {
        if (m_fp == nullptr) return 0;
        uint32_t n = (uint32_t)fwrite(buf, 1, size, m_fp);
        fflush(m_fp);
        _position += n;
        if (_position > _size) _size = _position;
        return n;
    }

    bool seek(uint32_t pos) override
    {
        if (m_fp == nullptr) return false;
        if (fseek(m_fp, (long)pos, SEEK_SET) != 0) return false;
        _position = pos;
        return true;
    }

    uint32_t size() override { return _size; }
    uint32_t available() override { return _size > _position ? _size - _position : 0; }
    uint32_t position() override { return _position; }

private:
    std::string m_path;
    FILE* m_fp = nullptr;
};

#endif
