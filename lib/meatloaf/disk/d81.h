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

// .D81 - This is a byte for byte copy of a physical 1581 disk
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC354
// https://ist.uwaterloo.ca/~schepers/formats/D81.TXT
//


#ifndef MEATLOAF_MEDIA_D81
#define MEATLOAF_MEDIA_D81

#include "../meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D81MStream : public D64MStream {
    // override everything that requires overriding here

public:
    D81MStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // D81 Partition Info
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
                0,      // sector
                0x10,   // offset
                41,     // start_track
                80,     // end_track
                6       // byte_count
            } 
        };

        Partition p = {
            40,    // track
            0,     // sector
            0x04,  // header_offset
            40,    // directory_track
            3,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 40 };
        has_subdirs = true;

        dos_rom = "dos1581";

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 819200:  // 80 tracks no errors
                break;

            case 822400:  // 80 w/ errors
                error_info = true;
                break;

            // https://sourceforge.net/p/vice-emu/bugs/1890/
            case 829440:  // 81 tracks no errors
                partitions[partition].block_allocation_map[1].end_track = 81;
                break;
        }
    };

    virtual uint8_t speedZone(uint8_t track) override { return 0; };

protected:

private:
    friend class D81MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D81MFile: public D64MFile {
public:
    D81MFile(std::string path) : D64MFile(path) 
    {
        size = 819200; // Default - 80 tracks no errors
    };

    MStream* getDecodedStream(std::shared_ptr<MStream> is) override
    {
        //Debug_printv("[%s]", url.c_str());

        return new D81MStream(is);
    }

    bool mkDir() override { return false; };
    bool rmDir() override { return false; };
};



/********************************************************
 * FS
 ********************************************************/

class D81MFileSystem: public MFileSystem
{
public:
    D81MFileSystem(): MFileSystem("d81") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        return byExtension(".d81", fileName);
    }

    MFile* getFile(std::string path) override {
        return new D81MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_D81 */
