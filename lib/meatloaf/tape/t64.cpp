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

#include "t64.h"

//#include "meat_broker.h"
#include "endianness.h"

/********************************************************
 * Streams
 ********************************************************/

std::string T64MStream::decodeType(uint8_t file_type, bool show_hidden)
{
    std::string type = "PRG";

    if ( file_type == 0x00 )
    {
        if ( entry.entry_type == 1 )
            type = "TAP";
        if ( entry.entry_type > 1 )
            type = "FRZ";
    }

    return " " + type;
}

bool T64MStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if ( filename.size() )
    {
        size_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );
        while ( seekEntry( index ) )
        {
            std::string entryFilename = entry.filename;
            entryFilename = entryFilename.substr(0, 16);
            //uint8_t i = entryFilename.find_first_of(0x20); // (in PETASCII, padded with $20, not $A0)
            //entryFilename = entryFilename.substr(0, (i > 16 ? 16 : i));
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

bool T64MStream::seekEntry( uint16_t index )
{
    if ( index > header.entry_count )
        return false;

    // Calculate Sector offset & Entry offset
    index--;
    uint16_t entryOffset = 0x40 + (index * sizeof(entry));

    //Debug_printv("----------");
    //Debug_printv("index[%d] sectorOffset[%d] entryOffset[%d] entry_index[%d]", index, sectorOffset, entryOffset, entry_index);

    containerStream->seek(entryOffset);
    containerStream->read((uint8_t *)&entry, sizeof(entry));

    //Debug_printv("index[%d] file_type[%02X] file_name[%.16s]", index, entry.file_type, entry.filename);

    entry_index = index + 1;    
    return true;
}


uint32_t T64MStream::readFile(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    if ( _position < 2)
    {
        //Debug_printv("position[%d] load00[%d] load01[%d]", _position, _load_address[0], _load_address[1]);
        if ( size > 1 )
        {
            buf[0] = _load_address[0];
            buf[1] = _load_address[1];
            bytesRead += containerStream->read(buf+2, size - 2);
        }
        bytesRead += 2;
    }
    else
    {
        bytesRead += containerStream->read(buf, size);
    }

    return bytesRead;
}


bool T64MStream::seekPath(std::string path) {
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if ( seekEntry(path) )
    {
        //auto entry = containerImage->entry;
        auto type = decodeType(entry.file_type).c_str();
        Debug_printv("filename [%.16s] type[%s] start_address[%lu] end_address[%lu] data_offset[%lu]", entry.filename, type, entry.start_address, entry.end_address, entry.data_offset);

        // Calculate file size
        _size = ( entry.end_address - entry.start_address ) + 2; // 2 bytes for load address

        // Load Address
        _load_address[0] = entry.start_address & 0xFF;
        _load_address[1] = entry.start_address & 0xFF00;
        Debug_printv("load00[%d] load01[%d]", _load_address[0], _load_address[1]);

        // Set position to beginning of file
        _position = 0;
        containerStream->seek(entry.data_offset);

        Debug_printv("File Size: size[%lu] available[%lu] position[%lu]", _size, available(), _position);

        return true;
    }
    else
    {
        Debug_printv( "Not found! [%s]", path.c_str());
    }

    return false;
};

/********************************************************
 * File implementations
 ********************************************************/

bool T64MFile::isDirectory() {
    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if ( pathInStream == "" )
        return true;
    else
        return false;
};

bool T64MFile::rewindDirectory() {
    dirIsOpen = true;
    Debug_printv("sourceFile->url[%s]", sourceFile->url.c_str());
    auto image = ImageBroker::obtain<T64MStream>(sourceFile->url);
    if (image == nullptr)
        return false;

    image->resetEntryCounter();

    // Set Media Info Fields
    media_header = mstr::format("%.16s", image->header.name);
    media_id = " T64 ";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    //mstr::toUTF8(media_image);

    Debug_printv("media_header[%s] media_id[%s] media_blocks_free[%d] media_block_size[%d] media_image[%s]", media_header.c_str(), media_id.c_str(), media_blocks_free, media_block_size, media_image.c_str());

    return true;
}

MFile* T64MFile::getNextFileInDir() {

    if(!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<T64MStream>(sourceFile->url);
    if ( image == nullptr )
        goto exit;

    if ( image->getNextImageEntry() )
    {
        std::string filename = image->entry.filename;
        filename = filename.substr(0, 16);
        //uint8_t i = filename.find_first_of(0x20); // (in PETASCII, padded with $20, not $A0)
        //filename = filename.substr(0, (i > 16 ? 16 : i));
        // mstr::rtrimA0(filename);
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (sourceFile->url + "/" + filename).c_str() );

        auto file = MFSOwner::File(sourceFile->url + "/" + filename);
        file->extension = image->decodeType(image->entry.file_type);
        file->size = ( image->entry.end_address - image->entry.start_address ) + 2; // 2 bytes for load address

        Debug_printv( "entry[%s] ext[%s] size[%lu]", filename.c_str(), file->extension.c_str(), file->size);
        
        return file;
    }

exit:
    //Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}

