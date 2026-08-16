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

#include "hdd.h"

#include "endianness.h"
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

/********************************************************
 * Utility Functions
 ********************************************************/

time_t HDDMStream::DirectoryEntry::getTimestamp() const
{
    // Decode packed timestamp (4 bytes)
    // Byte 0: Month HIGH (bits 7-6) + Seconds (0-59)
    // Byte 1: Month LOW (bits 7-6) + Minutes (0-59)
    // Byte 2: Hour HIGH (bits 7-6) + Year (0-63, base 1980)
    // Byte 3: Hour LOW (bits 7-5) + Day (1-31)

    uint8_t seconds = timestamp[0] & 0x3F;
    uint8_t month_hi = (timestamp[0] >> 6) & 0x03;

    uint8_t minutes = timestamp[1] & 0x3F;
    uint8_t month_lo = (timestamp[1] >> 6) & 0x03;

    uint8_t year = timestamp[2] & 0x3F;
    uint8_t hour_hi = (timestamp[2] >> 6) & 0x03;

    uint8_t day = timestamp[3] & 0x1F;
    uint8_t hour_lo = (timestamp[3] >> 5) & 0x07;

    uint8_t month = (month_hi << 2) | month_lo;  // 1-12
    uint8_t hour = (hour_hi << 3) | hour_lo;     // 0-23

    // Convert to tm structure
    std::tm t = {};
    t.tm_sec = seconds;
    t.tm_min = minutes;
    t.tm_hour = hour;
    t.tm_mday = day;
    t.tm_mon = month - 1;  // tm_mon is 0-11
    t.tm_year = (1980 + year) - 1900;  // tm_year is years since 1900

    return mktime(&t);
}

// Split an in-image path on '/' (a literal '/' in a filename is encoded
// as '\' in the URL and restored per-component by seekEntry)
static std::vector<std::string> splitPathComponents(const std::string &path)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < path.size())
    {
        size_t p = path.find('/', start);
        if (p == std::string::npos)
            p = path.size();
        if (p > start)
            parts.push_back(path.substr(start, p - start));
        start = p + 1;
    }
    return parts;
}

static std::string trimEntryName(const char *name, size_t max, char pad)
{
    std::string s(name, max);
    size_t e = s.find(pad);
    if (e != std::string::npos)
        s.resize(e);
    return s;
}

/********************************************************
 * Streams
 ********************************************************/

bool HDDMStream::readSector(uint32_t lba, uint8_t *buf)
{
    if (!containerStream->seek(lba * block_size))
        return false;
    return readContainer(buf, block_size) == block_size;
}

bool HDDMStream::readHeader()
{
    // Read boot sector (sector 0)
    containerStream->seek(0);
    if (readContainer((uint8_t*)&boot_sector, sizeof(BootSector)) != sizeof(BootSector))
    {
        Debug_printv("Failed to read boot sector");
        return false;
    }

    // Validate CFS signature
    if (strncmp(boot_sector.id, "C64 CFS", 7) != 0)
    {
        Debug_printv("Invalid CFS signature: %.16s", boot_sector.id);
        return false;
    }

    header.id = std::string(boot_sector.id, 16);
    header.disk_label = std::string(boot_sector.disk_label, 16);
    while (!header.disk_label.empty() && header.disk_label.back() == ' ')
        header.disk_label.pop_back();

    // Read the partition directory (one sector, 16 x 32-byte entries)
    uint32_t part_dir_lba = boot_sector.part_dir.getLBA();
    uint8_t sector[512];
    if (!readSector(part_dir_lba, sector))
    {
        Debug_printv("Failed to read partition directory at LBA %lu", part_dir_lba);
        return false;
    }
    memcpy(partition_entries, sector, sizeof(partition_entries));

    header.partition_count = 0;
    for (int i = 0; i < 16; i++)
    {
        if (partition_entries[i].isValid())
        {
            header.partition_count++;
            Debug_printv("Partition %d: [%.16s] Type:%d Root:%lu",
                i, partition_entries[i].name, partition_entries[i].getType(),
                partition_entries[i].root_dir.getLBA());
        }
    }

    Debug_printv("CFS label[%s] partitions[%d] default[%d]",
        header.disk_label.c_str(), header.partition_count, boot_sector.default_partition);

    header_read = true;
    return true;
}

void HDDMStream::setPartitionExtent(uint32_t start, uint32_t end)
{
    if (start == part_start_lba && end == part_end_lba)
        return;     // same partition, keep the cached free count

    part_start_lba = start;
    part_end_lba = end;
    blocks_free_valid = false;
}

// CFS lays a partition out in repeating groups of 4096 sectors: a usage
// bitmap sector, a backup copy of it, then 4094 data sectors. One bitmap
// sector describes all 4094 of its own group - byte 0 carries 6 bits, bytes
// 1-511 carry 8 each (6 + 511*8 = 4094) - and a SET bit means FREE.
//
// The two spare bits of byte 0 are unused and, like every sector outside the
// partition, must be recorded as USED (0), which is what lets this simply
// count set bits across the whole sector: the spec does not state the bit
// order within a byte, and a plain popcount does not depend on it.
uint32_t HDDMStream::countFreeBlocks()
{
    if (blocks_free_valid)
        return blocks_free;

    blocks_free = 0;
    blocks_free_valid = true;   // don't retry a failed walk on every listing

    if (part_start_lba == 0 || part_end_lba < part_start_lba)
        return 0;

    uint8_t buf[512];
    uint32_t groups = 0;
    for (uint32_t base = part_start_lba; base <= part_end_lba; base += 4096)
    {
        if (!readSector(base, buf))
        {
            Debug_printv("usage bitmap read failed at LBA %lu", base);
            break;
        }

        for (int i = 0; i < 512; i++)
            blocks_free += __builtin_popcount(buf[i]);
        groups++;

        // The count is only ever DISPLAYED through a 16-bit "BLOCKS FREE"
        // line, so once it is past that there is nothing more to learn -
        // and a bitmap sector every 4096 sectors means a 1 GB partition
        // otherwise costs ~500 seek+read round trips on SD (~25 s on the
        // first listing). A mostly empty partition passes the limit within
        // ~16 groups; a small one is walked in full anyway.
        if (blocks_free > 65535)
            break;
    }

    Debug_printv("free blocks[%lu] from [%lu] bitmap group(s)",
                 (unsigned long)blocks_free, (unsigned long)groups);
    return blocks_free;
}

bool HDDMStream::selectPartitionByName(std::string name)
{
    const PartitionEntry *sel = nullptr;

    if (name.empty())
    {
        // Default partition from the boot sector, else first valid CFS one
        uint8_t dp = boot_sector.default_partition & 0x0F;
        if (partition_entries[dp].isValid() && partition_entries[dp].isCFS())
            sel = &partition_entries[dp];
        else
        {
            for (int i = 0; i < 16; i++)
            {
                if (partition_entries[i].isValid() && partition_entries[i].isCFS())
                {
                    sel = &partition_entries[i];
                    break;
                }
            }
        }
    }
    else
    {
        bool wildcard = (mstr::contains(name, "*") || mstr::contains(name, "?"));
        for (int i = 0; i < 16; i++)
        {
            if (!partition_entries[i].isValid())
                continue;
            std::string pn = trimEntryName(partition_entries[i].name, 16, '\0');
            if (mstr::compareFilename(pn, name, wildcard))
            {
                if (!partition_entries[i].isCFS())
                    return false; // only CFS partitions are browsable
                sel = &partition_entries[i];
                break;
            }
        }
    }

    if (!sel)
        return false;

    partition_list = false;
    dir_start_lba = sel->root_dir.getLBA();
    dir_label = trimEntryName(sel->name, 16, '\0');
    setPartitionExtent(sel->start.getLBA(), sel->end.getLBA());
    restartDirWalk();
    entry_index = 0;

    //Debug_printv("selected partition [%s] root[%lu]", dir_label.c_str(), dir_start_lba);
    return true;
}

// 'number' is 1-based and counts only VALID entries - identical to the walk
// in seekPartitionEntry(), and it must stay identical, or the registry and
// the stream would disagree about which partition a number names. 0 is not a
// partition: it means "the currently selected one", which the caller has
// already resolved before reaching here.
bool HDDMStream::selectPartitionByNumber(uint8_t number)
{
    if (number == 0)
        return false;

    uint8_t count = 0;
    for (int i = 0; i < 16; i++)
    {
        const PartitionEntry &pe = partition_entries[i];
        if (!pe.isValid())
            continue;

        if (++count != number)
            continue;

        if (!pe.isCFS())
            return false;   // listed, but not browsable

        partition_list = false;
        dir_start_lba = pe.root_dir.getLBA();
        dir_label = trimEntryName(pe.name, 16, '\0');
        setPartitionExtent(pe.start.getLBA(), pe.end.getLBA());
        restartDirWalk();
        entry_index = 0;
        return true;
    }
    return false;
}

bool HDDMStream::seekPartitionEntry(uint16_t index)
{
    if (index == 0)
        return false;

    uint16_t count = 0;
    for (int i = 0; i < 16; i++)
    {
        const PartitionEntry &pe = partition_entries[i];
        if (!pe.isValid())
            continue;
        count++;
        if (count == index)
        {
            entry.filename = trimEntryName(pe.name, 16, '\0');
            entry.size = 0;
            if (pe.end.getLBA() >= pe.start.getLBA())
                entry.size = (pe.end.getLBA() - pe.start.getLBA() + 1) * block_size;
            entry.type = pe.isCFS() ? "DIR" : (pe.isGEOS() ? "GEO" : "???");
            entry.attributes = 0;
            entry.timestamp = 0;
            entry.pointer = pe.root_dir;
            entry.is_directory = pe.isCFS();
            entry.is_hidden = pe.isHidden();
            entry_index = index;
            return true;
        }
    }
    return false;
}

bool HDDMStream::readDirSector(uint32_t lba)
{
    if (!readSector(lba, (uint8_t*)&dir_buf))
    {
        Debug_printv("Failed to read directory sector at LBA %lu", lba);
        walk_lba = 0;
        return false;
    }
    walk_lba = lba;
    return true;
}

// The @Next directory sector pointer is sliced into 2-bit NEXTS fields of
// the 16 entry pointers: byte 3 from entries 1-4, byte 2 from entries 5-8,
// byte 1 from entries 9-12, byte 0 from entries 13-16 (MSB first each).
uint32_t HDDMStream::nextDirSector()
{
    uint8_t nb[4] = {0, 0, 0, 0};
    for (int e = 0; e < 16; e++)
        nb[3 - (e >> 2)] |= dir_buf.entries[e].pointer.slice() << (6 - 2 * (e & 3));

    if (nb[0] == 0 && nb[1] == 0 && nb[2] == 0 && nb[3] == 0)
        return 0; // end of directory

    Pointer p;
    memcpy(p.b, nb, 4);
    return p.getLBA();
}

void HDDMStream::restartDirWalk()
{
    walk_lba = 0;
    walk_pos = 0;
    walk_count = 0;
}

bool HDDMStream::seekEntry(uint16_t index)
{
    if (partition_list)
        return seekPartitionEntry(index);

    if (index == 0 || dir_start_lba == 0)
        return false;

    // Restart the walk when seeking backwards or when nothing is loaded
    if (index <= walk_count || walk_lba == 0)
    {
        restartDirWalk();
        if (!readDirSector(dir_start_lba))
            return false;
    }

    while (true)
    {
        if (walk_pos >= 16)
        {
            uint32_t nxt = nextDirSector();
            if (nxt == 0)
                return false;
            if (!readDirSector(nxt))
                return false;
            walk_pos = 0;
        }

        const DirectoryEntry &de = dir_buf.entries[walk_pos++];

        if (de.isLabel())
        {
            dir_label = trimEntryName(de.filename, 16, ' ');
            continue;
        }
        if (de.isFree())
            continue;

        walk_count++;
        if (walk_count < index)
            continue;

        // Fill in the friendly entry structure
        entry.filename = trimEntryName(de.filename, 16, '\0');
        if (de.isRELFile())
            entry.size = (uint32_t)de.info[0] | ((uint32_t)de.info[1] << 8) | ((uint32_t)de.info[2] << 16);
        else if (de.isNormalFile())
            entry.size = de.getFilesize();
        else
            entry.size = 0;
        entry.type = trimEntryName(de.type_str, 3, '\0');
        if (entry.type.empty())
        {
            switch (de.getFileType())
            {
                case 0: entry.type = "del"; break;
                case 1: entry.type = "prg"; break;
                case 2: entry.type = "rel"; break;
                case 3: entry.type = "dir"; break;
                case 4: entry.type = "lnk"; break;
                default: entry.type = "???"; break;
            }
        }
        entry.attributes = de.attributes;
        entry.timestamp = de.getTimestamp();
        entry.pointer = de.pointer;
        entry.is_directory = de.isDirectory();
        entry.is_hidden = de.pointer.isHidden() || !de.isClosed();

        entry_index = index;

        // Debug_printv("Entry[%d]: %s Type:%s Size:%lu IsDir:%d IsHidden:%d",
        //              index, entry.filename.c_str(), entry.type.c_str(), entry.size, entry.is_directory, entry.is_hidden);

        return true;
    }
}

bool HDDMStream::seekEntry(std::string filename)
{
    if (filename.empty())
        return false;

    mstr::replaceAll(filename, "\\", "/");
    filename = mstr::toPETSCII2(filename); // CFS uses ASCII, but the stream is PETSCII
    bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));

    // "*" (or any all-wildcard pattern) is the CBM "first entry" idiom and it
    // means the first loadable FILE: a CFS partition root typically opens with
    // the %DELETED FILES% directory, so LOAD"*" must skip directories and
    // separators to reach the first real file. Mirrors D64MStream::seekEntry(),
    // which skips DEL entries for the same reason. A pattern that carries real
    // name characters ("GAM*") is left alone so it can still match a
    // subdirectory for cd.
    bool files_only = wildcard &&
                      filename.find_first_not_of("*?") == std::string::npos;

    uint16_t index = 1;
    while (seekEntry(index))
    {
        if (files_only && (entry.is_directory || (entry.attributes & 0x07) == 0))
        {
            index++;
            continue;
        }

        if (mstr::compareFilename(entry.filename, filename, wildcard))
            return true;

        index++;
    }

    //Debug_printv("File not found: %s", filename.c_str());
    return false;
}

bool HDDMStream::enterDirectory(std::string name)
{
    if (!seekEntry(name))
        return false;
    if (!entry.is_directory)
        return false;

    dir_start_lba = entry.pointer.getLBA();
    if (dir_start_lba == 0)
        return false;

    // Name the directory we just entered. dir_label is otherwise only written
    // when the WALK finds a label entry, so a subdirectory that has none - an
    // empty one always does - kept whatever the parent or partition set, and a
    // listing of it announced the wrong directory. A real label entry found
    // during the walk still overrides this.
    dir_label = entry.filename;

    restartDirWalk();
    entry_index = 0;
    return true;
}

// The partition this stream treats as its root: whatever HDDMFile put in
// selected_partition, else the boot sector's default partition, else the
// first valid CFS partition (which is what selectPartitionByName("") does).
bool HDDMStream::selectCurrentPartition()
{
    if (selected_partition != 0 && selectPartitionByNumber(selected_partition))
        return true;

    return selectPartitionByName("");
}

bool HDDMStream::seekDirectory(std::string path)
{
    // The whole CFS geometry lives in the boot sector and partition directory,
    // and the constructor no longer reads them. Every path walk enters here,
    // so this is where a stream that has not been through a directory listing
    // gets its header - without it the partition table is empty and every
    // path resolves to "not found".
    if (!header_read && !readHeader())
        return false;

    // Reset to the image root: the partition list
    partition_list = true;
    dir_start_lba = 0;
    dir_label.clear();
    restartDirWalk();
    entry_index = 0;

    auto parts = splitPathComponents(path);
    size_t i = 0;

    // The image root is the SELECTED partition's directory, matching the CMD
    // HD/FD behaviour and a real drive. The partition list is served only in
    // response to "$=P", which HDDMFile handles.
    //
    // Only re-resolve a leading component as a partition name when NO partition
    // has been pushed in. HDDMFile::normalizePath() already stripped any
    // partition from the path, so once selected_partition is set, parts[0] is a
    // directory INSIDE that partition - and matching it against the partition
    // table here would silently switch partitions on a name collision.
    // A directly constructed stream (the native tests) has no selection and
    // keeps the legacy name-first behaviour.
    if (selected_partition == 0 && parts.size() && selectPartitionByName(parts[0]))
    {
        i = 1;
    }
    else if (!selectCurrentPartition())
    {
        return false;
    }

    for (; i < parts.size(); i++)
    {
        if (!enterDirectory(parts[i]))
            return false;
    }
    return true;
}

HDDMStream::PathResult HDDMStream::resolvePath(std::string path)
{
    auto parts = splitPathComponents(path);
    if (parts.empty())
        return seekDirectory("") ? PATH_DIR : PATH_NOT_FOUND;

    // Resolve everything up to the last component as directories
    std::string parent;
    for (size_t i = 0; i + 1 < parts.size(); i++)
    {
        if (i) parent += '/';
        parent += parts[i];
    }
    if (!seekDirectory(parent))
        return PATH_NOT_FOUND;

    std::string last = parts.back();

    if (!seekEntry(last))
        return PATH_NOT_FOUND;

    if (entry.is_directory)
        return enterDirectory(last) ? PATH_DIR : PATH_NOT_FOUND;

    return PATH_FILE;
}

/********************************************************
 * Data tree traversal
 ********************************************************/

// A depth-d tree node covers 64 KiB with its own 128 data pointers plus
// 8 subtrees of depth d-1: coverage(1) = 64 KiB, coverage(d) = 64K + 8*coverage(d-1)
uint64_t HDDMStream::treeCoverage(uint8_t depth)
{
    uint64_t cov = 65536ULL;
    for (uint8_t i = 1; i < depth; i++)
        cov = 65536ULL + 8 * cov;
    return cov;
}

// CFS stores the 512 bytes of file data in a data sector as FOUR interleaved
// 128-byte columns: file byte n of that sector lives at sector offset
// (n % 128) * 4 + (n / 128). Only FILE DATA is stored this way - boot,
// partition, directory and tree sectors are all linear, which is why listings
// and the tree walk work when reads do not.
//
// Verified on two independently authored images (a C64 OS 1 GB CF image and
// Soci/Singular's own IDE64 image): de-interleaving turns a PRG into a valid
// BASIC program whose line-link chain walks correctly across sector
// boundaries, and turns the file-association tables into ordered text. The
// published CFS 0.11 spec documents the tree geometry this code already
// implements but says nothing about the byte order inside a data sector.
bool HDDMStream::loadDataSector(uint32_t lba)
{
    if (data_cache_lba == lba)
        return true;
    if (!readSector(lba, data_buf))
        return false;
    data_cache_lba = lba;
    return true;
}

bool HDDMStream::loadTreeSector(uint32_t lba)
{
    if (tree_cache_lba == lba)
        return true;
    if (!readSector(lba, tree_buf))
        return false;
    tree_cache_lba = lba;
    return true;
}

// @Next tree pointer #k (0-based) is sliced into the SLICE bits of data
// pointers k*16+1 .. k*16+16, same method as the directory NEXTS pointer
HDDMStream::Pointer HDDMStream::assembleNextTree(uint8_t k)
{
    uint8_t nb[4] = {0, 0, 0, 0};
    for (int i = 0; i < 16; i++)
    {
        const Pointer *dp = (const Pointer *)&tree_buf[((k * 16) + i) * 4];
        nb[3 - (i >> 2)] |= dp->slice() << (6 - 2 * (i & 3));
    }
    Pointer p;
    memcpy(p.b, nb, 4);
    return p;
}

bool HDDMStream::dataSectorForPos(uint32_t pos, uint32_t *lba, bool *hole)
{
    *hole = false;

    if (file_tree.isZero())
    {
        *hole = true; // file is entirely a hole
        return true;
    }

    uint32_t tree_lba = file_tree.getLBA();
    uint8_t d = tree_depth;
    uint64_t p = pos;

    while (d > 1 && p >= 65536)
    {
        if (!loadTreeSector(tree_lba))
            return false;

        uint64_t sub = treeCoverage(d - 1);
        uint64_t q = p - 65536;
        uint8_t k = q / sub;
        if (k > 7)
            return false;

        Pointer np = assembleNextTree(k);
        if (np.b[0] == 0 && np.b[1] == 0 && np.b[2] == 0 && np.b[3] == 0)
        {
            *hole = true;
            return true;
        }

        tree_lba = np.getLBA();
        p = q % sub;
        d--;
    }

    if (p >= 65536)
    {
        Debug_printv("Tree too shallow for position %lu (depth %d)", pos, tree_depth);
        return false;
    }

    if (!loadTreeSector(tree_lba))
        return false;

    const Pointer *dp = (const Pointer *)&tree_buf[(p / 512) * 4];
    if (dp->isZero())
    {
        *hole = true;
        return true;
    }

    *lba = dp->getLBA();
    return true;
}

uint32_t HDDMStream::readFile(uint8_t* buf, uint32_t size)
{
    uint32_t total = 0;

    while (total < size)
    {
        uint32_t pos = _position + total;
        if (pos >= _size)
            break;

        uint32_t chunk = std::min(size - total, (uint32_t)(block_size - (pos % block_size)));
        chunk = std::min(chunk, _size - pos);

        uint32_t lba = 0;
        bool hole = false;
        if (!dataSectorForPos(pos, &lba, &hole))
            break;

        if (hole)
        {
            memset(buf + total, 0, chunk);
        }
        else
        {
            // A data sector must be read WHOLE and de-interleaved - the
            // requested range cannot be read directly out of the image.
            if (!loadDataSector(lba))
                break;

            const uint32_t col = block_size / 4;
            uint32_t off = pos % block_size;
            for (uint32_t i = 0; i < chunk; i++)
            {
                uint32_t n = off + i;
                buf[total + i] = data_buf[((n % col) * 4) + (n / col)];
            }
        }

        total += chunk;
    }

    return total;
}

bool HDDMStream::seekPath(std::string path)
{
    seekCalled = true;
    entry_index = 0;

    if (mode == std::ios_base::out)
    {
        Debug_printv("CFS write not supported [%s]", path.c_str());
        return false;
    }

    PathResult r = resolvePath(path);
    if (r == PATH_FILE)
    {
        _size = entry.size;
        _position = 0;
        file_tree = entry.pointer;

        // Minimal tree depth that covers the file size
        tree_depth = 1;
        while (tree_depth < 7 && treeCoverage(tree_depth) < _size)
            tree_depth++;

        tree_cache_lba = 0xFFFFFFFF;

        Debug_printv("File: %s Size:%lu TreeDepth:%d", entry.filename.c_str(), _size, tree_depth);
        return true;
    }
    if (r == PATH_DIR)
    {
        // Partition or subdirectory: succeed so broker-cached listing
        // streams open; there is nothing to read as a byte stream.
        _size = 0;
        _position = 0;
        return true;
    }

    Debug_printv("Not found: %s", path.c_str());
    return false;
}

/********************************************************
 * Partition registry
 ********************************************************/

std::map<std::string, HDDImageRegistry::Image> HDDImageRegistry::s_images;
bool HDDImageRegistry::s_probing = false;

const HDDPartition* HDDImageRegistry::Image::byNumber(uint8_t number) const
{
    for (auto &p : parts)
    {
        if (p.number == number)
            return &p;
    }
    return nullptr;
}

const HDDPartition* HDDImageRegistry::Image::byName(std::string name) const
{
    bool wildcard = (mstr::contains(name, "*") || mstr::contains(name, "?"));
    for (auto &p : parts)
    {
        std::string p_name = p.name;
        if (mstr::compareFilename(p_name, name, wildcard))
            return &p;
    }
    return nullptr;
}

bool HDDImageRegistry::Image::trySelect(uint8_t number)
{
    // 0 is not a partition - it means "the currently selected one". byNumber()
    // never returns an entry for it, so this is belt-and-braces, but it states
    // the rule where a reader will look for it.
    if (number == 0)
        return false;

    const HDDPartition* p = byNumber(number);
    if (p == nullptr || p->type != 1)   // only a CFS partition is browsable
        return false;
    selected = number;
    return true;
}

bool HDDImageRegistry::parseInto(MStream* s, Image& img)
{
    if (s == nullptr || !s->isOpen())
        return false;

    HDDMStream::BootSector boot;
    if (!s->seek(0))
        return false;
    if (s->read((uint8_t*)&boot, sizeof(boot)) != sizeof(boot))
        return false;

    if (strncmp(boot.id, "C64 CFS", 7) != 0)
    {
        Debug_printv("Invalid CFS signature");
        return false;
    }

    img.disk_label = std::string(boot.disk_label, 16);
    while (!img.disk_label.empty() &&
           (img.disk_label.back() == ' ' || img.disk_label.back() == '\0'))
        img.disk_label.pop_back();

    // DP is a SLOT index here; it is converted to a partition number below,
    // once the table has been walked and the numbering is known.
    const uint8_t dp_slot = boot.default_partition & 0x0F;
    img.default_part = 0;

    // The partition directory is exactly one 512-byte sector: 16 entries of
    // 32 bytes. There is no second sector and no chaining.
    uint8_t sector[512];
    if (!s->seek(boot.part_dir.getLBA() * 512))
        return false;
    if (s->read(sector, sizeof(sector)) != sizeof(sector))
        return false;

    img.parts.clear();
    uint8_t next_number = 1;                // partitions are numbered from 1
    for (uint8_t i = 0; i < 16; i++)
    {
        HDDMStream::PartitionEntry pe;
        memcpy(&pe, sector + (i * 32), sizeof(pe));

        if (!pe.isValid())
            continue;                       // an invalid slot consumes no number

        HDDPartition p;
        p.number = next_number++;           // must match seekPartitionEntry()
        p.slot = i;
        p.type = pe.getType();
        p.name = trimEntryName(pe.name, 16, '\0');
        p.root_lba = pe.root_dir.getLBA();
        p.size = 0;
        if (pe.end.getLBA() >= pe.start.getLBA())
            p.size = (pe.end.getLBA() - pe.start.getLBA() + 1) * 512;
        p.hidden = pe.isHidden();
        p.writeable = (pe.start.b[0] & 0x10) != 0;
        img.parts.push_back(p);

        Debug_printv("partition[%d] slot[%d] type[%d] name[%s] root[%lu] size[%lu]",
                     p.number, p.slot, p.type, p.name.c_str(), p.root_lba, p.size);
    }

    // Convert DP out of slot space into partition-number space. This is the
    // ONLY place the two meet; everything downstream speaks numbers.
    for (const HDDPartition &p : img.parts)
    {
        if (p.slot == dp_slot) { img.default_part = p.number; break; }
    }

    // The default partition when it names a CFS one, else the first CFS
    // partition. An image with NO CFS partition fails to parse: there is
    // nothing to select and nothing to mount, and the alternative is a
    // `selected` naming a partition trySelect() would refuse.
    if (!img.trySelect(img.default_part))
    {
        bool any = false;
        for (const HDDPartition &p : img.parts)
        {
            if (p.type == 1) { img.selected = p.number; any = true; break; }
        }
        if (!any)
        {
            Debug_printv("No usable CFS partitions");
            return false;
        }
    }

    img.valid = true;
    Debug_printv("CFS label[%s] partitions[%d] default[%d] selected[%d]",
                 img.disk_label.c_str(), img.parts.size(),
                 img.default_part, img.selected);
    return true;
}

std::string HDDImageRegistry::containerOf(const std::string &path)
{
    static const char *ext = ".hdd";
    const size_t elen = 4;

    std::string lower = path;
    for (auto &c : lower)
        c = tolower(c);

    size_t p = lower.find(ext);
    while (p != std::string::npos)
    {
        size_t end = p + elen;
        if (end == lower.size() || lower[end] == '/')
            return path.substr(0, end);
        p = lower.find(ext, p + 1);
    }
    return "";
}

HDDImageRegistry::Image* HDDImageRegistry::obtain(const std::string &containerUrl)
{
    if (containerUrl.empty())
        return nullptr;

    auto it = s_images.find(containerUrl);
    if (it != s_images.end() && it->second.valid)
        return &it->second;

    // (Re-)parse: not yet seen, or the image wasn't readable last time
    Image img;
    if (!parse(containerUrl, img))
        return nullptr;

    s_images[containerUrl] = std::move(img);
    return &s_images[containerUrl];
}

bool HDDImageRegistry::parse(const std::string &containerUrl, Image &img)
{
    // Open the raw image bytes: the probing flag makes HDDMFileSystem decline
    // the path so the underlying filesystem serves it, rather than handing
    // back another HDDMFile whose stream is already decoded.
    s_probing = true;
    std::unique_ptr<MFile> f(MFSOwner::File(containerUrl));
    std::shared_ptr<MStream> s = (f != nullptr) ? f->getSourceStream() : nullptr;
    s_probing = false;

    if (s == nullptr || !s->isOpen())
    {
        Debug_printv("Cannot open CFS image [%s]", containerUrl.c_str());
        return false;
    }

    return parseInto(s.get(), img);
}

bool HDDImageRegistry::select(const std::string &containerUrl, uint8_t number)
{
    Image* img = obtain(containerUrl);
    if (img == nullptr)
        return false;

    // No cached-stream disposal here, unlike DHDImageRegistry::select(): see
    // the class comment. HDDMStream re-derives its position on every call.
    return img->trySelect(number);
}

const HDDPartition* hddResolvePartitionIn(const HDDImageRegistry::Image &img,
                                          const std::string &in_path,
                                          std::string *out_rest,
                                          bool *out_explicit)
{
    if (out_rest) *out_rest = in_path;
    if (out_explicit) *out_explicit = false;

    if (!img.valid)
        return nullptr;

    const HDDPartition *p = nullptr;

    if (!in_path.empty())
    {
        std::string comp = in_path;
        size_t slash = comp.find('/');
        if (slash != std::string::npos)
            comp = comp.substr(0, slash);

        // A component made only of wildcard characters is the CBM "first
        // entry" pattern (LOAD"*"), never a deliberate partition reference.
        // byName() honours wildcards, so a bare "*" would otherwise match the
        // first partition and strip itself from the path, turning LOAD"*" into
        // a listing of that partition's root directory.
        bool wildcard_only = !comp.empty() &&
                             comp.find_first_not_of("*?") == std::string::npos;

        if (!comp.empty() && !wildcard_only)
        {
            bool numeric = comp.find_first_not_of("0123456789") == std::string::npos;
            if (numeric)
            {
                // Range-check BEFORE narrowing to uint8_t. An int silently
                // truncated into byNumber() is what once made "1571" resolve
                // to partition 35 in DHD.
                //
                // v == 0 means "the currently selected partition", exactly as
                // in DHD (vdrive.c:1324) - it must NOT go through byNumber().
                // 16 is the bound because a CFS table holds 16 entries, so
                // that is the highest number the 1-based numbering reaches.
                char *end = nullptr;
                long v = strtol(comp.c_str(), &end, 10);
                if (end != comp.c_str() && *end == '\0' && v >= 0 && v <= 16)
                    p = (v == 0) ? img.current() : img.byNumber((uint8_t)v);
            }
            else
            {
                p = img.byName(comp);
            }

            if (p != nullptr)
            {
                if (out_explicit) *out_explicit = true;
                if (out_rest)
                    *out_rest = (slash == std::string::npos) ? std::string()
                                                            : in_path.substr(slash + 1);
            }
        }
    }

    if (p == nullptr)
        p = img.current();

    return p;
}

const HDDPartition* HDDResolvePartition(const std::string &containerUrl,
                                        const std::string &in_path,
                                        std::string *out_rest,
                                        bool *out_explicit)
{
    if (out_rest) *out_rest = in_path;
    if (out_explicit) *out_explicit = false;

    HDDImageRegistry::Image *img = HDDImageRegistry::obtain(containerUrl);
    if (img == nullptr)
        return nullptr;

    return hddResolvePartitionIn(*img, in_path, out_rest, out_explicit);
}

/********************************************************
 * File implementations
 ********************************************************/

void HDDMFile::normalizePath()
{
    if (normalized)
        return;
    normalized = true;

    if (pathInStream.empty())
        return;

    // "$=P" switches to partition-list mode.
    if (mstr::startsWith(pathInStream, "$=P") || mstr::startsWith(pathInStream, "$=p"))
    {
        listing_partitions = true;
        pathInStream.clear();
        return;
    }

    std::string rest;
    bool explicit_part = false;
    const HDDPartition *p = HDDResolvePartition(
        HDDImageRegistry::containerOf(url), pathInStream, &rest, &explicit_part);

    if (p != nullptr && explicit_part)
    {
        m_part = p->number;
        pathInStream = rest;
    }
}

const HDDPartition* HDDMFile::effectivePartition()
{
    auto img = HDDImageRegistry::obtain(HDDImageRegistry::containerOf(url));
    if (img == nullptr)
        return nullptr;
    return (m_part == 0) ? img->current() : img->byNumber(m_part);
}

void HDDMFile::applyPartition(const std::shared_ptr<HDDMStream>& image)
{
    if (image == nullptr)
        return;

    // 0 when the image has no usable table, which is the stream's "use the
    // boot sector's DP" fallback - the same value m_part uses for "no
    // partition named", since partitions are numbered from 1.
    const HDDPartition *p = effectivePartition();
    image->selected_partition = (p != nullptr) ? p->number : 0;
}

bool HDDMFile::rewindDirectory()
{
    normalizePath();
    dirIsOpen = true;
    Debug_printv("url[%s] pathInStream[%s]", url.c_str(), pathInStream.c_str());

    if (listing_partitions)
    {
        auto img = HDDImageRegistry::obtain(HDDImageRegistry::containerOf(url));
        if (img == nullptr)
        {
            dirIsOpen = false;
            return false;
        }
        part_index = 0;
        media_header = img->disk_label;
        media_id = "IDE64";
        media_blocks_free = 0;
        media_block_size = 512;
        media_image = name;
        media_partition = 255; // system partition
        return true;
    }

    auto image = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (image == nullptr)
    {
        dirIsOpen = false;
        return false;
    }

    if (!image->readHeader())
    {
        Debug_printv("Failed to read HDD/CFS header");
        dirIsOpen = false;
        return false;
    }

    applyPartition(image);
    image->resetEntryCounter();

    if (!image->seekDirectory(pathInStream))
    {
        Debug_printv("directory not found in image [%s]", pathInStream.c_str());
        dirIsOpen = false;
        return false;
    }

    // Set Media Info Fields
    media_header = image->dir_label.empty() ? image->header.disk_label : image->dir_label;
    media_id = "IDE64";
    // Free data sectors from the partition's usage bitmaps. Capped because the
    // CBM "BLOCKS FREE" line carries a 16-bit count, and a CFS partition can
    // hold far more than that.
    media_blocks_free = std::min(image->countFreeBlocks(), (uint32_t)65535);
    media_block_size = image->block_size;
    media_image = name;
    media_partition = image->selected_partition;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    return true;
}

MFile* HDDMFile::getNextFileInDir()
{
    // Same as D64MFile::getNextFileInDir(): a failed rewind has already reset
    // the shared stream's entry counter, so reading on would restart the
    // listing from entry 0 forever.
    if (!dirIsOpen && !rewindDirectory())
        return nullptr;

    if (listing_partitions)
    {
        auto img = HDDImageRegistry::obtain(HDDImageRegistry::containerOf(url));
        if (img == nullptr || part_index >= img->parts.size())
        {
            dirIsOpen = false;
            return nullptr;
        }

        const HDDPartition &p = img->parts[part_index++];
        std::string fname = p.name;
        mstr::replaceAll(fname, "/", "\\");

        // By NUMBER, not name: CFS names are 16 bytes that may contain '/'
        // and spaces, which do not survive a URL path component.
        //auto file = MFSOwner::File(url + "/" + std::to_string((unsigned)p.number));
        auto file = MFSOwner::File(url + "/" + p.name);
        file->name = fname;
        file->extension = (p.type == 1) ? "*CFS"
                        : (p.type == 2) ? " GEOS"
                        : (p.type == 0) ? " ----" : "?";
        file->size = p.number * 256;  // fake size to show partition number
        file->is_dir = (p.type == 1) ? 1 : 0;   // only CFS is browsable
        file->is_hidden = p.hidden;
        return file;
    }

    auto image = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (image == nullptr)
        goto exit;

    applyPartition(image);

    if (image->getNextImageEntry())
    {
        std::string filename = image->entry.filename;
        mstr::replaceAll(filename, "/", "\\");

        // Entry URLs name the partition BY NUMBER, so an entry listed from a
        // partition other than the selected one still resolves back into its
        // own partition rather than into the selection.
        std::string entryUrl = url;
        const HDDPartition *p = effectivePartition();
        if (p != nullptr)
        {
            entryUrl += '/';
            entryUrl += std::to_string((unsigned)p->number);
        }
        if (pathInStream.size()) { entryUrl += '/'; entryUrl += pathInStream; }
        entryUrl += '/'; entryUrl += filename;

        auto file = MFSOwner::File(entryUrl);
        file->name = filename;  // Use actual entry name, not container image name
        file->extension = " " + image->entry.type;
        file->size = image->entry.size + (image->entry.is_directory ? 0 : 256); // real listing shows 256 more bytes for a file than the actual size, so add it back for display
        file->is_dir = image->entry.is_directory;
        file->is_hidden = image->entry.is_hidden;

        // Debug_printv("getNextFileInDir: %s Type:%s Size:%lu IsDir:%d IsHidden:%d",
        //              file->name.c_str(), file->extension.c_str(),
        //              file->size, file->is_dir, file->is_hidden);

        return file;
    }

exit:
    dirIsOpen = false;
    return nullptr;
}

bool HDDMFile::isDirectory()
{
    normalizePath();

    if (listing_partitions)
        return true;

    // Use cached value if set (e.g. by getNextFileInDir)
    if (is_dir != -1)
        return is_dir == 1;

    // Container root is always a directory
    if (pathInStream.empty() || pathInStream == "/")
        return true;

    auto stream = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (stream != nullptr)
    {
        applyPartition(stream);
        return stream->resolvePath(pathInStream) == HDDMStream::PATH_DIR;
    }

    return false;
}

bool HDDMFile::exists()
{
    normalizePath();

    if (listing_partitions)
        return true;

    auto stream = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (stream == nullptr)
        return false;

    applyPartition(stream);

    if (pathInStream.size() && pathInStream != "/")
        return stream->resolvePath(pathInStream) != HDDMStream::PATH_NOT_FOUND;

    return true;
}
