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

// .D71 - 1571 disk image format
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC373
// https://ist.uwaterloo.ca/~schepers/formats/D71.TXT
//


#ifndef MEATLOAF_MEDIA_D71
#define MEATLOAF_MEDIA_D71

#include "meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D71MStream : public D64MStream {
    // override everything that requires overriding here

public:
    D71MStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // D71 Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                18,     // track
                0,      // sector
                0x04,   // offset
                1,      // start_track
                35,     // end_track
                4       // byte_count
            },
            {
                53,     // track
                0,      // sector
                0x00,   // offset
                36,     // start_track
                70,     // end_track
                3       // byte_count
            } 
        };

        Partition p = {
            18,    // header_track
            0,     // header_sector
            0x90,  // header_offset
            18,    // directory_track
            1,     // directory_sector
            0x00,  // directory_offset
            0,     // parent_header_track
            0,     // parent_header_sector
            0,     // parent_entry_track
            0,     // parent_entry_sector
            0,     // parent_entry_offset
            b     // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 17, 18, 19, 21 };
        interleave = { 3, 6 }; // Directory, File
        dos_rom = "dos1571";

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 349696: // 70 tracks no errors
                break;

            case 351062: // 70 w/ errors
                error_info = true;
                break;
        }
    };

    // On a 1571 the side-2 BAM is SPLIT: the per-track free-sector COUNTS for
    // tracks 36-70 live in the header sector (18/0) at offset 0xDD - one byte
    // per track, filling 0xDD..0xFF - while the matching BITMAPS live at 53/0.
    // BlockAllocationMap can only describe one contiguous run of bytes, so the
    // base class maintains the bitmap and D71 maintains the counts alongside it.
    static constexpr uint8_t SIDE2_FIRST_TRACK = 36;
    static constexpr uint8_t SIDE2_COUNT_OFFSET = 0xDD;

    bool writeSide2FreeCount( uint8_t track, uint8_t count )
    {
        if (!seekSector( partitions[partition].header_track,
                         partitions[partition].header_sector,
                         SIDE2_COUNT_OFFSET + (track - SIDE2_FIRST_TRACK) ))
            return false;
        return writeContainer(&count, 1) == 1;
    }

    bool setBlockAllocation( uint8_t track, uint8_t sector, bool allocate ) override
    {
        // The base class owns the bitmap. For side 1 that is the whole job.
        if (!D64MStream::setBlockAllocation(track, sector, allocate))
            return false;

        if (track < SIDE2_FIRST_TRACK)
            return true;

        // Side 2: mirror the change into the split-off count byte. getTrackFreeCount()
        // counts bits in the bitmap the base class just updated, so it is already
        // the post-change value - no need to re-derive it here.
        return writeSide2FreeCount(track, getTrackFreeCount(track));
    }

    bool initializeBlockAllocationMap() override
    {
        if (!D64MStream::initializeBlockAllocationMap())
            return false;

        // The base initializer only writes the records BlockAllocationMap
        // describes, so side 2's counts would be left as whatever was in 18/0.
        // Seed them from the bitmaps it just wrote.
        uint8_t last_track = partitions[partition].block_allocation_map.back().end_track;
        for (uint8_t t = SIDE2_FIRST_TRACK; t <= last_track; t++)
        {
            if (!writeSide2FreeCount(t, getTrackFreeCount(t)))
                return false;
        }

        // A 1571 reserves the side-2 BAM track (53) IN FULL, not just the one
        // sector the BAM occupies - unlike track 18, where only the two sectors
        // actually in use (18/0 header+BAM, 18/1 first directory block) are
        // allocated. Verified against c1541's own format: it writes 00 00 00 for
        // track 53 while track 18 reads 11 fc ff 07 (17 of 19 free), and reports
        // "1328 blocks free" = 1366 - 19 - 19, excluding both tracks whole.
        uint8_t bam2_track = partitions[partition].block_allocation_map.back().track;
        uint16_t bam2_sectors = getSectorCount(bam2_track);
        for (uint16_t s = 0; s < bam2_sectors; s++)
        {
            if (!isBlockFree(bam2_track, (uint8_t)s))
                continue;
            if (!setBlockAllocation(bam2_track, (uint8_t)s, true))
                return false;
        }
        return true;
    }

    virtual uint8_t speedZone( uint8_t track) override
    {
        // Track 35 is the LAST track of side 1 (17 sectors). `track < 35` sent
        // it into the side-2 branch below, which resolved to 21 sectors and
        // inflated the computed image size by 4 blocks.
        if ( track <= 35 )
		    return (track < 18) + (track < 25) + (track < 31);
        else
            return (track < 53) + (track < 60) + (track < 66);
    };

    uint32_t defaultImageSize() override { return 349696; }

protected:

private:
    friend class D71MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D71MFile: public D64MFile {
public:
    D71MFile(std::string path) : D64MFile(path) 
    {
        size = 349696; // Default - 70 tracks no errors
    };

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<D71MStream>(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class D71MFileSystem: public MFileSystem
{
public:
    D71MFileSystem(): MFileSystem("d71") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        return byExtension(".d71", fileName);
    }

    MFile* getFile(std::string path) override {
        return new D71MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_D71 */
