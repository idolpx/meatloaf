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
    // Byte 0: Month HIGH + Seconds (0-59)
    // Byte 1: Month LOW + Minutes (0-59)
    // Byte 2: Hour HIGH + Year (0-63, base 1980)
    // Byte 3: Hour LOW + Day (1-31)

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

/********************************************************
 * Streams
 ********************************************************/

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

    // Extract header info
    header.id = std::string(boot_sector.id, 16);
    header.disk_label = std::string(boot_sector.disk_label, 16);
    // Trim trailing spaces
    while (!header.disk_label.empty() && header.disk_label.back() == ' ')
        header.disk_label.pop_back();

    Debug_printv("CFS ID: %s", header.id.c_str());
    Debug_printv("Disk Label: %s", header.disk_label.c_str());

    // Read partition directory
    if (!readPartitionDirectory())
    {
        Debug_printv("Failed to read partition directory");
        return false;
    }

    return true;
}

bool HDDMStream::readPartitionDirectory()
{
    // Read partition directory from the location specified in boot sector
    uint32_t part_dir_lba = boot_sector.part_dir.getLBA();

    Debug_printv("Reading partition directory at LBA %d", part_dir_lba);

    containerStream->seek(part_dir_lba * block_size);
    if (readContainer((uint8_t*)partitions, sizeof(partitions)) != sizeof(partitions))
    {
        Debug_printv("Failed to read partition directory");
        return false;
    }

    // Count valid partitions
    header.partition_count = 0;
    for (int i = 0; i < 16; i++)
    {
        if (partitions[i].start.isValid())
        {
            header.partition_count++;
            Debug_printv("Partition %d: %.8s Type:%d Start:%d End:%d",
                i, partitions[i].name, partitions[i].getType(),
                partitions[i].start.getLBA(), partitions[i].end.getLBA());
        }
    }

    Debug_printv("Found %d valid partitions", header.partition_count);

    return true;
}

bool HDDMStream::selectPartition(uint8_t partition_num)
{
    if (partition_num >= 16 || !partitions[partition_num].start.isValid())
    {
        Debug_printv("Invalid partition: %d", partition_num);
        return false;
    }

    if (!partitions[partition_num].isCFS())
    {
        Debug_printv("Partition %d is not CFS type", partition_num);
        return false;
    }

    current_partition = partition_num;
    current_partition_start = partitions[partition_num].start.getLBA();
    current_partition_end = partitions[partition_num].end.getLBA();

    // Root directory is typically at partition_start + 2 (after bitmap sectors)
    // Sector 0: Usage bitmap #1
    // Sector 1: Deleted directory (optional)
    // Sector 2: Root directory
    current_dir_sector = current_partition_start + 2;

    Debug_printv("Selected partition %d: %.8s (LBA %d-%d)",
        partition_num, partitions[partition_num].name,
        current_partition_start, current_partition_end);

    // Read root directory
    return readDirectorySector(current_dir_sector);
}

bool HDDMStream::readDirectorySector(uint32_t lba)
{
    containerStream->seek(lba * block_size);
    if (readContainer((uint8_t*)&current_dir, sizeof(DirectorySector)) != sizeof(DirectorySector))
    {
        Debug_printv("Failed to read directory sector at LBA %d", lba);
        return false;
    }

    current_dir_sector = lba;
    Debug_printv("Read directory sector at LBA %d", lba);

    return true;
}

bool HDDMStream::seekEntry(std::string filename)
{
    // Search for file in current directory
    if (filename.empty())
        return false;

    mstr::replaceAll(filename, "\\", "/");
    bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));

    uint16_t index = 1;
    while (seekEntry(index))
    {
        std::string entryFilename = entry.filename;

        Debug_printv("Comparing: %s vs %s", filename.c_str(), entryFilename.c_str());

        if (mstr::compareFilename(entryFilename, filename, wildcard))
        {
            Debug_printv("Found match: %s", entryFilename.c_str());
            return true;
        }

        index++;
    }

    Debug_printv("File not found: %s", filename.c_str());
    return false;
}

bool HDDMStream::seekEntry(uint16_t index)
{
    if (index == 0)
        return false;

    index--;  // Convert to 0-based

    // Calculate which directory sector and entry index
    uint16_t sector_index = index / 16;
    uint16_t entry_index = index % 16;

    // For now, we only support the first directory sector
    // Full implementation would follow next_sector pointers
    if (sector_index > 0)
    {
        Debug_printv("Multi-sector directories not yet supported");
        return false;
    }

    DirectoryEntry& dir_entry = current_dir.entries[entry_index];

    // Skip empty, deleted, and label entries
    if (dir_entry.isEmpty() || !dir_entry.isClosed())
    {
        return false;
    }

    // Fill in entry structure
    entry.filename = std::string(dir_entry.filename, 8);
    // Trim null padding
    size_t null_pos = entry.filename.find('\0');
    if (null_pos != std::string::npos)
        entry.filename = entry.filename.substr(0, null_pos);

    entry.size = dir_entry.filesize;
    entry.attributes = dir_entry.attributes;
    entry.timestamp = dir_entry.getTimestamp();
    entry.data_tree = dir_entry.data_tree;
    entry.is_directory = dir_entry.isDirectory();

    // Decode file type
    switch (dir_entry.getFileType())
    {
        case 0: entry.type = "DEL"; break;
        case 1: entry.type = "PRG"; break;  // Normal file, could be SEQ/PRG/USR
        case 2: entry.type = "REL"; break;
        case 3: entry.type = "DIR"; break;
        case 4: entry.type = "LNK"; break;
        default: entry.type = "???"; break;
    }

    entry_index = index + 1;

    Debug_printv("Entry[%d]: %s Type:%s Size:%d IsDir:%d",
        index, entry.filename.c_str(), entry.type.c_str(), entry.size, entry.is_directory);

    return true;
}

uint32_t HDDMStream::readDataSector(uint32_t lba, uint8_t* buf, uint32_t size)
{
    if (size > block_size)
        size = block_size;

    containerStream->seek(lba * block_size);
    return readContainer(buf, size);
}

bool HDDMStream::readDataTree(Pointer tree_ptr, uint32_t offset, uint8_t* buf, uint32_t size, uint32_t& bytes_read)
{
    // Simplified data tree reading - reads direct data sectors only
    // Full implementation would recursively traverse tree structure

    if (!tree_ptr.isValid())
    {
        Debug_printv("Invalid tree pointer");
        return false;
    }

    // For now, assume simple files with direct data pointers
    // Real CFS uses a balanced tree structure for large files

    uint32_t lba = tree_ptr.getLBA();
    bytes_read = readDataSector(lba + (offset / block_size), buf, size);

    return bytes_read > 0;
}

uint32_t HDDMStream::readFile(uint8_t* buf, uint32_t size)
{
    // Read file data using the data tree
    if (!entry.data_tree.isValid())
    {
        Debug_printv("Invalid data tree pointer");
        return 0;
    }

    // Limit read to remaining file size
    if (_position + size > _size)
    {
        size = _size - _position;
    }

    if (size == 0)
        return 0;

    uint32_t bytes_read = 0;

    // Simple direct read from data tree
    // Full implementation would traverse the tree structure properly
    if (readDataTree(entry.data_tree, _position, buf, size, bytes_read))
    {
        _position += bytes_read;
        return bytes_read;
    }

    return 0;
}

bool HDDMStream::seekPath(std::string path)
{
    seekCalled = true;
    entry_index = 0;

    // Handle subdirectories in path
    // For now, simple file lookup in root directory
    if (seekEntry(path))
    {
        _size = entry.size;
        _position = 0;

        Debug_printv("File: %s Size:%d", entry.filename.c_str(), entry.size);

        return true;
    }

    Debug_printv("File not found: %s", path.c_str());
    return false;
}

/********************************************************
 * File implementations
 ********************************************************/

bool HDDMFile::rewindDirectory()
{
    dirIsOpen = true;
    Debug_printv("url[%s]", url.c_str());

    auto image = ImageBroker::obtain<HDDMStream>("hdd", url);
    if (image == nullptr)
        return false;

    image->resetEntryCounter();

    // Set Media Info Fields
    media_header = image->header.disk_label;
    media_id = image->header.id;
    media_blocks_free = 0;  // TODO: Calculate from bitmap
    media_block_size = image->block_size;
    media_image = name;

    Debug_printv("media_header[%s] media_id[%s]",
        media_header.c_str(), media_id.c_str());

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

        auto file = MFSOwner::File(url + "/" + filename);
        file->name = filename;  // Use actual entry name, not container image name
        file->extension = image->entry.type;
        file->size = image->entry.size;

        Debug_printv("Entry: %s Type:%s Size:%d",
            filename.c_str(), file->extension.c_str(), file->size);

        return file;
    }

exit:
    dirIsOpen = false;
    return nullptr;
}
