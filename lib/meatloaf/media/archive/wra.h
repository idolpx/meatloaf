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

// .WRA, .WR3 - Wraptor and Wraptor 3
//
// https://ist.uwaterloo.ca/~schepers/formats/WRA-WR3.TXT
// https://cbmfiles.com/geos/geos-2.php
//
// ONE class handles both extensions. The .wra/.wr3 difference is entirely in
// how the two Wraptor versions reconstruct GEOS files - a bug-fix lineage, not
// a container or compression change - and since nothing here reconstructs a
// GEOS file, there is no behaviour to switch on. Verified: the .wra sample in
// the corpus decodes with the same decoder as the .wr3 ones.
//
// Layout is a bare concatenation. Each file is preceded by the four-byte
// signature FF 42 4C FF ("BL", the author's initials), then a NUL-terminated
// name, then a type byte, then LZSS-compressed data running to two bytes
// before the next signature - or two bytes before EOF for the last entry.
// Those two bytes are a 16-bit CRC whose algorithm the format documentation
// does not give, so nothing here verifies it.
//
// There is no directory, no stored size and no offset to the next entry, so
// the entry list is built by scanning for the signature.
//
// Read-only, and effectively PSRAM-only: decoding one entry needs a 32 KB
// LZSS window plus the compressed span plus the whole decompressed output at
// once (~100 KB for the largest entry in the corpus).


#ifndef MEATLOAF_MEDIA_WRA
#define MEATLOAF_MEDIA_WRA

#include "meatloaf.h"
#include "meat_media.h"

#include <vector>


/********************************************************
 * Streams
 ********************************************************/

class WRAMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    WRAMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        block_size = 254;
    };

    // Turns a stored name into the UTF-8 one Meatloaf uses internally.
    // Wraptor gives no flag saying which encoding it wrote, so this has to
    // decide - see wra.cpp for how, and why it cannot be decided per archive.
    // Public because it is a pure function of its argument and both the
    // listing and the lookup have to agree with it exactly.
    static std::string decodeName(const std::string &raw);

protected:
    struct Entry {
        std::string filename;       // as stored; see wra.cpp on its encoding
        uint8_t  file_type = 0;     // 1 SEQ, 2 PRG, 3 USR, 4 GEOS
        uint32_t sig_offset = 0;    // container offset of this entry's signature
        uint32_t data_offset = 0;   // first compressed byte
        uint32_t data_end = 0;      // exclusive; the two CRC bytes start here
        uint16_t crc = 0;           // stored, never checked - algorithm unknown
    };

    std::vector<Entry> entries;
    bool header_parsed = false;
    bool header_ok = false;

    // Scans the container for the entry signature and validates each hit. The
    // scan is buffered - reading the container a byte at a time would be
    // thousands of range requests over a network.
    bool loadEntries();

    // Decompresses one whole entry into `data`. There is no length anywhere in
    // the format, so an entry cannot be read incrementally without carrying the
    // LZSS state across readFile() calls for no gain.
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
    int32_t cached_entry = -1;

private:
    friend class WRAMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class WRAMFile: public MFile {
public:

    WRAMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
        isCBM = true;
        media_archive = name;
    };

    ~WRAMFile() {
        // don't close the stream here! It will be used by shared ptr to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<WRAMStream>(is);
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class WRAMFileSystem: public MFileSystem
{
public:
    WRAMFileSystem(): MFileSystem("wra") {};

    bool handles(std::string fileName) override {
        return byExtension( {
            ".wra",
            ".wr3",
        }, fileName);
    }

    MFile* getFile(std::string path) override {
        return new WRAMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_WRA */
