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

#include "tnfs.h"

#include "meatloaf.h"

#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>


/********************************************************
 * MSession implementations
 ********************************************************/

TNFSMSession::TNFSMSession(std::string host, uint16_t port)
    : MSession(host, port)
{
    Debug_printv("TNFSMSession created for %s:%d", host.c_str(), port);
    _mountinfo = std::make_unique<tnfsMountInfo>(host.c_str(), port);
}

TNFSMSession::~TNFSMSession() {
    Debug_printv("TNFSMSession destroyed for %s:%d", host.c_str(), port);
    disconnect();
}

bool TNFSMSession::connect() {
    if (connected) {
        Debug_printv("Already connected to %s:%d", host.c_str(), port);
        return true;
    }

    if (!_mountinfo) {
        Debug_printv("No mount info available");
        return false;
    }

    // Mount the TNFS server
    int result = tnfs_mount(_mountinfo.get());
    if (result != TNFS_RESULT_SUCCESS) {
        Debug_printv("Failed to mount TNFS server %s:%d - error %d", host.c_str(), port, result);
        connected = false;
        return false;
    }

    Debug_printv("Successfully mounted TNFS server %s:%d (session=%d)",
                 host.c_str(), port, _mountinfo->session);
    connected = true;
    updateActivity();
    return true;
}

void TNFSMSession::disconnect() {
    if (!connected) {
        return;
    }

    if (_mountinfo) {
        Debug_printv("Unmounting TNFS server %s:%d (session=%d)",
                     host.c_str(), port, _mountinfo->session);
        tnfs_umount(_mountinfo.get());
    }

    connected = false;
}

bool TNFSMSession::keep_alive() {
    if (!connected || !_mountinfo) {
        return false;
    }

    // Use getcwd as a lightweight keep-alive operation
    const char* cwd = tnfs_getcwd(_mountinfo.get());
    if (cwd != nullptr) {
        Debug_printv("Keep-alive for %s:%d (session=%d) - cwd: %s",
                     host.c_str(), port, _mountinfo->session, cwd);
        updateActivity();
        return true;
    }

    Debug_printv("Keep-alive failed for %s:%d (session=%d)",
                 host.c_str(), port, _mountinfo->session);
    connected = false;
    return false;
}


/********************************************************
 * MFile implementations
 ********************************************************/

TNFSMFile::TNFSMFile(std::string path): MFile(path) {
    //Debug_printv("url[%s] host[%s] path[%s]", url.c_str(), host.c_str(), this->path.c_str());

    // Obtain or create TNFS session via SessionBroker
    uint16_t tnfs_port = port.empty() ? TNFS_DEFAULT_PORT : std::stoi(port);
    _session = SessionBroker::obtain<TNFSMSession>(host, tnfs_port);

    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain TNFS session for %s:%d", host.c_str(), tnfs_port);
        m_isNull = true;
        return;
    }

    tnfsMountInfo* mountinfo = getMountInfo();
    if (!mountinfo) {
        Debug_printv("No mount info available");
        m_isNull = true;
        return;
    }

    // // Change to the requested directory if provided
    // if (!this->path.empty() && this->path != "/") {
    //     int result = tnfs_chdir(mountinfo, this->path.c_str());
    //     if (result != TNFS_RESULT_SUCCESS) {
    //         Debug_printv("Warning: Could not change to directory %s: error %d", this->path.c_str(), result);
    //     }
    // }

    // Find full filename for wildcard
    if (mstr::contains(name, "?") || mstr::contains(name, "*")) {
        readEntry(name);
    }

    if (!pathValid(this->path)) {
        m_isNull = true;
    } else {
        m_isNull = false;
    }

    //Debug_printv("TNFS path[%s] valid[%d]", this->path.c_str(), !m_isNull);
}

TNFSMFile::~TNFSMFile() {
    // Close any open directory handle
    closeDir();

    // Session is managed by SessionBroker, don't unmount here
    _session.reset();
}


bool TNFSMFile::pathValid(std::string path) {
    if (path.empty() || path.length() > TNFS_MAX_FILELEN) {
        return false;
    }

    // Root is always valid
    if (path == "/") {
        return true;
    }

    return getMountInfo() != nullptr;
}

bool TNFSMFile::isDirectory() {
    Debug_printv("path[%s] len[%d]", path.c_str(), path.size());

    if (path == "/" || path.empty()) {
        return true;
    }

    tnfsMountInfo* mountinfo = getMountInfo();
    if (!mountinfo) {
        return false;
    }

    tnfsStat filestat;
    int result = tnfs_stat(mountinfo, &filestat, path.c_str());
    if (result != TNFS_RESULT_SUCCESS) {
        return false;
    }

    return filestat.isDir;
}

std::shared_ptr<MStream> TNFSMFile::getSourceStream(std::ios_base::openmode mode) {

    // Add pathInStream to URL if specified
    if ( pathInStream.size() )
        url += "/" + pathInStream;

    std::shared_ptr<MStream> istream = std::make_shared<TNFSMStream>(url);
    istream->open(mode);
    return istream;
}

std::shared_ptr<MStream> TNFSMFile::getDecodedStream(std::shared_ptr<MStream> src) {
    // TNFS doesn't decode streams, just return the original
    return src;
}

std::shared_ptr<MStream> TNFSMFile::createStream(std::ios_base::openmode mode) {
    std::shared_ptr<MStream> istream = std::make_shared<TNFSMStream>(url);
    istream->open(mode);
    return istream;
}

time_t TNFSMFile::getLastWrite() {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (!mountinfo) {
        return 0;
    }

    tnfsStat filestat;
    if (tnfs_stat(mountinfo, &filestat, path.c_str()) != TNFS_RESULT_SUCCESS) {
        return 0;
    }

    return filestat.m_time;
}

time_t TNFSMFile::getCreationTime() {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (!mountinfo) {
        return 0;
    }

    tnfsStat filestat;
    if (tnfs_stat(mountinfo, &filestat, path.c_str()) != TNFS_RESULT_SUCCESS) {
        return 0;
    }

    return filestat.c_time;
}

uint64_t TNFSMFile::getAvailableSpace() {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (!mountinfo) {
        Debug_printv("mountinfo not available");
        return 0;
    }

    uint32_t free_bytes;
    if (tnfs_free(mountinfo, &free_bytes) != TNFS_RESULT_SUCCESS) {
        Debug_printv("tnfs_free failed");
        return 0;
    }

    Debug_printv("free[%u]", free_bytes);
    return free_bytes;
}

bool TNFSMFile::mkDir() {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (m_isNull || !mountinfo) {
        return false;
    }

    int result = tnfs_mkdir(mountinfo, path.c_str());
    return (result == TNFS_RESULT_SUCCESS);
}

bool TNFSMFile::exists() {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (m_isNull || !mountinfo) {
        return false;
    }

    if (path == "/" || path.empty()) {
        return true;
    }

    tnfsStat filestat;
    int result = tnfs_stat(mountinfo, &filestat, path.c_str());
    return (result == TNFS_RESULT_SUCCESS);
}

bool TNFSMFile::remove() {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (!mountinfo) {
        return false;
    }

    // Check if it's a directory or file
    if (isDirectory()) {
        return tnfs_rmdir(mountinfo, path.c_str()) == TNFS_RESULT_SUCCESS;
    } else {
        return tnfs_unlink(mountinfo, path.c_str()) == TNFS_RESULT_SUCCESS;
    }
}

bool TNFSMFile::rename(std::string pathTo) {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (pathTo.empty() || !mountinfo) {
        return false;
    }

    int result = tnfs_rename(mountinfo, path.c_str(), pathTo.c_str());
    return (result == TNFS_RESULT_SUCCESS);
}

void TNFSMFile::openDir(std::string apath) {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (!isDirectory() || !mountinfo) {
        dirOpened = false;
        return;
    }

    // Close any previously opened directory
    closeDir();

    // Open the directory for listing using shared session
    std::string dirPath = apath.empty() ? path : apath;
    int result = tnfs_opendirx(mountinfo, dirPath.c_str());
    if (result == TNFS_RESULT_SUCCESS) {
        dirOpened = true;
        _dir_handle = mountinfo->dir_handle; // Save our handle
        entry_index = 0;
    } else {
        dirOpened = false;
        _dir_handle = 0xFFFF;
    }

    Debug_printv("openDir path[%s] opened[%d] handle[%d] result[%d]",
                 dirPath.c_str(), dirOpened, _dir_handle, result);
}

void TNFSMFile::closeDir() {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (mountinfo && dirOpened && _dir_handle != 0xFFFF) {
        // Only close if the current handle in mountinfo matches ours
        // This prevents closing another TNFSMFile's directory
        if (mountinfo->dir_handle == _dir_handle) {
            tnfs_closedir(mountinfo);
            Debug_printv("Closed directory handle[%d]", _dir_handle);
        }
    }
    dirOpened = false;
    _dir_handle = 0xFFFF;
}

bool TNFSMFile::rewindDirectory() {
    // Close and reopen directory to reset position
    openDir(path);

    //Debug_printv("dirOpened[%d] entry_index[%d] path[%s]", dirOpened, entry_index, path.c_str());
    return dirOpened;
}

MFile* TNFSMFile::getNextFileInDir() {
    //Debug_printv("dirOpened[%d] entry_index[%d] path[%s]", dirOpened, entry_index, path.c_str());

    if (!dirOpened) {
        rewindDirectory();
    }

    tnfsMountInfo* mountinfo = getMountInfo();
    if (!mountinfo) {
        return nullptr;
    }

    // Verify we still have our directory handle active
    if (_dir_handle == 0xFFFF || mountinfo->dir_handle != _dir_handle) {
        Debug_printv("Directory handle mismatch - reopening");
        rewindDirectory();
        if (!dirOpened) {
            return nullptr;
        }
    }

    tnfsStat filestat;
    char entry_name[TNFS_MAX_FILELEN];

    int result = tnfs_readdirx(mountinfo, &filestat, entry_name, TNFS_MAX_FILELEN);

    if (result != TNFS_RESULT_SUCCESS) {
        closeDir();
        return nullptr;
    }

    // Skip . and .. entries
    if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
        return getNextFileInDir();
    }

    //Debug_printv("entry[%s] isDir[%d] size[%u]", entry_name, filestat.isDir, filestat.filesize);

    // Build full path
    std::string fullPath = url;
    if (!mstr::endsWith(fullPath, "/")) {
        fullPath += "/";
    }
    fullPath += entry_name;

    auto file = new TNFSMFile(fullPath);
    file->extension = " " + file->extension;
    file->size = filestat.isDir ? 0 : filestat.filesize;

    entry_index++;
    return file;
}

bool TNFSMFile::readEntry(std::string filename) {
    tnfsMountInfo* mountinfo = getMountInfo();
    if (!mountinfo || filename.empty()) {
        return false;
    }

    std::string searchPath = pathToFile();
    if (searchPath.empty()) {
        searchPath = "/";
    }

    Debug_printv("path[%s] filename[%s]", searchPath.c_str(), filename.c_str());

    // Open directory using shared session
    int result = tnfs_opendirx(mountinfo, searchPath.c_str());
    if (result != TNFS_RESULT_SUCCESS) {
        return false;
    }

    // Save our handle for this search
    uint16_t search_handle = mountinfo->dir_handle;

    tnfsStat filestat;
    char entry_name[TNFS_MAX_FILELEN];
    bool found = false;

    while ((result = tnfs_readdirx(mountinfo, &filestat, entry_name, TNFS_MAX_FILELEN)) == TNFS_RESULT_SUCCESS) {
        std::string entryFilename = entry_name;

        // Skip hidden files and directory entries
        if (entryFilename[0] == '.') {
            continue;
        }

        Debug_printv("path[%s] filename[%s] entry[%s]", searchPath.c_str(), filename.c_str(), entryFilename.c_str());

        // Check for matches
        if (filename == "*") {
            name = entryFilename;
            rebuildUrl();
            found = true;
            break;
        } else if (filename == entryFilename) {
            found = true;
            break;
        } else if (mstr::compare(filename, entryFilename)) {
            Debug_printv("Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str());
            name = entryFilename;
            rebuildUrl();
            found = true;
            break;
        }
    }

    // Close directory if we still have our handle
    if (mountinfo->dir_handle == search_handle) {
        tnfs_closedir(mountinfo);
    }

    if (!found) {
        Debug_printv("Not Found! file[%s]", filename.c_str());
    }

    return found;
}


/********************************************************
 * TNFSHandle implementations
 ********************************************************/

TNFSHandle::TNFSHandle() {
    Debug_printv("Creating TNFS handle");
    _mountinfo = std::make_unique<tnfsMountInfo>();
}

TNFSHandle::~TNFSHandle() {
    dispose();
}

void TNFSHandle::dispose() {
    if (_handle != TNFS_INVALID_HANDLE && _mountinfo) {
        tnfs_close(_mountinfo.get(), _handle);
        _handle = TNFS_INVALID_HANDLE;
    }
    if (_mountinfo) {
        tnfs_umount(_mountinfo.get());
    }
}

uint16_t TNFSHandle::mapOpenMode(std::ios_base::openmode mode) {
    uint16_t tnfs_mode = 0;

    // Map read/write modes
    if ((mode & std::ios_base::in) && (mode & std::ios_base::out)) {
        tnfs_mode = TNFS_OPENMODE_READWRITE;
    } else if (mode & std::ios_base::out) {
        tnfs_mode = TNFS_OPENMODE_WRITE;
    } else {
        tnfs_mode = TNFS_OPENMODE_READ;
    }

    // Map additional flags for write modes
    if (mode & std::ios_base::out) {
        if (mode & std::ios_base::trunc) {
            tnfs_mode |= TNFS_OPENMODE_WRITE_TRUNCATE | TNFS_OPENMODE_WRITE_CREATE;
        }
        if (mode & std::ios_base::app) {
            tnfs_mode |= TNFS_OPENMODE_WRITE_APPEND;
        }
        // Create file if it doesn't exist (unless exclusive flag would be set)
        if (!(mode & std::ios_base::trunc)) {
            tnfs_mode |= TNFS_OPENMODE_WRITE_CREATE;
        }
    }

    return tnfs_mode;
}

void TNFSHandle::obtain(std::string m_url, std::ios_base::openmode mode) {
    // Parse the URL to extract all components
    auto parser = PeoplesUrlParser::parseURL(m_url);
    if (!parser || parser->scheme != "tnfs") {
        Debug_printv("Invalid TNFS URL: %s", m_url.c_str());
        dispose();
        return;
    }

    std::string server = parser->host;
    uint16_t port = parser->port.empty() ? TNFS_DEFAULT_PORT : std::stoi(parser->port);
    std::string filepath = parser->path;  // path already includes the filename

    Debug_printv("Connecting to server[%s] port[%d] filepath[%s]", server.c_str(), port, filepath.c_str());

    // Initialize mount info
    _mountinfo = std::make_unique<tnfsMountInfo>(server.c_str(), port);

    // Mount the server
    int result = tnfs_mount(_mountinfo.get());
    if (result != TNFS_RESULT_SUCCESS) {
        Debug_printv("Failed to mount %s:%d - error %d", server.c_str(), port, result);
        dispose();
        return;
    }

    // Map open mode
    uint16_t tnfs_mode = mapOpenMode(mode);

    // Default permissions for new files
    uint16_t create_perms = TNFS_CREATEPERM_S_IRUSR | TNFS_CREATEPERM_S_IWUSR |
                            TNFS_CREATEPERM_S_IRGRP | TNFS_CREATEPERM_S_IROTH;

    Debug_printv("Opening filepath[%s] mode[0x%X] perms[0x%X]", filepath.c_str(), tnfs_mode, create_perms);

    // Open the file - use filepath (path only), not m_url (full URL with protocol)
    result = tnfs_open(_mountinfo.get(), filepath.c_str(), tnfs_mode, create_perms, &_handle);
    if (result != TNFS_RESULT_SUCCESS) {
        Debug_printv("Failed to open file %s: error %d", filepath.c_str(), result);
        dispose();
        return;
    }

    Debug_printv("Successfully opened TNFS file: %s (handle=%d)", m_url.c_str(), _handle);
}


/********************************************************
 * MStream implementations
 ********************************************************/

uint16_t TNFSMStream::mapOpenMode(std::ios_base::openmode mode) {
    uint16_t tnfs_mode = 0;

    // Map read/write modes
    if ((mode & std::ios_base::in) && (mode & std::ios_base::out)) {
        tnfs_mode = TNFS_OPENMODE_READWRITE;
    } else if (mode & std::ios_base::out) {
        tnfs_mode = TNFS_OPENMODE_WRITE;
    } else {
        tnfs_mode = TNFS_OPENMODE_READ;
    }

    // Map additional flags for write modes
    if (mode & std::ios_base::out) {
        if (mode & std::ios_base::trunc) {
            tnfs_mode |= TNFS_OPENMODE_WRITE_TRUNCATE | TNFS_OPENMODE_WRITE_CREATE;
        }
        if (mode & std::ios_base::app) {
            tnfs_mode |= TNFS_OPENMODE_WRITE_APPEND;
        }
        // Create file if it doesn't exist (unless exclusive flag would be set)
        if (!(mode & std::ios_base::trunc)) {
            tnfs_mode |= TNFS_OPENMODE_WRITE_CREATE;
        }
    }

    return tnfs_mode;
}

bool TNFSMStream::isOpen() {
    return _session && _session->isConnected() && _handle != TNFS_INVALID_HANDLE;
}

bool TNFSMStream::open(std::ios_base::openmode mode) {
    Debug_printv("Opening TNFS stream url[%s] mode[%d]", url.c_str(), mode);

    if (isOpen()) {
        Debug_printv("Stream already open");
        return true;
    }

    // Validate URL
    if (url.empty()) {
        Debug_printv("Empty URL");
        _error = EINVAL;
        return false;
    }

    // Parse URL
    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->scheme != "tnfs") {
        Debug_printv("Invalid TNFS URL: %s", url.c_str());
        _error = EINVAL;
        return false;
    }

    // Obtain or create TNFS session via SessionBroker
    uint16_t tnfs_port = parser->port.empty() ? TNFS_DEFAULT_PORT : std::stoi(parser->port);
    _session = SessionBroker::obtain<TNFSMSession>(parser->host, tnfs_port);

    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain TNFS session for %s:%d", parser->host.c_str(), tnfs_port);
        _error = ECONNREFUSED;
        return false;
    }

    // Map open mode
    uint16_t tnfs_mode = mapOpenMode(mode);

    // Default permissions for new files
    uint16_t create_perms = TNFS_CREATEPERM_S_IRUSR | TNFS_CREATEPERM_S_IWUSR |
                            TNFS_CREATEPERM_S_IRGRP | TNFS_CREATEPERM_S_IROTH;

    Debug_printv("Opening file[%s] mode[0x%X] perms[0x%X]", parser->path.c_str(), tnfs_mode, create_perms);

    // Open the file using the shared session's mountinfo
    tnfsMountInfo* mountinfo = _session->getMountInfo();
    int result = tnfs_open(mountinfo, parser->path.c_str(), tnfs_mode, create_perms, &_handle);
    if (result != TNFS_RESULT_SUCCESS) {
        Debug_printv("Failed to open file %s: error %d", parser->path.c_str(), result);
        _error = tnfs_code_to_errno(result);
        _session.reset();
        return false;
    }

    // Get file size
    tnfsStat filestat;
    if (tnfs_stat(mountinfo, &filestat, parser->path.c_str()) == TNFS_RESULT_SUCCESS) {
        _size = filestat.filesize;
        //Debug_printv("File size: %u bytes", _size);
    } else {
        Debug_printv("Warning: Could not get file size");
        _size = 0;
    }

    _position = 0;
    Debug_printv("Successfully opened TNFS file: %s (handle=%d)", url.c_str(), _handle);
    return true;
}

void TNFSMStream::close() {
    if (isOpen()) {
        tnfsMountInfo* mountinfo = _session->getMountInfo();
        if (mountinfo && _handle != TNFS_INVALID_HANDLE) {
            tnfs_close(mountinfo, _handle);
            _handle = TNFS_INVALID_HANDLE;
        }
        _session.reset();
        _position = 0;
        _size = 0;
    }
}

uint32_t TNFSMStream::read(uint8_t* buf, uint32_t size) {
    if (!buf) {
        Debug_printv("Null buffer");
        _error = EINVAL;
        return 0;
    }

    if (!isOpen()) {
        Debug_printv("Stream not open");
        _error = EBADF;
        return 0;
    }

    if (size == 0) {
        return 0;
    }

    tnfsMountInfo* mountinfo = _session->getMountInfo();
    if (!mountinfo) {
        _error = EBADF;
        return 0;
    }

    //Debug_printv("size[%u] position[%u] file[%s] url[%s]", size, _position, mountinfo->mountpath, url.c_str());

    // TNFS has a maximum read size per request
    uint32_t total_read = 0;
    while (total_read < size) {
        uint16_t chunk_size = std::min((uint32_t)TNFS_MAX_READWRITE_PAYLOAD, size - total_read);
        uint16_t bytes_read = 0;

        int result = tnfs_read(mountinfo, _handle, buf + total_read, chunk_size, &bytes_read);

        if (result == TNFS_RESULT_SUCCESS || (result == TNFS_RESULT_END_OF_FILE && bytes_read > 0)) {
            total_read += bytes_read;
            _position += bytes_read;
        } else {
            Debug_printv("TNFS read error: %d", result);
            _error = tnfs_code_to_errno(result);
            break;
        }

        // EOF or short read
        if (bytes_read < chunk_size) {
            break;
        }
    }

    return total_read;
}

uint32_t TNFSMStream::write(const uint8_t *buf, uint32_t size) {
    if (!buf) {
        Debug_printv("Null buffer");
        _error = EINVAL;
        return 0;
    }

    if (!isOpen()) {
        Debug_printv("Stream not open");
        _error = EBADF;
        return 0;
    }

    if (size == 0) {
        return 0;
    }

    tnfsMountInfo* mountinfo = _session->getMountInfo();
    if (!mountinfo) {
        _error = EBADF;
        return 0;
    }

    // TNFS has a maximum write size per request
    uint32_t total_written = 0;
    while (total_written < size) {
        uint16_t chunk_size = std::min((uint32_t)TNFS_MAX_READWRITE_PAYLOAD, size - total_written);
        uint16_t bytes_written = 0;

        int result = tnfs_write(mountinfo, _handle,
                                (uint8_t*)(buf + total_written), chunk_size, &bytes_written);

        if (result != TNFS_RESULT_SUCCESS) {
            Debug_printv("TNFS write error: %d", result);
            _error = tnfs_code_to_errno(result);
            break;
        }

        total_written += bytes_written;
        _position += bytes_written;

        if (_position > _size) {
            _size = _position;
        }

        // Short write
        if (bytes_written < chunk_size) {
            break;
        }
    }

    return total_written;
}

bool TNFSMStream::seek(uint32_t pos) {
    if (!isOpen()) {
        Debug_printv("Stream not open");
        _error = EBADF;
        return false;
    }

    tnfsMountInfo* mountinfo = _session->getMountInfo();
    if (!mountinfo) {
        _error = EBADF;
        return false;
    }

    uint32_t new_position = 0;
    // Use default TNFS caching behavior (skip_cache = false by default)
    int result = tnfs_lseek(mountinfo, _handle, pos, SEEK_SET, &new_position);

    if (result != TNFS_RESULT_SUCCESS) {
        Debug_printv("TNFS seek error: %d", result);
        _error = tnfs_code_to_errno(result);
        return false;
    }

    _position = new_position;
    //Debug_printv("Seek to pos[%u] -> new_position[%u]", pos, new_position);
    return true;
}

bool TNFSMStream::seek(uint32_t pos, int mode) {
    if (!isOpen()) {
        Debug_printv("Stream not open");
        _error = EBADF;
        return false;
    }

    tnfsMountInfo* mountinfo = _session->getMountInfo();
    if (!mountinfo) {
        _error = EBADF;
        return false;
    }

    uint32_t new_position = 0;
    // Use default TNFS caching behavior (skip_cache = false by default)
    int result = tnfs_lseek(mountinfo, _handle, pos, mode, &new_position);

    if (result != TNFS_RESULT_SUCCESS) {
        Debug_printv("TNFS seek error: %d", result);
        _error = tnfs_code_to_errno(result);
        return false;
    }

    _position = new_position;
    Debug_printv("Seek pos[%u] mode[%d] -> new_position[%u]", pos, mode, new_position);
    return true;
}
