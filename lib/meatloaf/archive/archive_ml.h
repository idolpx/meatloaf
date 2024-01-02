// .7Z, .ARC, .ARK, .BZ2, .GZ, .LHA, .LZH, .LZX, .RAR, .TAR, .TGZ, .XAR, .ZIP - libArchive for Meatloaf!
//
// https://stackoverflow.com/questions/22543179/how-to-use-libarchive-properly
// https://libarchive.org/

#ifndef MEATLOAF_ARCHIVE
#define MEATLOAF_ARCHIVE

#include <archive.h>
#include <archive_entry.h>

#include "meat_io.h"

// TODO: check how we can use archive_seek_callback, archive_passphrase_callback etc. to our benefit!

/* Returns pointer and size of next block of data from archive. */
// The read callback returns the number of bytes read, zero for end-of-file, or a negative failure code as above.
// It also returns a pointer to the block of data read.
ssize_t myRead(struct archive *a, void *__src_stream, const void **buff);

int myclose(struct archive *a, void *__src_stream);

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
int64_t myskip(struct archive *a, void *__src_stream, int64_t request);

/********************************************************
 * Streams implementations
 ********************************************************/

class ArchiveStreamData {
public:
    uint8_t *srcBuffer = nullptr;
    std::shared_ptr<MStream> srcStream = nullptr; // a stream that is able to serve bytes of this archive
};

class ArchiveStream : public MStream
{
    struct archive *a;
    bool is_open = false;
    uint32_t _position = 0;
    ArchiveStreamData streamData;

public:
    static const size_t buffSize = 4096;
    
    ArchiveStream(std::shared_ptr<MStream> srcStr);
    ~ArchiveStream();

    bool open() override;
    void close() override;
    bool isOpen() override;

    uint32_t read(uint8_t *buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    // For files with a browsable random access directory structure
    // d64, d74, d81, dnp, etc.
    bool seekPath(std::string path) override;

    // For files with no directory structure
    // tap, crt, tar
    std::string seekNextEntry() override;

    uint32_t position() override;

    // STREAM HAS NO IDEA HOW MUCH IS AVALABLE - IT'S ENDLESS!
    uint32_t available() override;

    // STREAM HAS NO IDEA ABOUT SIZE - IT'S ENDLESS!
    virtual uint32_t size() override;

    // WHAT DOES THIS FUNCTION DO???
    virtual size_t error() override;

    virtual bool seek(uint32_t pos) override;

protected:


private:
};

/********************************************************
 * Files implementations
 ********************************************************/

class ArchiveContainerFile : public MFile
{
    struct archive *a = nullptr;
    std::shared_ptr<MStream> dirStream = nullptr; // a stream that is able to serve bytes of this archive

public:
    ArchiveContainerFile(std::string path) : MFile(path){};

    ~ArchiveContainerFile()
    {
        if (dirStream.get() != nullptr)
        {
            dirStream->close();
        }
    }

    MStream *createIStream(std::shared_ptr<MStream> containerIstream);

    // archive file is always a directory
    bool isDirectory();
    bool rewindDirectory() override;
    MFile *getNextFileInDir() override;

    bool mkDir() override { return false; }
    bool exists() override { return true; }
    uint32_t size() override { return 0; }
    bool remove() override { return true; }
    bool rename(std::string dest) { return true; }
    time_t getLastWrite() override { return 0; }
    time_t getCreationTime() override { return 0; }

private:
    bool prepareDirListing();
};

/********************************************************
 * FS implementations
 ********************************************************/

class ArchiveContainerFileSystem : public MFileSystem
{
    MFile *getFile(std::string path)
    {
        return new ArchiveContainerFile(path);
    };

public:
    ArchiveContainerFileSystem() : MFileSystem("arch") {}

    bool handles(std::string fileName)
    {

        return byExtension(
            {".7z",
             ".arc",
             ".ark",
             ".bz2",
             ".gz",
             ".lha",
             ".lzh",
             ".lzx",
             ".rar",
             ".tar",
             ".tgz",
             ".xar",
             ".zip"},
            fileName);
    }

private:
};

#endif // MEATLOAF_ARCHIVE