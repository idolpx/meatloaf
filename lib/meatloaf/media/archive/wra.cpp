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

#include "wra.h"

#include <cstring>
#include <new>          // placement new for the heap-allocated WraDecoder

// The four bytes that precede every compressed file. "42 4C" is "BL", the
// author's initials.
static const uint8_t WRA_SIGNATURE[4] = { 0xFF, 0x42, 0x4C, 0xFF };

// The LZSS dictionary is an absolute index into a 32768-byte output window,
// not a back-reference distance, and the window wraps when it fills. Both
// facts come straight from the reference decoder in the format document.
static constexpr uint32_t WRA_WINDOW = 32768;

// The dictionary code starts at 8 bits and the stream grows it one bit at a
// time. Past 16 the offset can no longer address the window, so a stream still
// growing there is garbage rather than a bigger dictionary.
static constexpr uint8_t WRA_START_BITS = 8;
static constexpr uint8_t WRA_MAX_BITS   = 16;

// Nothing in the format states an entry's decompressed length, so a corrupt
// stream can emit forever. A CBM file - GEOS VLIR included - is orders below
// this; the largest in the sample corpus is 38491 bytes.
static constexpr uint32_t WRA_MAX_OUTPUT = 1024 * 1024;

// Longest name the scan will accept before deciding a signature hit was a
// false positive. Real names are CBM directory names, so 16 or fewer.
static constexpr uint32_t WRA_MAX_NAME = 32;

static constexpr uint32_t WRA_SCAN_CHUNK = 1024;


// MStream::read() returns at most one block, so a caller wanting `len` bytes
// has to loop until it has them or the stream stops giving.
static bool wra_read_at(MStream *stream, uint32_t offset, uint8_t *buf, uint32_t len)
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

// Wraptor stores the name exactly as it stood in the CBM directory, and gives
// no flag saying which encoding that is. A plain CBM file's name is PETSCII; a
// GEOS file's is the ASCII that GEOS itself uses, which is why part of the
// sample corpus reads as "ReadPaint" and "M.Randall" rather than as CBM
// uppercase. So the decision is per NAME - it cannot even be made per archive,
// since one archive can hold both.
//
// The two are separable: PETSCII has no ASCII lowercase range - $61-$7A are
// graphics characters there - so a name carrying any of those bytes is a GEOS
// ASCII name and has to be passed through untouched. Running toUTF8() over one
// of those would map its lowercase bytes to graphics characters.
//
// Anything else is treated as PETSCII, which for an all-uppercase name is the
// safe choice either way: ASCII and unshifted PETSCII have the same byte
// values there, so both readings draw the same glyphs, and toUTF8() is what
// makes the name round-trip back through toPETSCII2() at the IEC boundary. It
// lowercases - "GEOS" is held as "geos" - which is the repository-wide
// convention, not a quirk of this format.
std::string WRAMStream::decodeName(const std::string &raw)
{
    for (unsigned char c : raw)
    {
        if (c >= 0x61 && c <= 0x7A)
            return raw;
    }
    return mstr::toUTF8(raw);
}


/********************************************************
 * Decoder
 ********************************************************/

// HEAP, never the stack: the window alone is 32 KB, against a 16 KB
// console_exec and a 20 KB bus_iec. A stack frame is reserved on function
// ENTRY, so a local would fault before a line of the decoder ran - the defect
// ArcDecoder shipped with. ESP-IDF compiles -fno-exceptions, so a throwing
// `new` is an abort(); malloc plus a NULL check and placement new instead.
struct WraDecoder
{
    const uint8_t *input = nullptr;
    uint32_t input_size = 0;
    uint32_t pos = 0;           // byte cursor into `input`
    uint8_t  bit = 0;           // bit cursor within input[pos], MSB first
    bool     ran_out = false;

    uint32_t wpos = 0;
    uint8_t  window[WRA_WINDOW] = {};

    // Bits are consumed most-significant first. Established from the worked
    // example in the format document: reading its first two codes LSB-first
    // yields nothing, MSB-first yields $01 $08 - the load address of the PRG
    // it contains, whose BASIC link pointer then also decodes correctly.
    uint32_t getBits(uint8_t n)
    {
        uint32_t v = 0;
        while (n--)
        {
            if (pos >= input_size)
            {
                ran_out = true;
                return v;
            }
            v = (v << 1) | ((input[pos] >> (7 - bit)) & 1);
            if (++bit == 8)
            {
                bit = 0;
                pos++;
            }
        }
        return v;
    }
};


/********************************************************
 * Container
 ********************************************************/

bool WRAMStream::loadEntries()
{
    entries.clear();

    MStream *cs = containerStream.get();
    if (cs == nullptr)
        return false;

    const uint32_t container_size = cs->size();
    if (container_size < sizeof(WRA_SIGNATURE) + 4)
        return false;

    // Scan for signatures in chunks, carrying the last three bytes across each
    // boundary so a signature straddling one is still found. Reading the
    // container a byte at a time would be thousands of range requests over a
    // network - the defect nib.cpp's findSync() shipped with.
    std::vector<uint32_t> hits;
    {
        uint8_t buf[WRA_SCAN_CHUNK + 3];
        uint32_t carry = 0;     // bytes retained from the previous chunk
        uint32_t base = 0;      // container offset of buf[0]
        uint32_t pos = 0;       // next container offset to read

        while (pos < container_size)
        {
            uint32_t want = container_size - pos;
            if (want > WRA_SCAN_CHUNK)
                want = WRA_SCAN_CHUNK;

            if (!wra_read_at(cs, pos, buf + carry, want))
                break;

            const uint32_t have = carry + want;
            for (uint32_t i = 0; i + sizeof(WRA_SIGNATURE) <= have; i++)
            {
                if (std::memcmp(buf + i, WRA_SIGNATURE, sizeof(WRA_SIGNATURE)) == 0)
                    hits.push_back(base + i);
            }

            pos += want;
            carry = (have >= 3) ? 3 : have;
            std::memmove(buf, buf + have - carry, carry);
            base = pos - carry;
        }
    }

    // Validate each hit. The format document warns that the signature can turn
    // up inside compressed data; a hit that is not followed by a plausible name
    // and a valid type byte is one of those and is dropped, so the entry before
    // it keeps running to the next REAL signature.
    for (uint32_t hit : hits)
    {
        const uint32_t p = hit + sizeof(WRA_SIGNATURE);
        if (p >= container_size)
            continue;

        // Room for the longest name accepted, its terminator, AND the type
        // byte that follows it - the type byte is read out of this buffer, so
        // a window sized only to the name over-reads it by one.
        uint32_t want = container_size - p;
        if (want > WRA_MAX_NAME + 2)
            want = WRA_MAX_NAME + 2;

        uint8_t head[WRA_MAX_NAME + 2];
        if (!wra_read_at(cs, p, head, want))
            continue;

        uint32_t len = 0;
        while (len < want && head[len] != 0x00)
            len++;

        // Empty, no terminator in range, or a terminator with no type byte
        // behind it.
        if (len == 0 || len + 1 >= want)
            continue;

        const uint32_t type_offset = p + len + 1;
        if (type_offset >= container_size)
            continue;

        const uint8_t file_type = head[len + 1];
        if (file_type < 1 || file_type > 4)
            continue;

        Entry n;
        n.filename.assign((const char *)head, len);
        n.file_type = file_type;
        n.sig_offset = hit;
        n.data_offset = type_offset + 1;
        entries.push_back(n);
    }

    if (entries.empty())
        return false;

    // Compressed data runs to two bytes before the next entry's signature, or
    // two bytes before the end of the container for the last one. Those two
    // bytes are the CRC.
    for (size_t i = 0; i < entries.size(); i++)
    {
        const uint32_t next = (i + 1 < entries.size()) ? entries[i + 1].sig_offset
                                                       : container_size;
        if (next < entries[i].data_offset + 2)
        {
            entries[i].data_end = entries[i].data_offset;
            continue;
        }

        entries[i].data_end = next - 2;

        uint8_t crc[2];
        if (wra_read_at(cs, entries[i].data_end, crc, sizeof(crc)))
            entries[i].crc = crc[0] | ((uint16_t)crc[1] << 8);

        Debug_printv("name[%s] type[%u] data[%lu..%lu] crc[%04X]",
                     entries[i].filename.c_str(), entries[i].file_type,
                     (unsigned long)entries[i].data_offset,
                     (unsigned long)entries[i].data_end, entries[i].crc);
    }

    return true;
}

bool WRAMStream::readHeader()
{
    if (header_parsed)
        return header_ok;

    header_parsed = true;
    header_ok = false;

    if (!loadEntries())
    {
        Debug_printv("no Wraptor entries found");
        return false;
    }

    entry_count = entries.size();
    header_ok = true;
    return true;
}


/********************************************************
 * Entries
 ********************************************************/

bool WRAMStream::seekEntry( uint16_t index )
{
    if (!readHeader())
        return false;

    if (!index || index > entries.size())
        return false;

    entry = entries[index - 1];
    entry_index = index;
    return true;
}

bool WRAMStream::seekEntry( std::string filename )
{
    if (!filename.size())
        return false;

    if (!readHeader())
        return false;

    mstr::replaceAll(filename, "\\", "/");
    bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));

    for (uint16_t index = 1; index <= entries.size(); index++)
    {
        // Decoded the same way getNextFileInDir() decodes it, so a listed name
        // can be typed back and will match here.
        std::string name = decodeName(entries[index - 1].filename);

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

std::string WRAMStream::decodeType(uint8_t file_type, bool show_hidden)
{
    (void)show_hidden;

    switch (file_type)
    {
        case 1: return "seq";
        case 2: return "prg";
        // Type 4 is the format document's "GEOS". A GEOS file still has a C64
        // type on disk, and in the sample corpus every type-4 entry's info
        // sector reports PRG while every type-3 one reports USR - so PRG is
        // what a listing should show for it.
        case 3: return "usr";
        case 4: return "prg";
        default: return "prg";
    }
}


/********************************************************
 * Extraction
 ********************************************************/

bool WRAMStream::extractEntry( const Entry &e )
{
    if (cached_entry == (int32_t)e.data_offset)
        return true;

    data.clear();
    cached_entry = -1;

    if (e.data_end <= e.data_offset)
    {
        Debug_printv("[%s] has no data", e.filename.c_str());
        return false;
    }

    const uint32_t compressed_size = e.data_end - e.data_offset;
    uint8_t *compressed = (uint8_t *)malloc(compressed_size);
    if (compressed == nullptr)
    {
        Debug_printv("no memory for a %lu byte entry", (unsigned long)compressed_size);
        return false;
    }

    if (!wra_read_at(containerStream.get(), e.data_offset, compressed, compressed_size))
    {
        Debug_printv("cannot read the data of [%s]", e.filename.c_str());
        free(compressed);
        return false;
    }

    WraDecoder *decoder = (WraDecoder *)malloc(sizeof(WraDecoder));
    if (decoder == nullptr)
    {
        Debug_printv("no memory for the decoder (%u bytes)", (unsigned)sizeof(WraDecoder));
        free(compressed);
        return false;
    }
    // Placement-new so the member initialisers run - the window has to start
    // zeroed for a decode to be reproducible. WraDecoder is trivially
    // destructible, so free() alone is the correct teardown.
    WraDecoder &d = *new (decoder) WraDecoder();

    d.input = compressed;
    d.input_size = compressed_size;

    auto emit = [&](uint8_t v) {
        d.window[d.wpos] = v;
        data.push_back(v);
        if (++d.wpos >= WRA_WINDOW)
            d.wpos = 0;
    };

    uint8_t bits = WRA_START_BITS;
    bool ok = false;

    while (!d.ran_out)
    {
        if (d.getBits(1) == 0)
        {
            // A zero flag bit introduces one literal byte.
            uint8_t v = (uint8_t)d.getBits(8);
            if (d.ran_out)
                break;
            emit(v);
        }
        else
        {
            const uint32_t offset = d.getBits(bits);
            if (d.ran_out)
                break;

            if (offset == 0)
            {
                // Offset zero is an escape: the next bit says whether this is
                // the end of the stream or a request to widen the code.
                const uint32_t widen = d.getBits(1);
                if (d.ran_out)
                    break;

                if (!widen)
                {
                    ok = true;
                    break;
                }

                if (++bits > WRA_MAX_BITS)
                {
                    Debug_printv("[%s] grew its code past %u bits",
                                 e.filename.c_str(), WRA_MAX_BITS);
                    break;
                }
            }
            else
            {
                const uint32_t length = d.getBits(5);
                if (d.ran_out)
                    break;

                // The offset is an absolute index into the window, not a
                // distance back from the write position, and it is one-based.
                for (uint32_t i = 0; i < length; i++)
                    emit(d.window[(offset - 1 + i) % WRA_WINDOW]);
            }
        }

        if (data.size() > WRA_MAX_OUTPUT)
        {
            Debug_printv("[%s] decoded past %lu bytes, giving up",
                         e.filename.c_str(), (unsigned long)WRA_MAX_OUTPUT);
            break;
        }
    }

    free(decoder);
    free(compressed);

    if (!ok)
    {
        // Reaching the end of the compressed span without the stream's own end
        // marker means the entry is truncated or the scan mis-framed it. Fail
        // loudly rather than serve a short file that looks complete.
        Debug_printv("[%s] did not terminate cleanly after %lu bytes",
                     e.filename.c_str(), (unsigned long)data.size());
        data.clear();
        return false;
    }

    cached_entry = (int32_t)e.data_offset;
    return true;
}


/********************************************************
 * Reading
 ********************************************************/

bool WRAMStream::seekPath(std::string path)
{
    seekCalled = true;
    entry_index = 0;

    if (!seekEntry(path))
    {
        Debug_printv("Not found! [%s]", path.c_str());
        return false;
    }

    // The true size is only knowable once the entry has been decompressed -
    // nothing in the container states it. This is the ONLY place it is set;
    // a listing reports the compressed span instead, so that walking a
    // directory never decodes anything.
    if (!extractEntry(entry))
        return false;

    _size = (uint32_t)data.size();
    _position = 0;

    Debug_printv("filename[%s] type[%s] size[%lu]",
                 entry.filename.c_str(), decodeType(entry.file_type).c_str(),
                 (unsigned long)_size);

    return true;
}

uint32_t WRAMStream::readFile(uint8_t *buf, uint32_t size)
{
    // The entry is decompressed in full by seekPath(), so this is a plain copy
    // out of it.
    //
    // _position is READ here but never written: MMediaStream::read() advances
    // it by whatever this returns. Advancing it here as well moves it twice per
    // call, so the entry ends at half its length.
    if (_position >= data.size())
        return 0;

    uint32_t remaining = (uint32_t)data.size() - _position;
    if (size > remaining)
        size = remaining;

    std::memcpy(buf, data.data() + _position, size);
    return size;
}


/********************************************************
 * File implementations
 ********************************************************/

bool WRAMFile::rewindDirectory()
{
    dirIsOpen = true;

    auto image = ImageBroker::obtain<WRAMStream>("wra", url);
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

    media_id = "wra";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_archive = name;

    return true;
}

MFile *WRAMFile::getNextFileInDir()
{
    // A failed rewind has already reset the entry counter, so reading on would
    // serve entry 0 forever with dirIsOpen still false - an endless listing
    // rather than an empty one.
    if (!dirIsOpen && !rewindDirectory())
        return nullptr;

    auto image = ImageBroker::obtain<WRAMStream>("wra", url);
    if (image == nullptr)
    {
        dirIsOpen = false;
        return nullptr;
    }

    if (image->getNextImageEntry())
    {
        // Converted BEFORE the entry URL is built, so the name a listing shows
        // is the name seekEntry() matches.
        std::string filename = WRAMStream::decodeName(image->entry.filename);
        mstr::replaceAll(filename, "/", "\\");

        // fullUrl() rejoins any pathInStream: `url` alone is the CONTAINER's
        // path, so joining a child name onto it would name a file beside the
        // archive rather than one inside it.
        auto file = MFSOwner::File(fullUrl() + "/" + filename);
        file->name = filename;
        file->extension = image->decodeType(image->entry.file_type);

        // The COMPRESSED span. The decompressed length is not stated anywhere
        // in the container, and the only way to learn it is to decompress -
        // which a directory walk must never do: the format document calls that
        // "very slow", and archive.cpp already documents what decoding inside
        // a listing costs. seekPath() sets the true size on access. Same
        // choice, and same reasoning, as arc.cpp makes for a mode-5 entry.
        //
        // Note the DIRECTION, which is the opposite of every other container
        // here: this UNDER-reports, and by a lot - 8898 against a real 21616
        // on GEOBOOT - where a D64 listing over-reports. Nothing truncates on
        // it: the drive bounds reads by MStream::_size, which seekPath() sets
        // correctly, and WebDAV's doGet streams chunked until read() returns
        // 0. The one place it surfaces is a PROPFIND's D:getcontentlength.
        file->size = image->entry.data_end - image->entry.data_offset;
        file->is_dir = 0;

        return file;
    }

    dirIsOpen = false;
    return nullptr;
}
