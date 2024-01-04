
#include "archive_ml.h"

#include <archive.h>
#include <archive_entry.h>

#include "meat_io.h"

/* Returns pointer and size of next block of data from archive. */
// The read callback returns the number of bytes read, zero for end-of-file, or a negative failure code as above.
// It also returns a pointer to the block of data read.
// https://github.com/libarchive/libarchive/wiki/LibarchiveIO
//
// This callback is just a way to get bytes from srcStream into libarchive for processing
ssize_t myRead(struct archive *a, void *userData, const void **buff)
{
    ArchiveStreamData *streamData = (ArchiveStreamData *)userData;
    // 1. we have to call srcStr.read(...)
    ssize_t bc = streamData->srcStream->read(streamData->srcBuffer, ArchiveStream::buffSize);
    //std::string dump((char*)streamData->srcBuffer, bc);
    Debug_printv("Past read from container stream - got bytes:%d", bc);
    //Debug_printv("Dumping bytes: %s", dump.c_str());
    // 2. set *buff to the bufer read in 1.
    *buff = streamData->srcBuffer;
    // 3. return read bytes count
    return bc;
}

int myclose(struct archive *a, void *userData)
{
    ArchiveStreamData *src_str = (ArchiveStreamData *)userData;

    // do we want to close srcStream here???
    return (ARCHIVE_OK);
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
int64_t myskip(struct archive *a, void *userData, int64_t request)
{
    Debug_printv("skip requested, skipping %d bytes", request);
    ArchiveStreamData *streamData = (ArchiveStreamData *)userData;

    if (streamData->srcStream->isOpen())
    {
        bool rc = streamData->srcStream->seek(request, SEEK_CUR);
        return (rc) ? request : ARCHIVE_WARN;
    }
    else
    {
        return ARCHIVE_FATAL;
    }
}

int64_t myseek(struct archive *a, void *userData, int64_t offset, int whence)
{
    Debug_printv("seek requested, offset=%d, whence=%d", offset, whence);
    ArchiveStreamData *streamData = (ArchiveStreamData *)userData;

    if (streamData->srcStream->isOpen())
    {
        bool rc = streamData->srcStream->seek(offset, whence);
        Debug_printv("seek success=%d", rc);
        return (rc) ? offset : ARCHIVE_WARN;
    }
    else
    {
        return ARCHIVE_FATAL;
    }
}

/********************************************************
 * Streams implementations
 ********************************************************/

ArchiveStream::ArchiveStream(std::shared_ptr<MStream> srcStr)
{
    // it should be possible to to pass a password parameter here and somehow
    // call archive_passphrase_callback(password) from here, right?
    streamData.srcStream = srcStr;
    a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    streamData.srcBuffer = new uint8_t[buffSize];
    Debug_printv("Stream constructor OK!");
}

ArchiveStream::~ArchiveStream()
{
    close();
    if (streamData.srcBuffer != nullptr)
        delete[] streamData.srcBuffer;
    Debug_printv("Stream destructor OK!");
}

bool ArchiveStream::open()
{
    if (!is_open)
    {

        // TODO enable seek only if the stream is random access
        archive_read_set_read_callback(a, myRead);
        archive_read_set_skip_callback(a, myskip);
        archive_read_set_seek_callback(a, myseek);
        archive_read_set_close_callback(a, myclose);
        archive_read_set_callback_data(a, &streamData);
        Debug_printv("Calling open1 on archive_read_new");
        int r =  archive_read_open1(a);
        Debug_printv("open called, result=%d! (OK should be 0!)", r);

        //int r = archive_read_open2(a, &streamData, NULL, myRead, myskip, myclose);
        if (r == ARCHIVE_OK)
            is_open = true;
    }
    return is_open;
};

void ArchiveStream::close()
{
    if (is_open)
    {
        archive_read_free(a);
        is_open = false;
    }
    Debug_printv("Close called");
}

bool ArchiveStream::isOpen()
{
    return is_open;
};

uint32_t ArchiveStream::read(uint8_t *buf, uint32_t size)
{
    Debug_printv("calling read, if not open, opening");
    if(!is_open)
        open();

    // ok so here we will basically need to refill buff with consecutive
    // calls to srcStream.read, I assume buf is filled by myread callback
    size_t r = archive_read_data(a, buf, size); // calls myread?
    Debug_printv("After calling archive_read_data, RC=%d", r);
    m_position += r;
    return r;
}

uint32_t ArchiveStream::write(const uint8_t *buf, uint32_t size)
{
    return -1;
}

// For files with a browsable random access directory structure
// d64, d74, d81, dnp, etc.
bool ArchiveStream::seekPath(std::string path)
{
    Debug_printv("seekPath called for path: %s", path.c_str());
    bool wildcard =  ( mstr::contains(path, "*") || mstr::contains(path, "?") );

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        std::string entryFilename = archive_entry_pathname(entry);
        m_length = archive_entry_size(entry);
        m_position = 0;
        //archive_entry_filetype(entry);

        Debug_printv("filename[%s] entry.filename[%.16s] size[%d] available[%d]", path.c_str(), entryFilename.c_str(), m_length, available());

        // Read Entry From Stream
        if (path == "*") // Match first PRG
        {
            path = entryFilename;
            return true;
        }
        else if ( path == entryFilename ) // Match exact
        {
            return true;
        }
        else if ( wildcard )
        {
            if ( mstr::compare(path, entryFilename) ) // X?XX?X* Wildcard match
            {
                // Move stream pointer to start track/sector
                return true;
            }
        }
    }
    return false;
};

// // For files with no directory structure
// // tap, crt, tar
// std::string ArchiveStream::seekNextEntry()
// {
//     struct archive_entry *entry;
//     if (archive_read_next_header(a, &entry) == ARCHIVE_OK)
//         return archive_entry_pathname(entry);
//     else
//         return "";
// };


bool ArchiveStream::seek(uint32_t pos)
{
    return streamData.srcStream->seek(pos);
}

/********************************************************
 * Files implementations
 ********************************************************/

MStream *ArchiveContainerFile::createIStream(std::shared_ptr<MStream> containerIstream)
{
    // TODO - we can get password from this URL and pass it as a parameter to this constructor
    Debug_printv("calling createIStream for ArchiveContainerFile");
    auto stream = new ArchiveStream(containerIstream);
    return stream;
}

// archive file is always a directory
bool ArchiveContainerFile::isDirectory()
{
    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if ( pathInStream == "" )
        return true;
    else
        return false;
};

bool ArchiveContainerFile::rewindDirectory()
{
    dirIsOpen = true;
    if (a != nullptr)
        archive_read_free(a);

    return prepareDirListing();
}

MFile *ArchiveContainerFile::getNextFileInDir()
{
    if(!dirIsOpen)
        rewindDirectory();

    struct archive_entry *entry;

    Debug_printv("getNextFileInDir calling archive_read_next_header");
    if (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        Debug_printv("getNextFileInDir found entry: %s", archive_entry_pathname(entry));
        auto parent = this->cd(archive_entry_pathname(entry));
        auto newFile = MFSOwner::File(parent);
        // TODO - we can probably fill newFile with some info that is
        // probably available in archive_entry structure!
        //newFile->size(archive_entry_size(entry)); // etc.

        return newFile;
    }
    else
    {
        Debug_printv("getNextFileInDir found no more entries, freeing archive");
        archive_read_free(a);
        a = nullptr;

        //Debug_printv( "END OF DIRECTORY");
        dirIsOpen = false;
        return nullptr;
    }
}

uint32_t ArchiveContainerFile::size()
{
    return 0;
}

bool ArchiveContainerFile::prepareDirListing()
{
    if (dirStream.get() != nullptr)
    {
        dirStream->close();
    }

    Debug_printv("w prepare dir listing\n");

    dirStream = std::shared_ptr<MStream>(this->meatStream());
    a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    ArchiveStream* as = (ArchiveStream*)dirStream.get();

    // TODO enable seek only if the stream is random access
    archive_read_set_read_callback(a, myRead);
    archive_read_set_skip_callback(a, myskip);
    archive_read_set_seek_callback(a, myseek);
    archive_read_set_close_callback(a, myclose);
    archive_read_set_callback_data(a, &(as->streamData));
    Debug_printv("Calling open1 on prepareDirListing");
    int r =  archive_read_open1(a);
    Debug_printv("open called, result=%d! (OK should be 0!)", r);

    if (r == ARCHIVE_OK)
    {
        Debug_printv("opening Archive for dir ok");
        return true;
    }
    else
    {
        Debug_printv("opening Archive for dir nok, error=%d",r);
        archive_read_free(a);
        a = nullptr;
        return false;
    }
}
