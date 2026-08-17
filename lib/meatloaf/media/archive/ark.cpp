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

#include "ark.h"

//#include "endianness.h"
#include "utils.h"

/********************************************************
 * Streams
 ********************************************************/

bool ARKMStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if (filename.size())
    {
        size_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (seekEntry(index))
        {
            std::string entryFilename = entry.filename;
            size_t i = entryFilename.find_first_of(0xA0);
            if (i == std::string::npos || i > 16) i = 16;
            entryFilename = entryFilename.substr(0, i);
            //mstr::rtrimA0(entryFilename);
            entryFilename = mstr::toUTF8(entryFilename);

            //Debug_printv("filename[%s] entry.filename[%s]", filename.c_str(), entryFilename.c_str());

            if ( mstr::compareFilename(entryFilename, filename, wildcard) )
            {
                return true;
            }

            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}


bool ARKMStream::seekEntry( uint16_t index )
{
    // A stream that has not been through a directory listing has no entry
    // count, and the default (size_t)-1 makes the bound below useless: the
    // walk runs off the end of the directory into file data, and seekPath()
    // derives its data offset from the same number.
    if ( entry_count == (size_t)-1 && !readHeader() )
        return false;

    //Debug_printv("entry_count[%d] entry_index[%d] index[%d]", entry_count, entry_index, index);

    if ( !index || index > entry_count )
        return false;

    // Calculate Entry offset
    // 29 bytes Per Entry + 1 byte to include entry count
    index--;
    uint16_t entryOffset = (index * 29) + 1;

    if (!containerStream->seek(entryOffset))
        return false;

    readContainer((uint8_t *)&entry, sizeof(entry));

    entry_index = index + 1;

    //Debug_printv("entry_index[%d] entryOffset[%u] blocks[%u] filename[%s]", entry_index, entryOffset, entry.blocks, entry.filename);

    return true;
}


uint32_t ARKMStream::readFile(uint8_t *buf, uint32_t size)
{
    uint32_t bytesRead = 0;
    bytesRead += readContainer(buf, size);

    return bytesRead;
}

bool ARKMStream::seekPath(std::string path)
{
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if (seekEntry(path))
    {
        // auto entry = containerImage->entry;
        auto type = decodeType(entry.file_type);
        media_header_size = (entry_count * 29 + 1);
        media_data_offset = ((media_header_size + (254 - 1)) / 254) * 254; // Round to nearest block
        uint32_t entry_data_offset = media_data_offset;

        // Calculate file size
        uint16_t blocks = entry.blocks;
        _size = ((blocks - 1) * 254) + entry.lsu_byte - 1;

        Debug_printv("entry_index[%d] entry_data_offset[%lu] blocks[%u] _size[%lu] lsu[%d]", entry_index, entry_data_offset, blocks, _size, entry.lsu_byte);

        // Add the space every preceding file occupies (blocks * 254 each) to
        // reach this one's data. This walked with readEntry(), which is not
        // overridden here, so the base always returned false and left `entry`
        // untouched - the loop then added THIS file's block count once per
        // preceding entry, and every file after the first was read from the
        // wrong offset.
        uint16_t target = entry_index;
        for ( uint16_t c = 1; c < target; c++ )
        {
            if ( !seekEntry(c) )
                return false;

            entry_data_offset += ( entry.blocks * 254);
            Debug_printv("c[%d] blocks[%u] entry_data_offset[%lu]", c, entry.blocks, entry_data_offset);
        }

        // seekEntry() above overwrote `entry`; restore the one being opened
        if ( target > 1 && !seekEntry(target) )
            return false;

        Debug_printv("filename [%.16s] type[%s] data_offset[%lu] blocks[%u] file_size[%lu]", entry.filename, type.c_str(), entry_data_offset, blocks, _size);


        // Set position to beginning of file
        _position = 0;
        containerStream->seek(entry_data_offset);

        Debug_printv("File Size: size[%ld] available[%ld] position[%ld]", _size, available(), _position);
        return true;
    }

    Debug_printv("Not found! [%s]", path.c_str());
    return false;
};

/********************************************************
 * File implementations
 ********************************************************/

bool ARKMFile::rewindDirectory()
{
    dirIsOpen = true;
    Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto image = ImageBroker::obtain<ARKMStream>("ark", url);
    if (image == nullptr)
        return false;

    // Read Header (sets the entry count)
    image->readHeader();
    image->resetEntryCounter();

    // Set Media Info Fields
    media_header = mstr::toUTF8(mstr::format("%.16s", image->header.name.c_str()));
    media_id = mstr::toUTF8(image->header.id_dos);
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_archive = name;
    // mstr::toUTF8(media_image);

    Debug_printv("media_header[%s] media_id[%s] media_blocks_free[%d] media_block_size[%d] media_image[%s]", media_header.c_str(), media_id.c_str(), media_blocks_free, media_block_size, media_image.c_str());

    return true;
}

MFile *ARKMFile::getNextFileInDir()
{

    if (!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<ARKMStream>("ark", url);
    if (image == nullptr)
        goto exit;

    if (image->getNextImageEntry())
    {
        std::string filename = image->entry.filename;
        size_t i = filename.find_first_of(0xA0);
        if (i == std::string::npos || i > 16) i = 16;
        filename = filename.substr(0, i);
        // mstr::rtrimA0(filename);
        // PETSCII in the archive, UTF-8 from here on - seekEntry() converts
        // the stored name the same way, so a listed name can be typed back.
        filename = mstr::toUTF8(filename);
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (sourceFile->url + "/" + fileName).c_str() );

        // sourceFile->url is the archive's PARENT DIRECTORY, so this used to
        // name a file next to the archive rather than one inside it. The
        // entry then resolved to a plain flash file with isCBM false, and
        // `ls` printed the raw PETSCII bytes instead of the UTF-8 form that
        // seekEntry() matches against - so the listed name could not be typed
        // back. fullUrl() rejoins any pathInStream, per the rule on joining a
        // child name onto an MFile.
        auto file = MFSOwner::File(fullUrl() + "/" + filename);
        file->name = filename;  // Use actual entry name, not container image name
        file->extension = image->decodeType(image->entry.file_type);
        // Same expression seekPath() uses for _size: the last block holds
        // lsu_byte bytes, so the file is exact rather than an estimate. A
        // zero-block entry would underflow the subtraction.
        file->size = image->entry.blocks
                   ? ((image->entry.blocks - 1) * 254) + image->entry.lsu_byte - 1
                   : 0;
        //Debug_printv("entry[%s] ext[%s]", fileName.c_str(), file->extension.c_str());
        file->is_dir = 0;

        return file;
    }


exit:
    // Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}

