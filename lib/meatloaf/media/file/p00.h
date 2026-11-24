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

// .P00/P** - P00/S00/U00/R00 (Container files for the PC64 emulator)
// https://ist.uwaterloo.ca/~schepers/formats/PC64.TXT
//

#ifndef MEATLOAF_MEDIA_P00
#define MEATLOAF_MEDIA_P00

#include "meatloaf.h"
#include "meat_media.h"
#include "media/file/rel.h"
#include "cbm_defines.h"
#include "string_utils.h"

#include <new>
#include <utility>

/********************************************************
 * Streams
 ********************************************************/

class P00RelStorage;

class P00MStream : public MMediaStream {
    // override everything that requires overriding here

public:
    P00MStream(std::shared_ptr<MStream> is) : MMediaStream(is) {
        entry_count = 1;
        readHeader();
        _size = ( containerStream->size() - sizeof(header) );
        Debug_printv("name[%s] size[%d]", header.filename, _size);
    };

protected:
    struct Header {
        char signature[7];
        uint8_t pad1;
        char filename[16];
        uint8_t pad2;
        uint8_t rel_flag;
    };

public:
    static constexpr uint32_t HEADER_SIZE = 26;

    // A .r00 holds a REL file's records verbatim after the 26-byte header.
    uint8_t relRecordLength() override { return header.rel_flag; }
    std::unique_ptr<RELStorage> relStorage() override;

protected:
    bool readHeader() override {
        containerStream->seek(0x00);
        if (readContainer((uint8_t*)&header, sizeof(header)))
            return true;

        return false;
    }

    // This is a single load format so both of these are false
    bool isRandomAccess() override { return false; };
    bool isBrowsable() override { return false; };

    uint32_t readFile(uint8_t* buf, uint32_t size) override {
        uint32_t bytesRead = 0;

        bytesRead += readContainer(buf, size);

        return bytesRead;
    }
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };

    Header header;

private:
    friend class P00MFile;
    friend class P00RelStorage;
};

// Record bytes of a .r00: the container past the header. Holds the stream so
// it (and the container under it) lives as long as any RELMStream over it.
class P00RelStorage : public RELStorage {
public:
    explicit P00RelStorage(std::shared_ptr<P00MStream> stream) : s_(std::move(stream))
    {
        uint32_t total = s_->containerStream->size();
        size_ = total > P00MStream::HEADER_SIZE ? total - P00MStream::HEADER_SIZE : 0;
    }

    uint32_t size() override { return size_; }

    uint32_t readAt(uint32_t off, uint8_t* buf, uint32_t n) override
    {
        if (off >= size_) return 0;
        if (n > size_ - off) n = size_ - off;
        if (!s_->containerStream->seek(P00MStream::HEADER_SIZE + off)) return 0;
        uint32_t total = 0;
        while (total < n)
        {
            uint32_t r = s_->containerStream->read(buf + total, n - total);
            if (r == 0) break;
            total += r;
        }
        return total;
    }

    uint32_t writeAt(uint32_t off, const uint8_t* buf, uint32_t n) override
    {
        if (!writable() || off > size_) return 0;
        if (!s_->containerStream->seek(P00MStream::HEADER_SIZE + off)) return 0;
        uint32_t total = 0;
        while (total < n)
        {
            uint32_t w = s_->containerStream->write(buf + total, n - total);
            if (w == 0) break;
            total += w;
        }
        if (off + total > size_) size_ = off + total;
        return total;
    }

    // A file has no allocation limit beyond the medium: extend with zeros
    // (RELMStream writes the real record fill itself).
    uint8_t grow(uint32_t new_size) override
    {
        if (!writable()) return ST_WRITE_PROTECT_ON;
        if (new_size <= size_) return ST_OK;
        uint8_t zeros[64] = {0};
        if (!s_->containerStream->seek(P00MStream::HEADER_SIZE + size_)) return ST_DISK_FULL;
        while (size_ < new_size)
        {
            uint32_t chunk = new_size - size_;
            if (chunk > sizeof(zeros)) chunk = sizeof(zeros);
            uint32_t w = s_->containerStream->write(zeros, chunk);
            if (w == 0) return ST_DISK_FULL;
            size_ += w;
        }
        return ST_OK;
    }

    bool writable() override { return (s_->mode & std::ios_base::out) != 0; }

private:
    std::shared_ptr<P00MStream> s_;
    uint32_t size_ = 0;
};

inline std::unique_ptr<RELStorage> P00MStream::relStorage()
{
    if (header.rel_flag == 0)
        return nullptr;
    auto self = std::static_pointer_cast<P00MStream>(weak_from_this().lock());
    if (!self)
        return nullptr;
    // nothrow: plain new would abort on failure under -fno-exceptions.
    P00RelStorage* st = new (std::nothrow) P00RelStorage(std::move(self));
    return std::unique_ptr<RELStorage>(st);
}


/********************************************************
 * File implementations
 ********************************************************/

class P00MFile: public MFile {
public:

    P00MFile(std::string path, bool is_dir = false): MFile(path) {};
    
    ~P00MFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<P00MStream>(is);
    }

    // Creates an empty .r00: header only. The header is written through a
    // write-only open (which creates the file), then the file is reopened
    // in|out - a plain 'out' open of an existing file would truncate it.
    // Anything that fails removes what was created.
    std::shared_ptr<MStream> createRelStream(uint8_t record_length) override
    {
        if (record_length == 0 || sourceFile == nullptr || sourceFile->exists())
            return nullptr;

        P00MStream::Header h;
        memset(&h, 0, sizeof(h));
        memcpy(h.signature, "C64File", 7);
        std::string cbm = name;
        size_t dot = cbm.rfind('.');
        if (dot != std::string::npos) cbm.erase(dot);
        for (auto& c : cbm) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        cbm = mstr::toPETSCII2(cbm);
        memcpy(h.filename, cbm.data(), cbm.size() < 16 ? cbm.size() : 16);
        h.rel_flag = record_length;

        bool ok = false;
        {
            auto w = sourceFile->getSourceStream(std::ios_base::out);
            ok = w != nullptr && w->write((const uint8_t*)&h, sizeof(h)) == sizeof(h);
            if (w) w->close();
        }
        std::shared_ptr<MStream> result;
        if (ok)
        {
            auto src = sourceFile->getSourceStream(std::ios_base::in | std::ios_base::out);
            if (src != nullptr)
            {
                auto stream = std::static_pointer_cast<P00MStream>(getDecodedStream(src));
                stream->mode = std::ios_base::in | std::ios_base::out;
                auto storage = stream->relStorage();
                if (storage != nullptr)
                {
                    auto rel = std::make_shared<RELMStream>(std::move(storage), record_length);
                    // vdrive_rel_open: a new REL file has one record at OPEN.
                    if (rel->growTo(1) == ST_OK)
                        result = rel;
                }
            }
        }
        if (!result)
            sourceFile->remove();
        return result;
    }

    bool isDirectory() override { return false; };
    bool rewindDirectory() override { return false; };
    MFile* getNextFileInDir() override { return nullptr; };

    bool isDir = false;
    bool dirIsOpen = false;
};



/********************************************************
 * x00 header names (preferences.x00_header_names)
 ********************************************************/

// Type letter of a PC64 container name: 'p', 's', 'u' or 'r' for a name ending
// .p00/.s00/.u00/.r00 (case-insensitive), else 0.
inline char x00TypeLetter(const std::string& filename)
{
    size_t n = filename.size();
    if (n < 4 || filename[n - 4] != '.' || filename[n - 2] != '0' || filename[n - 1] != '0')
        return 0;
    char c = filename[n - 3];
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return (c == 'p' || c == 's' || c == 'u' || c == 'r') ? c : 0;
}

// CBM type name of an x00 type letter, LOWERCASE so toPETSCII2 lands in the
// unshifted range a C64 draws as capitals: "prg", "seq", "usr", "rel".
inline const char* x00TypeName(char letter)
{
    switch (letter) {
        case 'p': return "prg";
        case 's': return "seq";
        case 'u': return "usr";
        case 'r': return "rel";
    }
    return "";
}

// A P00 header's CBM name as UTF-8 (PETSCII converted once, here). False when
// the 26 bytes are not a "C64File\0" header.
inline bool x00HeaderName(const uint8_t* hdr, size_t n, std::string& utf8_name)
{
    if (n < P00MStream::HEADER_SIZE || memcmp(hdr, "C64File\0", 8) != 0)
        return false;
    size_t len = 0;
    while (len < 16 && hdr[8 + len] != 0) len++;
    utf8_name = mstr::toUTF8(std::string((const char*)hdr + 8, len));
    return true;
}

/********************************************************
 * FS
 ********************************************************/

class P00MFileSystem: public MFileSystem
{
public:
    P00MFileSystem(): MFileSystem("p00") {};

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".p00",
                ".s00",
                ".r00",
                ".c64"      // CCS64 freeze file format uses the .c64 extension too
            }, 
            fileName, true
        );
    }

    MFile* getFile(std::string path) override {
        return new P00MFile(path);
    }
};

#endif // MEATLOAF_MEDIA_P00