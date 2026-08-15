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

#include "csm.h"

#include <cstring>

/********************************************************
 * Streams
 ********************************************************/

std::string CSMMStream::decodeType(uint8_t file_type, bool show_hidden)
{
    // The CBM tape header types that carry data are 1/3 (program, relocatable
    // and not) and 2/4 (sequential data and its header). Type 5 ends the tape
    // and never reaches here - readHeader() stops on it.
    switch ( file_type )
    {
        case type_seq_data:
        case type_seq_header:
            return " SEQ";

        default:
            return " PRG";
    }
}

bool CSMMStream::readHeader()
{
    // entry_count is assigned unconditionally at the end, so it doubles as the
    // "this container has already been walked" marker - including for a file
    // that yielded no entries at all, which must not be re-walked on every
    // listing.
    if ( entry_count != (size_t)-1 )
        return !entries.empty();

    entries.clear();

    if ( containerStream == nullptr )
    {
        entry_count = 0;
        return false;
    }

    const uint32_t container_size = containerStream->size();
    uint32_t offset = 0;

    while ( entries.size() < max_entries )
    {
        // A header block that does not fit is the end of a truncated tape.
        if ( offset + header_block_size > container_size )
            break;

        uint8_t hdr[header_field_size];
        if ( !containerStream->seek(offset) )
            break;
        if ( readContainer(hdr, header_field_size) != header_field_size )
            break;

        // End of tape: a header block with no data block after it.
        if ( hdr[0] == type_end_tape )
            break;

        Entry e;
        e.file_type     = hdr[0];
        e.start_address = (uint16_t)hdr[1] | ((uint16_t)hdr[2] << 8);
        e.end_address   = (uint16_t)hdr[3] | ((uint16_t)hdr[4] << 8);

        // The data length is derived from the addresses, so a header claiming
        // an end below its start describes nothing we can read.
        if ( e.end_address < e.start_address )
            break;

        memcpy(e.filename, hdr + 5, 16);
        e.filename[16] = '\0';

        // The name is a fixed-width field padded with spaces, and sometimes
        // with $A0. rtrimPad() strips both - a CBM tape header is the case its
        // own comment cites. A name that is all padding trims to empty, which
        // is what several real tapes carry and what gets listed.
        std::string trimmed(e.filename);
        mstr::rtrimPad(trimmed);
        strncpy(e.filename, trimmed.c_str(), sizeof(e.filename) - 1);
        e.filename[sizeof(e.filename) - 1] = '\0';

        e.data_offset = offset + header_block_size;
        e.data_length = e.end_address - e.start_address;

        // A tape truncated mid-file serves the bytes that are actually there
        // rather than reading off the end of the container.
        uint32_t available_bytes = container_size - e.data_offset;
        if ( e.data_length > available_bytes )
            e.data_length = available_bytes;

        entries.push_back(e);

        // Advance by the length the header DECLARES, not the clamped one: on a
        // whole tape they are equal, and on a truncated one this lands past the
        // container end so the next iteration stops.
        offset = e.data_offset + ( e.end_address - e.start_address );
    }

    entry_count = entries.size();

    Debug_printv("url[%s] entries[%d]", url.c_str(), entries.size());

    return !entries.empty();
}

bool CSMMStream::seekEntry( std::string filename )
{
    if ( filename.size() )
    {
        size_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );

        // First match wins: a tape can legitimately carry the same name twice
        // (the two ABDUCTOR entries of Abductor.csm are a header loader and its
        // payload), and the earlier one is the one a LOAD wants.
        while ( seekEntry( index ) )
        {
            std::string entryFilename = entry.filename;
            entryFilename = mstr::toUTF8(entryFilename);

            if ( mstr::compareFilename(entryFilename, filename, wildcard) )
                return true;

            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}

bool CSMMStream::seekEntry( uint16_t index )
{
    // The entry list is built by walking the container, and a stream that has
    // gone straight to a LOAD has never been through a directory listing - so
    // this is where it gets walked.
    if ( entry_count == (size_t)-1 && !readHeader() )
        return false;

    if ( index == 0 || index > entries.size() )
        return false;

    entry = entries[index - 1];
    entry_index = index;

    return true;
}

uint32_t CSMMStream::readFile(uint8_t* buf, uint32_t size)
{
    uint32_t bytesRead = 0;

    // CSM data blocks hold raw program bytes - the load address is in the
    // header block, so the two bytes a PRG starts with are synthesized here.
    if ( _position < 2 )
    {
        if ( size > 1 )
        {
            buf[0] = _load_address[0];
            buf[1] = _load_address[1];
            bytesRead += readContainer(buf + 2, size - 2);
        }
        bytesRead += 2;
    }
    else
    {
        bytesRead += readContainer(buf, size);
    }

    return bytesRead;
}

bool CSMMStream::seekPath(std::string path)
{
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    if ( seekEntry(path) )
    {
        Debug_printv("filename[%s] type[%s] start_address[%u] end_address[%u] data_offset[%lu]",
                     entry.filename, decodeType(entry.file_type).c_str(),
                     entry.start_address, entry.end_address, entry.data_offset);

        // data_length is the clamped one, so a truncated tape reports the size
        // it can actually serve.
        _size = entry.data_length + 2; // 2 bytes for load address

        // Load Address
        _load_address[0] = entry.start_address & 0xFF;
        _load_address[1] = ( entry.start_address >> 8 ) & 0xFF;

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

bool CSMMFile::rewindDirectory()
{
    dirIsOpen = true;

    auto image = ImageBroker::obtain<CSMMStream>("csm", url);
    if (image == nullptr)
        return false;

    if ( !image->readHeader() )
    {
        dirIsOpen = false;
        return false;
    }

    image->resetEntryCounter();

    // Set Media Info Fields. A CSM carries no tape title, so the image's own
    // name stands in for one.
    media_header = name;
    auto dot = media_header.find_last_of('.');
    if ( dot != std::string::npos )
        media_header = media_header.substr(0, dot);
    mstr::toUpper(media_header);
    if ( media_header.size() > 16 )
        media_header = media_header.substr(0, 16);

    media_id = " CSM ";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    Debug_printv("media_header[%s] media_id[%s] media_image[%s]", media_header.c_str(), media_id.c_str(), media_image.c_str());

    return true;
}

MFile* CSMMFile::getNextFileInDir()
{
    // A failed rewind has already reset the entry counter on the shared
    // ImageBroker stream, so reading on would hand back entry 0 forever.
    if ( !dirIsOpen && !rewindDirectory() )
        return nullptr;

    auto image = ImageBroker::obtain<CSMMStream>("csm", url);
    if ( image == nullptr )
        goto exit;

    if ( image->getNextImageEntry() )
    {
        std::string filename = image->entry.filename;
        mstr::replaceAll(filename, "/", "\\");

        auto file = MFSOwner::File(url + "/" + filename);
        file->name = filename;  // Use actual entry name, not container image name
        file->extension = image->decodeType(image->entry.file_type);
        file->size = image->entry.data_length + 2; // 2 bytes for load address
        file->is_dir = 0;

        Debug_printv( "entry[%s] ext[%s] size[%lu]", filename.c_str(), file->extension.c_str(), file->size);

        return file;
    }

exit:
    dirIsOpen = false;
    return nullptr;
}
