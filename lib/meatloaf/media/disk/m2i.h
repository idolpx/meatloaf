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

// .M2I - MMC2IEC Disk Image format
//
// A plain-text index that gives FAT files CBM names: line 1 is the disk
// title (16 chars), then one line per entry
//
//   "T:DOSNAME.EXT :CBMNAME         " CR LF
//
// where T is P/S/U (PRG/SEQ/USR), D (deleted) or '-' (free slot), DOSNAME
// is the space-padded 8.3 host filename and CBMNAME the CBM name.
// Nominally fixed-width (33-byte records), but files in the wild have
// unpadded CBM names and even a UTF-8 BOM, so lines are parsed at the ':'
// separators. The entry data lives in the host files NEXT TO the .m2i -
// reads are delegated to the target file's stream.
//
// https://larsee.com/blog/2007/02/the-mmc2iec-device/
// https://github.com/idolpx/sd2iec/blob/master/src/m2iops.c
//

#ifndef MEATLOAF_MEDIA_M2I
#define MEATLOAF_MEDIA_M2I

#include <string>
#include <vector>

#include "meatloaf.h"
#include "meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class M2IMStream : public MMediaStream {

public:
    M2IMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        readHeader();
    };

protected:
    struct Entry {
        char type;              // 'P','S','U' = file; 'D' = DEL separator line
        std::string dosname;    // 8.3 host filename (trimmed; blank for 'D')
        std::string cbmname;    // CBM name (trimmed)
    };

    // The whole index is parsed once (M2I files are a few hundred bytes)
    bool readHeader() override;
    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

    // URL of the current entry's host file (sibling of the .m2i)
    std::string entryTargetUrl();

    std::string title;
    std::vector<Entry> entries;
    Entry entry;                // currently selected entry

    // The host file being served after seekPath()
    std::shared_ptr<MStream> fileStream;

private:
    friend class M2IMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class M2IMFile: public MFile {
public:

    M2IMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
        isPETSCII = true;
        media_image = name;
    };

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<M2IMStream>(is);
    }

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;

    bool isDir = true;
    bool dirIsOpen = false;
};


/********************************************************
 * FS
 ********************************************************/

class M2IMFileSystem: public MFileSystem
{
public:
    M2IMFileSystem(): MFileSystem("m2i") {};

    bool handles(std::string fileName) override {
        return byExtension(".m2i", fileName);
    }

    MFile* getFile(std::string path) override {
        return new M2IMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_M2I */
