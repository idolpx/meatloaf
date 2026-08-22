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

#include "spy.h"

#include <cstring>

// Bytes of file data in one SPYne block. A SPYne is a CBM file laid out in
// disk blocks with their two link bytes stripped, so a block carries 254 -
// NOT the 256 of MStream::block_size, which is the sector size. Every one of
// the three places below that needs it uses this constant: the directory
// stride, the data start, and the per-entry stride. Using block_size for any
// of them puts every entry two bytes late per block, which is exactly the
// defect lnx.cpp shipped with.
static constexpr uint32_t SPY_BLOCK_SIZE = 254;

// The self-extracting code occupies the first 15 blocks, so the central
// directory begins at block 15 - offset 3810, counted from byte 0 of the
// container (its own two-byte load address is inside that area).
static constexpr uint32_t SPY_CODE_BLOCKS = 15;
static constexpr uint32_t SPY_DIR_START   = SPY_CODE_BLOCKS * SPY_BLOCK_SIZE;

// An entry is 30 bytes of fields followed by two filler bytes, and EIGHT
// entries fit in one 254-byte block: 7 * 32 + 30 = 254. The eighth entry's
// filler simply does not exist, because it would cross the block boundary -
// so the stride within a block is 32 and the stride between blocks is 254,
// and those are two different numbers. Indexing with a flat `i * 32` walks
// off by two bytes per block from the ninth entry onward.
static constexpr uint32_t SPY_ENTRY_FIELDS   = 30;
static constexpr uint32_t SPY_ENTRY_SIZE     = 32;
static constexpr uint32_t SPY_ENTRIES_PER_BLOCK = 8;

// The format caps a container at 144 files.
static constexpr uint32_t SPY_MAX_ENTRIES = 144;

// Load address of the extraction code. Not a signature - the format has none -
// but every real SPYne carries it, so a mismatch is worth a log line.
static constexpr uint16_t SPY_LOAD_ADDRESS = 0x02A7;

// Field offsets within an entry.
enum {
    SPY_E_TYPE     = 0x00,
    SPY_E_NAME     = 0x03,   // 16 bytes, $A0 padded
    SPY_E_CHECKSUM = 0x17,   // low/high
    SPY_E_LSU      = 0x19,
    SPY_E_LAST     = 0x1A,   // $FF more files follow, $00 this is the last
    SPY_E_BLOCKS   = 0x1C,   // low/high
};

static constexpr uint32_t SPY_NAME_LENGTH = 16;


// MStream::read() returns at most one block, so a caller wanting `len` bytes
// has to loop until it has them or the stream stops giving.
static bool spy_read_at(MStream *stream, uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (stream == nullptr || !stream->seek(offset))
        return false;

    uint32_t got = 0;
    while (got < len)
    {
        uint32_t n = stream->read(buf + got, len - got);
        if (n == 0)
            return false;
        got += n;
    }
    return true;
}


/********************************************************
 * Container
 ********************************************************/

bool SPYMStream::loadEntries()
{
    entries.clear();

    MStream *cs = containerStream.get();
    if (cs == nullptr)
        return false;

    const uint32_t container_size = cs->size();

    for (uint32_t i = 0; i < SPY_MAX_ENTRIES; i++)
    {
        const uint32_t offset = SPY_DIR_START
                              + (i / SPY_ENTRIES_PER_BLOCK) * SPY_BLOCK_SIZE
                              + (i % SPY_ENTRIES_PER_BLOCK) * SPY_ENTRY_SIZE;

        if (offset + SPY_ENTRY_FIELDS > container_size)
        {
            Debug_printv("directory runs past the end of the container at entry[%lu]",
                         (unsigned long)i);
            break;
        }

        uint8_t e[SPY_ENTRY_FIELDS];
        if (!spy_read_at(cs, offset, e, sizeof(e)))
        {
            Debug_printv("cannot read directory entry[%lu]", (unsigned long)i);
            break;
        }

        Entry n;
        n.file_type = e[SPY_E_TYPE];
        n.checksum  = e[SPY_E_CHECKSUM] | ((uint16_t)e[SPY_E_CHECKSUM + 1] << 8);
        n.lsu       = e[SPY_E_LSU];
        n.blocks    = e[SPY_E_BLOCKS] | ((uint16_t)e[SPY_E_BLOCKS + 1] << 8);

        // Only the $A0 padding comes off - a trailing SPACE is part of a CBM
        // name, and this format's names routinely carry them ("08.   1996").
        n.filename.assign((const char *)&e[SPY_E_NAME], SPY_NAME_LENGTH);
        mstr::rtrimA0(n.filename);

        entries.push_back(n);

        if (e[SPY_E_LAST] == 0x00)
            break;
    }

    if (entries.empty())
        return false;

    // The directory occupies a whole number of blocks, eight entries each, and
    // the file data starts in the block that follows it.
    const uint32_t dir_blocks = (uint32_t)((entries.size() + SPY_ENTRIES_PER_BLOCK - 1)
                                           / SPY_ENTRIES_PER_BLOCK);
    uint32_t offset = (SPY_CODE_BLOCKS + dir_blocks) * SPY_BLOCK_SIZE;

    for (auto &n : entries)
    {
        n.offset = offset;

        // LSU is the INDEX of the last used byte within the 256-byte sector,
        // where data begins at index 2, so the last block contributes lsu - 1
        // bytes. Same convention as LNX.
        n.size = (n.blocks && n.lsu) ? ((uint32_t)(n.blocks - 1) * SPY_BLOCK_SIZE + (n.lsu - 1))
                                     : 0;

        // The stride is the entry's DECLARED block count, not its byte size
        // rounded up: the two differ in the last block, so deriving the stride
        // from the size compounds any error in the size.
        offset += (uint32_t)n.blocks * SPY_BLOCK_SIZE;

        Debug_printv("name[%s] type[%02X] blocks[%u] lsu[%u] size[%lu] offset[%lu]",
                     n.filename.c_str(), n.file_type, n.blocks, n.lsu,
                     (unsigned long)n.size, (unsigned long)n.offset);
    }

    return true;
}

bool SPYMStream::readHeader()
{
    if (header_parsed)
        return header_ok;

    header_parsed = true;
    header_ok = false;

    MStream *cs = containerStream.get();
    if (cs == nullptr)
        return false;

    // There is no signature to check. What CAN be checked is that the first
    // directory entry is structurally a directory entry - a valid CBM file
    // type and a well-formed last-file marker - which is what the walk below
    // depends on and is enough to reject a file that merely has the extension.
    uint8_t probe[SPY_ENTRY_FIELDS];
    if (!spy_read_at(cs, SPY_DIR_START, probe, sizeof(probe)))
    {
        Debug_printv("too small to hold a SPYne directory");
        return false;
    }

    if (probe[SPY_E_TYPE] < 0x81 || probe[SPY_E_TYPE] > 0x83 ||
        (probe[SPY_E_LAST] != 0x00 && probe[SPY_E_LAST] != 0xFF))
    {
        Debug_printv("not a SPYne: first entry has type[%02X] marker[%02X]",
                     probe[SPY_E_TYPE], probe[SPY_E_LAST]);
        return false;
    }

    uint8_t load[2];
    if (spy_read_at(cs, 0, load, sizeof(load)))
    {
        uint16_t address = load[0] | ((uint16_t)load[1] << 8);
        if (address != SPY_LOAD_ADDRESS)
            Debug_printv("unusual load address[$%04X], expected [$%04X]",
                         address, SPY_LOAD_ADDRESS);
    }

    if (!loadEntries())
    {
        Debug_printv("no entries in the container");
        return false;
    }

    entry_count = entries.size();
    header_ok = true;
    return true;
}


/********************************************************
 * Entries
 ********************************************************/

bool SPYMStream::seekEntry( uint16_t index )
{
    if (!readHeader())
        return false;

    if (!index || index > entries.size())
        return false;

    entry = entries[index - 1];
    entry_index = index;
    return true;
}

bool SPYMStream::seekEntry( std::string filename )
{
    if (!filename.size())
        return false;

    if (!readHeader())
        return false;

    mstr::replaceAll(filename, "\\", "/");
    bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));

    for (uint16_t index = 1; index <= entries.size(); index++)
    {
        // PETSCII in the container, UTF-8 everywhere inside Meatloaf -
        // getNextFileInDir() converts the same way, so a listed name can be
        // typed back and will match here.
        std::string name = mstr::toUTF8(entries[index - 1].filename);

        if (mstr::compareFilename(name, filename, wildcard))
        {
            entry = entries[index - 1];
            entry_index = index;
            return true;
        }
    }

    entry.filename.clear();
    return false;
}

std::string SPYMStream::decodeType(uint8_t file_type, bool show_hidden)
{
    (void)show_hidden;

    switch (file_type)
    {
        case 0x81: return "seq";
        case 0x82: return "prg";
        case 0x83: return "usr";
        default:   return "prg";
    }
}


/********************************************************
 * Reading
 ********************************************************/

bool SPYMStream::seekPath(std::string path)
{
    seekCalled = true;
    entry_index = 0;

    if (!seekEntry(path))
    {
        Debug_printv("Not found! [%s]", path.c_str());
        return false;
    }

    _size = entry.size;
    _position = 0;
    containerStream->seek(entry.offset);

    Debug_printv("filename[%s] type[%s] offset[%lu] size[%lu]",
                 entry.filename.c_str(), decodeType(entry.file_type).c_str(),
                 (unsigned long)entry.offset, (unsigned long)_size);

    return true;
}

uint32_t SPYMStream::readFile(uint8_t *buf, uint32_t size)
{
    // _position is READ here but never written: MMediaStream::read() advances
    // it by whatever this returns.
    if (_position >= _size)
        return 0;

    uint32_t remaining = _size - _position;
    if (size > remaining)
        size = remaining;

    // A short return at the end is CORRECT, not a truncation: the container
    // stores no padding after the last file's real bytes, so its final block
    // ends at the byte the directory says and the file simply stops there.
    return readContainer(buf, size);
}


/********************************************************
 * File implementations
 ********************************************************/

bool SPYMFile::rewindDirectory()
{
    dirIsOpen = true;

    auto image = ImageBroker::obtain<SPYMStream>("spy", url);
    if (image == nullptr)
        return false;

    if (!image->readHeader())
    {
        dirIsOpen = false;
        return false;
    }

    image->resetEntryCounter();

    // The host filename, which is already UTF-8 - the IEC boundary converts.
    media_header = name;
    std::string ext = "." + extension;
    mstr::replaceAll(media_header, ext, "");

    media_id = "spy";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_archive = name;

    return true;
}

MFile *SPYMFile::getNextFileInDir()
{
    // A failed rewind has already reset the entry counter, so reading on would
    // serve entry 0 forever with dirIsOpen still false - an endless listing
    // rather than an empty one.
    if (!dirIsOpen && !rewindDirectory())
        return nullptr;

    auto image = ImageBroker::obtain<SPYMStream>("spy", url);
    if (image == nullptr)
    {
        dirIsOpen = false;
        return nullptr;
    }

    if (image->getNextImageEntry())
    {
        // PETSCII in the container, UTF-8 from here on - and converted BEFORE
        // the entry URL is built, so the name a listing shows is the name
        // seekEntry() matches.
        std::string filename = mstr::toUTF8(image->entry.filename);
        mstr::replaceAll(filename, "/", "\\");

        // fullUrl() rejoins any pathInStream: `url` alone is the CONTAINER's
        // path, so joining a child name onto it would name a file beside the
        // container rather than one inside it.
        auto file = MFSOwner::File(fullUrl() + "/" + filename);
        file->name = filename;
        file->extension = image->decodeType(image->entry.file_type);
        file->size = image->entry.size;
        file->is_dir = 0;

        return file;
    }

    dirIsOpen = false;
    return nullptr;
}
