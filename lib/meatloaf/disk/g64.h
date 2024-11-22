// .G64 - The G64 G64-encoded disk image format
//
// https://vice-emu.sourceforge.io/vice_16.html#SEC398
// https://ist.uwaterloo.ca/~schepers/formats/G64.TXT
// http://www.linusakesson.net/programming/gcr-decoding/index.php
// https://www.pagetable.com/?p=1356
// http://www.baltissen.org/newhtm/1541c.htm
//

#ifndef MEATLOAF_MEDIA_G64
#define MEATLOAF_MEDIA_G64

#include "../meatloaf.h"
#include "d64.h"

#include "endianness.h"

// Format codes:
// ID	Description
// 0	Unknown format
// 1	G64 Data
// 2	CBM DOS
// 3	CBM DOS Extended
// 4	MicroProse
// 5	RapidLok
// 6	Datasoft
// 7	Vorpal
// 8	V-MAX!
// 9	Teque
// 10	TDP
// 11	Big Five
// 12	OziSoft

// Format Extensions:
// ID	Description
// 0	Unknown protection
// 1	Datasoft with Weak bits
// 2	CBM DOS with Cyan loader, Weak bits
// 3	CBM DOS with Datasoft, Weak bits
// 4	RapidLok Key
// 5	Data Duplication
// 6	Melbourne House
// 7	Melbourne House, Weak bits
// 8	PirateBusters v1.0
// 9	PirateBusters v2.0, Track A
// 10	PirateBusters v2.0, Track B
// 11	PirateSlayer
// 12	CBM DOS, XEMAG


#define TRACK_TABLE_OFFSET 0x000C
#define SPEED_ZONE_OFFSET  0x015C


/********************************************************
 * Streams
 ********************************************************/

class G64MStream : public D64MStream {
    // override everything that requires overriding here

protected:
    struct MediaHeader {
        char signature[8];
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
    G64MStream(std::shared_ptr<MStream> is) : D64MStream(is) 
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
    friend class G64MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class G64MFile: public D64MFile {
public:
    G64MFile(std::string path, bool is_dir = true) : D64MFile(path, is_dir) {};

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        return new G64MStream(containerIstream);
    }
};



/********************************************************
 * FS
 ********************************************************/

class G64MFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new G64MFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".g41",
                ".g64"
             },
            fileName
        );
    }

    G64MFileSystem(): MFileSystem("G64") {};
};


#endif /* MEATLOAF_MEDIA_G64 */
