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

#include "lbr.h"

#include "endianness.h"

/********************************************************
 * Streams
 ********************************************************/

int8_t LBRMStream::loadEntries()
{
    std::string signature = readUntil(0x20);
    if (signature[0] != 'D' || signature[1] != 'W' || signature[2] != 'B')
    {
        std::cout << "Error: invalid signature, not an LBR file?" << std::endl;
        return -1;
    }
    std::string count = readUntil(0x20);
    seekCurrent(1); // cr
    entry_count = atoi(count.c_str());

    //Debug_printv("signature[%s] count[%s] entry_count[%d]", signature.c_str(), count.c_str(), entry_count);

    for (int i = 0; i < entry_count; ++i)
    {
        std::string filename = readUntil(0x0D);
        std::string type = readUntil(0x0D);
        seekCurrent(1); // space
        std::string size = readUntil(0x20);
        seekCurrent(1); // cr

        //Debug_printv("i[%d] filename[%s] type[%s] size[%s]", i, filename.c_str(), type.c_str(), size.c_str());

        // Add Entry to array
        Entry e;
        e.filename = filename;
        e.size = atoi(size.c_str());
        e.type = decodeType(type);
        entries.push_back(e);

        //Debug_printv("i[%d] filename[%s] type[%s] size[%d]", i, e.filename.c_str(), e.type.c_str(), e.size);
    }

    // Calculate offset for start of entry
    uint32_t offset = _position;
    for (auto &e : entries)
    {
        e.offset = offset;
            offset += e.size;

        //Debug_printv("name[%s] type[%s] size[%d] offset[%d]", e.filename.c_str(), e.type.c_str(), e.size, e.offset);
    }

    return entry_count;
}


bool LBRMStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if (filename.size())
    {
        size_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (readEntry(index))
        {
            std::string entryFilename = entry.filename.c_str();
            uint8_t i = entryFilename.find_first_of(0xA0);
            entryFilename = entryFilename.substr(0, i);
            //mstr::rtrimA0(entryFilename);
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

bool LBRMStream::seekEntry( uint16_t index )
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

uint32_t LBRMStream::readFile(uint8_t *buf, uint32_t size)
{
    uint32_t bytesRead = 0;
    bytesRead += containerStream->read(buf, size);

    return bytesRead;
}

bool LBRMStream::seekPath(std::string path)
{
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if (seekEntry(path))
    {
        // auto entry = containerImage->entry;
        auto type = entry.type.c_str();
        uint32_t data_offset = entry.offset;
        Debug_printv("filename [%.16s] type[%s] start_address[%lu] data_offset[%lu]", entry.filename.c_str(), type, entry.offset, data_offset);

        // Calculate file size
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

bool LBRMFile::rewindDirectory()
{
    dirIsOpen = true;
    Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto image = ImageBroker::obtain<LBRMStream>("lbr", url);
    if (image == nullptr)
        return false;

    image->resetEntryCounter();

    // Set Media Info Fields
    media_header = mstr::format("%.16s", image->header.name.c_str());
    media_id = image->header.id_dos;
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    // mstr::toUTF8(media_image);

    Debug_printv("media_header[%s] media_id[%s] media_blocks_free[%d] media_block_size[%d] media_image[%s]", media_header.c_str(), media_id.c_str(), media_blocks_free, media_block_size, media_image.c_str());

    return true;
}

MFile *LBRMFile::getNextFileInDir()
{

    if (!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<LBRMStream>("lbr", sourceFile->url);
    if (image == nullptr)
        goto exit;

    if (image->getNextImageEntry())
    {
        std::string filename = image->entry.filename;
        uint8_t i = filename.find_first_of(0xA0);
        filename = filename.substr(0, i);
        // mstr::rtrimA0(filename);
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (sourceFile->url + "/" + fileName).c_str() );

        auto file = MFSOwner::File(sourceFile->url + "/" + filename);
        file->extension = image->entry.type;
        //Debug_printv("entry[%s] ext[%s]", fileName.c_str(), file->extension.c_str());
        
        return file;
    }

exit:
    // Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}

