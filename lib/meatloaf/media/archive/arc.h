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

// .ARC - Compressed ARChive)
// .SDA - Self-Dissolving Compressed Archive
//
// https://ist.uwaterloo.ca/~schepers/formats/ARC.TXT
// https://ist.uwaterloo.ca/~schepers/formats/SDA.TXT
// https://cbmfiles.com/genie/C64ArcadeListing.php
// http://fileformats.archiveteam.org/wiki/ARC_(Commodore)
// https://www.zimmers.net/anonftp/pub/cbm/crossplatform/converters/unix/cbmconvert-2.1.2.tar.gz
// https://cbmfiles.com/genie/C64ArcadeListing.php
//
// MSDOS ARC
// https://github.com/hyc/arc
//


#ifndef MEATLOAF_MEDIA_ARC
#define MEATLOAF_MEDIA_ARC

#include "meatloaf.h"
#include "meat_media.h"

#include <vector>


/********************************************************
 * Streams
 ********************************************************/

class ARCMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    ARCMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // ARC uses 254-byte blocks
        block_size = 254;
    };

protected:
    // One entry's header, as it sits in the container. The fixed part is
    // version..fnlen, then the name, then two more fields on version 2, then
    // per-mode extras that only the decompressor needs.
    struct Entry {
        std::string filename;
        uint8_t version = 0;    // 1 or 2
        uint8_t mode = 0;       // 0 store, 1 pack, 2 squeeze, 3 crunch,
                                // 4 squeeze+pack, 5 crunch in one pass
        uint16_t check = 0;     // checksum the entry claims
        uint32_t size = 0;      // uncompressed size (three bytes stored)
        uint16_t blocks = 0;    // compressed size in 254-byte CBM blocks
        uint8_t type = 0;       // 'P', 'S', 'U' or 'R'
        uint8_t rel_record_size = 0;
        uint16_t date = 0;
        uint32_t offset = 0;    // container offset of this entry's HEADER
    };

    // Where the archive proper starts. Zero for a .arc; for a .sda it is past
    // the BASIC loader and machine code that make it self-dissolving.
    uint32_t archive_offset = 0;
    bool header_parsed = false;
    bool header_ok = false;

    std::vector<Entry> entries;

    // Walks every entry header once. Cheap: the next header sits exactly
    // blocks * 254 bytes past this one, so the walk never has to look at
    // compressed data or build a Huffman table.
    bool loadEntries();

    // Finds the first byte of the archive, skipping a .sda's BASIC loader.
    bool skipBasicLoader();

    // Decompresses one whole entry into `data`. Streaming it would mean
    // carrying Huffman and LZW state across readFile() calls for no gain: a
    // CBM file is small, and the drive reads it start to end anyway.
    bool extractEntry( const Entry &e );

    bool readHeader() override;
    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

    Entry entry;

    // The entry seekPath() last extracted, and which one it is.
    std::vector<uint8_t> data;
    int cached_entry = -1;
    bool checksum_ok = true;

private:
    friend class ARCMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class ARCMFile: public MFile {
public:

    ARCMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
        isPETSCII = true;
        media_archive = name;
    };

    ~ARCMFile() {
        // don't close the stream here! It will be used by shared ptr to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<ARCMStream>(is);
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class ARCMFileSystem: public MFileSystem
{
public:
    ARCMFileSystem(): MFileSystem("arc") {};

    bool handles(std::string fileName) override {
        return byExtension( {
            ".arc",
            ".sda",
        }, fileName);
    }

    MFile* getFile(std::string path) override {
        return new ARCMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_ARC */
