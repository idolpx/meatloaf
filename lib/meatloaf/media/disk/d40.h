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

// .D40, .D67 - 2040, 3040 disk image format
//
// http://www.baltissen.org/newhtm/diskimag.htm
//


#ifndef MEATLOAF_MEDIA_D40
#define MEATLOAF_MEDIA_D40

#include "meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D40MStream : public D64MStream {
    // override everything that requires overriding here

public:
    D40MStream(std::shared_ptr<MStream> is) : D64MStream(is)
    {
        // D40 Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                18,     // track
                0,      // sector
                0x04,   // offset
                1,      // start_track
                35,     // end_track
                4       // byte_count
            } 
        };

        Partition p = {
            18,    // track
            0,     // sector
            0x90,  // header_offset
            18,    // directory_track
            1,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 17, 18, 20, 21 };
        dos_rom = "dos2040";

        // Read Header
        readHeader();

        // this.size = data.media_data.length;
        // switch (this.size + this.media_header_size) {
        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 176640: // 35 tracks no errors
                break;

            case 175531: // 35 w/ errors
                error_info = true;
                break;
        }
    };

    uint8_t speedZone( uint8_t track) override
    {
        return (track < 18) + (track < 25) + (track < 31);
    };

protected:

private:
    friend class D40MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D40MFile: public D64MFile {
public:
    D40MFile(std::string path) : D64MFile(path) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<D40MStream>(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class D40MFileSystem: public MFileSystem
{
public:
    D40MFileSystem(): MFileSystem("d40") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".d40",
                ".d67"
            }, 
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new D40MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_D40 */
