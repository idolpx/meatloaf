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

#include "meatloaf.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <sstream>


#ifdef FLASH_SPIFFS
#include "esp_spiffs.h"
#endif

#ifdef FLASH_LITTLEFS
#include "esp_littlefs.h"
#endif

#include "../device/iec/meatloaf.h"

#include "meat_broker.h"
#include "meat_buffer.h"
//#include "wrappers/directory_stream.h"

#include "string_utils.h"
#include "peoples_url_parser.h"

#include "MIOException.h"
#include "../../include/debug.h"

// Archive
#include "archive/archive.h"
#include "archive/ark.h"
#include "archive/lbr.h"

// Cartridge

// Container
#include "container/d8b.h"
#include "container/dfi.h"

// Device
#include "device/flash.h"
#include "device/sd.h"

// Disk
#include "disk/d64.h"
#include "disk/d71.h"
#include "disk/d80.h"
#include "disk/d81.h"
#include "disk/d82.h"
#include "disk/d90.h"
#include "disk/dnp.h"
#include "disk/g64.h"
#include "disk/nib.h"

// File
#include "file/p00.h"

// Link
// Loaders

// Network
#include "network/http.h"
#include "network/tnfs.h"
// #include "network/ipfs.h"
// #include "network/smb.h"
// #include "network/ws.h"

// Scanners

// Service
#include "service/csip.h"
#include "service/ml.h"

// Tape
#include "tape/t64.h"
#include "tape/tcrt.h"

std::unordered_map<std::string, MFile*> FileBroker::file_repo;
std::unordered_map<std::string, MStream*> StreamBroker::stream_repo;

/********************************************************
 * MFSOwner implementations
 ********************************************************/

// initialize other filesystems here
FlashMFileSystem defaultFS;
#ifdef SD_CARD
SDFileSystem sdFS;
#endif


// Archive
ArchiveMFileSystem archiveFS;
ARKMFileSystem arkFS;
LBRMFileSystem lbrFS;

// Cartridge

// Container
D8BMFileSystem d8bFS;
DFIMFileSystem dfiFS;

// File
P00MFileSystem p00FS;

// Disk
D64MFileSystem d64FS;
D71MFileSystem d71FS;
D80MFileSystem d80FS;
D81MFileSystem d81FS;
D82MFileSystem d82FS;
D90MFileSystem d90FS;
DNPMFileSystem dnpFS;
G64MFileSystem g64FS;
NIBMFileSystem nibFS;

// Network
HTTPMFileSystem httpFS;
TNFSMFileSystem tnfsFS;
// IPFSFileSystem ipfsFS;
// TcpFileSystem tcpFS;
//WSFileSystem wsFS;

// Service
CSIPMFileSystem csipFS;
MLMFileSystem mlFS;

// Tape
T64MFileSystem t64FS;
TCRTMFileSystem tcrtFS;


// put all available filesystems in this array - first matching system gets the file!
// fist in list is default
std::vector<MFileSystem*> MFSOwner::availableFS { 
    &defaultFS,
#ifdef SD_CARD
    &sdFS,
#endif
    &archiveFS, // extension-based FS have to be on top to be picked first, otherwise the scheme will pick them!
    &arkFS, &lbrFS,
//#ifndef USE_VDRIVE
    &d64FS, &d71FS, &d80FS, &d81FS, &d82FS, &d90FS, &dnpFS, 
    &g64FS,
//  &p64FS,
//#endif
    &nibFS,
    &d8bFS, &dfiFS,

    &t64FS, &tcrtFS,

    &p00FS,

    &httpFS, &tnfsFS,
//    &csipFS, &mlFS,
//    &ipfsFS, &tcpFS,
//    &tnfsFS
};

bool MFSOwner::mount(std::string name) {
    Debug_print("MFSOwner::mount fs:");
    Debug_println(name.c_str());

    for(auto i = availableFS.begin() + 1; i < availableFS.end() ; i ++) {
        auto fs = (*i);

        if(fs->handles(name)) {
                Debug_printv("MFSOwner found a proper fs");

            bool ok = fs->mount();

            if(ok)
                Debug_print("Mounted fs: ");
            else
                Debug_print("Couldn't mount fs: ");

            Debug_println(name.c_str());

            return ok;
        }
    }
    return false;
}

bool MFSOwner::umount(std::string name) {
    for(auto i = availableFS.begin() + 1; i < availableFS.end() ; i ++) {
        auto fs = (*i);

        if(fs->handles(name)) {
            return fs->umount();
        }
    }
    return true;
}

MFile* MFSOwner::File(MFile* file) {
    return File(file->url);
}

MFile* MFSOwner::File(std::shared_ptr<MFile> file) {
    return File(file->url);
}

void MFile::setupFields() {
    std::vector<std::string> paths = mstr::split(path,'/');
    auto pathIterator = paths.end();
    auto begin = paths.begin();
    auto end = paths.end();

    MFileSystem * thisPathFactoringFS = MFSOwner::findParentFS(begin, end, pathIterator);

    _pathInStream = mstr::joinToString(&pathIterator, &end, "/");
    Debug_printv("MFSOwner::setupFields(%s) path relative to '%s' root is: [%s]", path.c_str(), thisPathFactoringFS->symbol, _pathInStream.c_str());

    end = pathIterator;
    auto containerPath = mstr::joinToString(&begin, &pathIterator, "/");

    MFileSystem *containerFileSystem = &defaultFS;

    if(_pathInStream.empty())
    {
        Debug_printv("MFSOwner::setupFields(%s) is the container itself, let's go one up", path.c_str());
        pathIterator--;
    }

    if( pathIterator != begin ) {
        containerFileSystem = MFSOwner::findParentFS(begin, end, pathIterator);
    }
    _sourceFile = containerFileSystem->getFile(containerPath);
    Debug_printv("MFSOwner::setupFields(%s) container path [%s], will be created by this fs: %s", path.c_str(), containerPath.c_str(), containerFileSystem->symbol);
}

std::string MFile::pathInStream() {
    if(_pathInStream == "")
        setupFields();
    return _pathInStream;
}

MFile* MFile::sourceFile() {
    if(_sourceFile == nullptr)
        setupFields();
    return _sourceFile;
}

MFile* MFSOwner::File(std::string path, bool default_fs) {

    if ( !default_fs )
    {
        if ( mlFS.handles(path) )
        {
            path = mlFS.resolve(path);
        }

        if ( csipFS.handles(path) )
        {
            printf("C=Server!\r\n");
            return csipFS.getFile(path);
        }
    }

    std::vector<std::string> paths = mstr::split(path,'/');
    auto pathIterator = paths.end();
    auto begin = paths.begin();
    auto end = paths.end();

    Debug_printv("MFSOwner::File(%s) let's find factoring filesystem of this path", path.c_str());
    MFileSystem * thisPathFactoringFS = findParentFS(begin, end, pathIterator);
    MFile *thisFile = thisPathFactoringFS->getFile(path);

    Debug_printv("MFSOwner::File(%s) is created by '%s' FS", path.c_str(), thisPathFactoringFS->symbol);

    return thisFile;
}

MFileSystem* MFSOwner::findParentFS(std::vector<std::string>::iterator &begin, std::vector<std::string>::iterator &end, std::vector<std::string>::iterator &pathIterator) {
    while (pathIterator != begin)
    {
        //Debug_printv("MFSOwner::findParentFS - going left the path");
        pathIterator--;

        auto part = *pathIterator;
        mstr::toLower(part);
        //Debug_printv("MFSOwner::findParentFS - examining part[%s]", part.c_str());
        if ( part.size() )
        {
            auto foundFS=std::find_if(availableFS.begin() + 1, availableFS.end(), [&part](MFileSystem* fs){ 
                //Debug_printv("symbol[%s]", fs->symbol);
                bool found = fs->handles(part);
                if ( !found )
                    return false;

                //Debug_printv("found[%d] part[%s] use_vdrive[%d] vdrive_compatible[%d]", found, part.c_str(), Meatloaf.use_vdrive, fs->vdrive_compatible);

                // If we're using vdrive, and this filesystem is vdrive compatible, skip it
                if (Meatloaf.use_vdrive && fs->vdrive_compatible)
                    return false;
                else
                    return true;
            });

            if(foundFS != availableFS.end()) {
                //Debug_printv("matched[%s] foundFS[%s]", part.c_str(), (*foundFS)->symbol);
                pathIterator++;
                return (*foundFS);
            }
        } else {
            Debug_printv("MFSOwner::findParentFS - FINISHED WALKING TO THE LEFT");
        }
    };

    auto fs = *availableFS.begin();  // The first filesystem in the list is the default
    pathIterator++;
    //Debug_printv("default[%s]", fs->symbol);
    return fs;
}


MFile* MFSOwner::NewFile(std::string path) {

    auto newFile = File(path);
    if ( newFile != nullptr )
        return nullptr;
    
    if (newFile->exists()) {
        Debug_printv("File already exists [%s]", path.c_str());
        return nullptr;
    }

    return newFile;
}


std::string MFSOwner::existsLocal( std::string path )
{
    auto url = PeoplesUrlParser::parseURL( path );

    // Debug_printv( "path[%s] name[%s] size[%d]", path.c_str(), url.name.c_str(), url.name.size() );
    if ( url->name.size() == 16 )
    {
        struct stat st;
        int i = stat(std::string(path).c_str(), &st);

        // If not found try for a wildcard match
        if ( i == -1 )
        {
            DIR *dir;
            struct dirent *ent;

            std::string p = url->pathToFile();
            std::string name = url->name;
            // Debug_printv( "pathToFile[%s] basename[%s]", p.c_str(), name.c_str() );
            if ((dir = opendir ( p.c_str() )) != NULL)
            {
                /* print all the files and directories within directory */
                std::string e;
                while ((ent = readdir (dir)) != NULL) {
                    // Debug_printv( "%s\r\n", ent->d_name );
                    e = ent->d_name;
                    if ( mstr::compare( name, e ) )
                    {
                        path = ( p + "/" + e );
                        break;
                    }
                }
                closedir (dir);
            }
        }        
    }

    return path;
}

/********************************************************
 * MFileSystem implementations
 ********************************************************/

MFileSystem::MFileSystem(const char* s)
{
    symbol = s;
}

MFileSystem::~MFileSystem() {}

/********************************************************
 * MFile implementations
 ********************************************************/

MFile::MFile(std::string path) {
    // Debug_printv("path[%s]", path.c_str());

    // if ( mstr::contains(path, "$") )
    // {
    //     // Create directory stream here
    //     Debug_printv("Create directory stream here!");
    //     path = "";
    // }
    //Debug_printv("ctor path[%s]", path.c_str());

    resetURL(path);
}

MFile::MFile(std::string path, std::string name) : MFile(path + "/" + name) {
    if(mstr::startsWith(name, "xn--")) {
        this->path = path + "/" + U8Char::fromPunycode(name);
    }
}

MFile::MFile(MFile* path, std::string name) : MFile(path->path + "/" + name) {
    if(mstr::startsWith(name, "xn--")) {
        this->path = path->path + "/" + U8Char::fromPunycode(name);
    }
}

bool MFile::operator!=(nullptr_t ptr) {
    return m_isNull;
}

MStream* MFile::getSourceStream(std::ios_base::openmode mode) {
    // has to return OPENED stream

    auto sourceStream = sourceFile()->getSourceStream(mode);
    if ( sourceStream == nullptr )
    {
        Debug_printv("UNFORTUNATELY recursive call returned NULL!, bailing out");
        return nullptr;
    }

    // will be replaced by streamBroker->getSourceStream(sourceFile, mode)
    std::shared_ptr<MStream> containerStream(sourceStream); // get its base stream, i.e. zip raw file contents

    Debug_printv("containerStream isRandomAccess[%d] isBrowsable[%d] null[%d]", containerStream->isRandomAccess(), containerStream->isBrowsable(), (containerStream == nullptr));

    // will be replaced by streamBroker->getDecodedStream(this, mode, containerStream)
    MStream* decodedStream(getDecodedStream(containerStream)); // wrap this stream into decoded stream, i.e. unpacked zip files
    decodedStream->url = this->url;
    Debug_printv("decodedStream isRandomAccess[%d] isBrowsable[%d] null[%d]", decodedStream->isRandomAccess(), decodedStream->isBrowsable(), (decodedStream == nullptr));

    if(decodedStream->isRandomAccess() && pathInStream() != "")
    {
        Debug_printv("pathInStream[%s]", pathInStream().c_str());

        // For files with a browsable random access directory structure
        // d64, d74, d81, dnp, etc.
        bool foundIt = decodedStream->seekPath(this->pathInStream());

        if(!foundIt)
        {
            Debug_printv("path in random access stream not found");
            return nullptr;
        }        
    }
    else if(decodedStream->isBrowsable() && pathInStream() != "")
    {
        // For files with no directory structure
        // tap, crt, tar
        auto pointedFile = decodedStream->seekNextEntry();

        while (!pointedFile.empty())
        {
            if(mstr::compare(_pathInStream, pointedFile))
            {
                Debug_printv("returning decodedStream 1");
                return decodedStream;
            }

            pointedFile = decodedStream->seekNextEntry();
        }
        Debug_printv("path in browsable stream not found!");
        if(pointedFile.empty())
            return nullptr;
    }

    Debug_printv("returning decodedStream 2");
    return decodedStream;
};


MFile* MFile::cd(std::string newDir) 
{
    Debug_printv("cd[%s]", newDir.c_str());

    // OK to clarify - coming here there should be ONLY path or magicSymbol-path combo!
    // NO "cd:xxxxx", no "/cd:xxxxx" ALLOWED here! ******************
    //
    // if you want to support LOAD"CDxxxxxx" just parse/drop the CD BEFORE calling this function
    // and call it ONLY with the path you want to change into!

    if(newDir.find(':') != std::string::npos) 
    {
        // I can only guess we're CDing into another url scheme, this means we're changing whole path
        return MFSOwner::File(newDir);
    }
    else if(newDir[0]=='_') // {CBM LEFT ARROW}
    {
        // user entered: CD:_ or CD_ 
        // means: go up one directory

        // user entered: CD:_DIR or CD_DIR
        // means: go to a directory in the same directory as this one
        return cdParent(mstr::drop(newDir,1));
    }
    else if(newDir[0]=='.' && newDir[1]=='.')
    {
        if(newDir.size()==2) 
        {
            // user entered: CD:.. or CD..
            // means: go up one directory
            return cdParent();
        }
        else 
        {
            // user entered: CD:..DIR or CD..DIR
            // meaning: Go back one directory
            return cdLocalParent(mstr::drop(newDir,2));
        }
    }
    else if(newDir[0]=='/' && newDir[1]=='/') 
    {
        // user entered: CD:// or CD//
        // means: change to the root of stream

        // user entered: CD://DIR or CD//DIR
        // means: change to a dir in root of stream
        return cdLocalRoot(mstr::drop(newDir,2));
    }
    else if(newDir[0]=='/') 
    {
        // user entered: CD:/DIR or CD/DIR
        // means: go to a directory in the same directory as this one
        return cdParent(mstr::drop(newDir,1));
    }
    else if(newDir[0]=='^') // {CBM UP ARROW}
    {
        // user entered: CD:^ or CD^ 
        // means: change to flash root
        return cdRoot(mstr::drop(newDir,1));
    }
    else 
    {
        //newDir = mstr::toUTF8( newDir );

        // Add new directory to path
        if ( !mstr::endsWith(url, "/") && newDir.size() )
            url.push_back('/');

        // Add new directory to path
        MFile* newPath = MFSOwner::File(url + newDir);

        if(mstr::endsWith(newDir, ".url", false)) {
            // we need to get actual url

            //auto reader = Meat::New<MFile>(newDir);
            //auto istream = reader->getSourceStream();
            Meat::iostream reader(newPath);

            //uint8_t url[istream->size()]; // NOPE, streams have no size!
            //istream->read(url, istream->size());
            std::string url;
            reader >> url;

            Debug_printv("url[%s]", url.c_str());
            //std::string ml_url((char *)url);

            delete newPath;
            newPath = MFSOwner::File(url);
        }

        return newPath;
    }

    return nullptr;
};


MFile* MFile::cdParent(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());

    // drop last dir
    // add plus
    if(path.empty()) 
    {
        // from here we can go only to flash root!
        return MFSOwner::File("/", true);
    }
    else 
    {
        int lastSlash = url.find_last_of('/');
        if ( lastSlash == url.size() - 1 ) 
        {
            lastSlash = url.find_last_of('/', url.size() - 2);
        }
        std::string newDir = mstr::dropLast(url, url.size() - lastSlash);
        if(!plus.empty())
            newDir+= ("/" + plus);

        return MFSOwner::File(newDir);
    }
};

MFile* MFile::cdLocalParent(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());
    // drop last dir
    // check if it isn't shorter than sourceFile
    // add plus
    int lastSlash = url.find_last_of('/');
    if ( lastSlash == url.size() - 1 ) {
        lastSlash = url.find_last_of('/', url.size() - 2);
    }
    std::string parent = mstr::dropLast(url, url.size() - lastSlash);
    if(parent.length()-sourceFile()->url.length()>1)
        parent = sourceFile()->url;
    return MFSOwner::File( parent + "/" + plus );
};

MFile* MFile::cdRoot(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());
    return MFSOwner::File( "/" + plus, true );
};

MFile* MFile::cdLocalRoot(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());

    if ( path.empty() || sourceFile() == nullptr ) {
        // from here we can go only to flash root!
        return MFSOwner::File( "/" + plus, true );
    }
    return MFSOwner::File( sourceFile()->url + "/" + plus );
};

// bool MFile::copyTo(MFile* dst) {
//     Debug_printv("in copyTo\r\n");
//     Meat::iostream istream(this);
//     Meat::iostream ostream(dst);

//     int rc;

//     Debug_printv("in copyTo, iopen=%d oopen=%d\r\n", istream.is_open(), ostream.is_open());

//     if(!istream.is_open() || !ostream.is_open())
//         return false;

//     Debug_printv("commencing copy\r\n");

//     while((rc = istream.get())!= EOF) {     
//         ostream.put(rc);
//         if(ostream.bad() || istream.bad())
//             return false;
//     }

//     Debug_printv("copying finished, rc=%d\r\n", rc);

//     return true;
// };

bool MFile::exists() { 
    Debug_printv("here!");
    return _exists; 
};

uint64_t MFile::getAvailableSpace()
{
    if ( mstr::startsWith(path, (char *)"/sd") )
    {
#ifdef SD_CARD
        FATFS* fsinfo;
        DWORD fre_clust;

        if (f_getfree("/", &fre_clust, &fsinfo) == 0)
        {
            uint64_t total = ((uint64_t)(fsinfo->csize)) * (fsinfo->n_fatent - 2) * (fsinfo->ssize);
            uint64_t used = ((uint64_t)(fsinfo->csize)) * ((fsinfo->n_fatent - 2) - (fsinfo->free_clst)) * (fsinfo->ssize);
            uint64_t free = total - used;
            //Debug_printv("total[%llu] used[%llu free[%llu]", total, used, free);
            return free;
        }
#endif
    }
    else
    {
        size_t total = 0, used = 0;
#ifdef FLASH_SPIFFS
        esp_spiffs_info("flash", &total, &used);
#elif FLASH_LITTLEFS
        esp_littlefs_info("flash", &total, &used);
#endif
        size_t free = total - used;
        //Debug_printv("total[%d] used[%d] free[%d]", total, used, free);
        return free;
    }

    return 65535;
}
