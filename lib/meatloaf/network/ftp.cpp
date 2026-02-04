// Implementations for FTPMFile and FTPMStream

#include "ftp.h"

#include <errno.h>

#include "../../FileSystem/fnFile.h"
#include "../../FileSystem/fnFsFTP.h"

// Define the stream impl struct
struct FTPMStream_impl_access {
#ifdef FNIO_IS_STDIO
    FILE* fh = nullptr;
#else
    FileHandler* fh = nullptr;
#endif
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
    file->name = de->filename;
    file->extension = std::string(" ") + file->extension;
    //file->size = de->isDir ? 0 : de->size;
    file->size = de->size;
    file->is_dir = de->isDir;
    //Debug_printv("url[%s] full[%s] filename[%s] ext[%s]", url.c_str(), full.c_str(), de->filename, file->extension.c_str());
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

    // Obtain or create FTP session via SessionBroker
    uint16_t ftp_port = parser->port.empty() ? 21 : std::stoi(parser->port);
    _session = SessionBroker::obtain<FTPMSession>(parser->host, ftp_port);

    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain FTP session for %s:%d", parser->host.c_str(), ftp_port);
        _error = ECONNREFUSED;
        return false;
    }

    _impl = new FTPMStream_impl_access();

    const char* mstr = FILE_READ;
    if (mode & std::ios_base::out) {
        if (mode & std::ios_base::app)
            mstr = FILE_APPEND;
        else
            mstr = FILE_WRITE;
    }

    // Use the shared session's filesystem
    FileSystemFTP* fs = _session->fs();
    if (!fs) {
        Debug_printv("Session has no filesystem");
        _error = ECONNREFUSED;
        delete _impl;
        _impl = nullptr;
        return false;
    }

#ifndef FNIO_IS_STDIO
    _impl->fh = fs->filehandler_open(parser->path.c_str(), mstr);
    Debug_printv("After filehandler_open");
#else
    _impl->fh = nullptr;
    Debug_printv("FTP file_open not supported - FTP needs filehandler_open");
    _error = ENOTSUP;
    delete _impl;
    _impl = nullptr;
    return false;
#endif

    if (!_impl->fh) {
        Debug_printv("Failed to open FTP file for %s", parser->path.c_str());
        _error = ENOENT;
        delete _impl;
        _impl = nullptr;
        return false;
    }

    // Get file size by seeking to end
    Debug_printv("Seeking to get file size");
    _impl->fh->seek(0, SEEK_END);
    long fsz = _impl->fh->tell();
    _impl->fh->seek(0, SEEK_SET);
    _size = fsz > 0 ? (uint32_t)fsz : 0;
    _position = 0;
    Debug_printv("FTP stream open complete, size=%u", _size);
    return true;
}

void FTPMStream::close() {
    if (!_impl) return;
    if (_impl->fh) {
#ifdef FNIO_IS_STDIO
        fclose(_impl->fh);
#else
        _impl->fh->close();
#endif
        _impl->fh = nullptr;
    }
    delete _impl;
    _impl = nullptr;
    _session.reset();
}

uint32_t FTPMStream::read(uint8_t* buf, uint32_t size) {
    if (!_impl || !_impl->fh || !buf) {
        _error = EBADF;
        return 0;
    }
#ifdef FNIO_IS_STDIO
    size_t rd = fread(buf, 1, size, _impl->fh);
#else
    size_t rd = _impl->fh->read(buf, 1, size);
#endif
    _position += rd;
    return (uint32_t)rd;
}

uint32_t FTPMStream::write(const uint8_t* buf, uint32_t size) {
    if (!_impl || !_impl->fh || !buf) {
        _error = EBADF;
        return 0;
    }
#ifdef FNIO_IS_STDIO
    size_t wr = fwrite(buf, 1, size, _impl->fh);
#else
    size_t wr = _impl->fh->write(buf, 1, size);
#endif
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
#ifdef FNIO_IS_STDIO
    int res = fseek(_impl->fh, (long)pos, whence);
    if (res != 0) {
        _error = EIO;
        return false;
    }
    long t = ftell(_impl->fh);
#else
    int res = _impl->fh->seek((long)pos, whence);
    if (res != 0) {
        _error = EIO;
        return false;
    }
    long t = _impl->fh->tell();
#endif
    if (t >= 0) _position = (uint32_t)t;
    return true;
}
