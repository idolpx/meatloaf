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

//   Future Enhancements
//   For full TAP support, the analyzeTapeData() method should be enhanced to:
//   1. Decode pulses to bits: Use timing thresholds to determine 0/1 bits
//   2. Find sync bytes: Look for standard C64 tape sync pattern (0x89...)
//   3. Parse tape headers: Extract filename, type, load address
//   4. Extract file data: Follow data blocks with checksums
//   5. Support turbo loaders: Handle non-standard pulse timings
//   6. Multiple files: TAP can contain multiple programs

// TODO: Add full emulation of tape counter (Absolute and relative) that allows to load tapes asking to reset the counter and the to rewind to 0. Positioning is very reliable and rewind to zero can be either manual (REW) or with an instant-execution functionality.

#ifndef MEATLOAF_MEDIA_TAP
#define MEATLOAF_MEDIA_TAP

#include "meatloaf.h"
#include "meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class TAPMStream : public MStream {

public:
    TAPMStream(std::shared_ptr<MStream> containerStream) : MStream(containerStream->url)
    {
        this->containerStream = containerStream;

        // Read TAP header
        if (!readHeader())
        {
            Debug_printv("Failed to read TAP header");
            return;
        }

        // Analyze tape data to find files
        analyzeTapeData();
    };

    // TAP file header (20 bytes)
    struct TAPHeader {
        char signature[12];     // "C64-TAPE-RAW"
        uint8_t version;        // $00 or $01
        uint8_t reserved[3];    // Reserved for future use
        uint32_t data_size;     // Size of pulse data (little-endian)
    } __attribute__((packed));

    // Detected tape file entry
    struct TapeFile {
        std::string filename;
        uint8_t file_type;
        uint32_t data_offset;       // Offset in cached data
        uint32_t data_length;       // Length of file data
        uint16_t start_address;     // Load address
        uint16_t end_address;       // End address
        std::vector<uint8_t> cached_data;  // Decoded file data (cached during analysis)
    };

protected:
    struct Header {
        std::string signature;
        uint8_t version;
        uint32_t data_size;
    };

    struct Entry {
        std::string filename;
        uint8_t file_type;
        uint32_t data_offset;
        uint32_t data_length;
        uint16_t start_address;
        uint16_t end_address;
    };

    std::shared_ptr<MStream> containerStream;
    TAPHeader tap_header;
    std::vector<TapeFile> tape_files;
    uint32_t pulse_data_start;      // Offset where pulse data begins
    uint32_t current_file_index = 0;

    // TAP decoding state
    uint32_t tap_position;          // Current position in TAP data
    static const uint16_t kernal_thresholds[2];  // Pulse duration thresholds: 426, 616
    static const uint8_t kernal_pilot_sequence[9];  // 137,136,135,134,133,132,131,130,129

    Header header;
    Entry entry;

    bool readHeader();
    void analyzeTapeData();

    // TAP decoding functions (adapted from wav-prg)
    bool readTAPPulse(uint32_t& pulse);
    bool pulseToBit(uint8_t pulse1, uint8_t pulse2, uint8_t& bit);
    bool getPulseBit(uint8_t& bit);
    bool getByte(uint8_t& byte);
    bool getByteWithSync(uint8_t& byte, bool allow_short_first);
    bool findSync();
    bool readTapeHeader(uint8_t& file_type, std::string& filename, uint16_t& start_addr, uint16_t& end_addr);
    bool readDataBlock(uint8_t* buffer, uint16_t max_size, uint16_t& bytes_read);

    // TAP is browseable (can list files) but not random access (sequential tape data)
    bool isBrowsable() override { return true; };
    bool isRandomAccess() override {
        // If IDX file present, could be random access
        // TODO: Check for .idx companion file logic here (not implemented)
        return false;
    };

    // MStream required methods
    bool isOpen() override { return containerStream != nullptr && containerStream->isOpen(); };
    bool open(std::ios_base::openmode mode) override { return containerStream->open(mode); };
    void close() override { if (containerStream) containerStream->close(); };
    bool seek(uint32_t pos) override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t* buf, uint32_t size) override { return 0; };

    // Sequential access for TAP files
    std::string seekNextEntry() override;

    // Random access if IDX file present
    bool seekPath(std::string path) override;

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
        isPETSCII = true;
    };

    ~TAPMFile() {
        // Close the cached stream
        if (cached_stream)
            cached_stream->close();
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override;

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;

private:
    std::shared_ptr<TAPMStream> cached_stream;  // Cached decoded stream
};



/********************************************************
 * FS
 ********************************************************/

class TAPMFileSystem: public MFileSystem
{
public:
    TAPMFileSystem(): MFileSystem("tap") {};

    bool handles(std::string fileName) override {
        return byExtension({
            ".tap",
            ".idx",  // https://www.luigidifraia.com/technical-info/
            ".wav",  // Some TAPs are stored in WAV containers
            ".tzx"   // https://worldofspectrum.org/faq/reference/formats.htm
        },
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new TAPMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_TAP */
