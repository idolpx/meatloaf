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

// .LNX - LyNX containers
//
// https://ist.uwaterloo.ca/~schepers/formats/LNX.TXT
//


#ifndef MEATLOAF_MEDIA_LNX
#define MEATLOAF_MEDIA_LNX

#include "meatloaf.h"
#include "meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class LNXMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    LNXMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // LNX uses 254-byte blocks
        block_size = 254;

        // Read Header
        readHeader();

        loadEntries();
    };

protected:
    struct Header {
        std::string signature;
        uint16_t directory_blocks;
        uint16_t entry_count;
    };

    struct Entry {
        std::string filename;
        uint16_t block_count;
        std::string type;
        uint8_t lsu;  // Last Sector Used
        uint16_t rel_record_size;  // For REL files
        uint32_t offset;
        uint32_t size;
    };

    std::vector<Entry> entries;
    int8_t loadEntries();
    void skipBasicLoader();

    bool readHeader() override;
    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    Header header;
    Entry entry;

private:
    friend class LNXMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class LNXMFile: public MFile {
public:

    LNXMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
        isPETSCII = true;
    };

    ~LNXMFile() {
        // don't close the stream here! It will be used by shared ptr to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<LNXMStream>(is);
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class LNXMFileSystem: public MFileSystem
{
public:
    LNXMFileSystem(): MFileSystem("lnx") {};

    bool handles(std::string fileName) override {
        return byExtension(".lnx", fileName);
    }

    MFile* getFile(std::string path) override {
        return new LNXMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_LNX */
