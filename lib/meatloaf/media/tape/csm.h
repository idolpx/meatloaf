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
// Unlike TAP there are no pulses here and nothing to decode: the blocks are
// already the program bytes, so an entry is found by WALKING the file (each
// entry's offset is the sum of every preceding block) rather than by scanning
// for loaders. The BEHAVIOUR, however, is a datasette exactly as .TAP is: a
// directory request returns ONE entry - the next program on the tape - and
// leaves it ready to load (LOAD"*",8, or console cat/hex "*"; the tape state
// is shared across stream instances). The next request returns the next entry;
// at the end of the tape a "no more entries" line is shown and the tape
// rewinds for the following request. Loads search forward from the current
// position and wrap once, so a tape carrying the same name twice - a BASIC
// loader and its payload, which is the norm - resolves positionally rather
// than ambiguously.
//
// Read-only.
//
// https://en.wikipedia.org/wiki/Commodore_Datasette
//


#ifndef MEATLOAF_MEDIA_CSM
#define MEATLOAF_MEDIA_CSM

#include "meatloaf.h"
#include "meat_media.h"

#include <memory>
#include <string>
#include <vector>


/********************************************************
 * Streams
 ********************************************************/

// One entry as the tape carries it: the header block's fields plus where its
// data block sits in the container.
struct CSMEntry {
    uint8_t     file_type = 0;
    uint16_t    start_address = 0;
    uint16_t    end_address = 0;
    std::string name;               // padding-trimmed; may legitimately be empty
    uint32_t    data_offset = 0;
    uint32_t    data_length = 0;    // clamped to what the container holds
};

// Tape state shared by every CSMMStream on the same image: the walked entry
// list and the datasette position. File opens create fresh CSMMStream
// instances (MFile::getSourceStream -> getDecodedStream) while directory
// listings use the ImageBroker instance - sharing this state is what makes
// LOAD"*",8 / console "cat *" serve the program at the current tape position,
// and stops the container being re-walked on every open (each step of the walk
// is a seek plus a read: one HTTP range request per entry over the network).
struct CSMState {
    std::vector<CSMEntry> entries;
    bool walked = false;

    size_t tape_index = 0;      // index of the next entry to serve
    bool tape_ended = false;

    CSMEntry current;           // last entry found (ready to load)
    bool have_current = false;

    // Registry of live states keyed by container URL
    static std::shared_ptr<CSMState> obtain(const std::string &url);
};


class CSMMStream : public MMediaStream {

protected:
    // Shared per-image state; the references below alias into it so the rest
    // of the class (and CSMMFile) reads like plain members. Declaration order
    // matters - they are bound in the constructor's init list, after `state`.
    std::shared_ptr<CSMState> state;
    std::vector<CSMEntry> &entries;
    bool &walked;
    size_t &tape_index;
    bool &tape_ended;

public:
    CSMEntry &current;          // last entry found (ready to load)
    bool &have_current;

    CSMMStream(std::shared_ptr<MStream> is) : MMediaStream(is),
        state(CSMState::obtain(is != nullptr ? is->url : "")),
        entries(state->entries),
        walked(state->walked),
        tape_index(state->tape_index),
        tape_ended(state->tape_ended),
        current(state->current),
        have_current(state->have_current)
    {
        // The container is walked lazily on first use
    };

    // Name used for entries the tape leaves unnamed (the media file's name)
    void setDefaultName(std::string name) { default_name = name; };

    // --- Sequential tape access ---
    // Advance to the next entry on the tape; fills 'current' and leaves it
    // ready to load. Returns false at the end of the tape (tapeEnded()).
    bool nextTapeEntry();
    void resetTape();
    bool tapeEnded() { return tape_ended; };

    // Display name for an entry (media name when the tape leaves it unnamed)
    std::string entryDisplayName(const CSMEntry &e);

    // Sequential media resolves a name by SCANNING, not by seeking: this is
    // the primitive MFile::getSourceStream() drives for a browsable stream.
    // Advances the head by one, serves that entry so the stream is ready to
    // read the moment the caller's name matches, and returns its display name.
    // At the end of the tape it WRAPS; it returns "" only once it has been all
    // the way round, so a name behind the head is still reachable and a miss
    // still terminates.
    std::string seekNextEntry() override;

    // A tape, not a directory - so getSourceStream() takes the browsable
    // branch and calls seekNextEntry() rather than seekPath().
    bool isBrowsable() override { return true; };
    bool isRandomAccess() override { return false; };

protected:
    // Size of an on-tape header block, and of the part of it that carries
    // fields. The remaining 171 bytes are padding.
    static constexpr uint32_t header_block_size = 192;
    static constexpr uint32_t header_field_size = 21;

    // CBM tape header block types.
    static constexpr uint8_t type_basic      = 1;   // relocatable program
    static constexpr uint8_t type_seq_data   = 2;
    static constexpr uint8_t type_program    = 3;   // non-relocatable program
    static constexpr uint8_t type_seq_header = 4;
    static constexpr uint8_t type_end_tape   = 5;   // no data block follows

    // A tape with more entries than this is corrupt, not ambitious - the walk
    // is bounded so a garbage image cannot spin.
    static constexpr size_t max_entries = 256;

    // Walks the container building `entries`. Idempotent via the shared
    // `walked` flag, which is set even when the walk yields nothing so a
    // broken image is not re-walked on every listing.
    bool readHeader() override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };

    void serveCurrent();        // expose 'current' as the loaded file

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

    std::string default_name = "csm file";

    // Entries handed out by seekNextEntry() during the caller's current scan,
    // used to spot a full lap. Deliberately NOT in CSMState: this is search
    // bookkeeping rather than tape position, and a fresh stream is built for
    // every open (which is why the position has to be shared but this does
    // not), so it starts at 0 on each search with no reset needed.
    size_t scan_steps = 0;

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

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override;

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDirectory() override;
    bool exists() override;

    bool isDir = true;
    bool dirIsOpen = false;

protected:
    uint16_t entry_index = 0;
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
