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

#include "mfm.h"

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
    bool crc_ok = true;
    bool found = mfm::readSector(gcr_track.data(), gcr_track_bytes,
                                 cylinder, head, physical,
                                 physical_sector, P81_SECTOR_BYTES, &crc_ok);
    last_data_checksum_ok = crc_ok;
    return found;
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
