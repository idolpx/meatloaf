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

// .TAP - The raw tape image format
//
// https://en.wikipedia.org/wiki/Commodore_Datasette
// https://vice-emu.sourceforge.io/vice_17.html#SEC330
// https://ist.uwaterloo.ca/~schepers/formats/TAP.TXT
// https://sourceforge.net/p/tapclean/gitcode/ci/master/tree/
// https://github.com/binaryfields/zinc64/blob/master/doc/Analyzing%20C64%20tape%20loaders.txt
// https://web.archive.org/web/20170117094643/http://tapes.c64.no/
// https://web.archive.org/web/20191021114418/http://www.subchristsoftware.com:80/finaltap.htm
//


#ifndef MEATLOAF_MEDIA_TAP
#define MEATLOAF_MEDIA_TAP

#include "../meatloaf.h"
#include "../meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class TAPMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    TAPMStream(std::shared_ptr<MStream> is) : MMediaStream(is) { };

protected:
    struct Header {
        char name[24];
    };

    struct Entry {
        uint8_t entry_type;
        uint8_t file_type;
        uint16_t start_address;
        uint16_t end_address;
        uint16_t free_1;
        uint32_t data_offset;
        uint32_t free_2;
        char filename[16];
    };

    bool readHeader() override {
        Debug_printv("here");
        containerStream->seek(0x28);
        if (containerStream->read((uint8_t*)&header, sizeof(header)))
            return true;

        return false;
    }

    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    Header header;
    Entry entry;

private:
    friend class TAPMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class TAPMFile: public MFile {
public:

    TAPMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        //mstr::toUTF8(media_image);
    };
    
    ~TAPMFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override;

    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override { return false; };

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class TAPMFileSystem: public MFileSystem
{
public:
    TAPMFileSystem(): MFileSystem("tap") {};

    bool handles(std::string fileName) override {
        return byExtension(".tap", fileName);
    }

    MFile* getFile(std::string path) override {
        return new TAPMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_TAP */
