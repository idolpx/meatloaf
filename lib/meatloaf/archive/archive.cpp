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

#include "archive.h"

#include <string.h>
#include <archive.h>
#include <archive_entry.h>

#include "../meatloaf.h"

/* Returns pointer and size of next block of data from archive. */
// The read callback returns the number of bytes read, zero for end-of-file, or a negative failure code as above.
// It also returns a pointer to the block of data read.
// https://github.com/libarchive/libarchive/wiki/LibarchiveIO
//
// This callback is just a way to get bytes from srcStream into libarchive for processing
ssize_t cb_read(struct archive *a, void *userData, const void **buff)
{
    ArchiveMStreamData *streamData = (ArchiveMStreamData *)userData;
    // 1. we have to call srcStr.read(...)
    ssize_t bc = streamData->srcStream->read(streamData->srcBuffer, ArchiveMStream::buffSize);
    //std::string dump((char*)streamData->srcBuffer, bc);
    //Debug_printv("libarchive pulling data from src MStream, got bytes:%d", bc);
    //Debug_printv("Dumping bytes: %s", dump.c_str());
    // 2. set *buff to the bufer read in 1.
    *buff = streamData->srcBuffer;
    // 3. return read bytes count
    return bc;
}

/*
It must return the number of bytes actually skipped, or a negative failure code if skipping cannot be done.
It can skip fewer bytes than requested but must never skip more.
Only positive/forward skips will ever be requested.
If skipping is not provided or fails, libarchive will call the read() function and simply ignore any data that it does not need.

* Skips at most request bytes from archive and returns the skipped amount.
* This may skip fewer bytes than requested; it may even skip zero bytes.
* If you do skip fewer bytes than requested, libarchive will invoke your
* read callback and discard data as necessary to make up the full skip.
*/
// https://github.com/libarchive/libarchive/wiki/LibarchiveIO
int64_t cb_skip(struct archive *a, void *userData, int64_t request)
{
    //Debug_printv("bytes[%lld]", request);
    ArchiveMStreamData *streamData = (ArchiveMStreamData *)userData;

    if (streamData->srcStream->isOpen())
    {
        bool rc = streamData->srcStream->seek(request, SEEK_CUR);
        return (rc) ? request : ARCHIVE_WARN;
    }
    else
    {
        Debug_printv("ERROR! skip failed");
        return ARCHIVE_FATAL;
    }
}

int64_t cb_seek(struct archive *a, void *userData, int64_t offset, int whence)
{
    //Debug_printv("offset[%lld] whence[%d] (0=begin, 1=curr, 2=end)", offset, whence);
    ArchiveMStreamData *streamData = (ArchiveMStreamData *)userData;

    if (streamData->srcStream->isOpen())
    {
        bool rc = streamData->srcStream->seek(offset, whence);
        return (rc) ? offset : ARCHIVE_WARN;
    }
    else
    {
        Debug_printv("ERROR! seek failed");
        return ARCHIVE_FATAL;
    }
}

int cb_close(struct archive *a, void *userData)
{
    //ArchiveMStreamData *src_str = (ArchiveMStreamData *)userData;
    
    //Debug_printv("Libarch wants to close, but we do nothing here...");

    // do we want to close srcStream here???
    return (ARCHIVE_OK);
}

int cb_open(struct a *arch, void *userData)
{
    // maybe we can use open for something? Check if stream is open?
    return (ARCHIVE_OK);
}


/********************************************************
 * Streams implementations
 ********************************************************/

bool ArchiveMStream::open(std::ios_base::openmode mode)
{
    if (!is_open)
    {
        // TODO enable seek only if the stream is random access
        archive_read_set_read_callback(a, cb_read);
        archive_read_set_seek_callback(a, cb_seek);
        archive_read_set_skip_callback(a, cb_skip);
        archive_read_set_close_callback(a, cb_close);
        // archive_read_set_open_callback(mpa->arch, cb_open); - what does it do?
        archive_read_set_callback_data(a, &streamData);

        int r = archive_read_open1(a);
        //int r = archive_read_open2(a, &streamData, NULL, myRead, myskip, myclose);
        if ( r == ARCHIVE_OK )
        {
            is_open = true;
            Debug_printv("*** Archive Opened! (%d)", r);
        // std::string archiveFormat = archive_format_name(a);
        // std::string archiveCompression = archive_compression_name(a);
        // Debug_printv("archive format[%s] compression[%s]", archiveFormat.c_str(), archiveCompression.c_str());
        }
        else
        {
            Debug_printv("*** Error opening archive! (%d)", r);
        }
    }
    else
        Debug_printv("*** Archive already open!");

    return is_open;
};

void ArchiveMStream::close()
{
    if (is_open)
    {
        archive_read_close(a);
        archive_read_free(a);
        is_open = false;
    }
    //Debug_printv("Close called");
}

bool ArchiveMStream::isOpen()
{
    return is_open;
};

uint32_t ArchiveMStream::read(uint8_t *buf, uint32_t size)
{
    //Debug_printv("calling read, buff size=[%ld]", size);

    uint64_t zsize = archive_read_data(a, buf, size);

    //Debug_printv("archive returned [%llu] unarchived bytes", zsize);
    if ( zsize > 0 ) {
        _position += zsize;
        return zsize;
    }
    else
    {
        return 0;
    }
}

uint32_t ArchiveMStream::write(const uint8_t *buf, uint32_t size)
{
    return -1;
}


bool ArchiveMStream::seekEntry( std::string filename )
{
    //Debug_printv( "filename[%s] size[%d]", filename.c_str(), filename.size());

    // Read Directory Entries
    if ( filename.size() > 0 )
    {
        bool found = false;
        bool wildcard =  ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );
        while ( archive_read_next_header(a, &entry) == ARCHIVE_OK )
        {
            entry_index++;

            // Check filetype
            const mode_t type = archive_entry_filetype(entry);
            if ( S_ISREG(type) )
            {
                std::string entryFilename = basename(archive_entry_pathname(entry));

                Debug_printv("filename[%s] entry.filename[%.16s]", filename.c_str(), entryFilename.c_str());

                // Read Entry From Stream
                if ( filename == entryFilename ) // Match exact
                {
                    found = true;
                }
                else if ( wildcard ) // Wildcard Match
                {
                    if (filename == "*") // Match first entry
                    {
                        filename = entryFilename;
                        found = true;
                    }
                    else if ( mstr::compare(filename, entryFilename) ) // X?XX?X* Wildcard match
                    {
                        // Set filename to this filename
                        Debug_printv( "Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str() );
                        found = true;
                    }
                }

                if ( found )
                {
                    _size = archive_entry_size(entry);
                    
                    return true;
                }
            }
        }

        Debug_printv( "Not Found! file[%s]", filename.c_str() );
    }

    entry = nullptr;

    return false;
}


uint32_t ArchiveMStream::readFile(uint8_t *buf, uint32_t size)
{
    uint32_t bytesRead = 0;
    bytesRead += read(buf, size);

    return bytesRead;
}

bool ArchiveMStream::seekPath(std::string path)
{
    Debug_printv("seekPath called for path: %s", path.c_str());

    seekCalled = true;

    entry_index = 0;

    if ( seekEntry( path ) )
    {
        Debug_printv("entry[%s]", archive_entry_pathname(entry));
        return true;
    }

    return false;
}



bool ArchiveMStream::seek(uint32_t pos)
{
    return streamData.srcStream->seek(pos);
}

/********************************************************
 * Files implementations
 ********************************************************/

// archive file is always a directory
bool ArchiveMFile::isDirectory()
{
    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if ( pathInStream == "" )
        return true;
    else
        return false;
};

bool ArchiveMFile::rewindDirectory()
{
    if (dirStream.get() != nullptr)
    {
        dirStream->close();
    }

    Debug_printv("w prepare dir listing");

    dirIsOpen = false;
    dirStream = std::shared_ptr<MStream>(this->getSourceStream());

    if (dirStream == nullptr)
    {
        Debug_printv("dirStream is null");
        return false;
    }

    if(dirStream->isOpen())
    {
        dirIsOpen = true;
        return true;
    }

    Debug_printv("failed to open archive");
    return false;

}

MFile *ArchiveMFile::getNextFileInDir()
{
    if(!dirIsOpen)
        rewindDirectory();

    struct archive_entry *entry;

    std::string filename;
    do
    {
        if (archive_read_next_header(getArchive(), &entry) != ARCHIVE_OK)
            break;

        filename = basename(archive_entry_pathname(entry));
        Debug_printv("size[%d] empty[%d] pathInStream[%s] filename[%s]", filename.size(), filename.empty(), pathInStream.c_str(), filename.c_str());
    } while (filename.empty()); // Skip empty filenames

    Debug_printv("getNextFileInDir calling archive_read_next_header");
    if (filename.size() > 0)
    {
        auto file = MFSOwner::File(streamFile->url + "/" + filename);
        file->size = archive_entry_size(entry);
        file->_exists = true;
        return file;
    }
    else
    {
        Debug_printv( "END OF DIRECTORY");
        dirStream->close();
        dirIsOpen = false;
        return nullptr;
    }
}
