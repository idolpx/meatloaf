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

#include <stdio.h>
#include <archive.h>
#include <archive_entry.h>
#include <string.h>

#include "meatloaf.h"

// // HIMEM is only available on original ESP32 with SPIRAM (not S2, S3, C3, etc.)
// #if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
// // Include HIMEM allocator for LZMA decompression (.7z files)
// #include <esp32_himem_allocator.h>
// #define USE_ESP32_HIMEM 1
// #endif

// int cb_open(struct archive *, void *userData)
// {
//     Archive *a = (Archive *) userData;

//     // maybe we can use open for something? Check if stream is open?
//     a->m_srcStream->seek(0, SEEK_CUR); // move to beginning of stream

//     return (ARCHIVE_OK);
// }

// int cb_close(struct archive *, void *userData)
// {
//     //ArchiveMStreamData *src_str = (ArchiveMStreamData *)userData;
    
//     //Debug_printv("Libarch wants to close, but we do nothing here...");

//     // do we want to close srcStream here???
//     return (ARCHIVE_OK);
// }

ssize_t cb_read(struct archive *, void *userData, const void **buff) {
    // Returns pointer and size of next block of data from archive.
    // The read callback returns the number of bytes read, zero for end-of-file,
    // or a negative failure code as above. It also returns a pointer to the
    // block of data read.
    // https://github.com/libarchive/libarchive/wiki/LibarchiveIO
    Archive *a = (Archive *)userData;
    *buff = a->m_srcBuffer;
    if (a->m_archive == NULL) return 0;
    ssize_t n = (ssize_t)a->m_srcStream->read(a->m_srcBuffer, a->m_buffSize);
    return n;
}


int64_t cb_skip(struct archive *, void *userData, int64_t request)
{
    // It must return the number of bytes actually skipped, or a negative failure code if skipping cannot be done.
    // It can skip fewer bytes than requested but must never skip more.
    // Only positive/forward skips will ever be requested.
    // If skipping is not provided or fails, libarchive will call the read() function and simply ignore any data that it does not need.
    //
    // * Skips at most request bytes from archive and returns the skipped amount.
    // * This may skip fewer bytes than requested; it may even skip zero bytes.
    // * If you do skip fewer bytes than requested, libarchive will invoke your
    // * read callback and discard data as necessary to make up the full skip.
    //
    // https://github.com/libarchive/libarchive/wiki/LibarchiveIO
    Archive *a = (Archive *) userData;

    if (a->m_archive)
    {
        // When compression filters are active (gzip, bz2, xz, etc.), raw seeking
        // corrupts the decompressor state. Return 0 to force libarchive to use
        // read-based skipping through the decompression pipeline instead.
        if (a->hasCompressionFilter()) {
            return 0;
        }

        uint32_t old_pos = a->m_srcStream->position();
        bool rc = a->m_srcStream->seek(request, SEEK_CUR);
        if (rc) {
            // Return actual bytes skipped (may differ from request if seek is clamped)
            int64_t skipped = a->m_srcStream->position() - old_pos;
            // Debug_printv("skip request[%lld] old_pos[%u] new_pos[%u] skipped[%lld]",
            //              request, old_pos, a->m_srcStream->position(), skipped);
            return skipped;
        }
        Debug_printv("ERROR! skip failed: request[%lld]", request);
        return ARCHIVE_WARN;
    }
    else
    {
        Debug_printv("ERROR! skip failed - no archive");
        return ARCHIVE_FATAL;
    }
}


int64_t cb_seek(struct archive *, void *userData, int64_t offset, int whence)
{
    Archive *a = (Archive *) userData;

    if (a->m_archive)
    {
        // When compression filters are active, only allow rewinding to start.
        // Other seeks would corrupt the decompressor state.
        // This check is skipped during archive_read_open1() (before filters are detected)
        // so ZIP format detection (which needs SEEK_END) still works.
        if (a->hasCompressionFilter()) {
            if (whence == SEEK_SET && offset == 0) {
                bool rc = a->m_srcStream->seek(0, SEEK_SET);
                return rc ? 0 : ARCHIVE_FATAL;
            }
            return ARCHIVE_FATAL;
        }

        bool rc = a->m_srcStream->seek(offset, whence);
        if (rc) {
            // Must return the resulting absolute position, not the offset
            // This is critical for .7z files which require accurate positioning
            int64_t pos = a->m_srcStream->position();
            //Debug_printv("seek offset[%lld] whence[%d] -> pos[%lld]", offset, whence, pos);
            return pos;
        }
        Debug_printv("ERROR! seek failed: offset[%lld] whence[%d]", offset, whence);
        return ARCHIVE_WARN;
    }
    else
    {
        Debug_printv("ERROR! seek failed - no archive");
        return ARCHIVE_FATAL;
    }
}



bool Archive::open(std::ios_base::openmode mode, bool rawOnly) {
    // close the archive if it was already open
    close();

    Debug_printv("Archive::open [%s] rawOnly[%d]", m_srcStream->url.c_str(), rawOnly);

    m_srcBuffer = new uint8_t[m_buffSize];
    m_archive = archive_read_new();
    Debug_printv("pre-seek pos[%lu]", (unsigned long)m_srcStream->position());
    bool seekOk = m_srcStream->seek(0, SEEK_SET);
    Debug_printv("post-seek pos[%lu] seekOk[%d]", (unsigned long)m_srcStream->position(), (int)seekOk);

    if (rawOnly) {
        // Only add decompression filters + raw format — no competing archive formats.
        // This guarantees archive_read_next_header() returns ARCHIVE_OK (synthetic raw
        // entry) for single compressed files (.gz, .bz2, etc.) whose decompressed content
        // looks like an unknown format and causes ARCHIVE_EOF when all formats compete.
        archive_read_support_filter_all(m_archive);
        archive_read_support_format_raw(m_archive);
        m_hasCompressionFilter = true;
    } else {
        archive_read_support_filter_all(m_archive);
        archive_read_support_format_all(m_archive);
        archive_read_support_format_raw(m_archive);  // Support single compressed files like .gz
    }

    //archive_read_set_open_callback(m_archive, cb_open);
    //archive_read_set_close_callback(m_archive, cb_close);
    archive_read_set_read_callback(m_archive, cb_read);
    archive_read_set_skip_callback(m_archive, cb_skip);
    archive_read_set_seek_callback(m_archive, cb_seek);
    archive_read_set_callback_data(m_archive, this);

    Debug_printv("Calling archive_read_open1");
    int r = archive_read_open1(m_archive);
    if (r != ARCHIVE_OK) {
        Debug_printv("Error opening archive: %d! [%s]", r, archive_error_string(m_archive));
        archive_read_free(m_archive);
        m_archive = NULL;
    } else {
        Debug_printv("Archive opened successfully");
        if (!rawOnly) {
            const char* format_name = archive_format_name(m_archive);
            Debug_printv("Archive format: %s", format_name ? format_name : "(null)");
            int filter_count = archive_filter_count(m_archive);
            Debug_printv("Archive filter count: %d", filter_count);
            if (filter_count > 0) {
                const char* filter_name = archive_filter_name(m_archive, 0);
                Debug_printv("Archive filter 0: %s", filter_name ? filter_name : "(null)");
            }
            // filter_count > 1 means a compression filter is present (filter 0 is always "none")
            m_hasCompressionFilter = (filter_count > 1);
            Debug_printv("hasCompressionFilter: %d", m_hasCompressionFilter);
        }
    }

    return isOpen();
}

void Archive::close() {
    if (m_archive != NULL) {
        archive_read_close(m_archive);
        archive_read_free(m_archive);
        m_archive = NULL;
    }
    if (m_srcBuffer != nullptr) {
        delete[] m_srcBuffer;
        m_srcBuffer = nullptr;
    }
    m_hasCompressionFilter = false;
}


/********************************************************
 * ArchiveMStream implementation
 ********************************************************/

bool ArchiveMStream::open(std::ios_base::openmode mode) {
    m_mode = mode;
    if (!m_archive) return false;
    return m_archive->open(mode);
}

void ArchiveMStream::close() {
    if (m_archive) {
        m_archive->close();
    }

    if (m_session) {
        m_session->releaseIO();
        m_session.reset();
    }
    m_cachedEntry.reset();
}

bool ArchiveMStream::isOpen() {
    // Open if archive handle is active OR if we have cached data
    return (m_archive && m_archive->isOpen()) || (m_cachedEntry && m_cachedEntry->isAllocated());
}

bool ArchiveMStream::ensureData() {
    if (m_cachedEntry && m_cachedEntry->isAllocated()) {
        return true;
    }

    if (m_isCompressedOnly) {
        // For single compressed files (no multi-entry archive format), the standard open()
        // results in ARCHIVE_EOF on archive_read_next_header() because all format detectors
        // compete and none wins for the decompressed content.  Reopen with rawOnly=true so
        // the raw format is the sole competitor and always produces a synthetic header entry,
        // then advance past that header so archive_read_data() can stream the bytes.
        if (!m_archive) {
            Debug_printv("ERROR: archive object is null for %s", entry.filename.c_str());
            return false;
        }
        if (!m_archive->open(m_mode, true)) {
            Debug_printv("ERROR: failed to open raw-only archive for %s", entry.filename.c_str());
            return false;
        }
        struct archive_entry *ae = nullptr;
        if (archive_read_next_header(m_archive->getArchive(), &ae) != ARCHIVE_OK) {
            Debug_printv("ERROR: raw-only header read failed for %s", entry.filename.c_str());
            return false;
        }
        Debug_printv("raw-only header OK, proceeding with extraction of %s (%lu bytes)",
                     entry.filename.c_str(), (unsigned long)_size);
    } else if (!m_archive || !m_archive->isOpen()) {
        Debug_printv("ERROR: archive not open");
        return false;
    }

    // Find or create ArchiveMSession via SessionBroker
    std::string sessionKey = "archive:" + url;
    m_session = SessionBroker::find<ArchiveMSession>(sessionKey);
    if (!m_session) {
        m_session = std::make_shared<ArchiveMSession>(url);
        m_session->connect();
        SessionBroker::add(sessionKey, m_session);
    }
    m_session->acquireIO();

    // Get or extract the entry data
    m_cachedEntry = m_session->getEntry(entry.filename, m_archive->getArchive(), _size);
    if (m_cachedEntry) {
        // Data is now cached in ArchiveMSession — release the entire source chain
        // (libarchive buffers, source stream, SD file handle) to free memory
        delete m_archive;
        m_archive = nullptr;
        containerStream.reset();
    }
    return (m_cachedEntry != nullptr);
}

uint32_t ArchiveMStream::read(uint8_t *buf, uint32_t size) {
    if (!ensureData()) return 0;

    uint32_t numRead = m_cachedEntry->read(_position, buf, size);
    _position += numRead;
    return numRead;
}

uint32_t ArchiveMStream::write(const uint8_t *buf, uint32_t size) {
    if (!ensureData()) return 0;

    // NOTE: this function can NOT write past the end of the extracted file,
    //       i.e. it can NOT extend the size of a file, only modify existing
    //       data. Most disk images (D64, D81, G64 etc) have a fixed size.
    uint32_t numWritten = m_cachedEntry->write(_position, buf, size);
    if (numWritten > 0) m_cachedEntry->dirty = true;
    _position += numWritten;
    return numWritten;
}

bool ArchiveMStream::seek(uint32_t pos) {
    if (!ensureData()) return false;

    if (pos < _size) {
        _position = pos;
        return true;
    }
    return false;
}

bool ArchiveMStream::seekEntry(std::string filename)
{
    // Read Directory Entries
    if (filename.size())
    {
        m_archive->open( std::ios_base::in );

        size_t index = 1;
        //mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (seekEntry(index))
        {
            std::string entryFilename = entry.filename;
            //uint8_t i = entryFilename.find_first_of(0xA0);
            //entryFilename = entryFilename.substr(0, i);
            //mstr::rtrimA0(entryFilename);
            //entryFilename = mstr::toUTF8(entryFilename);

            Debug_printv("filename[%s] entry.filename[%s]", filename.c_str(), entryFilename.c_str());

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


bool ArchiveMStream::seekEntry( uint16_t index )
{
    Debug_printv("entry_count[%d] entry_index[%d] index[%d] m_isCompressedOnly[%d]", entry_count, entry_index, index, m_isCompressedOnly);

    if ( !m_archive->isOpen() ) {
        Debug_printv("ERROR: archive not open");
        return false;
    }

    // For compressed-only files, there's only one entry
    // If we're being asked for index > 1, return false immediately
    if (m_isCompressedOnly && index > 1) {
        Debug_printv("Compressed-only file has only one entry, index %d is out of bounds", index);
        return false;
    }

    index--;

    entry.filename.clear();
    entry.size = 0;

    archive *a = m_archive->getArchive();

    int r = archive_read_next_header(a, &a_entry);
    Debug_printv("archive_read_next_header returned: %d", r);

    // Special handling for compressed-only files (e.g., standalone .gz, .bz2 files)
    // These have compression filters but no archive format, so archive_read_next_header returns EOF
    if (r == ARCHIVE_EOF && index == 0 && archive_filter_count(a) > 1) {
        Debug_printv("Detected compressed-only file (no archive format)");

        // Mark this as a compressed-only file so we don't try to read more entries
        m_isCompressedOnly = true;

        // Derive filename from archive URL by removing compression extension
        std::string archivePath = url;

        // Extract filename from path
        size_t lastSlash = archivePath.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos)
            ? archivePath.substr(lastSlash + 1)
            : archivePath;

        // Remove compression extension
        const char* compressionExts[] = {".gz", ".bz2", ".xz", ".lz", ".z", ".zst", ".lz4"};
        for (const char* ext : compressionExts) {
            if (mstr::endsWith(filename, ext, false)) {
                filename = filename.substr(0, filename.length() - strlen(ext));
                break;
            }
        }

        entry.filename = filename;
        Debug_printv("Synthesized filename: %s", entry.filename.c_str());

        // Determine decompressed size via gzip ISIZE trailer (last 4 bytes of .gz file).
        // ISIZE = decompressed size mod 2^32 — exact for files < 4 GB.
        // We close the libarchive handle (which held gzip state) and seek containerStream
        // directly; ensureData() will reopen with rawOnly=true for actual extraction.
        entry.size = 0;
        if (mstr::endsWith(url, ".gz", false) && containerStream) {
            uint32_t srcSize = (uint32_t)containerStream->size();
            Debug_printv("gzip ISIZE check: srcSize=%lu", (unsigned long)srcSize);
            if (srcSize >= 18) {  // min valid gzip: 10 header + 2 deflate + 8 trailer
                m_archive->close();
                if (containerStream->seek(srcSize - 4, SEEK_SET)) {
                    uint8_t trailer[4] = {0};
                    if (containerStream->read(trailer, 4) == 4) {
                        uint32_t isizeVal = ((uint32_t)trailer[0])       |
                                            ((uint32_t)trailer[1] << 8)  |
                                            ((uint32_t)trailer[2] << 16) |
                                            ((uint32_t)trailer[3] << 24);
                        static const uint32_t MAX_SANE_ISIZE = 8u * 1024u * 1024u; // 8 MB
                        if (isizeVal > 0 && isizeVal <= MAX_SANE_ISIZE) {
                            entry.size = isizeVal;
                            Debug_printv("gzip ISIZE: %lu bytes", (unsigned long)entry.size);
                        } else {
                            Debug_printv("gzip ISIZE %lu out of range — ignoring", (unsigned long)isizeVal);
                        }
                    }
                }
                // Leave archive closed — ensureData() will call openRaw() for extraction
            }
        }
        if (entry.size == 0) {
            Debug_printv("Size unknown (non-gz or ISIZE read failed); will be determined on extraction");
        }

        entry_index = 1;
        return true;
    }

    if ( r != ARCHIVE_OK ) {
        if (r == ARCHIVE_EOF) {
            Debug_printv("End of archive reached");
        } else {
            // Suppress expected end-of-archive errors from compressed streams
            const char* err_str = archive_error_string(a);
            if (err_str && (strstr(err_str, "Truncated") ||
                           strstr(err_str, "decompression failed") ||
                           strstr(err_str, "bad header checksum"))) {
                Debug_printv("End of compressed archive");
            } else {
                Debug_printv("ERROR reading header: %s", err_str ? err_str : "(unknown)");
            }
        }
        return false;
    }

    // Check filetype
    const mode_t type = archive_entry_filetype(a_entry);
    Debug_printv("entry filetype: 0x%x, S_ISREG=%d", type, S_ISREG(type));
    if ( S_ISREG(type) ) {
        const char* pathname = archive_entry_pathname(a_entry);

        // For raw compressed files (.gz, .bz2, etc.), pathname may be NULL or empty
        // (libarchive raw format sets "data" but other formats may leave it null).
        // Derive filename from archive URL by removing compression extension.
        // Also track whether this is a raw/compressed-only entry so we can force
        // a byte-count scan below (archive_entry_size() returns the COMPRESSED file
        // size for these entries, not the decompressed size).
        bool isRawCompressedEntry = (pathname == nullptr || pathname[0] == '\0' ||
                                     strcmp(pathname, "data") == 0);
        if (isRawCompressedEntry) {
            std::string archivePath = url;

            // Extract filename from path
            size_t lastSlash = archivePath.find_last_of("/\\");
            std::string filename = (lastSlash != std::string::npos)
                ? archivePath.substr(lastSlash + 1)
                : archivePath;

            // Remove compression extension
            const char* compressionExts[] = {".gz", ".bz2", ".xz", ".lz", ".z", ".zst", ".lz4"};
            for (const char* ext : compressionExts) {
                if (mstr::endsWith(filename, ext, false)) {
                    filename = filename.substr(0, filename.length() - strlen(ext));
                    break;
                }
            }

            entry.filename = filename;
        } else {
            entry.filename = basename(pathname);
        }

        entry.size = archive_entry_size(a_entry);

        // For raw compressed entries (standalone .gz, .bz2, etc.) archive_entry_size()
        // returns the COMPRESSED file size, not the decompressed size.  Determine
        // the true decompressed size and reset the archive for data extraction.
        if (entry.size == 0 || isRawCompressedEntry) {
            bool sizeKnown = false;

            // For .gz files: read the ISIZE field from the gzip trailer (last 4 bytes).
            // ISIZE = decompressed size mod 2^32 — exact for files < 4 GB.
            // This avoids reading through all the compressed data (which exhauts the
            // source stream and can cause Z_DATA_ERROR on the subsequent reopen).
            if (mstr::endsWith(url, ".gz", false)) {
                uint32_t srcSize = (uint32_t)containerStream->size();
                Debug_printv("gzip ISIZE check: srcSize=%lu", (unsigned long)srcSize);
                if (srcSize >= 18) {  // min valid gzip: 10 header + 2 deflate + 8 trailer
                    m_archive->close();
                    if (containerStream->seek(srcSize - 4, SEEK_SET)) {
                        uint8_t trailer[4] = {0};
                        if (containerStream->read(trailer, 4) == 4) {
                            entry.size = ((uint32_t)trailer[0])       |
                                         ((uint32_t)trailer[1] << 8)  |
                                         ((uint32_t)trailer[2] << 16) |
                                         ((uint32_t)trailer[3] << 24);
                            // Sanity check: the ISIZE field in gzip is only reliable when
                            // the file was written by a conformant tool with a known size.
                            // Streaming compressors (pigz --synchronous, etc.) may write 0.
                            // More dangerously, files created with non-standard tools may
                            // write garbage. If the claimed size exceeds a sane maximum for
                            // Commodore disk images (largest real image: D90 ~8 MB), treat
                            // the field as untrustworthy and fall through to byte counting.
                            static const uint32_t MAX_SANE_ISIZE = 8u * 1024u * 1024u; // 8 MB
                            if (entry.size > MAX_SANE_ISIZE) {
                                Debug_printv("gzip ISIZE %lu exceeds 8 MB cap — field is corrupt or streaming; falling back to byte count", (unsigned long)entry.size);
                                entry.size = 0;
                                sizeKnown = false;
                            } else {
                                sizeKnown = (entry.size > 0);
                                Debug_printv("gzip ISIZE trailer: %lu bytes", (unsigned long)entry.size);
                            }
                        }
                    }
                    // Reopen archive; Archive::open() seeks containerStream back to 0
                    m_archive->open(std::ios_base::in);
                    a = m_archive->getArchive();
                    if (archive_read_next_header(a, &a_entry) != ARCHIVE_OK) {
                        entry.size = 0;
                        return false;
                    }
                    // Use the filename embedded in the gzip header (FNAME field) if present.
                    // gzip_read_header() runs during archive_read_next_header and overrides
                    // the raw format's "data" pathname with the stored name, if any.
                    const char* embeddedName = archive_entry_pathname(a_entry);
                    if (embeddedName && embeddedName[0] != '\0' &&
                        strcmp(embeddedName, "data") != 0) {
                        entry.filename = basename(embeddedName);
                        Debug_printv("gzip embedded filename: %s", entry.filename.c_str());
                    }
                }
            }

            if (!sizeKnown) {
                // Fallback: count actual decompressed bytes by reading through the data.
                // Used for non-gz compressed formats (.bz2, .xz, etc.) or when ISIZE read fails.
                uint8_t buff[256] = {0};
                ssize_t nread;
                uint64_t total = 0;

                do {
                    nread = archive_read_data(a, &buff, sizeof(buff) - 1);
                    if (nread > 0) total += (uint64_t)nread;
                } while (nread > 0);

                entry.size = (uint32_t)total;
                Debug_printv("Counted decompressed size: %lu bytes (loop exit nread=%d, archive_error='%s')",
                    entry.size, (int)nread,
                    archive_error_string(a) ? archive_error_string(a) : "none");

                // Reopen to reset read position for actual data extraction
                m_archive->close();
                m_archive->open(std::ios_base::in);
                a = m_archive->getArchive();
                if (archive_read_next_header(a, &a_entry) != ARCHIVE_OK) {
                    entry.size = 0;
                    return false;
                }
            }
        }
    }

    entry_index = index + 1;

    Debug_printv("entry_index[%d] filename[%s] size[%lu]", entry_index, entry.filename.c_str(), entry.size);
    return true;
}


uint32_t ArchiveMStream::readFile(uint8_t *buf, uint32_t size) 
{
    uint32_t bytesRead = 0;
    bytesRead += read(buf, size);

    Debug_printv("size[%lu] bytesRead[%lu] _position[%lu]", size, bytesRead, _position);
    return bytesRead;
}

bool ArchiveMStream::seekCachedFile(const std::string sessionKey, const std::string path) {
    // Check if this entry is already cached in ArchiveMSession — avoids
    // re-opening the archive (which can fail if DMA memory is exhausted)
    auto session = SessionBroker::find<ArchiveMSession>(sessionKey);
    if (session) {
        auto cached = session->getCachedFile(path);
        if (cached) {
            Debug_printv("Cache hit in seekPath for: %s (%u bytes)", path.c_str(), cached->size);
            entry.filename = path;
            entry.size = cached->size;
            _size = cached->size;
            _position = 0;
            m_session = session;
            m_cachedEntry = cached;
            m_session->acquireIO();
            return true;
        }
    }
    return false;
}

bool ArchiveMStream::seekPath(std::string path) {
    Debug_printv("seekPath called for path: %s", path.c_str());

    seekCalled = true;

    entry_index = 0;

    // Check if this entry is already cached in ArchiveMSession — avoids
    // re-opening the archive (which can fail if DMA memory is exhausted)
    std::string sessionKey = "archive:" + url;
    if (seekCachedFile(sessionKey, path)) {
        return true;
    }

    if (seekEntry(path)) {
        Debug_printv("entry[%s]", entry.filename.c_str());
        _size = entry.size;
        _position = 0;

        if (!ensureData()) return false;

        // Correct _size from the actual cached data if seekEntry() underestimated
        // (e.g. byte-count on a non-compressed source gave a wrong/truncated result)
        if (m_cachedEntry && m_cachedEntry->size > _size) {
            Debug_printv("correcting _size from %ld to %ld (cache)", _size, m_cachedEntry->size);
            _size = m_cachedEntry->size;
        }

        Debug_printv("File Size: size[%ld] available[%ld] position[%ld]", _size, available(), _position);
        return true;
    }

    Debug_printv("Not found! [%s]", path.c_str());
    return false;
}



/********************************************************
 * ArchiveMFile Implementation
 ********************************************************/

bool ArchiveMFile::rewindDirectory()
{
    // Single-file compressed archives (.d81.gz, .prg.gz, etc.) are transparent:
    // delegate to the inner file so the compression layer is invisible.
    if (isSingleFileCompression()) {
        auto inner = getInnerFile();
        if (inner) {
            Debug_printv("single-file compression: delegating rewindDirectory to [%s]", inner->url.c_str());
            dirIsOpen = true;
            bool result = inner->rewindDirectory();
            if ( result )
            {
                media_header = m_innerFile->media_header;
                media_id = m_innerFile->media_id;
                media_image = m_innerFile->media_image;
                media_partition = m_innerFile->media_partition;
                media_blocks_free = m_innerFile->media_blocks_free;
                media_block_size = m_innerFile->media_block_size;
            }
            return result;
        }
        return false;
    }

    Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto image = ImageBroker::obtain<ArchiveMStream>("archive", url);
    if (image == nullptr)
        return false;

    dirIsOpen = true;
    image->m_archive->open( std::ios_base::in );
    image->resetEntryCounter();

    media_archive = name;
    Debug_printv("Archive opened: [%s]", media_archive.c_str());

    return true;
}

MFile *ArchiveMFile::getNextFileInDir()
{
    //Debug_printv("getNextFileInDir() called, dirIsOpen=%d", dirIsOpen);

    // Delegate to inner file for single-file compressed archives
    if (isSingleFileCompression()) {
        auto inner = getInnerFile();
        if (inner) return inner->getNextFileInDir();
        dirIsOpen = false;
        return nullptr;
    }

    bool r = false;

    if (!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<ArchiveMStream>("archive", url);
    if (image == nullptr) {
        Debug_printv("ERROR: ImageBroker returned nullptr");
        goto exit;
    }

    Debug_printv("Calling getNextImageEntry()");
    do
    {
        r = image->getNextImageEntry();
        Debug_printv("getNextImageEntry() returned %d, filename=[%s]", r, r ? image->entry.filename.c_str() : "");
    } while (r && image->entry.filename.empty()); // Don't want empty entries

    if (r)
    {
        std::string filename = image->entry.filename;
        Debug_printv("Found entry: filename=[%s] size=%lu", filename.c_str(), image->entry.size);

        auto file = MFSOwner::File(sourceFile->url + "/" + filename);
        file->name = filename;  // Use actual entry name, not container image name
        file->size = image->entry.size;

        return file;
    }

exit:
    dirIsOpen = false;
    image->m_archive->close();
    return nullptr;
}
