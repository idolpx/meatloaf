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

// .SPY - SPYne containers
//
// https://ist.uwaterloo.ca/~schepers/formats/SPYNE.TXT
//
// A SPYne is an uncompressed self-extracting container, and its layout is the
// LNX model: files stored one after another, each occupying a whole number of
// 254-byte CBM blocks. The first 15 blocks are the extraction code (load
// address $02A7), the central directory starts at block 15, and the file data
// starts at the block after the directory ends.
//
// Read-only. Writing would mean rebuilding the directory and re-laying every
// following file, and the extraction code at the front is opaque machine code
// that a rewrite would have to keep consistent.


#ifndef MEATLOAF_MEDIA_SPY
#define MEATLOAF_MEDIA_SPY

#include "meatloaf.h"
#include "meat_media.h"

#include <vector>


/********************************************************
 * Streams
 ********************************************************/

class SPYMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    SPYMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // A SPYne block is 254 bytes - a CBM disk block with its two link
        // bytes stripped. The inherited MStream::block_size is the 256-byte
        // SECTOR size and must never be used for this geometry; see the LNX
        // entry in AGENTS.md for what happens when it is.
        block_size = 254;
    };

protected:
    // One central-directory entry. The container stores 30 bytes of these
    // fields plus two filler bytes - see SPY_ENTRY_SIZE in spy.cpp for why the
    // filler is not simply part of the stride.
    struct Entry {
        std::string filename;   // $A0 padding already stripped, still PETSCII
        uint8_t  file_type = 0; // $81 SEQ, $82 PRG, $83 USR
        uint16_t checksum = 0;  // 16-bit sum, no carry, over the file's bytes
        uint8_t  lsu = 0;       // last sector usage
        uint16_t blocks = 0;    // length in 254-byte blocks
        uint32_t offset = 0;    // container offset of the file's first byte
        uint32_t size = 0;      // derived: (blocks - 1) * 254 + (lsu - 1)
    };

    std::vector<Entry> entries;
    bool header_parsed = false;
    bool header_ok = false;

    // Walks the central directory. Cheap - it never touches file data, and it
    // stops at the entry whose "last file" marker says it is the last.
    bool loadEntries();

    bool readHeader() override;
    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

    Entry entry;

private:
    friend class SPYMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class SPYMFile: public MFile {
public:

    SPYMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
        isCBM = true;
        media_archive = name;
    };

    ~SPYMFile() {
        // don't close the stream here! It will be used by shared ptr to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<SPYMStream>(is);
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class SPYMFileSystem: public MFileSystem
{
public:
    SPYMFileSystem(): MFileSystem("spy") {};

    bool handles(std::string fileName) override {
        return byExtension(".spy", fileName);
    }

    MFile* getFile(std::string path) override {
        return new SPYMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_SPY */
