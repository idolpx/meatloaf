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

/********************************************************
 * Streams
 ********************************************************/

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

    bool readHeader() override {
        containerStream->seek(0x00);
        if (containerStream->read((uint8_t*)&header, sizeof(header)))
            return true;

        return false;
    }

    // bool getNextImageEntry() override {
    //     if ( entry_index == 0 ) {
    //         entry_index = 1;
    //         readHeader();

    //         _size = ( containerStream->size() - sizeof(header) );

    //         return true;
    //     }
    //     return false;
    // }

    // // For files with no directory structure
    // // tap, crt, tar
    // std::string seekNextEntry() override {
    //     seekCalled = true;
    //     if ( getNextImageEntry() ) {
    //         return header.filename;
    //     }
    //     return "";
    // };

    bool isRandomAccess() override { return false; };
    bool isBrowsable() override { return false; };

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };

    Header header;

private:
    friend class P00MFile;
};


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

    bool isDirectory() override { return false; };
    bool rewindDirectory() override { return false; };
    MFile* getNextFileInDir() override { return nullptr; };

    bool isDir = false;
    bool dirIsOpen = false;
};



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
                ".p??",
                ".s??",
                ".r??"
            }, 
            fileName, true
        );
    }

    MFile* getFile(std::string path) override {
        return new P00MFile(path);
    }
};

#endif // MEATLOAF_MEDIA_P00