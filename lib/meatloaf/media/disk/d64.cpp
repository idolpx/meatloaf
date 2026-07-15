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

    return containerStream->seek(partition_base + (index * block_size) + offset);
}

bool D64MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
{
    uint16_t sectorOffset = 0;

    //Debug_printv("track[%d] sector[%d] offset[%d]", track, sector, offset);

    // Is this a valid track?
    uint16_t c = partitions[partition].block_allocation_map.size() - 1;
    uint8_t start_track = partitions[partition].block_allocation_map[0].start_track;
    uint8_t end_track = partitions[partition].block_allocation_map[c].end_track;
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

    track--;
    for (uint8_t index = 0; index < track; ++index)
    {
        sectorOffset += getSectorCount(index + 1);
        //Debug_printv("track[%d] speedZone[%d] secotorsPerTrack[%d] sectorOffset[%d]", (index + 1), speedZone(index), getSectorCount(index + 1), sectorOffset);
    }
    track++;
    sectorOffset += sector;

    this->block = sectorOffset;
    this->track = track;
    this->sector = sector;

    //Debug_printv("track[%d] sector[%d] speedZone[%d] sectorOffset[%d]", track, sector, speedZone(track), sectorOffset);

    return containerStream->seek(partition_base + (sectorOffset * block_size) + offset);
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
    for (auto &bam : partitions[partition].block_allocation_map)
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

bool D64MStream::setBlockAllocation(uint8_t track, uint8_t sector, bool allocate)
{
    BAMRecord rec;
    uint8_t buf[32]; // largest byte_count in use is 32 (DNP/DHD native: 256 sectors/track)
    if (!readBAMRecord(track, &rec, buf))
        return false;

    uint8_t base = rec.has_count ? 1 : 0;
    uint8_t byte_index = base + (sector >> 3);
    if (byte_index >= rec.byte_count)
        return false; // sector beyond what this BAM record can represent

    uint8_t bitmask = (uint8_t)(1 << (sector & 7));
    if (allocate)
    {
        if ((buf[byte_index] & bitmask) == 0)
        {
            Debug_printv("Block already allocated: track[%d] sector[%d]", track, sector);
            return false;
        }
        buf[byte_index] &= ~bitmask; // 0 = allocated
        if (rec.has_count && buf[0] > 0)
            buf[0]--;
    }
    else
    {
        if ((buf[byte_index] & bitmask) == bitmask)
        {
            Debug_printv("Block already free: track[%d] sector[%d]", track, sector);
            return false;
        }
        buf[byte_index] |= bitmask; // 1 = free
        if (rec.has_count)
            buf[0]++;
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

uint8_t D64MStream::getTrackFreeCount(uint8_t track)
{
    BAMRecord rec;
    uint8_t buf[32];
    if (!readBAMRecord(track, &rec, buf))
        return 0;

    if (rec.has_count)
        return buf[0];

    // Bitmap-only record: count the free (1) bits
    uint8_t count = 0;
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

    uint8_t base = rec.has_count ? 1 : 0;
    uint16_t spt = getSectorCount(track);
    for (uint16_t i = 0; i < spt; i++)
    {
        uint8_t s = (startSector + i) % spt;
        uint8_t byte_index = base + (s >> 3);
        if (byte_index >= rec.byte_count)
            continue; // beyond bitmap coverage - treat as allocated
        if (buf[byte_index] & (1 << (s & 7)))
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
            if (t == partitions[partition].header_track)
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
bool D64MStream::getNextFreeBlock(uint8_t startTrack, uint8_t startSector, uint8_t *foundTrack, uint8_t *foundSector, bool forDirectory)
{
    uint8_t dir_track = partitions[partition].directory_track;
    uint8_t first_track = partitions[partition].block_allocation_map.front().start_track;
    uint8_t last_track = partitions[partition].block_allocation_map.back().end_track;

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
        return false; // directory track is full
    }

    if (startTrack == 0)
    {
        // First block: closest track to the directory track with a free sector
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
        if (t == dir_track)
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
        if (t == dir_track)
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
        if (t == dir_track)
            continue;
        if (getTrackFreeCount(t) && findFreeSectorOnTrack(t, 0, foundSector))
        {
            *foundTrack = t;
            return true;
        }
    }

    return false;
}

bool D64MStream::isBlockFree(uint8_t track, uint8_t sector)
{
    BAMRecord rec;
    uint8_t buf[32];
    if (!readBAMRecord(track, &rec, buf))
        return false;

    uint8_t byte_index = (rec.has_count ? 1 : 0) + (sector >> 3);
    if (byte_index >= rec.byte_count)
        return false;

    // Check if bit is set (1 = free, 0 = allocated)
    uint8_t bitmask = (uint8_t)(1 << (sector & 7));
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
    // At the root of a multi-partition image the "entries" are partitions
    if (partition_list)
        return seekPartitionEntry(index);

    // Current directory defaults to the partition's root directory
    if (dir_track == 0)
    {
        dir_track = partitions[partition].directory_track;
        dir_sector = partitions[partition].directory_sector;
    }

    // Calculate Sector offset & Entry offset
    // 8 Entries Per Sector, 32 bytes Per Entry
    index--;
    uint16_t sectorOffset = index / 8;
    uint16_t entryOffset = (index % 8) * 32;

    //Debug_printv("----------");
    //Debug_printv("index[%d] sectorOffset[%d] entryOffset[%d] entry_index[%d]", index, sectorOffset, entryOffset, entry_index);

    if (index == 0 || index != entry_index)
    {
        // Start at first sector of directory
        next_track = 0;
        if (!seekSector(
                dir_track,
                dir_sector,
                partitions[partition].directory_offset))
            return false;

        // Find sector with requested entry
        do
        {
            if (next_track)
            {
                //Debug_printv("next_track[%d] next_sector[%d]", entry.next_track, entry.next_sector);
                if (!seekSector(entry.next_track, entry.next_sector))
                    return false;
            }

            readContainer((uint8_t *)&entry, sizeof(entry));
            next_track = entry.next_track;
            next_sector = entry.next_sector;

            //Debug_printv("sectorOffset[%d] -> track[%d] sector[%d]", sectorOffset, track, sector);

        } while (sectorOffset-- > 0);
        if (!seekSector(track, sector, entryOffset))
            return false;
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

    // Find the first free directory entry, following the chain of the
    // directory the file was created in
    uint8_t dt = dir_track ? dir_track : partitions[partition].directory_track;
    uint8_t ds = dir_track ? dir_sector : partitions[partition].directory_sector;
    std::string dirsec;
    int slot = -1;
    uint16_t chain_safety = 0;
    while (true)
    {
        dirsec = readBlock(dt, ds);
        if (dirsec.size() != block_size || chain_safety++ > 1000)
        {
            rollbackFileWrite();
            _error = ST_DIR_ERROR;
            return false;
        }

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
            Debug_printv("Directory full, cannot add entry for [%s]", create_filename.c_str());
            rollbackFileWrite();
            _error = ST_DISK_FULL;
            return false;
        }

        // Link the current last directory sector to the new one
        dirsec[0] = nt;
        dirsec[1] = ns;
        if (!writeBlock(dt, ds, dirsec))
        {
            deallocateBlock(nt, ns);
            rollbackFileWrite();
            _error = ST_WRITE_VERIFY;
            return false;
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
    std::string name = mstr::toPETSCII2(create_filename);
    if (name.size() > 16)
        name = name.substr(0, 16);

    dirsec[o + 2] = 0x82; // closed PRG
    dirsec[o + 3] = create_start_track;
    dirsec[o + 4] = create_start_sector;
    memset(&dirsec[o + 5], 0xA0, 16);
    memcpy(&dirsec[o + 5], name.c_str(), name.size());
    memset(&dirsec[o + 21], 0x00, 9);
    uint16_t blocks = create_allocated.size();
    dirsec[o + 30] = blocks & 0xFF;
    dirsec[o + 31] = blocks >> 8;

    if (!writeBlock(dt, ds, dirsec))
    {
        rollbackFileWrite();
        _error = ST_WRITE_VERIFY;
        return false;
    }

    Debug_printv("Created [%s] start[%d/%d] blocks[%d] entry at [%d/%d] slot[%d]",
                 create_filename.c_str(), create_start_track, create_start_sector, blocks, dt, ds, slot);
    return true;
}

void D64MStream::rollbackFileWrite()
{
    // Free every block claimed for this file; no directory entry was written
    for (auto &b : create_allocated)
        deallocateBlock(b.track, b.sector);
    create_allocated.clear();
    creating = false;
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

    // Mark the directory entry as deleted
    std::string dirsec = readBlock(dir_track, dir_sector);
    if (dirsec.size() != block_size)
        return false;
    dirsec[slot * 32 + 2] = 0x00;
    return writeBlock(dir_track, dir_sector, dirsec);
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
    for (auto &bam : partitions[partition].block_allocation_map)
    {
        for (uint16_t t = bam.start_track; t <= bam.end_track; t++)
        {
            if (t == partitions[partition].directory_track)
                continue;
            free_count += getTrackFreeCount(t);
        }
    }

    return free_count;
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
    partition_list = hasPartitions();
    dir_track = 0;
    dir_sector = 0;
    entry_index = 0;

    auto parts = splitPathComponents(path);
    size_t i = 0;

    if (partition_list && parts.size())
    {
        if (selectPartitionByName(parts[0]))
            i = 1;
        else if (!selectPartitionByName("")) // fall back to default partition
            return false;
        partition_list = false;
        entry_index = 0;
    }

    for (; i < parts.size(); i++)
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

    // At the root of a multi-partition image the last component may be a
    // partition name; otherwise fall through to the default partition.
    if (partition_list)
    {
        if (selectPartitionByName(last))
        {
            partition_list = false;
            entry_index = 0;
            return PATH_DIR;
        }
        if (!selectPartitionByName(""))
            return PATH_NOT_FOUND;
        partition_list = false;
        entry_index = 0;
    }

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
        if (partition_list)
        {
            if (!selectPartitionByName(""))
                return false;
            partition_list = false;
        }
        return beginFileWrite(last);
    }

    PathResult pr = resolvePath(path);
    if (pr == PATH_DIR)
    {
        // Partition or subdirectory: position at the start of its directory
        // chain so broker-cached streams for listings open successfully
        // (and the chain can be read raw, as before).
        _size = 0;
        uint8_t dt = dir_track ? dir_track : partitions[partition].directory_track;
        uint8_t ds = dir_track ? dir_sector : partitions[partition].directory_sector;
        return seekSector(dt, ds);
    }
    if (pr == PATH_FILE)
    {
        // auto entry = containerImage->entry;
        //auto type = decodeType(entry.file_type).c_str();
        //Debug_printv("filename[%.16s] type[%s] start_track[%d] start_sector[%d]", entry.filename, type, entry.start_track, entry.start_sector);

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
 * File implementations
 ********************************************************/

bool D64MFile::format(std::string header_info)
{
    Debug_printv("header_info[%s] url[%s]", header_info.c_str(), url.c_str());

    // Set the stream file
    auto newFile = MFSOwner::File(url);
    std::shared_ptr<D64MStream> image = std::static_pointer_cast<D64MStream>(newFile->getSourceStream(std::ios_base::in | std::ios_base::out | std::ios_base::trunc));
    if (image == nullptr)
        return false;

    // Initialize Blocks
    image->initializeBlocks();

    // Initialize Block Allocation Map
    image->initializeBlockAllocationMap();

    // Initialize directory
    image->initializeDirectory();

    // Write the header to the file
    size_t comma = header_info.find(',');
    std::string diskname = header_info.substr(0,comma);
    std::string id = header_info.substr(comma+1);
    Debug_printv("write media header");
    Debug_printv("name[%s] id[%s]", diskname.c_str(), id.c_str());
    image->writeHeader(diskname, id);

    // Truncate the file to the desired size
    image->seek(size - 1);
    uint8_t data = 0x00;
    image->write(&data, 1);

    delete newFile;
    //delete image;
    return true;
}


bool D64MFile::rewindDirectory()
{
    dirIsOpen = true;
    //Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto image = ImageBroker::obtain<D64MStream>("d64", url);
    if (image == nullptr)
        return false;

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
    media_header = mstr::format("%.16s", image->header.name);
    mstr::A02Space(media_header);
    media_id = mstr::format("%.5s", image->header.id_dos);
    mstr::A02Space(media_id);
    media_blocks_free = image->blocksFree();
    media_block_size = image->block_size;
    media_image = name;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    return true;
}

MFile* D64MFile::getNextFileInDir()
{
    bool r = false;

    if (!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<D64MStream>("d64", url);
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
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (url + "/" + filename).c_str() );

        // Entry URL must include the in-image path (partition/subdirectory)
        std::string entryUrl;
        entryUrl.reserve(url.size() + pathInStream.size() + 2 + filename.size());
        entryUrl = url;
        if (pathInStream.size()) { entryUrl += '/'; entryUrl += pathInStream; }
        entryUrl += '/'; entryUrl += filename;
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
    auto stream = ImageBroker::obtain<D64MStream>("d64", url);
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
    auto stream = ImageBroker::obtain<D64MStream>("d64", url);
    if (stream != nullptr)
        return stream->resolvePath(pathInStream) == D64MStream::PATH_DIR;

    return false;
}

bool D64MFile::exists()
{
    //Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto stream = ImageBroker::obtain<D64MStream>("d64", url);
    if ( stream == nullptr )
        return false;

    // A path inside the image only exists if it resolves to a partition,
    // directory or file entry
    if ( pathInStream.size() && pathInStream != "/" )
        return stream->resolvePath(pathInStream) != D64MStream::PATH_NOT_FOUND;

    return true;
}

