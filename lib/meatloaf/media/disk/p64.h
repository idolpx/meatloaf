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
// https://github.com/markusC64/p64conv/blob/master/lib/p64refimp/p64tech.txt
// https://www.cbmstuff.com/forum/showthread.php?tid=70&pid=302
//

// Bit 1 of the flags is read only as written down in the VICE page. But there
// are other flags not mentioned there:
// * Bit 2 is high resolution, if set, the timing values aren't 16 MHz but 48
// MHz.

// You might wonder why 48 MHz? Well, the 16 MHz are from the internal hardware
// of the 1541. The CBM 8250 has 24 MHz instead. And the least common multiple is
// - yes, 48 MHz.

// If the floppy is the P64 file format is set to "8250", an implementation that
// handles multiple different CBM floppies should therefore assume 24 MHz resp.
// 48 MHz. It's extension would be P82. Or D82 for the sector dump.

// P81 obviously has only the double sided case :-)

#ifndef MEATLOAF_MEDIA_P64
#define MEATLOAF_MEDIA_P64

#include "meatloaf.h"
#include "d64.h"

#include <map>

// .P64 - NRZI flux pulse level disk image
//
// A P64 stores no sectors and no GCR bytes. It stores the raw magnetic flux
// transitions of each half track, range-coded, so it can carry the timing
// tricks copy protections rely on. Getting a file out of one is therefore
// three decodes stacked:
//
//   range-coded chunk  ->  flux pulses  ->  GCR bitstream  ->  CBM sector
//
// The last two steps are what a real 1541's read logic does in hardware, so
// the GCR bitstream this produces is NOT byte-aligned - a sector is found by
// scanning for a sync (ten or more 1 bits) at BIT resolution, which is the one
// substantial difference from g64.h, where the container already holds aligned
// GCR bytes.
//
// Only side 0 is read, and only strong pulses (strength >= 0x80000000) trigger,
// exactly as the reference implementation does. Weak-bit protection tracks are
// therefore expected to decode partially or not at all; the CBM-DOS-formatted
// tracks a directory and a normal file live on are unaffected.
//
// Read-only. Decoding one track costs a ~1 MB probability table plus the
// compressed chunk, both transient, so this effectively requires PSRAM - the
// same trade the tape decoder makes.
//
// File layout (all values little endian):
//
//   0x00  8   signature "P64-1541"
//   0x08  4   version (0)
//   0x0C  4   flags (bit 0 = write protected)
//   0x10  4   size of the chunk stream that follows
//   0x14  4   CRC32 of the chunk stream
//   0x18      chunk stream
//
// Each chunk is a 4-byte signature, a 4-byte data size and a 4-byte CRC32,
// followed by that many bytes of data. "HTPx" carries half track x & 0x7F of
// side (x >> 7); its data is a 4-byte pulse count, a 4-byte range-coded byte
// count, and the range-coded pulses. "DONE" (size 0) ends the stream.

#define P64_HEADER_SIZE         0x18
#define P64_CHUNK_HEADER_SIZE   0x0C
#define P64_FIRST_HALF_TRACK    2
#define P64_LAST_HALF_TRACK     85

// (16 MHz * 60) / 300 = 3200000 samples per rotation at 300 RPM
#define P64_SAMPLES_PER_ROTATION 3200000

// How much of the NEXT rotation is decoded past the end of the first, so that a
// sector straddling the image's rotation seam is contiguous somewhere in the
// buffer. A header plus its gap plus a data block is about 350 GCR bytes.
#define P64_OVERLAP_BYTES       512

// Longest GCR bitstream a rotation can produce is at speed zone 3, where a bit
// cell is 4 * (16 - 3) = 52 cycles: 3200000 / 52 = 61539 bits = 7693 bytes,
// plus the overlap.
#define P64_MAX_TRACK_BYTES     (7936 + P64_OVERLAP_BYTES)


/********************************************************
 * Streams
 ********************************************************/

class P64MStream : public D64MStream {

public:
    P64MStream(std::shared_ptr<MStream> is) : D64MStream(is) {};

    bool readHeader() override;

    bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) override;
    using D64MStream::seekSector;

    uint32_t readContainer(uint8_t *buf, uint32_t size) override;

    // Read-only, and this is where that is enforced rather than assumed.
    // D64MStream's whole write path - new files, directory entries, BAM
    // updates, format - reaches the container through writeContainer() and
    // nothing else, and it addresses the container as a linear .d64. A P64 is a
    // chunk stream, so a single such write lands at an arbitrary offset inside
    // the range-coded pulse data and destroys the image. MFile::isWritable is
    // no defence: MFSOwner::File() copies it from the container, so a .p64 on
    // an SD card inherits true.
    uint32_t writeContainer(uint8_t *buf, uint32_t size) override
    {
        (void)buf; (void)size;
        Debug_printv("P64 images are read-only");
        return 0;
    }

protected:
    // Where one HTPx chunk's range-coded pulse data sits in the container.
    struct HalfTrack {
        uint32_t offset = 0;    // container offset of the range-coded data
        uint32_t size = 0;      // its length in bytes
        uint32_t pulses = 0;    // pulse count the chunk header claims
    };

    // Walks the chunk stream once, recording every side-0 HTPx chunk. Repeated
    // calls are free - a directory listing calls readHeader() on every rewind
    // and each walk is a seek plus a read per chunk, which over the network is
    // a request per chunk.
    bool parseChunks();

    // Decodes one track's pulses into gcr_track. Caches the last track, since
    // reading a file walks its block chain within a track before leaving it.
    bool decodeTrack( uint8_t track );

    // Finds sector on the decoded track and fills sector_buffer with its 256
    // bytes (link bytes included, as the D64 layer expects).
    bool loadSector( uint8_t track, uint8_t sector );

    // Bit-resolution scan for a sync mark, starting at bit p and giving up
    // after limit bits. Returns the bit position of the first non-sync bit, or
    // -1. Wraps at the end of the track, since a track is a loop.
    int findSync( int p, int limit ) const;

    // Decodes groups * 5 GCR bytes starting at bit p into groups * 4 plain
    // bytes, wrapping at the end of the track.
    void decodeBlock( int p, uint8_t *buf, int groups ) const;

    std::map<uint8_t, HalfTrack> half_tracks;
    bool chunks_parsed = false;
    bool chunks_ok = false;

    std::vector<uint8_t> gcr_track;
    uint32_t gcr_track_bytes = 0;
    int cached_track = -1;

    // Whether the sector loadSector() last served had a valid data checksum.
    // The bytes are handed over either way - an original disk can carry
    // deliberately bad checksums and the caller has no error channel - so this
    // is how anything that wants to know finds out.
    bool last_data_checksum_ok = true;

    // A header naming a different track than the one that was decoded is worth
    // saying out loud once, not once per sector: a directory listing reads 19
    // of them, and on an image where the mapping really is off that would be 19
    // identical lines every time the directory is opened.
    bool logged_track_mismatch = false;

    uint8_t sector_buffer[256] = { 0 };
    uint16_t sector_pos = 0;

private:
    friend class P64MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class P64MFile: public D64MFile {
public:
    P64MFile(std::string path) : D64MFile(path) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        return std::make_shared<P64MStream>(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class P64MFileSystem: public MFileSystem
{
public:
    P64MFileSystem(): MFileSystem("p64") {};

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".p64"
            },
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new P64MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_P64 */
