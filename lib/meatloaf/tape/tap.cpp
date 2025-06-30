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

#include "tap.h"

#include "meat_broker.h"
#include "endianness.h"

/********************************************************
 * Streams
 ********************************************************/

bool TAPMStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if ( filename.size() )
    {
        uint16_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while ( readEntry( index ) )
        {
            std::string entryFilename = entry.filename;
            uint8_t i = entryFilename.find_first_of(0xA0);
            entryFilename = entryFilename.substr(0, i);
            //mstr::rtrimA0(entryFilename);
            entryFilename = mstr::toUTF8(entryFilename);
            Debug_printv("filename[%s] entry.filename[%.16s]", filename.c_str(), entryFilename.c_str());

            // Read Entry From Stream
            if (filename == entryFilename) // Match exact
            {
                return true;
            }
            else if (wildcard) // Wildcard Match
            {
                if (filename == "*") // Match first PRG
                {
                    filename = entryFilename;
                    return true;
                }
                else if ( mstr::compare(filename, entryFilename) )
                {
                    // Move stream pointer to start track/sector
                    return true;
                }
            }
            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}

bool TAPMStream::seekEntry( uint16_t index )
{
    // Calculate Sector offset & Entry offset
    index--;
    uint16_t entryOffset = 0x40 + (index * sizeof(entry));

    //Debug_printv("----------");
    //Debug_printv("index[%d] sectorOffset[%d] entryOffset[%d] entry_index[%d]", index, sectorOffset, entryOffset, entry_index);

    containerStream->seek(entryOffset);
    containerStream->read((uint8_t *)&entry, sizeof(entry));

    //Debug_printv("r[%d] file_type[%02X] file_name[%.16s]", r, entry.file_type, entry.filename);

    //if ( next_track == 0 && next_sector == 0xFF )
    entry_index = index + 1;    
    if ( entry.file_type == 0x00 )
        return false;
    else
        return true;
}


uint32_t TAPMStream::readFile(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    bytesRead += containerStream->read(buf, size);
    _position += bytesRead;

    return bytesRead;
}

bool TAPMStream::seekPath(std::string path) {
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if ( seekEntry(path) )
    {
        //auto entry = containerImage->entry;
        auto type = decodeType(entry.file_type).c_str();
        size_t start_address = UINT16_FROM_LE_UINT16(entry.start_address);
        size_t end_address = UINT16_FROM_LE_UINT16(entry.end_address);
        size_t data_offset = UINT32_FROM_LE_UINT32(entry.data_offset);
        Debug_printv("filename [%.16s] type[%s] start_address[%zu] end_address[%zu] data_offset[%zu]", entry.filename, type, start_address, end_address, data_offset);

        // Calculate file size
        _size = ( end_address - start_address );

        // Set position to beginning of file
        containerStream->seek(entry.data_offset);

        Debug_printv("File Size: size[%ld] available[%ld]", _size, available());
        
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

std::shared_ptr<MStream> TAPMFile::getDecodedStream(std::shared_ptr<MStream> is) {
    Debug_printv("[%s]", url.c_str());

    return std::make_shared<TAPMStream>(is);
}


bool TAPMFile::isDirectory() {
    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if ( pathInStream == "" )
        return true;
    else
        return false;
};

bool TAPMFile::rewindDirectory() {
    dirIsOpen = true;
    Debug_printv("sourceFile->url[%s]", sourceFile->url.c_str());
    auto image = ImageBroker::obtain<TAPMStream>(sourceFile->url);
    if ( image == nullptr )
        Debug_printv("image pointer is null");

    image->resetEntryCounter();

    // Read Header
    image->readHeader();

    // Set Media Info Fields
    media_header = mstr::format("%.24", image->header.name);
    media_id = "tap";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    //mstr::toUTF8(media_image);

    Debug_printv("media_header[%s] media_id[%s] media_blocks_free[%d] media_block_size[%d] media_image[%s]", media_header.c_str(), media_id.c_str(), media_blocks_free, media_block_size, media_image.c_str());

    return true;
}

MFile* TAPMFile::getNextFileInDir() {

    if(!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<TAPMStream>(sourceFile->url);
    if ( image == nullptr )
        goto exit;

    if ( image->getNextImageEntry() )
    {
        std::string filename = mstr::format("%.16s", image->entry.filename);
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (sourceFile->url + "/" + filename).c_str() );

        auto file = MFSOwner::File(sourceFile->url + "/" + filename);
        file->extension = image->decodeType(image->entry.file_type);
        
        return file;
    }

exit:
    //Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}


