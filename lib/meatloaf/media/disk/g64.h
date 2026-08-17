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

// .G64 - The G64 G64-encoded disk image format
//
// https://vice-emu.sourceforge.io/vice_16.html#SEC398
// https://ist.uwaterloo.ca/~schepers/formats/G64.TXT
// http://www.linusakesson.net/programming/gcr-decoding/index.php
// https://www.pagetable.com/?p=1356
// http://www.baltissen.org/newhtm/1541c.htm
// https://csdb.dk/forums/?roomid=11&topicid=94092
//

#ifndef MEATLOAF_MEDIA_G64
#define MEATLOAF_MEDIA_G64

#include "meatloaf.h"
#include "d64.h"

#include "endianness.h"

#include <cstring>

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

        // // Read Header
        // readHeader();

        // containerStream->read((uint8_t*)&gcr_header, sizeof(gcr_header));

        // Debug_printv("signature[%s] version[%d] track_count[%d] track_size[%d]", gcr_header.signature, gcr_header.version, gcr_header.track_count, gcr_header.track_size);
    };

    MediaHeader gcr_header;
    SectorHeader gcr_sector_header;

    // The 8-byte signature this stream accepts. A .g71 is the same container
    // with a different signature and a 1571's geometry - see g71.h.
    virtual const char *imageSignature() const { return "GCR-1541"; }

    bool readHeader() override
    {
        // Read and validate the container header BEFORE delegating.
        // D64MStream::readHeader() immediately seeks 18/0, which goes through
        // seekSector() and the track table, so a file that is not a G64 at all
        // would otherwise attempt a sector decode before anything rejected it.
        //
        // This also used to read gcr_header from wherever the stream happened
        // to be left AFTER that delegation rather than from offset 0, so the
        // values it logged were never the header's.
        uint8_t buf[12];
        if (!containerStream->seek(0) || containerStream->read(buf, sizeof(buf)) != sizeof(buf))
        {
            Debug_printv("cannot read the container header");
            return false;
        }

        if (std::memcmp(buf, imageSignature(), 8) != 0)
        {
            Debug_printv("not a %s image", imageSignature());
            return false;
        }

        std::memcpy(gcr_header.signature, buf, 8);
        gcr_header.version = buf[8];
        gcr_header.track_count = buf[9];
        gcr_header.track_size = (uint16_t)(buf[10] | (buf[11] << 8));

        //Debug_printv("signature[%.8s] version[%d] track_count[%d] track_size[%d]", gcr_header.signature, gcr_header.version, gcr_header.track_count, gcr_header.track_size);

        return D64MStream::readHeader();
    }

    bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) override;

    uint32_t readContainer(uint8_t *buf, uint32_t size) override;

    // Read-only, enforced here rather than assumed. D64MStream's whole write
    // path reaches the container through writeContainer() and addresses it as a
    // linear .d64; a G64 is a GCR bitstream with a track table, so any such
    // write lands at an arbitrary offset in the encoded data. MFile::isWritable
    // is no defence - MFSOwner::File() copies it from the container, so a .g64
    // on an SD card inherits true. Covers .g71 too.
    uint32_t writeContainer(uint8_t *buf, uint32_t size) override
    {
        (void)buf; (void)size;
        Debug_printv("G64/G71 images are read-only");
        return 0;
    }

    bool readSectorHeader();
    bool readSector();
    bool findSync(uint32_t gcr_end);
    int convert4BytesFromGCR(uint8_t * gcr, uint8_t * plain);

protected:
    uint8_t sector_buffer[260];

    // Read cursor WITHIN sector_buffer. It cannot be _position: that is the
    // position in the FILE being read, and D64MStream::readFile() calls
    // readContainer() repeatedly between seekSector() calls, so indexing a
    // 260-byte sector buffer by it walks off the end on the second block of
    // any file.
    uint16_t sector_pos = 0;

private:
    friend class G64MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class G64MFile: public D64MFile {
public:
    G64MFile(std::string path) : D64MFile(path) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        return std::make_shared<G64MStream>(is);
    }

    bool rewindDirectory() override {

        auto image = ImageBroker::obtain<D64MStream>("g64", url);
        if (image == nullptr)
            return false;

        // Read Header
        image->readHeader();

        return D64MFile::rewindDirectory();
    }
};



/********************************************************
 * FS
 ********************************************************/

class G64MFileSystem: public MFileSystem
{
public:
    G64MFileSystem(): MFileSystem("g64") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".g41",
                ".g64"
            },
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new G64MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_G64 */
