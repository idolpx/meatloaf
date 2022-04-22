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
    auto apath = path.c_str();
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
    stat( path.c_str(), &info);
    return (info.st_mode == S_IFDIR) ? true: false;
}

MIStream* FlashFile::createIStream(std::shared_ptr<MIStream> is) {
    return is.get(); // we don't have to process this stream in any way, just return the original stream
}

MIStream* FlashFile::inputStream()
{
    MIStream* istream = new FlashIStream(path);
    istream->open();   
    return istream;
}

MOStream* FlashFile::outputStream()
{
    MOStream* ostream = new FlashOStream(path);
    ostream->open();   
    return ostream;
}

time_t FlashFile::getLastWrite()
{
    struct stat info;
    stat( path.c_str(), &info);

    time_t ftime = info.st_mtime; // Time of last modification
    return ftime;
}

time_t FlashFile::getCreationTime()
{
    struct stat info;
    stat( path.c_str(), &info);

    time_t ftime = info.st_ctime; // Time of last status change
    return ftime;
}

bool FlashFile::mkDir()
{
    if (m_isNull) {
        return false;
    }
    int rc = mkdir(path.c_str(), ALLPERMS);
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

    struct stat st;
    int i = stat(path.c_str(), &st);

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
        stat( path.c_str(), &info);
        return info.st_size;
    }
}

bool FlashFile::remove() {
    // musi obslugiwac usuwanie plikow i katalogow!
    if(path.empty())
        return false;

    int rc = ::remove( path.c_str() );
    if (rc != 0) {
        Debug_printf("lfs_remove: rc=%d path=`%s`\n", rc, path);
        return false;
    }
    // Now try and remove any empty subdirs this makes, silently
    char *pathStr = new char[path.length()];
    strncpy(pathStr, path.data(), path.length());

    char *ptr = strrchr(pathStr, '/');
    while (ptr) {
        *ptr = 0;
        ::remove( pathStr ); // Don't care if fails if there are files left
        ptr = strrchr(pathStr, '/');
    }
    delete[] pathStr;

    return true;
}


bool FlashFile::rename(std::string pathTo) {
    if(pathTo.empty())
        return false;

    int rc = ::rename( path.c_str(), pathTo.c_str() );
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

    media_blocks_free = 0;
    return (dir != NULL) ? true: false;
}


MFile* FlashFile::getNextFileInDir()
{
    if(!dirOpened)
        openDir(path.c_str());

    struct dirent* dirent = NULL;
    if((dirent = readdir( dir )) != NULL)
    {
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

    int result = fwrite((void*) buf, 1, size, handle->lfsFile );

    //Serial.printf("in byteWrite '%c'\n", buf[0]);
    //Serial.println("after lfs_file_write");

    if (result < 0) {
        Debug_printf("lfs_write rc=%d\n", result);
    }
    return result;
};

bool FlashOStream::isOpen() {
    return handle->rc >= 0;
}

size_t FlashOStream::position() {
    if(!isOpen()) return 0;
    else return ftell(handle->lfsFile);
};



/********************************************************
 * MIStreams implementations
 ********************************************************/


bool FlashIStream::open() {
    if(!isOpen()) {
        handle->obtain(localPath, "r");
    }

    // Set file size
    fseek(handle->lfsFile, 0, SEEK_END);
    _size = ftell(handle->lfsFile);
    fseek(handle->lfsFile, 0, SEEK_SET);

    return isOpen();
};

void FlashIStream::close() {
    if(isOpen()) handle->dispose();
};

size_t FlashIStream::read(uint8_t* buf, size_t size) {
    if (!isOpen() || !buf) {
        Debug_printv("Not open");
        return 0;
    }
    
    int result = fread((void*) buf, 1, size, handle->lfsFile );
    if (result < 0) {
        Debug_printf("lfs_read rc=%d\n", result);
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
    return ftell(handle->lfsFile);
};

bool FlashIStream::seek(size_t pos) {
    // Debug_printv("pos[%d]", pos);
    return ( fseek( handle->lfsFile, pos, SEEK_SET ) ) ? true : false;
};

bool FlashIStream::seek(size_t pos, int mode) {
    // Debug_printv("pos[%d] mode[%d]", pos, mode);
    if (!isOpen()) {
        Debug_printv("Not open");
        return false;
    }
    return ( fseek( handle->lfsFile, pos, mode ) ) ? true: false;
}

bool FlashIStream::isOpen() {
    return handle->rc >= 0;
}

/********************************************************
 * FlashHandle implementations
 ********************************************************/


FlashHandle::~FlashHandle() {
    dispose();
}

void FlashHandle::dispose() {
    if (rc >= 0) {

        fclose( lfsFile );
        rc = -255;
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

    lfsFile = fopen( m_path.c_str(), mode.c_str());
    rc = 1;

    //Serial.printf("FSTEST: lfs_file_open file rc:%d\n",rc);

//     if (rc == LFS_ERR_ISDIR) {
//         // To support the SD.openNextFile, a null FD indicates to the FlashFSFile this is just
//         // a directory whose name we are carrying around but which cannot be read or written
//     } else if (rc == 0) {
// //        lfs_file_sync(&FlashFileSystem::lfsStruct, &lfsFile);
//     } else {
//         Debug_printf("FlashFile::open: unknown return code rc=%d path=`%s`\n",
//                rc, m_path.c_str());
//     }
}
