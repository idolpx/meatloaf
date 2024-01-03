
#include "archive_ml.h"

#include <archive.h>
#include <archive_entry.h>

#include "meat_io.h"

/* Returns pointer and size of next block of data from archive. */
// The read callback returns the number of bytes read, zero for end-of-file, or a negative failure code as above.
// It also returns a pointer to the block of data read.
// https://github.com/libarchive/libarchive/wiki/LibarchiveIO
ssize_t myRead(struct archive *a, void *userData, const void **buff)
{
    ArchiveStreamData *streamData = (ArchiveStreamData *)userData;
    // 1. we have to call srcStr.read(...)
    ssize_t bc = streamData->srcStream->read(streamData->srcBuffer, ArchiveStream::buffSize);
    Debug_printv("Past read");
    // 2. set *buff to the bufer read in 1.
    *buff = streamData->srcBuffer;
    Debug_printv("Past setting buffer");
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
    MStream *src_str = (MStream *)userData;
    if (src_str->isOpen())
    {
        bool rc = src_str->seek(request, SEEK_CUR);
        return (rc) ? request : ARCHIVE_WARN;
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
        // callbacks set here:
        //                                         open, read  , skip,   close
        int r = archive_read_open2(a, &streamData, NULL, myRead, myskip, myclose);
        if (r == ARCHIVE_OK)
            is_open = true;
        Debug_printv("open called, result=%d! (OK should be 0!)", r);
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
    Debug_printv("Read called");
    // ok so here we will basically need to refill buff with consecutive
    // calls to srcStream.read, I assume buf is filled by myread callback
    size_t r = archive_read_data(a, buf, size); // calls myread?
    _position += r;
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
    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        std::string entryName = archive_entry_pathname(entry);
        if (mstr::compare(path, entryName))
            return true;
    }
    return false;
};

// For files with no directory structure
// tap, crt, tar
std::string ArchiveStream::seekNextEntry()
{
    struct archive_entry *entry;
    if (archive_read_next_header(a, &entry) == ARCHIVE_OK)
        return archive_entry_pathname(entry);
    else
        return "";
};

uint32_t ArchiveStream::position()
{
    return _position;
}

// STREAM HAS NO IDEA HOW MUCH IS AVALABLE - IT'S ENDLESS!
uint32_t ArchiveStream::available()
{
    return 0; // whatever...
}

// STREAM HAS NO IDEA ABOUT SIZE - IT'S ENDLESS!
uint32_t ArchiveStream::size()
{
    return -1;
}

// WHAT DOES THIS FUNCTION DO???
size_t ArchiveStream::error()
{
    return -1;
}

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
    return new ArchiveStream(containerIstream);
}

// archive file is always a directory
bool ArchiveContainerFile::isDirectory()
{
    return true;
};

bool ArchiveContainerFile::rewindDirectory()
{
    if (a != nullptr)
        archive_read_free(a);

    return prepareDirListing();
}

MFile *ArchiveContainerFile::getNextFileInDir()
{
    struct archive_entry *entry;

    if (a == nullptr)
    {
        prepareDirListing();
    }

    if (a != nullptr)
    {
        if (archive_read_next_header(a, &entry) == ARCHIVE_OK)
        {
            auto newFile = MFSOwner::File(archive_entry_pathname(entry));
            // TODO - we can probably fill newFile with some info that is
            // probably available in archive_entry structure!
            //newFile->size(archive_entry_size(entry)); // etc.

            return newFile;
        }
        else
        {
            archive_read_free(a);
            a = nullptr;
            return nullptr;
        }
    }
    else
    {
        return nullptr;
    }
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

    int r = archive_read_open2(a, &(as->streamData), NULL, myRead, myskip, myclose);
    if (r == ARCHIVE_OK)
    {
        Debug_printv("Archive ok");
        return true;
    }
    else
    {
        Debug_printv("Archive nok, error=%d",r);
        archive_read_free(a);
        a = nullptr;
        return false;
    }
}
