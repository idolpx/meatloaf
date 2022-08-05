#include "flash.h"

#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>


/********************************************************
 * MFileSystem implementations
 ********************************************************/

bool FlashFileSystem::handles(std::string apath) 
{
    return true; // fallback fs, so it must be last on FS list
}

MFile* FlashFileSystem::getFile(std::string apath)
{
    return new FlashFile(apath);
}


/********************************************************
 * MFile implementations
 ********************************************************/

bool FlashFile::pathValid(std::string path) 
{
    auto apath = std::string(basepath + path).c_str();
    while (*apath) {
        const char *slash = strchr(apath, '/');
        if (!slash) {
            if (strlen(apath) >= FILENAME_MAX) {
                // Terminal filename is too long
                return false;
            }
            break;
        }
        if ((slash - apath) >= FILENAME_MAX) {
            // This subdir name too long
            return false;
        }
        apath = slash + 1;
    }

    return true;
}

bool FlashFile::isDirectory()
{
    if(path=="/" || path=="")
        return true;

    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);
    return S_ISDIR(info.st_mode);
}

MIStream* FlashFile::createIStream(std::shared_ptr<MIStream> is) {
    return is.get(); // we don't have to process this stream in any way, just return the original stream
}

MIStream* FlashFile::inputStream()
{
    std::string full_path = basepath + path;
    MIStream* istream = new FlashIStream(full_path);
    Debug_printv("FlashFile::inputStream() 3, not null=%d", istream != nullptr);
    istream->open();   
    Debug_printv("FlashFile::inputStream() 4");
    return istream;
}

MOStream* FlashFile::outputStream()
{
    std::string full_path = basepath + path;
    MOStream* ostream = new FlashOStream(full_path);
    ostream->open();   
    return ostream;
}

time_t FlashFile::getLastWrite()
{
    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);

    time_t ftime = info.st_mtime; // Time of last modification
    return ftime;
}

time_t FlashFile::getCreationTime()
{
    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);

    time_t ftime = info.st_ctime; // Time of last status change
    return ftime;
}

bool FlashFile::mkDir()
{
    if (m_isNull) {
        return false;
    }
    int rc = mkdir(std::string(basepath + path).c_str(), ALLPERMS);
    return (rc==0);
}

bool FlashFile::exists()
{
    if (m_isNull) {
        return false;
    }
    if (path=="/" || path=="") {
        return true;
    }

    Debug_printv( "basepath[%s] path[%s]", basepath.c_str(), path.c_str() );

    struct stat st;
    int i = stat(std::string(basepath + path).c_str(), &st);

    return (i == 0);
}

size_t FlashFile::size() {
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

bool FlashFile::remove() {
    // musi obslugiwac usuwanie plikow i katalogow!
    if(path.empty())
        return false;

    int rc = ::remove( std::string(basepath + path).c_str() );
    if (rc != 0) {
        Debug_printv("remove: rc=%d path=`%s`\n", rc, path);
        return false;
    }

    return true;
}


bool FlashFile::rename(std::string pathTo) {
    if(pathTo.empty())
        return false;

    int rc = ::rename( std::string(basepath + path).c_str(), std::string(basepath + pathTo).c_str() );
    if (rc != 0) {
        return false;
    }
    return true;
}


void FlashFile::openDir(std::string apath) 
{
    if (!isDirectory()) { 
        dirOpened = false;
        return;
    }
    
    // Debug_printv("path[%s]", apath.c_str());
    if(apath.empty()) {
        dir = opendir( "/" );
    }
    else {
        dir = opendir( apath.c_str() );
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


void FlashFile::closeDir() 
{
    if(dirOpened) {
        closedir( dir );
        dirOpened = false;
    }
}


bool FlashFile::rewindDirectory()
{
    _valid = false;
    rewinddir( dir );

    // // Skip the . and .. entries
    // struct dirent* dirent = NULL;
    // dirent = readdir( dir );
    // dirent = readdir( dir );

    return (dir != NULL) ? true: false;
}


MFile* FlashFile::getNextFileInDir()
{
    // Debug_printv("base[%s] path[%s]", basepath.c_str(), path.c_str());
    if(!dirOpened)
        openDir(std::string(basepath + path).c_str());

    if(dir == nullptr)
        return nullptr;

    // Debug_printv("before readdir(), dir not null:%d", dir != nullptr);
    struct dirent* dirent = NULL;
    if((dirent = readdir( dir )) != NULL)
    {
        // Debug_printv("path[%s] name[%s]", this->path, dirent->d_name);
        return new FlashFile(this->path + ((this->path == "/") ? "" : "/") + std::string(dirent->d_name));
    }
    else
    {
        closeDir();
        return nullptr;
    }
}





/********************************************************
 * MOStreams implementations
 ********************************************************/
// MStream methods

bool FlashOStream::open() {
    Debug_printv("Ostream: trying to open flash fs, calling isOpen");
    if(!isOpen()) {
        handle->obtain(localPath, "w+");
    }
    return isOpen();
};

void FlashOStream::close() {
    if(isOpen()) {
        handle->dispose();
    }
};

size_t FlashOStream::write(const uint8_t *buf, size_t size) {
    if (!isOpen() || !buf) {
        return 0;
    }

    Serial.printf("in byteWrite '%c', handle->file_h is null=[%d]\n", buf[0], handle->file_h == nullptr);

    // buffer, element size, count, handle
    int result = fwrite((void*) buf, 1, size, handle->file_h );

    Debug_printv("after lfs_file_write");

    if (result < 0) {
        Debug_printv("write rc=%d\n", result);
    }
    return result;
};

bool FlashOStream::isOpen() {
    Debug_printv("in isOpen, handle not null=%d", handle != nullptr);
    
    return handle != nullptr && handle->file_h != nullptr;
}


size_t FlashOStream::size() {
    return _size;
};

size_t FlashOStream::available() {
    if(!isOpen()) return 0;
    return _size - position();
};


size_t FlashOStream::position() {
    if(!isOpen()) return 0;
    return ftell(handle->file_h);
};

bool FlashOStream::seek(size_t pos) {
    // Debug_printv("pos[%d]", pos);
        if (!isOpen()) {
        Debug_printv("Not open");
        return false;
    }
    return ( fseek( handle->file_h, pos, SEEK_SET ) ) ? true : false;
};

bool FlashOStream::seek(size_t pos, int mode) {
    // Debug_printv("pos[%d] mode[%d]", pos, mode);
    if (!isOpen()) {
        Debug_printv("Not open");
        return false;
    }
    return ( fseek( handle->file_h, pos, mode ) ) ? true: false;
}


/********************************************************
 * MIStreams implementations
 ********************************************************/


bool FlashIStream::open() {
    if(isOpen())
        return true;

    Debug_printv("IStream: trying to open flash fs, calling isOpen");

    Debug_printv("IStream: wasn't open, calling obtain");
    handle->obtain(localPath, "r");

    if(isOpen()) {
        Debug_printv("IStream: past obtain");
        // Set file size
        fseek(handle->file_h, 0, SEEK_END);
        Debug_printv("IStream: past fseek 1");
        _size = ftell(handle->file_h);
        Debug_printv("IStream: past ftell");
        fseek(handle->file_h, 0, SEEK_SET);
        Debug_printv("IStream: past fseek 2");
        return true;
    }
    return false;
};

void FlashIStream::close() {
    if(isOpen()) handle->dispose();
};

size_t FlashIStream::read(uint8_t* buf, size_t size) {
    if (!isOpen() || !buf) {
        Debug_printv("Not open");
        return 0;
    }
    
    int result = fread((void*) buf, 1, size, handle->file_h );
    if (result < 0) {
        Debug_printv("read rc=%d\n", result);
        return 0;
    }

    return result;
};


size_t FlashIStream::size() {
    return _size;
};

size_t FlashIStream::available() {
    if(!isOpen()) return 0;
    return _size - position();
};


size_t FlashIStream::position() {
    if(!isOpen()) return 0;
    return ftell(handle->file_h);
};

bool FlashIStream::seek(size_t pos) {
    // Debug_printv("pos[%d]", pos);
        if (!isOpen()) {
        Debug_printv("Not open");
        return false;
    }
    return ( fseek( handle->file_h, pos, SEEK_SET ) ) ? true : false;
};

bool FlashIStream::seek(size_t pos, int mode) {
    // Debug_printv("pos[%d] mode[%d]", pos, mode);
    if (!isOpen()) {
        Debug_printv("Not open");
        return false;
    }
    return ( fseek( handle->file_h, pos, mode ) ) ? true: false;
}

bool FlashIStream::isOpen() {
    // Debug_printv("Inside isOpen, handle notnull:%d", handle != nullptr);
    auto temp = handle != nullptr && handle->file_h != nullptr;
    // Debug_printv("returning");
    return temp;
}

/********************************************************
 * FlashHandle implementations
 ********************************************************/


FlashHandle::~FlashHandle() {
    dispose();
}

void FlashHandle::dispose() {
    //Debug_printv("file_h[%d]", file_h);
    if (file_h != nullptr) {

        fclose( file_h );
        file_h = nullptr;
        // rc = -255;
    }
}

void FlashHandle::obtain(std::string m_path, std::string mode) {

    //Serial.printf("*** Atempting opening flash  handle'%s'\n", m_path.c_str());

    if ((mode[0] == 'w') && strchr(m_path.c_str(), '/')) {
        // For file creation, silently make subdirs as needed.  If any fail,
        // it will be caught by the real file open later on

        char *pathStr = new char[m_path.length()];
        strncpy(pathStr, m_path.data(), m_path.length());

        if (pathStr) {
            // Make dirs up to the final fnamepart
            char *ptr = strchr(pathStr, '/');
            while (ptr) {
                *ptr = 0;
                mkdir(pathStr, ALLPERMS);
                *ptr = '/';
                ptr = strchr(ptr+1, '/');
            }
        }
        delete[] pathStr;
    }

    Debug_printv("m_path[%s] mode[%s]", m_path.c_str(), mode.c_str());
    file_h = fopen( m_path.c_str(), mode.c_str());
    // rc = 1;

    //Serial.printf("FSTEST: lfs_file_open file rc:%d\n",rc);

//     if (rc == LFS_ERR_ISDIR) {
//         // To support the SD.openNextFile, a null FD indicates to the FlashFSFile this is just
//         // a directory whose name we are carrying around but which cannot be read or written
//     } else if (rc == 0) {
// //        lfs_file_sync(&FlashFileSystem::lfsStruct, &file_h);
//     } else {
//         Debug_printv("FlashFile::open: unknown return code rc=%d path=`%s`\n",
//                rc, m_path.c_str());
//     }
}
