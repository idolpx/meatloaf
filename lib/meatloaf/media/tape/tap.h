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
// Decoding is done by the vendored wav2prg engine (see wav2prg/): the tape
// is scanned for programs, the loader each program uses is detected via the
// wav2prg observer chain (Kernal, Turbo Tape 64, Freeload, Novaload,
// Pavloda, Ocean, and every other wav-prg plugin), and each program is
// decoded into a PRG (2-byte load address + data).
//
// If a companion ".idx" file exists (same base name), it is used for the
// directory listing instead of analyzing the whole tape: each line is
// "<offset> <name>" (decimal, 0x hex or octal offsets; comments start with
// #, ; or '). Loading an entry then only analyzes the tape from that offset.
//
// https://en.wikipedia.org/wiki/Commodore_Datasette
// https://vice-emu.sourceforge.io/vice_17.html#SEC330
// https://ist.uwaterloo.ca/~schepers/formats/TAP.TXT
// https://sourceforge.net/p/tapclean/gitcode/ci/master/tree/
// https://wav-prg.sourceforge.io/tape.html
// https://www.luigidifraia.com/technical-info/
// https://github.com/binaryfields/zinc64/blob/master/doc/Analyzing%20C64%20tape%20loaders.txt
// https://web.archive.org/web/20170117094643/http://tapes.c64.no/
// https://web.archive.org/web/20191021114418/http://www.subchristsoftware.com:80/finaltap.htm
//

// .DMP - DC2N format tape image
//
// "DC2N-TAP-RAW" signature; 16-bit LE samples at the counter rate stored
// in the header (usually 2 MHz), 0xFFFF = counter overflow. Decoding and
// loader detection are shared with .TAP (see tape_decoder.h).
//
// https://www.luigidifraia.com/technical-info/

// .HTAP - High resolution tape image format (Manosoft)
//
// "-HIRES" signature at offset 0x06; halfwave oriented: pulses are signed
// 16-bit LE values (bit 15 = polarity, bits 0-14 = duration in 0.5 us
// ticks, max 10 ms), pauses are 0x0000 0x0000 followed by a 32-bit
// duration in us. Decoding and loader detection are shared with .TAP
// (see tape_decoder.h).
//
// https://www.manosoft.it/?page_id=4678
// https://drive.google.com/file/d/11IK1m5-k5Jk9-iR9TpWUmxMAX9MC-s9D/view?usp=drive_link
//   (HTAP File Format Specifications V0 sub 2.0, Manosoft Group)
//

#ifndef MEATLOAF_MEDIA_TAP
#define MEATLOAF_MEDIA_TAP

#include "meatloaf.h"
#include "meat_media.h"

#include "tape_decoder.h"


/********************************************************
 * Streams
 ********************************************************/

class TAPMStream : public MMediaStream {

public:
    TAPMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // Analysis is lazy: listing via a .idx needs no decode at all
    };

    ~TAPMStream()
    {
        freeImage();
    }

    struct IdxEntry {
        uint32_t offset;
        std::string name;
    };

    // Provide the contents of the companion .idx file (empty = none)
    void loadIndex(const std::string &idx_text);

    // Directory entries: from .idx when present, else from full analysis
    uint16_t entryCount();
    bool getEntry(uint16_t index, std::string &name, std::string &loader,
                  uint32_t &size, bool &checksum_ok);

    bool seekPath(std::string path) override;

    // Tape counter: current read position as time from the start of the
    // tape (like a datasette counter). Interpolated within the file being
    // read; durationMs() is the length of the whole tape.
    uint32_t counterMs();
    uint32_t durationMs();
    std::string counterString();   // "MMM:SS/MMM:SS"

    std::unordered_map<std::string, std::string> info() override;

    std::string media_label = "c64 tape";

protected:
    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };

    bool loadImage();       // buffer the raw image in RAM
    void freeImage();
    bool ensureAnalyzed();  // full-tape analysis (no .idx)
    bool decodeAt(uint32_t offset, TapeEntry &out); // single program at offset

    uint8_t *image_data = nullptr;
    uint32_t image_len = 0;

    bool analyzed = false;
    bool analysis_ok = false;
    std::vector<TapeEntry> entries;

    bool has_idx = false;
    std::vector<IdxEntry> idx_entries;

    // Currently selected file (after seekPath)
    std::vector<uint8_t> current_prg;
    uint32_t cur_start_ms = 0;     // tape time of the selected file's block
    uint32_t cur_end_ms = 0;
    uint32_t total_ms = 0;         // whole tape duration (0 = not computed)
    bool times_computed = false;

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
        isPETSCII = true;
        media_image = name;
    };

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override;

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDirectory() override;
    bool exists() override;

    bool isDir = true;
    bool dirIsOpen = false;

protected:
    // Reads the companion .idx file next to the image ("" if none)
    std::string readIdxSibling();

    uint16_t entry_index = 0;
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
            ".dmp",
            ".htap"
        },
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new TAPMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_TAP */
