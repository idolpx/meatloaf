// Implementations for FTPMFile and FTPMStream

#include "ftp.h"

#include <errno.h>

#include "../../FileSystem/fnFile.h"
#include "../../FileSystem/fnFsFTP.h"

// Define the stream impl struct
struct FTPMStream_impl_access {
    std::unique_ptr<FileSystemFTP> fs;
    FILE* fh = nullptr;  // Use FILE* instead of FileHandler*
};

// FTPMFile implementations

// bool FTPMFile::pathValid(std::string apath) {
//     if (apath.empty()) return false;
//     if (apath == "/") return true;
//     FileSystemFTP* fs = getFS();
//     if (!fs) return false;
//     return fs->exists(apath.c_str());
// }

void FTPMFile::openDir(std::string apath) {
    FileSystemFTP* fs = getFS();
    if (!fs) return;
    if (apath.empty()) apath = path.empty() ? "/" : path;
    dirOpened = fs->dir_open(apath.c_str(), "", DIR_OPTION_UNSORTED);
}

void FTPMFile::closeDir() {
    FileSystemFTP* fs = getFS();
    if (!fs) return;
    fs->dir_close();
    dirOpened = false;
}

bool FTPMFile::isDirectory() {
    if (is_dir > -1) return is_dir;
    if (path.empty() || path == "/") return true;
    FileSystemFTP* fs = getFS();
    if (!fs) return false;
    return fs->is_dir(path.c_str());
}

std::shared_ptr<MStream> FTPMFile::getSourceStream(
    std::ios_base::openmode mode) {
    if (pathInStream.size()) url += "/" + pathInStream;

    std::shared_ptr<MStream> s = std::make_shared<FTPMStream>(url);
    s->open(mode);
    return s;
}

std::shared_ptr<MStream> FTPMFile::getDecodedStream(
    std::shared_ptr<MStream> src) {
    return src;
}

std::shared_ptr<MStream> FTPMFile::createStream(std::ios_base::openmode mode) {
    auto s = std::make_shared<FTPMStream>(url);
    s->open(mode);
    return s;
}

time_t FTPMFile::getLastWrite() { return 0; }
time_t FTPMFile::getCreationTime() { return 0; }

bool FTPMFile::rewindDirectory() {
    openDir(path);
    return dirOpened;
}

MFile* FTPMFile::getNextFileInDir() {
    FileSystemFTP* fs = getFS();
    if (!fs) return nullptr;
    if (!dirOpened) rewindDirectory();
    fsdir_entry_t* de = fs->dir_read();
    if (!de) return nullptr;
    if (de->filename[0] == '.') return getNextFileInDir();
    std::string full = url;
    if (!mstr::endsWith(full, "/")) full += "/";
    full += de->filename;
    auto file = new FTPMFile(full);
    file->extension = std::string(" ") + file->extension;
    //file->size = de->isDir ? 0 : de->size;
    file->size = de->size;
    file->is_dir = de->isDir;
    return file;
}

bool FTPMFile::mkDir() { return false; }

bool FTPMFile::exists() {
    FileSystemFTP* fs = getFS();
    if (!fs) return false;
    if (path.empty() || path == "/") return true;
    return fs->exists(path.c_str());
}

bool FTPMFile::remove() {
    FileSystemFTP* fs = getFS();
    if (!fs) return false;
    return fs->remove(path.c_str());
}

bool FTPMFile::rename(std::string dest) {
    FileSystemFTP* fs = getFS();
    if (!fs) return false;
    return fs->rename(path.c_str(), dest.c_str());
}

bool FTPMFile::readEntry(std::string filename) {
    FileSystemFTP* fs = getFS();
    if (!fs) return false;
    std::string searchPath = pathToFile();
    if (searchPath.empty()) searchPath = "/";
    if (!fs->dir_open(searchPath.c_str(), "", 0)) {
        fsdir_entry_t* de;
        while ((de = fs->dir_read()) != nullptr) {
            std::string en = de->filename;
            if (en == filename || filename == "*") {
                name = en;
                rebuildUrl();
                return true;
            }
        }
    }
    return false;
}

// FTPMStream implementations

bool FTPMStream::isOpen() { return _impl != nullptr && _impl->fh != nullptr; }

bool FTPMStream::open(std::ios_base::openmode mode) {
    Debug_printv("Opening FTP stream url[%s] mode[%d]", url.c_str(), mode);

    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->scheme != "ftp") {
        Debug_printv("Invalid FTP URL: %s", url.c_str());
        _error = EINVAL;
        return false;
    }

    _impl = new FTPMStream_impl_access();
    _impl->fs = std::make_unique<FileSystemFTP>();

    std::string base = std::string("ftp://") + parser->host;
    if (!parser->port.empty()) base += ":" + parser->port;

    if (!_impl->fs->start(
            base.c_str(), parser->user.empty() ? nullptr : parser->user.c_str(),
            parser->password.empty() ? nullptr : parser->password.c_str())) {
        Debug_printv("Failed to login to FTP: %s", parser->host.c_str());
        _error = ECONNREFUSED;
        delete _impl;
        _impl = nullptr;
        return false;
    }

    const char* mstr = FILE_READ;
    if (mode & std::ios_base::out) {
        if (mode & std::ios_base::app)
            mstr = FILE_APPEND;
        else
            mstr = FILE_WRITE;
    }

    _impl->fh = _impl->fs->file_open(parser->path.c_str(), mstr);
    if (!_impl->fh) {
        Debug_printv("Failed to open FTP file for %s", parser->path.c_str());
        _error = ENOENT;
        delete _impl;
        _impl = nullptr;
        return false;
    }

    // For FTP, we don't know the file size without additional STAT calls
    // Use FileSystem::filesize() to get the size from FILE*
    long fsz = FileSystem::filesize(_impl->fh);
    _size = fsz > 0 ? (uint32_t)fsz : 0;
    _position = 0;
    return true;
}

void FTPMStream::close() {
    if (!_impl) return;
    if (_impl->fh) {
        fclose(_impl->fh);
        _impl->fh = nullptr;
    }
    _impl->fs.reset();
    delete _impl;
    _impl = nullptr;
}

uint32_t FTPMStream::read(uint8_t* buf, uint32_t size) {
    if (!_impl || !_impl->fh || !buf) {
        _error = EBADF;
        return 0;
    }
    size_t rd = fread(buf, 1, size, _impl->fh);
    _position += rd;
    return (uint32_t)rd;
}

uint32_t FTPMStream::write(const uint8_t* buf, uint32_t size) {
    if (!_impl || !_impl->fh || !buf) {
        _error = EBADF;
        return 0;
    }
    size_t wr = fwrite(buf, 1, size, _impl->fh);
    _position += wr;
    if (_position > _size) _size = _position;
    return (uint32_t)wr;
}

bool FTPMStream::seek(uint32_t pos) { return seek(pos, SEEK_SET); }

bool FTPMStream::seek(uint32_t pos, int mode) {
    if (!_impl || !_impl->fh) {
        _error = EBADF;
        return false;
    }
    int whence = SEEK_SET;
    if (mode == SEEK_CUR)
        whence = SEEK_CUR;
    else if (mode == SEEK_END)
        whence = SEEK_END;
    int res = fseek(_impl->fh, (long)pos, whence);
    if (res != 0) {
        _error = EIO;
        return false;
    }
    long t = ftell(_impl->fh);
    if (t >= 0) _position = (uint32_t)t;
    return true;
}
