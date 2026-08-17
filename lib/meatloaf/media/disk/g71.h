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
// Differences of G71 to G64:
//
// * "GCR-1571" instead of "GCR-1541" in the Header
//

#ifndef MEATLOAF_MEDIA_G71
#define MEATLOAF_MEDIA_G71

#include "g64.h"
#include "d71.h"

// .G71 - a GCR bitstream image of a 1571 disk
//
// The container is a G64 in every respect except the signature, and the GCR on
// it is the same GCR: a 1571 in double-sided mode is two 1541 surfaces, written
// by the same read/write logic at the same four speed zones. So the only things
// that differ from g64.h are the signature and the geometry.
//
// **Track numbering is flat.** Tracks are 1-70, side 2 being 36-70, and the
// half track of track N is N * 2 exactly as it is for a 1541 - so the offset
// table entry is at `12 + (N - 1) * 2 * 4` with no per-side base. That is not an
// assumption: VICE's own reader indexes the table with `half_track - 2` and
// derives the track back as `half_track / 2` for every image type it supports
// (`fsimage-gcr.c`), and its table is sized MAX_GCR_TRACKS = 168 = 84 * 2.
// G64MStream::seekSector() already computes exactly this, so it needs no
// changes to read a 1571 image.

/********************************************************
 * Streams
 ********************************************************/

class G71MStream : public G64MStream {

public:
    G71MStream(std::shared_ptr<MStream> is) : G64MStream(is)
    {
        // 1571 geometry, copied from D71MStream. On a 1571 the side-2 BAM is
        // SPLIT - the per-track free counts for tracks 36-70 live at 18/0+$DD
        // while their bitmaps live at 53/0 - and D71MStream carries extra
        // handling for that in its WRITE paths only. None of it is repeated
        // here because a .g71 is read-only.
        std::vector<BlockAllocationMap> b = {
            {
                18,     // track
                0,      // sector
                0x04,   // offset
                1,      // start_track
                35,     // end_track
                4       // byte_count
            },
            {
                53,     // track
                0,      // sector
                0x00,   // offset
                36,     // start_track
                70,     // end_track
                3       // byte_count
            }
        };

        Partition p = {
            18,    // header_track
            0,     // header_sector
            0x90,  // header_offset
            18,    // directory_track
            1,     // directory_sector
            0x00,  // directory_offset
            0,     // parent_header_track
            0,     // parent_header_sector
            0,     // parent_entry_track
            0,     // parent_entry_sector
            0,     // parent_entry_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 17, 18, 19, 21 };
        interleave = { 3, 6 }; // Directory, File
        dos_rom = "dos1571";
    };

    const char *imageSignature() const override { return "GCR-1571"; }

    // Both surfaces run the same four speed zones; tracks 36-70 are side 2 and
    // repeat the side-1 progression. Copied from D71MStream, including the
    // `track <= 35` boundary it documents.
    uint8_t speedZone( uint8_t track ) override
    {
        if ( track <= 35 )
            return (track < 18) + (track < 25) + (track < 31);
        else
            return (track < 53) + (track < 60) + (track < 66);
    };

    uint32_t defaultImageSize() override { return 349696; }

private:
    friend class G71MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class G71MFile: public D64MFile {
public:
    G71MFile(std::string path) : D64MFile(path)
    {
        size = 349696; // 70 tracks, both sides
    };

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        return std::make_shared<G71MStream>(is);
    }
};


/********************************************************
 * FS
 ********************************************************/

class G71MFileSystem: public MFileSystem
{
public:
    G71MFileSystem(): MFileSystem("g71") {};

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".g71"
            },
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new G71MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_G71 */
