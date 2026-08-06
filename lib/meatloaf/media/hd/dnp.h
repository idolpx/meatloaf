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
            0,     // directory_sector
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
    // 4 tracks (256 KB) - large enough that data blocks are not confined to
    // the directory track, small enough to keep a full-image scan cheap: a DNP
    // track is 256 sectors, so every extra track costs 256 block reads in any
    // exhaustive check.
    uint32_t defaultImageSize() override { return 4 * 65536; };

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
