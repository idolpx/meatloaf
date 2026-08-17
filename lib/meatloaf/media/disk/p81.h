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

#ifndef MEATLOAF_MEDIA_P81
#define MEATLOAF_MEDIA_P81

#include "p64.h"

// .P81 - NRZI flux pulse level image of a 1581 disk
//
// Same container as a .p64 - the header, the chunk stream and the range-coded
// pulse encoding are identical, and all of that is inherited. What is NOT the
// same is everything downstream of the pulses:
//
//                        .p64 (1541)              .p81 (1581)
//   encoding             GCR                      MFM
//   read logic           1541 clock/counter        fixed 2 us cell
//   sync                 ten or more 1 bits        $4489 (A1 with a
//                                                  missing clock bit) x3
//   integrity            XOR checksum              CRC-16/CCITT
//   sides                one                       two
//   half stepping        yes, half track = t * 2   no, cylinder = ht - 2
//   speed zones          four                      one
//   sector               256 bytes                 512 bytes, split into two
//                                                  256-byte CBM blocks
//
// So this is not a geometry override - the flux-to-bits half of the decoder is
// replaced (emitDelta) and so is the sector search (loadSector). What IS reused
// is the expensive and fiddly part: the chunk walk, the range decoder, the
// rotation-seam overlap replay and the one-track cache.
//
// All of the above was established by measurement rather than assumption. The
// pulse spacings on a real image are 4.00, 6.00 and 8.00 us - the 2T/3T/4T of
// double-density MFM at a 2 us cell - and not the 1541's GCR timing.
//
// Read-only, like .p64.

// A 1581 runs 250 kbit/s MFM: 500000 cells/s, so one cell is 2 us, which at the
// P64 sample clock of 16 MHz is 32 samples.
#define P81_CELL_SAMPLES        32

// One rotation is 3200000 / 32 = 100000 cells = 12500 bytes. The overlap has to
// exceed a whole sector - a 512-byte MFM sector with its address mark and gap is
// about 1150 bytes of cells - so that a sector sitting on the rotation seam is
// contiguous somewhere in the buffer.
#define P81_OVERLAP_BYTES       2048
#define P81_MAX_TRACK_BYTES     (12800 + P81_OVERLAP_BYTES)

// The raw MFM cell pattern of an $A1 sync byte with its missing clock bit.
#define P81_SYNC_PATTERN        0x4489

// IBM System 34 address marks.
#define P81_MARK_ID             0xFE
#define P81_MARK_DATA           0xFB
#define P81_MARK_DELETED_DATA   0xF8

// A 1581 formats ten 512-byte sectors per side of a cylinder, numbered from 1.
#define P81_SECTOR_BYTES        512

// The chunk whose half track byte is 2 holds cylinder 0.
#define P81_FIRST_CYLINDER_HT   2


/********************************************************
 * Streams
 ********************************************************/

class P81MStream : public P64MStream {

public:
    P81MStream(std::shared_ptr<MStream> is);

    bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) override;
    using P64MStream::seekSector;

    // One zone: a 1581 spins every track at the same rate, which is the whole
    // reason its tracks all hold the same ten sectors.
    uint8_t speedZone( uint8_t track ) override { return 0; }

    uint32_t defaultImageSize() override { return 819200; }

protected:
    const char *imageSignature() const override { return "P64-1581"; }

    uint32_t trackBufferBytes() const override { return P81_MAX_TRACK_BYTES; }
    uint32_t overlapBytes() const override { return P81_OVERLAP_BYTES; }

    // MFM has no read-logic state to keep - a flux gap IS the cell count.
    void resetEmitState( uint8_t track ) override;
    void emitDelta( uint32_t delta ) override;

    // Decodes the cylinder/head the CBM track and sector live on, then finds the
    // physical sector and copies the requested half of it into sector_buffer.
    bool loadSector( uint8_t track, uint8_t sector ) override;

    // Finds the next $4489 sync at or after cell p, returning the cell index
    // just past it, or -1. Wraps like the GCR scan does.
    int findMfmSync( int p, int limit ) const;

    // Finds the physical 512-byte sector and leaves it in physical_sector.
    bool readPhysicalSector( uint8_t cylinder, uint8_t head, uint8_t physical );

    // Reads `count` MFM data bytes starting at cell p. The data bit is the
    // second cell of each pair; the first is the clock.
    bool readMfmBytes( int p, uint8_t *buf, int count ) const;

    // The 512-byte physical sector currently decoded, and which one it is.
    uint8_t physical_sector[P81_SECTOR_BYTES] = { 0 };
    int cached_physical = -1;   // (cylinder << 8) | (head << 4) | sector, or -1

private:
    friend class P81MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class P81MFile: public D64MFile {
public:
    P81MFile(std::string path) : D64MFile(path)
    {
        size = 819200; // 80 tracks, both sides
    };

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        return std::make_shared<P81MStream>(is);
    }

    bool mkDir() override { return false; };
    bool rmDir() override { return false; };
};


/********************************************************
 * FS
 ********************************************************/

class P81MFileSystem: public MFileSystem
{
public:
    P81MFileSystem(): MFileSystem("p81") {};

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".p81"
            },
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new P81MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_P81 */
