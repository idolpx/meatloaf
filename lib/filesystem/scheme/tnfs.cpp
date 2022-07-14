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

    return _filesystem.is_dir( path.c_str() );
}


MIStream* TNFSFile::inputStream()
{
    MIStream* istream = new TNFSIStream( path );
    istream->open();   
    return istream;
}

MOStream* TNFSFile::outputStream()
{
    MOStream* ostream = new TNFSOStream( path );
    ostream->open();   
    return ostream;
}

time_t TNFSFile::getLastWrite()
{
    struct stat info;
    stat( path.c_str(), &info);

    time_t ftime = info.st_mtime; // Time of last modification
    return ftime;
}

time_t TNFSFile::getCreationTime()
{
    struct stat info;
    stat( path.c_str(), &info);

    time_t ftime = info.st_ctime; // Time of last status change
    return ftime;
}

bool TNFSFile::mkDir()
{
    if (m_isNull) {
        return false;
    }
    int rc = mkdir( path.c_str(), ALLPERMS );
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

    return _filesystem.exists( path.c_str() );
}

size_t TNFSFile::size() {
    if(m_isNull || path=="/" || path=="")
        return 0;
    else if(isDirectory()) {
        return 0;
    }
    //Debug_printv("path[%s]", path.c_str());
    return _filesystem.filesize( path.c_str() );
}

bool TNFSFile::remove() {
    // musi obslugiwac usuwanie plikow i katalogow!
    if(path.empty())
        return false;

    return _filesystem.remove( path.c_str() );
}

bool TNFSFile::rename(std::string pathTo) {
    if(pathTo.empty())
        return false;

    return _filesystem.rename( path.c_str(), pathTo.c_str() );
}

void TNFSFile::openDir(std::string apath) 
{
    if (!isDirectory()) { 
        dirOpened = false;
        return;
    }
    
    if (_filesystem.dir_open( path.c_str(), "*", 0 ))
        dirOpened = true;

}

void TNFSFile::closeDir() 
{
    if(dirOpened) {
        _filesystem.dir_close();
        dirOpened = false;
    }
}

bool TNFSFile::rewindDirectory()
{
    media_blocks_free = 0;
    return _filesystem.dir_seek(0);
}


MFile* TNFSFile::getNextFileInDir()
{
    if( !dirOpened ) {
        openDir(path.c_str());
    }
        

    fsdir_entry* direntry;
    if((direntry = _filesystem.dir_read()) != NULL)
    {
        return new TNFSFile(this->path + ((this->path == "/") ? "" : "/") + std::string(direntry->filename));
    }
    else
    {
        _filesystem.dir_close();
        return nullptr;
    }
}