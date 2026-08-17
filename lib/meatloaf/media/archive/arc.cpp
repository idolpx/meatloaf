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
//
// The decompressors below are a port of Chris Smeets' and Marko Makela's
// unarc.c from cbmconvert 2.1.2 (GPL), which is the reference implementation
// of this format. See .reference/cbmconvert-2.1.2/unarc.c.

#include "arc.h"


#include <cstdlib>
#include <cstring>

#include "utils.h"

// A Commodore ARC entry is one of six things, and only the first is plain:
//
//   0  stored          the bytes as they are
//   1  packed          run-length encoded
//   2  squeezed        Huffman coded
//   3  crunched        LZW
//   4  squashed        Huffman coded, then run-length encoded
//   5  crunched, one pass   LZW, with the size and checksum at the END
//
// Run-length decoding sits ON TOP of the byte producer for every mode except
// stored and squeezed, so it is applied to whatever Huffman or LZW hands back
// rather than to the container bytes.
#define ARC_MODE_STORED     0
#define ARC_MODE_PACKED     1
#define ARC_MODE_SQUEEZED   2
#define ARC_MODE_CRUNCHED   3
#define ARC_MODE_SQUASHED   4
#define ARC_MODE_CRUNCHED1  5

// Largest entry this will decompress. A mode-5 entry does not declare its size
// until the stream ends, so it needs a ceiling of some sort; the reference uses
// 64 KB and calls it enough for everyone. This is more generous but still
// bounded, because the size field is three bytes and a corrupt one can ask for
// 16 MB.
#define ARC_MAX_ENTRY_SIZE  (1024UL * 1024UL)

// Codes 256 and 257 are reserved by the cruncher: 256 ends the entry.
#define ARC_LZW_EOF         256
#define ARC_LZW_TABLE_SIZE  4096
#define ARC_LZW_STACK_SIZE  512


namespace {

// Everything the decompressors need, over a buffer of container bytes already
// in RAM. The reference reads the container a bit at a time through stdio;
// doing that against an MStream would be a read call per BIT, which over a
// network is hopeless - so the entry's compressed bytes are fetched once and
// this walks them in memory.
struct ArcDecoder
{
    const uint8_t *input = nullptr;
    uint32_t input_size = 0;
    uint32_t input_pos = 0;
    bool eof = false;

    uint32_t bit_buffer = 2;

    // Why the decode stopped. These are different faults: running out of
    // container bytes means the window this was given is too small, while the
    // stream ending on its own means the archive says the entry is over.
    bool input_ran_out = false;
    bool stream_ended = false;

    // Huffman, for the squeezed and squashed modes.
    uint32_t hc[256] = { 0 };   // codes
    uint8_t hl[256] = { 0 };    // their lengths
    uint8_t hv[256] = { 0 };    // the byte each represents
    int hcount = 0;

    // LZW, for the crunched modes.
    uint16_t lz_prefix[ARC_LZW_TABLE_SIZE] = { 0 };
    uint8_t lz_ext[ARC_LZW_TABLE_SIZE] = { 0 };
    uint8_t stack[ARC_LZW_STACK_SIZE] = { 0 };
    int lzstack = 0;
    int state = 0;
    int cdlen = 0;
    int code = 0;
    int wtcl = 0;
    int wttcl = 0;
    int oldcode = 0;
    int incode = 0;
    uint8_t kay = 0;
    int omega = 0;
    uint8_t finchar = 0;
    int ncodes = 0;

    uint8_t mode = 0;
    uint8_t version = 0;
    uint32_t ctrl = 254;

    // Checksum, and the two shapes it takes.
    uint32_t crc = 0;
    uint8_t crc2 = 0;

    uint8_t getByte()
    {
        if (eof || input_pos >= input_size)
        {
            if (input_pos >= input_size)
                input_ran_out = true;
            eof = true;
            return 0;
        }
        return input[input_pos++];
    }

    uint16_t getWord()
    {
        uint16_t v = getByte();
        v |= (uint16_t)getByte() << 8;
        return v;
    }

    uint32_t getThree()
    {
        uint32_t v = getByte();
        v |= (uint32_t)getByte() << 8;
        v |= (uint32_t)getByte() << 16;
        return v;
    }

    // Bits come out least significant first, and the buffer carries a sentinel
    // bit above the byte so it knows when it is empty.
    bool getBit()
    {
        uint32_t result = (bit_buffer >>= 1);
        if (result == 1)
            return (bool)(1 & (bit_buffer = getByte() | 0x0100));
        return (bool)(1 & result);
    }

    // Shell sort, ordering the Huffman table by descending code length. The
    // decoder walks it from the shortest code up, so the order is load-bearing.
    void sortHuffman()
    {
        int m = 256;
        while (m >>= 1)
        {
            int k = 256 - m;
            int j = 1;
            do {
                int i = j;
                do {
                    int h = i + m;
                    if (hl[h - 1] > hl[i - 1])
                    {
                        uint32_t t = hc[i - 1]; hc[i - 1] = hc[h - 1]; hc[h - 1] = t;
                        uint8_t u = hv[i - 1]; hv[i - 1] = hv[h - 1]; hv[h - 1] = u;
                        u = hl[i - 1]; hl[i - 1] = hl[h - 1]; hl[h - 1] = u;
                        i -= m;
                    }
                    else
                        break;
                } while (i >= 1);
                j += 1;
            } while (j <= k);
        }
    }

    // Reads the 256 five-bit code lengths and the codes themselves. Only the
    // squeezed and squashed modes carry one.
    bool readHuffmanTable()
    {
        hcount = 255;

        for (int w = 0; w < 256; w++)
        {
            hv[w] = (uint8_t)w;

            hl[w] = 0;
            uint32_t mask = 1;
            for (int i = 1; i < 6; i++)
            {
                if (getBit())
                    hl[w] |= mask;
                mask <<= 1;
            }

            if (hl[w] > 24)
                return false;   // code too long to be real

            hc[w] = 0;
            if (hl[w])
            {
                uint32_t bit = 0;
                mask = 1;
                while (bit < hl[w])
                {
                    if (getBit())
                        hc[w] |= mask;
                    bit++;
                    mask <<= 1;
                }
            }
            else
                hcount--;
        }

        sortHuffman();
        return !eof;
    }

    uint8_t huffin()
    {
        uint32_t hcode = 0;
        uint32_t mask = 1;
        int size = 1;
        int now = hcount;

        do {
            if (getBit())
                hcode |= mask;

            while (hl[now] == size)
            {
                if (hc[now] == hcode)
                    return hv[now];

                if (--now < 0)
                {
                    eof = true;     // the table cannot decode this
                    return 0;
                }
            }
            size++;
            mask <<= 1;
        } while (size < 24);

        eof = true;
        return 0;
    }

    void push(uint8_t c)
    {
        if (lzstack >= ARC_LZW_STACK_SIZE)
        {
            // The reference longjmps out here. Ending the entry is the same
            // outcome without the control flow.
            eof = true;
            return;
        }
        stack[lzstack++] = c;
    }

    uint8_t pop()
    {
        if (!lzstack)
        {
            eof = true;
            return 0;
        }
        return stack[--lzstack];
    }

    int getCode()
    {
        code = 0;
        for (int i = cdlen; i > 0; i--)
            code = (code << 1) | (getBit() ? 1 : 0);

        // A one-pass crunch does not know its own size until it ends, so the
        // size and checksum are trailers rather than header fields.
        if (code == ARC_LZW_EOF && mode == ARC_MODE_CRUNCHED1)
        {
            uint32_t check = 0;
            for (int i = 16; i > 0; i--)
                check = (check << 1) | (getBit() ? 1 : 0);
            trailer_check = (uint16_t)check;

            uint32_t size = 0;
            for (int i = 24; i > 0; i--)
                size = (size << 1) | (getBit() ? 1 : 0);
            trailer_size = size;

            for (int i = 16; i > 0; i--)
                getBit();   // never implemented, but the bits are there
            has_trailer = true;
        }

        // Codes grow to twelve bits and stay there.
        if (cdlen < 12)
        {
            if (!(--wttcl))
            {
                wtcl = wtcl << 1;
                cdlen++;
                wttcl = wtcl;
            }
        }

        return code;
    }

    // LZW, per Welch. Code 256 ends the entry; the table never resets.
    uint8_t uncrunch()
    {
        for (;;)
        {
            switch (state)
            {
            case 0:
                lzstack = 0;
                ncodes = 258;   // two reserved codes
                wtcl = 256;
                wttcl = 254;    // the first bump is short by the reserved pair
                cdlen = 9;
                oldcode = getCode();

                if (oldcode == ARC_LZW_EOF)
                {
                    eof = true;     // a zero length entry
                    stream_ended = true;
                    return 0;
                }
                kay = (uint8_t)(oldcode & 0xff);
                finchar = kay;
                state = 1;
                return kay;

            case 1:
                incode = getCode();

                if (incode == ARC_LZW_EOF)
                {
                    state = 0;
                    eof = true;
                    stream_ended = true;
                    return 0;
                }

                if (incode >= ncodes)
                {
                    // A code the table does not hold yet: it can only be the
                    // previous string plus its own first character.
                    kay = finchar;
                    push(kay);
                    code = oldcode;
                    omega = oldcode;
                    incode = ncodes;
                }

                while (code > 255)
                {
                    if (code >= ARC_LZW_TABLE_SIZE)
                    {
                        eof = true;
                        return 0;
                    }
                    push(lz_ext[code]);
                    code = lz_prefix[code];
                }

                kay = (uint8_t)code;
                finchar = (uint8_t)code;
                state = 2;
                return kay;

            case 2:
                if (!lzstack)
                {
                    omega = oldcode;
                    if (ncodes < ARC_LZW_TABLE_SIZE)
                    {
                        lz_prefix[ncodes] = (uint16_t)omega;
                        lz_ext[ncodes] = kay;
                        ncodes++;
                    }
                    oldcode = incode;
                    state = 1;
                    continue;   // the reference recurses here
                }
                return pop();

            default:
                eof = true;
                return 0;
            }
        }
    }

    // One byte from whichever producer this entry's mode calls for.
    uint8_t unpack()
    {
        switch (mode)
        {
        case ARC_MODE_STORED:
        case ARC_MODE_PACKED:
            return getByte();

        case ARC_MODE_SQUEEZED:
        case ARC_MODE_SQUASHED:
            return huffin();

        case ARC_MODE_CRUNCHED:
        case ARC_MODE_CRUNCHED1:
            return uncrunch();

        default:
            eof = true;
            return 0;
        }
    }

    void updateChecksum(uint8_t c)
    {
        if (version == 1)
            crc += c;                   // version 1 just adds
        else
            crc += (uint8_t)(c ^ (++crc2));
    }

    uint16_t trailer_check = 0;
    uint32_t trailer_size = 0;
    bool has_trailer = false;
};

} // namespace


// MStream::read() returns at most one block, so a caller wanting `len` bytes
// has to loop until it has them or the stream stops giving.
static bool arc_read_at(MStream *stream, uint32_t offset, uint8_t *buf, uint32_t len)
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

bool ARCMStream::skipBasicLoader()
{
    // A .arc starts with its first entry header, whose version byte is 1 or 2.
    // A .sda is a PRG: a BASIC line that SYSes into the dissolver, then the
    // machine code, then the archive. The BASIC line number doubles as the
    // count of 254-byte blocks the whole preamble occupies.
    uint8_t probe[10];
    if (!arc_read_at(containerStream.get(), 0, probe, sizeof(probe)))
        return false;

    if (probe[0] == 2)
    {
        archive_offset = 0;     // a version 2 archive
        return true;
    }

    if (probe[0] != 1)
        return false;           // neither, so not a Commodore archive

    // probe[1] is the rest of the load address / link, probe[2..3] the next
    // line pointer, probe[4..5] the line number, probe[6] the token.
    uint16_t line_number = (uint16_t)(probe[4] | (probe[5] << 8));
    uint8_t token = probe[6];

    if (token != 0x9e)
    {
        archive_offset = 0;     // a version 1 archive, not an SDA
        return true;
    }

    uint8_t cpu = probe[8];     // '2' for a C64 dissolver, '7' for a C128

    int32_t skip = ((int32_t)line_number - 6) * 254;
    if (line_number == 15 && cpu == '7')
        skip -= 1;              // SDA232.128 is one byte short of the pattern

    if (skip < 0)
        return false;

    archive_offset = (uint32_t)skip;
    return true;
}

bool ARCMStream::loadEntries()
{
    entries.clear();

    MStream *cs = containerStream.get();
    const uint32_t size = cs->size();

    uint32_t offset = archive_offset;

    // Bounded by what a CBM disk could hold rather than by trusting the chain
    // to terminate: a truncated or padded archive otherwise walks forever.
    for (int guard = 0; guard < 1024; guard++)
    {
        if (offset + 8 > size)
            break;

        uint8_t fixed[8];
        if (!arc_read_at(cs, offset, fixed, sizeof(fixed)))
            break;

        Entry e;
        e.offset = offset;
        e.version = fixed[0];
        e.mode = fixed[1];
        e.check = (uint16_t)(fixed[2] | (fixed[3] << 8));
        e.size = (uint32_t)fixed[4] | ((uint32_t)fixed[5] << 8) | ((uint32_t)fixed[6] << 16);
        // fixed[7] is the low byte of the block count; the header continues.

        uint8_t rest[4];
        if (!arc_read_at(cs, offset + 7, rest, 4))
            break;

        e.blocks = (uint16_t)(rest[0] | (rest[1] << 8));
        e.type = rest[2];
        uint8_t fnlen = rest[3];

        // The end of the archive is padding, not a marker - an invalid header
        // is how the reference knows it has run out too.
        if (e.version == 0 || e.version > 2 || e.mode > 5 || fnlen > 16 || e.blocks == 0)
            break;

        if (e.version == 1 && e.mode > ARC_MODE_SQUEEZED)
            break;              // version 1 only ever stored, packed, squeezed

        if (e.type != 'P' && e.type != 'S' && e.type != 'U' && e.type != 'R')
            break;

        uint8_t name[17] = { 0 };
        if (fnlen && !arc_read_at(cs, offset + 11, name, fnlen))
            break;
        e.filename = std::string((const char *)name, fnlen);

        uint32_t after_name = offset + 11 + fnlen;
        if (e.version > 1)
        {
            uint8_t tail[3];
            if (!arc_read_at(cs, after_name, tail, sizeof(tail)))
                break;
            e.rel_record_size = tail[0];
            e.date = (uint16_t)(tail[1] | (tail[2] << 8));
        }

        entries.push_back(e);

        // The next header sits exactly this many bytes on. That is what makes
        // the directory walk cheap: no compressed data is touched, and no
        // Huffman table has to be built to find the entry after this one.
        uint32_t next = offset + ((uint32_t)e.blocks * 254);
        if (next <= offset || next >= size)
            break;
        offset = next;
    }

    entry_count = entries.size();
    return !entries.empty();
}

bool ARCMStream::readHeader()
{
    if (header_parsed)
        return header_ok;

    header_parsed = true;
    header_ok = false;

    if (!skipBasicLoader())
    {
        Debug_printv("not a Commodore ARC or SDA");
        return false;
    }

    if (!loadEntries())
    {
        Debug_printv("no entries in the archive");
        return false;
    }

    header_ok = true;
    return true;
}


/********************************************************
 * Entries
 ********************************************************/

bool ARCMStream::seekEntry( uint16_t index )
{
    if (!readHeader())
        return false;

    if (!index || index > entries.size())
        return false;

    entry = entries[index - 1];
    entry_index = index;
    return true;
}

bool ARCMStream::seekEntry( std::string filename )
{
    if (!filename.size())
        return false;

    if (!readHeader())
        return false;

    mstr::replaceAll(filename, "\\", "/");
    bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));

    for (uint16_t index = 1; index <= entries.size(); index++)
    {
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

std::string ARCMStream::decodeType(uint8_t file_type, bool show_hidden)
{
    (void)show_hidden;

    switch (file_type)
    {
        case 'S': return "seq";
        case 'P': return "prg";
        case 'U': return "usr";
        case 'R': return "rel";
        default:  return "prg";
    }
}


/********************************************************
 * Extraction
 ********************************************************/

bool ARCMStream::extractEntry( const Entry &e )
{
    if (cached_entry == (int)e.offset)
        return true;

    data.clear();
    cached_entry = -1;
    checksum_ok = true;

    // The whole of this entry's compressed data, which the header locates for
    // us. Read once rather than a bit at a time out of the container.
    //
    // Plus slack past the entry's own blocks. The reference reads the container
    // as one continuous stream and only seeks to the next entry once this one
    // has finished, so a decoder that needs a few more bits than the block
    // count strictly covers simply reads on; cutting it off exactly at
    // blocks * 254 truncates such an entry and fails its checksum.
    const uint32_t compressed_size = ((uint32_t)e.blocks * 254) + 512;
    uint8_t *compressed = (uint8_t *)malloc(compressed_size);
    if (compressed == nullptr)
    {
        Debug_printv("no memory for a %lu byte entry", (unsigned long)compressed_size);
        return false;
    }

    uint32_t available_bytes = compressed_size;
    uint32_t container_size = containerStream->size();
    if (e.offset + compressed_size > container_size)
        available_bytes = (container_size > e.offset) ? (container_size - e.offset) : 0;

    if (available_bytes == 0 || !arc_read_at(containerStream.get(), e.offset, compressed, available_bytes))
    {
        Debug_printv("cannot read the data of [%s]", e.filename.c_str());
        free(compressed);
        return false;
    }

    ArcDecoder d;
    d.input = compressed;
    d.input_size = available_bytes;
    d.version = e.version;
    d.mode = e.mode;

    // Step the decoder over the header it has just been handed - the bit reader
    // has to start exactly where the payload does, and for the squeezed modes
    // the Huffman table is part of that.
    uint32_t header_bytes = 11 + (uint32_t)e.filename.size() + (e.version > 1 ? 3 : 0);
    d.input_pos = header_bytes;

    d.ctrl = 254;
    if (e.mode == ARC_MODE_PACKED)
        d.ctrl = d.getByte();   // version 2 always uses $FE, version 1 varies

    if (e.mode == ARC_MODE_SQUEEZED || e.mode == ARC_MODE_SQUASHED)
    {
        if (!d.readHuffmanTable())
        {
            Debug_printv("[%s] has an unreadable Huffman table", e.filename.c_str());
            free(compressed);
            return false;
        }
    }

    // A one-pass crunch does not declare its size up front.
    uint32_t length = e.size;
    if (e.mode == ARC_MODE_CRUNCHED1 || length == 0)
        length = 65536;
    if (length > ARC_MAX_ENTRY_SIZE)
        length = ARC_MAX_ENTRY_SIZE;

    data.reserve(length);

    while (data.size() < length)
    {
        uint8_t c = d.unpack();
        if (d.eof)
            break;

        // Run-length decoding rides on top of the byte producer, for every
        // mode except stored and squeezed.
        if (e.mode != ARC_MODE_STORED && e.mode != ARC_MODE_SQUEEZED && c == d.ctrl)
        {
            uint32_t count = d.unpack();
            c = d.unpack();

            if (d.eof)
                break;

            if (count == 0)
                count = (e.version == 1) ? 255 : 256;

            while (--count && data.size() < length)
            {
                d.updateChecksum(c);
                data.push_back(c);
            }
        }

        d.updateChecksum(c);
        data.push_back(c);
    }

    uint16_t expected = d.has_trailer ? d.trailer_check : e.check;
    if (((uint16_t)d.crc ^ expected) & 0xffff)
    {
        // Reported, not refused: the bytes are the best this can do, and the
        // caller has no error channel to hear about it on.
        Debug_printv("[%s] checksum error - got %04X, expected %04X, "
                     "produced %lu of %lu declared, mode %u, %s",
                     e.filename.c_str(), (unsigned)(d.crc & 0xffff), (unsigned)expected,
                     (unsigned long)data.size(), (unsigned long)e.size,
                     (unsigned)e.mode,
                     d.stream_ended ? "the archive ended the entry"
                                    : (d.input_ran_out ? "ran out of container bytes"
                                                       : "reached the declared length"));
        checksum_ok = false;
    }

    free(compressed);

    cached_entry = (int)e.offset;
    return true;
}

bool ARCMStream::seekPath(std::string path)
{
    seekCalled = true;
    entry_index = 0;

    if (!seekEntry(path))
    {
        Debug_printv("Not found! [%s]", path.c_str());
        return false;
    }

    if (!extractEntry(entry))
        return false;

    _size = (uint32_t)data.size();
    _position = 0;

    Debug_printv("filename[%s] type[%s] mode[%d] size[%lu]",
                 entry.filename.c_str(), decodeType(entry.type).c_str(),
                 entry.mode, (unsigned long)_size);

    return true;
}

uint32_t ARCMStream::readFile(uint8_t *buf, uint32_t size)
{
    // The entry is decompressed in full by seekPath(), so this is a plain copy
    // out of it - and it must never hand back more than remains.
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

bool ARCMFile::rewindDirectory()
{
    dirIsOpen = true;

    auto image = ImageBroker::obtain<ARCMStream>("arc", url);
    if (image == nullptr)
        return false;

    if (!image->readHeader())
    {
        dirIsOpen = false;
        return false;
    }

    image->resetEntryCounter();

    media_header = name;
    media_id = "arc";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_archive = name;

    return true;
}

MFile *ARCMFile::getNextFileInDir()
{
    // A failed rewind has already reset the entry counter, so reading on would
    // serve entry 0 forever with dirIsOpen still false - an endless listing
    // rather than an empty one.
    if (!dirIsOpen && !rewindDirectory())
        return nullptr;

    auto image = ImageBroker::obtain<ARCMStream>("arc", url);
    if (image == nullptr)
    {
        dirIsOpen = false;
        return nullptr;
    }

    if (image->getNextImageEntry())
    {
        std::string filename = image->entry.filename;
        mstr::replaceAll(filename, "/", "\\");

        // fullUrl() rejoins any pathInStream: `url` alone is the CONTAINER's
        // path, so joining a child name onto it would name a file beside the
        // archive rather than one inside it.
        auto file = MFSOwner::File(fullUrl() + "/" + filename);
        file->name = filename;
        file->extension = image->decodeType(image->entry.type);

        // The size the entry declares. A mode-5 entry does not know it until
        // it has been decompressed, and its header carries zero - reporting
        // the compressed span is closer than reporting nothing.
        file->size = image->entry.size ? image->entry.size
                                       : ((uint32_t)image->entry.blocks * 254);
        file->is_dir = 0;

        return file;
    }

    dirIsOpen = false;
    return nullptr;
}
