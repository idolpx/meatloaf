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

// SD:// - Secure Digital Card File System
// https://en.wikipedia.org/wiki/SD_card
// https://github.com/arduino-libraries/SD
//
#ifndef TEST_NATIVE
#ifndef MEATLOAF_DEVICE_SD
#define MEATLOAF_DEVICE_SD

#include "meatloaf.h"

#include "flash.h"
#include "fnFsSD.h"

#include "peoples_url_parser.h"

#include <dirent.h>
#include <string.h>

#define _filesystem fnSDFAT


/********************************************************
 * MFileSystem
 ********************************************************/

class SDFileSystem: public MFileSystem 
{
public:
    SDFileSystem(): MFileSystem("sd") {};

    bool handles(std::string name) {
        std::string pattern = "sd:";
        return mstr::equals(name, pattern, false);
    }

    MFile *getFile(std::string path) override {
        auto url = PeoplesUrlParser::parseURL( path );

        std::string basepath = _filesystem.basepath();
        basepath += std::string("/");
        //Debug_printv("basepath[%s] url.path[%s]", basepath.c_str(), url.path.c_str());

        return new FlashMFile(path);
    }
};


#endif // MEATLOAF_DEVICE_SD
#endif // TEST_NATIVE