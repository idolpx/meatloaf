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

// .ATR - Atari disk image format
//
// https://github.com/jhallen/atari-tools
//


#ifndef MEATLOAF_MEDIA_ATR
#define MEATLOAF_MEDIA_ATR

#include "../meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class ATRMStream : public D64MStream {
    // override everything that requires overriding here

public:
    ATRMStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // ATR Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                1,      // track
                0,      // sector
                0x00,   // offset
                1,      // start_track
                40,     // end_track
                4       // byte_count
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
        sectorsPerTrack = { 18 };
        
        block_size = 128;
        media_header_size = 0x0F; // 16 byte .atr header
        media_data_offset = 0x0F;

        dos_rom = "a8b-dos20s";

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 92176:  // DOS 2.0s single density
                break;   // 16 byte .atr header + 40 tracks * 18 sectors per track * 128 bytes per sector

            case 133136: // DOS 2.5 enhanced density
                sectorsPerTrack = { 26 };
                break;   // 16 byte .atr header + 40 tracks * 26 sectors per track * 128 bytes per sector

            case 183952: // DOS 2.0d double density
                block_size = 256;
                break;   // 16 byte .atr header + 40 track * 18 sectors per track * 256 bytes per sector - 384 bytes because first three sectors are short
        }
    };

    virtual uint8_t speedZone( uint8_t track) override { return 0; };

protected:

private:
    friend class ATRMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class ATRMFile: public D64MFile {
public:
    ATRMFile(std::string path, bool is_dir = true) : D64MFile(path, is_dir) 
    {
        media_block_size = 128;
        size = 92176; // Default - 16 byte .atr header + 40 tracks * 18 sectors per track * 128 bytes per sector
    };

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<ATRMStream>(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class ATRMFileSystem: public MFileSystem
{
public:
    ATRMFileSystem(): MFileSystem("atr") {};

    bool handles(std::string fileName) override {
        return byExtension(".atr", fileName);
    }

    MFile* getFile(std::string path) override {
        return new ATRMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_ATR */
