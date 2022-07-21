// SD:// - Secure Digital Card File System
// https://en.wikipedia.org/wiki/SD_card
// https://github.com/arduino-libraries/SD
//

#ifndef MEATFILE_DEFINES_SDFS_H
#define MEATFILE_DEFINES_SDFS_H

#include "meat_io.h"

#include "../../include/global_defines.h"
#include "../../include/make_unique.h"

#include "../device/flash.h"
#include "fnFsSD.h"

#include "device_db.h"
#include "peoples_url_parser.h"

#include <dirent.h>
#include <string.h>

#define _filesystem fnSDFAT


/********************************************************
 * MFileSystem
 ********************************************************/

class SDFileSystem: public MFileSystem 
{
private:
    MFile* getFile(std::string path) override {
        PeoplesUrlParser url;

        url.parseUrl(path);

        std::string basepath = fnSDFAT.basepath();
        basepath += std::string("/");
        device_config.url("/");
        device_config.basepath( basepath );

        return new FlashFile( url.path );
    }

    bool handles(std::string name) {
        std::string pattern = "sd:";
        return mstr::equals(name, pattern, false);
    }
public:
    SDFileSystem(): MFileSystem("sd") {};
};


#endif // MEATFILE_DEFINES_SD_H
