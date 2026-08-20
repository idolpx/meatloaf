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

// .D64, .D41 - 1541 disk image format
//
// https://vice-emu.sourceforge.io/vice_16.html#SEC408
// http://fileformats.archiveteam.org/wiki/CBMFS
// https://ist.uwaterloo.ca/~schepers/formats/D64.TXT
// https://ist.uwaterloo.ca/~schepers/formats/REL.TXT
// https://ist.uwaterloo.ca/~schepers/formats/GEOS.TXT
// https://www.lemon64.com/forum/viewtopic.php?t=70024&start=0 (File formats = Why is D64 not called D40/D41)
//  - discussion of disk id in sector missing from d64 file format is interesting
// https://www.c64-wiki.com/wiki/Disk_Image
// http://unusedino.de/ec64/technical3.html
// http://www.baltissen.org/newhtm/diskimag.htm
// http://www.baltissen.org/newhtm/1541c.htm
// https://www.c64os.com/post/softwareiecgap
//

#ifndef MEATLOAF_MEDIA_D64
#define MEATLOAF_MEDIA_D64

#include "meatloaf.h"

#include <map>
#include <bitset>
#include <ctime>
#include <cstring>

#include "meat_media.h"
#include "string_utils.h"
#include "utils.h"


/********************************************************
 * Streams
 ********************************************************/

// The string a CBM NEW (N0:) hands to format(), parsed.
//
// CBM syntax is "name,id". Meatloaf extends it with two optional trailing
// fields, so a 40-track D64 or a multi-track DNP can be created from BASIC:
//
//     N0:name,id                 media default geometry, no error info
//     N0:name,id,40              40 tracks
//     N0:name,id,40,1            40 tracks with an error-info area
//     N0:name,id,,1              default geometry with an error-info area
//
// Missing or unparseable trailing fields fall back to the defaults, so a plain
// "name,id" behaves exactly as it always did.
struct D64FormatSpec
{
    std::string name;
    std::string id;
    size_t      track_count = 0;      // 0 = whatever the media's default is
    bool        error_info  = false;
};

D64FormatSpec parseD64FormatSpec(const std::string& header_info);

class D64MStream : public MMediaStream {

protected:

    struct Block {
        uint8_t track;
        uint8_t sector;
    };

    using BlockChain = std::vector<Block>;

    struct BlockAllocationMap {
        uint8_t track;
        uint8_t sector;
        uint8_t offset;
        uint8_t start_track;
        uint8_t end_track;
        uint8_t byte_count;
    };

    struct Partition {
        uint8_t header_track;
        uint8_t header_sector;
        uint8_t header_offset;
        uint8_t directory_track;
        uint8_t directory_sector;
        uint8_t directory_offset;
        uint8_t parent_header_track;
        uint8_t parent_header_sector;
        uint8_t parent_entry_track;
        uint8_t parent_entry_sector;
        uint8_t parent_entry_offset;
        std::vector<BlockAllocationMap> block_allocation_map;
    };

    struct Header {
        char name[16];
        char unused[2];
        char id_dos[5];
    };

    struct Entry {
        uint8_t next_track;
        uint8_t next_sector;
        uint8_t file_type;
        uint8_t start_track;
        uint8_t start_sector;
        char filename[16];
        uint8_t rel_start_track;    // Or GOES info block start track
        uint8_t rel_start_sector;   // Or GEOS info block start sector
        uint8_t rel_record_length;  // Or GEOS file structure (Sequential / VLIR file)
        uint8_t geos_file_type;     // $00 - Non-GEOS (normal C64 file)
        uint8_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hour;
        uint8_t minute;
        uint16_t blocks;
    };

public:
    std::vector<Partition> partitions;
    std::vector<uint16_t> sectorsPerTrack = { 17, 18, 19, 21 };
    std::vector<uint8_t> interleave = { 3, 10 }; // Directory, File

    // True for the floppy formats: one whole track belongs to the directory,
    // so directory blocks are only ever placed there and file data never is.
    // CMD native partitions (DNP) do not work that way - their directory is a
    // plain chain that can extend onto any track, and file data shares track 1
    // with it. On a small native partition that distinction is not cosmetic:
    // excluding the directory track would leave a 1-track partition with no
    // data blocks at all.
    bool dedicated_directory_track = true;

    // Opt-in permission to extend the medium as it fills. OFF by default, and
    // deliberately so: a growable partition is a MEATLOAF EXTENSION, not CMD
    // behaviour - a real CMD native partition is fixed at the size it was
    // created with. Leaving it off keeps us compatible by default.
    //
    // It must also stay off whenever the image is embedded in a container. A
    // DNP inside a DHD occupies a fixed window at a fixed offset, so growing it
    // would write straight over the NEXT partition. Nothing sets this flag for
    // a DHD-hosted partition, and nothing should.
    bool allow_grow = false;

    uint8_t dos_version = 0x41; // 'A' - CBM DOS 2.6 (1541)
    std::string dos_rom = "dos1541";
    std::string dos_name = "";

    bool error_info = false;
    std::string bam_message = "";

    D64MStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // D64 Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                18,     // track
                0,      // sector
                0x04,   // offset
                1,      // start_track
                35,     // end_track
                4       // byte_count
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
            b     // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 17, 18, 19, 21 };


        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 174848: // 35 tracks no errors
                break;

            case 175531: // 35 w/ errors
                error_info = true;
                break;

            case 196608: // 40 tracks no errors
                curPartition().block_allocation_map[0].end_track = 40;
                break;

            case 197376: // 40 w/ errors
                curPartition().block_allocation_map[0].end_track = 40;
                error_info = true;
                break;

            case 205312: // 42 tracks no errors
                curPartition().block_allocation_map[0].end_track = 42;
                break;

            case 206114: // 42 w/ errors
                curPartition().block_allocation_map[0].end_track = 42;
                error_info = true;
                break;
        }

        // Get DOS Version

        // Extend BAM Info for DOLPHIN, SPEED, and ProLogic DOS
        // The location of the extra BAM information in sector 18/0, for 40 track images, 
        // will be different depending on what standard the disks have been formatted with. 
        // SPEED DOS stores them from $C0 to $D3, DOLPHIN DOS stores them from $AC to $BF 
        // and PrologicDOS stored them right after the existing BAM entries from $90-A3. 
        // PrologicDOS also moves the disk label and ID forward from the standard location 
        // of $90 to $A4. 64COPY and Star Commander let you select from several different 
        // types of extended disk formats you want to create/work with. 

        // // DOLPHIN DOS
        // partitions[0].block_allocation_map.push_back( 
        //     {
        //         18,     // track
        //         0,      // sector
        //         0xAC,   // offset
        //         36,     // start_track
        //         40,     // end_track
        //         4       // byte_count
        //     } 
        // );

        // // SPEED DOS
        // partitions[0].block_allocation_map.push_back( 
        //     {
        //         18,     // track
        //         0,      // sector
        //         0xC0,   // offset
        //         36,     // start_track
        //         40,     // end_track
        //         4       // byte_count
        //     } 
        // );

        // // PrologicDOS
        // partitions[0].block_allocation_map.push_back( 
        //     {
        //         18,     // track
        //         0,      // sector
        //         0x90,   // offset
        //         36,     // start_track
        //         40,     // end_track
        //         4       // byte_count
        //     } 
        // );
        // partitions[0].header_offset = 0xA4;

        //getBAMMessage();

    };

	// virtual std::unordered_map<std::string, std::string> info() override { 
    //     return {
    //         {"System", "Commodore"},
    //         {"Format", "D64"},
    //         {"Media Type", "DISK"},
    //         {"Tracks", getTrackCount()},
    //         {"Sectors / Blocks", this.getSectorCount()},
    //         {"Sector / Block Size", std::string(block_size)},
    //         {"Error Info", (this.error_info) ? "Available" : "Not Available"},
    //         {"Write Protected", ""},
    //         {"DOS Format", this.getDiskFormat()}
    //     }; 
    // };

    uint16_t blocksFree() override;

    // Canonical image size in bytes for this media type. Used when format()
    // creates an image from nothing. Declared explicitly per format rather
    // than derived from the geometry tables, so the two can be cross-checked
    // against each other instead of a wrong table validating itself.
    virtual uint32_t defaultImageSize() { return 174848; } // D64, 35 tracks

    uint8_t speedZone( uint8_t track) override
    {
        return (track < 18) + (track < 25) + (track < 31);
    };

    bool seekBlock( uint64_t index, uint8_t offset = 0 ) override;
    bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) override;
    bool seekSector( std::vector<uint8_t> trackSectorOffset ) override;
    int32_t sectorByteOffset( uint8_t track, uint8_t sector ) override;
    int32_t linearBlock( uint8_t track, uint8_t sector );


    uint16_t getSectorCount( uint16_t track )
    {
        return sectorsPerTrack[speedZone(track)];
    }
    uint16_t getTrackCount()
    {
        return curPartition().block_allocation_map[curPartition().block_allocation_map.size() - 1].end_track;
    }

    virtual bool seekPath(std::string path) override;
    uint32_t readFile(uint8_t* buf, uint32_t size) override;

    // Seek to any byte offset within the SELECTED file, by walking its block
    // chain -- the base class seeks the container, which is meaningless for a
    // file whose bytes are scattered across blocks (it left every later read
    // returning zeros). With no file selected the stream IS the raw image and
    // the base class is already right.
    bool seek(uint32_t offset) override;
    // seek(uint32_t) would otherwise HIDE the inherited two-argument form
    // from anything holding a D64MStream*.
    using MMediaStream::seek;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override;

    // Finalizes a streamed file write (last block + directory entry)
    // before releasing the container stream.
    void close() override;

    // --- Path navigation (subdirectories) ---
    // Paths inside the image are '/'-separated: DIR/.../FILE
    enum PathResult { PATH_NOT_FOUND, PATH_FILE, PATH_DIR };

    // Walks a directory path (all components must be directories)
    // and leaves the stream's current directory set to it. Empty path = root.
    bool seekDirectory(std::string path);
    // Walks a full path; on PATH_FILE the entry is loaded, on PATH_DIR the
    // current directory is set to it.
    PathResult resolvePath(std::string path);

    // Delete a file inside the image: free its block chain and mark the
    // directory entry scratched. Public counterpart to scratchEntry(), which
    // is protected because it assumes the caller has already positioned on
    // the entry - a contract that is easy to get wrong from outside (seekPath
    // leaves track/sector on the file's first block, not its directory sector).
    bool removeFile(std::string path);

    // Recover a scratched file, the way a CBM unscratch utility does. A scratch
    // only zeroes the entry's type byte, so the name, start track/sector and
    // block count all survive - the file is recoverable until either its
    // directory slot is reused or its blocks are handed to a new file.
    //
    // Fails, changing nothing, if any block of the chain has since been
    // allocated. Restores the type as PRG: the original type is genuinely gone,
    // since that is the single byte a scratch overwrites.
    bool unremoveFile(std::string path);

    // Enter a subdirectory entry (CMD native DIR or 1581 CBM sub-partition)
    bool enterDirectory(std::string name);

    // Current directory chain start (0 = use partition defaults)
    uint8_t dir_track = 0;
    uint8_t dir_sector = 0;

    Header header;      // Directory header data
    Entry entry;        // Directory entry data

    // The current partition WITHIN this disk image - an index into
    // partitions[], selecting among a 1581's CBM sub-partitions or a CMD
    // native image's sub-partitions. Every format currently pushes exactly one
    // Partition, so it is 0 today, but this is a real index and must stay one:
    // callers index partitions[] with it, and the bounds guards on it are
    // load-bearing.
    //
    // This is NOT the CMD HD partition. Which partition of a .dhd/.d1m/.d2m/
    // .d4m file a disk image lives in is tracked by DHDImageRegistry, one
    // level up - this field is scoped to the decoded disk itself.
    uint8_t partition = 0;

    // Geometry for the current partition of this image.
    Partition& curPartition() { return partitions[partition]; }
    const Partition& curPartition() const { return partitions[partition]; }

    uint8_t track = 0;
    uint8_t sector = 0;
    uint8_t offset = 0;
    uint64_t block = 0;
    uint64_t blocks_free = 0;

    uint8_t next_track = 0;
    uint8_t next_sector = 0;
    uint8_t sector_offset = 0;

protected:

    virtual bool readHeader() override
    {
        memset(&header, 0, sizeof(header));
        if (partitions.empty() || partition >= partitions.size()) {
            Debug_printv("Invalid partition index: %d", partition);
            return false;
        }
        return seekSector(curPartition().header_track, 
                        curPartition().header_sector, 
                        curPartition().header_offset) &&
            readContainer((uint8_t*)&header, sizeof(header));
    }

    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index = 0 ) override;
    bool readEntry( uint16_t index = 0 ) override;
    bool writeEntry( uint16_t index = 0 ) override;

    std::string readBlock( uint8_t track, uint8_t sector );
    bool writeBlock( uint8_t track, uint8_t sector, std::string data );
    bool allocateBlock( uint8_t track, uint8_t sector );
    bool deallocateBlock( uint8_t track, uint8_t sector );
    BlockChain getFreeBlocks(uint16_t file_size);
    bool getNextFreeBlock(uint8_t startTrack, uint8_t startSector, uint8_t *foundTrack, uint8_t *foundSector, bool forDirectory = false);
    bool findFreeBlock(uint8_t startTrack, uint8_t startSector, uint8_t *foundTrack, uint8_t *foundSector, bool forDirectory);

    // True for blocks a format permanently reserves for its own structures,
    // beyond the header/BAM/first-directory blocks the generic code already
    // knows about. They are allocated but belong to no chain, so anything
    // auditing reachability has to treat them as reservations rather than
    // leaks. Only CMD native partitions have such an area today.
    virtual bool isReservedBlock(uint8_t track, uint8_t sector)
    {
        (void)track; (void)sector;
        return false;
    }

    // Grow the medium by one track and return true if that succeeded. Fixed
    // geometry formats cannot, so the default refuses; a CMD native partition
    // (DNP) can, which is the whole point of "native" - it is sized at creation
    // but not capped there. getNextFreeBlock() calls this once when it can find
    // no free block, then retries.
    virtual bool growImage() { return false; }
    bool isBlockFree(uint8_t track, uint8_t sector);

    // BAM record location for one track (which BAM sector holds it and where)
    struct BAMRecord {
        uint8_t bam_track;
        uint8_t bam_sector;
        uint8_t offset;       // offset of this track's record within the BAM sector
        uint8_t byte_count;   // record length in bytes
        bool has_count;       // first byte of record is the free-sector count
    };
    bool getBAMRecord( uint8_t track, BAMRecord *rec );
    bool readBAMRecord( uint8_t track, BAMRecord *rec, uint8_t *buf );
    // Virtual so formats whose BAM splits the free COUNT away from the BITMAP
    // can maintain both halves - see D71MStream, where side 2's counts live in
    // 18/0 at 0xDD while its bitmaps live at 53/0.
    bool setBlockAllocation( uint8_t track, uint8_t sector, bool allocate ) override;
    uint16_t getTrackFreeCount( uint8_t track );
    bool findFreeSectorOnTrack( uint8_t track, uint8_t startSector, uint8_t *foundSector );

    // Streamed new-file write (SAVE): blocks are allocated one at a time as
    // data arrives, the directory entry is added when the stream is closed.
    bool beginFileWrite( std::string filename );
    uint32_t writeFileNew( uint8_t *buf, uint32_t size );
    bool finalizeFileWrite();
    void rollbackFileWrite();
    bool scratchEntry();

    // Restore the entry scratchEntry() blanked. Assumes the caller has already
    // positioned on it (see seekScratchedEntry), same contract as scratchEntry.
    bool unscratchEntry();

    // Locate a SCRATCHED entry by name. seekEntry() skips type-0 entries, which
    // is correct for every normal lookup and exactly wrong for recovery. Slots
    // that were never used are also type 0, so a blank name is not a match.
    bool seekScratchedEntry(std::string filename);

    // Read a block's BAM bit. Returns false if the block is not representable
    // in the BAM at all, so a caller that cannot answer treats it as unsafe
    // rather than assuming free. out_allocated is only set when it returns true.
    bool queryBlockAllocation(uint8_t track, uint8_t sector, bool& out_allocated);

    bool creating = false;          // building a new file via streamed writes
    std::string create_filename;    // name for the directory entry (UTF8)
    uint8_t cbuf[254];              // pending data for the current block
    uint16_t cbuf_len = 0;
    uint8_t create_start_track = 0; // first block of the file
    uint8_t create_start_sector = 0;
    uint8_t create_track = 0;       // block currently being filled
    uint8_t create_sector = 0;
    BlockChain create_allocated;    // all blocks claimed so far (for count/rollback)

    BlockChain getBlocks( uint8_t track, uint8_t sector );
    BlockChain getBlocks( std::string filename );

    bool initializeBlocks();

public:
    // Lay out a blank image: fill blocks, initialize the BAM and directory,
    // and write the header. Contains no MFSOwner/MFile resolution so it can
    // be driven directly over any container stream.
    bool formatImage(std::string name, std::string id, size_t track_count = 0, bool error_info = false);

protected:
    // CBM 8050/8250 BAM blocks carry a 6-byte header ahead of their per-track
    // entries: a T/S link, the DOS version, a reserved byte, and the range of
    // tracks the block covers (lowest, highest + 1). The blocks chain to each
    // other and the last one links to the first directory sector. Without this
    // c1541 derives a bogus track range and reports "ERR = 65, NO BLOCK" for
    // tracks outside the disk. BlockAllocationMap has no room for it, so
    // formats that need it call this after the generic initializer.
    // Uses the dos_version member rather than a parameter: passing it in let
    // D80 and D82 drift apart (one read the member, the other hardcoded 0x43).
    bool writeBamBlockHeaders()
    {
        auto &maps = curPartition().block_allocation_map;
        for (size_t i = 0; i < maps.size(); i++)
        {
            // Link to the next BAM block, or to the directory from the last one.
            uint8_t next_track  = (i + 1 < maps.size()) ? maps[i + 1].track
                                                        : curPartition().directory_track;
            uint8_t next_sector = (i + 1 < maps.size()) ? maps[i + 1].sector
                                                        : curPartition().directory_sector;

            uint8_t hdr[6] = {
                next_track,
                next_sector,
                dos_version,
                0x00,                               // reserved
                maps[i].start_track,
                (uint8_t)(maps[i].end_track + 1)    // highest track + 1
            };

            if (!seekSector(maps[i].track, maps[i].sector, 0))
                return false;
            if (writeContainer(hdr, sizeof(hdr)) != sizeof(hdr))
                return false;

            // Zero the unused tail of the sector. initializeBlocks() fills every
            // non-header track with the CBM 0x4B/0x01 pattern, and the per-track
            // entries only cover part of the sector, so the remainder would keep
            // that fill - a real format leaves it zero.
            uint16_t used = maps[i].offset
                          + (uint16_t)(maps[i].end_track - maps[i].start_track + 1) * maps[i].byte_count;
            if (used < block_size)
            {
                std::vector<uint8_t> pad(block_size - used, 0);
                if (!seekSector(maps[i].track, maps[i].sector, (uint8_t)used))
                    return false;
                if (writeContainer(pad.data(), pad.size()) != pad.size())
                    return false;
            }
        }
        return true;
    }

    // Virtual for the same reason as setBlockAllocation(): a format whose BAM
    // splits counts from bitmaps has to seed both halves at format time.
    virtual bool initializeBlockAllocationMap()
    {
        uint16_t bam_index = 0;
        uint16_t bam_count = curPartition().block_allocation_map.size();

        Debug_printv("initialize block allocation map");

        while (bam_index < bam_count) {
            seekSector( 
                curPartition().block_allocation_map[bam_index].track, 
                curPartition().block_allocation_map[bam_index].sector, 
                curPartition().block_allocation_map[bam_index].offset 
            );

            // Set default values
            uint8_t track = curPartition().block_allocation_map[bam_index].start_track;
            uint8_t end_track = curPartition().block_allocation_map[bam_index].end_track;
            uint8_t rec_bytes = curPartition().block_allocation_map[bam_index].byte_count;

            // Update BAM for each track
            for (uint16_t t = track; t <= end_track; t++) {
                // MUST be 16-bit: CMD native formats (DNP, DHD) have 256
                // sectors per track, which truncates to 0 in a uint8_t. That
                // made the bitmap loop write all-zero bytes - every block
                // marked ALLOCATED - and made bitmap_bytes compute as 0, which
                // flipped has_count on so the writer emitted a leading count
                // byte the reader does not expect. getSectorCount() returns
                // uint16_t for exactly this reason.
                uint16_t sectors = getSectorCount(t);
                uint8_t data = 0;

                // A record carries a leading free-sector count ONLY when it has
                // more bytes than the bitmap needs (D64/D81). Bitmap-only records
                // - D71 side 2, CMD native 32-byte records - must not get one:
                // writing it anyway shifts the whole bitmap over by a byte and
                // truncates its last byte, so blocks read back as allocated that
                // were never allocated. Same test getBAMRecord() uses, so the
                // writer and the readers agree about the record's shape.
                uint8_t bitmap_bytes = (uint8_t)((sectors + 7) / 8);
                bool has_count = rec_bytes > bitmap_bytes;
                if (has_count)
                {
                    data = (uint8_t)sectors;
                    if (!writeContainer((uint8_t*)&data, 1))
                        return false;
                }
                // Keep the record's fixed width either way, so the next track's
                // entry lands at the right offset.
                bitmap_bytes = has_count ? (uint8_t)(rec_bytes - 1) : rec_bytes;

                // Next bytes are the bit map (1 = Free, 0 = Allocated)
                for (uint8_t i = 0; i < bitmap_bytes; i++) {
                    if ( sectors >= 8 )
                    {
                        data = 0xFF;
                        sectors -= 8;
                    }
                    else
                    {
                        data = 0x00;
                        while (sectors > 0) {
                            data |= 0x01 << (sectors - 1);
                            sectors--;
                        }
                    }

                    if (!writeContainer((uint8_t*)&data, 1))
                        return false;
                }
            }
            bam_index++;
        }

        return true;
    }


    // Block/entry helpers usable by derived formats (D71/D81/DNP/DHD/...)
    bool writeHeader(std::string name, std::string id) override
    {
        if (partitions.empty() || partition >= partitions.size()) {
            Debug_printv("Invalid partition index: %d", partition);
            return false;
        }
        seekSector(
            curPartition().header_track,
            curPartition().header_sector, 
            curPartition().header_offset 
        );

        name = mstr::toPETSCII2(name);
        id = mstr::toPETSCII2(id);

        // Set default values
        memset(&header, 0xA0, sizeof(header));
        memcpy(header.id_dos, "\x30\x30\xA0\x32\x41", 5); // "00 2A"

        // Set values
        memcpy(header.name, name.c_str(), name.size());
        memcpy(header.id_dos, id.c_str(), id.size());
        Debug_printv("name[%16s] id_dos[%5s]", header.name, header.id_dos);
        // NOTE: writeContainer() returns a BYTE COUNT, so `if (writeContainer(...))
        // return true;` used to exit here on SUCCESS - making everything below
        // unreachable, and inverting the return value on the failure path.
        if (writeContainer((uint8_t*)&header, sizeof(header)) != sizeof(header))
            return false;

        // NOTE: a "meatloaf!!! https://meatloaf.cc" bam_message write used to sit
        // here, but it was unreachable behind the early return above and used
        // sizeof() on a std::string (the OBJECT size, not the text length).
        // Left out deliberately: enabling it writes bytes into the header sector
        // that were never written before, which is a behavior change this fix
        // should not smuggle in.

        // The BAM/header sector and the first directory sector are allocated by
        // initializeDirectory(), which runs before this. Allocating them again
        // here would fail: setBlockAllocation() returns false for a block that
        // is already allocated.

        return true;
    }

    // bool initializeDirectory()
    // {
    //     Debug_printv("initialize directory");
    //     seekSector( 
    //         curPartition().header_track, 
    //         curPartition().header_sector, 
    //         0
    //     );

    //     // Set default values in sector 0
    //     uint8_t track = curPartition().directory_track;
    //     uint8_t sector = curPartition().directory_sector;
    //     uint8_t data = 0x41;

    //     writeContainer((uint8_t*)&track, 1);
    //     writeContainer((uint8_t*)&sector, 1);
    //     writeContainer((uint8_t*)&data, 1);

    //     // Set T/S link in first sector to 0x00 0xFF
    //     seekSector( 
    //         curPartition().directory_track,
    //         curPartition().directory_sector,
    //         curPartition().directory_offset
    //     );
    //     data = 0x00;
    //     writeContainer((uint8_t*)&data, 1);
    //     data = 0xFF;
    //     writeContainer((uint8_t*)&data, 1);

    //     // Clear directory entries (e.g., 8 entries per sector)
    //     uint8_t emptyEntry[32] = {0}; // 32 bytes per entry
    //     for (int i = 0; i < 8; i++) {
    //         if (!writeContainer(emptyEntry, 32)) return false;
    //     }

    //     return true;
    // }
    bool initializeDirectory()
    {
        Debug_printv("initialize directory");
        if (!seekSector(curPartition().header_track, curPartition().header_sector, 0)) {
            return false;
        }
        uint8_t track = curPartition().directory_track;
        uint8_t sector = curPartition().directory_sector;
        uint8_t data = dos_version; // DOS version
        if (!writeContainer(&track, 1) || !writeContainer(&sector, 1) || !writeContainer(&data, 1)) {
            return false;
        }
        if (!seekSector(curPartition().directory_track, curPartition().directory_sector, curPartition().directory_offset)) {
            return false;
        }
        data = 0x00; // End of directory chain
        if (!writeContainer(&data, 1)) return false;
        data = 0xFF;
        if (!writeContainer(&data, 1)) return false;
        // Clear the rest of the sector. A directory sector holds 8 x 32-byte
        // entries = 256 bytes TOTAL, and the 2-byte T/S link written above
        // occupies the head of the first entry slot - so only 254 bytes may
        // follow it. Writing a full 8 x 32 here overran the sector by 2 bytes
        // and clobbered the NEXT sector's T/S link.
        std::vector<uint8_t> empty(block_size - 2, 0);
        if (writeContainer(empty.data(), empty.size()) != empty.size()) return false;

        // Allocate EVERY sector the BAM itself occupies. Formats with more than
        // one BAM record spread them over several sectors - D71 has 18/0 plus
        // 53/0 for side 2, D80 has 38/0 and 38/3, D82 adds 38/6 and 38/9 - and
        // leaving those unallocated makes the BAM advertise its own storage as
        // free. Records can share a sector, so skip one that is already taken
        // rather than letting setBlockAllocation() fail on a double allocation.
        for (auto &bam : curPartition().block_allocation_map)
        {
            if (!isBlockFree(bam.track, bam.sector))
                continue;
            if (!setBlockAllocation(bam.track, bam.sector, true))
                return false;
        }

        // ...and the header sector. On a D64/D71 that IS block_allocation_map[0]
        // (18/0) so the loop above already took it, but on a D80/D82 the header
        // lives at 39/0 while the BAM sits on track 38 - it is not in the map at
        // all and would otherwise never be allocated.
        if (isBlockFree(curPartition().header_track,
                        curPartition().header_sector))
        {
            if (!setBlockAllocation(curPartition().header_track,
                                    curPartition().header_sector, true))
                return false;
        }

        // ...and the first directory sector (e.g. 18/1 on D64). Without this
        // the BAM claims a sector the directory chain is actively using, so a
        // later write can allocate it a second time and corrupt the directory.
        if (!setBlockAllocation( curPartition().directory_track,
                                 curPartition().directory_sector,
                                 true ))
            return false;

        return true;
    }

    // Container
    friend class D8BMFile;
    friend class DFIMFile;

    // Test-only structural invariant checker (test/native/test_disk_write)
    friend struct ImageInvariantChecker;

    // Disk
    friend class D64MFile;
    friend class D71MFile;
    friend class D80MFile;
    friend class D81MFile;
    friend class D82MFile;
    friend class DNPMFile;
    friend class G64MFile;
    friend class NIBMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D64MFile: public MFile {
public:

    D64MFile(std::string path): MFile(path)
    {
        media_image = name;
        isCBM = true;
        size = 174848; // Default - 35 tracks no errors
    };
    
    ~D64MFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        // Debug_printv("[%s]", url.c_str());
        if (!is) return nullptr;
            return std::make_shared<D64MStream>(is);
    }

    bool format(std::string header_info) override;

    virtual bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    // Builds the URL for one entry of THIS directory. Virtual so a subclass can
    // inject a component the base class knows nothing about - DHDPartitionMFile
    // re-inserts the partition, without which a listing of a non-selected
    // partition would emit entries that resolve into the selected one.
    virtual std::string entryUrlFor(const std::string& filename);

    // The URL handed to ImageBroker::obtain(). The broker rebuilds the stream
    // from this URL, so a subclass whose stream depends on more than the
    // container must include that here - DHDPartitionMFile appends the CMD
    // partition number, without which the rebuilt stream decodes whichever
    // partition happens to be selected.
    virtual std::string brokerUrl() { return url; }

    bool isDirectory() override;
    bool exists() override;
    bool remove() override;
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override;
    time_t getCreationTime() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * MFileSystem
 ********************************************************/

class D64MFileSystem: public MFileSystem
{
public:
    D64MFileSystem(): MFileSystem("d64") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        //printf("handles w dnp %s %d\r\n", fileName.rfind(".dnp"), fileName.length()-4);
        return byExtension(
            {
                ".d64",
                ".d41"
            }, 
            fileName
        );
    }

    MFile* getFile(std::string path) override {
        //Debug_printv("path[%s]", path.c_str());
        return new D64MFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_D64 */
