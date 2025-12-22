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

// .HDD - IDE64 Filesystem 0.11 revision 5 © 2001-2003 by Soci/Singular (Commodore File System)
//
// https://singularcrew.hu/idedos/cfs.html
// https://singularcrew.hu/ide64warez/site/other-OS/Windows/fusecfs-2.0.4-win.zip
//

//  CFS Format Support

//   The implementation supports:
//   - ✓ Boot sector reading with signature validation
//   - ✓ Partition directory (up to 16 partitions)
//   - ✓ Partition selection and boundaries
//   - ✓ Directory reading (single sector, root only)
//   - ✓ File entry parsing with all attributes
//   - ✓ Timestamp decoding (4-byte packed format)
//   - ✓ File type detection (DEL/PRG/REL/DIR/LNK)
//   - ✓ Basic file reading via data tree
//   - ⚠ Simplified data tree (direct pointers only, no recursive tree traversal)
//   - ⚠ Single directory sector (no multi-sector directory chaining)
//   - ⚠ Root directory only (no subdirectory navigation)
//   - ✗ Bitmap allocation reading
//   - ✗ Write operations
//   - ✗ REL file support
//   - ✗ Link file resolution

#ifndef MEATLOAF_MEDIA_HDD
#define MEATLOAF_MEDIA_HDD

#include "meatloaf.h"
#include "meat_media.h"

#include <ctime>


/********************************************************
 * Streams
 ********************************************************/

class HDDMStream : public MMediaStream {

public:
    HDDMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // CFS uses 512-byte sectors
        block_size = 512;
        has_subdirs = true;

        // Read boot sector and partition directory
        if (!readHeader())
        {
            Debug_printv("Failed to read HDD header");
            return;
        }

        // Select default partition
        if (!selectPartition(boot_sector.default_partition))
        {
            Debug_printv("Failed to select default partition");
        }
    };

    // Pointer structure (4 bytes) used throughout CFS
    struct Pointer {
        uint8_t flags;          // VALID, LBA, HIDDEN, WRITEABLE + high bits
        uint8_t cyl_lba_mid;    // Middle byte of address
        uint8_t cyl_lba_low;    // Low byte of address
        uint8_t head_lba_high;  // High byte (bits 0-3) or head (bits 0-1)

        bool isValid() const { return (flags & 0x80) != 0; }
        bool isLBA() const { return (flags & 0x40) != 0; }
        bool isHidden() const { return (flags & 0x20) != 0; }
        bool isWriteable() const { return (flags & 0x10) != 0; }

        uint32_t getLBA() const {
            if (!isLBA()) return 0;
            return ((uint32_t)(flags & 0x0F) << 20) |
                   ((uint32_t)head_lba_high << 16) |
                   ((uint32_t)cyl_lba_mid << 8) |
                   cyl_lba_low;
        }

        void setLBA(uint32_t lba) {
            flags = (flags & 0xF0) | ((lba >> 20) & 0x0F);
            head_lba_high = (lba >> 16) & 0xFF;
            cyl_lba_mid = (lba >> 8) & 0xFF;
            cyl_lba_low = lba & 0xFF;
        }
    } __attribute__((packed));

    // Boot sector structure
    struct BootSector {
        uint8_t reserved0;          // $00
        uint8_t default_partition;  // $01
        Pointer last_sector;        // $02-$05
        uint8_t reserved1[2];       // $06-$07
        char id[16];                // $08-$17: "C64 CFS V 0.11B "
        Pointer part_dir;           // $18-$1B: Partition directory pointer
        Pointer part_dir_backup;    // $1C-$1F: Backup location
        char disk_label[16];        // $20-$2F: Global disk label
        uint8_t reserved2[464];     // $30-$1FF
    } __attribute__((packed));

    // Partition entry (32 bytes)
    struct PartitionEntry {
        char name[8];           // $00-$07: Partition name (null-padded)
        Pointer start;          // $08-$0B: Start sector
        Pointer end;            // $0C-$0F: End sector (type in flags)
        uint8_t reserved[16];   // $10-$1F

        uint8_t getType() const { return end.flags & 0x0F; }
        bool isCFS() const { return getType() == 0x01; }
        bool isGEOS() const { return getType() == 0x02; }
    } __attribute__((packed));

    // Directory entry (32 bytes)
    struct DirectoryEntry {
        char filename[8];       // $00-$07: Filename (null-padded)
        uint32_t filesize;      // $08-$0B: File size in bytes
        Pointer data_tree;      // $0C-$0F: Data tree pointer or resource
        uint8_t attributes;     // $10: File attributes
        uint8_t timestamp[4];   // $11-$14: Packed timestamp
        uint8_t filetype_extra; // $15: Extra file type info
        uint8_t reserved[10];   // $16-$1F

        bool isClosed() const { return (attributes & 0x80) != 0; }
        bool isDeleteable() const { return (attributes & 0x40) != 0; }
        bool isReadable() const { return (attributes & 0x20) != 0; }
        bool isWriteable() const { return (attributes & 0x10) != 0; }
        bool isExecutable() const { return (attributes & 0x08) != 0; }
        uint8_t getFileType() const { return attributes & 0x07; }

        bool isEmpty() const { return getFileType() == 0 && isClosed(); }
        bool isNormalFile() const { return getFileType() == 1; }
        bool isRELFile() const { return getFileType() == 2; }
        bool isDirectory() const { return getFileType() == 3; }
        bool isLink() const { return getFileType() == 4; }
        bool isLabel() const { return !isClosed() && getFileType() == 3; }

        time_t getTimestamp() const;
    } __attribute__((packed));

    // Directory sector (512 bytes)
    struct DirectorySector {
        DirectoryEntry entries[16];     // $000-$1EF: 16 entries
        Pointer next_sector;            // $1F0-$1F3: Next directory sector
        uint8_t reserved[12];           // $1F4-$1FF
    } __attribute__((packed));

protected:
    struct Header {
        std::string disk_label;
        std::string id;
        uint8_t partition_count;
    };

    struct Entry {
        std::string filename;
        uint32_t size;
        std::string type;
        uint8_t attributes;
        time_t timestamp;
        Pointer data_tree;
        bool is_directory;
    };

    BootSector boot_sector;
    PartitionEntry partitions[16];
    DirectorySector current_dir;
    uint32_t current_partition_start;
    uint32_t current_partition_end;
    uint32_t current_dir_sector;
    uint8_t current_partition;

    Header header;
    Entry entry;

    bool readHeader() override;
    bool readPartitionDirectory();
    bool selectPartition(uint8_t partition_num);
    bool readDirectorySector(uint32_t lba);

    bool seekEntry(std::string filename) override;
    bool seekEntry(uint16_t index) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    bool readDataTree(Pointer tree_ptr, uint32_t offset, uint8_t* buf, uint32_t size, uint32_t& bytes_read);
    uint32_t readDataSector(uint32_t lba, uint8_t* buf, uint32_t size);

private:
    friend class HDDMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class HDDMFile: public MFile {
public:
    HDDMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
        media_image = name;
        isPETSCII = false;  // CFS uses ASCII
    };

    ~HDDMFile() {
        // don't close the stream here!
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());
        return std::make_shared<HDDMStream>(is);
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;
};


/********************************************************
 * FS
 ********************************************************/

class HDDMFileSystem: public MFileSystem
{
public:
    HDDMFileSystem(): MFileSystem("hdd") {};

    bool handles(std::string fileName) override {
        return byExtension(".hdd", fileName);
    }

    MFile* getFile(std::string path) override {
        return new HDDMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_HDD */
