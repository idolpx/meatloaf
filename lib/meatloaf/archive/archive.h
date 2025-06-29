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
#include "../meat_media.h"
#include "../meatloaf.h"

#ifdef BOARD_HAS_PSRAM
#include <esp_psram.h>
#ifdef CONFIG_IDF_TARGET_ESP32
#include <esp32/himem.h>
#endif
#endif

class Archive {
   public:
    Archive(std::shared_ptr<MStream> srcStream) {
        m_srcStream = srcStream;
        m_srcBuffer = nullptr;
        m_archive = nullptr;
        Debug_printv("Archive constructor");
    }

    ~Archive() {
        close();
        if (m_srcBuffer != nullptr) {
            delete m_srcBuffer;
        }
        Debug_printv("Archive destructor");
    }

    bool open(std::ios_base::openmode mode);
    void close();

    bool isOpen() { return m_archive != nullptr; }
    archive *getArchive() { return m_archive; }

   private:
    struct archive *m_archive = nullptr;
    uint8_t *m_srcBuffer = nullptr;
    std::shared_ptr<MStream> m_srcStream = nullptr;  // a stream that is able to serve bytes of this archive

  static const size_t m_buffSize = 4096;

  //friend int cb_open(struct archive *, void *userData);
  //friend int cb_close(struct archive *, void *userData);
  friend ssize_t cb_read(struct archive *, void *userData, const void **buff);
  friend int64_t cb_skip(struct archive *, void *userData, int64_t request);
  friend int64_t cb_seek(struct archive *, void *userData, int64_t offset, int whence);
};

/********************************************************
 * Streams implementations
 ********************************************************/

class ArchiveMStream : public MMediaStream {
   public:

    ArchiveMStream(std::shared_ptr<MStream> is) : MMediaStream(is) {
        m_archive = new Archive(containerStream);
        m_haveData = 0;
        m_mode = std::ios::in;
        m_dirty = false;
        Debug_printv("ArchiveMStream constructor");
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

   private:
    void readArchiveData();

    Archive *m_archive;
    std::ios_base::openmode m_mode;

    int m_haveData;
    bool m_dirty;

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(BOARD_HAS_PSRAM)
    // contains unzipped contents of archive (in HIMEM)
    esp_himem_handle_t m_data;

    // memory range mapped to HIMEM
    static esp_himem_rangehandle_t s_range;
    static int s_rangeUsed;
#else
    uint8_t *m_data;
#endif

    friend class ArchiveMFile;
};

/********************************************************
 * Files implementations
 ********************************************************/

class ArchiveMFile : public MFile {
   public:
    ArchiveMFile(std::string path) : MFile(path)
    {
        media_archive = name;
    }

    ~ArchiveMFile() {
        if (m_archive != nullptr) delete m_archive;
    }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<ArchiveMStream>(is);
    }

    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile *getNextFileInDir() override;
    bool mkDir() override { return false; };

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };

    bool isDir = true;
    bool dirIsOpen = false;

   private:
    Archive *m_archive = nullptr;
};

/********************************************************
 * FS implementations
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