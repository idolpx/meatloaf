// TNFS:// - Spectranet File System
// https://www.bytedelight.com/?page_id=3515
// https://github.com/FujiNetWIFI/spectranet/blob/master/tnfs/tnfs-protocol.md
//

#ifndef MEATLOAF_SCHEME_TNFS
#define MEATLOAF_SCHEME_TNFS

#include "meat_io.h"

#include "../../include/global_defines.h"
#include "../../include/make_unique.h"

#include "../device/flash.h"
#include "fnFsTNFS.h"

#include "device_db.h"
#include "peoples_url_parser.h"

#include <dirent.h>
#include <string.h>

#define _filesystem fnTNFS


/********************************************************
 * MFileSystem
 ********************************************************/

class TNFSFileSystem: public MFileSystem 
{
private:
    MFile* getFile(std::string path) override {
        PeoplesUrlParser url;

        url.parseUrl(path);

        if (!fnTNFS.running())
            fnTNFS.start(url.host.c_str(), TNFS_DEFAULT_PORT, url.path.c_str() , url.user.c_str(), url.pass.c_str());

        std::string basepath = fnTNFS.basepath();
        basepath += std::string("/");
        device_config.url("/");
        device_config.basepath( basepath );

        return new FlashFile( url.path );
    }

    bool handles(std::string name) {
        std::string pattern = "tnfs:";
        return mstr::equals(name, pattern, false);
    }
public:
    TNFSFileSystem(): MFileSystem("tnfs") {};
};


#endif // MEATLOAF_SCHEME_TNFS
