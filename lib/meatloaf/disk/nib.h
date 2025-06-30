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

// .NIB - Commodore 1541/1571 nibbler disk image
//
// https://github.com/markusC64/nibtools
// 

#ifndef MEATLOAF_MEDIA_NIB
#define MEATLOAF_MEDIA_NIB

#include "../meatloaf.h"
#include "d64.h"

#include "endianness.h"

#define NIB_TRACK_LENGTH 0x2000
#define NIB_HEADER_SIZE 0xFF


/********************************************************
 * Streams
 ********************************************************/

class NIBMStream : public D64MStream {
    // override everything that requires overriding here

protected:
    struct MediaHeader {
        char signature[13];
        uint8_t version;
        uint8_t track_count;
        uint16_t track_size;
    };

    struct SectorHeader {
        uint8_t code; // 0x08
        uint8_t checksum;
        uint8_t sector;
        uint8_t track;
        uint8_t id1;
        uint8_t id0;
    };

public:
    NIBMStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // G64 Offsets
        //directory_header_offset = {18, 0, 0x90};
        //directory_list_offset = {18, 1, 0x00};
        //block_allocation_map = { {18, 0, 0x04, 1, 35, 4}, {53, 0, 0x00, 36, 70, 3} };
        //sectorsPerTrack = { 17, 18, 19, 21 };

        containerStream->read((uint8_t*)&gcr_header, sizeof(gcr_header));

        Debug_printv("signature[%s] version[%d] track_count[%d] track_size[%d]", gcr_header.signature, gcr_header.version, gcr_header.track_count, gcr_header.track_size);
    };

    MediaHeader gcr_header;
    SectorHeader gcr_sector_header;

    bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) override;

    uint32_t readContainer(uint8_t *buf, uint32_t size) override;

    bool readSectorHeader();
    bool readSector();
    bool findSync(uint32_t gcr_end);
    int convert4BytesFromGCR(uint8_t * gcr, uint8_t * plain);

protected:
    uint8_t sector_buffer[260];

private:
    friend class NIBMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class NIBMFile: public D64MFile {
public:
    NIBMFile(std::string path) : D64MFile(path) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        return std::make_shared<NIBMStream>(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class NIBMFileSystem: public MFileSystem
{
public:
    NIBMFileSystem(): MFileSystem("nib") {};

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".nib",
                ".nb2",
                ".nbz"
             },
            fileName
        );
    }

    MFile *getFile(std::string path) override {
        return new NIBMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_NIB */
