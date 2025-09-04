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

#include "../meatloaf.h"
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
                0x10,   // offset
                1,      // start_track
                255,    // end_track
                8       // byte_count
            } 
        };

        Partition p = {
            1,     // track
            0,     // sector
            0x04,  // header_offset
            1,     // directory_track
            0,     // directory_sector
            0x20,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 256 };
        has_subdirs = true;

        // Read Header
        readHeader();
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
