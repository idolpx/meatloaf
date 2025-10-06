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

// .CRT - The CRT cartridge image format
// https://vice-emu.sourceforge.io/vice_17.html#SEC369
// https://ist.uwaterloo.ca/~schepers/formats/CRT.TXT
//


#ifndef MEATLOAF_MEDIA_CRT
#define MEATLOAF_MEDIA_CRT

#include "meatloaf.h"
#include "meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class CRTMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    CRTMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // Read Header
        readHeader();
    };

protected:
    struct Header {
        char name[16];
    };

    struct Entry {
        char filename[16];
        uint8_t file_type;
        uint8_t file_start_address[2]; // from tcrt file system at 0xD8
        uint8_t file_size[3];
        uint8_t file_load_address[2];
        uint16_t bundle_compatibility;
        uint16_t bundle_main_start;
        uint16_t bundle_main_length;
        uint16_t bundle_main_call_address;
    };

    bool readHeader() override {
        containerStream->seek(0x18);
        containerStream->read((uint8_t*)&header, sizeof(header));
    }

    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index = 0 ) override;
    bool readEntry( uint16_t index ) override;
    bool seekPath(std::string path) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;

    Header header;
    Entry entry;

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

private:
    friend class CRTMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class CRTMFile: public MFile {
public:

    CRTMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        isPETSCII = true;
    };
    
    ~CRTMFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<CRTMStream>(is);
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class CRTMFileSystem: public MFileSystem
{
public:
    CRTMFileSystem(): MFileSystem("crt") {};

    bool handles(std::string fileName) override {
        return byExtension(".crt", fileName);
    }

    MFile* getFile(std::string path) override {
        return new CRTMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_CRT */
