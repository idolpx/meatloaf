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

// .7Z, .ARC, .ARK, .BZ2, .GZ, .LHA, .LZH, .LZX, .RAR, .TAR, .TGZ, .XAR, .ZIP -
// libArchive for Meatloaf!
//
// https://stackoverflow.com/questions/22543179/how-to-use-libarchive-properly
// https://libarchive.org/

#ifndef MEATLOAF_ARCHIVE
#define MEATLOAF_ARCHIVE

#include <archive.h>
#include <archive_entry.h>

#include "../../../include/debug.h"
#include "meat_media.h"
#include "meatloaf.h"
#include "meat_session.h"


class Archive {
   public:
    Archive(std::shared_ptr<MStream> srcStream) {
        m_srcStream = srcStream;
        m_srcBuffer = nullptr;
        m_archive = nullptr;
        Debug_printv("Archive constructor url[%s]", srcStream->url.c_str());
    }

    ~Archive() {
        close();
        Debug_printv("Archive destructor");
    }

    bool open(std::ios_base::openmode mode, bool rawOnly = false);
    void close();

    bool isOpen() { return m_archive != nullptr; }
    archive *getArchive() { return m_archive; }
    bool hasCompressionFilter() { return m_hasCompressionFilter; }

   private:
    struct archive *m_archive = nullptr;
    uint8_t *m_srcBuffer = nullptr;
    std::shared_ptr<MStream> m_srcStream = nullptr;  // a stream that is able to serve bytes of this archive
    bool m_hasCompressionFilter = false;  // True when gzip/bz2/xz/etc filter is active (disables raw seeking)

  static const size_t m_buffSize = 4096;

  //friend int cb_open(struct archive *, void *userData);
  //friend int cb_close(struct archive *, void *userData);
  friend ssize_t cb_read(struct archive *, void *userData, const void **buff);
  friend int64_t cb_skip(struct archive *, void *userData, int64_t request);
  friend int64_t cb_seek(struct archive *, void *userData, int64_t offset, int whence);
};


/********************************************************
 * ArchiveMSession Implementation
 ********************************************************/

class ArchiveMSession : public MSession {
public:
    ArchiveMSession(const std::string& archiveUrl)
        : MSession("archive://" + archiveUrl, "", 0)
    {
        setKeepAliveInterval(0);  // disable keep-alive for archive sessions
    }

    ~ArchiveMSession() {
        disconnect();
    }

    static std::string getScheme() { return "archive"; }

    bool connect() override {
        connected = true;
        return true;
    }

    void disconnect() override {
        clearFileCache();
        connected = false;
    }

    bool keep_alive() override {
        return true;  // No network to maintain
    }

    // Extract archive entry data into a CachedFile using loadViaReader
    bool loadEntryFromArchive(std::shared_ptr<CachedFile>& cf, struct archive* a, uint32_t entrySize) {
        return cf->loadViaReader(entrySize, [a](uint8_t* buf, uint32_t n) -> uint32_t {
            la_ssize_t r = archive_read_data(a, buf, n);
            if (archive_errno(a) != ARCHIVE_OK) {
                Debug_printv("archive read error %i: %s", archive_errno(a), archive_error_string(a));
                return 0;
            }
            if ((uint32_t)r != n) {
                Debug_printv("expected to read %u bytes from archive, got %zd", n, r);
            }
            return (uint32_t)r;
        });
    }

    // Get or extract an archive entry, caching the result
    std::shared_ptr<CachedFile> getEntry(const std::string& entryPath, struct archive* a, uint32_t entrySize) {
        // Check cache first
        auto cached = getCachedFile(entryPath);
        if (cached) {
            Debug_printv("Cache hit for entry: %s (%u bytes)", entryPath.c_str(), cached->size);
            return cached;
        }

        // Extract from archive into new CachedFile
        Debug_printv("Extracting entry: %s (%u bytes)", entryPath.c_str(), entrySize);
        auto cf = std::make_shared<CachedFile>(entrySize);
        if (!loadEntryFromArchive(cf, a, entrySize)) {
            Debug_printv("Failed to extract entry: %s", entryPath.c_str());
            return nullptr;
        }

        cacheFile(entryPath, cf);
        return cf;
    }
};


/********************************************************
 * ArchiveMStream implementations
 ********************************************************/

class ArchiveMStream : public MMediaStream {
   public:

    ArchiveMStream(std::shared_ptr<MStream> is) : MMediaStream(is) {
        Debug_printv("Creating Archive object url[%s]", is->url.c_str());
        m_archive = new Archive(is);
        m_mode = std::ios::in;
        m_isCompressedOnly = false;
        Debug_printv("constructor url[%s]", is->url.c_str());
    }

    ~ArchiveMStream() {
        close();
        if (m_archive) delete m_archive;
        Debug_printv("ArchiveMStream destructor");
    }

   protected:

    struct archive_entry *a_entry;
    struct Entry {
        std::string filename;
        uint32_t size;
    };
    Entry entry;

    bool isOpen() override;
    bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t *buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    virtual bool seek(uint32_t pos) override;

    bool readHeader() override { return true; };
    bool seekEntry(std::string filename) override;
    bool seekEntry( uint16_t index ) override;

    // For files with a browsable random access directory structure
    // d64, d74, d81, dnp, etc.
    uint32_t readFile(uint8_t *buf, uint32_t size) override;
    uint32_t writeFile(uint8_t *buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;
    bool seekCachedFile(const std::string sessionKey, const std::string path);

   private:
    bool ensureData();

    Archive *m_archive;
    std::ios_base::openmode m_mode;

    bool m_isCompressedOnly;  // True for standalone compressed files like .gz, .bz2 (single entry only)

    std::shared_ptr<ArchiveMSession> m_session;
    std::shared_ptr<MSession::CachedFile> m_cachedEntry;

    friend class ArchiveMFile;
};

/********************************************************
 * ArchiveMFile Implementation
 ********************************************************/

class ArchiveMFile : public MFile {
   public:
    ArchiveMFile(std::string path) : MFile(path)
    {
        media_archive = name;
        //Debug_printv("constructor url[%s]", path.c_str());
    }

    ~ArchiveMFile() {
        if (m_archive != nullptr) delete m_archive;
        if (m_innerFile != nullptr) delete m_innerFile;
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        if (isSingleFileCompression() && !pathInStream.empty()) {
            std::string innerFilename = getInnerFilename();
            // If pathInStream is NOT the inner compressed file itself, it refers to a file
            // INSIDE the inner container (e.g. LOADER inside mars saga.d81 inside .d81.gz).
            // Build InnerFormatStream(ArchiveMStream(is)) so the caller's seekPath()
            // resolves against the inner container format (D81, D64, etc.), not the gz.
            if (!mstr::compareFilename(pathInStream, innerFilename, false)) {
                auto archiveStream = std::make_shared<ArchiveMStream>(is);
                if (archiveStream->seekPath(innerFilename)) {
                    auto inner = getInnerFile();
                    if (inner) {
                        auto innerStream = inner->getDecodedStream(archiveStream);
                        if (innerStream) return innerStream;
                    }
                }
            }
        }
        return std::make_shared<ArchiveMStream>(is);
    }

    // Returns true if this archive is a single-file compression (.gz, .bz2, etc.)
    // as opposed to a multi-file archive (.tar.gz, .zip, .7z, etc.).
    // Single-file compressed archives are transparent: directory operations delegate
    // directly to the inner file so the compression layer is invisible to the user.
    bool isSingleFileCompression() const {
        static const char* multiFileExts[] = {
            ".tar.gz", ".tgz", ".tar.bz2", ".tar.xz", ".tar.lz", ".tar.z", ".cpgz", nullptr
        };
        for (int i = 0; multiFileExts[i]; i++) {
            if (mstr::endsWith(name, multiFileExts[i], false)) return false;
        }
        static const char* singleFileExts[] = {
            ".gz", ".bz2", ".xz", ".lz", ".z", ".zst", ".lz4", nullptr
        };
        for (int i = 0; singleFileExts[i]; i++) {
            if (mstr::endsWith(name, singleFileExts[i], false)) return true;
        }
        return false;
    }

    // Strip the outermost compression extension to get the inner filename.
    std::string getInnerFilename() const {
        static const char* exts[] = {".gz", ".bz2", ".xz", ".lz", ".z", ".zst", ".lz4", nullptr};
        for (int i = 0; exts[i]; i++) {
            if (mstr::endsWith(name, exts[i], false)) {
                return name.substr(0, name.length() - strlen(exts[i]));
            }
        }
        return name;
    }

    // Lazily create/return the inner MFile (e.g. D81MFile for a .d81.gz).
    MFile* getInnerFile() {
        if (!m_innerFile) {
            m_innerFile = MFSOwner::File(url + "/" + getInnerFilename());
            isPETSCII = m_innerFile->isPETSCII;
        }
        return m_innerFile;
    }

    bool isDirectory() override {
        if (isSingleFileCompression()) {
            // Non-empty pathInStream means the drive is looking for a specific file/pattern
            // *inside* the inner container (e.g. "*" or "LOADER") — that's a file, not a dir.
            if (!pathInStream.empty()) return false;
            auto inner = getInnerFile();
            if (inner) return inner->isDirectory();
        }
        return isDir;
    }

    bool rewindDirectory() override;
    MFile *getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;

   private:
    Archive *m_archive = nullptr;
    MFile   *m_innerFile = nullptr;
};

/********************************************************
 * ArchiveMFileSystem Implementation
 ********************************************************/

class ArchiveMFileSystem : public MFileSystem
{
public:
    ArchiveMFileSystem() : MFileSystem("archive") {};

    bool handles(std::string fileName)
    {
        return byExtension(
            {
                ".tar.xz",
                ".tar.bz2",
                ".tar.gz",
                ".tar.z",
                ".tar.lz",
                ".tar",
                ".tgz",
                ".7z",
                ".bz2",
                ".gz",
                ".lha",
                ".lzh",
                ".lzx",
                ".rar",
                ".xar",
                ".zip",
                ".zst",
                ".iso",
                ".lz4",
                ".cpgz",
                ".cpio",
                ".rp9",     // Cloanto RetroPlatform Archive (https://www.retroplatform.com/kb/15-122)
                ".vms"      // Meatloaf Virtual Media Stack!
                //".arc",  // Have to find a way to distinquish between PC/C64 ARC file
                //".ark",  // Have to find a way to distinquish between PC/C64 ARK file
            },
            fileName
        );
    }

    MFile *getFile(std::string path)
    {
        //Debug_printv("path[%s]", path.c_str());
        return new ArchiveMFile(path);
    };
};

#endif  // MEATLOAF_ARCHIVE