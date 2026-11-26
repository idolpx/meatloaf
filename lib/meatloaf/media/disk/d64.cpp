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

#include "d64.h"

#include <fstream>
#include <new>
#include <iostream>

//#include "meat_broker.h"
#include "meat_media.h"
#include "endianness.h"

#include "../../../../include/cbm_defines.h"   // ST_* DOS status codes

// D64 Utility Functions

bool D64MStream::seekBlock(uint64_t index, uint8_t offset)
{
    uint16_t sectorOffset = 0;
    uint8_t track = 0;

    // Debug_printv("track[%d] sector[%d] offset[%d]", track, sector, offset);

    // Determine actual track & sector from index
    do
    {
        track++;
        uint8_t count = getSectorCount(track);
        if (sectorOffset + count < index)
            sectorOffset += count;
        else
            break;

        // Debug_printv("track[%d] speedZone[%d] secotorsPerTrack[%d] sectorOffset[%d]", track, speedZone(track), count, sectorOffset);
    } while (true);
    uint8_t sector = index - sectorOffset;

    this->block = index;
    this->track = track;
    this->sector = sector;

    // Debug_printv("track[%d] sector[%d] speedZone[%d] sectorOffset[%d]", track, sector, speedZone(track), sectorOffset);

    return containerStream->seek((index * block_size) + offset);
}

int32_t D64MStream::sectorByteOffset(uint8_t track, uint8_t sector)
{
    int32_t block_index = linearBlock(track, sector);
    if (block_index < 0)
        return -1;

    return block_index * (int32_t) block_size;
}


// Linear block index of a track/sector, or -1 when it is not on this medium.
// Shared by seekSector() and sectorByteOffset() so the geometry walk exists
// once -- they disagreed about nothing today, and that is worth keeping.
int32_t D64MStream::linearBlock(uint8_t track, uint8_t sector)
{
    uint16_t sectorOffset = 0;

    // Is this a valid track?
    uint16_t c = curPartition().block_allocation_map.size() - 1;
    uint8_t start_track = curPartition().block_allocation_map[0].start_track;
    uint8_t end_track = curPartition().block_allocation_map[c].end_track;
    if (track < start_track || track > end_track)
        return -1;

    // Is this a valid sector?
    if (sector > getSectorCount(track))
        return -1;

    if (dos_version != 0xFF) track--; // D9060/D9090: Track 1 is at index 0, not 1
    for (uint8_t index = 0; index < track; ++index)
        sectorOffset += getSectorCount(index + 1);
    if (dos_version != 0xFF) track++;

    return (int32_t) (sectorOffset + sector);
}


bool D64MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
{
    uint16_t sectorOffset = 0;

    //Debug_printv("track[%d] sector[%d] offset[%d]", track, sector, offset);

    // Is this a valid track?
    uint16_t c = curPartition().block_allocation_map.size() - 1;
    uint8_t start_track = curPartition().block_allocation_map[0].start_track;
    uint8_t end_track = curPartition().block_allocation_map[c].end_track;
    if (track < start_track || track > end_track)
    {
        Debug_printv("Invalid Track: track[%d] start_track[%d] end_track[%d]", track, start_track, end_track);
        return false;
    }

    // Is this a valid sector?
    c = getSectorCount(track);
    if (sector > c)
    {
        Debug_printv("Invalid Sector: sector[%d] sectorsPerTrack[%d]", sector, c);
        return false;
    }

    // Check for error info
    if (error_info)
    {
        // Look up error for this track/sector
    }

    if (dos_version != 0xFF) track--; // D9060/D9090: Track 1 is at index 0, not 1
    for (uint8_t index = 0; index < track; ++index)
    {
        sectorOffset += getSectorCount(index + 1);
        //Debug_printv("track[%d] speedZone[%d] secotorsPerTrack[%d] sectorOffset[%d]", (index + 1), speedZone(index), getSectorCount(index + 1), sectorOffset);
    }
    if (dos_version != 0xFF) track++;
    sectorOffset += sector;

    this->block = sectorOffset;
    this->track = track;
    this->sector = sector;

    //Debug_printv("track[%d] sector[%d] speedZone[%d] sectorOffset[%d]", track, sector, speedZone(track), sectorOffset);

    return containerStream->seek((sectorOffset * block_size) + offset);
}

bool D64MStream::seekSector(std::vector<uint8_t> trackSectorOffset)
{
    return seekSector(trackSectorOffset[0], trackSectorOffset[1], trackSectorOffset[2]);
}

std::string D64MStream::readBlock(uint8_t track, uint8_t sector)
{
    if (!seekSector(track, sector, 0))
        return "";

    uint8_t buffer[block_size];
    uint32_t bytesRead = readContainer(buffer, block_size);

    if (bytesRead != block_size)
    {
        Debug_printv("Failed to read full block: track[%d] sector[%d] bytesRead[%d]", track, sector, bytesRead);
        return "";
    }

    return std::string((char*)buffer, block_size);
}

bool D64MStream::writeBlock(uint8_t track, uint8_t sector, std::string data)
{
    if (!seekSector(track, sector, 0))
        return false;

    // Ensure data is exactly block_size, pad with zeros if needed
    if (data.size() < block_size)
        data.resize(block_size, 0x00);

    uint32_t bytesWritten = writeContainer((uint8_t*)data.c_str(), block_size);

    if (bytesWritten != block_size)
    {
        Debug_printv("Failed to write full block: track[%d] sector[%d] bytesWritten[%d]", track, sector, bytesWritten);
        return false;
    }

    return true;
}

// Locate the BAM record for a track using the partition's block allocation
// map(s), so multi-BAM formats (D71 second side, D81 side 2) work too.
bool D64MStream::getBAMRecord(uint8_t track, BAMRecord *rec)
{
    for (auto &bam : curPartition().block_allocation_map)
    {
        if (track >= bam.start_track && track <= bam.end_track)
        {
            uint16_t offset = bam.offset + (uint16_t)(track - bam.start_track) * bam.byte_count;
            rec->bam_track = bam.track;
            // Contiguous multi-sector BAMs: advance to the sector holding this record
            rec->bam_sector = bam.sector + (offset / block_size);
            rec->offset = offset % block_size;
            rec->byte_count = bam.byte_count;
            // Records with more bytes than the bitmap needs carry a leading
            // free-sector count (D64/D81); bitmap-only records don't (D71 side 2)
            uint8_t bitmap_bytes = (getSectorCount(track) + 7) / 8;
            rec->has_count = bam.byte_count > bitmap_bytes;
            return true;
        }
    }
    Debug_printv("No BAM record for track[%d]", track);
    return false;
}

bool D64MStream::readBAMRecord(uint8_t track, BAMRecord *rec, uint8_t *buf)
{
    if (!getBAMRecord(track, rec))
        return false;
    if (!seekSector(rec->bam_track, rec->bam_sector, rec->offset))
        return false;
    return readContainer(buf, rec->byte_count) == rec->byte_count;
}

bool D64MStream::bamIndex(const BAMRecord &rec, uint8_t sector, uint8_t *byte_index, int *count_index)
{
    uint8_t grp = bamGroupSectors();
    unsigned bi;
    if (grp)
    {
        unsigned stride = grp / 8 + 1;              // count byte + bitmap
        unsigned g = sector / grp;
        *count_index = (int)(g * stride);
        bi = g * stride + 1 + ((sector % grp) >> 3);
    }
    else
    {
        *count_index = rec.has_count ? 0 : -1;
        bi = (rec.has_count ? 1 : 0) + (sector >> 3);
    }
    if (bi >= rec.byte_count)
        return false; // sector beyond what this BAM record can represent
    *byte_index = (uint8_t)bi;
    return true;
}

bool D64MStream::setBlockAllocation(uint8_t track, uint8_t sector, bool allocate)
{
    BAMRecord rec;
    uint8_t buf[32]; // largest byte_count in use is 32 (DNP/DHD native: 256 sectors/track; D9090: 6 x 5)
    if (!readBAMRecord(track, &rec, buf))
        return false;

    uint8_t byte_index;
    int count_index;
    if (!bamIndex(rec, sector, &byte_index, &count_index))
        return false;

    uint8_t bitmask = bamBitMask(sector);
    if (allocate)
    {
        if ((buf[byte_index] & bitmask) == 0)
        {
            Debug_printv("Block already allocated: track[%d] sector[%d]", track, sector);
            return false;
        }
        buf[byte_index] &= ~bitmask; // 0 = allocated
        if (count_index >= 0 && buf[count_index] > 0)
            buf[count_index]--;
    }
    else
    {
        if ((buf[byte_index] & bitmask) == bitmask)
        {
            Debug_printv("Block already free: track[%d] sector[%d]", track, sector);
            //return false; Don't fail if the block is already free - just leave it as is
            return true;
        }
        buf[byte_index] |= bitmask; // 1 = free
        if (count_index >= 0)
            buf[count_index]++;
    }

    if (!seekSector(rec.bam_track, rec.bam_sector, rec.offset))
        return false;
    return writeContainer(buf, rec.byte_count) == rec.byte_count;
}

bool D64MStream::allocateBlock(uint8_t track, uint8_t sector)
{
    return setBlockAllocation(track, sector, true);
}

bool D64MStream::deallocateBlock(uint8_t track, uint8_t sector)
{
    return setBlockAllocation(track, sector, false);
}

bool D64MStream::validateBAM()
{
    // TODO: Walk each files blocks to ensure the BAM accurately reflects the allocation.
    // Correct the BAM if it is not accurate.
    // Make safe for REL and VLIR files.
    return false;
}

// Returns uint16_t, not uint8_t: a CMD native track (DNP, DHD) has 256 sectors,
// so a fully free one counts 256 - which truncates to 0 in a byte. That made
// blocksFree() report 0 and, worse, made getNextFreeBlock() treat every track as
// full so no write could allocate anything.
uint16_t D64MStream::getTrackFreeCount(uint8_t track)
{
    BAMRecord rec;
    uint8_t buf[32];
    if (!readBAMRecord(track, &rec, buf))
        return 0;

    uint8_t grp = bamGroupSectors();
    if (grp)
    {
        // One count per group of 'grp' sectors
        unsigned stride = grp / 8 + 1;
        uint16_t count = 0;
        for (unsigned g = 0; g * grp < getSectorCount(track) && g * stride < rec.byte_count; g++)
            count += buf[g * stride];
        return count;
    }

    if (rec.has_count)
        return buf[0];

    // Bitmap-only record: count the free (1) bits
    uint16_t count = 0;
    for (uint8_t i = 0; i < rec.byte_count; i++)
        count += std::bitset<8>(buf[i]).count();
    return count;
}

// Scan a track's bitmap for a free sector, starting at startSector and
// wrapping around, so the caller's interleave offset is honored.
bool D64MStream::findFreeSectorOnTrack(uint8_t track, uint8_t startSector, uint8_t *foundSector)
{
    BAMRecord rec;
    uint8_t buf[32];
    if (!readBAMRecord(track, &rec, buf))
        return false;

    uint16_t spt = getSectorCount(track);
    for (uint16_t i = 0; i < spt; i++)
    {
        uint8_t s = (startSector + i) % spt;
        uint8_t byte_index;
        int count_index;
        if (!bamIndex(rec, s, &byte_index, &count_index))
            continue; // beyond bitmap coverage - treat as allocated
        if (buf[byte_index] & bamBitMask(s))
        {
            *foundSector = s;
            return true;
        }
    }
    return false;
}

bool D64MStream::initializeBlocks()
{
    Debug_printv("initialize blocks");

    // 1541/1571 Fill Pattern: 0x4B followed by 0x01 bytes
    // This fills all blocks with the standard CBM DOS empty pattern

    uint8_t fill_block[block_size];
    fill_block[0] = 0x4B;  // First byte is 0x4B
    memset(&fill_block[1], 0x01, block_size - 1);  // Rest is 0x01

    // Fill all blocks in all tracks
    for (uint8_t t = 1; t <= getTrackCount(); t++)
    {
        uint8_t sectors = getSectorCount(t);
        for (uint8_t s = 0; s < sectors; s++)
        {
            // Skip the directory track (track 18) - it will be initialized separately
            if (t == curPartition().header_track)
                continue;

            if (!seekSector(t, s, 0))
            {
                Debug_printv("Failed to seek track[%d] sector[%d]", t, s);
                return false;
            }

            if (writeContainer(fill_block, block_size) != block_size)
            {
                Debug_printv("Failed to write block track[%d] sector[%d]", t, s);
                return false;
            }
        }
    }

    return true;
}

D64MStream::BlockChain D64MStream::getFreeBlocks(uint16_t file_size)
{
    // Return a vector of <track, sector> offsets for the required file size
    BlockChain blockChain;

    uint16_t remainingSize = file_size;
    uint8_t track = 1;
    uint8_t sector = 0;

    while (remainingSize > 0)
    {
        uint8_t foundTrack, foundSector;
        if (!getNextFreeBlock(track, sector, &foundTrack, &foundSector))
        {
            break; // No more free blocks available
        }

        Block block;
        block.track = foundTrack;
        block.sector = foundSector;
        blockChain.push_back(block);

        remainingSize -= (block_size - 2); // Adjust for t/s link
        track = foundTrack;
        sector = foundSector + 1;
    }

    return blockChain;
}

// Pick the next block for a file the way CBM DOS does on a physical drive:
// - Directory blocks stay on the directory track, stepped by the directory
//   interleave from the previous directory sector.
// - The first block of a file goes on the track with free space closest to
//   the directory track (head travel is shortest near the directory).
// - Subsequent blocks stay on the same track, offset by the file interleave;
//   when the track fills up, move one track further away from the directory,
//   and when that side of the disk is exhausted, switch to the other side.
// startTrack == 0 (files only) means "first block of a new file".
// Searches the tracks that exist right now. getNextFreeBlock() wraps this with
// a grow-and-retry for media that can be extended.
bool D64MStream::findFreeBlock(uint8_t startTrack, uint8_t startSector, uint8_t *foundTrack, uint8_t *foundSector, bool forDirectory)
{
    uint8_t dir_track = curPartition().directory_track;
    uint8_t first_track = curPartition().block_allocation_map.front().start_track;
    uint8_t last_track = curPartition().block_allocation_map.back().end_track;

    if (forDirectory)
    {
        // Directory blocks stay on the track the directory lives on
        // (the root directory track, or a subdirectory's current track)
        uint8_t t = startTrack ? startTrack : dir_track;
        uint8_t start = (startSector + interleave[0]) % getSectorCount(t);
        if (findFreeSectorOnTrack(t, start, foundSector))
        {
            *foundTrack = t;
            return true;
        }
        // A dedicated directory track is a hard limit. Without one the
        // directory is just a chain, so a new block may go anywhere and the
        // search below applies - the caller links it to the previous block.
        if (dedicated_directory_track)
            return false;
    }

    if (startTrack == 0)
    {
        // First block: closest track to the directory track with a free sector.
        // Distance 0 - the directory track itself - is only a candidate when the
        // format does not dedicate that track to the directory. Skipping it
        // unconditionally left a 1-track CMD native partition with nowhere to
        // put a first block, so every save "filled the disk" immediately.
        if (!dedicated_directory_track &&
            getTrackFreeCount(dir_track) && findFreeSectorOnTrack(dir_track, 0, foundSector))
        {
            *foundTrack = dir_track;
            return true;
        }

        for (uint16_t d = 1; ; d++)
        {
            int below = (int)dir_track - d;
            int above = (int)dir_track + d;
            bool in_range = false;
            if (below >= first_track)
            {
                in_range = true;
                if (getTrackFreeCount(below) && findFreeSectorOnTrack(below, 0, foundSector))
                {
                    *foundTrack = below;
                    return true;
                }
            }
            if (above <= last_track)
            {
                in_range = true;
                if (getTrackFreeCount(above) && findFreeSectorOnTrack(above, 0, foundSector))
                {
                    *foundTrack = above;
                    return true;
                }
            }
            if (!in_range)
                return false; // no free blocks anywhere
        }
    }

    // Subsequent block: try the same track first, using the file interleave
    uint8_t start = (startSector + interleave[1]) % getSectorCount(startTrack);
    if (findFreeSectorOnTrack(startTrack, start, foundSector))
    {
        *foundTrack = startTrack;
        return true;
    }

    // Track is full: keep moving away from the directory track...
    int step = (startTrack < dir_track) ? -1 : 1;
    for (int t = (int)startTrack + step; t >= first_track && t <= last_track; t += step)
    {
        if (dedicated_directory_track && t == dir_track)
            continue;
        if (getTrackFreeCount(t) && findFreeSectorOnTrack(t, 0, foundSector))
        {
            *foundTrack = t;
            return true;
        }
    }
    // ...then try the other side of the directory
    for (int t = (int)dir_track - step; t >= first_track && t <= last_track; t -= step)
    {
        if (dedicated_directory_track && t == dir_track)
            continue;
        if (getTrackFreeCount(t) && findFreeSectorOnTrack(t, 0, foundSector))
        {
            *foundTrack = t;
            return true;
        }
    }

    // Last resort: sweep every track (skipping the directory) so the save
    // only fails when the disk is truly full - blocks freed by scratched
    // files between the file's current track and the directory would
    // otherwise be missed.
    for (int t = first_track; t <= last_track; t++)
    {
        if (dedicated_directory_track && t == dir_track)
            continue;
        if (getTrackFreeCount(t) && findFreeSectorOnTrack(t, 0, foundSector))
        {
            *foundTrack = t;
            return true;
        }
    }

    return false;
}

bool D64MStream::getNextFreeBlock(uint8_t startTrack, uint8_t startSector, uint8_t *foundTrack, uint8_t *foundSector, bool forDirectory)
{
    if (findFreeBlock(startTrack, startSector, foundTrack, foundSector, forDirectory))
        return true;

    // Nothing free in the tracks that exist. Media that can be extended get one
    // more track and a second look; for everything else growImage() refuses and
    // this is a genuine DISK FULL.
    if (!growImage())
        return false;

    return findFreeBlock(startTrack, startSector, foundTrack, foundSector, forDirectory);
}

bool D64MStream::isBlockFree(uint8_t track, uint8_t sector)
{
    BAMRecord rec;
    uint8_t buf[32];
    if (!readBAMRecord(track, &rec, buf))
        return false;

    uint8_t byte_index;
    int count_index;
    if (!bamIndex(rec, sector, &byte_index, &count_index))
        return false;

    // Check if bit is set (1 = free, 0 = allocated)
    uint8_t bitmask = bamBitMask(sector);
    return (buf[byte_index] & bitmask) == bitmask;
}

bool D64MStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if (filename.size())
    {
        uint16_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (seekEntry(index))
        {
            if (entry.file_type == 0x00) // Skip deleted/never-used entries
            {
                index++;
                continue;
            }

            if (wildcard && !(entry.file_type & 0b00000111)) // Skip non-PRG files
            {
                index++;
                continue;
            }

            std::string entryFilename = entry.filename;
            size_t i = entryFilename.find_first_of(0xA0);
            if (i == std::string::npos || i > 16) i = 16;
            entryFilename = entryFilename.substr(0, i);
            entryFilename = mstr::toUTF8(entryFilename);

            //Debug_printv("index[%d] track[%d] sector[%d] filename[%s] entry.filename[%.16s]", index, track, sector, filename.c_str(), entryFilename.c_str());
            //Debug_printv("filename[%s] entry[%s]", filename.c_str(), entryFilename.c_str());

            if ( mstr::compareFilename(entryFilename, filename, wildcard) )
            {
                return true;
            }

            index++;
        }

        Debug_printv("File not found!");
    }

    entry.next_track = 0;
    entry.next_sector = 0;
    entry.blocks = 0;
    entry.filename[0] = '\0';

    return false;
}

bool D64MStream::seekEntry( uint16_t index )
{
    // Current directory defaults to the partition's root directory
    if (dir_track == 0)
    {
        dir_track = curPartition().directory_track;
        dir_sector = curPartition().directory_sector;
    }

    // Calculate Sector offset & Entry offset
    // 8 Entries Per Sector, 32 bytes Per Entry
    index--;
    uint16_t sectorOffset = index / 8;
    uint16_t entryOffset = (index % 8) * 32;

    //Debug_printv("----------");
    //Debug_printv("index[%d] sectorOffset[%d] entryOffset[%d] entry_index[%d]", index, sectorOffset, entryOffset, entry_index);

    // Set once the entry we want has already been read into `entry`, so the
    // unconditional readContainer() below is skipped instead of re-seeking to
    // the exact same offset and reading it a second time.
    bool haveEntry = false;

    if (index == 0 || index != entry_index)
    {
        // Start at first sector of directory
        next_track = 0;
        if (!seekSector(
                dir_track,
                dir_sector,
                curPartition().directory_offset))
            return false;

        // Find sector with requested entry
        do
        {
            if (next_track)
            {
                Debug_printv("next_track[%d] next_sector[%d]", entry.next_track, entry.next_sector);
                if (!seekSector(entry.next_track, entry.next_sector))
                    return false;
            }

            readContainer((uint8_t *)&entry, sizeof(entry));
            next_track = entry.next_track;
            next_sector = entry.next_sector;

            Debug_printv("sectorOffset[%d] -> track[%d] sector[%d]", sectorOffset, track, sector);

        } while (sectorOffset-- > 0);

        if (entryOffset == 0)
        {
            // The loop above just landed on and read entry 0 of the target
            // sector -- which IS the entry being sought. Re-seeking to that
            // same offset and reading it again was pure duplication: a
            // second seek()/read() round trip over the exact same bytes,
            // and on a reused HTTP connection that extra round trip is what
            // came back with corrupted/blank data instead of a cache hit.
            haveEntry = true;
        }
        else if (!seekSector(track, sector, entryOffset))
        {
            return false;
        }
    }
    else
    {
        if (entryOffset == 0)
        {
            if (next_track == 0)
                return false;

            //Debug_printv("Follow link track[%d] sector[%d] entryOffset[%d]", next_track, next_sector, entryOffset);
            if (!seekSector(next_track, next_sector, entryOffset))
                return false;
        }
    }

    if (!haveEntry)
        readContainer((uint8_t *)&entry, sizeof(entry));

    // If we are at the first entry in the sector then get next_track/next_sector
    if (entryOffset == 0)
    {
        next_track = entry.next_track;
        next_sector = entry.next_sector;
    }

    std::string e = mstr::toHex((uint8_t *)&entry, sizeof(entry));
    //Debug_printv("file_type[%02X] file_name[%.16s] entry[%s]", entry.file_type, entry.filename, e.c_str());

    // if ( next_track == 0 && next_sector == 0xFF )
    entry_index = index + 1;

    // if (entry.file_type == 0x00 || entry.file_type == 0xFF)
    //     return false;

    return true;
}

bool D64MStream::readEntry( uint16_t index ) {
    return seekEntry(index);
}
bool D64MStream::writeEntry( uint16_t index) {
    if ( seekEntry(index - 1) ) {
        return writeContainer((uint8_t*)&entry, sizeof(entry));
    }
    return false;
}

// --- Streamed new-file write (SAVE into the image) ---------------------
// Files are streamed, so the size is unknown up front: one block is claimed
// at a time, each full block is written with its T/S link as soon as the
// next block is known, and the directory entry is added at close() when the
// final block (and total block count) is known.

bool D64MStream::beginFileWrite(std::string filename)
{
    uint8_t t, s;
    if (!getNextFreeBlock(0, 0, &t, &s) || !allocateBlock(t, s))
    {
        // No free block for even the first block: keep the stream usable as
        // an error carrier so the save completes on the wire and the error
        // is reported on the drive status channel after close.
        Debug_printv("Disk full, cannot create [%s]", filename.c_str());
        _error = ST_DISK_FULL;
        return true;
    }

    creating = true;
    create_filename = filename;
    create_allocated.clear();
    create_allocated.push_back({t, s});
    create_start_track = t;
    create_start_sector = s;
    create_track = t;
    create_sector = s;
    cbuf_len = 0;
    _size = 0;
    _position = 0;

    Debug_printv("Creating [%s] first block track[%d] sector[%d]", filename.c_str(), t, s);
    return true;
}

uint32_t D64MStream::writeFileNew(uint8_t *buf, uint32_t size)
{
    // After a failure the remaining bytes are swallowed so the IEC transfer
    // finishes cleanly; the error is reported when the channel is closed.
    if (_error)
        return size;

    uint32_t written = 0;
    while (written < size)
    {
        if (cbuf_len == (block_size - 2))
        {
            // Current block is full and more data is coming: claim the next
            // block so this block's T/S link can be written.
            uint8_t nt, ns;
            if (!getNextFreeBlock(create_track, create_sector, &nt, &ns) || !allocateBlock(nt, ns))
            {
                Debug_printv("Disk full writing [%s] after %d blocks", create_filename.c_str(), create_allocated.size());
                rollbackFileWrite();
                _error = ST_DISK_FULL;
                return size;
            }

            std::string block(block_size, '\0');
            block[0] = nt;
            block[1] = ns;
            memcpy(&block[2], cbuf, block_size - 2);
            if (!writeBlock(create_track, create_sector, block))
            {
                rollbackFileWrite();
                _error = ST_WRITE_VERIFY;
                return size;
            }

            create_allocated.push_back({nt, ns});
            create_track = nt;
            create_sector = ns;
            cbuf_len = 0;
        }

        uint32_t n = std::min(size - written, (uint32_t)(block_size - 2 - cbuf_len));
        memcpy(cbuf + cbuf_len, buf + written, n);
        cbuf_len += n;
        written += n;
    }

    return size;
}

bool D64MStream::finalizeFileWrite()
{
    creating = false;

    // Write the final block: link track 0, "sector" = offset of last used byte
    std::string block(block_size, '\0');
    block[0] = 0;
    block[1] = (uint8_t)(cbuf_len + 1);
    memcpy(&block[2], cbuf, cbuf_len);
    if (!writeBlock(create_track, create_sector, block))
    {
        rollbackFileWrite();
        _error = ST_WRITE_VERIFY;
        return false;
    }

    uint8_t dt, ds, slot;
    uint16_t blocks = create_allocated.size();
    uint8_t st = addDirectoryEntry(create_filename, 0x82, // closed PRG
                                   create_start_track, create_start_sector,
                                   0, 0, 0, blocks, &dt, &ds, &slot);
    if (st != 0)
    {
        rollbackFileWrite();
        _error = st;
        return false;
    }

    Debug_printv("Created [%s] start[%d/%d] blocks[%d] entry at [%d/%d] slot[%d]",
                 create_filename.c_str(), create_start_track, create_start_sector, blocks, dt, ds, slot);
    return true;
}

uint8_t D64MStream::addDirectoryEntry(const std::string& filename, uint8_t type,
                                      uint8_t start_track, uint8_t start_sector,
                                      uint8_t rel_track, uint8_t rel_sector, uint8_t rel_length,
                                      uint16_t blocks,
                                      uint8_t* out_track, uint8_t* out_sector, uint8_t* out_slot)
{
    // Find the first free directory entry, following the chain of the
    // directory the file was created in
    uint8_t dt = dir_track ? dir_track : curPartition().directory_track;
    uint8_t ds = dir_track ? dir_sector : curPartition().directory_sector;
    std::string dirsec;
    int slot = -1;
    uint16_t chain_safety = 0;
    while (true)
    {
        dirsec = readBlock(dt, ds);
        if (dirsec.size() != block_size || chain_safety++ > 1000)
            return ST_DIR_ERROR;

        for (uint8_t i = 0; i < 8; i++)
        {
            if ((uint8_t)dirsec[i * 32 + 2] == 0x00)
            {
                slot = i;
                break;
            }
        }
        if (slot >= 0)
            break;

        if ((uint8_t)dirsec[0] != 0)
        {
            // Follow the chain to the next directory sector
            uint8_t nt = (uint8_t)dirsec[0];
            ds = (uint8_t)dirsec[1];
            dt = nt;
            continue;
        }

        // No free entry: extend the directory with a new block on the
        // directory track, honoring the directory interleave
        uint8_t nt, ns;
        if (!getNextFreeBlock(dt, ds, &nt, &ns, true) || !allocateBlock(nt, ns))
        {
            Debug_printv("Directory full, cannot add entry for [%s]", filename.c_str());
            return ST_DISK_FULL;
        }

        // Link the current last directory sector to the new one
        dirsec[0] = nt;
        dirsec[1] = ns;
        if (!writeBlock(dt, ds, dirsec))
        {
            deallocateBlock(nt, ns);
            return ST_WRITE_VERIFY;
        }

        // Fresh directory sector: end-of-chain link, all entries free
        dirsec.assign(block_size, '\0');
        dirsec[0] = 0x00;
        dirsec[1] = 0xFF;
        dt = nt;
        ds = ns;
        slot = 0;
        break;
    }

    // Build the 32-byte entry in place and write the sector back
    uint16_t o = slot * 32;
    std::string name = mstr::toPETSCII2(filename);
    if (name.size() > 16)
        name = name.substr(0, 16);

    dirsec[o + 2] = type;
    dirsec[o + 3] = start_track;
    dirsec[o + 4] = start_sector;
    memset(&dirsec[o + 5], 0xA0, 16);
    memcpy(&dirsec[o + 5], name.c_str(), name.size());
    memset(&dirsec[o + 21], 0x00, 9);
    dirsec[o + 21] = rel_track;
    dirsec[o + 22] = rel_sector;
    dirsec[o + 23] = rel_length;
    dirsec[o + 30] = blocks & 0xFF;
    dirsec[o + 31] = blocks >> 8;

    if (!writeBlock(dt, ds, dirsec))
        return ST_WRITE_VERIFY;

    *out_track = dt;
    *out_sector = ds;
    *out_slot = (uint8_t)slot;
    return 0;
}

void D64MStream::rollbackFileWrite()
{
    // Free every block claimed for this file; no directory entry was written
    for (auto &b : create_allocated)
        deallocateBlock(b.track, b.sector);
    create_allocated.clear();
    creating = false;
}

// Defined further down; unremoveFile() needs it and sits above the definition.
static std::vector<std::string> splitPathComponents(const std::string &path);

bool D64MStream::queryBlockAllocation(uint8_t track, uint8_t sector, bool& out_allocated)
{
    BAMRecord rec;
    uint8_t buf[32];
    if (!readBAMRecord(track, &rec, buf))
        return false;

    uint8_t byte_index;
    int count_index;
    if (!bamIndex(rec, sector, &byte_index, &count_index))
        return false;   // sector beyond what this BAM record can represent

    uint8_t bitmask = bamBitMask(sector);
    out_allocated = (buf[byte_index] & bitmask) == 0;   // 1 = free, 0 = allocated
    return true;
}

bool D64MStream::seekScratchedEntry(std::string filename)
{
    if (filename.empty())
        return false;

    mstr::replaceAll(filename, "\\", "/");
    bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));

    uint16_t index = 1;
    while (seekEntry(index))
    {
        // Only scratched entries are candidates. A never-used slot is also
        // type 0, but carries no name, so an empty name is not a match.
        if (entry.file_type != 0x00)
        {
            index++;
            continue;
        }

        std::string entryFilename = entry.filename;
        size_t i = entryFilename.find_first_of(0xA0);
        if (i == std::string::npos || i > 16) i = 16;
        entryFilename = mstr::toUTF8(entryFilename.substr(0, i));

        if (!entryFilename.empty() &&
            mstr::compareFilename(entryFilename, filename, wildcard))
            return true;

        index++;
    }

    return false;
}

bool D64MStream::unscratchEntry()
{
    uint8_t dir_track = track;
    uint8_t dir_sector = sector;
    uint8_t slot = (entry_index - 1) % 8;

    // Pass 1: walk the chain and prove every block is still free. The links
    // live in the blocks themselves, which are unallocated now, so they may
    // have been overwritten - hence bounding the walk by the block count the
    // entry recorded and bailing out on anything that does not add up.
    //
    // Nothing is modified during this pass. A recovery that cannot complete
    // must leave the disk exactly as it found it, or a failed unscratch would
    // be worse than the delete it was trying to undo.
    std::vector<Block> chain;
    uint8_t t = entry.start_track;
    uint8_t s = entry.start_sector;
    size_t limit = entry.blocks ? (size_t)entry.blocks + 2 : 10000;

    while (t != 0)
    {
        if (chain.size() >= limit)
            return false;   // chain runs longer than the entry claims

        bool allocated = false;
        if (!queryBlockAllocation(t, s, allocated))
            return false;   // not a representable block: garbage link
        if (allocated)
            return false;   // already handed to a newer file - unrecoverable

        chain.push_back({t, s});

        if (!seekSector(t, s))
            return false;
        uint8_t link[2];
        if (readContainer(link, 2) != 2)
            return false;
        t = link[0];
        s = link[1];
    }

    if (chain.empty())
        return false;

    // Pass 2: commit. Reclaim the blocks, then put the type back.
    for (const Block& b : chain)
    {
        if (!allocateBlock(b.track, b.sector))
            return false;
    }

    std::string dirsec = readBlock(dir_track, dir_sector);
    if (dirsec.size() != block_size)
        return false;
    dirsec[slot * 32 + 2] = 0x82;   // PRG, closed
    return writeBlock(dir_track, dir_sector, dirsec);
}

bool D64MStream::unremoveFile(std::string path)
{
    auto parts = splitPathComponents(path);
    if (parts.empty())
        return false;

    std::string parent;
    for (size_t i = 0; i + 1 < parts.size(); i++)
    {
        if (i) parent += '/';
        parent += parts[i];
    }

    if (!seekDirectory(parent))
        return false;
    if (!seekScratchedEntry(parts.back()))
        return false;
    return unscratchEntry();
}

bool D64MStream::removeFile(std::string path)
{
    // resolvePath() leaves track/sector on the entry's DIRECTORY sector, which
    // is what scratchEntry() requires. seekPath() would not: in read mode it
    // goes on to seek the file's first block.
    if (resolvePath(path) != PATH_FILE)
        return false;
    return scratchEntry();
}

// Scratch an existing entry so a SAVE"@:file" can rewrite it: free the block
// chain and mark the entry deleted. Assumes seekEntry() just succeeded, so
// 'entry' holds the file and track/sector point at its directory sector.
bool D64MStream::scratchEntry()
{
    uint8_t dir_track = track;
    uint8_t dir_sector = sector;
    uint8_t slot = (entry_index - 1) % 8;

    // Free the file's block chain
    uint8_t t = entry.start_track;
    uint8_t s = entry.start_sector;
    uint16_t safety = 0;
    while (t != 0 && safety++ < 10000)
    {
        if (!seekSector(t, s))
            break;
        uint8_t link[2];
        if (readContainer(link, 2) != 2)
            break;
        deallocateBlock(t, s);
        t = link[0];
        s = link[1];
    }

    // A REL file's side sectors are a second chain
    if ((entry.file_type & 0x07) == 4 && entry.rel_start_track != 0)
        freeRelSideSectors(entry.rel_start_track, entry.rel_start_sector);

    // Mark the directory entry as deleted
    std::string dirsec = readBlock(dir_track, dir_sector);
    if (dirsec.size() != block_size)
        return false;
    dirsec[slot * 32 + 2] = 0x00;
    return writeBlock(dir_track, dir_sector, dirsec);
}

// vdrive_rel_scratch + vdrive_dir_free_chain: each chain is freed until a
// block that is already free. With a super side sector every group is a chain
// of its own (they are also linked to each other, so the later ones stop at
// once). VICE never frees the super side sector itself; we do, or every
// scratched 1581/CMD REL file would leak a block.
void D64MStream::freeRelSideSectors(uint8_t t, uint8_t s)
{
    auto freeChain = [this](uint8_t ct, uint8_t cs) {
        uint16_t safety = 0;
        while (ct != 0 && safety++ < 10000)
        {
            if (isBlockFree(ct, cs))
                break;
            uint8_t link[2];
            if (readBlockBytes(ct, cs, 0, link, 2) != 2)
                break;
            deallocateBlock(ct, cs);
            ct = link[0];
            cs = link[1];
        }
    };

    uint8_t blk[256];
    if (readBlockBytes(t, s, 0, blk, 256) != 256)
        return;
    if (blk[2] != 0xFE)
    {
        freeChain(t, s);
        return;
    }
    for (uint32_t i = 0; i < 126; i++)
        freeChain(blk[3 + i * 2], blk[4 + i * 2]);
    deallocateBlock(t, s);
}

void D64MStream::close()
{
    if (creating && !_error)
        finalizeFileWrite();
    creating = false;
    MMediaStream::close();
}


D64MStream::BlockChain D64MStream::getBlocks( uint8_t track, uint8_t sector )
{
    BlockChain chain;
    Block link;
    uint8_t next_track = track;
    uint8_t next_sector = sector;
    do
    {
        if (!seekSector(next_track, next_sector))
            break;

        readContainer((uint8_t *)&link, sizeof(entry));
        chain.push_back(link);
        next_track = link.track;
        next_sector = link.sector;
    } while (next_track != 0);

    return chain;
}

D64MStream::BlockChain D64MStream::getBlocks( std::string filename )
{
    if ( seekEntry(filename) )
        return getBlocks(entry.next_track, entry.next_sector);
    return BlockChain();
}

uint16_t D64MStream::blocksFree()
{
    uint16_t free_count = 0;

    // getTrackFreeCount handles both record styles: free-count byte
    // (D64/D81) and bitmap-only (D71 side 2, CMD native)
    for (auto &bam : curPartition().block_allocation_map)
    {
        for (uint16_t t = bam.start_track; t <= bam.end_track; t++)
        {
            if (dedicated_directory_track && t == curPartition().directory_track)
                continue;
            free_count += getTrackFreeCount(t);
        }
    }

    return free_count;
}

bool D64MStream::seek(uint32_t offset)
{
    // No file selected: the stream is the raw image, and MMediaStream::seek()
    // seeking the container is exactly right.
    if ( !seekCalled )
        return MMediaStream::seek( offset );

    if ( entry.start_track == 0 )
        return false;

    // A block carries block_size bytes, the first two of which are the link to
    // the next block -- so 254 bytes of FILE data per block on a 1541.
    const uint32_t data_per_block = block_size - 2;

    uint8_t t = entry.start_track;
    uint8_t s = entry.start_sector;
    uint32_t remaining = offset;

    // Walk the chain from the file's first block. Bounded by the entry's own
    // block count where it has one, so a corrupt link cannot spin forever on
    // the IEC task.
    uint32_t guard = entry.blocks ? (uint32_t) entry.blocks + 2 : 10000;
    while ( remaining >= data_per_block )
    {
        if ( !seekSector( t, s ) )
            return false;

        uint8_t link_track = 0, link_sector = 0;
        readContainer( &link_track, 1 );
        readContainer( &link_sector, 1 );

        // Last block of the file: the requested offset is past its end.
        if ( link_track == 0 )
            return false;

        t = link_track;
        s = link_sector;
        remaining -= data_per_block;

        if ( guard-- == 0 )
            return false;
    }

    // Land inside block t/s. Read its link first so readFile() can follow the
    // chain from here, then position past the two link bytes plus the
    // remainder -- sector_offset is deliberately non-zero so readFile() does
    // NOT re-read the header it has just been handed.
    if ( !seekSector( t, s ) )
        return false;

    readContainer( &next_track, 1 );
    readContainer( &next_sector, 1 );

    track  = t;
    sector = s;
    sector_offset = (uint8_t) ( 2 + remaining );   // < block_size, fits uint8_t

    if ( !seekSector( t, s, sector_offset ) )
        return false;

    _position = offset;
    return true;
}


uint32_t D64MStream::readFile(uint8_t *buf, uint32_t size)
{
    //Debug_printv("readFile(%lu) sector_offset[%d] pos[%lu]", size, sector_offset, _position);
    if (sector_offset % block_size == 0)
    {
        // If we previously read a block header with next_track==0 (EOF) and the last block
        // was fully used, sector_offset lands here again. Guard against reading garbage bytes
        // from the container as if they were a new block header.
        if (sector_offset > 0 && next_track == 0)
            return 0;

        // We are at the beginning of the block
        // Read track/sector link
        readContainer((uint8_t *)&next_track, 1);
        readContainer((uint8_t *)&next_sector, 1);
        sector_offset += 2;

        //Debug_printv("next_track[%d] next_sector[%d]", next_track, next_sector);
        if ( next_track == 0 )
        {
            // End of file reached
            // next_sector = byte offset of last used byte (1-indexed from sector start,
            // so data = bytes 2..next_sector = next_sector-1 data bytes). Match seekFileSize().
            uint32_t lastBlockBytes = (next_sector > 1) ? (uint32_t)(next_sector - 1) : 0;

            // Adjust _size to reflect actual file size based on last block byte count
            if ( available() > lastBlockBytes )
            {
                _size = _position + lastBlockBytes;
                //Debug_printv("End of file reached, adjusting size to [%lu] available[%lu]", _size, available());
            }
            else if ( available() == 0 )
            {
                _size += lastBlockBytes;
                //Debug_printv("End of file reached with 0 available bytes, adjusting size to [%lu]", _size);
            }
        }
        else if ( available() == 0 )
        {
            // Not end of file, _size should be at least the current position + bytes available in this block
            _size += (block_size - 2);
            //Debug_printv("Adjusting size to [%lu] available[%lu]", _size, available());
        }
    }

    uint32_t bytesRead = 0;

    if (size > 0)
    {
        if (size > available())
            size = available();
        
        // Only read up to the bytes remaining in this sector
        size = std::min(size, (uint32_t) (block_size - sector_offset % block_size));

        bytesRead += readContainer(buf, size);
        sector_offset += bytesRead;

        if (next_track && sector_offset % block_size == 0)
        {
            // We are at the end of the block
            // Follow track/sector link to move to next block
            if (!seekSector(next_track, next_sector))
            {
                return 0;
            }
            //Debug_printv("track[%d] sector[%d] sector_offset[%d]", track, sector, sector_offset);
        }
    }

    // if ( !bytesRead )
    // {
    //     sector_offset = 0;
    // }

    return bytesRead;
}

uint32_t D64MStream::writeFile(uint8_t *buf, uint32_t size)
{
    // Streamed new-file write (or swallowing data after a write error)
    if (creating || _error)
        return writeFileNew(buf, size);

    Debug_printv("writeFile(%d)", size);
    if (sector_offset % block_size == 0)
    {
        // We are at the beginning of the block
        // Read track/sector link
        readContainer((uint8_t *)&next_track, 1);
        readContainer((uint8_t *)&next_sector, 1);
        sector_offset += 2;
        //Debug_printv("next_track[%d] next_sector[%d] sector_offset[%d]", next_track, next_sector, sector_offset);
    }

    uint32_t bytesWritten = 0;

    if (size > 0)
    {
        if (size > available())
            size = available();

        // Only write up to the bytes remaining in this sector
        size = std::min(size, (uint32_t) (block_size - sector_offset % block_size));

        bytesWritten += writeContainer(buf, size);
        sector_offset += bytesWritten;

        if (next_track && sector_offset % block_size == 0)
        {
            // We are at the end of the block
            // Follow track/sector link to move to next block
            if (!seekSector(next_track, next_sector))
            {
                return 0;
            }
            //Debug_printv("track[%d] sector[%d] sector_offset[%d]", track, sector, sector_offset);
        }
    }

    // if ( !bytesWritten )
    // {
    //     sector_offset = 0;
    // }

    return bytesWritten;
}

// Split an in-image path on '/' (a literal '/' in a filename is encoded
// as '\' in the URL and restored per-component by seekEntry)
static std::vector<std::string> splitPathComponents(const std::string &path)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < path.size())
    {
        size_t p = path.find('/', start);
        if (p == std::string::npos)
            p = path.size();
        if (p > start)
            parts.push_back(path.substr(start, p - start));
        start = p + 1;
    }
    return parts;
}

// Enter a subdirectory entry in the current directory: CMD native "DIR"
// entries and 1581 "CBM" sub-partitions both point at a header block whose
// first two bytes link to the first directory sector.
bool D64MStream::enterDirectory(std::string name)
{
    if (!seekEntry(name))
        return false;

    uint8_t t = entry.file_type & 0b00000111;
    if (t != 5 && t != 6) // 5 = CBM sub-partition, 6 = CMD native DIR
        return false;

    std::string hdr = readBlock(entry.start_track, entry.start_sector);
    if (hdr.size() != block_size)
        return false;

    dir_track = (uint8_t)hdr[0];
    dir_sector = (uint8_t)hdr[1];
    entry_index = 0;
    if (dir_track == 0)
        return false; // not a formatted subdirectory

    //Debug_printv("entered dir[%s] chain[%d/%d]", name.c_str(), dir_track, dir_sector);
    return true;
}

bool D64MStream::seekDirectory(std::string path)
{
    // Reset to the image root
    dir_track = 0;
    dir_sector = 0;
    entry_index = 0;

    auto parts = splitPathComponents(path);
    for (size_t i = 0; i < parts.size(); i++)
    {
        if (!enterDirectory(parts[i]))
            return false;
    }
    return true;
}

D64MStream::PathResult D64MStream::resolvePath(std::string path)
{
    auto parts = splitPathComponents(path);
    if (parts.empty())
        return seekDirectory("") ? PATH_DIR : PATH_NOT_FOUND;

    // Resolve everything up to the last component as directories
    std::string parent;
    for (size_t i = 0; i + 1 < parts.size(); i++)
    {
        if (i) parent += '/';
        parent += parts[i];
    }
    if (!seekDirectory(parent))
        return PATH_NOT_FOUND;

    std::string last = parts.back();

    if (!seekEntry(last))
        return PATH_NOT_FOUND;

    uint8_t t = entry.file_type & 0b00000111;
    if (t == 5 || t == 6)
        return enterDirectory(last) ? PATH_DIR : PATH_NOT_FOUND;

    return PATH_FILE;
}

bool D64MStream::seekPath(std::string path)
{
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    next_track = 0;
    next_sector = 0;
    sector_offset = 0;
    _position = 0;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    // return D64Image.seekFile(containerIStream, path);
    if (mstr::endsWith(path, "#")) // Direct Access Mode
    {
        Debug_printv("Direct Access Mode track[1] sector[0] path[%s]", path.c_str());
        seekCalled = false;
        _size = block_size;
        return seekSector(1, 0);
    }

    if (mode == std::ios_base::out)
    {
        PathResult r = resolvePath(path);
        if (r == PATH_DIR)
            return false; // cannot write to a directory/partition

        auto parts = splitPathComponents(path);
        if (parts.empty())
            return false;
        std::string last = parts.back();

        if (r == PATH_FILE)
        {
            // SAVE"@:file" overwrite: scratch the old file, then stream a new
            // one - its entry reuses the slot just freed.
            Debug_printv("Overwriting [%s]", path.c_str());
            scratchEntry();
            return beginFileWrite(last);
        }

        // Not found: resolve the parent directory and create the file there
        std::string parent;
        for (size_t i = 0; i + 1 < parts.size(); i++)
        {
            if (i) parent += '/';
            parent += parts[i];
        }
        if (!seekDirectory(parent))
            return false;
        return beginFileWrite(last);
    }

    PathResult pr = resolvePath(path);
    if (pr == PATH_DIR)
    {
        // Partition or subdirectory: position at the start of its directory
        // chain so broker-cached streams for listings open successfully
        // (and the chain can be read raw, as before).
        _size = 0;
        uint8_t dt = dir_track ? dir_track : curPartition().directory_track;
        uint8_t ds = dir_track ? dir_sector : curPartition().directory_sector;
        return seekSector(dt, ds);
    }
    if (pr == PATH_FILE)
    {
        // auto entry = containerImage->entry;
        //auto type = decodeType(entry.file_type).c_str();
        //Debug_printv("filename[%.16s] type[%s] start_track[%d] start_sector[%d]", entry.filename, type, entry.start_track, entry.start_sector);

        // resolvePath() left track/sector on the entry's directory sector
        entry_dir_track = track;
        entry_dir_sector = sector;
        entry_dir_slot = (entry_index - 1) % 8;

        // Calculate file size
        uint8_t t = entry.start_track;
        uint8_t s = entry.start_sector;
        //if (containerStream && containerStream->isNetwork()) {
            // For network streams, skip the expensive block-chain walk (hundreds of RPCs).
            // entry.blocks * (block_size-2) always >= actual byte size, so the chain-end
            // marker (track=0) fires before _size is reached — safe upper bound.
            _size = (uint32_t)entry.blocks * (block_size - 2);
            //Debug_printv("Network stream: using blocks[%d] → size[%lu]", entry.blocks, _size);
        //} else {
        //    _size = seekFileSize(t, s);
        //}

        // Set position to beginning of file
        bool r = seekSector(t, s);

        Debug_printv("blocks[%d] size[%d] available[%d] r[%d]", entry.blocks, _size, available(), r);

        return r;
    }
    else
    {
        Debug_printv("Not found! [%s]", path.c_str());
    }

    return false;
};

/********************************************************
 * REL files (read)
 ********************************************************/

uint32_t D64MStream::readBlockBytes(uint8_t track, uint8_t sector, uint8_t off, uint8_t* buf, uint32_t n)
{
    if (!seekSector(track, sector, off))
        return 0;
    return readContainer(buf, n);
}

uint32_t D64MStream::writeBlockBytes(uint8_t track, uint8_t sector, uint8_t off, const uint8_t* buf, uint32_t n)
{
    if (!seekSector(track, sector, off))
        return 0;
    return writeContainer((uint8_t*)buf, n);
}

// Data bytes live in the file's blocks at offsets 2..255 (254 per block).
// Where block N is comes from the side sectors, loaded once into a heap table
// of track/sector pairs - the chain is never walked, which matters over a
// network filesystem where each block read is a round trip.
//
// Side sector: 0-1 next side sector, 2 its number within its group, 3 record
// length, 4-15 the six T/S of its group, 16-255 up to 120 data block T/S. On a
// 1581/CMD native the entry points at a SUPER side sector instead: 0-1 first
// side sector, 2 = $FE, 3.. the first side sector of each six-sector group
// (confirmed against a c1541-made image: 39/1 then 21/8, whose own next link
// is 0). Side sectors are also chained to each other across groups.
//
// Growth follows vdrive_rel_add_sector/vdrive_rel_grow: whole blocks are
// added and filled with records ($FF then zeros), and the size is the whole
// records the blocks hold - the last block's last-used byte is 255 - k, where
// k is the partial record left over. Unlike VICE a grow is all or nothing: the
// blocks are claimed first and only written once every one was found, so a
// 52 or 72 leaves the image as it was.
class D64RelStorage : public RELStorage {
public:
    static constexpr uint32_t DATA_PER_BLOCK = 254;
    static constexpr uint32_t PLAIN_SIDE_SECTORS = 6;   // per group
    static constexpr uint32_t MAX_GROUPS = 126;         // SIDE_SUPER_MAX
    static constexpr uint32_t MAX_SIDE_SECTORS = MAX_GROUPS * PLAIN_SIDE_SECTORS;
    static constexpr uint32_t ENTRIES_PER_SIDE = 120;

    // Owns a reference to the stream: the storage reads through it, and a
    // RELMStream may well outlive every other holder of the stream.
    D64RelStorage(std::shared_ptr<D64MStream> image, uint8_t record_length,
                  uint8_t dir_t, uint8_t dir_s, uint8_t slot)
        : img_(std::move(image)), len_(record_length), dir_t_(dir_t), dir_s_(dir_s), slot_(slot)
    {
        max_blocks_ = img_->relBlocksMax();
        super_ = img_->relHasSuper();
    }
    // A grow changed the BAM, the directory entry's block count and the
    // file's blocks through THIS stream; a listing stream ImageBroker cached
    // has its own handle on the container and would keep serving the old
    // view, so it is dropped (as D64MFile::remove() does). Matched by the
    // container url, which every stream decoded from it shares.
    ~D64RelStorage() override
    {
        free(table_);
        if (grew_ && !img_->url.empty())
            ImageBroker::disposeUnderPath(img_->url);
    }

    // Loads the side sectors and sizes the file. False if the image is
    // damaged (bad link, truncated list) or the file is larger than the
    // format allows.
    bool load(uint8_t first_track, uint8_t first_sector)
    {
        uint8_t* blk = (uint8_t*)malloc(256);
        if (blk == nullptr) return false;
        bool ok = loadTable(blk, first_track, first_sector);
        if (ok && blocks_ > 0)
        {
            uint8_t link[2];
            ok = img_->readBlockBytes(table_[(blocks_ - 1) * 2], table_[(blocks_ - 1) * 2 + 1], 0, link, 2) == 2;
            if (ok)
            {
                // Last block: link track 0, sector = index of the last used byte.
                uint32_t used = (link[0] == 0) ? (link[1] > 1 ? link[1] - 1u : 0u) : DATA_PER_BLOCK;
                size_ = (blocks_ - 1) * DATA_PER_BLOCK + used;
            }
        }
        free(blk);
        return ok;
    }

    uint32_t size() override { return size_; }

    uint32_t readAt(uint32_t off, uint8_t* buf, uint32_t n) override
    {
        return transfer(off, buf, n, false);
    }

    uint32_t writeAt(uint32_t off, const uint8_t* buf, uint32_t n) override
    {
        if (!writable()) return 0;
        return transfer(off, (uint8_t*)buf, n, true);
    }

    bool writable() override { return (img_->mode & std::ios_base::out) != 0; }

    // writeDataBlocks() fills everything past the old end with records.
    bool fillsNewRecords() const override { return true; }

    // vdrive_rel_grow / vdrive_rel_add_sector
    uint8_t grow(uint32_t new_size) override
    {
        if (!writable()) return ST_WRITE_PROTECT_ON;
        if (new_size <= size_) return ST_OK;

        uint64_t bytes = (uint64_t)((new_size + len_ - 1) / len_) * len_;
        uint32_t need = (uint32_t)((bytes + DATA_PER_BLOCK - 1) / DATA_PER_BLOCK);

        // vdrive_rel_blocks_max: checked against the file's total before each
        // data block is added, exactly as VICE does - but all of it up front.
        uint32_t total = blocks_ + nsides_ + (super_t_ ? 1 : 0);
        uint32_t sides = nsides_;
        uint32_t new_sides = 0;
        bool new_super = false;
        for (uint32_t b = blocks_; b < need; b++)
        {
            if (total >= max_blocks_)
                return ST_FILE_TOO_LARGE;
            total++;
            if (b % ENTRIES_PER_SIDE == 0) { total++; sides++; new_sides++; }
            if (b == 0 && super_) { total++; new_super = true; }
        }
        if (sides > (super_ ? MAX_SIDE_SECTORS : PLAIN_SIDE_SECTORS))
            return ST_FILE_TOO_LARGE;
        if (!reserveTable(need))
            return ST_DISK_FULL;

        // Claim every block before writing anything: data blocks, each side
        // sector right after the data block that needs it, the super side
        // sector after the first side sector (vdrive_rel_add_sector's order).
        // Both buffers before the first claim: running out of memory once
        // blocks are claimed would leave them allocated and owned by nothing.
        uint32_t nclaim = (need - blocks_) + new_sides + (new_super ? 1 : 0);
        uint8_t* claimed = (uint8_t*)malloc((size_t)nclaim * 2);
        uint8_t* blk = (uint8_t*)malloc(256);
        if (claimed == nullptr || blk == nullptr)
        {
            free(claimed);
            free(blk);
            return ST_DISK_FULL;
        }
        uint32_t nc = 0;
        uint8_t t = blocks_ ? table_[(blocks_ - 1) * 2] : 0;
        uint8_t sct = blocks_ ? table_[(blocks_ - 1) * 2 + 1] : 0;
        bool ok = true;
        for (uint32_t b = blocks_; b < need && ok; b++)
        {
            ok = claim(t, sct, claimed, nc);
            if (!ok) break;
            t = claimed[(nc - 1) * 2];
            sct = claimed[(nc - 1) * 2 + 1];
            table_[b * 2] = t;
            table_[b * 2 + 1] = sct;
            if (b % ENTRIES_PER_SIDE == 0)
            {
                ok = claim(t, sct, claimed, nc);
                if (!ok) break;
                sides_[(b / ENTRIES_PER_SIDE) * 2] = claimed[(nc - 1) * 2];
                sides_[(b / ENTRIES_PER_SIDE) * 2 + 1] = claimed[(nc - 1) * 2 + 1];
                if (b == 0 && super_)
                {
                    ok = claim(claimed[(nc - 1) * 2], claimed[(nc - 1) * 2 + 1], claimed, nc);
                    if (!ok) break;
                    new_super_t_ = claimed[(nc - 1) * 2];
                    new_super_s_ = claimed[(nc - 1) * 2 + 1];
                }
            }
        }
        if (!ok)
        {
            for (uint32_t i = 0; i < nc; i++)
                img_->deallocateBlock(claimed[i * 2], claimed[i * 2 + 1]);
            free(claimed);
            free(blk);
            return ST_DISK_FULL;
        }
        free(claimed);
        grew_ = true;

        uint32_t old_blocks = blocks_, old_sides = nsides_, old_size = size_;
        blocks_ = need;
        nsides_ = sides;
        if (new_super) { super_t_ = new_super_t_; super_s_ = new_super_s_; }
        uint32_t k = (uint32_t)(((uint64_t)blocks_ * DATA_PER_BLOCK) % len_);
        size_ = blocks_ * DATA_PER_BLOCK - k;

        bool w = writeDataBlocks(blk, old_blocks ? old_blocks - 1 : 0, old_size, k) &&
                 writeSideSectors(blk, old_sides) &&
                 (!super_t_ || writeSuper(blk)) &&
                 writeDirEntry(blk);
        free(blk);
        return w ? ST_OK : ST_WRITE_VERIFY;
    }

private:
    uint32_t transfer(uint32_t off, uint8_t* buf, uint32_t n, bool write)
    {
        if (off >= size_) return 0;
        if (n > size_ - off) n = size_ - off;
        uint32_t done = 0;
        while (done < n)
        {
            uint32_t idx = (off + done) / DATA_PER_BLOCK;
            uint32_t in = (off + done) % DATA_PER_BLOCK;
            uint32_t chunk = std::min<uint32_t>(n - done, DATA_PER_BLOCK - in);
            uint8_t bt = table_[idx * 2], bs = table_[idx * 2 + 1];
            uint32_t got = write ? img_->writeBlockBytes(bt, bs, (uint8_t)(2 + in), buf + done, chunk)
                                 : img_->readBlockBytes(bt, bs, (uint8_t)(2 + in), buf + done, chunk);
            if (got == 0) break;
            done += got;
        }
        return done;
    }

    // Room in the data block table for n entries; the table grows on demand
    // rather than reserving the format's maximum for every open.
    bool reserveTable(uint32_t n)
    {
        if (n <= capacity_) return true;
        uint32_t cap = capacity_ ? capacity_ : ENTRIES_PER_SIDE;
        while (cap < n) cap *= 2;
        uint8_t* t = (uint8_t*)realloc(table_, (size_t)cap * 2);
        if (t == nullptr) return false;
        table_ = t;
        capacity_ = cap;
        return true;
    }

    // One free block after (t, s), allocated and recorded in 'claimed'.
    bool claim(uint8_t t, uint8_t s, uint8_t* claimed, uint32_t& nc)
    {
        uint8_t ft, fs;
        if (!img_->getNextFreeBlock(t, s, &ft, &fs) || !img_->allocateBlock(ft, fs))
            return false;
        claimed[nc * 2] = ft;
        claimed[nc * 2 + 1] = fs;
        nc++;
        return true;
    }

    // Blocks from 'first' to the end: the old content up to the old end of
    // file is kept, everything after it becomes $FF,0... records.
    bool writeDataBlocks(uint8_t* blk, uint32_t first, uint32_t old_size, uint32_t k)
    {
        for (uint32_t b = first; b < blocks_; b++)
        {
            uint8_t bt = table_[b * 2], bs = table_[b * 2 + 1];
            uint32_t base = b * DATA_PER_BLOCK;
            uint32_t keep = old_size > base ? std::min<uint32_t>(old_size - base, DATA_PER_BLOCK) : 0;
            if (keep && img_->readBlockBytes(bt, bs, 0, blk, 256) != 256) return false;
            for (uint32_t i = keep; i < DATA_PER_BLOCK; i++)
                blk[2 + i] = ((base + i) % len_) == 0 ? 0xFF : 0x00;
            if (b + 1 < blocks_)
            {
                blk[0] = table_[(b + 1) * 2];
                blk[1] = table_[(b + 1) * 2 + 1];
            }
            else
            {
                blk[0] = 0;
                blk[1] = (uint8_t)(255 - k);
            }
            if (img_->writeBlockBytes(bt, bs, 0, blk, 256) != 256) return false;
        }
        return true;
    }

    // Every side sector of every group that changed. A new side sector
    // changes the link of the one before it and the group list in all of its
    // group, so rewriting from the group of the last old side sector covers it.
    bool writeSideSectors(uint8_t* blk, uint32_t old_sides)
    {
        uint32_t from = 0;
        if (old_sides == nsides_)
            from = old_sides - 1;   // only the last one gained data blocks
        else if (old_sides)
            from = ((old_sides - 1) / PLAIN_SIDE_SECTORS) * PLAIN_SIDE_SECTORS;
        for (uint32_t i = from; i < nsides_; i++)
        {
            memset(blk, 0, 256);
            uint32_t first = i * ENTRIES_PER_SIDE;
            uint32_t n = std::min<uint32_t>(ENTRIES_PER_SIDE, blocks_ - first);
            if (i + 1 < nsides_)
            {
                blk[0] = sides_[(i + 1) * 2];
                blk[1] = sides_[(i + 1) * 2 + 1];
            }
            else
            {
                blk[0] = 0;
                blk[1] = (uint8_t)(16 + 2 * n - 1);
            }
            blk[2] = (uint8_t)(i % PLAIN_SIDE_SECTORS);
            blk[3] = len_;
            uint32_t g = (i / PLAIN_SIDE_SECTORS) * PLAIN_SIDE_SECTORS;
            for (uint32_t j = 0; j < PLAIN_SIDE_SECTORS && g + j < nsides_; j++)
            {
                blk[4 + j * 2] = sides_[(g + j) * 2];
                blk[5 + j * 2] = sides_[(g + j) * 2 + 1];
            }
            memcpy(blk + 16, table_ + first * 2, n * 2);
            if (img_->writeBlockBytes(sides_[i * 2], sides_[i * 2 + 1], 0, blk, 256) != 256) return false;
        }
        return true;
    }

    bool writeSuper(uint8_t* blk)
    {
        memset(blk, 0, 256);
        blk[0] = sides_[0];
        blk[1] = sides_[1];
        blk[2] = 0xFE;
        for (uint32_t g = 0; g * PLAIN_SIDE_SECTORS < nsides_; g++)
        {
            blk[3 + g * 2] = sides_[g * PLAIN_SIDE_SECTORS * 2];
            blk[4 + g * 2] = sides_[g * PLAIN_SIDE_SECTORS * 2 + 1];
        }
        return img_->writeBlockBytes(super_t_, super_s_, 0, blk, 256) == 256;
    }

    // First data block, side sector (or super side sector) and block count.
    bool writeDirEntry(uint8_t* blk)
    {
        if (img_->readBlockBytes(dir_t_, dir_s_, 0, blk, 256) != 256) return false;
        uint8_t* e = blk + slot_ * 32;
        uint32_t total = blocks_ + nsides_ + (super_t_ ? 1 : 0);
        e[3] = table_[0];
        e[4] = table_[1];
        e[21] = super_t_ ? super_t_ : sides_[0];
        e[22] = super_t_ ? super_s_ : sides_[1];
        e[30] = total & 0xFF;
        e[31] = (total >> 8) & 0xFF;
        return img_->writeBlockBytes(dir_t_, dir_s_, 0, blk, 256) == 256;
    }

    // Appends the data block T/S of one side sector.
    bool addSideSector(uint8_t* blk, uint8_t t, uint8_t s)
    {
        if (nsides_ >= MAX_SIDE_SECTORS)
        {
            Debug_printv("REL: more than %u side sectors", (unsigned)MAX_SIDE_SECTORS);
            return false;
        }
        sides_[nsides_ * 2] = t;
        sides_[nsides_ * 2 + 1] = s;
        nsides_++;
        if (img_->readBlockBytes(t, s, 0, blk, 256) != 256) return false;
        for (uint32_t i = 0; i < ENTRIES_PER_SIDE; i++)
        {
            uint8_t dt = blk[16 + i * 2], ds = blk[17 + i * 2];
            if (dt == 0) return true;   // end of this (and so of every) list
            if (blocks_ >= max_blocks_)
            {
                Debug_printv("REL: more data blocks than the format allows (%u)", (unsigned)max_blocks_);
                return false;
            }
            if (!reserveTable(blocks_ + 1)) return false;
            table_[blocks_ * 2] = dt;
            table_[blocks_ * 2 + 1] = ds;
            blocks_++;
        }
        return true;
    }

    // A group is up to six side sectors, and every one of them carries the
    // whole group's T/S list at bytes 4-15, so the head alone names the rest.
    bool addGroup(uint8_t* blk, uint8_t t, uint8_t s)
    {
        if (img_->readBlockBytes(t, s, 0, blk, 256) != 256) return false;
        uint8_t list[PLAIN_SIDE_SECTORS * 2];
        memcpy(list, blk + 4, sizeof(list));
        for (uint32_t i = 0; i < PLAIN_SIDE_SECTORS && list[i * 2] != 0; i++)
        {
            uint32_t before = blocks_;
            if (!addSideSector(blk, list[i * 2], list[i * 2 + 1])) return false;
            if (blocks_ - before < ENTRIES_PER_SIDE) break;   // a short side sector is the last
        }
        return true;
    }

    bool loadTable(uint8_t* blk, uint8_t t, uint8_t s)
    {
        if (img_->readBlockBytes(t, s, 0, blk, 256) != 256) return false;

        if (blk[2] != 0xFE)
            return addGroup(blk, t, s);

        // Super side sector (1581/CMD native): bytes 3..254 are the T/S of
        // each group's first side sector - all 126 of them. Copy the list
        // first, blk is reused below.
        super_t_ = t;
        super_s_ = s;
        uint8_t list[MAX_GROUPS * 2];
        memcpy(list, blk + 3, sizeof(list));
        for (uint32_t i = 0; i < MAX_GROUPS && list[i * 2] != 0; i++)
        {
            uint32_t before = blocks_;
            if (!addGroup(blk, list[i * 2], list[i * 2 + 1])) return false;
            if (blocks_ - before < PLAIN_SIDE_SECTORS * ENTRIES_PER_SIDE) break;
        }
        return true;
    }

    std::shared_ptr<D64MStream> img_;
    uint8_t  len_;
    uint8_t  dir_t_, dir_s_, slot_;   // directory entry, for growth
    uint32_t max_blocks_ = 0;         // vdrive_rel_blocks_max
    bool     super_ = false;          // vdrive_rel_has_super
    uint8_t* table_ = nullptr;        // T/S pairs of every data block, heap
    uint32_t capacity_ = 0;
    uint32_t blocks_ = 0;
    uint32_t size_ = 0;
    uint8_t  sides_[MAX_SIDE_SECTORS * 2] = {};   // T/S of every side sector, in order
    uint32_t nsides_ = 0;
    uint8_t  super_t_ = 0, super_s_ = 0;          // 0: no super side sector
    uint8_t  new_super_t_ = 0, new_super_s_ = 0;
    bool     grew_ = false;                       // the image was changed by grow()
};

uint8_t D64MStream::relRecordLength()
{
    if (!seekCalled || (entry.file_type & 0x07) != 4)
        return 0;
    return entry.rel_record_length;
}

std::unique_ptr<RELStorage> D64MStream::relStorage()
{
    if (relRecordLength() == 0)
        return nullptr;
    // nothrow: plain new would abort on failure under -fno-exceptions.
    // Needs a shared_ptr owner to pin; a stream not held by one cannot serve REL.
    auto self = std::static_pointer_cast<D64MStream>(weak_from_this().lock());
    if (!self)
        return nullptr;
    D64RelStorage* st = new (std::nothrow) D64RelStorage(std::move(self), entry.rel_record_length,
                                                         entry_dir_track, entry_dir_sector, entry_dir_slot);
    if (st == nullptr)
        return nullptr;
    if (!st->load(entry.rel_start_track, entry.rel_start_sector))
    {
        delete st;
        return nullptr;
    }
    return std::unique_ptr<RELStorage>(st);
}

// vdrive_rel_open_new: only the directory entry is written; the first write
// grows the file (vdrive_rel_add_sector with no side sectors yet).
std::unique_ptr<RELStorage> D64MStream::createRel(std::string path, uint8_t record_length)
{
    if (record_length == 0 || record_length > 254)
        return nullptr;
    auto self = std::static_pointer_cast<D64MStream>(weak_from_this().lock());
    if (!self)
        return nullptr;
    if (resolvePath(path) != PATH_NOT_FOUND)
        return nullptr;

    auto parts = splitPathComponents(path);
    if (parts.empty())
        return nullptr;
    std::string parent;
    for (size_t i = 0; i + 1 < parts.size(); i++)
    {
        if (i) parent += '/';
        parent += parts[i];
    }
    if (!seekDirectory(parent))
        return nullptr;

    uint8_t dt, ds, slot;
    uint8_t st = addDirectoryEntry(parts.back(), 0x84, 0, 0, 0, 0, record_length, 0, &dt, &ds, &slot);
    if (st != 0)
    {
        _error = st;
        return nullptr;
    }
    D64RelStorage* rel = new (std::nothrow) D64RelStorage(std::move(self), record_length, dt, ds, slot);
    if (rel == nullptr)
        removeFile(path);   // no storage: take back the entry just written
    return std::unique_ptr<RELStorage>(rel);
}

/********************************************************
 * File implementations
 ********************************************************/

bool D64MStream::formatImage(std::string name, std::string id, size_t track_count, bool error_info)
{
    // A D9090/D9060 (dos_version 0xFF, as in linearBlock()) keeps its BAM as a
    // chain of blocks with a configuration sector at 0/0 and bad-block lists;
    // none of the layout below applies to it, and running it would write a
    // floppy BAM over the drive. Refuse instead.
    if (dos_version == 0xFF)
    {
        Debug_printv("format: not supported for D9090/D9060 images");
        return false;
    }

    // Settle the geometry BEFORE anything is laid out: initializeBlocks() fills
    // every track and initializeBlockAllocationMap() writes one BAM entry per
    // track, and both read end_track. A non-zero track_count is how a 40- or
    // 42-track D64, an 81-track D81, or a DNP created with more than one track
    // is asked for; 0 means "whatever this media's default is".
    if (track_count > 0)
        setTrackCount((uint8_t)track_count);

    // The parameter shadows the member of the same name - keep the member in
    // step so anything reading it later sees what was actually created.
    this->error_info = error_info;

    if (!initializeBlocks())
    {
        Debug_printv("format: initializeBlocks failed");
        return false;
    }

    if (!initializeBlockAllocationMap())
    {
        Debug_printv("format: initializeBlockAllocationMap failed");
        return false;
    }

    if (!writeHeader(name, id))
    {
        Debug_printv("format: writeHeader failed");
        return false;
    }

    if (!initializeDirectory())
    {
        Debug_printv("format: initializeDirectory failed");
        return false;
    }

    // Total blocks across the geometry just laid down.
    uint8_t last_track = curPartition().block_allocation_map.back().end_track;
    uint32_t blocks = 0;
    for (uint16_t t = 1; t <= last_track; t++)
        blocks += getSectorCount(t);

    // Canonical size only for a brand new image with no track count asked for.
    // Otherwise size from the geometry, which both honours an explicit
    // track_count and stops an existing 40-track D64 being truncated to 35.
    uint32_t image_size = (track_count == 0 && containerStream->size() == 0)
                        ? defaultImageSize()
                        : blocks * block_size;

    // An image carrying error info appends one status byte per sector after the
    // data area: 174848 + 683 = 175531 for a 35-track D64, 196608 + 768 =
    // 197376 for 40 tracks, 205312 + 802 = 206114 for 42.
    if (error_info)
        image_size += blocks;

    if (!seek(image_size - 1))
    {
        Debug_printv("format: seek to %lu failed", (unsigned long)(image_size - 1));
        return false;
    }

    uint8_t pad = 0x00;
    if (write(&pad, 1) != 1)
    {
        Debug_printv("format: final pad write at %lu failed", (unsigned long)(image_size - 1));
        return false;
    }
    return true;
}

D64FormatSpec parseD64FormatSpec(const std::string& header_info)
{
    D64FormatSpec spec;

    // Split into at most four fields; everything after the fourth comma is
    // ignored rather than treated as an error.
    std::string field[4];
    size_t n = 0, start = 0;
    while (n < 4)
    {
        size_t comma = header_info.find(',', start);
        field[n++] = (comma == std::string::npos)
                   ? header_info.substr(start)
                   : header_info.substr(start, comma - start);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }

    spec.name = field[0];
    spec.id   = field[1];

    // strtol, NOT std::stoi: this string arrives from the C64 and can be
    // anything, and ESP-IDF builds with -fno-exceptions - a stoi on garbage
    // would call std::terminate rather than throw something catchable.
    if (!field[2].empty())
    {
        char* end = nullptr;
        long v = strtol(field[2].c_str(), &end, 10);
        // Only a whole, in-range track count counts; anything else leaves the
        // default in place rather than creating a nonsense image.
        if (end != field[2].c_str() && *end == '\0' && v > 0 && v <= 255)
            spec.track_count = (size_t)v;
    }

    if (!field[3].empty())
    {
        char* end = nullptr;
        long v = strtol(field[3].c_str(), &end, 10);
        spec.error_info = (end != field[3].c_str() && *end == '\0' && v != 0);
    }

    return spec;
}

bool D64MFile::format(std::string header_info)
{
    Debug_printv("header_info[%s] url[%s]", header_info.c_str(), url.c_str());

    auto newFile = MFSOwner::File(url);
    if (newFile == nullptr)
        return false;

    std::shared_ptr<D64MStream> image = std::static_pointer_cast<D64MStream>(
        newFile->getSourceStream(std::ios_base::in | std::ios_base::out | std::ios_base::trunc));
    if (image == nullptr)
    {
        delete newFile;
        return false;
    }

    D64FormatSpec spec = parseD64FormatSpec(header_info);
    Debug_printv("name[%s] id[%s] tracks[%d] error_info[%d]",
                 spec.name.c_str(), spec.id.c_str(),
                 (int)spec.track_count, (int)spec.error_info);

    bool ok = image->formatImage(spec.name, spec.id, spec.track_count, spec.error_info);

    delete newFile;
    return ok;
}


bool D64MFile::rewindDirectory()
{
    dirIsOpen = true;
    //Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto image = ImageBroker::obtain<D64MStream>("d64", brokerUrl());
    if (image == nullptr)
        return false;

    image->readHeader();
    //Debug_printv("image->url[%s]", image->url.c_str());
    image->resetEntryCounter();

    // Position the stream at the directory this MFile points at
    // (partition and/or subdirectory path inside the image)
    if (!image->seekDirectory(pathInStream))
    {
        Debug_printv("directory not found in image [%s]", pathInStream.c_str());
        dirIsOpen = false;
        return false;
    }

    // Set Media Info Fields
    //Debug_printv("name[%s]", image->header.name);
    //Debug_printv("id_dos[%s]", image->header.id_dos);
    // On-disk bytes are PETSCII; Meatloaf carries names as UTF-8 and converts
    // back once at the IEC boundary. A02Space runs first, on the raw bytes.
    media_header = mstr::format("%.16s", image->header.name);
    mstr::A02Space(media_header);
    media_header = mstr::toUTF8(media_header);
    media_id = mstr::format("%.5s", image->header.id_dos);
    mstr::A02Space(media_id);
    media_id = mstr::toUTF8(media_id);
    media_blocks_free = image->blocksFree();
    media_block_size = image->block_size;
    media_image = name;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    return true;
}

std::string D64MFile::entryUrlFor(const std::string& filename)
{
    // Entry URL must include the in-image path (partition/subdirectory)
    std::string entryUrl;
    entryUrl.reserve(url.size() + pathInStream.size() + 2 + filename.size());
    entryUrl = url;
    if (pathInStream.size()) { entryUrl += '/'; entryUrl += pathInStream; }
    entryUrl += '/'; entryUrl += filename;
    return entryUrl;
}

MFile* D64MFile::getNextFileInDir()
{
    bool r = false;

    // A failed rewind leaves dirIsOpen false but has already reset the shared
    // stream's entry counter, so continuing would hand back the first entry
    // again on every call - an endless listing (e.g. "ls sett*", where
    // pathInStream names a file, not a directory).
    if (!dirIsOpen && !rewindDirectory())
        return nullptr;

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<D64MStream>("d64", brokerUrl());
    if (image == nullptr)
        goto exit;

    r = image->getNextImageEntry();

    if (r)
    {
        std::string filename = image->entry.filename;
        size_t i = filename.find_first_of(0xA0);
        if (i == std::string::npos || i > 16) i = 16;
        filename = filename.substr(0, i);
        // mstr::rtrimA0(filename);
        // The directory holds PETSCII. Convert HERE, before the entry URL is
        // built from it, so the name a listing shows is the name seekEntry()
        // matches (it converts the on-disk name the same way) and the name
        // that can be typed back.
        filename = mstr::toUTF8(filename);
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (url + "/" + filename).c_str() );

        std::string entryUrl = entryUrlFor(filename);
        auto file = MFSOwner::File(entryUrl);
        file->name = filename;  // Use actual CBM entry name, not container image name
        file->extension = image->decodeType(image->entry.file_type);
        file->size = image->entry.blocks * image->block_size;
        file->is_dir = image->isDirectory(image->entry.file_type);
        if ( (image->entry.file_type) == 0x00 )  // No type is hidden/deleted
            file->is_hidden = 1;

        //Debug_printv("name[%s] ext[%s][%02X] size[%lu] is_dir[%d] is_hidden[%d]", file->name.c_str(), file->extension.c_str(), image->entry.file_type, file->size, file->is_dir, file->is_hidden);

        return file;
    }

exit:
    // Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}

time_t D64MFile::getLastWrite()
{
    return getCreationTime();
}

time_t D64MFile::getCreationTime()
{
    // Use a stack-allocated tm to avoid dereferencing a null pointer.
    std::tm entry_time = {};
    auto stream = ImageBroker::obtain<D64MStream>("d64", brokerUrl());
    if ( stream != nullptr )
    {
        auto entry = stream->entry;
        entry_time.tm_year = entry.year + 1900;
        entry_time.tm_mon = entry.month;
        entry_time.tm_mday = entry.day;
        entry_time.tm_hour = entry.hour;
        entry_time.tm_min = entry.minute;
    }

    return mktime(&entry_time);
}

bool D64MFile::isDirectory()
{
    // Use cached value if set (e.g. by getNextFileInDir)
    if (is_dir != -1)
        return is_dir == 1;

    // Container root is always a directory
    if (pathInStream.empty() || pathInStream == "/")
        return true;

    // Walk the path inside the image (partition / subdirectory / file)
    auto stream = ImageBroker::obtain<D64MStream>("d64", brokerUrl());
    if (stream != nullptr)
        return stream->resolvePath(pathInStream) == D64MStream::PATH_DIR;

    return false;
}

bool D64MFile::remove()
{
    // The container itself - a .d64/.dhd sitting on flash or SD - is an
    // ordinary file, and deleting it is the base class's business.
    if (pathInStream.empty() || pathInStream == "/")
        return MFile::remove();

    // A file INSIDE the image. The base MFile::remove() deliberately refuses
    // this (there is no such path for ::remove() to unlink), so without this
    // override "rm" simply could not delete anything in a disk image.
    //
    // Not the ImageBroker's stream: that one is opened read-only, so freeing
    // blocks and rewriting the directory sector through it would fail. Ask for
    // in|out, which makes MFile::getSourceStream() open the container "r+".
    auto stream = std::static_pointer_cast<D64MStream>(
        getSourceStream(std::ios_base::in | std::ios_base::out));
    if (stream == nullptr || !stream->isOpen())
        return false;

    bool ok = stream->removeFile(pathInStream);
    stream->close();

    // The cached listing stream has its own handle on the container and would
    // keep serving the entry we just scratched.
    if (ok)
        ImageBroker::disposeFor("d64", brokerUrl());

    return ok;
}

std::shared_ptr<MStream> D64MFile::createRelStream(uint8_t record_length)
{
    if (pathInStream.empty() || pathInStream == "/" || sourceFile == nullptr)
        return nullptr;

    // in|out on the container: plain out would truncate the image.
    auto src = sourceFile->getSourceStream(std::ios_base::in | std::ios_base::out);
    if (src == nullptr)
        return nullptr;
    auto stream = std::static_pointer_cast<D64MStream>(getDecodedStream(src));
    if (stream == nullptr)
        return nullptr;
    stream->mode = std::ios_base::in | std::ios_base::out;

    auto storage = stream->createRel(pathInStream, record_length);
    if (storage == nullptr)
        return nullptr;

    // The cached listing stream would not show the new entry.
    ImageBroker::disposeFor("d64", brokerUrl());
    auto rel = std::make_shared<RELMStream>(std::move(storage), record_length);
    // vdrive_rel_open (newrelfile): a new REL file has one record at OPEN.
    // VICE ignores a failure here and leaves a 0-block REL in the directory;
    // we take the entry back instead, so a full disk answers the error at
    // OPEN and leaves nothing behind (the .r00 create does the same).
    if (rel->growTo(1) != ST_OK)
    {
        rel.reset();                     // release the storage's hold first
        stream->removeFile(pathInStream);
        ImageBroker::disposeFor("d64", brokerUrl());
        return nullptr;
    }
    return rel;
}

bool D64MFile::exists()
{
    //Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());

    // Container root (no pathInStream): the image IS one ordinary file on its
    // backend, so existence is just the underlying file's existence. Don't
    // build/parse a disk-image stream to answer it — for G64 that constructs a
    // G64MStream (readHeader() -> seekSector() -> GCR findSync()), needless
    // work here and actively fragile when the caller (e.g. rm) only wants to
    // delete the container file. isDirectory() already short-circuits the root
    // the same way.
    if ( pathInStream.empty() || pathInStream == "/" )
        return ( sourceFile != nullptr ) ? sourceFile->exists() : true;

    auto stream = ImageBroker::obtain<D64MStream>("d64", brokerUrl());
    if ( stream == nullptr )
        return false;

    // A path inside the image only exists if it resolves to a partition,
    // directory or file entry
    return stream->resolvePath(pathInStream) != D64MStream::PATH_NOT_FOUND;
}

