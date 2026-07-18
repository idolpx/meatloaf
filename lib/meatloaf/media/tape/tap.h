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
// Decoding is done by the vendored TAPClean engine (see
// components/tapclean): the whole image is scanned once at open and every
// program is extracted with its loader identified (~90 formats: Kernal,
// Turbotape 250/64-fast, Cyberload, Visiload, US Gold, Novaload, Freeload,
// Pavloda, Ocean, ...). Each program is decoded into a PRG (2-byte load
// address + data).
//
// Tapes behave like a real datasette: a directory request returns ONE
// entry - the next program found from the current tape position - and
// leaves it ready to load (LOAD"*",8 or console cat/hex "*" then serve
// that program - the tape state is shared across stream instances). The
// next directory request returns the next entry; at the end of the tape
// a "no more entries" line is shown and the tape rewinds for the
// following request. Only payloads are listed: the Kernal-loaded boot
// stub of a turbo-loader tape is skipped (its name goes to the payload).
// The image is buffered in PSRAM only during the open() scan; afterwards
// just the decoded programs stay resident.
//
// If a companion ".idx" file exists (same base name), it is treated like a
// normal directory listing for random access instead: each line is
// "<offset>[:<length>] <name>" (decimal, 0x hex or octal numbers; length
// is the file size in bytes and is optional; comments start with #, ; or
// '). Loading an entry decodes the program at that tape offset. The drive
// command "T-I" scans the tape and generates the .idx; "T-C <ms|MMM:SS>"
// sets the tape counter (read position) by time.
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

// Tape state shared by every TAPMStream on the same image: the decoder
// (one whole-tape scan) and the datasette position. File opens create
// fresh TAPMStream instances (MFile::getSourceStream -> getDecodedStream)
// while directory listings use the ImageBroker instance - sharing this
// state is what makes LOAD"*",8 / console "cat *" serve the program at
// the current tape position, and prevents a full re-scan on every open.
struct TapeState {
    TapeDecoder decoder;
    bool decoder_tried = false;
    bool decoder_ok = false;

    uint32_t tape_pos = 0;      // where the next program scan resumes
    bool tape_ended = false;

    TapeEntry current;          // last program found (ready to load)
    bool have_current = false;

    // Registry of live states keyed by container URL
    static std::shared_ptr<TapeState> obtain(const std::string &url);
};

class TAPMStream : public MMediaStream {

protected:
    // Shared per-image state; the references below alias into it so the
    // rest of the class (and TAPMFile) reads like plain members
    std::shared_ptr<TapeState> state;
    TapeDecoder &decoder;
    bool &decoder_tried;
    bool &decoder_ok;
    uint32_t &tape_pos;         // where the next program scan resumes
    bool &tape_ended;

public:
    TapeEntry &current;         // last program found (ready to load)
    bool &have_current;

    TAPMStream(std::shared_ptr<MStream> is) : MMediaStream(is),
        state(TapeState::obtain(is != nullptr ? is->url : "")),
        decoder(state->decoder),
        decoder_tried(state->decoder_tried),
        decoder_ok(state->decoder_ok),
        tape_pos(state->tape_pos),
        tape_ended(state->tape_ended),
        current(state->current),
        have_current(state->have_current)
    {
        // The decoder opens lazily on first use
    };

    struct IdxEntry {
        uint32_t offset;
        uint32_t size;      // file length in bytes (0 = not in the .idx)
        std::string name;
    };

    // Provide the contents of the companion .idx file (empty = none)
    void loadIndex(const std::string &idx_text);
    bool hasIndex() { return has_idx; }

    // Name used for entries with no name (the media file's name)
    void setDefaultName(std::string name);

    // --- Sequential tape access (no .idx) ---
    // Advance to the next program on the tape; fills 'current' and leaves
    // it ready to load. Returns false at the end of the tape (tapeEnded()).
    bool nextTapeEntry();
    void resetTape();
    bool tapeEnded() { return tape_ended; }

    // --- .idx directory (random access) ---
    uint16_t idxCount() { return idx_entries.size(); }
    bool idxEntry(uint16_t index, std::string &name, uint32_t &size);

    // Set the tape read position by counter time: milliseconds or "MMM:SS"
    bool setCounter(std::string spec);
    bool setCounterMs(uint32_t ms);

    // Display name for the current entry (media name when unnamed)
    std::string entryDisplayName(const TapeEntry &e);

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

    bool ensureDecoder();
    void serveCurrent();            // expose 'current' as the loaded file

    bool has_idx = false;
    std::vector<IdxEntry> idx_entries;

    std::string default_name = "tape file";

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

    // Scan the whole tape and write a companion .idx file:
    // "<offset>:<length> <name>" per program (length in bytes is the new
    // optional field; readers still accept plain "<offset> <name>" lines)
    bool buildIndex();

    bool isDir = true;
    bool dirIsOpen = false;

protected:
    // Reads the companion .idx file next to the image ("" if none)
    std::string readIdxSibling();
    std::string idxSiblingPath();

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
