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
// A raw GCR capture taken off a real drive over a parallel cable, rather than a
// reconstruction like a .g64. Layout:
//
//   0x00  13  "MNIB-1541-RAW"
//   0x0D  1   version
//   0x0E  2   (unused)
//   0x10  ..  track table, two bytes per stored track: half track index, then
//             density. Ends at the first entry whose half track byte is zero.
//   0x100 ..  the tracks themselves, in table order
//
// A NIB track is a fixed-size window with NO length field - unlike a .g64,
// which stores one. The nibbler fills what it captured and whatever follows is
// padding or noise, so the whole window is searchable and the header checksum
// is what rejects the junk. Do not invent a length.
//
// A .nb2 is the same container holding SEVERAL passes of each track, one after
// another. Nothing in the header says how many, so the stride is derived from
// the file length rather than assumed - which means one reader handles both.
// Only the first pass is used; the rest exist so a copier can compare them.
//
// A .nbz is a COMPRESSED .nib. The container is identified by content rather
// than by extension: a gzip magic number means inflate it first, and the
// MNIB signature is accepted at offset 0 or offset 1 (the latter is what
// MFileSystem::byContent() has always claimed for .nbz). Anything else is
// refused rather than guessed at.
//
// Read-only.

#ifndef MEATLOAF_MEDIA_NIB
#define MEATLOAF_MEDIA_NIB

#include "meatloaf.h"
#include "d64.h"

#include "endianness.h"

#include <cstring>
#include <vector>

#define NIB_TRACK_LENGTH  0x2000
#define NIB_HEADER_SIZE   0x100
#define NIB_TABLE_OFFSET  0x10
#define NIB_MAX_TRACKS    84


/********************************************************
 * Streams
 ********************************************************/

class NIBMStream : public D64MStream {

protected:
    struct SectorHeader {
        uint8_t code; // 0x08
        uint8_t checksum;
        uint8_t sector;
        uint8_t track;
        uint8_t id1;
        uint8_t id0;
    };

public:
    NIBMStream(std::shared_ptr<MStream> is) : D64MStream(is) {};

    bool readHeader() override;

    bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) override;
    using D64MStream::seekSector;

    uint32_t readContainer(uint8_t *buf, uint32_t size) override;

    // Read-only. D64MStream's write path addresses the container as a linear
    // .d64, which on a raw GCR capture lands in the middle of encoded data, and
    // MFile::isWritable inherits true from the container.
    uint32_t writeContainer(uint8_t *buf, uint32_t size) override
    {
        (void)buf; (void)size;
        Debug_printv("NIB images are read-only");
        return 0;
    }

    SectorHeader gcr_sector_header;

protected:
    // Reads the signature and the track table, and works out the per-track
    // stride. Idempotent - readHeader() runs on every directory rewind.
    bool parseHeader();

    // Copies one track's GCR window into track_buffer. Caches the last one,
    // since a file's block chain walks a track before leaving it. This is also
    // why the sync scan below works on RAM: it used to read ONE BYTE at a time
    // straight from the container, which over a network is thousands of range
    // requests per sector.
    bool loadTrack( uint8_t track );

    // Finds sector on the loaded track and fills sector_buffer with its 256
    // bytes (link bytes included, as the D64 layer expects).
    bool loadSector( uint8_t track, uint8_t sector );

    // Byte-wise scan for a sync mark starting at byte p, returning the offset
    // of the first non-sync byte or -1. A nibbler captures through the drive's
    // own sync detector, so the bytes it stores are aligned to sync marks - the
    // same assumption g64.cpp makes about its container.
    int findSync( int p ) const;

    // Decodes groups * 5 GCR bytes at offset p into groups * 4 plain bytes.
    bool decodeBlock( int p, uint8_t *buf, int groups ) const;

    bool header_parsed = false;
    bool header_ok = false;

    // Where each half track's window starts, or 0 for one the image does not
    // carry. Indexed by half track (track * 2).
    uint32_t track_offset[NIB_MAX_TRACKS * 2 + 2] = { 0 };

    // Distance between one stored track and the next. A .nib stores one pass
    // per track, so this is NIB_TRACK_LENGTH; a .nb2 stores several, and the
    // stride is derived from the file rather than assumed - see parseHeader().
    uint32_t track_stride = NIB_TRACK_LENGTH;

    // Byte offset of the container proper. Zero for a plain .nib, one for the
    // variant whose signature starts a byte in.
    uint32_t base_offset = 0;

    // A .nbz is inflated wholesale - gzip has no random access - and then read
    // from memory. Empty for an uncompressed image, which streams as before.
    std::vector<uint8_t> image_buffer;
    bool inflated = false;

    // Reads from the inflated buffer when there is one, from the container
    // otherwise. Offsets are relative to base_offset either way.
    bool imageRead( uint32_t offset, uint8_t *buf, uint32_t len );
    uint32_t imageSize();

    // Inflates a gzip container into image_buffer. Refuses anything larger than
    // a .nib can legitimately be: a .nbz that expands past that is a compressed
    // .nb2, and "not supported" is a better answer than exhausting the heap.
    bool inflateContainer();

    std::vector<uint8_t> track_buffer;
    uint32_t track_bytes = 0;
    int cached_track = -1;

    bool last_data_checksum_ok = true;

    uint8_t sector_buffer[256] = { 0 };
    uint16_t sector_pos = 0;

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

    MFile* getFile(std::string path) override {
        return new NIBMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_NIB */
