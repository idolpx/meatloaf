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
#include <map>
#include <mutex>

/********************************************************
 * Shared tape state
 ********************************************************/

std::shared_ptr<CSMState> CSMState::obtain(const std::string &url)
{
    static std::map<std::string, std::weak_ptr<CSMState>> registry;
    static std::mutex mtx;

    std::lock_guard<std::mutex> lock(mtx);

    for (auto it = registry.begin(); it != registry.end(); )
    {
        if (it->second.expired())
            it = registry.erase(it);
        else
            ++it;
    }

    auto &slot = registry[url];
    auto state = slot.lock();
    if (state == nullptr)
    {
        state = std::make_shared<CSMState>();
        slot = state;
    }
    return state;
}

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

std::string CSMMStream::entryDisplayName(const CSMEntry &e)
{
    if ( !e.name.empty() )
        return e.name;

    // Unnamed entries take the media file's name (duplicates resolve
    // positionally: loads search forward from the current tape position)
    return default_name;
}

bool CSMMStream::readHeader()
{
    // `walked` lives in the shared state and is set even when the walk yields
    // nothing, so a broken image is not re-walked on every listing - and a
    // freshly constructed stream over an already-walked container does no I/O
    // at all.
    if ( walked )
        return !entries.empty();

    walked = true;
    entries.clear();

    if ( containerStream == nullptr )
        return false;

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

        CSMEntry e;
        e.file_type     = hdr[0];
        e.start_address = (uint16_t)hdr[1] | ((uint16_t)hdr[2] << 8);
        e.end_address   = (uint16_t)hdr[3] | ((uint16_t)hdr[4] << 8);

        // The data length is derived from the addresses, so a header claiming
        // an end below its start describes nothing we can read.
        if ( e.end_address < e.start_address )
            break;

        // The name is a fixed-width field padded with spaces, and sometimes
        // with $A0. rtrimPad() strips both - a CBM tape header is the case its
        // own comment cites. A name that is all padding trims to empty, and
        // such an entry is listed under the media file's name.
        e.name.assign((const char *)hdr + 5, 16);
        mstr::rtrimPad(e.name);
        // The header field is PETSCII. Converting HERE rather than at the two
        // consumers makes both the listing and the name lookup UTF-8 at once -
        // they compared raw PETSCII against a UTF-8 path before, and only
        // agreed because compareFilename() is case-insensitive.
        e.name = mstr::toUTF8(e.name);

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

void CSMMStream::resetTape()
{
    tape_index = 0;
    tape_ended = false;
    have_current = false;
}

bool CSMMStream::nextTapeEntry()
{
    if ( !readHeader() )
    {
        tape_ended = true;
        have_current = false;
        return false;
    }

    if ( tape_index >= entries.size() )
    {
        tape_ended = true;
        have_current = false;
        return false;
    }

    current = entries[tape_index++];
    have_current = true;
    return true;
}

void CSMMStream::serveCurrent()
{
    // MMediaStream::read() only routes through readFile() once a file has been
    // selected; without this it would hand back raw container bytes.
    seekCalled = true;

    // data_length is the clamped one, so a truncated tape serves the size it
    // can actually deliver.
    _size = current.data_length + 2;    // 2 bytes for load address

    // CSM data blocks hold raw program bytes - the load address lives in the
    // header block, so the two bytes a PRG starts with are synthesized here.
    _load_address[0] = current.start_address & 0xFF;
    _load_address[1] = ( current.start_address >> 8 ) & 0xFF;

    _position = 0;
    if ( containerStream != nullptr )
        containerStream->seek(current.data_offset);

    // Serving an entry moves the head past it, so the "already ready" shortcut
    // in seekPath() must not fire for it again. Without this, loading the same
    // name twice re-serves the same entry - and a tape whose loader and payload
    // share a name (Abductor.csm, and the norm for multi-part tapes) could
    // never reach the payload. `current` itself stays valid: readFile() works
    // from _load_address and the container position, not from it.
    have_current = false;
}

uint32_t CSMMStream::readFile(uint8_t* buf, uint32_t size)
{
    uint32_t bytesRead = 0;

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

std::string CSMMStream::seekNextEntry()
{
    if ( mode == std::ios_base::out )
    {
        Debug_printv("Writing to tape images is not supported");
        return "";
    }

    if ( !readHeader() || entries.empty() )
        return "";

    // A full lap and the caller still has not stopped: report the end of the
    // media so its search terminates. One step per entry covers the whole tape
    // from wherever the head happened to be, so nothing is skipped and nothing
    // is offered twice.
    if ( scan_steps >= entries.size() )
        return "";

    if ( have_current )
    {
        // The entry the last directory request left under the head. Serve it
        // before moving on, so a LOAD"*" straight after a listing gets what was
        // just listed rather than the one after it. serveCurrent() consumes the
        // flag, so the next step advances.
        scan_steps++;
        serveCurrent();
        return entryDisplayName(current);
    }

    // The end of the tape is not the end of the scan - the head runs back to
    // the start rather than stopping there.
    if ( tape_index >= entries.size() )
        resetTape();

    current = entries[tape_index++];
    scan_steps++;
    serveCurrent();

    Debug_printv("Tape entry [%s] size[%lu]", entryDisplayName(current).c_str(), _size);

    return entryDisplayName(current);
};

/********************************************************
 * File implementations
 ********************************************************/

std::shared_ptr<MStream> CSMMFile::getDecodedStream(std::shared_ptr<MStream> is)
{
    auto stream = std::make_shared<CSMMStream>(is);
    stream->setDefaultName(name);
    return stream;
}

bool CSMMFile::rewindDirectory()
{
    dirIsOpen = true;
    entry_index = 0;

    auto image = ImageBroker::obtain<CSMMStream>("csm", url);
    if (image == nullptr)
        return false;

    image->setDefaultName(name);

    // Set Media Info Fields. The tape position is NOT reset - listings advance
    // through the tape sequentially. A CSM carries no tape title, so the
    // image's own name stands in for one.
    media_header = name;
    auto dot = media_header.find_last_of('.');
    if ( dot != std::string::npos )
        media_header = media_header.substr(0, dot);
    // Lowercase, not upper: this is UTF-8 until the IEC boundary converts it,
    // and lowercase UTF-8 is what maps to the PETSCII the C64 draws as caps.
    mstr::toLower(media_header);
    if ( media_header.size() > 16 )
        media_header = media_header.substr(0, 16);

    media_id = " csm ";
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
    if (!dirIsOpen)
        rewindDirectory();

    auto image = ImageBroker::obtain<CSMMStream>("csm", url);
    if (image == nullptr)
    {
        dirIsOpen = false;
        return nullptr;
    }

    // Sequential tape: each directory request returns ONE entry - the next
    // program on the tape - and leaves it ready to load
    if (entry_index > 0)
    {
        dirIsOpen = false;
        return nullptr;
    }
    entry_index++;

    // The previous request reached the end of the tape: start over
    if (image->tapeEnded())
        image->resetTape();

    if (image->nextTapeEntry())
    {
        std::string filename = image->entryDisplayName(image->current);
        mstr::replaceAll(filename, "/", "\\");

        auto file = MFSOwner::File(url + "/" + filename);
        file->name = filename;
        file->extension = image->decodeType(image->current.file_type);
        file->size = image->current.data_length + 2; // 2 bytes for load address
        file->is_dir = 0;

        Debug_printv("Tape entry: name[%s] addr[%04X-%04X] size[%lu]",
                     filename.c_str(), image->current.start_address,
                     image->current.end_address, file->size);
        return file;
    }

    // End of the tape reached
    std::string marker = "end of tape";  // lowercase: the IEC boundary converts, and lowercase UTF-8 is what the C64 draws as capitals
    auto file = MFSOwner::File(url + "/" + marker);
    file->name = marker;
    file->extension = " NFO";
    file->size = 0;
    file->is_dir = 0;
    return file;
}

bool CSMMFile::isDirectory()
{
    if (is_dir != -1)
        return is_dir == 1;

    // The image itself is a directory; entries inside it are files
    return pathInStream.empty() || pathInStream == "/";
}

bool CSMMFile::exists()
{
    auto stream = ImageBroker::obtain<CSMMStream>("csm", url);
    if (stream == nullptr)
        return false;

    // Sequential tapes resolve names when loading (seekPath scans forward and
    // wraps), so an entry name cannot be answered for without moving the tape.
    return true;
}
