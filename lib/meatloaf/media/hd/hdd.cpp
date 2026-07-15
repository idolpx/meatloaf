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
#include <cstring>

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

    return true;
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
    restartDirWalk();
    entry_index = 0;

    //Debug_printv("selected partition [%s] root[%lu]", dir_label.c_str(), dir_start_lba);
    return true;
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
                case 0: entry.type = "DEL"; break;
                case 1: entry.type = "PRG"; break;
                case 2: entry.type = "REL"; break;
                case 3: entry.type = "DIR"; break;
                case 4: entry.type = "LNK"; break;
                default: entry.type = "???"; break;
            }
        }
        entry.attributes = de.attributes;
        entry.timestamp = de.getTimestamp();
        entry.pointer = de.pointer;
        entry.is_directory = de.isDirectory();
        entry.is_hidden = de.pointer.isHidden() || !de.isClosed();

        entry_index = index;

        //Debug_printv("Entry[%d]: %s Type:%s Size:%lu IsDir:%d",
        //    index, entry.filename.c_str(), entry.type.c_str(), entry.size, entry.is_directory);

        return true;
    }
}

bool HDDMStream::seekEntry(std::string filename)
{
    if (filename.empty())
        return false;

    mstr::replaceAll(filename, "\\", "/");
    bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));

    uint16_t index = 1;
    while (seekEntry(index))
    {
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

    restartDirWalk();
    entry_index = 0;
    return true;
}

bool HDDMStream::seekDirectory(std::string path)
{
    // Reset to the image root: the partition list
    partition_list = true;
    dir_start_lba = 0;
    dir_label.clear();
    restartDirWalk();
    entry_index = 0;

    auto parts = splitPathComponents(path);
    size_t i = 0;

    if (parts.size())
    {
        if (selectPartitionByName(parts[0]))
            i = 1;
        else if (!selectPartitionByName("")) // fall back to default partition
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

    if (partition_list)
    {
        if (selectPartitionByName(last))
            return PATH_DIR;
        if (!selectPartitionByName(""))
            return PATH_NOT_FOUND;
    }

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
            if (!containerStream->seek((lba * block_size) + (pos % block_size)))
                break;
            if (readContainer(buf + total, chunk) != chunk)
                break;
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
 * File implementations
 ********************************************************/

bool HDDMFile::rewindDirectory()
{
    dirIsOpen = true;
    Debug_printv("url[%s] pathInStream[%s]", url.c_str(), pathInStream.c_str());

    auto image = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (image == nullptr)
        return false;

    image->resetEntryCounter();

    if (!image->seekDirectory(pathInStream))
    {
        Debug_printv("directory not found in image [%s]", pathInStream.c_str());
        dirIsOpen = false;
        return false;
    }

    // Set Media Info Fields
    media_header = image->dir_label.empty() ? image->header.disk_label : image->dir_label;
    media_id = "cfs";
    media_blocks_free = 0;  // TODO: count usage bitmap bits
    media_block_size = image->block_size;
    media_image = name;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    return true;
}

MFile* HDDMFile::getNextFileInDir()
{
    if (!dirIsOpen)
        rewindDirectory();

    auto image = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (image == nullptr)
        goto exit;

    if (image->getNextImageEntry())
    {
        std::string filename = image->entry.filename;
        mstr::replaceAll(filename, "/", "\\");

        // Entry URL must include the in-image path (partition/subdirectory)
        std::string entryUrl = url;
        if (pathInStream.size()) { entryUrl += '/'; entryUrl += pathInStream; }
        entryUrl += '/'; entryUrl += filename;
        auto file = MFSOwner::File(entryUrl);
        file->name = filename;  // Use actual entry name, not container image name
        file->extension = image->entry.type;
        file->size = image->entry.size;
        file->is_dir = image->entry.is_directory;
        file->is_hidden = image->entry.is_hidden;

        //Debug_printv("Entry: %s Type:%s Size:%lu Dir:%d",
        //    filename.c_str(), file->extension.c_str(), file->size, (int)file->is_dir);

        return file;
    }

exit:
    dirIsOpen = false;
    return nullptr;
}

bool HDDMFile::isDirectory()
{
    // Use cached value if set (e.g. by getNextFileInDir)
    if (is_dir != -1)
        return is_dir == 1;

    // Container root is always a directory
    if (pathInStream.empty() || pathInStream == "/")
        return true;

    auto stream = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (stream != nullptr)
        return stream->resolvePath(pathInStream) == HDDMStream::PATH_DIR;

    return false;
}

bool HDDMFile::exists()
{
    auto stream = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (stream == nullptr)
        return false;

    if (pathInStream.size() && pathInStream != "/")
        return stream->resolvePath(pathInStream) != HDDMStream::PATH_NOT_FOUND;

    return true;
}
