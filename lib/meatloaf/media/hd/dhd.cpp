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

#include "dhd.h"

#include <cstring>

bool DHDMStream::readPartitionTable()
{
    // CMD HD boot code signature found at track 0, sector 5, offset $F0
    // of the system partition
    static const uint8_t hdmagic[16] = {
        0x43, 0x4D, 0x44, 0x20, 0x48, 0x44, 0x20, 0x20,   // "CMD HD  "
        0x8D, 0x03, 0x88, 0x8E, 0x02, 0x88, 0xEA, 0x60
    };

    uint32_t image_size = containerStream->size();
    uint8_t cfg[256];

    // The system partition sits on a 64 KiB boundary
    sys_base = 0xFFFFFFFF;
    for (uint32_t base = 0; base + 0x600 <= image_size; base += 65536)
    {
        if (!containerStream->seek(base + 0x500))
            break;
        if (readContainer(cfg, sizeof(cfg)) != sizeof(cfg))
            break;
        if (memcmp(&cfg[0xF0], hdmagic, sizeof(hdmagic)) == 0)
        {
            sys_base = base;
            break;
        }
    }

    if (sys_base == 0xFFFFFFFF)
    {
        Debug_printv("No CMD HD system partition found");
        return false;
    }

    default_part = cfg[0xE2];

    // Partition table on track 1 of the system partition: 32-byte entries,
    // 8 per 256-byte sector, laid out contiguously. Entry 0 is the system
    // partition itself.
    part_table.clear();
    uint8_t buf[32];
    for (uint16_t i = 1; i <= 254; i++)
    {
        uint32_t off = sys_base + 65536 + (uint32_t)i * 32;
        if (off + 32 > image_size)
            break;
        if (!containerStream->seek(off))
            break;
        if (readContainer(buf, sizeof(buf)) != sizeof(buf))
            break;

        uint8_t type = buf[2];
        if (type < 1 || type > 4)
            continue;

        PartEntry p;
        p.number = i;
        p.type = type;
        p.name = std::string((char *)&buf[5], 16);
        size_t e = p.name.find((char)0xA0);
        if (e != std::string::npos)
            p.name.resize(e);
        // 3-byte big-endian offset/size in 512-byte LBA blocks
        p.start = (((uint32_t)buf[0x15] << 16) | ((uint32_t)buf[0x16] << 8) | buf[0x17]) * 512;
        p.size = (((uint32_t)buf[0x1D] << 16) | ((uint32_t)buf[0x1E] << 8) | buf[0x1F]) * 512;
        part_table.push_back(p);

        Debug_printv("partition[%d] type[%d] name[%s] start[%lu] size[%lu]",
                     p.number, p.type, p.name.c_str(), p.start, p.size);
    }

    Debug_printv("CMD HD sys_base[%lu] default[%d] partitions[%d]",
                 sys_base, default_part, part_table.size());
    return part_table.size() > 0;
}

bool DHDMStream::selectPartitionByName(std::string name)
{
    const PartEntry *sel = nullptr;

    if (name.empty())
    {
        // Default partition
        for (auto &p : part_table)
        {
            if (p.number == default_part)
            {
                sel = &p;
                break;
            }
        }
        if (!sel && part_table.size())
            sel = &part_table[0];
    }
    else
    {
        bool wildcard = (mstr::contains(name, "*") || mstr::contains(name, "?"));
        for (auto &p : part_table)
        {
            std::string pn = mstr::toUTF8(p.name);
            if (mstr::compareFilename(pn, name, wildcard))
            {
                sel = &p;
                break;
            }
        }
    }

    if (!sel)
        return false;

    return configurePartition(*sel);
}

bool DHDMStream::configurePartition(const PartEntry &p)
{
    partition = 0;
    partition_base = p.start;
    cur_type = p.type;
    dir_track = 0;
    dir_sector = 0;
    entry_index = 0;

    std::vector<BlockAllocationMap> b;
    Partition part = {};

    switch (p.type)
    {
        case 2: // 1541 emulation partition (D64 layout)
            sectorsPerTrack = { 17, 18, 19, 21 };
            interleave = { 3, 10 };
            b = { { 18, 0, 0x04, 1, 35, 4 } };
            part = { 18, 0, 0x90, 18, 1, 0x00, 0, 0, 0, 0, 0, b };
            break;

        case 3: // 1571 emulation partition (D71 layout)
            sectorsPerTrack = { 17, 18, 19, 21 };
            interleave = { 3, 6 };
            b = { { 18, 0, 0x04, 1, 35, 4 },
                  { 53, 0, 0x00, 36, 70, 3 } };
            part = { 18, 0, 0x90, 18, 1, 0x00, 0, 0, 0, 0, 0, b };
            break;

        case 4: // 1581 emulation partition (D81 layout)
            sectorsPerTrack = { 40 };
            interleave = { 1, 1 };
            b = { { 40, 1, 0x10, 1, 40, 6 },
                  { 40, 2, 0x10, 41, 80, 6 } };
            part = { 40, 0, 0x04, 40, 3, 0x00, 0, 0, 0, 0, 0, b };
            break;

        default: // 1 = Native partition (DNP layout)
        {
            sectorsPerTrack = { 256 };
            interleave = { 1, 1 };
            uint8_t end_track = p.size / 65536;
            if (p.size % 65536)
                end_track++;
            if (end_track == 0)
                end_track = 1;
            b = { { 1, 2, 0x20, 1, end_track, 32 } };
            part = { 1, 1, 0x04, 1, 0, 0x00, 0, 0, 0, 0, 0, b };
            break;
        }
    }

    partitions.clear();
    partitions.push_back(part);

    readHeader();

    if (p.type == 1)
    {
        // Native partitions link to their root directory chain from the
        // header block's first two bytes (like DNP)
        std::string hdr = readBlock(1, 1);
        if (hdr.size() == block_size && (uint8_t)hdr[0] != 0)
        {
            partitions[0].directory_track = (uint8_t)hdr[0];
            partitions[0].directory_sector = (uint8_t)hdr[1];
        }
    }

    Debug_printv("selected partition[%d] type[%d] name[%s]", p.number, p.type, p.name.c_str());
    return true;
}

bool DHDMStream::seekPartitionEntry(uint16_t index)
{
    if (index == 0 || index > part_table.size())
        return false;

    const PartEntry &p = part_table[index - 1];

    memset(&entry, 0, sizeof(entry));
    memset(entry.filename, 0xA0, sizeof(entry.filename));
    memcpy(entry.filename, p.name.c_str(), std::min(p.name.size(), sizeof(entry.filename)));
    entry.file_type = 0x86; // closed DIR entry - listed as a directory
    uint32_t blocks = p.size / block_size;
    entry.blocks = (blocks > 0xFFFF) ? 0xFFFF : blocks;

    entry_index = index;
    return true;
}
