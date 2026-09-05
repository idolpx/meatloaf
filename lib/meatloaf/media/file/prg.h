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

// .PRG, .C64 - Commodore 64/128 Standard PRG File
//
// https://www.infinite-loop.at/Power64/Documentation/Power64-ReadMe/AE-File_Formats.html#Section%20E.2
//

#ifndef MEATLOAF_MEDIA_PRG
#define MEATLOAF_MEDIA_PRG

#include "meatloaf.h"
#include "meat_media.h"

/********************************************************
 * Streams
 ********************************************************/

class PRGMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    PRGMStream(std::shared_ptr<MStream> is) : MMediaStream(is) {
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
    friend class PRGMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class PRGMFile: public MFile {
public:

    PRGMFile(std::string path, bool is_dir = false): MFile(path) { isDir = is_dir; };
    
    ~PRGMFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<PRGMStream>(is);
    }

    bool isDirectory() override { return false; };
    bool rewindDirectory() override { return false; };
    MFile* getNextFileInDir() override { return nullptr; };

    bool isDir = false;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class PRGMFileSystem: public MFileSystem
{
public:
    PRGMFileSystem(): MFileSystem("prg") {};

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".prg"
            }, 
            fileName, true
        );
    }

    MFile* getFile(std::string path) override {
        return new PRGMFile(path);
    }
};

#endif // MEATLOAF_MEDIA_PRG