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
#else
#include "esp_littlefs.h"
#endif

#include "../device/iec/meatloaf.h"

#include "meat_broker.h"
#include "meat_buffer.h"

#include "string_utils.h"
#include "peoples_url_parser.h"

#include "MIOException.h"
#include "../../include/debug.h"



// Device
#include "device/flash.h"
#include "device/sd.h"

#ifndef MIN_CONFIG
// Archive
#include "media/archive/archive.h"
#include "media/archive/ark.h"
#include "media/archive/lbr.h"

// Service
#include "service/csip.h"

// Network
#include "network/tnfs.h"
// #include "network/ipfs.h"
// #include "network/smb.h"
// #include "network/ws.h"
#endif

// Cartridge

// Container
#include "media/container/d8b.h"
#include "media/container/dfi.h"

// File
#include "media/file/p00.h"

// Disk
#include "media/disk/d64.h"
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"
#include "media/disk/d90.h"
#include "media/disk/g64.h"
#include "media/disk/nib.h"

// Hard Disk
#include "media/hd/dnp.h"

// Tape
#include "media/tape/t64.h"
#include "media/tape/tcrt.h"

// Network
#include "network/http.h"

// Service
#include "service/ml.h"


// Codec
#include "codec/qr.h"

// Hash
#include "hash/hash.h"

// Link
// Loader
// Parser
// Scanner

//std::unordered_map<std::string, MFile*> FileBroker::file_repo;
//std::unordered_map<std::string, std::shared_ptr<MStream>> StreamBroker::stream_repo;

/********************************************************
 * MFSOwner implementations
 ********************************************************/

// initialize other filesystems here

// Device
FlashMFileSystem defaultFS;
#ifdef SD_CARD
SDFileSystem sdFS;
#endif

#ifndef MIN_CONFIG
// Archive
ArchiveMFileSystem archiveFS;
ARKMFileSystem arkFS;
LBRMFileSystem lbrFS;

// Service
CSIPMFileSystem csipFS;

// Network
TNFSMFileSystem tnfsFS;
// IPFSFileSystem ipfsFS;
// TcpFileSystem tcpFS;
//WSFileSystem wsFS;
#endif

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
G64MFileSystem g64FS;
NIBMFileSystem nibFS;

// Hard Disk
DNPMFileSystem dnpFS;

// Tape
T64MFileSystem t64FS;
TCRTMFileSystem tcrtFS;

// Network
HTTPMFileSystem httpFS;

// Service
MLMFileSystem mlFS;

// Codec
QRMFileSystem qrcEncoder;
HashMFileSystem hashEncoder;

// put all available filesystems in this array - first matching system gets the file!
// fist in list is default
std::vector<MFileSystem*> MFSOwner::availableFS { 

    // Device
    &defaultFS,     // Flash Filesystem
#ifdef SD_CARD
    &sdFS,
#endif

#ifndef MIN_CONFIG
    // Archive
    &archiveFS,     // extension-based FS have to be on top to be picked first, otherwise the scheme will pick them!
    &arkFS, &lbrFS,
#endif

    // Container
    &d8bFS, &dfiFS,

    // Disk
    &d64FS, &d71FS, &d80FS, &d81FS, &d82FS, &d90FS,
    &g64FS, &nibFS,

    // Hard Disk
    &dnpFS,

    // Tape
    &t64FS, &tcrtFS,

    // File
//    &prgFS,         // needs to be on top to be picked first
    &p00FS,

    // Network
    &httpFS,
#ifndef MIN_CONFIG
    &tnfsFS,
    //&ipfsFS, &tcpFS,
#endif

};

std::vector<MFileSystem*> MFSOwner::availableELLPSW {
    // Service
#ifndef MIN_CONFIG
    &csipFS,
#endif

    // Codec
    &qrcEncoder, &hashEncoder
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


MFile* MFSOwner::File(std::string path, bool default_fs) {

    if ( path.empty() )
        return nullptr;

    if ( !default_fs )
    {
        // If this is a ML Short Code, resolve it!
        if ( mlFS.handles(path) )
        {
            path = mlFS.resolve(path);
        }
        else
        {
            // Check for Encoder/Link/Loader/Scanner/Wrapper
            for(auto i = availableELLPSW.begin(); i < availableELLPSW.end() ; i ++) {
                auto ellpsw = (*i);

                Debug_printv("Checking symbol[%s]", ellpsw->symbol);
                if(ellpsw->handles(path)) {
                    return ellpsw->getFile(path);
                }
            }
        }
    }

    Debug_println("vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv");
    Debug_printv("targetPath[%s]", path.c_str());

    std::vector<std::string> paths = mstr::split(path,'/');
    auto pathIterator = paths.end();
    auto begin = paths.begin();
    auto end = paths.end();

    MFileSystem *targetFileSystem = &defaultFS;
    if ( !default_fs )
    {
        targetFileSystem = findParentFS(begin, end, pathIterator);
    }

    auto targetFile = targetFileSystem->getFile(path);

    // Set path to file in filesystem stream
    targetFile->pathInStream = mstr::joinToString(&pathIterator, &end, "/");

    end = pathIterator;
    pathIterator--;
    Debug_printv("targetFile[%s] in targetFileSystem[%s][%s]", targetFile->pathInStream.c_str(), targetFile->url.c_str(), targetFileSystem->symbol);

    auto sourcePath = mstr::joinToString(&begin, &pathIterator, "/");
    Debug_printv("sourcePath[%s]", sourcePath.c_str());

    if( begin == pathIterator )
    {
        Debug_printv("** LOOK UP PATH NOT NEEDED   path[%s] sourcePath[%s]", path.c_str(), sourcePath.c_str());
        targetFile->sourceFile = targetFileSystem->getFile(sourcePath);
    } 
    else 
    {
        //Debug_printv("** LOOK UP PATH: %s", sourcePath.c_str());

        // Find the container filesystem
        MFileSystem *sourceFileSystem = &defaultFS;
        if ( !default_fs )
        {
            sourceFileSystem = findParentFS(begin, end, pathIterator);
        }

        auto wholePath = mstr::joinToString(&begin, &end, "/");
        Debug_printv("wholePath[%s]", wholePath.c_str());

        // sourceFile is for raw access to the container stream
        targetFile->sourceFile = sourceFileSystem->getFile(wholePath);

        targetFile->isWritable = targetFile->sourceFile->isWritable;   // This stream is writable if the container is writable
        Debug_printv("sourceFile[%s] is in [%s][%s]", targetFile->sourceFile->pathInStream.c_str(), sourcePath.c_str(), sourceFileSystem->symbol);
    }

    if (targetFile != nullptr)
    {
        if (targetFile->sourceFile != nullptr)
        {
            Debug_printv("source good rootfs[%d][%s]", targetFile->sourceFile->m_rootfs, targetFile->sourceFile->url.c_str());
        }
        else
            Debug_printv("source bad");

        Debug_printv("target good rootfs[%d][%s]", targetFile->m_rootfs, targetFile->url.c_str());
    }
    else
        Debug_printv("target bad");

    Debug_println("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");

    return targetFile;
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

MFileSystem* MFSOwner::findParentFS(std::vector<std::string>::iterator &begin, std::vector<std::string>::iterator &end, std::vector<std::string>::iterator &pathIterator) {
    while (pathIterator != begin)
    {
        pathIterator--;

        auto part = *pathIterator;
        mstr::toLower(part);
        //Debug_printv("part[%s]", part.c_str());
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
        }
    };

    auto fs = *availableFS.begin();  // The first filesystem in the list is the default
    pathIterator++;
    //Debug_printv("default[%s]", fs->symbol);
    return fs;
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

std::shared_ptr<MStream> MFile::getSourceStream(std::ios_base::openmode mode) {

    if ( sourceFile == nullptr )
    {
        Debug_printv("null sourceFile for path[%s]", path.c_str());
        return nullptr;
    }

    // has to return OPENED stream
    Debug_printv("pathInStream[%s] sourceFile[%s]", pathInStream.c_str(), sourceFile->url.c_str());

    auto sourceStream = sourceFile->getSourceStream(mode);
    if ( sourceStream == nullptr )
    {
        Debug_printv("null sourceStream for path[%s]", path.c_str());
        return nullptr;
    }

    // will be replaced by streamBroker->getSourceStream(sourceFile, mode)
    std::shared_ptr<MStream> containerStream(sourceStream); // get its base stream, i.e. zip raw file contents

    Debug_printv("containerStream isRandomAccess[%d] isBrowsable[%d] null[%d]", containerStream->isRandomAccess(), containerStream->isBrowsable(), (containerStream == nullptr));

    // will be replaced by streamBroker->getDecodedStream(this, mode, containerStream)
    std::shared_ptr<MStream> decodedStream(getDecodedStream(containerStream)); // wrap this stream into decoded stream, i.e. unpacked zip files
    decodedStream->url = this->url;
    Debug_printv("decodedStream isRandomAccess[%d] isBrowsable[%d] null[%d]", decodedStream->isRandomAccess(), decodedStream->isBrowsable(), (decodedStream == nullptr));

    if(decodedStream->isRandomAccess() && pathInStream != "")
    {
        // For files with a browsable random access directory structure
        // d64, d74, d81, dnp, etc.
        bool foundIt = decodedStream->seekPath(this->pathInStream);

        if(!foundIt)
        {
            Debug_printv("path in stream not found");
            return nullptr;
        }        
    }
    else if(decodedStream->isBrowsable() && pathInStream != "")
    {
        // For files with no directory structure
        // tap, crt, tar
        auto pointedFile = decodedStream->seekNextEntry();

        while (!pointedFile.empty())
        {
            if(mstr::compare(this->pathInStream, pointedFile))
            {
                Debug_printv("returning decodedStream 1");
                return decodedStream;
            }

            pointedFile = decodedStream->seekNextEntry();
        }
        Debug_printv("path in stream not found!");
        if(pointedFile.empty())
            return nullptr;
    }

    Debug_printv("returning decodedStream 2");
    return decodedStream;
};


MFile* MFile::cd(std::string newDir) 
{
    Debug_printv("url[%s] cd[%s]", url.c_str(), newDir.c_str());

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
            return cdParent(mstr::drop(newDir,2));
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
    else if(newDir[0]=='^') // {CBM UP ARROW}
    {
        // user entered: CD:^ or CD^ 
        // means: change to flash root
        return cdRoot(mstr::drop(newDir,1));
    }
    else 
    {
        //newDir = mstr::toUTF8( newDir );
        if ( newDir[0]=='/' )
            newDir = mstr::drop(newDir,1);

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
        int lastSlash = path.find_last_of('/');
        if ( lastSlash == path.size() - 1 ) 
        {
            if ( lastSlash == 0 )
                return MFSOwner::File("/", true);

            lastSlash = path.find_last_of('/', path.size() - 2);
        }
        std::string newDir = mstr::dropLast(path, path.size() - lastSlash);
        if( plus.size() )
            newDir += "/" + plus;

        path = newDir;
        rebuildUrl();
        //Debug_printv("url[%s]", url.c_str());

        return MFSOwner::File(url);
    }
};

MFile* MFile::cdLocalParent(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());
    // drop last dir
    // check if it isn't shorter than sourceFile
    // add plus
    int lastSlash = path.find_last_of('/');
    if ( lastSlash == path.size() - 1 ) {
        lastSlash = path.find_last_of('/', path.size() - 2);
    }
    std::string parent = mstr::dropLast(path, path.size() - lastSlash);
    if(parent.length()-sourceFile->path.length()>1)
        parent = sourceFile->path;

    if(!plus.empty())
        parent += "/" + plus;

    path = parent;
    rebuildUrl();

    return MFSOwner::File(url);
};

MFile* MFile::cdRoot(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());
    return MFSOwner::File( "/" + plus, true );
};

MFile* MFile::cdLocalRoot(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());

    if ( path.empty() || sourceFile == nullptr ) {
        // from here we can go only to flash root!
        path = "/";
    } else {
        path = sourceFile->path;
    }
    if ( plus.size() )
        path += "/" + plus;

    rebuildUrl();
    Debug_printv("url[%s]", url.c_str());
    return MFSOwner::File( url );
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
        esp_spiffs_info("storage", &total, &used);
#elif FLASH_LITTLEFS
        esp_littlefs_info("storage", &total, &used);
#endif
        size_t free = total - used;
        //Debug_printv("total[%d] used[%d] free[%d]", total, used, free);
        return free;
    }

    return 65535;
}
