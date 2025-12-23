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
// https://ist.uwaterloo.ca/~schepers/formats/D64.TXT
// https://ist.uwaterloo.ca/~schepers/formats/REL.TXT
// https://ist.uwaterloo.ca/~schepers/formats/GEOS.TXT
// https://www.lemon64.com/forum/viewtopic.php?t=70024&start=0 (File formats = Why is D64 not called D40/D41)
//  - disucssion of disk id in sector missing from d64 file format is interesting
// https://www.c64-wiki.com/wiki/Disk_Image
// http://unusedino.de/ec64/technical3.html
// http://www.baltissen.org/newhtm/diskimag.htm
// http://www.baltissen.org/newhtm/1541c.htm
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

class D64MStream : public MMediaStream {

protected:

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

    uint8_t dos_version = 0x41;
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
            18,    // track
            0,     // sector
            0x90,  // header_offset
            18,    // directory_track
            1,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 17, 18, 19, 21 };

        // Read Header
        readHeader();

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 174848: // 35 tracks no errors
                break;

            case 175531: // 35 w/ errors
                error_info = true;
                break;

            case 196608: // 40 tracks no errors
                partitions[partition].block_allocation_map[0].end_track = 40;
                break;

            case 197376: // 40 w/ errors
                partitions[partition].block_allocation_map[0].end_track = 40;
                error_info = true;
                break;

            case 205312: // 42 tracks no errors
                partitions[partition].block_allocation_map[0].end_track = 42;
                break;

            case 206114: // 42 w/ errors
                partitions[partition].block_allocation_map[0].end_track = 42;
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

    uint8_t speedZone( uint8_t track) override
    {
        return (track < 18) + (track < 25) + (track < 31);
    };

    bool seekBlock( uint64_t index, uint8_t offset = 0 ) override;
    bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) override;
    bool seekSector( std::vector<uint8_t> trackSectorOffset ) override;


    uint16_t getSectorCount( uint16_t track )
    {
        return sectorsPerTrack[speedZone(track)];
    }
    uint16_t getTrackCount()
    {
        return partitions[0].block_allocation_map[0].end_track;
    }

    virtual bool seekPath(std::string path) override;
    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override;

    Header header;      // Directory header data
    Entry entry;        // Directory entry data

    uint8_t partition = 0;
    uint64_t block = 0;
    uint8_t track = 0;
    uint8_t sector = 0;
    uint8_t offset = 0;
    uint64_t blocks_free = 0;

    uint8_t next_track = 0;
    uint8_t next_sector = 0;
    uint8_t sector_offset = 0;

protected:
    bool readHeader() override
    {
        if (partitions.empty() || partition >= partitions.size()) {
            Debug_printv("Invalid partition index: %d", partition);
            return false;
        }
        return seekSector(partitions[partition].header_track, 
                        partitions[partition].header_sector, 
                        partitions[partition].header_offset) &&
            readContainer((uint8_t*)&header, sizeof(header));
    }

private:
    bool writeHeader(std::string name, std::string id) override
    {
        if (partitions.empty() || partition >= partitions.size()) {
            Debug_printv("Invalid partition index: %d", partition);
            return false;
        }
        seekSector( 
            partitions[partition].header_track, 
            partitions[partition].header_sector, 
            partitions[partition].header_offset 
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
        if (writeContainer((uint8_t*)&header, sizeof(header)))
            return true;

        std::string bam_message = "meatloaf!!! https://meatloaf.cc";
        if (writeContainer((uint8_t*)bam_message.c_str(), sizeof(bam_message)))
            return true;

        return false;
    }

    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index = 0 ) override;
    bool readEntry( uint16_t index = 0 ) override;
    bool writeEntry( uint16_t index = 0 ) override;

    std::string readBlock( uint8_t track, uint8_t sector );
    bool writeBlock( uint8_t track, uint8_t sector, std::string data );
    bool allocateBlock( uint8_t track, uint8_t sector );
    bool deallocateBlock( uint8_t track, uint8_t sector );
    bool getNextFreeBlock(uint8_t startTrack, uint8_t startSector, uint8_t *foundTrack, uint8_t *foundSector, bool forDirectory = false);
    bool isBlockFree(uint8_t track, uint8_t sector);

    bool initializeBlocks();

    bool initializeBlockAllocationMap()
    {
        uint16_t bam_index = 0;
        uint16_t bam_count = partitions[partition].block_allocation_map.size();

        Debug_printv("initialize block allocation map");

        while (bam_index < bam_count) {
            seekSector( 
                partitions[partition].block_allocation_map[bam_index].track, 
                partitions[partition].block_allocation_map[bam_index].sector, 
                partitions[partition].block_allocation_map[bam_index].offset 
            );

            // Set default values
            uint8_t track = partitions[partition].block_allocation_map[bam_index].start_track;
            uint8_t end_track = partitions[partition].block_allocation_map[bam_index].end_track;
            uint8_t byte_count = partitions[partition].block_allocation_map[bam_index].byte_count;
            byte_count--; // First byte is number of sectors allocated

            // Update BAM for each track
            for (uint8_t t = track; t <= end_track; t++) {
                uint8_t sectors = getSectorCount(t);
                uint8_t data = 0;
                
                // First byte is count of free sectors
                data = sectors;
                if (!writeContainer((uint8_t*)&data, 1))
                    return false;

                // Next bytes are the bit map (1 = Free, 0 = Allocated)
                for (uint8_t i = 0; i < byte_count; i++) {
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
    // bool initializeDirectory()
    // {
    //     Debug_printv("initialize directory");
    //     seekSector( 
    //         partitions[partition].header_track, 
    //         partitions[partition].header_sector, 
    //         0
    //     );

    //     // Set default values in sector 0
    //     uint8_t track = partitions[partition].directory_track;
    //     uint8_t sector = partitions[partition].directory_sector;
    //     uint8_t data = 0x41;

    //     writeContainer((uint8_t*)&track, 1);
    //     writeContainer((uint8_t*)&sector, 1);
    //     writeContainer((uint8_t*)&data, 1);

    //     // Set T/S link in first sector to 0x00 0xFF
    //     seekSector( 
    //         partitions[partition].directory_track,
    //         partitions[partition].directory_sector,
    //         partitions[partition].directory_offset
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
        if (!seekSector(partitions[partition].header_track, partitions[partition].header_sector, 0)) {
            return false;
        }
        uint8_t track = partitions[partition].directory_track;
        uint8_t sector = partitions[partition].directory_sector;
        uint8_t data = 0x41; // DOS version
        if (!writeContainer(&track, 1) || !writeContainer(&sector, 1) || !writeContainer(&data, 1)) {
            return false;
        }
        if (!seekSector(partitions[partition].directory_track, partitions[partition].directory_sector, partitions[partition].directory_offset)) {
            return false;
        }
        data = 0x00; // End of directory chain
        if (!writeContainer(&data, 1)) return false;
        data = 0xFF;
        if (!writeContainer(&data, 1)) return false;
        // Clear directory entries (e.g., 8 entries per sector)
        uint8_t emptyEntry[32] = {0}; // 32 bytes per entry
        for (int i = 0; i < 8; i++) {
            if (!writeContainer(emptyEntry, 32)) return false;
        }
        return true;
    }

    // Container
    friend class D8BMFile;
    friend class DFIMFile;

    // Disk
    friend class D64MFile;
    friend class D71MFile;
    friend class D80MFile;
    friend class D81MFile;
    friend class D82MFile;
    friend class DNPMFile;    
};


/********************************************************
 * File implementations
 ********************************************************/

class D64MFile: public MFile {
public:

    D64MFile(std::string path): MFile(path)
    {
        media_image = name;
        isPETSCII = true;
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

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool exists() override;
    bool remove() override { return false; };
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override;
    time_t getCreationTime() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
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
