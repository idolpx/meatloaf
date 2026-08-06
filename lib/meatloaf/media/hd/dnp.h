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

// .DNP - CMD hard Disk Native Partition
//
// https://ist.uwaterloo.ca/~schepers/formats/D2M-DNP.TXT
//

#ifndef MEATLOAF_MEDIA_DNP
#define MEATLOAF_MEDIA_DNP

#include "meatloaf.h"
#include "../disk/d64.h"


/********************************************************
 * Streams
 ********************************************************/

class DNPMStream : public D64MStream {
    // override everything that requires overriding here

public:
    DNPMStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // DNP Partition Info
        std::vector<BlockAllocationMap> b = {
            {
                1,      // track
                2,      // sector
                0x20,   // offset
                1,      // start_track
                255,    // end_track
                32      // byte_count (256 sectors/track = 32 bitmap bytes, no count byte)
            }
        };

        Partition p = {
            1,     // track
            1,     // sector
            0x04,  // header_offset
            1,     // directory_track
            34,     // directory_sector
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
        sectorsPerTrack = { 256 };
        has_subdirs = true;

        // A CMD native partition has no dedicated directory track: the
        // directory is a plain chain that may extend onto any track, and file
        // data shares track 1 with it. On a 1-track partition that matters a
        // great deal - reserving track 1 would leave nowhere to put data.
        dedicated_directory_track = false;


        // Read this offset to get t/s link to start of directory
        seek(0x100);
        partitions[0].directory_track = read(); 
        partitions[0].directory_sector = read();

        // A blank container reads back 0/0, which is not a valid location - and
        // formatImage() would then lay the directory down on track 0. CMD native
        // partitions put it at 1/34: the BAM is 32 bytes per track for 255 tracks
        // starting at 1/2 offset 0x20, i.e. 544 + 8160 = 8704 bytes = exactly 34
        // sectors, so the directory begins right after it. That area is reserved
        // at full size regardless of how many tracks the partition actually has,
        // so 1/34 holds for every DNP.
        if (partitions[0].directory_track == 0)
        {
            partitions[0].directory_track = 1;
            partitions[0].directory_sector = 34;
        }

        // Calculate number of tracks based on file size
        uint32_t size = containerStream->size() / 65536;
        if ( containerStream->size() % 65536 != 0 )
            size++;
        partitions[0].block_allocation_map[0].end_track = size;
        Debug_printv("size[%d] tracks[%d]", size, partitions[0].block_allocation_map[0].end_track);
    }

    // A CMD native partition has no canonical size - it is whatever it was
    // created as, and the constructor derives the track count from the
    // container. This is only the size used when creating one from nothing.
    // One track (64 KB). A native partition is not capped at its creation size
    // - growImage() adds tracks on demand - so there is no reason to reserve
    // space up front.
    uint32_t defaultImageSize() override { return 65536; }

    // The system area of a CMD native partition, all on track 1:
    //
    //   1/0        autoboot sector
    //   1/1        partition info (the header)
    //   1/2..1/33  BAM - 255 tracks x 32 bytes starting at 1/2 offset 0x20 is
    //              8160 bytes, ending exactly at the start of sector 34
    //   1/34       first directory block
    //
    // Sectors 0..33 are reserved whatever the partition's size, because the BAM
    // is laid out for the maximum 255 tracks up front. 1/34 is allocated by
    // initializeDirectory() along with every other format's first directory
    // block, so it is not part of this range.
    static constexpr uint8_t SYSTEM_AREA_LAST_SECTOR = 33;

    bool isReservedBlock(uint8_t track, uint8_t sector) override
    {
        return track == partitions[partition].header_track &&
               sector <= SYSTEM_AREA_LAST_SECTOR;
    }

    bool initializeBlockAllocationMap() override
    {
        if (!D64MStream::initializeBlockAllocationMap())
            return false;

        // Reserve the whole system area. The generic code only knows to
        // allocate the FIRST sector of each BAM record, which would leave the
        // autoboot sector and 31 of the BAM's 32 sectors advertised as free.
        for (uint8_t s = 0; s <= SYSTEM_AREA_LAST_SECTOR; s++)
        {
            if (!isBlockFree(partitions[partition].header_track, s))
                continue;
            if (!setBlockAllocation(partitions[partition].header_track, s, true))
                return false;
        }
        return true;
    }

    // Add one track and mark it entirely free. This is what makes a CMD native
    // partition "native": it starts small and grows as files are written,
    // rather than being fixed at creation like a floppy image.
    //
    // The BAM area is reserved at full size (32 bytes per track for 255 tracks,
    // sectors 1/2..1/33) regardless of how many tracks exist, so a new track's
    // entry always lands inside space that is already allocated to the BAM -
    // no relocation, no risk of overrunning into the directory.
    bool growImage() override
    {
        auto &bam = partitions[partition].block_allocation_map[0];
        uint16_t new_track = (uint16_t)bam.end_track + 1;

        // 255 tracks is the ceiling the BAM can describe (~16 MB).
        if (new_track > 255)
            return false;

        // Extend the container by writing the last byte of the new track. A
        // seek alone does not grow a file; a write does.
        uint32_t new_size = (uint32_t)new_track * 65536;
        if (!containerStream->seek(new_size - 1))
            return false;
        uint8_t pad = 0x00;
        if (containerStream->write(&pad, 1) != 1)
            return false;

        bam.end_track = (uint8_t)new_track;

        // Mark every sector of the new track free. 256 sectors = 32 bitmap
        // bytes, all 0xFF; this record style carries no leading count byte.
        if (!seekSector(bam.track, bam.sector, 0))
            return false;
        uint16_t offset = bam.offset + (uint16_t)(new_track - bam.start_track) * bam.byte_count;
        if (!seekSector(bam.track, (uint8_t)(bam.sector + offset / block_size),
                        (uint8_t)(offset % block_size)))
            return false;
        std::vector<uint8_t> all_free(bam.byte_count, 0xFF);
        if (writeContainer(all_free.data(), all_free.size()) != all_free.size())
            return false;

        Debug_printv("grew DNP to %d tracks", (int)new_track);
        return true;
    };

    virtual uint8_t speedZone(uint8_t track) override { return 0; };

protected:

private:
    friend class DNPMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class DNPMFile: public D64MFile {
public:
    DNPMFile(std::string path) : D64MFile(path) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<DNPMStream>(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class DNPMFileSystem: public MFileSystem
{
public:
    DNPMFileSystem(): MFileSystem("dnp") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        return byExtension(".dnp", fileName);
    }

    MFile* getFile(std::string path) override {
        return new DNPMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_DNP */
