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

// .7Z, .ARC, .ARK, .BZ2, .GZ, .LHA, .LZH, .LZX, .RAR, .TAR, .TGZ, .XAR, .ZIP - libArchive for Meatloaf!
//
// https://stackoverflow.com/questions/22543179/how-to-use-libarchive-properly
// https://libarchive.org/

#ifndef MEATLOAF_ARCHIVE
#define MEATLOAF_ARCHIVE

#include <archive.h>
#include <archive_entry.h>
#include "esp32/himem.h"

#include "../meatloaf.h"
#include "../meat_media.h"

#include "../../../include/debug.h"


class Archive
{
public:
  Archive(std::shared_ptr<MStream> srcStream)
  {
    m_srcStream = srcStream;
    m_srcBuffer = nullptr;
    m_archive   = nullptr;
  }
  
  ~Archive()
  {
    close();
    if (m_srcBuffer != nullptr)
    {
      delete m_srcBuffer;
    }
  }

  bool open(std::ios_base::openmode mode);
  void close();

  bool isOpen()         { return m_archive!=nullptr; }
  archive *getArchive() { return m_archive; }

private:
  struct archive *m_archive   = nullptr;
  uint8_t *m_srcBuffer = nullptr;
  std::shared_ptr<MStream> m_srcStream = nullptr; // a stream that is able to serve bytes of this archive

  static const size_t m_buffSize = 4096;

  friend ssize_t cb_read(struct archive *, void *userData, const void **buff);
};

/********************************************************
 * Streams implementations
 ********************************************************/

class ArchiveMStream : public MMediaStream
{
public:
    struct archive_entry *entry;

    ArchiveMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        m_archive = new Archive(is);
        m_haveData = 0;
        m_mode = std::ios::in;
        m_dirty = false;
    }

    ~ArchiveMStream()
    {
        close();
        if (m_archive)
            delete m_archive;
        Debug_printv("Stream destructor OK!");
    }

protected:
    bool isOpen() override;
    bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t *buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    virtual bool seek(uint32_t pos) override;

    bool readHeader() override { return false; };
    bool seekEntry( std::string filename ) override;
    // bool readEntry( uint16_t index ) override;

    // For files with a browsable random access directory structure
    // d64, d74, d81, dnp, etc.
    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

private:
    void readArchiveData();

    Archive *m_archive;
    std::ios_base::openmode m_mode;

    // contains unzipped contents of archive (in HIMEM)
    int m_haveData;
    esp_himem_handle_t m_data;
    bool m_dirty;

    // memory range mapped to HIMEM
    static esp_himem_rangehandle_t s_range;
    static int s_rangeUsed;
};


/********************************************************
 * Files implementations
 ********************************************************/

class ArchiveMFile : public MFile
{
public:
    ArchiveMFile(std::string path) : MFile(path)
    {
        media_archive = name;
    }

    ~ArchiveMFile()
    {
        if (m_archive != nullptr)
            delete m_archive;
    }

    MStream* getDecodedStream(std::shared_ptr<MStream> is)
    {
        Debug_printv("[%s]", url.c_str());
    
        return new ArchiveMStream(is);
    }

    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override { return false; };

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };

    bool isDir = true;

 private:
    Archive *m_archive = nullptr;
};


/********************************************************
 * FS implementations
 ********************************************************/

class ArchiveMFileSystem : public MFileSystem
{
    MFile *getFile(std::string path)
    {
        return new ArchiveMFile(path);
    };

public:
    ArchiveMFileSystem() : MFileSystem("archive") {}

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
                ".cpio"
                //".arc",  // Have to find a way to distinquish between PC/C64 ARC file
                //".ark",  // Have to find a way to distinquish between PC/C64 ARK file
            },
            fileName
        );
    }

private:
};

#endif // MEATLOAF_ARCHIVE