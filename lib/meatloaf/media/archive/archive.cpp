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

#include <esp_heap_caps.h>

#include "meatloaf.h"

static inline void *psram_malloc(size_t sz) {
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}

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

    // Keep the first bytes libarchive is handed. When no format recognizes
    // the stream, these say WHY in one line: a container that opens fine
    // locally but not over the network is being served bytes that are not
    // its first bytes, and the signature here proves it rather than
    // suggesting it. Costs one memcpy per archive open.
    if (a->m_firstBytesLen == 0 && n > 0) {
        size_t keep = (n < (ssize_t)sizeof(a->m_firstBytes)) ? (size_t)n : sizeof(a->m_firstBytes);
        memcpy(a->m_firstBytes, a->m_srcBuffer, keep);
        a->m_firstBytesLen = keep;
    }
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

        // Skip forward by seeking to an ABSOLUTE target (position + request)
        // rather than a relative SEEK_CUR. Some source streams implement
        // SEEK_CUR relative to an internal buffer-fill position that differs
        // from the logical read position (fsplib's fsp_fseek does), which
        // lands the skip at the wrong place and corrupts the next header read.
        // Absolute positioning (SEEK_SET) is unambiguous and every source
        // supports it — the same reason cb_seek converts everything to it.
        uint32_t old_pos = a->m_srcStream->position();
        uint32_t target = old_pos + (uint32_t)request;   // libarchive only ever skips forward
        bool rc = a->m_srcStream->seek(target);          // single-arg seek == absolute SEEK_SET
        int64_t skipped = rc ? ((int64_t)a->m_srcStream->position() - old_pos) : 0;
        if (rc && skipped > 0) {
            // Return actual bytes skipped (may differ from request if seek is clamped)
            return skipped;
        }
        Debug_printv("ERROR! skip failed: request[%lld]", (long long)request);
        // Never return a negative code here: libarchive's client_skip_proxy()
        // does not check for negative returns — it subtracts them from the
        // remaining request, so the request GROWS by |code| each iteration and
        // it loops forever. Returning 0 makes libarchive fall back to
        // read-and-discard via cb_read.
        return 0;
    }
    else
    {
        Debug_printv("ERROR! skip failed - no archive");
        return 0;
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

        // Translate whence -> absolute position in int64, then seek the source
        // with an ABSOLUTE position. libarchive passes signed int64 offsets
        // (SEEK_END uses negative offsets like -22 to read the End Of Central
        // Directory record), which must not be truncated to uint32 or fed to a
        // source seek that computes _size-offset. Doing the translation here —
        // and only ever calling the source's absolute SEEK_SET path — means we
        // never depend on any source stream implementing SEEK_END/SEEK_CUR
        // (fsplib's fsp_fseek returns ENOTSUP for SEEK_END; HTTP's SEEK_END is
        // unreliable). Every source supports absolute positioning.
        int64_t total = (int64_t)a->m_srcStream->size();
        int64_t abs;
        switch (whence) {
            case SEEK_SET: abs = offset; break;
            case SEEK_CUR: abs = (int64_t)a->m_srcStream->position() + offset; break;
            case SEEK_END: abs = total + offset; break;
            default:       return ARCHIVE_FATAL;
        }
        if (abs < 0) abs = 0;

        // Seeking to (or past) EOF is never a real data position — there are no
        // bytes there, and a network range at EOF 416s. Handle it WITHOUT a
        // physical request:
        //   - randomAccess (directory listing): report the total size so
        //     libarchive's SEEK_END size-probe succeeds and the SEEKABLE reader
        //     (ZIP central directory) activates — fast, no per-entry data skip.
        //   - otherwise (streaming extraction): fail the probe so the streaming
        //     reader is used, and never issue the wasted 416.
        if (total > 0 && abs >= total) {
            return a->m_randomAccess ? total : ARCHIVE_WARN;
        }

        bool rc = a->m_srcStream->seek((uint32_t)abs);
        if (rc) {
            // Must return the resulting absolute position, not the offset
            // This is critical for .7z files which require accurate positioning
            return (int64_t)a->m_srcStream->position();
        }
        Debug_printv("ERROR! seek failed: offset[%lld] whence[%d] abs[%lld]", (long long)offset, whence, (long long)abs);
        return ARCHIVE_WARN;
    }
    else
    {
        Debug_printv("ERROR! seek failed - no archive");
        return ARCHIVE_FATAL;
    }
}



// The name to give the single entry of a compressed-only file (.gz, .bz2, ...),
// derived from the container's own path because the stream carries no directory.
//
// Percent-decoding is applied ONLY when the container came from a URL. A URL
// path component is encoded, and letting the encoding through names the
// extracted file literally `ordeal%2b2100p.d64`; a LOCAL path is not encoded,
// so a file genuinely containing '%' must keep it. `alter_pluses` is false
// because a '+' in a path is a literal plus, not the form-encoded space it
// means in a query string — the same call fnFsHTTP.cpp makes for the filenames
// in an HTTP directory listing.
std::string Archive::gzipNameFromHeader(const uint8_t *p, size_t n)
{
    // RFC 1952: ID1 ID2 CM FLG MTIME(4) XFL OS  = 10 bytes, then the optional
    // fields in FLG order. FNAME is a NUL-terminated string, and it is only
    // present when bit 3 is set.
    static const uint8_t FEXTRA = 0x04, FNAME = 0x08;

    if (p == nullptr || n < 10) return "";
    if (p[0] != 0x1f || p[1] != 0x8b || p[2] != 0x08) return "";   // not gzip/deflate

    const uint8_t flg = p[3];
    if ((flg & FNAME) == 0) return "";

    size_t at = 10;
    if (flg & FEXTRA) {
        if (at + 2 > n) return "";
        const size_t xlen = (size_t)p[at] | ((size_t)p[at + 1] << 8);
        at += 2 + xlen;
    }
    if (at >= n) return "";

    // The name must terminate within what we have; a partial one would be a
    // silently truncated filename, which is worse than falling back to the URL.
    const void *nul = memchr(p + at, '\0', n - at);
    if (nul == nullptr) return "";

    std::string name((const char *)(p + at), (const char *)nul - (const char *)(p + at));

    // Stored names are meant to be bare, but strip any directory part rather
    // than trust an archive to write outside where the caller intends.
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);

    return name;
}

static std::string compressedEntryNameFromUrl(const std::string &containerUrl)
{
    size_t lastSlash = containerUrl.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos)
        ? containerUrl.substr(lastSlash + 1)
        : containerUrl;

    static const char *compressionExts[] = {".gz", ".bz2", ".xz", ".lz", ".z", ".zst", ".lz4"};
    for (const char *ext : compressionExts) {
        if (mstr::endsWith(filename, ext, false)) {
            filename = filename.substr(0, filename.length() - strlen(ext));
            break;
        }
    }

    if (containerUrl.find("://") != std::string::npos)
        filename = mstr::urlDecode(filename, false);

    return filename;
}

bool Archive::open(std::ios_base::openmode mode, bool rawOnly, bool randomAccess) {
    // close the archive if it was already open
    close();

    Debug_printv("Archive::open [%s] rawOnly[%d]", m_srcStream->url.c_str(), rawOnly);

    m_srcBuffer = (uint8_t*)psram_malloc(m_buffSize);
    m_archive = archive_read_new();
    m_firstBytesLen = 0;
    Debug_printv("pre-seek pos[%lu]", (unsigned long)m_srcStream->position());
    bool seekOk = m_srcStream->seek(0, SEEK_SET);
    Debug_printv("post-seek pos[%lu] seekOk[%d]", (unsigned long)m_srcStream->position(), (int)seekOk);

    // randomAccess (directory listing of a central-directory format like ZIP):
    // let cb_seek satisfy libarchive's SEEK_END size-probe so the SEEKABLE
    // reader activates and reads the central directory via range jumps — no
    // per-entry data skip. Sequential (streaming) bulk reads use the opposite:
    // cb_seek fails the EOF probe → streaming reader, paired with an open-ended
    // source (set by extractAll after open).
    m_randomAccess = randomAccess;

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

        // Select the archive format by file extension instead of registering
        // every format and letting libarchive bid. Bidding reads a lot of the
        // file up front — the ISO9660 bidder alone probes at offset 32768 —
        // which is very slow over network sources that deliver small blocks
        // per request (HTTP hands back ~256 bytes at a time, so the ~48 KB of
        // bid probing was ~180 round-trips before the first entry). With one
        // format registered, only that format's (cheap) bidder runs. Unknown
        // or ambiguous extensions fall back to trying everything.
        std::string u = m_srcStream->url;
        mstr::toLower(u);
        if (mstr::endsWith(u, ".zip") || mstr::endsWith(u, ".jar") || mstr::endsWith(u, ".rp9")) {
            archive_read_support_format_zip(m_archive);
        } else if (mstr::endsWith(u, ".tar") || mstr::endsWith(u, ".tgz") ||
                   mstr::contains(u, (char *)".tar.")) {
            archive_read_support_format_tar(m_archive);
        } else if (mstr::endsWith(u, ".7z")) {
            archive_read_support_format_7zip(m_archive);
        } else if (mstr::endsWith(u, ".rar")) {
            archive_read_support_format_rar(m_archive);
            archive_read_support_format_rar5(m_archive);
        } else if (mstr::endsWith(u, ".lha") || mstr::endsWith(u, ".lzh") ||
                   mstr::endsWith(u, ".lzx")) {
            archive_read_support_format_lha(m_archive);
        } else if (mstr::endsWith(u, ".xar")) {
            archive_read_support_format_xar(m_archive);
        } else if (mstr::endsWith(u, ".iso")) {
            archive_read_support_format_iso9660(m_archive);
        } else if (mstr::endsWith(u, ".cpio") || mstr::endsWith(u, ".cpgz")) {
            archive_read_support_format_cpio(m_archive);
        } else {
            // Unknown/ambiguous extension — bid across the formats named
            // above, and fall back to raw for single compressed files like
            // .gz/.bz2, whose decompressed content is just bytes that no
            // archive format recognizes.
            //
            // This is deliberately NOT archive_read_support_format_all():
            // that reference is the only thing linking the cab, mtree, warc
            // and ar readers, ~31 KB of flash text for formats a Commodore
            // device does not meet. A plain ESP32's flash-text window
            // (iram0_2_seg, ~3.3 MB) is what this project links against, and
            // it had under 1.1 KB spare — adding the -lh1- decoder to the lha
            // reader overflowed it by 985 bytes. Every format Meatloaf can
            // name by extension is registered here, so the fallback still
            // recognizes any of them from content alone.
            archive_read_support_format_zip(m_archive);
            archive_read_support_format_tar(m_archive);
            archive_read_support_format_7zip(m_archive);
            archive_read_support_format_rar(m_archive);
            archive_read_support_format_rar5(m_archive);
            archive_read_support_format_lha(m_archive);
            archive_read_support_format_xar(m_archive);
            archive_read_support_format_iso9660(m_archive);
            archive_read_support_format_cpio(m_archive);
            // Keep `empty` as well: it is what gives a zero-byte file a clean
            // empty listing. Without it that file falls to raw, which bids 1
            // on anything and synthesizes one entry named "data" — the exact
            // failure the note below is about.
            archive_read_support_format_empty(m_archive);
            archive_read_support_format_raw(m_archive);
        }

        // NOTE: raw is deliberately NOT registered above when the extension
        // names a real container. raw bids 1 on ANY byte stream and
        // synthesizes a single entry called "data" spanning the whole input,
        // so registering it alongside zip/tar/7z/... turns "this isn't the
        // format it claims to be" into a silent success: the caller extracts
        // one bogus file that is a byte-for-byte copy of the container. That
        // is what `unzipx <a .zip whose source stream was misaligned>`
        // produced — "extracted 1 entries, 303509 bytes" of nothing but the
        // zip itself. With raw absent the open fails, which callers report.
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
        // No format bid on this stream. Report what it was actually handed —
        // a container whose first bytes are not its own signature is being
        // read from the wrong offset, which is a source-stream fault, not an
        // archive one, and is otherwise invisible from the outside.
        // Only the leading bytes are diagnostic — m_firstBytes is sized for a
        // gzip FNAME, not for printing.
        const size_t kShow = 16;
        char hex[3 * kShow + 1] = {0};
        for (size_t i = 0; i < m_firstBytesLen && i < kShow; i++)
            snprintf(hex + (i * 3), 4, "%02X ", m_firstBytes[i]);
        Debug_printv("first bytes[%s] srcSize[%lu] srcPos[%lu]", hex,
                     (unsigned long)m_srcStream->size(),
                     (unsigned long)m_srcStream->position());
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
        free(m_srcBuffer);
        m_srcBuffer = nullptr;
    }
    m_hasCompressionFilter = false;
    // Clear the bulk-read hint so a later non-archive read of the same shared
    // (pooled) source stream reverts to normal range-based paging.
    if (m_srcStream) m_srcStream->setSequentialAccess(false);
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
        // randomAccess=true: single-entry lookup needs the seekable reader.
        // 7z is seekable-ONLY (no streaming reader — it must seek to the footer
        // to read entries), so without this archive_read_next_header() returns
        // ARCHIVE_FATAL. Harmless for streaming formats (zip/tar) and for
        // compressed-only files (.xz/.bz2/.lz4 register no seekable format, so
        // they stay streaming regardless).
        m_archive->open( std::ios_base::in, false, true );

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
    //Debug_printv("entry_count[%d] entry_index[%d] index[%d] m_isCompressedOnly[%d]", entry_count, entry_index, index, m_isCompressedOnly);

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
    //Debug_printv("archive_read_next_header returned: %d", r);

    // Special handling for compressed-only files (e.g., standalone .gz, .bz2 files)
    // These have compression filters but no archive format, so archive_read_next_header returns EOF
    if (r == ARCHIVE_EOF && index == 0 && archive_filter_count(a) > 1) {
        Debug_printv("Detected compressed-only file (no archive format)");

        // Mark this as a compressed-only file so we don't try to read more entries
        m_isCompressedOnly = true;

        // Prefer the name the gzip stream stores in its own header: it is the
        // ORIGINAL filename, which the URL cannot reproduce
        // (`ordeal%2b2100p.d64.gz` on the server is
        // `ordeal +2 100% (ntsc pal) wanderer.d64` inside). libarchive exposes
        // FNAME as the entry pathname, but only when a format reader yields an
        // entry — and getting here means it returned ARCHIVE_EOF instead, so it
        // is read from the header bytes cb_read already captured. Falls back to
        // the archive's own path (percent-decoded only when that path is a URL
        // — see compressedEntryNameFromUrl).
        entry.filename = Archive::gzipNameFromHeader(m_archive->firstBytes(),
                                                     m_archive->firstBytesLen());
        if (entry.filename.empty())
            entry.filename = compressedEntryNameFromUrl(url);
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
            // Non-gz single-file compressions (.xz/.bz2/.lz4) carry no stored
            // size (bz2 has none at all). Leave size 0 — ensureData() extracts
            // in a single pass into a growing HIMEM/PSRAM CachedFile, which
            // discovers the size as it decompresses.
            Debug_printv("Size unknown (non-gz); determined during single-pass extraction");
        }

        entry_index = 1;
        return true;
    }

    if ( r != ARCHIVE_OK ) {
        if (r == ARCHIVE_EOF) {
            //Debug_printv("End of archive reached");
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
    //Debug_printv("entry filetype: 0x%x, S_ISREG=%d", type, S_ISREG(type));
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
            entry.filename = compressedEntryNameFromUrl(url);
        } else {
            entry.filename = basename(pathname);
        }

        entry.size = archive_entry_size(a_entry);

        // For raw compressed entries (standalone .gz, .bz2, etc.) archive_entry_size()
        // returns the COMPRESSED file size, not the decompressed size.  Determine
        // the true decompressed size and reset the archive for data extraction.
        //
        // Only ask when the archive is decoding a compressed STREAM, where that
        // size field is meaningless and there is exactly one entry. Inside a real
        // container a size of 0 means the file IS empty, and probing it there is
        // actively harmful: the probe below re-opens the archive and re-reads
        // from the FIRST entry, which resets a name walk already in progress.
        // Found on hardware — entry 66 of the 997 in mce.lha is an empty file,
        // and `hex mce.lha/MCE.info` re-opened the archive about once a second
        // forever, never reaching a name that sits past it.
        if (isRawCompressedEntry ||
            (entry.size == 0 && m_archive->hasCompressionFilter())) {
            bool sizeKnown = false;

            // Whether the archive is decoding a COMPRESSED stream right now,
            // captured before the probes below re-open it. That is the state
            // any later byte count has to still be in to mean anything, and
            // it is not the same question as isRawCompressedEntry: a gzip
            // stream carrying an FNAME header (Elite.d64.gz does) yields a
            // real pathname, so isRawCompressedEntry is false and only the
            // `entry.size == 0` arm brought us here.
            const bool hadCompressionFilter = m_archive->hasCompressionFilter();

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
                // The count below measures whatever the archive currently decodes.
                // If the re-open above came back WITHOUT the compression filter,
                // it measures the COMPRESSED stream and reports that as the
                // decompressed size — and the caller then extracts exactly that
                // many bytes, i.e. a silently truncated file. Seen on hardware
                // for a .gz over HTTPS: `srcSize=41448` (compressed) followed by
                // `Counted decompressed size: 41448` for a 174848-byte D64,
                // because the re-open reported `filter count: 1 / none`. The
                // cause is upstream — the source served something other than the
                // file's start after a seek, which a network source does when a
                // re-request desynchronises — but counting the wrong stream must
                // not be how it surfaces.
                //
                // A further re-open is worth one try: on the hardware trace the
                // NEXT open of the same source did come back with the filter.
                if (hadCompressionFilter && !m_archive->hasCompressionFilter()) {
                    Debug_printv("compression filter LOST after reopen — retrying once");
                    m_archive->close();
                    m_archive->open(std::ios_base::in);
                    a = m_archive->getArchive();
                    if (archive_read_next_header(a, &a_entry) != ARCHIVE_OK ||
                        !m_archive->hasCompressionFilter()) {
                        Debug_printv("ERROR! source is not serving [%s] as a compressed stream; "
                                     "refusing to report the compressed length as the entry size",
                                     url.c_str());
                        entry.size = 0;
                        return false;
                    }
                }

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

    //Debug_printv("entry_index[%d] filename[%s] size[%lu]", entry_index, entry.filename.c_str(), entry.size);
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
    // re-opening the archive (which can fail if DMA memory is exhausted).
    // Uses wildcard-aware lookup so "seekPath("*")" hits a cached "Elite.d64" entry.
    auto session = SessionBroker::find<ArchiveMSession>(sessionKey);
    if (session) {
        auto [key, cached] = session->findEntry(path);
        if (cached) {
            Debug_printv("Cache hit in seekPath for: path[%s] key[%s] (%u bytes)", path.c_str(), key.c_str(), cached->size);
            entry.filename = key;
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

bool ArchiveMStream::nextEntrySimple() {
    if (!m_archive || !m_archive->isOpen()) return false;

    struct archive *a = m_archive->getArchive();
    while (true) {
        int r = archive_read_next_header(a, &a_entry);
        if (r != ARCHIVE_OK) {
            entry.filename.clear();
            entry.size = 0;
            return false;  // EOF or error ends the walk
        }
        if (!S_ISREG(archive_entry_filetype(a_entry))) continue;  // skip dirs

        const char *pn = archive_entry_pathname(a_entry);
        // Keep BOTH: the basename for flat listings, and the stored path so
        // extraction can recreate the directory structure. basename() is
        // destructive on some platforms, so read pathname first.
        entry.pathname = (pn && pn[0]) ? pn : "";
        entry.filename = (pn && pn[0]) ? basename((char *)pn) : "";
        if (entry.filename.empty()) continue;  // unnamed/synthetic — skip

        entry.size = (uint32_t)archive_entry_size(a_entry);
        return true;
    }
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
    // randomAccess=true: prefer libarchive's seekable reader so a
    // central-directory format (ZIP/7z/…) lists via the directory (range jumps)
    // instead of skipping each entry's data — the difference between a couple
    // of requests and reading/discarding the whole container.
    image->m_archive->open( std::ios_base::in, false, true );
    image->resetEntryCounter();

    media_archive = name;
    Debug_printv("Archive opened: [%s]", media_archive.c_str());

    return true;
}

bool ArchiveMFile::extractAll(const ExtractCallback &onEntry)
{
    // Single-file compressions (.gz/.bz2/…) have no directory to walk — the
    // caller extracts them transparently via the inner file.
    if (isSingleFileCompression())
        return false;

    // Reuse the one shared ArchiveMStream: listing and extraction go through
    // the same instance and the same source, so nothing else opens the host
    // concurrently (the reset that would corrupt a pooled HTTP connection).
    auto image = ImageBroker::obtain<ArchiveMStream>("archive", url);
    if (image == nullptr || image->m_archive == nullptr)
        return false;

    // 7z is a seekable-ONLY format (no streaming reader — it must seek to the
    // footer to read entries), so it has to open randomAccess=true and CANNOT
    // use the open-ended sequential source (which precludes seeking). Streaming
    // formats (zip/tar) open randomAccess=false and flip the source to
    // sequential — the whole container streams over ONE connection instead of
    // churning a request per block. Sequential is set AFTER open so the
    // SEEK_END probe during bidding stays a cheap range check, not a read
    // through.
    bool seekable = mstr::endsWith(url, ".7z", false);
    if (!image->m_archive->open(std::ios_base::in, false, seekable)) {
        Debug_printv("extractAll: failed to open archive [%s]", url.c_str());
        return false;
    }
    if (!seekable)
        image->m_archive->setSequential(true);
    image->resetEntryCounter();

    // Streams the current entry's raw bytes; returns 0 at end-of-entry.
    auto readFn = [image](uint8_t *buf, uint32_t n) -> uint32_t {
        if (!image->m_archive || !image->m_archive->isOpen()) return 0;
        la_ssize_t r = archive_read_data(image->m_archive->getArchive(), buf, n);
        if (r < 0) {
            Debug_printv("extractAll: read error: %s",
                         archive_error_string(image->m_archive->getArchive()));
            return 0;
        }
        return (uint32_t)r;
    };

    bool ok = true;
    while (image->nextEntrySimple()) {
        // Any bytes of this entry left unread are skipped by the next
        // archive_read_next_header(), so aborting a partial read is safe.
        // Hand over the STORED path so the caller can recreate the directory
        // structure; fall back to the basename when the archive stored none.
        const std::string &entryName = image->entry.pathname.empty()
                                     ? image->entry.filename
                                     : image->entry.pathname;
        if (!onEntry(entryName, image->entry.size, readFn)) {
            ok = false;
            break;
        }
    }

    image->m_archive->close();
    return ok;
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

    //Debug_printv("Calling getNextImageEntry()");
    do
    {
        r = image->getNextImageEntry();
        //Debug_printv("getNextImageEntry() returned %d, filename=[%s]", r, r ? image->entry.filename.c_str() : "");
    } while (r && image->entry.filename.empty()); // Don't want empty entries

    if (r)
    {
        std::string filename = image->entry.filename;
        //Debug_printv("Found entry: filename=[%s] size=%lu", filename.c_str(), image->entry.size);

        std::string entryUrl;
        entryUrl.reserve(url.size() + 1 + filename.size());
        entryUrl = url; entryUrl += '/'; entryUrl += filename;
        auto file = MFSOwner::File(entryUrl);
        file->name = filename;  // Use actual entry name, not container image name
        file->size = image->entry.size;

        return file;
    }

exit:
    dirIsOpen = false;
    image->m_archive->close();
    return nullptr;
}
