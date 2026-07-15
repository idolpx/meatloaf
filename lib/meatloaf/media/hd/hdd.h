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
//
//   The implementation supports:
//   - ✓ Boot sector reading with signature validation
//   - ✓ Partition directory (16 partitions, listed as directories at image root)
//   - ✓ Partition selection by name (default partition for bare paths)
//   - ✓ Multi-sector directory chaining (sliced NEXTS pointer)
//   - ✓ Subdirectory navigation
//   - ✓ Balanced data tree traversal (all depths, SLICE-assembled next-tree pointers)
//   - ✓ Holes in files (read as $00)
//   - ✓ File entry parsing with attributes, 3-char filetypes, timestamps
//   - ✗ Bitmap allocation reading (blocks free always 0)
//   - ✗ Write operations
//   - ✗ REL file side data / Link file resolution (LNK listed, not followed)

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

        if (!readHeader())
        {
            Debug_printv("Failed to read HDD/CFS header");
            return;
        }

        // Start at the image root (partition list)
        seekDirectory("");
    };

    // 4-byte CFS pointer: byte0 = flags + LBA bits 27-24, bytes 1-3 = LBA
    // high/mid/low (big-endian). CHS format (LBA bit clear) is not supported.
    struct Pointer {
        uint8_t b[4];

        bool isLBA() const { return (b[0] & 0x40) != 0; }
        bool isHidden() const { return (b[0] & 0x80) != 0; }   // file pointers ($14)
        bool isValid() const { return (b[0] & 0x80) != 0; }    // partition start pointer
        uint8_t slice() const { return (b[0] >> 4) & 0x03; }   // NEXTS / SLICE bits

        uint32_t getLBA() const {
            return ((uint32_t)(b[0] & 0x0F) << 24) |
                   ((uint32_t)b[1] << 16) |
                   ((uint32_t)b[2] << 8) |
                   b[3];
        }

        // Hole / end-of-chain marker: all zero except the SLICE bits
        bool isZero() const {
            return (b[0] & 0xCF) == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0;
        }
    } __attribute__((packed));

    // Boot sector (sector 0)
    struct BootSector {
        uint8_t reserved0;          // $00
        uint8_t default_partition;  // $01: DP (0-15)
        Pointer last_sector;        // $02-$05
        uint8_t reserved1[2];       // $06-$07
        char id[16];                // $08-$17: "C64 CFS V 0.11B "
        Pointer part_dir;           // $18-$1B: Partition directory pointer
        Pointer part_dir_backup;    // $1C-$1F: Backup location
        char disk_label[16];        // $20-$2F: Global disk label ($20 padded)
    } __attribute__((packed));

    // Partition entry (32 bytes)
    struct PartitionEntry {
        char name[16];          // $00-$0F: Partition name ($00 padded)
        Pointer start;          // $10-$13: Start sector (VALID/HIDDEN/WRITEABLE flags)
        Pointer end;            // $14-$17: End sector (TYPE in flags)
        Pointer deleted_dir;    // $18-$1B: CFS: deleted directory sector
        Pointer root_dir;       // $1C-$1F: CFS: root directory sector

        bool isValid() const { return start.isValid(); }
        bool isHidden() const { return (start.b[0] & 0x20) != 0; }
        // TYPE is the high nibble of the end pointer with the LBA bit cleared
        uint8_t getType() const { return (end.b[0] >> 4) & 0x0B; }
        bool isCFS() const { return getType() == 0x01; }
        bool isGEOS() const { return getType() == 0x02; }
    } __attribute__((packed));

    // Directory entry (32 bytes)
    struct DirectoryEntry {
        char filename[16];      // $00-$0F: Filename ($00 padded)
        uint8_t info[4];        // $10-$13: filesize (normal, LE) / @this dir (label) / $00 (subdir)
        Pointer pointer;        // $14-$17: @data tree / @subdirectory / @parent dir; carries NEXTS
        uint8_t attributes;     // $18: CLOSED/DELETEABLE/READABLE/WRITEABLE/EXECUTEABLE/FILETYPE
        char type_str[3];       // $19-$1B: filetype string ("PRG", "DIR", "DEL", ...)
        uint8_t timestamp[4];   // $1C-$1F: packed creation/modification time

        bool isClosed() const { return (attributes & 0x80) != 0; }
        bool isDeleteable() const { return (attributes & 0x40) != 0; }
        bool isReadable() const { return (attributes & 0x20) != 0; }
        bool isWriteable() const { return (attributes & 0x10) != 0; }
        bool isExecutable() const { return (attributes & 0x08) != 0; }
        uint8_t getFileType() const { return attributes & 0x07; }

        bool isFree() const { return getFileType() == 0 && !isClosed(); }
        bool isSeparator() const { return getFileType() == 0 && isClosed(); }
        bool isNormalFile() const { return getFileType() == 1; }
        bool isRELFile() const { return getFileType() == 2; }
        bool isDirType() const { return getFileType() == 3; }
        bool isLabel() const { return isDirType() && !isClosed(); }
        bool isDirectory() const { return isDirType() && isClosed(); }
        bool isLink() const { return getFileType() == 4; }

        uint32_t getFilesize() const {
            return (uint32_t)info[0] | ((uint32_t)info[1] << 8) |
                   ((uint32_t)info[2] << 16) | ((uint32_t)info[3] << 24);
        }

        time_t getTimestamp() const;
    } __attribute__((packed));

    // Directory sector (512 bytes): 16 entries, the @Next directory sector
    // pointer is sliced into the NEXTS bits of the 16 entry pointers
    struct DirectorySector {
        DirectoryEntry entries[16];
    } __attribute__((packed));

    enum PathResult { PATH_NOT_FOUND, PATH_FILE, PATH_DIR };

    // Path navigation: [PARTITION/]DIR/.../FILE
    bool seekDirectory(std::string path);
    PathResult resolvePath(std::string path);

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
        Pointer pointer;
        bool is_directory;
        bool is_hidden;
    };

    BootSector boot_sector;
    PartitionEntry partition_entries[16];

    bool partition_list = false;    // at image root: list partitions
    uint32_t dir_start_lba = 0;     // first sector of the current directory
    std::string dir_label;

    // Directory walk state (sequential entry iteration)
    DirectorySector dir_buf;
    uint32_t walk_lba = 0;          // sector currently in dir_buf (0 = none)
    uint8_t walk_pos = 0;           // next entry slot to examine (0-15)
    uint16_t walk_count = 0;        // listable entries delivered so far

    // File read state
    Pointer file_tree;              // data tree pointer of the selected file
    uint8_t tree_depth = 1;

    // Tree/data sector cache
    uint8_t tree_buf[512];
    uint32_t tree_cache_lba = 0xFFFFFFFF;

    Header header;
    Entry entry;

    bool readHeader() override;
    bool readSector(uint32_t lba, uint8_t *buf);

    bool selectPartitionByName(std::string name);   // "" = default partition
    bool seekPartitionEntry(uint16_t index);
    bool enterDirectory(std::string name);

    bool readDirSector(uint32_t lba);
    uint32_t nextDirSector();                       // assemble NEXTS pointer
    void restartDirWalk();

    bool seekEntry(std::string filename) override;
    bool seekEntry(uint16_t index) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    // Data tree traversal
    bool loadTreeSector(uint32_t lba);
    Pointer assembleNextTree(uint8_t k);            // from cached tree sector
    bool dataSectorForPos(uint32_t pos, uint32_t *lba, bool *hole);
    static uint64_t treeCoverage(uint8_t depth);

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

    bool isDirectory() override;
    bool exists() override;

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
