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

// .D82 - This is a sector-for-sector copy of an 8250 floppy disk
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC363
// https://ist.uwaterloo.ca/~schepers/formats/D80-D82.TXT
//


#ifndef MEATLOAF_MEDIA_D82
#define MEATLOAF_MEDIA_D82

#include "../meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D82MStream : public D64MStream {
    // override everything that requires overriding here

public:
    D82MStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // D82 Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                38,     // track
                0,      // sector
                0x06,   // offset
                1,      // start_track
                50,     // end_track
                5       // byte_count
            },
            {
                38,     // track
                3,      // sector
                0x06,   // offset
                51,     // start_track
                100,    // end_track
                5       // byte_count
            },
            {
                38,     // track
                6,      // sector
                0x06,   // offset
                101,    // start_track
                150,    // end_track
                5       // byte_count
            },
            {
                38,     // track
                9,      // sector
                0x06,   // offset
                151,    // start_track
                154,    // end_track
                5       // byte_count
            }
        };

        Partition p = {
            39,    // track
            0,     // sector
            0x06,  // header_offset
            39,    // directory_track
            1,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 23, 25, 27, 29 };
    };

    virtual uint8_t speedZone(uint8_t track) override
    {
        if (track < 78)
            return (track < 40) + (track < 54) + (track < 65);
        else
            return (track < 117) + (track < 131) + (track < 142);
    };

protected:

private:
    friend class D82MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D82MFile: public D64MFile {
public:
    D82MFile(std::string path) : D64MFile(path) 
    {
        size = 1066496; // Default - 154 tracks no errors
    };

    MStream* getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return new D82MStream(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class D82MFileSystem: public MFileSystem
{
public:
    D82MFileSystem(): MFileSystem("d82") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        return byExtension(".d82", fileName);
    }

    MFile* getFile(std::string path) override {
        return new D82MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_D82 */
