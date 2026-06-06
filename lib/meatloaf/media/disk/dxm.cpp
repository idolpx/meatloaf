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

// .D1M/D2M/D4M - CMD FD2000/FD4000 Disk Image Format
//
// https://cbm8bit.com/8bit/commodore/server/Unrenamed%20Achives/browse/c64/d2m
// https://web.archive.org/web/20180925144409/https://cbm8bit.com/articles/user-contributions/howto_d1m_d2m_d4m
// https://ist.uwaterloo.ca/~schepers/formats/D2M-DNP.TXT
//


#include "dxm.h"
#include "d64.h"
#include "d71.h"
#include "d81.h"
#include "../hd/dnp.h"


bool DXMMStream::initializePartitionTable()
{
    // For D64, we only have one partition, so this is just a formality
    return true;
}

bool DXMMStream::seekPartition( std::string filename )
{
    // Read Directory Entries
    if (filename.size())
    {
        uint16_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (seekPartition(index))
        {
            if (wildcard && !(entry.file_type & 0b00000111)) // Skip non-PRG files
            {
                index++;
                continue;
            }

            std::string entryFilename = entry.filename;
            size_t i = entryFilename.find_first_of(0xA0);
            if (i == std::string::npos || i > 16) i = 16;
            entryFilename = entryFilename.substr(0, i);
            entryFilename = mstr::toUTF8(entryFilename);

            //Debug_printv("index[%d] track[%d] sector[%d] filename[%s] entry.filename[%.16s]", index, track, sector, filename.c_str(), entryFilename.c_str());
            //Debug_printv("filename[%s] entry[%s]", filename.c_str(), entryFilename.c_str());

            if ( mstr::compareFilename(entryFilename, filename, wildcard) )
            {
                return true;
            }

            index++;
        }

        Debug_printv("File not found!");
    }

    entry.next_track = 0;
    entry.next_sector = 0;
    entry.blocks = 0;
    entry.filename[0] = '\0';

    return false;
}

bool DXMMStream::seekPartition( uint8_t index )
{
    // Calculate Sector offset & Entry offset
    // 8 Entries Per Sector, 32 bytes Per Entry
    index--;
    uint16_t sectorOffset = index / 8;
    uint16_t entryOffset = (index % 8) * 32;

    //Debug_printv("----------");
    //Debug_printv("index[%d] sectorOffset[%d] entryOffset[%d] entry_index[%d]", index, sectorOffset, entryOffset, entry_index);

    if (index == 0 || index != entry_index)
    {
        // Start at first sector of directory
        next_track = 0;
        if (!seekSector(
                partitions[partition].directory_track,
                partitions[partition].directory_sector,
                partitions[partition].directory_offset))
            return false;

        // Find sector with requested entry
        do
        {
            readContainer((uint8_t *)&partition, sizeof(partition));

            //Debug_printv("sectorOffset[%d] -> track[%d] sector[%d]", sectorOffset, track, sector);

        } while (sectorOffset-- > 0);
        if (!seekSector(track, sector, entryOffset))
            return false;
    }
    else
    {
        if (entryOffset == 0)
        {
            if (next_track == 0)
                return false;

            //Debug_printv("Follow link track[%d] sector[%d] entryOffset[%d]", next_track, next_sector, entryOffset);
            if (!seekSector(next_track, next_sector, entryOffset))
                return false;
        }
    }

    readContainer((uint8_t *)&partition_entry, sizeof(partition_entry));

    std::string e = mstr::toHex((uint8_t *)&partition_entry, sizeof(partition_entry));
    //Debug_printv("file_type[%02X] file_name[%.16s] entry[%s]", entry.file_type, entry.filename, e.c_str());

    partition_index = index + 1;

    return true;
}

bool DXMMStream::readPartition( uint8_t index ) {
    return seekPartition(index);
}
bool DXMMStream::writePartition( uint8_t index) {
    if ( seekPartition(index - 1) ) {
        return writeContainer((uint8_t*)&partition_entry, sizeof(partition_entry));
    }
    return false;
}


bool DXMMFile::rewindPartitionTable()
{
    dirIsOpen = true;
    //Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto image = ImageBroker::obtain<DXMMStream>("dxm", url);
    if (image == nullptr)
        return false;

    //Debug_printv("image->url[%s]", image->url.c_str());
    image->resetPartitionCounter();

    // Set Media Info Fields
    //Debug_printv("name[%s]", image->header.name);
    //Debug_printv("id_dos[%s]", image->header.id_dos);
    media_header = mstr::format("%.16s", image->header.name);
    mstr::A02Space(media_header);
    media_id = mstr::format("%.5s", image->header.id_dos);
    mstr::A02Space(media_id);
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    return true;
}


MFile* DXMMFile::getNextPartition()
{
    bool r = false;

    if (!dirIsOpen)
        rewindPartitionTable();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<DXMMStream>("dxm", url);
    if (image == nullptr)
        goto exit;

    r = image->getNextPartition();

    if (r)
    {
        std::string name = image->partition_entry.name;
        size_t i = name.find_first_of(0xA0);
        if (i == std::string::npos || i > 16) i = 16;
        name = name.substr(0, i);
        // mstr::rtrimA0(name);
        mstr::replaceAll(name, "/", "\\");
        //Debug_printv( "entry[%s]", (url + "/" + filename).c_str() );

        std::string entryUrl;
        entryUrl.reserve(url.size() + 1 + name.size());
        entryUrl = url; entryUrl += '/'; entryUrl += name;
        auto file = MFSOwner::File(entryUrl);
        file->name = name;  // Use actual CBM entry name, not container image name
        file->extension = image->decodeType(image->entry.file_type);
        file->size = image->entry.blocks * image->block_size;
        file->is_dir = false;
        file->is_hidden = false;

        Debug_printv("name[%s] ext[%s][%02X] size[%lu] is_dir[%d] is_hidden[%d]", file->name.c_str(), file->extension.c_str(), image->entry.file_type, file->size, file->is_dir, file->is_hidden);

        return file;
    }

exit:
    // Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}