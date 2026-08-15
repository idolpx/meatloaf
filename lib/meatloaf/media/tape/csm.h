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

// .CSM - Commodore cassette image holding DECODED tape blocks
//
// There is no magic number, no version field, no directory. A CSM file is a
// flat run of the blocks a datasette loader would have produced:
//
//   [192-byte header block][data block][192-byte header block][data block]...
//   ...terminated by a type-$05 (end of tape) header block, which has no data.
//
// Header block: 0 = file type, 1-2 = start address LE, 3-4 = end address LE,
// 5-20 = 16-byte PETSCII name, 21-191 = padding. The data block that follows
// is (end - start) RAW program bytes - the two-byte load address is NOT
// stored, it lives in the header and is synthesized on read.
//
// Because an entry's offset depends on every preceding entry's size, entries
// are found by WALKING the file, not by indexing it. Unlike TAP there are no
// pulses here and nothing to decode, so this is modeled on T64.
//


#ifndef MEATLOAF_MEDIA_CSM
#define MEATLOAF_MEDIA_CSM

#include "meatloaf.h"
#include "meat_media.h"

#include <vector>


/********************************************************
 * Streams
 ********************************************************/

class CSMMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    CSMMStream(std::shared_ptr<MStream> is) : MMediaStream(is) {};

protected:
    // Size of an on-tape header block, and of the part of it that carries
    // fields. The remaining 171 bytes are padding.
    static constexpr uint32_t header_block_size = 192;
    static constexpr uint32_t header_field_size = 21;

    // CBM tape header block types.
    static constexpr uint8_t type_basic     = 1;    // relocatable program
    static constexpr uint8_t type_seq_data  = 2;
    static constexpr uint8_t type_program   = 3;    // non-relocatable program
    static constexpr uint8_t type_seq_header= 4;
    static constexpr uint8_t type_end_tape  = 5;    // no data block follows

    // A tape with more entries than this is corrupt, not ambitious - the
    // walk is bounded so a garbage image cannot spin.
    static constexpr size_t max_entries = 256;

    struct Entry {
        uint8_t     file_type;
        uint16_t    start_address;
        uint16_t    end_address;
        char        filename[17];   // 16 stored bytes + terminator
        uint32_t    data_offset;    // absolute offset of the data block
        uint32_t    data_length;    // clamped to what the container holds
    };

    // Walks the container building `entries`. Idempotent - `entry_count` is
    // the marker for "already walked", since it is assigned unconditionally
    // below. rewindDirectory() calls this on every listing, and each step of
    // the walk is a seek plus a read: one HTTP range request per entry over
    // the network.
    bool readHeader() override;

    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

    std::vector<Entry> entries;
    Entry entry = { 0, 0, 0, "", 0, 0 };

private:
    friend class CSMMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class CSMMFile: public MFile {
public:

    CSMMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
        isPETSCII = true;
        media_image = name;
    };

    ~CSMMFile() {
        // don't close the stream here! It will be used by shared ptr to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<CSMMStream>(is);
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class CSMMFileSystem: public MFileSystem
{
public:
    CSMMFileSystem(): MFileSystem("csm") {};

    bool handles(std::string fileName) override {
        return byExtension(".csm", fileName);
    }

    MFile* getFile(std::string path) override {
        return new CSMMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_CSM */
