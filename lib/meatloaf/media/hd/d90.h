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

// .D90, .D60 - The D90 image is bit-for-bit copy of the hard drives in the D9090 and D9060
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC439
// http://www.baltissen.org/newhtm/diskimag.htm
// http://www.baltissen.org/newhtm/sasi.htm
// https://www.lemon64.com/forum/viewtopic.php?t=76483&sid=c3d6ee61cd935ebf0dc4fb3eebfd724e
//


#ifndef MEATLOAF_MEDIA_D90
#define MEATLOAF_MEDIA_D90

#include "meatloaf.h"
#include "../disk/d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D90MStream : public D64MStream {
    // override everything that requires overriding here

public:
    D90MStream(std::shared_ptr<MStream> is) : D64MStream(is)
    {
        // D90 Partition Info
        // BAM blocks link to next bam block starting at Track 1
        std::vector<BlockAllocationMap> b = { 
            {
                1,      // track
                0,      // sector
                0x10,   // offset
                0,      // start_track
                152,    // end_track - 153 tracks indexed 0-152
                5       // byte_count
            }
        };

        Partition p = {
            76,    // track
            20,    // sector
            0x06,  // header_offset
            76,    // directory_track - 0x390000
            10,    // directory_sector
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
        sectorsPerTrack = { 32 };
        interleave = { 10, 10 }; // Directory, File
        dos_rom = "dos9000";
        dos_version = 0xFF;

        // this.size = data.media_data.length;
        // switch (this.size + this.media_header_size) {
        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
             case 5013504:  // D9060 - 153 tracks, 32 sectors, 4 heads
                 sectorsPerTrack = { (4 * 32) }; // Heads * Sectors/Track = 128 sectors/track
                 break;

             case 7520256:  // D9090 - 153 tracks, 32 sectors, 6 heads
                sectorsPerTrack = { (6 * 32) }; // Heads * Sectors/Track = 192 sectors/track
                 break;
        }

        // Read Configuration Sector @ Track 0, Sector 0, Offset 0x04
        // this.seek(0x04);
        seek( 0x04 );
        // this.partitions[0].directory_track = this.read();
        partitions[0].directory_track = read();
        // this.partitions[0].directory_sector = this.read();
        partitions[0].directory_sector = read();
        // this.partitions[0].track = this.read();
        partitions[0].header_track = read();
        // this.partitions[0].sector = this.read();
        partitions[0].header_sector = read();
        // this.partitions[0].block_allocation_map[0].track = this.read();
        partitions[0].block_allocation_map[0].track = read();
        // this.partitions[0].block_allocation_map[0].sector = this.read();
        partitions[0].block_allocation_map[0].sector = read();

        // VICE: "reserved DIR space is skipped on all drives except D9090/60"
        // - the directory track is open to file data.
        dedicated_directory_track = false;

        readBAMChain();
    };

    virtual uint8_t speedZone(uint8_t track) override { return 0; };

    // Each track's BAM entry is one 5-byte group (free count + 32 bits) per 32
    // sectors: 4 groups on a D9060, 6 on a D9090.
    uint8_t bamGroupSectors() override { return 32; }

    // vdrive_bam_free_block_count counts tracks 1 up: track 0 (configuration
    // sector and bad-block lists) can still be allocated when everything else
    // is full, but it is not reported as free.
    uint16_t blocksFree() override { return D64MStream::blocksFree() - getTrackFreeCount(0); }

    uint32_t defaultImageSize() override { return 7520256; } // D9090 - 153 tracks, 32 sectors, 6 heads

protected:

private:
    // The BAM is a chain of blocks, linked at bytes 0/1 and ended by track 255
    // (vdrive_bam_allocate_chain_255). Byte 4 is the lowest track a block
    // covers and byte 5 one past the highest; the entries start at offset
    // $10 (BAM_BIT_MAP_9000), one per track, 5 bytes per 32-sector group.
    // One block_allocation_map record per BAM block is exactly the shape the
    // generic BAM code already walks for the 8050's several blocks. If the
    // chain cannot be read the single placeholder record stays, which keeps
    // the image readable and refuses allocations.
    void readBAMChain()
    {
        // Until the chain is known the placeholder record covers the track
        // range (seekSector() needs it) but describes no BAM bytes, so a
        // failed read below leaves the image listable and every allocation
        // refused, rather than written through a guessed layout.
        partitions[0].block_allocation_map[0].byte_count = 0;
        uint8_t groups = (uint8_t)(getSectorCount(0) / 32);
        if (groups == 0)
            return;
        uint8_t record = (uint8_t)(groups * 5);
        std::vector<BlockAllocationMap> chain;
        uint8_t t = partitions[0].block_allocation_map[0].track;
        uint8_t s = partitions[0].block_allocation_map[0].sector;
        while (t != 255 && chain.size() < 64)
        {
            uint8_t hdr[6];
            if (!seekSector(t, s, 0) || readContainer(hdr, sizeof(hdr)) != sizeof(hdr))
                return;
            uint8_t lo = hdr[4], hi = hdr[5];
            if (hi <= lo || (uint32_t)0x10 + (uint32_t)(hi - lo) * record > block_size)
                return;    // not a BAM block this layout describes
            chain.push_back({ t, s, 0x10, lo, (uint8_t)(hi - 1), record });
            t = hdr[0];
            s = hdr[1];
        }
        if (t != 255 || chain.empty())
            return;
        partitions[0].block_allocation_map = chain;
    }

    void allocateBadBlocks() {
        // seekSector( 0, 10, 0x00 );
        // uint8_t next_track = read();
        // uint8_t next_sector = read();
        // uint8_t current_track = 0;
        // uint8_t current_sector = read();

        // while (current_track != 0xFF && current_sector != 0xFF) {
        //     //Debug_printv("current_track[%d] current_sector[%d]", current_track, current_sector);
        //     seekSector( current_track, current_sector, 0x00 );
        //     current_track = read();
        //     current_sector = read();
            
        // }
    };

    friend class D90MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D90MFile: public D64MFile {
public:
    D90MFile(std::string path) : D64MFile(path) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<D90MStream>(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class D90MFileSystem: public MFileSystem
{
public:
    D90MFileSystem(): MFileSystem("d90") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".d90",
                ".d60",
                ".d96"
            }, 
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        return new D90MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_D90 */
