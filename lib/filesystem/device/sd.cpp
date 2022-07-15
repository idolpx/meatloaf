#include "sd.h"

#include "../../../include/debug.h"

#include "fnFsSD.h"

#include <sys/stat.h>
#include <unistd.h>


/********************************************************
 * MFile implementations
 ********************************************************/

bool SDFile::isDirectory()
{
    if(path=="/" || path=="")
        return true;

    return _filesystem.is_dir( path.c_str() );
}


MIStream* SDFile::inputStream()
{
    MIStream* istream = new SDIStream( path );
    istream->open();   
    return istream;
}

MOStream* SDFile::outputStream()
{
    MOStream* ostream = new SDOStream( path );
    ostream->open();   
    return ostream;
}

time_t SDFile::getLastWrite()
{
    struct stat info;
    stat( path.c_str(), &info);

    time_t ftime = info.st_mtime; // Time of last modification
    return ftime;
}

time_t SDFile::getCreationTime()
{
    struct stat info;
    stat( path.c_str(), &info);

    time_t ftime = info.st_ctime; // Time of last status change
    return ftime;
}

bool SDFile::mkDir()
{
    if (m_isNull) {
        return false;
    }
    int rc = mkdir( path.c_str(), ALLPERMS );
    return (rc==0);
}

bool SDFile::exists()
{
    if (m_isNull) {
        return false;
    }
    if (path=="/" || path=="") {
        return true;
    }

    return _filesystem.exists( path.c_str() );
}

size_t SDFile::size() {
    if(m_isNull || path=="/" || path=="")
        return 0;
    else if(isDirectory()) {
        return 0;
    }
    //Debug_printv("path[%s]", path.c_str());
    return _filesystem.filesize( path.c_str() );
}

bool SDFile::remove() {
    // musi obslugiwac usuwanie plikow i katalogow!
    if(path.empty())
        return false;

    return _filesystem.remove( path.c_str() );
}

bool SDFile::rename(std::string pathTo) {
    if(pathTo.empty())
        return false;

    return _filesystem.rename( path.c_str(), pathTo.c_str() );
}

void SDFile::openDir(std::string apath) 
{
    if (!isDirectory()) { 
        dirOpened = false;
        return;
    }
    
    if (_filesystem.dir_open( path.c_str(), "*", 0 ))
        dirOpened = true;

}

void SDFile::closeDir() 
{
    if(dirOpened) {
        _filesystem.dir_close();
        dirOpened = false;
    }
}

bool SDFile::rewindDirectory()
{
    media_blocks_free = 0;
    return _filesystem.dir_seek(0);
}


MFile* SDFile::getNextFileInDir()
{
    if( !dirOpened ) {
        openDir(path.c_str());
    }

    fsdir_entry* direntry;
    if((direntry = _filesystem.dir_read()) != NULL)
    {
        return new SDFile(this->path + ((this->path == "/") ? "" : "/") + std::string(direntry->filename));
    }
    else
    {
        _filesystem.dir_close();
        return nullptr;
    }
}