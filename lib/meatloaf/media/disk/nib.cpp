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

#include "nib.h"

#include <cstring>

#include <zlib.h>

/* GCR-to-Nibble conversion tables */
static const uint8_t gcr_decode_high[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x80, 0x00, 0x10, 0xff, 0xc0, 0x40, 0x50,
    0xff, 0xff, 0x20, 0x30, 0xff, 0xf0, 0x60, 0x70,
    0xff, 0x90, 0xa0, 0xb0, 0xff, 0xd0, 0xe0, 0xff
};

static const uint8_t gcr_decode_low[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x08, 0x00, 0x01, 0xff, 0x0c, 0x04, 0x05,
    0xff, 0xff, 0x02, 0x03, 0xff, 0x0f, 0x06, 0x07,
    0xff, 0x09, 0x0a, 0x0b, 0xff, 0x0d, 0x0e, 0xff
};

// 5 GCR bytes -> 4 plain bytes. Invalid nibbles decode to 0xf0/0x0f rather than
// failing: a false sync inside a gap yields garbage that the header checksum
// then rejects, which is the check that matters.
static void gcr_to_bytes(const uint8_t *gcr, uint8_t *plain)
{
    plain[0] = gcr_decode_high[gcr[0] >> 3]
             | gcr_decode_low[((gcr[0] << 2) | (gcr[1] >> 6)) & 0x1f];
    plain[1] = gcr_decode_high[(gcr[1] >> 1) & 0x1f]
             | gcr_decode_low[((gcr[1] << 4) | (gcr[2] >> 4)) & 0x1f];
    plain[2] = gcr_decode_high[((gcr[2] << 1) | (gcr[3] >> 7)) & 0x1f]
             | gcr_decode_low[(gcr[3] >> 2) & 0x1f];
    plain[3] = gcr_decode_high[((gcr[3] << 3) | (gcr[4] >> 5)) & 0x1f]
             | gcr_decode_low[gcr[4] & 0x1f];
}

// MStream::read() returns at most one block, so a caller wanting `len` bytes
// has to loop until it has them or the stream stops giving.
static bool nib_read_at(MStream *stream, uint32_t offset, uint8_t *buf, uint32_t len)
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
 * Image access
 *
 * A .nib streams straight out of the container; a .nbz is inflated once and
 * read from memory, because gzip has no random access. Everything above this
 * point works in terms of imageRead() and does not care which it is.
 ********************************************************/

uint32_t NIBMStream::imageSize()
{
    if (inflated)
        return (uint32_t)image_buffer.size();

    uint32_t size = containerStream->size();
    return (size > base_offset) ? (size - base_offset) : 0;
}

bool NIBMStream::imageRead(uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (inflated)
    {
        if ((uint64_t)offset + len > image_buffer.size())
            return false;
        std::memcpy(buf, image_buffer.data() + offset, len);
        return true;
    }

    return nib_read_at(containerStream.get(), base_offset + offset, buf, len);
}

bool NIBMStream::inflateContainer()
{
    // The largest a .nib can legitimately be. Anything claiming more is a
    // compressed .nb2, and refusing is better than exhausting the heap for a
    // format whose extra passes would be discarded anyway.
    const uint32_t limit = NIB_HEADER_SIZE + (NIB_MAX_TRACKS * NIB_TRACK_LENGTH);

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));

    // 16 + MAX_WBITS selects gzip framing rather than raw zlib.
    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK)
    {
        Debug_printv("cannot start the inflater");
        return false;
    }

    image_buffer.clear();

    std::vector<uint8_t> in(4096);
    std::vector<uint8_t> out(16384);

    uint32_t offset = 0;
    int status = Z_OK;

    while (status != Z_STREAM_END)
    {
        if (!containerStream->seek(offset))
            break;

        uint32_t got = containerStream->read(in.data(), (uint32_t)in.size());
        if (got == 0)
            break;
        offset += got;

        strm.next_in = in.data();
        strm.avail_in = got;

        while (strm.avail_in > 0)
        {
            strm.next_out = out.data();
            strm.avail_out = (uInt)out.size();

            status = inflate(&strm, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END)
            {
                Debug_printv("inflate failed [%d]", status);
                inflateEnd(&strm);
                image_buffer.clear();
                return false;
            }

            uint32_t produced = (uint32_t)(out.size() - strm.avail_out);
            if (image_buffer.size() + produced > limit)
            {
                Debug_printv("compressed image expands past %lu bytes - a .nbz of a "
                             "multi-pass .nb2 is not supported", (unsigned long)limit);
                inflateEnd(&strm);
                image_buffer.clear();
                return false;
            }

            image_buffer.insert(image_buffer.end(), out.begin(), out.begin() + produced);

            if (status == Z_STREAM_END)
                break;
        }
    }

    inflateEnd(&strm);

    if (status != Z_STREAM_END || image_buffer.size() < NIB_HEADER_SIZE)
    {
        Debug_printv("compressed image ended early [%lu bytes]",
                     (unsigned long)image_buffer.size());
        image_buffer.clear();
        return false;
    }

    inflated = true;
    Debug_printv("inflated %lu bytes", (unsigned long)image_buffer.size());
    return true;
}


/********************************************************
 * Container
 ********************************************************/

bool NIBMStream::parseHeader()
{
    // Idempotent, and latches on failure too: readHeader() runs on every
    // directory rewind, and re-walking a header that will not parse costs a
    // request every time.
    if (header_parsed)
        return header_ok;

    header_parsed = true;
    header_ok = false;
    std::memset(track_offset, 0, sizeof(track_offset));

    MStream *cs = containerStream.get();

    base_offset = 0;
    inflated = false;
    image_buffer.clear();

    // Identify the container by CONTENT, not by extension - the same reader
    // serves .nib, .nb2 and .nbz, and a misnamed file is common enough.
    uint8_t probe[16];
    if (!nib_read_at(cs, 0, probe, sizeof(probe)))
    {
        Debug_printv("cannot read the NIB header");
        return false;
    }

    if (probe[0] == 0x1f && probe[1] == 0x8b)
    {
        if (!inflateContainer())
            return false;
    }
    else if (std::memcmp(probe + 1, "MNIB-1541-RAW", 13) == 0)
    {
        // MFileSystem::byContent() has always described .nbz this way: the
        // signature a byte in. Honour it rather than argue with it.
        base_offset = 1;
    }

    const uint32_t size = imageSize();

    uint8_t header[NIB_HEADER_SIZE];
    if (size < NIB_HEADER_SIZE || !imageRead(0, header, sizeof(header)))
    {
        Debug_printv("cannot read the NIB header");
        return false;
    }

    if (std::memcmp(header, "MNIB-1541-RAW", 13) != 0)
    {
        Debug_printv("not an MNIB-1541-RAW image");
        return false;
    }

    // The table is two bytes per stored track - the half track index, then its
    // density - and ends at the first entry with a zero half track. Bounded by
    // the table's own capacity rather than by trusting a terminator to arrive:
    // the previous implementation walked until it found one and would read on
    // through the track data forever if it did not.
    uint32_t entries = 0;
    for (uint32_t i = 0; i < NIB_MAX_TRACKS; i++)
    {
        uint8_t half_track = header[NIB_TABLE_OFFSET + (i * 2)];
        if (half_track == 0)
            break;
        entries++;
    }

    if (entries == 0)
    {
        Debug_printv("NIB track table is empty");
        return false;
    }

    // Per-track stride, DERIVED rather than assumed. A .nib stores one pass per
    // track and a .nb2 stores several, and nothing in the header says which -
    // but the file length does, so the same code reads either. It has to come
    // out a whole number of track windows; anything else means the layout is
    // not what this expects and serving a track from a fractional offset would
    // be worse than refusing.
    if (size <= NIB_HEADER_SIZE)
    {
        Debug_printv("NIB image holds no track data");
        return false;
    }

    const uint32_t data_bytes = size - NIB_HEADER_SIZE;
    track_stride = data_bytes / entries;
    track_stride -= (track_stride % NIB_TRACK_LENGTH);

    if (track_stride < NIB_TRACK_LENGTH)
    {
        Debug_printv("NIB image has %lu bytes for %lu tracks - less than one track each",
                     (unsigned long)data_bytes, (unsigned long)entries);
        return false;
    }

    if (track_stride != NIB_TRACK_LENGTH)
    {
        // A .nb2 holds several passes of each track. Only the first is read;
        // the rest exist so a copier can compare them.
        Debug_printv("NIB stride is %lu bytes - %lu passes per track",
                     (unsigned long)track_stride,
                     (unsigned long)(track_stride / NIB_TRACK_LENGTH));
    }

    for (uint32_t i = 0; i < entries; i++)
    {
        uint8_t half_track = header[NIB_TABLE_OFFSET + (i * 2)];
        if (half_track > (NIB_MAX_TRACKS * 2))
            continue;

        uint32_t offset = NIB_HEADER_SIZE + (i * track_stride);
        if (offset + NIB_TRACK_LENGTH > size)
            break;

        track_offset[half_track] = offset;
    }

    header_ok = true;
    return true;
}

bool NIBMStream::readHeader()
{
    // The container header first: D64MStream::readHeader() immediately seeks
    // 18/0, which goes through seekSector() and needs the track table. This
    // used to delegate FIRST and then read the header from wherever the stream
    // had been left, so the values it logged were never the header's.
    if (!parseHeader())
        return false;

    return D64MStream::readHeader();
}

bool NIBMStream::loadTrack(uint8_t track)
{
    if (cached_track == (int)track)
        return true;

    const uint32_t half_track = (uint32_t)track * 2;
    if (half_track >= (sizeof(track_offset) / sizeof(track_offset[0])) ||
        track_offset[half_track] == 0)
    {
        Debug_printv("track[%d] is not present in the image", track);
        return false;
    }

    if (track_buffer.size() != NIB_TRACK_LENGTH)
        track_buffer.assign(NIB_TRACK_LENGTH, 0);

    track_bytes = 0;
    cached_track = -1;

    if (!imageRead(track_offset[half_track], track_buffer.data(), NIB_TRACK_LENGTH))
    {
        Debug_printv("cannot read track[%d]", track);
        return false;
    }

    track_bytes = NIB_TRACK_LENGTH;
    cached_track = track;
    return true;
}


/********************************************************
 * GCR
 ********************************************************/

int NIBMStream::findSync(int p) const
{
    if (track_bytes == 0)
        return -1;

    const int bytes = (int)track_bytes;
    if (p < 0 || p >= bytes)
        return -1;

    uint8_t previous = 0x00;

    while (p < bytes)
    {
        uint8_t current = track_buffer[p];
        p++;

        // The sync flag goes up after the tenth 1 bit.
        if ((previous & 0x03) == 0x03 && current == 0xff)
        {
            // Step over the rest of the sync.
            while (p < bytes && track_buffer[p] == 0xff)
                p++;
            return (p < bytes) ? p : -1;
        }

        previous = current;
    }

    return -1;
}

bool NIBMStream::decodeBlock(int p, uint8_t *buf, int groups) const
{
    if (p < 0 || (uint32_t)(p + (groups * 5)) > track_bytes)
        return false;

    for (int i = 0; i < groups; i++)
        gcr_to_bytes(track_buffer.data() + p + (i * 5), buf + (i * 4));

    return true;
}


/********************************************************
 * Sectors
 ********************************************************/

bool NIBMStream::loadSector(uint8_t track, uint8_t sector)
{
    uint8_t header[8];
    uint8_t block[260];

    int p = 0;
    last_data_checksum_ok = true;

    // A track holds at most 21 sectors; the bound is slack for false syncs in
    // the gaps. The previous implementation looped on a flag that a failing
    // findSync() could leave true, and read a sector header before it had found
    // any sync at all.
    for (int guard = 0; guard < 64; guard++)
    {
        p = findSync(p);
        if (p < 0)
            break;

        if (!decodeBlock(p, header, 2))
            break;

        if (header[0] != 0x08)
            continue;   // not a sector header block

        uint8_t checksum = header[1] ^ header[2] ^ header[3] ^ header[4] ^ header[5];
        if (checksum != 0)
            continue;   // false sync, or a header this drive cannot read

        gcr_sector_header.code = header[0];
        gcr_sector_header.checksum = header[1];
        gcr_sector_header.sector = header[2];
        gcr_sector_header.track = header[3];
        gcr_sector_header.id1 = header[4];
        gcr_sector_header.id0 = header[5];

        if (header[2] != sector)
        {
            p += 10;    // past this header, on to the next sync
            continue;
        }

        // The data block follows its own sync.
        int d = findSync(p + 10);
        if (d < 0)
        {
            Debug_printv("no data block after header for track[%d] sector[%d]", track, sector);
            return false;
        }

        if (!decodeBlock(d, block, 65))
        {
            Debug_printv("track[%d] sector[%d] data block runs past the track", track, sector);
            return false;
        }

        if (block[0] != 0x07)
        {
            // Returning true here left sector_buffer holding the PREVIOUS
            // sector's bytes for the caller to serve as this one's.
            Debug_printv("track[%d] sector[%d] data block id [%02X]", track, sector, block[0]);
            return false;
        }

        uint8_t data_checksum = block[257];
        for (int i = 0; i < 256; i++)
            data_checksum ^= block[i + 1];
        if (data_checksum != 0)
        {
            // Reported, not refused: a nibbled original can carry deliberately
            // bad checksums, and the caller has no error channel.
            Debug_printv("track[%d] sector[%d] data checksum error", track, sector);
            last_data_checksum_ok = false;
        }

        std::memcpy(sector_buffer, block + 1, sizeof(sector_buffer));
        return true;
    }

    Debug_printv("track[%d] sector[%d] not found", track, sector);
    return false;
}

bool NIBMStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
{
    uint16_t c = curPartition().block_allocation_map.size() - 1;
    uint8_t start_track = curPartition().block_allocation_map[0].start_track;
    uint8_t end_track = curPartition().block_allocation_map[c].end_track;
    if (track < start_track || track > end_track)
    {
        Debug_printv("Invalid Track: track[%d] start_track[%d] end_track[%d]", track, start_track, end_track);
        return false;
    }

    c = getSectorCount(track);
    if (sector >= c)
    {
        Debug_printv("Invalid Sector: sector[%d] sectorsPerTrack[%d]", sector, c);
        return false;
    }

    if (!parseHeader())
        return false;

    if (!loadTrack(track))
        return false;

    if (!loadSector(track, sector))
        return false;

    // Block number is the same linear count a .d64 would give, so anything
    // reporting one stays comparable across the two formats.
    uint16_t sectorOffset = 0;
    for (uint8_t index = 1; index < track; ++index)
        sectorOffset += getSectorCount(index);
    sectorOffset += sector;

    this->block = sectorOffset;
    this->track = track;
    this->sector = sector;
    sector_pos = offset;

    return true;
}

uint32_t NIBMStream::readContainer(uint8_t *buf, uint32_t size)
{
    // The sector is already decoded in RAM, so the cursor is sector_pos and NOT
    // _position - that is the position in the FILE being read, and readFile()
    // calls this repeatedly between seeks, so indexing a sector buffer by it
    // walked off the end from the second block of any file onward and returned
    // whatever followed in memory as file content. Same defect g64.cpp had.
    if (sector_pos >= block_size)
        return 0;

    uint32_t remaining = (uint32_t)block_size - sector_pos;
    if (size > remaining)
        size = remaining;

    std::memcpy(buf, sector_buffer + sector_pos, size);
    sector_pos += size;
    return size;
}
