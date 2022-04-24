#include "tnfs.h"

#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>


/********************************************************
 * MFile implementations
 ********************************************************/

bool TNFSFile::isDirectory()
{
    if(path=="/" || path=="")
        return true;

    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);
    return (info.st_mode == S_IFDIR) ? true: false;
}


MIStream* TNFSFile::inputStream()
{
    std::string full_path = basepath + path;
    MIStream* istream = new TNFSIStream(full_path);
    istream->open();   
    return istream;
}

MOStream* TNFSFile::outputStream()
{
    std::string full_path = basepath + path;
    MOStream* ostream = new TNFSOStream(full_path);
    ostream->open();   
    return ostream;
}

time_t TNFSFile::getLastWrite()
{
    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);

    time_t ftime = info.st_mtime; // Time of last modification
    return ftime;
}

time_t TNFSFile::getCreationTime()
{
    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);

    time_t ftime = info.st_ctime; // Time of last status change
    return ftime;
}

bool TNFSFile::mkDir()
{
    if (m_isNull) {
        return false;
    }
    int rc = mkdir(std::string(basepath + path).c_str(), ALLPERMS);
    return (rc==0);
}

bool TNFSFile::exists()
{
    if (m_isNull) {
        return false;
    }
    if (path=="/" || path=="") {
        return true;
    }

    struct stat st;
    int i = stat(std::string(basepath + path).c_str(), &st);

    return (i == 0);
}

size_t TNFSFile::size() {
    if(m_isNull || path=="/" || path=="")
        return 0;
    else if(isDirectory()) {
        return 0;
    }
    else {
        struct stat info;
        stat( std::string(basepath + path).c_str(), &info);
        return info.st_size;
    }
}

bool TNFSFile::remove() {
    // musi obslugiwac usuwanie plikow i katalogow!
    if(path.empty())
        return false;

    int rc = ::remove( std::string(basepath + path).c_str() );
    if (rc != 0) {
        Debug_printf("remove: rc=%d path=`%s`\n", rc, path);
        return false;
    }

    return true;
}

bool TNFSFile::rename(std::string pathTo) {
    if(pathTo.empty())
        return false;

    int rc = ::rename( std::string(basepath + path).c_str(), std::string(basepath + pathTo).c_str() );
    if (rc != 0) {
        return false;
    }
    return true;
}

void TNFSFile::openDir(std::string apath) 
{
    if (!isDirectory()) { 
        dirOpened = false;
        return;
    }
    
    if(apath.empty()) {
        dir = opendir( "/" );
    }
    else {
        dir = opendir( std::string(basepath + apath).c_str() );
    }

    dirOpened = true;
    if ( dir == NULL ) {
        dirOpened = false;
    }
    // else {
    //     // Skip the . and .. entries
    //     struct dirent* dirent = NULL;
    //     dirent = readdir( dir );
    //     dirent = readdir( dir );
    // }
}

