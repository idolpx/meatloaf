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

// .PRG - RAW CBM PRG file
//

#ifndef MEATLOAF_MEDIA_PRG
#define MEATLOAF_MEDIA_PRG

#include "meatloaf.h"
#include "meat_media.h"

/********************************************************
 * Streams
 ********************************************************/

class PRGMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    PRGMStream(std::shared_ptr<MStream> is) : MMediaStream(is) {
        _size = ( containerStream->size() );
    };

protected:
    uint32_t readFile(uint8_t* buf, uint32_t size) override {
        uint32_t bytesRead = 0;

        bytesRead += containerStream->read(buf, size);
        _position += bytesRead;

        return bytesRead;
    }
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };

private:
    friend class PRGMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class PRGMFile: public MFile {
public:

    PRGMFile(std::string path, bool is_dir = false): MFile(path) {};
    
    // ~PRGMFile() {
    //     // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    // }

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<PRGMStream>(is);
    }
};



/********************************************************
 * FS
 ********************************************************/

class PRGMFileSystem: public MFileSystem
{
public:
    PRGMFileSystem(): MFileSystem("prg") {};

    bool handles(std::string fileName) override {
        return byExtension(".prg", fileName);
    }

    MFile* getFile(std::string path) override {
        return new PRGMFile(path);
    }
};

#endif // MEATLOAF_MEDIA_PRG