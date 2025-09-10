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

// .DSK - This is a byte for byte copy of a physical disk
//
// https://github.com/simonowen/samdisk/tree/main
// Apple - https://gswv.apple2.org.za/a2zine/faqs/Csa2FLUTILS.html#006
// Apple - http://fileformats.archiveteam.org/wiki/DSK_(Apple_II)
// Coleco Adam - https://retrocomputing.stackexchange.com/questions/15833/what-floppy-disk-format-and-layout-and-what-disk-image-format-are-used-for-the-c

#ifndef MEATLOAF_MEDIA_DSK
#define MEATLOAF_MEDIA_DSK

#include "meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class DSKIStream : public D64MStream {
    // override everything that requires overriding here

public:
    DSKIStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // DSK Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                40,     // track
                1,      // sector
                0x10,   // offset
                1,      // start_track
                40,     // end_track
                6       // byte_count
            }
        };

        Partition p = {
            35,    // track
            0,     // sector
            0x04,  // header_offset
            40,    // directory_track
            3,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 16 };
        has_subdirs = false;
        error_info = false;

        dos_rom = "";
        dos_name = "";

        // Read Header
        readHeader();

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            // Apple        // 35 Tracks, 16 sector/track, 256 bytes/sector
            case 146660:    
                break;

            // Coco         // 35 Tracks, 16 sector/track, 256 bytes/sector
            case 161280:
                break;

            // Apple        // 40 Tracks, 16 sector/track, 256 bytes/sector
            // Coleco Adam  // 40 Tracks, 8 sectors per track, 512 bytes per sectors
            case 163840:
                break;

            // Coco OS9
            case 184320:
                break;
        }
    };

    virtual uint8_t speedZone(uint8_t track) override { return 0; };

protected:

private:
    friend class DSKMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class DSKMFile: public D64MFile {
public:
    DSKMFile(std::string path, bool is_dir = true) : D64MFile(path, is_dir) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<DSKIStream>(is);
    }

};



/********************************************************
 * FS
 ********************************************************/

class DSKFileSystem: public MFileSystem
{
public:
    DSKFileSystem(): MFileSystem("dsk") {};

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".dsk",
                ".do",
                ".po",
                ".hdv"
            }, 
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new DSKMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_DSK */
