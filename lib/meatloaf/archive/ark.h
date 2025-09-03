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

// .ARK - ARKive containers
// .SRK - Compressed ARKive archives (very rare)
//
// https://ist.uwaterloo.ca/~schepers/formats/ARK-SRK.TXT
// https://www.zimmers.net/anonftp/pub/cbm/crossplatform/converters/unix/unkar.c
//


#ifndef MEATLOAF_MEDIA_ARK
#define MEATLOAF_MEDIA_ARK

#include "../meatloaf.h"
#include "../meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class ARKMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    ARKMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // Read Header
        readHeader();

        // Get the entry count
        containerStream->seek(0);
        uint8_t count;
        readContainer((uint8_t *)&count, 1);
        entry_count = count;
    };

protected:
    struct Header {
        std::string name;
        std::string id_dos;
    };

    struct __attribute__ ((__packed__)) Entry {
        uint8_t file_type;
        uint8_t lsu_byte;           // Last Sector Usage (bytes used in the last sector)
        char filename[16];
        uint8_t rel_record_length;  // Or GEOS file structure (Sequential / VLIR file)
        uint8_t geos_file_type;     // $00 - Non-GEOS (normal C64 file)
        uint8_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hour;
        uint8_t minute;
        uint8_t rel_block_count;    // REL file side sector block count (side sector info contained at end of file)
        uint8_t rel_lsu;            // Number of bytes+1 used in the last side sector entry
        uint16_t blocks;
    };

    bool readHeader() override { return true; };
    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    Header header;
    Entry entry;

private:
    friend class ARKMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class ARKMFile: public MFile {
public:

    ARKMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        isPETSCII = true;
    };
    
    ~ARKMFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<ARKMStream>(is);
    }

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

class ARKMFileSystem: public MFileSystem
{
public:
    ARKMFileSystem(): MFileSystem("ark") {};

    bool handles(std::string fileName) override {
        return byExtension(".ark", fileName);
    }

    MFile* getFile(std::string path) override {
        return new ARKMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_ARK */
