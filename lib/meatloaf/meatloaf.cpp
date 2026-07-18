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
#include "meat_session.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <sstream>
#include <cerrno>
#include <map>
#include <cstdio>
#include <mutex>

#include "esp_timer.h"


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
#include "utils.h"
#include "../encoding/hash.h"

#include "MIOException.h"
#include "../../include/debug.h"
#include "../../include/global_defines.h"



// Device
#include "device/flash.h"
#include "device/sd.h"

#ifndef MIN_CONFIG
// Device
#include "device/i2c.h"

// Archive
#include "media/archive/archive.h"
#include "media/archive/ark.h"
#include "media/archive/lbr.h"
#include "media/archive/lnx.h"

// Service
#include "service/csip.h"

// Network
#include "network/ftp.h"
#include "network/sftp.h"
#include "network/tnfs.h"
#include "network/smb.h"
#include "network/nfs.h"
#include "network/afp.h"
#include "network/fsp.h"
#include "network/iscsi.h"
#endif

// Cartridge
#include "media/cartridge/crt.h"

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
#include "media/disk/dxm.h"
#include "media/disk/g64.h"
#include "media/disk/m2i.h"
#include "media/disk/nib.h"

// Hard Disk
#include "media/hd/dnp.h"
#include "media/hd/dhd.h"
#include "media/hd/hdd.h"

// Tape
#include "media/tape/tap.h"
#include "media/tape/t64.h"
#include "media/tape/tcrt.h"

// Network
#include "network/http.h"
// #include "network/ipfs.h"
// #include "network/ws.h"

// Service
#include "service/ml.h"
#include "service/mqtt.h"
#include "service/mdns.h"


#ifndef MIN_CONFIG
// Codec
#include "codec/qr.h"
#include "codec/retropixels.h"

// Data
#include "data/json.h"

// Hash
#include "hash/hash.h"

// Link
// Loader
// Parser
// Scanner
#endif

//std::unordered_map<std::string, MFile*> FileBroker::file_repo;
//std::unordered_map<std::string, std::shared_ptr<MStream>> StreamBroker::stream_repo;

CacheOptions parse_cache_fragment(const std::string& url) {
    CacheOptions flags;
    // Fast path: '#' is required for any fragment — skip full URL parse if absent.
    if (url.find('#') == std::string::npos)
        return flags;

    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->fragment.empty())
        return flags;

    std::string cacheValue = parser->fragmentValue("cache");
    if (!cacheValue.empty()) {
        cacheValue = util_tolower(cacheValue);
        if (cacheValue == "ram")
            flags.store = CACHE_RAM;
        else if (cacheValue == "sd")
            flags.store = CACHE_SD;
    }

    if (util_string_value_is_true(parser->fragmentValue("refresh")) ||
        util_string_value_is_true(parser->fragmentValue("force")))
        flags.force_refresh = true;

    return flags;
}

std::string strip_cache_fragment_from_url(const std::string& url) {
    Debug_printv("url[%s]", url.c_str());
    // Fast path: no '#' means no fragment to strip.
    if (url.find('#') == std::string::npos)
        return url;

    // Parse once — check for cache key before allocating a copy.
    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->fragmentValue("cache").empty())
        return url;

    parser->fragment.clear();
    parser->fragment.shrink_to_fit();
    return parser->rebuildUrl();
}


/********************************************************
 * MStream implementations
 ********************************************************/

// A file entry (track) in the directory this stream's url points at.
struct MStreamTrackEntry {
    std::string url;
    uint32_t size;
};

// Returns the file entries (tracks) in the directory referenced by dirUrl,
// sorted alphabetically for a stable track order. Sizes come straight from
// the directory listing metadata, so callers can compute sector counts
// without opening any of the files.
static std::vector<MStreamTrackEntry> mstream_track_entries(const std::string& dirUrl)
{
    std::vector<MStreamTrackEntry> entries;

    MFile* dir = MFSOwner::File(dirUrl);
    if (dir != nullptr)
    {
        if (dir->isDirectory())
        {
            dir->rewindDirectory();
            MFile* entry;
            while ((entry = dir->getNextFileInDir()) != nullptr)
            {
                if (!entry->isDirectory())
                    entries.push_back({ entry->url, entry->size });
                delete entry;
            }
        }
        delete dir;
    }

    std::sort(entries.begin(), entries.end(), [](const MStreamTrackEntry& a, const MStreamTrackEntry& b) {
        return a.url < b.url;
    });
    return entries;
}

uint16_t MStream::getTrackCount()
{
    return mstream_track_entries(_trackDirUrl.empty() ? url : _trackDirUrl).size();
}

uint16_t MStream::getSectorCount(uint16_t track)
{
    if (track < 1)
        return 0;

    auto entries = mstream_track_entries(_trackDirUrl.empty() ? url : _trackDirUrl);
    if (track > entries.size())
        return 0;

    return (entries[track - 1].size + block_size - 1) / block_size;
}

bool MStream::openTrack(uint16_t track)
{
    if (track < 1)
        return false;

    if (_trackDirUrl.empty())
        _trackDirUrl = url;

    if (_openTrack == track && isOpen())
        return true;

    auto entries = mstream_track_entries(_trackDirUrl);
    if (track > entries.size())
        return false;

    close();
    url = entries[track - 1].url;
    if (!open(std::ios_base::in | std::ios_base::out))
        return false;

    _openTrack = track;
    return true;
}

bool MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
{
    if (!openTrack(track))
        return false;

    return seek((sector * block_size) + offset);
}

bool MStream::seekSector(std::vector<uint8_t> trackSectorOffset)
{
    return seekSector(trackSectorOffset[0], trackSectorOffset[1], trackSectorOffset[2]);
}

bool MStream::seekBlock(uint64_t index, uint8_t offset)
{
    uint16_t trackCount = getTrackCount();
    if (trackCount == 0)
        return false;

    uint16_t sectorOffset = 0;
    uint16_t track = 0;

    do
    {
        track++;
        uint16_t count = getSectorCount(track);
        if (sectorOffset + count <= index)
            sectorOffset += count;
        else
            break;
    } while (track < trackCount);

    uint8_t sector = index - sectorOffset;

    return seekSector((uint8_t)track, sector, offset);
}


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
// Device
I2CMFileSystem i2cFS;

// Archive
ArchiveMFileSystem archiveFS;
ARKMFileSystem arkFS;
LBRMFileSystem lbrFS;
LNXMFileSystem lnxFS;

// Service
CSIPMFileSystem csipFS;
MQTTMFileSystem mqttFS;
MDNSMFileSystem mdnsFS;

// Network
FTPMFileSystem ftpFS;
SFTPMFileSystem sftpFS;
TNFSMFileSystem tnfsFS;
SMBMFileSystem smbFS;
NFSMFileSystem nfsFS;
AFPMFileSystem afpFS;
FSPMFileSystem fspFS;
ISCSIMFileSystem iscsiFS;
#endif

// Cartridge
//CRTMFileSystem crtFS;

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
DXMMFileSystem dxmFS;
G64MFileSystem g64FS;
M2IMFileSystem m2iFS;
NIBMFileSystem nibFS;

// Hard Disk
DNPMFileSystem dnpFS;
DHDMFileSystem dhdFS;
HDDMFileSystem hddFS;

// Tape
TAPMFileSystem tapFS;
T64MFileSystem t64FS;
TCRTMFileSystem tcrtFS;

// Network
HTTPMFileSystem httpFS;
// IPFSFileSystem ipfsFS;
// TcpFileSystem tcpFS;
//WSFileSystem wsFS;

// Service
MLMFileSystem mlFS;

#ifndef MIN_CONFIG
// Codec
QRMFileSystem qrcEncoder;
HashMFileSystem hashEncoder;
RetroPixelsMFileSystem retroPixelsEncoder;

// Data
JSONMFileSystem jsonFS;
#endif

// put all available filesystems in this array - first matching system gets the file!
// fist in list is default
std::vector<MFileSystem*> MFSOwner::availableFS { 

    // Device
    &defaultFS,     // Flash Filesystem
#ifdef SD_CARD
    &sdFS,
#endif

#ifndef MIN_CONFIG
    // Device
    &i2cFS,

    // Archive
    &archiveFS,     // extension-based FS have to be on top to be picked first, otherwise the scheme will pick them!
    &arkFS, &lbrFS, &lnxFS,
#endif

    // Cartridge
    //&crtFS,

    // Container
    &d8bFS, &dfiFS,

    // Disk
    &d64FS, &d71FS, &d80FS, &d81FS, &d82FS, &d90FS,
    &dxmFS,
    &g64FS, &m2iFS, &nibFS,

    // Hard Disk
    &dnpFS, &dhdFS, &hddFS,

    // Tape
    &tapFS, &t64FS, &tcrtFS,

    // File
//    &prgFS,         // needs to be on top to be picked first
    &p00FS,

    // Network
    &httpFS,
#ifndef MIN_CONFIG
    &ftpFS, &sftpFS, &tnfsFS, &smbFS, &nfsFS, &afpFS, &fspFS, &iscsiFS,
    //&ipfsFS, &tcpFS,
#endif

#ifndef MIN_CONFIG
    // Service
    &mdnsFS, &mqttFS,
#endif

};

std::vector<MFileSystem*> MFSOwner::availableOther {
#ifndef MIN_CONFIG
    // Service
    &csipFS,

    // Codec
    &qrcEncoder, &hashEncoder, &retroPixelsEncoder,

    // Data
    &jsonFS
#endif
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

// Directory listings call MFSOwner::File() once per entry, and each call
// probes the same directories up the tree. A short-TTL cache turns those
// repeated failed fopen() calls (a full FATFS path scan each) into map hits.
// Write paths that touch .config files call MFSOwner::invalidateConfigCache()
// so edits take effect immediately instead of after the TTL.
static std::map<std::string, std::pair<uint64_t, std::map<std::string, std::string>>> s_configCache;
static std::mutex s_configCacheMutex;

void MFSOwner::invalidateConfigCache()
{
    std::lock_guard<std::mutex> lock(s_configCacheMutex);
    s_configCache.clear();
}

// Read key=value pairs from a local .config file using raw POSIX I/O.
// Uses fopen directly to avoid recursion through MFSOwner::File().
static std::map<std::string, std::string> readLocalConfig(const std::string& dirPath)
{
    std::map<std::string, std::string> cfg;

    std::string configPath = dirPath;
    while (configPath.size() > 1 && configPath.back() == '/')
        configPath.pop_back();
    configPath += "/.config";

    const uint64_t CACHE_TTL_MS = 10000;
    uint64_t now = esp_timer_get_time() / 1000ULL;

    {
        std::lock_guard<std::mutex> lock(s_configCacheMutex);
        auto it = s_configCache.find(configPath);
        if (it != s_configCache.end() && (now - it->second.first) < CACHE_TTL_MS)
            return it->second.second;
    }

    FILE* f = fopen(configPath.c_str(), "r");
    if (!f)
    {
        std::lock_guard<std::mutex> lock(s_configCacheMutex);
        if (s_configCache.size() > 64)
            s_configCache.clear();
        s_configCache[configPath] = {now, cfg};
        return cfg;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        char* eq = strchr(line, '=');
        if (!eq || eq == line)
            continue;
        cfg[std::string(line, eq - line)] = std::string(eq + 1);
    }
    fclose(f);

    {
        std::lock_guard<std::mutex> lock(s_configCacheMutex);
        if (s_configCache.size() > 64)
            s_configCache.clear();
        s_configCache[configPath] = {now, cfg};
    }
    return cfg;
}

// Given a local POSIX path, return the parent directory path.
static std::string localParentDir(const std::string& path)
{
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    size_t slash = p.rfind('/');
    if (slash == std::string::npos || slash == 0)
        return "/";
    return p.substr(0, slash);
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
            for(auto i = availableOther.begin(); i < availableOther.end() ; i ++) {
                auto ellpsw = (*i);

                //Debug_printv("Checking symbol[%s]", ellpsw->symbol);
                if(ellpsw->handles(path)) {
                    //Debug_printv("Found other FS for symbol[%s]", ellpsw->symbol);
                    return ellpsw->getFile(path);
                }
            }
        }
    }

    //Debug_println("vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv");
    //Debug_printv("targetPath[%s]", path.c_str());

    std::vector<std::string> paths = mstr::split(path,'/');
    auto pathIterator = paths.end();
    auto begin = paths.begin();
    auto end = paths.end();

    MFile* targetFile = nullptr;
    MFileSystem *targetFileSystem = &defaultFS;
    if ( !default_fs )
    {
        //Debug_printv("Finding Target FS for path[%s]", path.c_str());
        targetFileSystem = findParentFS(begin, pathIterator);
    }

    // targetFile is for access to the file in the container stream
    auto sourcePath = mstr::joinToString(&begin, &pathIterator, "/");
    auto sourcePathInStream = mstr::joinToString(&pathIterator, &end, "/");

    end = pathIterator;
    pathIterator--;

    if( begin == pathIterator )
    {
        //Debug_printv("** LOOK UP PATH NOT NEEDED   path[%s]", path.c_str());
        targetFile = targetFileSystem->getFile(path);
        //targetFile->pathInStream = sourcePathInStream;
        targetFile->type = targetFileSystem->symbol;
        //Debug_printv( ANSI_WHITE_BOLD "targetPathInStream[%s] is in sourcePath[%s][%s]", targetFile->pathInStream.c_str(), path.c_str(), targetFileSystem->symbol);
    } 
    else
    {
        targetFile = targetFileSystem->getFile(sourcePath);
        targetFile->type = targetFileSystem->symbol;
        targetFile->pathInStream = sourcePathInStream;
        //Debug_printv( ANSI_WHITE_BOLD "targetPathInStream[%s] is in sourcePath[%s][%s]", targetFile->pathInStream.c_str(), sourcePath.c_str(), targetFileSystem->symbol);

        //Debug_printv("** LOOK UP PATH: %s", sourcePath.c_str());

        // Find the container filesystem
        MFileSystem *sourceFileSystem = &defaultFS;
        if ( !default_fs )
        {
            sourcePath = mstr::joinToString(&begin, &pathIterator, "/");
            sourcePathInStream = mstr::joinToString(&pathIterator, &end, "/");
            //Debug_printv("Finding Source FS for sourcePath[%s]", sourcePath.c_str());
            sourceFileSystem = findParentFS(begin, pathIterator);
        }

        // If the target is a root filesystem, then the sourcePathInStream is part of the sourcePath
        if( (targetFileSystem->isRootFS) && sourcePathInStream.size() )
        {
            //sourcePath += "/" + sourcePathInStream;
            //sourcePathInStream.clear();
            targetFile->sourceFile = sourceFileSystem->getFile(sourcePath);
        }
        else
        {
            // Recursively set the source file
            targetFile->sourceFile = File(sourcePath);
            targetFile->sourceFile->pathInStream = sourcePathInStream;
        }
        //Debug_printv( ANSI_RED_BOLD "sourcePath[%s] sourcePathInStream[%s]", sourcePath.c_str(), sourcePathInStream.c_str());

        targetFile->sourceFile->type = sourceFileSystem->symbol;
        targetFile->isWritable = targetFile->sourceFile->isWritable;   // This stream is writable if the container is writable
        //Debug_printv( ANSI_WHITE_BOLD "sourcePathInStream[%s] is in sourcePath[%s][%s]", sourcePathInStream.c_str(), sourcePath.c_str(), sourceFileSystem->symbol);

        // if (targetFile->sourceFile != nullptr)
        // {
        //     Debug_printv("source good rootfs[%d][%s][%s]", sourceFileSystem->isRootFS, targetFile->sourceFile->url.c_str(), targetFile->sourceFile->pathInStream.c_str());
        // }
        // else
        //     Debug_printv("source bad");
    }

    // if (targetFile != nullptr)
    // {
    //     Debug_printv("target good rootfs[%d][%s][%s]", targetFileSystem->isRootFS, targetFile->url.c_str(), targetFile->pathInStream.c_str());
    // }
    // else
    //     Debug_printv("target bad");

    // Debug_println("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");

    // .config-based URL redirect: applies to local paths only (no ://) and never to
    // .config files themselves (would cause infinite recursion reading the config).
    // Walk up the directory tree so that /sd/zimmers.net/bin inherits the redirect
    // from /sd/zimmers.net/.config even though /bin doesn't exist locally.
    if (targetFile != nullptr &&
        targetFile->name != ".config" &&
        path.find("://") == std::string::npos)
    {
        std::string currentDir = path;
        // A .config can never exist on a path segment inside a container image,
        // so start probing at the container itself. This keeps directory listings
        // from fopen()ing a unique nonexistent path for every entry.
        if (!targetFile->pathInStream.empty() && path.size() > targetFile->pathInStream.size())
            currentDir = path.substr(0, path.size() - targetFile->pathInStream.size());
        while (true) {
            auto cfg = readLocalConfig(currentDir);
            auto baseIt = cfg.find("base_url");
            if (baseIt != cfg.end() && !baseIt->second.empty()) {
                std::string base = baseIt->second;
                while (!base.empty() && base.back() == '/')
                    base.pop_back();

                // Compute the relative path from currentDir to the original path.
                std::string relPath;
                if (path.length() > currentDir.length()) {
                    relPath = path.substr(currentDir.length());
                    if (!relPath.empty() && relPath.front() == '/')
                        relPath = relPath.substr(1);
                }

                std::string remoteUrl = relPath.empty() ? base : (base + "/" + relPath);
                auto cacheIt = cfg.find("cache");
                if (cacheIt != cfg.end() && !cacheIt->second.empty())
                    remoteUrl += "#cache=" + cacheIt->second;

                delete targetFile;
                return File(remoteUrl);
            }

            // Stop at the filesystem root.
            if (currentDir.empty() || currentDir == "/" || currentDir == ".")
                break;

            std::string parent = localParentDir(currentDir);
            if (parent == currentDir)
                break;
            currentDir = parent;
        }
    }

    return targetFile;
}

MFile* MFSOwner::NewFile(std::string path) {

    auto newFile = File(path);
    if ( newFile == nullptr )
        return nullptr;

    if (newFile->exists()) {
        Debug_printv("File already exists [%s]", path.c_str());
        delete newFile;
        return nullptr;
    }

    return newFile;
}


bool MFSOwner::Copy(std::string sourcePath, std::string destPath) {
    MFile* sourceFile = File(sourcePath);
    if (sourceFile == nullptr || !sourceFile->exists()) {
        Debug_printv("Source file does not exist [%s]", sourcePath.c_str());
        return false;
    }

    MFile* destFile = NewFile(destPath);
    if (destFile == nullptr) {
        Debug_printv("Destination file already exists or cannot be created [%s]", destPath.c_str());
        return false;
    }

    auto sourceStream = sourceFile->getSourceStream(std::ios_base::in);
    if (sourceStream == nullptr || !sourceStream->isOpen()) {
        Debug_printv("Failed to open source file for reading [%s]", sourcePath.c_str());
        delete sourceFile;
        delete destFile;
        return false;
    }

    auto destStream = destFile->getSourceStream(std::ios_base::out);
    if (destStream == nullptr || !destStream->isOpen()) {
        Debug_printv("Failed to open destination file for writing [%s]", destPath.c_str());
        delete sourceFile;
        delete destFile;
        return false;
    }

    const uint32_t bufferSize = 512;
    uint8_t buffer[bufferSize];
    uint32_t bytesRead;
    while ((bytesRead = sourceStream->read(buffer, bufferSize)) > 0) {
        destStream->write(buffer, bytesRead);
    }

    delete sourceFile;
    delete destFile;
    return true;
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
                while ((ent = readdir (dir)) != NULL) {
                    // Debug_printv( "%s\r\n", ent->d_name );
                    if ( mstr::compare( ent->d_name, name ) )
                    {
                        path.reserve(p.size() + 1 + strlen(ent->d_name));
                        path = p; path += '/'; path += ent->d_name;
                        break;
                    }
                }
                closedir (dir);
            }
        }        
    }

    return path;
}

MFileSystem* MFSOwner::findParentFS(std::vector<std::string>::iterator &begin, std::vector<std::string>::iterator &pathIterator) {
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
    //Debug_printv("MFile ctor path[%s] url[%s]", path.c_str(), url.c_str());
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

std::string MFile::buildRequestUrl() const {
    std::string requestUrl = strip_cache_fragment_from_url(url);
    if (pathInStream.size()) {
        if (!mstr::endsWith(requestUrl, "/")) {
            requestUrl += "/";
        }
        requestUrl += pathInStream;
    }
    return requestUrl;
}

bool MFile::isCacheEnabled(std::ios_base::openmode mode) const {
    auto flags = parse_cache_fragment(url);
    return mode == std::ios_base::in && flags.store == CACHE_SD;
}

bool MFile::isCacheForceRefresh() const {
    return parse_cache_fragment(url).force_refresh;
}

std::shared_ptr<MStream> MFile::openStreamWithCache(
    const std::string& requestUrl,
    std::ios_base::openmode mode,
    const std::function<std::shared_ptr<MStream>(const std::string&, std::ios_base::openmode)>& opener,
    const CacheOptions* overrideFlags) {
    CacheOptions cacheFlags = overrideFlags ? *overrideFlags : parse_cache_fragment(url);

    // ---- RAM cache (no SD_CARD dependency) -----------------------------------
    if (mode == std::ios_base::in && cacheFlags.store == CACHE_RAM) {
        if (cacheFlags.force_refresh) {
            MSession::CachedFile::clearRAMCached(requestUrl);
        } else {
            auto cached = MSession::CachedFile::getRAMCached(requestUrl);
            if (cached) {
                auto cacheStream = cached->openStream(std::ios_base::in);
                if (cacheStream) {
                    size = cacheStream->size();
                    Debug_printv("RAM cache hit: %s (%u bytes)", requestUrl.c_str(), size);
                    return cacheStream;
                }
            }
        }

        Debug_printv("Fetching remote for RAM caching: %s", requestUrl.c_str());
        auto remoteStream = opener(requestUrl, mode);
        if (remoteStream != nullptr && remoteStream->isOpen()) {
            auto cf = std::make_shared<MSession::CachedFile>(0u);
            if (cf->loadFromStream(remoteStream.get(), 0)) {
                MSession::CachedFile::setRAMCached(requestUrl, cf);
                remoteStream->close();
                auto cacheStream = cf->openStream(std::ios_base::in);
                if (cacheStream) {
                    size = cacheStream->size();
                    return cacheStream;
                }
            }
            remoteStream->close();
        }
        // Fall through to direct open if RAM cache population failed
    }

#ifdef SD_CARD
    // ---- SD cache ------------------------------------------------------------
    if (mode == std::ios_base::in && cacheFlags.store == CACHE_SD) {
        std::string cacheRoot = CACHE_DIR;
        if (!host.empty()) { cacheRoot += '/'; cacheRoot += host; }
        std::string cacheDir;
        cacheDir.reserve(cacheRoot.size() + 1 + 8);
        cacheDir = cacheRoot; cacheDir += '/'; cacheDir += mstr::crc32(requestUrl);
        fnSDFAT.create_path(cacheDir.c_str());  // Ensure the directory exists
        std::string cachePath;
        cachePath.reserve(4 + cacheDir.size() + 1 + 64);
        cachePath = "/sd"; cachePath += cacheDir; cachePath += '/';
        size_t _lastSlash = requestUrl.find_last_of('/');
        cachePath += (_lastSlash != std::string::npos)
            ? requestUrl.substr(_lastSlash + 1)
            : name;

        ::PeoplesUrlParser::dump(); // Show url parts
        Debug_printv("url[%s] root[%s] dir[%s] path[%s]", url.c_str(), cacheRoot.c_str(), cacheDir.c_str(), cachePath.c_str());
        auto cf = MSession::CachedFile::forSD(cachePath);

        if (!cacheFlags.force_refresh && cf->isAllocated()) {
            auto cacheStream = cf->openStream(std::ios_base::in);
            if (cacheStream != nullptr) {
                size = cacheStream->size();
                return cacheStream;
            }
        }

        Debug_printv("Fetching remote for SD caching: %s", requestUrl.c_str());
        auto remoteStream = opener(requestUrl, mode);
        if (remoteStream != nullptr && remoteStream->isOpen()) {
            if (cf->loadFromStream(remoteStream.get(), 0)) {
                remoteStream->close();
                auto cacheStream = cf->openStream(std::ios_base::in);
                if (cacheStream != nullptr) {
                    size = cacheStream->size();
                    Debug_printv("Returning SD cached stream: %s", cachePath.c_str());
                    return cacheStream;
                }
            }
            remoteStream->close();
        }
    }
#else
    __IGNORE_UNUSED_VAR(requestUrl);
    __IGNORE_UNUSED_VAR(mode);
    __IGNORE_UNUSED_VAR(opener);
#endif

    // ---- No caching / fallback -----------------------------------------------
    Debug_printv("Opening without cache: %s", requestUrl.c_str());
    auto stream = opener(requestUrl, mode);
    if (stream != nullptr) {
        size = stream->size();
    }
    return stream;
}

std::shared_ptr<MStream> MFile::getSourceStream(std::ios_base::openmode mode) {

    if ( sourceFile == nullptr )
    {
        //Debug_printv("null sourceFile for path[%s]", path.c_str());
        return nullptr;
    }

    // has to return OPENED stream
    //Debug_printv( ANSI_CYAN_BOLD_HIGH_INTENSITY "sourceFile[%s] pathInStream[%s]", sourceFile->url.c_str(), pathInStream.c_str());

    // Writing a file INSIDE a container (e.g. saving into a .d64): the
    // container itself must be opened read-write, never write-only —
    // plain 'out' would truncate the image file on flash/SD.
    auto containerMode = mode;
    if ( pathInStream.size() > 0 && (mode & std::ios_base::out) )
        containerMode = std::ios_base::in | std::ios_base::out;

    auto sourceStream = sourceFile->getSourceStream(containerMode);
    if ( sourceStream == nullptr )
    {
        //Debug_printv("null sourceStream for path[%s]", path.c_str());
        return nullptr;
    }

    // will be replaced by streamBroker->getSourceStream(sourceFile, mode)
    std::shared_ptr<MStream> containerStream(sourceStream); // get its base stream, i.e. zip raw file contents
    //Debug_printv("containerStream isRandomAccess[%d] isBrowsable[%d] null[%s]", containerStream->isRandomAccess(), containerStream->isBrowsable(), (containerStream == nullptr) ? "null" : "good");
    //Debug_printv("containerStream url[%s]", containerStream->url.c_str());

    // will be replaced by streamBroker->getDecodedStream(this, mode, containerStream)
    std::shared_ptr<MStream> decodedStream(getDecodedStream(containerStream)); // wrap this stream into decoded stream, i.e. unpacked zip files
    //Debug_printv("decodedStream isRandomAccess[%d] isBrowsable[%d] null[%s]", decodedStream->isRandomAccess(), decodedStream->isBrowsable(), (decodedStream == nullptr) ? "null" : "good");
    //Debug_printv("decodedStream url[%s]", decodedStream->url.c_str());

    // Media decoder streams never set their mode themselves; seekPath needs it
    // to decide between read-existing and create-new, and the drive checks it
    // to know when to flush buffered write data.
    decodedStream->mode = mode;


    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if(decodedStream->isRandomAccess() && pathInStream.size() > 0)
    {
        // For files with a browsable random access directory structure
        // d64, d74, d81, dnp, etc.
        bool foundIt = decodedStream->seekPath(pathInStream);

        if(!foundIt)
        {
            //Debug_printv("path in stream not found");
            return nullptr;
        }        
        //decodedStream->url += "/" + pathInStream;
    }
    else if(decodedStream->isBrowsable() && pathInStream.size() > 0)
    {
        // For files with no directory structure
        // tap, crt, tar
        auto pointedFile = decodedStream->seekNextEntry();

        while (!pointedFile.empty())
        {
            if(mstr::compare(pointedFile, pathInStream))
            {
                //Debug_printv("returning decodedStream 1 [%s]", decodedStream->url.c_str());
                return decodedStream;
            }

            pointedFile = decodedStream->seekNextEntry();
        }
        //Debug_printv("path in stream not found!");
        if(pointedFile.empty())
            return nullptr;
    }

    //Debug_printv("returning decodedStream 2 [%s][%s]", decodedStream->url.c_str(), pathInStream.c_str());
    return decodedStream;
};


MFile* MFile::cd(std::string newDir) 
{
    Debug_printv("url[%s] cd[%s] hex[%s]", url.c_str(), newDir.c_str(), mstr::toHex(newDir).c_str());

    if(newDir.find(':') != std::string::npos) 
    {
        // // Check if this is a wrapper scheme with a relative path
        // size_t firstColon = newDir.find(':');
        // std::string wrapperScheme = newDir.substr(0, firstColon + 1);  // e.g., "retropixels:"
        // std::string wrappedPath = newDir.substr(firstColon + 1);
        
        // // If the wrapped path doesn't contain a scheme and isn't absolute, it's relative
        // if(wrappedPath.find(':') == std::string::npos && 
        //    !wrappedPath.empty() && 
        //    wrappedPath[0] != '/') 
        // {
        //     // Prepend current directory to make it absolute
        //     std::string currentPath = url;
        //     if(!mstr::endsWith(currentPath, "/")) {
        //         currentPath.push_back('/');
        //     }
        //     newDir = wrapperScheme + currentPath + wrappedPath;
        //     Debug_printv("Adjusted wrapper path: [%s]", newDir.c_str());
        // }
        
        // I can only guess we're CDing into another url scheme, this means we're changing whole path
        return MFSOwner::File(newDir);
    }
    else if(newDir[0]=='_') // {CBM LEFT ARROW}
    {
        // Special case: if we're at filesystem root (path.empty()) and target starts with '_',
        // treat it as navigation within current filesystem, not "go up"
        if (path.size() == 1 && newDir.size() > 1) {
            // At filesystem root, navigate to subdirectory
            std::string newPath = url;
            // For MDNS and similar filesystems, don't add extra '/' at root
            if (mstr::startsWith(url, "mdns://")) {
                newPath += newDir; // Keep the '_'
            } else {
                if (!mstr::endsWith(newPath, "/")) {
                    newPath += "/";
                }
                newPath += newDir; // Keep the '_'
            }
            if (!mstr::endsWith(newPath, "/")) {
                newPath += "/";
            }
            return MFSOwner::File(newPath);
        }
        
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
        //Debug_printv("url[%s] newDir[%s]", url.c_str(), newDir.c_str());
        if (newDir[0] == '/' && !url.empty() && url[0] == '/') {
            // Absolute local path; don't append to current URL
            return MFSOwner::File(newDir);
        }
        if ( newDir[0]=='/' )
            newDir = mstr::drop(newDir,1);

        // Add new directory to path
        if ( !mstr::endsWith(url, "/") && newDir.size() )
            url.push_back('/');

        // Network Explorer
        if ( url == "/" && newDir == "network") {
            url = "mdns://";
            newDir = "";
        }

        // Add new directory to path
        //Debug_printv("url[%s] newDir[%s]", url.c_str(), newDir.c_str());
        MFile* newPath = MFSOwner::File(url + newDir);
        if (newPath == nullptr) {
            return nullptr;
        }

#ifdef DEBUG
        Debug_printv("url[%s][%s]", newPath->url.c_str(), newPath->pathInStream.c_str());
        newPath->dump();
#endif

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

        // Strip leading / from plus, then consume any _ (CBM ←) characters as
        // additional "go up" steps — resolves _/_/dir by walking the path string
        // directly rather than creating intermediate MFile objects.
        if (!plus.empty() && plus[0] == '/')
            plus = mstr::drop(plus, 1);

        while (!plus.empty() && plus[0] == '_') {
            plus = mstr::drop(plus, 1);          // consume the _
            if (!plus.empty() && plus[0] == '/')
                plus = mstr::drop(plus, 1);      // consume the separator
            // go up one more level in newDir
            auto ls = newDir.rfind('/');
            if (ls == std::string::npos || ls == 0) {
                newDir = "";
                break;
            }
            newDir = newDir.substr(0, ls);
        }

        if (!plus.empty()) { newDir += '/'; newDir += plus; }

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
    if(sourceFile != nullptr && parent.length()-sourceFile->path.length()>1)
        parent = sourceFile->path;

    if (!plus.empty()) { parent += '/'; parent += plus; }

    path = parent;
    rebuildUrl();

    return MFSOwner::File(url);
};

MFile* MFile::cdRoot(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());
    if (plus.empty() || plus[0] != '/') { plus.insert(plus.begin(), '/'); }

    return MFSOwner::File( plus, true );
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
    if (!plus.empty()) { path += '/'; path += plus; }

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
    if (scheme.empty()) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0)
            return true;

        // A file nested inside a container (e.g. a .d64/.tcrt/.t64 entry inside
        // a .zip) has no physical path of its own, so stat() fails. It was
        // resolved through a container source (sourceFile != nullptr); defer to
        // whether that container exists. Plain flash/SD files have no sourceFile
        // and correctly return false here. The actual entry is validated later
        // when the stream is opened via seekPath()/seekNextEntry().
        if (sourceFile != nullptr && sourceFile != this)
            return sourceFile->exists();

        return false;
    }
    //return _exists;
    return true; // Assume it exists; if it doesn't, we'll find out when we try to open it
};

uint64_t MFile::getAvailableSpace()
{
    //Debug_printv("url[%s]", url.c_str());
    if ( mstr::startsWith(url, (char *)"/sd") )
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
