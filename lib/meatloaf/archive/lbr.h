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

// .LBR - LiBRary containers
//
// https://ist.uwaterloo.ca/~schepers/formats/LBR.TXT
// https://github.com/talas/lbrtool
//


#ifndef MEATLOAF_MEDIA_LBR
#define MEATLOAF_MEDIA_LBR

#include "../meatloaf.h"
#include "../meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class LBRMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    LBRMStream(std::shared_ptr<MStream> is) : MMediaStream(is) {
        loadEntries();
    };

protected:
    struct Header {
        std::string name;
        std::string id_dos;
    };

    struct Entry {
        std::string filename;
        uint16_t size;
        std::string type;
        uint32_t offset;
    };

    std::vector<Entry> entries;
    int8_t loadEntries();

    bool readHeader() override { return true; };
    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    Header header;
    Entry entry;

private:
    friend class LBRMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class LBRMFile: public MFile {
public:

    LBRMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        isPETSCII = true;
    };
    
    ~LBRMFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MStream* getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return new LBRMStream(is);
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
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class LBRMFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new LBRMFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".lbr", fileName);
    }

    LBRMFileSystem(): MFileSystem("lbr") {};
};


#endif /* MEATLOAF_MEDIA_LBR */
