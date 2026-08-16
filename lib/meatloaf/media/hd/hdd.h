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
#include <map>
#include <vector>


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

        // if (!readHeader())
        // {
        //     Debug_printv("Failed to read HDD/CFS header");
        //     return;
        // }

        // // Start at the image root (partition list)
        // seekDirectory("");
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

    // Boot sector (sector 0). Offsets per the CFS 0.11 spec, whose table is
    // colspan-encoded: "Unused" spans $00-$02, DP is $03, and @Last disk
    // sector spans $04-$07. Confirmed against every image in .archive/hdd/,
    // where @Last disk sector is @Partition directory backup + 1.
    struct BootSector {
        uint8_t reserved0[3];       // $00-$02: unused
        uint8_t default_partition;  // $03: DP (0-15)
        Pointer last_sector;        // $04-$07
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

    // The partition NUMBER (1-based over valid entries) this stream treats as
    // its root. 0 means "no selection - use the boot sector's default", which
    // is unambiguous precisely because partitions are numbered from 1. A
    // directly constructed stream (tests, ImageBroker rebuilds) gets 0;
    // HDDMFile overwrites it with whatever the registry or the path resolved.
    //
    // The stream deliberately does NOT consult HDDImageRegistry itself: that
    // would need MFSOwner::File(), which the native test stubs abort on.
    uint8_t selected_partition = 0;

    // Path navigation: [PARTITION/]DIR/.../FILE
    bool seekDirectory(std::string path);
    PathResult resolvePath(std::string path);

    // Free data sectors in the SELECTED partition, from its usage bitmaps.
    // Cached: it costs one sector read per 2 MB of partition and CFS is
    // read-only here, so the count cannot change under us.
    uint32_t countFreeBlocks();

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
    bool header_read = false;       // boot sector + partition directory parsed

    bool partition_list = false;    // at image root: list partitions
    uint32_t dir_start_lba = 0;     // first sector of the current directory
    std::string dir_label;

    // Extent of the selected partition, for the usage-bitmap walk
    uint32_t part_start_lba = 0;
    uint32_t part_end_lba = 0;
    uint32_t blocks_free = 0;
    bool blocks_free_valid = false;

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

    // The last data sector read, still interleaved as stored (see
    // loadDataSector()). Cached because a sector must be read whole even
    // when the caller wants a few bytes of it.
    uint8_t data_buf[512];
    uint32_t data_cache_lba = 0xFFFFFFFF;

    Header header;
    Entry entry;

    bool readHeader() override;
    bool readSector(uint32_t lba, uint8_t *buf);

    void setPartitionExtent(uint32_t start, uint32_t end);
    bool selectPartitionByName(std::string name);   // "" = default partition
    bool selectPartitionByNumber(uint8_t number);   // 1-based, valid entries only
    bool selectCurrentPartition();
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
    bool loadDataSector(uint32_t lba);
    bool loadTreeSector(uint32_t lba);
    Pointer assembleNextTree(uint8_t k);            // from cached tree sector
    bool dataSectorForPos(uint32_t pos, uint32_t *lba, bool *hole);
    static uint64_t treeCoverage(uint8_t depth);

private:
    friend class HDDMFile;
};


/********************************************************
 * Partition registry
 ********************************************************/

// TWO numbering spaces, and they must not be confused:
//   number - 1-based, counting only VALID table entries. What paths, CP<n>,
//            "$=P" and the `partition` command all speak. Never 0: that
//            number means "the currently selected partition", as in DHD.
//   slot   - the raw 0-15 index into the 16-entry partition directory, and
//            what the boot sector's DP byte holds. Converted into `number`
//            once, at parse time; nothing downstream sees a slot.
struct HDDPartition {
    uint8_t     number;      // 1-based over valid entries
    uint8_t     slot;        // raw table index, 0-15
    uint8_t     type;        // 0=unformatted, 1=CFS, 2=GEOS, 3-11 reserved
    std::string name;        // ASCII, $00 padding trimmed
    uint32_t    root_lba;    // @Root directory (entry +$1C)
    uint32_t    size;        // bytes: (end - start + 1) * 512
    bool        hidden;      // @Start bit 5: excluded from a plain listing
    bool        writeable;   // @Start bit 4 (recorded; CFS support is read-only)
};

// Per-image partition table and selection state, keyed by container URL.
//
// Deliberately SMALLER than DHDImageRegistry: there is no cached_part, no
// brokerUrl() and no dispose-on-select. DHD needs those because ImageBroker
// caches one DECODED D64/D71/D81/DNP stream per image and cannot tell
// partitions apart. HDDMStream re-derives its whole position from
// seekDirectory(pathInStream) on every operation, so a cached stream holds no
// partition identity that could go stale.
class HDDImageRegistry {
public:
    struct Image {
        bool        valid = false;
        uint8_t     default_part = 0;
        uint8_t     selected = 0;
        std::string disk_label;
        std::vector<HDDPartition> parts;

        const HDDPartition* byNumber(uint8_t number) const;
        const HDDPartition* byName(std::string name) const;
        const HDDPartition* current() const { return byNumber(selected); }

        // Selects a partition if it exists and is CFS. Leaves the selection
        // untouched and returns false otherwise - including for 0, which
        // means "the currently selected partition" and is never a real one.
        bool trySelect(uint8_t number);
    };

    // Parses a boot sector + partition directory from an ALREADY OPEN stream.
    // Split out from parse() so the registry is testable natively, where
    // MFSOwner::File() aborts.
    static bool parseInto(MStream* s, Image& img);

    static Image* obtain(const std::string& containerUrl);
    static bool   select(const std::string& containerUrl, uint8_t number);

    // True while the registry reads the raw image bytes, so
    // HDDMFileSystem::handles() declines the path and the underlying
    // filesystem serves them instead of another HDDMFile.
    static bool probing() { return s_probing; }

    // Path of the ".hdd" container within 'path', or "" if none.
    static std::string containerOf(const std::string& path);

private:
    static bool parse(const std::string& containerUrl, Image& img);

    static std::map<std::string, Image> s_images;
    static bool s_probing;
};

// Resolve which partition an in-image path refers to, WITHOUT changing the
// image's selected partition.
//
// Resolution order for the FIRST component: an in-range partition number
// 0-16, then byName(), otherwise it is not a partition and the current
// selection applies. A partition wins over a same-named file; such a file
// stays reachable as "<image>/<number>/<file>".
//
// As in DHD, a partition number of 0 means "the currently selected
// partition" - it is NOT a table entry, and must never reach byNumber().
//
// hddResolvePartitionIn() is the pure core, taking an already-parsed image so
// it can be tested without MFSOwner. HDDResolvePartition() is the wrapper the
// firmware calls.
const HDDPartition* hddResolvePartitionIn(const HDDImageRegistry::Image& img,
                                          const std::string& in_path,
                                          std::string* out_rest = nullptr,
                                          bool* out_explicit = nullptr);

const HDDPartition* HDDResolvePartition(const std::string& containerUrl,
                                        const std::string& in_path,
                                        std::string* out_rest = nullptr,
                                        bool* out_explicit = nullptr);


/********************************************************
 * File implementations
 ********************************************************/

class HDDMFile: public MFile {
public:

    HDDMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
        media_image = name;
        isPETSCII = true;  // CFS uses ASCII but filenames are PETSCII
    };

    ~HDDMFile() {
        // don't close the stream here!
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        normalizePath();
        Debug_printv("[%s]", url.c_str());
        auto stream = std::make_shared<HDDMStream>(is);
        applyPartition(stream);
        return stream;
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDirectory() override;
    bool exists() override;

    bool isDir = true;
    bool dirIsOpen = false;

protected:
    // Handle the partition part of pathInStream once per MFile. "$=P"
    // switches to partition-list mode; a leading component naming a partition
    // binds THIS path to it and is stripped, so the rest resolves inside it.
    // It deliberately does NOT call select(): naming a partition in a path
    // must not change what the image has selected.
    void normalizePath();

    // The partition this MFile operates on, or nullptr when the image has no
    // usable partition table.
    const HDDPartition* effectivePartition();

    // Writes the resolved partition into the stream, which is how the stream
    // learns which directory is its root. Safe to call with a null stream.
    void applyPartition(const std::shared_ptr<HDDMStream>& image);

    bool normalized = false;
    bool listing_partitions = false;
    uint16_t part_index = 0;

    // The partition NUMBER this MFile's path names. 0 = the path named none,
    // so the image's current selection applies - the same convention and the
    // same sentinel value DHDPartitionMFile::m_part uses.
    uint8_t m_part = 0;
};


/********************************************************
 * FS
 ********************************************************/

class HDDMFileSystem: public MFileSystem
{
public:
    HDDMFileSystem(): MFileSystem("hdd") {};

    bool handles(std::string fileName) override {
        // Decline while the registry reads the raw image bytes
        if (HDDImageRegistry::probing())
            return false;
        return byExtension(".hdd", fileName);
    }

    MFile* getFile(std::string path) override {
        return new HDDMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_HDD */
