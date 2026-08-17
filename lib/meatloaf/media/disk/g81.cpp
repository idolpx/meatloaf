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

#include "g81.h"

#include <cstring>

static uint32_t g81_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// MStream::read() returns at most one block, so a caller wanting `len` bytes
// has to loop until it has them or the stream stops giving.
static bool g81_read_at(MStream *stream, uint32_t offset, uint8_t *buf, uint32_t len)
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


bool G81MStream::parseHeader()
{
    // Idempotent, and latches on failure too: readHeader() runs on every
    // directory rewind.
    if (header_parsed)
        return header_ok;

    header_parsed = true;
    header_ok = false;

    uint8_t header[G81_HEADER_SIZE];
    if (!g81_read_at(containerStream.get(), 0, header, sizeof(header)))
    {
        Debug_printv("cannot read the G81 header");
        return false;
    }

    if (std::memcmp(header, "MFM-1581", 8) != 0)
    {
        Debug_printv("not an MFM-1581 image");
        return false;
    }

    half_track_count = header[9];
    max_track_size = (uint16_t)(header[10] | (header[11] << 8));

    if (half_track_count == 0)
    {
        Debug_printv("G81 header declares no tracks");
        return false;
    }

    header_ok = true;
    return true;
}

bool G81MStream::readHeader()
{
    // The container header first: D81MStream::readHeader() immediately seeks
    // 40/0, which goes through seekSector() and needs the track table.
    if (!parseHeader())
        return false;

    return D81MStream::readHeader();
}

bool G81MStream::loadTrack(uint8_t cylinder, uint8_t head)
{
    const int id = (cylinder * 2) + head;
    if (cached_cylinder_head == id)
        return true;

    // ASSUMPTION, and one of the two this format's layout rests on: the two
    // heads of a cylinder occupy consecutive table entries, so the entry index
    // is cylinder * 2 + head. That preserves the G64 addressing shape - one
    // entry per half track position - which is the only reading consistent with
    // the rest of the note in g81.h. It has never been checked against a real
    // .g81 because none exists here.
    if (id >= (int)half_track_count)
    {
        Debug_printv("cylinder %d head %d is past the %d tracks the header declares",
                     cylinder, head, half_track_count);
        return false;
    }

    // The offset table follows the header directly - there is no speed zone
    // table on a 1581, which is the other assumption.
    uint8_t entry[4];
    if (!g81_read_at(containerStream.get(), G81_HEADER_SIZE + (id * 4), entry, sizeof(entry)))
        return false;

    uint32_t offset = g81_le32(entry);
    if (offset == 0)
    {
        Debug_printv("cylinder %d head %d is not present in the image", cylinder, head);
        return false;
    }

    // Four bytes of length, counting BITS.
    uint8_t length[4];
    if (!g81_read_at(containerStream.get(), offset, length, sizeof(length)))
        return false;

    uint32_t bits = g81_le32(length);
    uint32_t bytes = (bits + 7) / 8;

    if (bits == 0 || bytes > G81_MAX_TRACK_BYTES)
    {
        // A length read as bytes rather than bits lands around 12500 here
        // instead of 100000, and finds no sectors at all; one read from the
        // wrong offset is usually absurd. Either way, say which it was.
        Debug_printv("cylinder %d head %d declares %lu bits (%lu bytes)",
                     cylinder, head, (unsigned long)bits, (unsigned long)bytes);
        return false;
    }

    if (mfm_track.size() < bytes)
        mfm_track.assign(bytes, 0);

    if (!g81_read_at(containerStream.get(), offset + 4, mfm_track.data(), bytes))
    {
        Debug_printv("cannot read the cells of cylinder %d head %d", cylinder, head);
        return false;
    }

    mfm_track_bytes = bytes;
    cached_cylinder_head = id;
    cached_physical = -1;
    return true;
}

bool G81MStream::readPhysicalSector(uint8_t cylinder, uint8_t head, uint8_t physical)
{
    bool crc_ok = true;
    bool found = mfm::readSector(mfm_track.data(), mfm_track_bytes,
                                 cylinder, head, physical,
                                 physical_sector, G81_SECTOR_BYTES, &crc_ok);
    last_data_checksum_ok = crc_ok;
    return found;
}

bool G81MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
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

    // A CBM 1581 block is half of a physical MFM sector: blocks 0-19 are head
    // 0, 20-39 are head 1, and two consecutive blocks share one 512-byte
    // sector. Unlike a .p81 there is no side inversion here - that is a P64
    // container quirk, and this format has its own (unverified) head order.
    const uint8_t cylinder = (uint8_t)(track - 1);
    const uint8_t head = (uint8_t)(sector / 20);
    const uint8_t physical = (uint8_t)(((sector % 20) / 2) + 1);
    const uint8_t half = (uint8_t)(sector % 2);

    if (!loadTrack(cylinder, head))
        return false;

    const int want = (cylinder << 8) | (head << 4) | physical;
    if (cached_physical != want)
    {
        if (!readPhysicalSector(cylinder, head, physical))
            return false;
        cached_physical = want;
    }

    std::memcpy(sector_buffer, physical_sector + (half * 256), sizeof(sector_buffer));

    // Block number is the same linear count a .d81 would give.
    this->block = ((uint32_t)(track - 1) * 40) + sector;
    this->track = track;
    this->sector = sector;
    sector_pos = offset;

    return true;
}

uint32_t G81MStream::readContainer(uint8_t *buf, uint32_t size)
{
    // The block is already decoded in RAM, so the cursor is sector_pos and NOT
    // _position - that is the position in the FILE being read, and readFile()
    // calls this repeatedly between seeks. See the same note in g64.cpp.
    if (sector_pos >= block_size)
        return 0;

    uint32_t remaining = (uint32_t)block_size - sector_pos;
    if (size > remaining)
        size = remaining;

    std::memcpy(buf, sector_buffer + sector_pos, size);
    sector_pos += size;
    return size;
}
