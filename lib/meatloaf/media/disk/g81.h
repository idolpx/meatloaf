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

//
// Differences of G81 to G64:
//
// * No speed zone table any more
// * Obvioulsly larger track size in header
// * "MFM-1581" instead of "GCR-1541" in the Header
// * On each track 1st 4 bytes are the size instead of 1st 2 bytes. In contrast
// to G64 the unit is Bits, not Bytes.
//

#ifndef MEATLOAF_MEDIA_G81
#define MEATLOAF_MEDIA_G81

#include "d81.h"
#include "mfm.h"

#include <vector>

// .G81 - an MFM bitstream image of a 1581 disk
//
// The container is a G64 with three changes, all of them from the note above:
// a different signature, no speed zone table (a 1581 has one data rate), and a
// four-byte per-track length that counts BITS rather than the two-byte byte
// count a .g64 carries.
//
// The bitstream itself is MFM, so everything downstream of it - the $4489 sync,
// the address marks, CRC-16, the 512-byte sectors - is the same machinery a
// .p81 uses, and is shared through mfm.h. The two formats differ only in how
// the cell bitstream is obtained: a .p81 decodes it from flux pulses, a .g81
// reads it out of the container.
//
// ** VERIFICATION STATUS - read this before trusting the layout. **
//
// There is no .g81 anywhere in .archive, VICE has no MFM-1581 support at all
// (`grep MFM-1581 lib/vdrive` finds nothing), and the P64 reference
// implementation does not know the format either. The four bullets above are
// the entire specification this is written from. Two of them cannot be checked
// without a real image:
//
//   * where the track data starts, which follows from "no speed zone table"
//     but is not stated outright, and
//   * that the per-track prefix is four bytes.
//
// The fixture the tests run against encodes the SAME reading of that note, so a
// passing test proves the decoder and the generator agree - not that either
// agrees with a real .g81. What can be self-checked is the unit: an MFM 1581
// track is about 100000 cells, so a length field read as bits lands near that
// figure while one misread as bytes lands near 12500 and finds no sectors at
// all. If a real .g81 ever turns up and the base offset is wrong, every track
// fails at once and loudly, which is the right way for this to break.
//
// Read-only.

#define G81_HEADER_SIZE         0x0C

// One cell is 2 us at the 1581's 250 kbit/s MFM rate, so a 300 RPM rotation is
// 100000 cells = 12500 bytes. The buffer allows for a long track.
#define G81_MAX_TRACK_BYTES     16384

#define G81_SECTOR_BYTES        512


/********************************************************
 * Streams
 ********************************************************/

class G81MStream : public D81MStream {

public:
    G81MStream(std::shared_ptr<MStream> is) : D81MStream(is) {};

    bool readHeader() override;

    bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) override;
    using D81MStream::seekSector;

    uint32_t readContainer(uint8_t *buf, uint32_t size) override;

    // Read-only. D81MStream's write path addresses the container as a linear
    // .d81, which on a bitstream lands in the middle of encoded cells, and
    // MFile::isWritable inherits true from the container.
    uint32_t writeContainer(uint8_t *buf, uint32_t size) override
    {
        (void)buf; (void)size;
        Debug_printv("G81 images are read-only");
        return 0;
    }

protected:
    bool parseHeader();

    // Reads one cylinder/head's cells out of the container into mfm_track.
    // Caches the last one, since a file's block chain walks a track before
    // leaving it.
    bool loadTrack( uint8_t cylinder, uint8_t head );

    // Finds the physical 512-byte sector and leaves it in physical_sector.
    bool readPhysicalSector( uint8_t cylinder, uint8_t head, uint8_t physical );

    bool header_parsed = false;
    bool header_ok = false;
    uint8_t half_track_count = 0;
    uint16_t max_track_size = 0;

    std::vector<uint8_t> mfm_track;
    uint32_t mfm_track_bytes = 0;
    int cached_cylinder_head = -1;

    uint8_t physical_sector[G81_SECTOR_BYTES] = { 0 };
    int cached_physical = -1;

    bool last_data_checksum_ok = true;

    uint8_t sector_buffer[256] = { 0 };
    uint16_t sector_pos = 0;

private:
    friend class G81MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class G81MFile: public D64MFile {
public:
    G81MFile(std::string path) : D64MFile(path)
    {
        size = 819200; // 80 tracks, both sides
    };

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        return std::make_shared<G81MStream>(is);
    }

    bool mkDir() override { return false; };
    bool rmDir() override { return false; };
};


/********************************************************
 * FS
 ********************************************************/

class G81MFileSystem: public MFileSystem
{
public:
    G81MFileSystem(): MFileSystem("g81") {};

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".g81"
            },
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new G81MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_G81 */
