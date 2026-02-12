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

#include "fsp.h"

#include "meatloaf.h"

#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>


/********************************************************
 * MSession implementations
 ********************************************************/

FSPMSession::FSPMSession(std::string host, uint16_t port)
    : MSession(host, port)
{
    Debug_printv("FSPMSession created for %s:%d", host.c_str(), port);
    _password = ""; // Empty password by default
}

FSPMSession::~FSPMSession() {
    Debug_printv("FSPMSession destroyed for %s:%d", host.c_str(), port);
    disconnect();
}

bool FSPMSession::connect() {
    std::lock_guard<std::mutex> lock(_session_mutex);
    
    if (connected) {
        Debug_printv("Already connected to %s:%d", host.c_str(), port);
        return true;
    }

    if (_session) {
        Debug_printv("Session already exists");
        return false;
    }

    // Open FSP session
    _session.reset(fsp_open_session(host.c_str(), port, _password.c_str()));
    if (!_session) {
        Debug_printv("Failed to open FSP session to %s:%d", host.c_str(), port);
        connected = false;
        return false;
    }

    Debug_printv("Successfully opened FSP session to %s:%d", host.c_str(), port);
    connected = true;
    updateActivity();
    return true;
}

void FSPMSession::disconnect() {
    std::lock_guard<std::mutex> lock(_session_mutex);
    
    if (!connected) {
        return;
    }

    if (_session) {
        Debug_printv("Closing FSP session to %s:%d", host.c_str(), port);
        // unique_ptr deleter will call fsp_close_session() which frees the pointer
        _session.reset();
    }

    connected = false;
}

bool FSPMSession::keep_alive() {
    
    // Check if connected and session is valid
    if (!connected) {
        Debug_printv("Keep-alive skipped - not connected: %s:%d", host.c_str(), port);
        return false;
    }

    if (!_session) {
        Debug_printv("Keep-alive skipped - null session: %s:%d", host.c_str(), port);
        connected = false;  // Mark as disconnected if session is null
        return false;
    }

    // Try to acquire lock - if busy, skip keep-alive to avoid interfering with active operations
    if (!_session_mutex.try_lock()) {
        Debug_printv("Keep-alive skipped - session busy: %s:%d", host.c_str(), port);
        return true; // Return true because session is active (busy = alive)
    }
    std::lock_guard<std::mutex> lock(_session_mutex, std::adopt_lock);

    // Get raw pointer and validate before use
    FSP_SESSION* session_ptr = _session.get();
    if (!session_ptr) {
        Debug_printv("Keep-alive skipped - invalid session pointer: %s:%d", host.c_str(), port);
        connected = false;
        return false;
    }

    // Try to stat the root directory as a keep-alive operation
    struct stat sb;
    if (fsp_stat(session_ptr, "/", &sb) == 0) {
        Debug_printv("Keep-alive for %s:%d successful", host.c_str(), port);
        updateActivity();
        return true;
    }

    Debug_printv("Keep-alive failed for %s:%d", host.c_str(), port);
    return false;
}


/********************************************************
 * MFile implementations
 ********************************************************/

FSPMFile::FSPMFile(std::string path) : MFile(path) {
    // Obtain or create FSP session via SessionBroker
    uint16_t fsp_port = FSP_DEFAULT_PORT;
    if (!port.empty()) {
        fsp_port = std::stoi(port);
    }
    _session = SessionBroker::obtain<FSPMSession>(host, fsp_port);

    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain FSP session for %s:%d", host.c_str(), fsp_port);
        m_isNull = true;
        return;
    }

    // Find full filename for wildcard
    if (mstr::contains(name, "?") || mstr::contains(name, "*")) {
        readEntry(name);
    }

    if (!pathValid(this->path)) {
        m_isNull = true;
    } else {
        m_isNull = false;
    }

    //Debug_printv("FSPMFile created for %s", path.c_str());
}

FSPMFile::~FSPMFile() {
    closeDir();
    //Debug_printv("FSPMFile destroyed for %s", path.c_str());
}

std::shared_ptr<MStream> FSPMFile::getSourceStream(std::ios_base::openmode mode) {
    std::string requestUrl = buildRequestUrl();
    Debug_printv("url[%s] mode[%d]", requestUrl.c_str(), mode);
    return openStreamWithCache(
        requestUrl,
        mode,
        [](const std::string& openUrl, std::ios_base::openmode openMode) -> std::shared_ptr<MStream> {
            std::string mutableUrl = openUrl;
            auto stream = std::make_shared<FSPMStream>(mutableUrl);
            stream->open(openMode);
            return stream;
        });
}

std::shared_ptr<MStream> FSPMFile::getDecodedStream(std::shared_ptr<MStream> src) {
    return src; // FSP is already a bottom stream
}

std::shared_ptr<MStream> FSPMFile::createStream(std::ios_base::openmode mode) {
    std::shared_ptr<MStream> istream = std::make_shared<FSPMStream>(url);
    istream->open(mode);
    return istream;
}

bool FSPMFile::isDirectory() {
    if (is_dir > -1) return is_dir;
    //Debug_printv("path[%s] pathInStream[%s]", path.c_str(), pathInStream.c_str());
    if (path == "/" || path.empty()) {
        return true;
    }

    if (!pathValid(path)) {
        return false;
    }

    struct stat sb;
    if (fsp_stat(getSession(), path.c_str(), &sb) != 0) {
        return false;
    }

    return S_ISDIR(sb.st_mode);
}

time_t FSPMFile::getLastWrite() {
    if (!pathValid(path)) {
        return 0;
    }

    struct stat sb;
    if (fsp_stat(getSession(), pathInStream.c_str(), &sb) != 0) {
        return 0;
    }

    return sb.st_mtime;
}

time_t FSPMFile::getCreationTime() {
    // FSP doesn't provide creation time, return last write time
    return getLastWrite();
}

uint64_t FSPMFile::getAvailableSpace() {
    // FSP doesn't provide available space information
    return 0;
}

bool FSPMFile::rewindDirectory() {
    openDir(path);
    _current_entry = 0;
    return dirOpened;
}

MFile* FSPMFile::getNextFileInDir() {
    if (!dirOpened || !_dir_handle) {
        return nullptr;
    }

    // Read directory entries if not already cached
    if (_dir_entries.empty()) {
        FSP_RDENTRY entry;
        FSP_RDENTRY* result;

        while (fsp_readdir_native(_dir_handle, &entry, &result) == 0 && result) {
            _dir_entries.push_back(entry);
        }

        // Sort entries by name
        std::sort(_dir_entries.begin(), _dir_entries.end(), [](const FSP_RDENTRY& a, const FSP_RDENTRY& b) {
            return strcmp(a.name, b.name) < 0;
        });
    }

    // Return next entry
    if (_current_entry < _dir_entries.size()) {
        const FSP_RDENTRY& entry = _dir_entries[_current_entry];
        std::string entryName = entry.name;
        std::string fullPath = pathInStream;
        if (!fullPath.empty() && fullPath.back() != '/') {
            fullPath += "/";
        }
        fullPath += entryName;

        _current_entry++;

        // Create new FSPMFile for this entry
        std::string url = "fsp://" + host;
        if (!port.empty()) {
            url += ":" + port;
        }
        // Ensure path starts with /
        if (fullPath.empty() || fullPath[0] != '/') {
            url += "/";
        }
        url += fullPath;
        
        FSPMFile* file = new FSPMFile(url);
        
        // Set file size for regular files (not directories)
        if (entry.type != FSP_RDTYPE_DIR) {
            file->size = entry.size;
        }
        
        return file;
    }

    return nullptr;
}

bool FSPMFile::mkDir() {
    if (!pathValid(path)) {
        return false;
    }

    return fsp_mkdir(getSession(), pathInStream.c_str()) == 0;
}

bool FSPMFile::exists() {
    if (!pathValid(path)) {
        return false;
    }

    struct stat sb;
    return fsp_stat(getSession(), pathInStream.c_str(), &sb) == 0;
}

bool FSPMFile::remove() {
    if (!pathValid(path)) {
        return false;
    }

    if (isDirectory()) {
        return fsp_rmdir(getSession(), pathInStream.c_str()) == 0;
    } else {
        return fsp_unlink(getSession(), pathInStream.c_str()) == 0;
    }
}

bool FSPMFile::rename(std::string dest) {
    if (!pathValid(path)) {
        return false;
    }

    // Parse destination path
    auto destParser = PeoplesUrlParser::parseURL(dest);
    if (!destParser) {
        return false;
    }

    if (destParser->host != host || destParser->port != port) {
        // Cross-server rename not supported
        return false;
    }

    return fsp_rename(getSession(), pathInStream.c_str(), destParser->path.c_str()) == 0;
}

bool FSPMFile::readEntry(std::string filename) {
    if (filename.empty() || !pathValid(path)) {
        return false;
    }

    std::string searchPath = pathToFile();
    if (searchPath.empty()) {
        searchPath = "/";
    }

    Debug_printv("path[%s] filename[%s]", searchPath.c_str(), filename.c_str());

    // Open directory
    FSP_DIR* dir_handle = fsp_opendir(getSession(), searchPath.c_str());
    if (!dir_handle) {
        Debug_printv("Failed to open directory: %s", searchPath.c_str());
        return false;
    }

    FSP_RDENTRY entry;
    FSP_RDENTRY* result;
    bool found = false;

    while (fsp_readdir_native(dir_handle, &entry, &result) == 0 && result) {
        std::string entryFilename = entry.name;

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
        } else if (mstr::compare(entryFilename, filename)) {
            Debug_printv("Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str());
            name = entryFilename;
            rebuildUrl();
            found = true;
            break;
        }
    }

    // Close directory
    fsp_closedir(dir_handle);

    if (!found) {
        Debug_printv("Not Found! file[%s]", filename.c_str());
    }

    return found;
}

void FSPMFile::openDir(std::string path) {
    if (dirOpened) {
        closeDir();
    }

    _dir_handle = fsp_opendir(getSession(), path.c_str());
    if (_dir_handle) {
        dirOpened = true;
        _dir_entries.clear();
        _current_entry = 0;
        Debug_printv("Opened FSP directory: %s", path.c_str());
    } else {
        Debug_printv("Failed to open FSP directory: %s", path.c_str());
    }
}

void FSPMFile::closeDir() {
    if (_dir_handle) {
        fsp_closedir(_dir_handle);
        _dir_handle = nullptr;
        Debug_printv("Closed FSP directory");
    }
    dirOpened = false;
    _dir_entries.clear();
    _current_entry = 0;
}

bool FSPMFile::pathValid(std::string path) {
    return _session && _session->isConnected();
}


/********************************************************
 * MStream implementations
 ********************************************************/

void FSPMStream::setModeString(std::ios_base::openmode mode) {
    _mode_str = "";
    if (mode & std::ios_base::in) _mode_str += "r";
    if (mode & std::ios_base::out) {
        if (_mode_str.empty()) {
            _mode_str = "w";
        } else {
            _mode_str += "+"; // Read-write mode
        }
    }
    if (_mode_str.empty()) _mode_str = "r"; // Default to read
}

bool FSPMStream::isOpen() {
    return _file_handle != nullptr;
}

bool FSPMStream::open(std::ios_base::openmode mode) {
    if (isOpen()) {
        close();
    }

    _mode = mode;
    setModeString(mode);

    // Parse URL
    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->scheme != "fsp") {
        Debug_printv("Invalid FSP URL: %s", url.c_str());
        _error = EINVAL;
        return false;
    }

    // Get session
    uint16_t fsp_port = parser->port.empty() ? FSP_DEFAULT_PORT : std::stoi(parser->port);
    _session = SessionBroker::obtain<FSPMSession>(parser->host, fsp_port);
    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to get FSP session for %s:%d", parser->host.c_str(), fsp_port);
        _error = ECONNREFUSED;
        return false;
    }

    // Open FSP file
    _file_handle = fsp_fopen(_session->getSession(), parser->path.c_str(), _mode_str.c_str());
    if (!_file_handle) {
        Debug_printv("Failed to open FSP file: %s", parser->path.c_str());
        _error = ENOENT;
        return false;
    }

    // Get file size
    struct stat sb;
    if (fsp_stat(_session->getSession(), parser->path.c_str(), &sb) == 0) {
        _size = sb.st_size;
        Debug_printv("Opened FSP file: %s (size: %u)", parser->path.c_str(), _size);
    } else {
        Debug_printv("Opened FSP file: %s (size unknown)", parser->path.c_str());
        _size = 0;
    }
    
    _position = 0;
    return true;
}

void FSPMStream::close() {
    if (_file_handle) {
        fsp_fclose(_file_handle);
        _file_handle = nullptr;
        Debug_printv("Closed FSP file");
    }
    _session.reset();
    _position = 0;
    _size = 0;
}

uint32_t FSPMStream::read(uint8_t* buf, uint32_t size) {
    if (!isOpen()) {
        return 0;
    }

    size_t bytes_read = fsp_fread(buf, 1, size, _file_handle);
    _position += bytes_read;
    return bytes_read;
}

uint32_t FSPMStream::write(const uint8_t *buf, uint32_t size) {
    if (!isOpen()) {
        return 0;
    }

    size_t bytes_written = fsp_fwrite(buf, 1, size, _file_handle);
    _position += bytes_written;
    
    if (_position > _size) {
        _size = _position;
    }
    
    return bytes_written;
}

bool FSPMStream::seek(uint32_t pos) {
    return seek(pos, SEEK_SET);
}

bool FSPMStream::seek(uint32_t pos, int mode) {
    if (!isOpen()) {
        return false;
    }

    if (fsp_fseek(_file_handle, pos, mode) == 0) {
        // Update position based on seek mode
        if (mode == SEEK_SET) {
            _position = pos;
        } else if (mode == SEEK_CUR) {
            _position += pos;
        } else if (mode == SEEK_END) {
            _position = _size + pos;
        }
        return true;
    }
    
    return false;
}