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

#include "../meatloaf.h"
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
            1,     // track
            0,     // sector
            0x04,  // header_offset
            1,     // directory_track
            4,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 17, 18, 19, 21 };

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

    virtual uint8_t speedZone( uint8_t track) override
    {
        if ( track < 35 )
		    return (track < 18) + (track < 25) + (track < 31);
        else
            return (track < 53) + (track < 60) + (track < 66);
    };

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
