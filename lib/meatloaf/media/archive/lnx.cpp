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

#include "lnx.h"

#include "endianness.h"

/********************************************************
 * Streams
 ********************************************************/

void LNXMStream::skipBasicLoader()
{
    // Skip the BASIC loader program (typically 94 bytes)
    // We'll search for the LNX header signature "LYNX" instead of hardcoding offset
    containerStream->seek(0);

    uint8_t buffer[256];
    uint32_t bytesRead = containerStream->read(buffer, 256);

    // Search for "LYNX" signature in first 256 bytes
    for (uint32_t i = 0; i < bytesRead - 4; i++)
    {
        if (buffer[i] == 'L' && buffer[i+1] == 'Y' && buffer[i+2] == 'N' && buffer[i+3] == 'X')
        {
            // Found LYNX signature, position stream at start of header
            containerStream->seek(i);
            Debug_printv("Found LYNX signature at offset[%d]", i);
            return;
        }
    }

    // If not found, assume typical offset
    Debug_printv("LYNX signature not found, using default offset");
    containerStream->seek(0x5E);  // Typical offset after BASIC loader
}

bool LNXMStream::readHeader()
{
    skipBasicLoader();

    // Read signature line (contains "LYNX")
    header.signature = readUntil(0x0D);  // Read until CR
    if (!mstr::contains(header.signature, "LYNX"))
    {
        Debug_printv("Error: invalid signature[%s], not a LNX file?", header.signature.c_str());
        return false;
    }

    // Read directory size in blocks (ASCII number with spaces)
    std::string dir_blocks = readUntil(0x0D);
    header.directory_blocks = atoi(dir_blocks.c_str());

    // Read number of entries (ASCII number with spaces)
    std::string count = readUntil(0x0D);
    header.entry_count = atoi(count.c_str());
    entry_count = header.entry_count;

    // Read creator information (optional)
    header.creator = readUntil(0x0D);

    Debug_printv("signature[%s] dir_blocks[%d] entry_count[%d] creator[%s]",
        header.signature.c_str(), header.directory_blocks, header.entry_count, header.creator.c_str());

    return true;
}

int8_t LNXMStream::loadEntries()
{
    // Skip to start of directory entries (after header padding to 254-byte boundary)
    // The directory starts at next 254-byte block boundary
    uint32_t current_pos = containerStream->position();
    uint32_t dir_start = ((current_pos + block_size - 1) / block_size) * block_size;
    containerStream->seek(dir_start);

    Debug_printv("Loading %d entries from offset[%d]", entry_count, dir_start);

    for (int i = 0; i < entry_count; ++i)
    {
        Entry e;

        // Read filename (16 chars, PETASCII, padded with $A0)
        uint8_t filename_buf[17];
        containerStream->read(filename_buf, 16);
        filename_buf[16] = '\0';
        e.filename = std::string((char*)filename_buf, 16);

        // Skip CR after filename
        containerStream->read(filename_buf, 1);  // CR

        // Read block count (ASCII number)
        std::string block_count = readUntil(0x0D);
        e.block_count = atoi(block_count.c_str());

        // Read filetype (P/S/R/U)
        std::string type = readUntil(0x0D);
        e.type = decodeType(type);

        // Read LSU (Last Sector Used)
        std::string lsu = readUntil(0x0D);
        e.lsu = atoi(lsu.c_str());

        // For REL files, read record size
        if (type == "R")
        {
            std::string rec_size = readUntil(0x0D);
            e.rel_record_size = atoi(rec_size.c_str());
        }
        else
        {
            e.rel_record_size = 0;
        }

        // Calculate size: (blocks - 1) * 254 + LSU
        if (e.block_count > 0)
        {
            e.size = (e.block_count - 1) * block_size + e.lsu;
        }
        else
        {
            e.size = 0;
        }

        entries.push_back(e);

        Debug_printv("i[%d] filename[%.16s] type[%s] blocks[%d] lsu[%d] size[%d]",
            i, e.filename.c_str(), e.type.c_str(), e.block_count, e.lsu, e.size);
    }

    // Calculate file data start offset
    // Files start after directory blocks (aligned to 254-byte boundaries)
    uint32_t data_start = dir_start + (header.directory_blocks * block_size);

    // Assign offsets to each entry
    uint32_t offset = data_start;
    for (auto &e : entries)
    {
        e.offset = offset;
        // Each file is padded to 254-byte boundaries
        uint32_t padded_size = ((e.size + block_size - 1) / block_size) * block_size;
        offset += padded_size;

        Debug_printv("name[%.16s] type[%s] size[%d] offset[%d]",
            e.filename.c_str(), e.type.c_str(), e.size, e.offset);
    }

    return entry_count;
}


bool LNXMStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if (filename.size())
    {
        size_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (seekEntry(index))
        {
            std::string entryFilename = entry.filename.c_str();
            uint8_t i = entryFilename.find_first_of(0xA0);
            entryFilename = entryFilename.substr(0, i);
            entryFilename = mstr::toUTF8(entryFilename);

            Debug_printv("filename[%s] entry.filename[%s]", filename.c_str(), entryFilename.c_str());

            if ( mstr::compareFilename(filename, entryFilename, wildcard) )
            {
                return true;
            }

            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}

bool LNXMStream::seekEntry( uint16_t index )
{
    if ( index && index <= entries.size() )
    {
        index--;
        entry = entries[index];
        entry_index = index + 1;
        return true;
    }

    return false;
}

uint32_t LNXMStream::readFile(uint8_t *buf, uint32_t size)
{
    uint32_t bytesRead = 0;

    // Don't read beyond the actual file size
    if (_position + size > _size)
    {
        size = _size - _position;
    }

    if (size > 0)
    {
        bytesRead += containerStream->read(buf, size);
        _position += bytesRead;
    }

    return bytesRead;
}

bool LNXMStream::seekPath(std::string path)
{
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if (seekEntry(path))
    {
        auto type = entry.type.c_str();
        uint32_t data_offset = entry.offset;
        Debug_printv("filename[%.16s] type[%s] start_offset[%lu] size[%lu]",
            entry.filename.c_str(), type, entry.offset, entry.size);

        // Set file size
        _size = entry.size;

        // Set position to beginning of file
        _position = 0;
        containerStream->seek(data_offset);

        Debug_printv("File Size: size[%ld] available[%ld] position[%ld]", _size, available(), _position);

        return true;
    }
    else
    {
        Debug_printv("Not found! [%s]", path.c_str());
    }

    return false;
};

/********************************************************
 * File implementations
 ********************************************************/

bool LNXMFile::rewindDirectory()
{
    dirIsOpen = true;
    Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto image = ImageBroker::obtain<LNXMStream>("lnx", url);
    if (image == nullptr)
        return false;

    image->resetEntryCounter();

    // Set Media Info Fields
    media_header = "LYNX ARCHIVE";
    media_id = mstr::format("%.16s", image->header.creator.c_str());
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;

    Debug_printv("media_header[%s] media_id[%s] media_blocks_free[%d] media_block_size[%d] media_image[%s]",
        media_header.c_str(), media_id.c_str(), media_blocks_free, media_block_size, media_image.c_str());

    return true;
}

MFile *LNXMFile::getNextFileInDir()
{
    if (!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<LNXMStream>("lnx", sourceFile->url);
    if (image == nullptr)
        goto exit;

    if (image->getNextImageEntry())
    {
        std::string filename = image->entry.filename;
        uint8_t i = filename.find_first_of(0xA0);
        filename = filename.substr(0, i);
        mstr::replaceAll(filename, "/", "\\");

        auto file = MFSOwner::File(sourceFile->url + "/" + filename);
        file->extension = image->entry.type;
        file->size = image->entry.size;

        Debug_printv("entry[%s] ext[%s] size[%d]", filename.c_str(), file->extension.c_str(), file->size);

        return file;
    }

exit:
    dirIsOpen = false;
    return nullptr;
}
