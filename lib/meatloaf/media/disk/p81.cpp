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

#include "p81.h"

#include <cstring>

/********************************************************
 * P81MStream
 ********************************************************/

P81MStream::P81MStream(std::shared_ptr<MStream> is) : P64MStream(is)
{
    // 1581 geometry, the same layout D81MStream sets up. It is repeated here
    // rather than inherited because the inheritance chain runs through
    // P64MStream for the container and pulse decoding, which is the half that
    // is genuinely shared; D81MStream shares only this block.
    std::vector<BlockAllocationMap> b = {
        {
            40,     // track
            1,      // sector
            0x10,   // offset
            1,      // start_track
            40,     // end_track
            6       // byte_count
        },
        {
            40,     // track
            2,      // sector (40/1 = BAM side 1, 40/2 = BAM side 2; 40/0 is the header)
            0x10,   // offset
            41,     // start_track
            80,     // end_track
            6       // byte_count
        }
    };

    Partition p = {
        40,    // header_track
        0,     // header_sector
        0x04,  // header_offset
        40,    // directory_track
        3,     // directory_sector
        0x00,  // directory_offset
        0,     // parent_header_track
        0,     // parent_header_sector
        0,     // parent_entry_track
        0,     // parent_entry_sector
        0,     // parent_entry_offset
        b      // block_allocation_map
    };
    partitions.clear();
    partitions.push_back(p);
    sectorsPerTrack = { 40 };
    interleave = { 1, 1 }; // Directory, File
    has_subdirs = true;
    dos_rom = "dos1581";
    dos_version = 0x44; // 'D' - CBM DOS 3.0 (1581)
}


/********************************************************
 * Flux to MFM cells
 ********************************************************/

void P81MStream::resetEmitState(uint8_t track)
{
    (void)track;

    // MFM has no PLL state worth carrying: a cell is a fixed 2 us, so the gap
    // between two transitions IS the cell count. Everything the 1541 logic
    // needs - clock, counter, flip flop - is unused here.
    emit_last_position = 0;
    emit_bit_pos = 0;

    cached_physical = -1;
}

void P81MStream::emitDelta(uint32_t delta)
{
    // A flux transition marks a cell containing a 1; the gap to the previous
    // transition is 2, 3 or 4 cells, which becomes 10, 100 or 1000. Rounding to
    // the nearest cell is the whole of the data separator - real drives run a
    // PLL, but a P64 records ideal-case positions at 16 MHz and the measured
    // spacings on a real image sit within a quarter cell of exact.
    uint32_t cells = (delta + (P81_CELL_SAMPLES / 2)) / P81_CELL_SAMPLES;
    if (cells < 1)
        cells = 1;

    // The zeros need no writing - the buffer starts cleared - so only the
    // transition itself is stored.
    if (emit_bit_pos + cells > emit_target_bits)
    {
        emit_bit_pos = emit_target_bits;
        return;
    }

    emit_bit_pos += cells - 1;
    gcr_track[emit_bit_pos >> 3] |= (uint8_t)(0x80 >> (emit_bit_pos & 7));
    emit_bit_pos++;
}


/********************************************************
 * MFM bitstream reading
 ********************************************************/

int P81MStream::findMfmSync(int p, int limit) const
{
    if (gcr_track_bytes == 0)
        return -1;

    const int cells = (int)gcr_track_bytes * 8;
    if (p < 0 || p >= cells)
        p = 0;

    // A rolling 16-cell window compared against the raw pattern of an $A1 whose
    // clock bit between bits 4 and 3 is deliberately missing. That pattern
    // cannot occur in encoded data, which is exactly why it is the sync.
    uint32_t window = 0;
    int filled = 0;

    while (limit-- > 0)
    {
        uint32_t bit = (gcr_track[p >> 3] >> (7 - (p & 7))) & 1;
        window = ((window << 1) | bit) & 0xffff;

        p++;
        if (p >= cells)
            p = 0;

        if (++filled >= 16 && window == P81_SYNC_PATTERN)
            return p;
    }

    return -1;
}

bool P81MStream::readMfmBytes(int p, uint8_t *buf, int count) const
{
    if (gcr_track_bytes == 0)
        return false;

    const int cells = (int)gcr_track_bytes * 8;

    for (int i = 0; i < count; i++)
    {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; bit++)
        {
            // Cells alternate clock, data, clock, data - the data bit is the
            // second of each pair.
            int index = p + (i * 16) + (bit * 2) + 1;
            if (index >= cells)
                index -= cells;
            value = (uint8_t)((value << 1) | ((gcr_track[index >> 3] >> (7 - (index & 7))) & 1));
        }
        buf[i] = value;
    }

    return true;
}

// CRC-16/CCITT, the one a WD177x computes over the address mark bytes and the
// payload that follows them.
static uint16_t mfm_crc16(const uint8_t *data, uint32_t length, uint16_t crc = 0xffff)
{
    while (length--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}


/********************************************************
 * Sector access
 ********************************************************/

bool P81MStream::loadSector(uint8_t track, uint8_t sector)
{
    // A CBM 1581 block is half of a physical MFM sector. Logical sectors 0-19
    // are head 0, 20-39 are head 1; within a head, two consecutive blocks share
    // one 512-byte sector.
    const uint8_t cylinder = (uint8_t)(track - 1);
    const uint8_t head = (uint8_t)(sector / 20);
    const uint8_t physical = (uint8_t)(((sector % 20) / 2) + 1);
    const uint8_t half = (uint8_t)(sector % 2);

    // The P64 side bit is INVERTED relative to the head the address marks
    // report: side 0 of the image carries head 1. Verified on td1581.p81, where
    // every chunk on side 0 reports H=1 and every chunk on side 1 reports H=0.
    const uint8_t side = (uint8_t)(1 - head);
    const uint8_t key = (uint8_t)((side << 7) | (cylinder + P81_FIRST_CYLINDER_HT));

    // One cache id per (cylinder, head) - the two heads of a cylinder are two
    // different chunks and must not share a decoded buffer.
    const int cache_id = (cylinder * 2) + head;

    if (cached_track != cache_id)
    {
        if (!decodeChunk(key, cache_id))
            return false;
        cached_physical = -1;
    }

    const int want = (cylinder << 8) | (head << 4) | physical;
    if (cached_physical != want)
    {
        if (!readPhysicalSector(cylinder, head, physical))
            return false;
        cached_physical = want;
    }

    std::memcpy(sector_buffer, physical_sector + (half * 256), sizeof(sector_buffer));
    return true;
}

bool P81MStream::readPhysicalSector(uint8_t cylinder, uint8_t head, uint8_t physical)
{
    const int cells = (int)gcr_track_bytes * 8;

    int p = 0;
    int first = -1;

    last_data_checksum_ok = true;

    for (int guard = 0; guard < 256; guard++)
    {
        p = findMfmSync(p, cells);
        if (p < 0)
            break;
        if (p == first)
            break;  // all the way round
        if (first < 0)
            first = p;

        // An address mark is three $A1 syncs then the mark byte. findMfmSync
        // stopped after the first, so step over any that follow.
        int marks = 1;
        while (true)
        {
            int next = findMfmSync(p, 16);
            if (next != p + 16)
                break;
            p = next;
            marks++;
        }

        uint8_t mark = 0;
        if (!readMfmBytes(p, &mark, 1))
            break;

        if (mark != P81_MARK_ID || marks < 3)
            continue;

        // FE, cylinder, head, sector, size code, then the CRC of all of it
        // including the three $A1 bytes the controller counts but does not
        // store as data.
        uint8_t id[7];
        if (!readMfmBytes(p, id, sizeof(id)))
            break;

        uint8_t crc_input[3 + 5] = { 0xa1, 0xa1, 0xa1, id[0], id[1], id[2], id[3], id[4] };
        uint16_t crc = mfm_crc16(crc_input, sizeof(crc_input));
        if (crc != (uint16_t)((id[5] << 8) | id[6]))
            continue;   // false sync, or a header this drive cannot read

        if (id[1] != cylinder || id[2] != head || id[3] != physical)
        {
            p = p + (7 * 16);
            continue;
        }

        // The data mark follows within a header gap.
        int d = findMfmSync(p + (7 * 16), 100 * 16);
        if (d < 0)
        {
            Debug_printv("no data mark for C%d H%d R%d", cylinder, head, physical);
            return false;
        }
        while (true)
        {
            int next = findMfmSync(d, 16);
            if (next != d + 16)
                break;
            d = next;
        }

        uint8_t data_mark = 0;
        if (!readMfmBytes(d, &data_mark, 1))
            return false;
        if (data_mark != P81_MARK_DATA && data_mark != P81_MARK_DELETED_DATA)
        {
            Debug_printv("C%d H%d R%d data mark is [%02X]", cylinder, head, physical, data_mark);
            return false;
        }

        // Mark, 512 data bytes, then the CRC.
        uint8_t tail[3] = { 0 };
        if (!readMfmBytes(d + 16, physical_sector, P81_SECTOR_BYTES))
            return false;
        if (!readMfmBytes(d + 16 + (P81_SECTOR_BYTES * 16), tail, 2))
            return false;

        uint16_t running = mfm_crc16((const uint8_t *)"\xa1\xa1\xa1", 3);
        running = mfm_crc16(&data_mark, 1, running);
        running = mfm_crc16(physical_sector, P81_SECTOR_BYTES, running);
        if (running != (uint16_t)((tail[0] << 8) | tail[1]))
        {
            // Reported, not refused, for the same reason the GCR path reports a
            // bad checksum: the bytes are the drive's best read and the caller
            // has no error channel to hear about it on.
            Debug_printv("C%d H%d R%d data CRC error", cylinder, head, physical);
            last_data_checksum_ok = false;
        }

        return true;
    }

    Debug_printv("C%d H%d R%d not found", cylinder, head, physical);
    return false;
}

bool P81MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
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

    if (!parseChunks())
        return false;

    if (!loadSector(track, sector))
        return false;

    // Block number is the same linear count a .d81 would give.
    uint32_t sectorOffset = ((uint32_t)(track - 1) * 40) + sector;

    this->block = sectorOffset;
    this->track = track;
    this->sector = sector;
    sector_pos = offset;

    return true;
}
