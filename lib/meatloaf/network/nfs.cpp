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

#include "nfs.h"

#include "meatloaf.h"

#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Include mount header for export enumeration
#include "../../components/libnfs/mount/libnfs-raw-mount.h"

// Static members for export enumeration
std::vector<std::string> NFSMFile::exports;

/********************************************************
 * NFSMSession implementations
 ********************************************************/

const std::vector<std::string>& NFSMSession::getExports() {
    if (!_exports_enumerated) {
        enumerateExports();
    }
    return _exports_list;
}

void NFSMSession::enumerateExports() {
    if (!_nfs) return;
    
    _exports_list.clear();
    _exports_enumerated = true;
    
    struct exportnode *exports_ptr = NULL;
    exports_ptr = mount_getexports(host.c_str());
    
    if (!exports_ptr) {
        Debug_printv("Failed to get exports from %s", host.c_str());
        return;
    }
    
    struct exportnode *export_iter = exports_ptr;
    while (export_iter != NULL) {
        Debug_printv("Found export: %s", export_iter->ex_dir);
        _exports_list.push_back(export_iter->ex_dir);
        export_iter = export_iter->ex_next;
    }
    
    mount_free_export_list(exports_ptr);
    Debug_printv("Export enumeration complete: %lu exports cached", _exports_list.size());
}

/********************************************************
 * Helper Functions
 ********************************************************/

bool parseNFSPath(const std::string& path, std::string& export_path, std::string& file_path) {
    // Find the first directory component as the export
    size_t pos = path.find('/', 1);
    if (pos == std::string::npos) {
        export_path = path.substr(1);
        file_path = "";
    } else {
        export_path = path.substr(1, pos - 1);
        file_path = path.substr(pos + 1);
    }

    return true;
}


/********************************************************
 * MFile implementations
 ********************************************************/

bool NFSMFile::pathValid(std::string path)
{
    if (path.empty() || path.length() > PATH_MAX) {
        return false;
    }

    // Check if NFS context is available
    if (!getNFS()) {
        return false;
    }

    return true;
}

bool NFSMFile::isDirectory()
{
    if (is_dir > -1) return is_dir;
    if(file_path=="/" || file_path.empty())
        return true;

    auto nfs = getNFS();
    if (!nfs) {
        return false;
    }

    struct nfs_stat_64 st;
    if (nfs_stat64(nfs, std::string(basepath + file_path).c_str(), &st) < 0) {
        return false;
    }

    return S_ISDIR(st.nfs_mode);
}

std::shared_ptr<MStream> NFSMFile::getSourceStream(std::ios_base::openmode mode) {
    std::string requestUrl = buildRequestUrl();
    Debug_printv("url[%s] mode[%d]", requestUrl.c_str(), mode);
    return openStreamWithCache(
        requestUrl,
        mode,
        [](const std::string& openUrl, std::ios_base::openmode openMode) -> std::shared_ptr<MStream> {
            std::string mutableUrl = openUrl;
            auto stream = std::make_shared<NFSMStream>(mutableUrl);
            stream->open(openMode);
            return stream;
        });
}

std::shared_ptr<MStream> NFSMFile::getDecodedStream(std::shared_ptr<MStream> is) {
    return is; // we don't have to process this stream in any way, just return the original stream
}

std::shared_ptr<MStream> NFSMFile::createStream(std::ios_base::openmode mode)
{
    std::shared_ptr<MStream> istream = std::make_shared<NFSMStream>(url);
    istream->open(mode);
    return istream;
}

time_t NFSMFile::getLastWrite()
{
    auto nfs = getNFS();
    if (!nfs) {
        return 0;
    }

    struct nfs_stat_64 st;
    if (nfs_stat64(nfs, std::string(basepath + file_path).c_str(), &st) < 0) {
        return 0;
    }

    return st.nfs_mtime;
}

time_t NFSMFile::getCreationTime()
{
    auto nfs = getNFS();
    if (!nfs) {
        return 0;
    }

    struct nfs_stat_64 st;
    if (nfs_stat64(nfs, std::string(basepath + file_path).c_str(), &st) < 0) {
        return 0;
    }

    return st.nfs_ctime;
}

uint64_t NFSMFile::getAvailableSpace()
{
    Debug_printv("NFSMFile::getAvailableSpace() for path[%s]", file_path.c_str());
    
    // If no export is mounted, return 0
    if (export_path.empty()) {
        Debug_printv("No export mounted, returning 0");
        return 0;
    }
    
    auto nfs = getNFS();
    if (!nfs) {
        Debug_printv("NFS context not available");
        return 0;
    }

    // Get bytes free
    struct statvfs status;
    if (nfs_statvfs(nfs, file_path.c_str(), &status) != 0) {
        Debug_printv("nfs_statvfs failed: %s", nfs_get_error(nfs));
        return 0;
    }
    Debug_printv("size[%llu] blocks[%llu] free[%llu] avail[%llu]", status.f_bsize, status.f_blocks, status.f_bfree, status.f_bavail);
    return (status.f_bfree * status.f_bsize);
}

bool NFSMFile::mkDir()
{
    auto nfs = getNFS();
    if (m_isNull || !nfs) {
        return false;
    }

    int rc = nfs_mkdir(nfs, std::string(basepath + file_path).c_str());
    return (rc == 0);
}

bool NFSMFile::exists()
{
    auto nfs = getNFS();
    if (m_isNull || !nfs) {
        return false;
    }
    if (file_path=="/" || file_path=="") {
        return true;
    }

    struct nfs_stat_64 st;
    int rc = nfs_stat64(nfs, std::string(basepath + file_path).c_str(), &st);
    return (rc == 0);
}


bool NFSMFile::remove() {
    auto nfs = getNFS();
    if (!nfs) {
        return false;
    }

    // Check if it's a directory or file
    if (isDirectory()) {
        return nfs_rmdir(nfs, std::string(basepath + file_path).c_str()) == 0;
    } else {
        return nfs_unlink(nfs, std::string(basepath + file_path).c_str()) == 0;
    }
}


bool NFSMFile::rename(std::string pathTo) {
    auto nfs = getNFS();
    if(pathTo.empty() || !nfs) {
        return false;
    }

    int rc = nfs_rename(nfs, std::string(basepath + file_path).c_str(), std::string(basepath + pathTo).c_str());
    return (rc == 0);
}


void NFSMFile::openDir(std::string apath)
{
    auto nfs = getNFS();
    if (!isDirectory() || !nfs) {
        Debug_printv("openDir failed: isDirectory[%d] nfs[%p]", isDirectory(), nfs);
        dirOpened = false;
        return;
    }

    // Close any previously opened directory
    closeDir();

    // Open the directory for listing
    std::string dirPath = apath.empty() ? file_path : apath;
    
    // For nfs_opendir, we can use empty string for root or "."
    if (dirPath.empty()) {
        dirPath = ".";
    }
    
    Debug_printv("openDir calling nfs_opendir with path[%s]", dirPath.c_str());
    if (nfs_opendir(nfs, dirPath.c_str(), &_handle_dir) < 0) {
        Debug_printv("nfs_opendir failed: %s", nfs_get_error(nfs));
        _handle_dir = nullptr;
    }
    dirOpened = (_handle_dir != nullptr);
    entry_index = 0;

    Debug_printv("openDir path[%s] opened[%d]", dirPath.c_str(), dirOpened);
}


void NFSMFile::closeDir()
{
    if (!_handle_dir) {
        return;  // Already closed
    }
    
    auto nfs = getNFS();
    if(nfs && _handle_dir) {
        nfs_closedir(nfs, _handle_dir);
    }
    _handle_dir = nullptr;
    dirOpened = false;
}


bool NFSMFile::rewindDirectory()
{
    auto nfs = getNFS();
    if (!nfs) {
        return false;
    }

    if (!export_path.empty()) {
        // Close and reopen directory to reset position
        openDir(file_path);
    } else {
        dirOpened = true;
        entry_index = 0;
        exports = _session->getExports();
    }

    return dirOpened;
}


MFile* NFSMFile::getNextFileInDir()
{
    if (!dirOpened) {
        rewindDirectory();
    }

    // Check if directory was successfully opened
    if (!dirOpened) {
        Debug_printv("Directory not opened, cannot read entries");
        return nullptr;
    }

    std::string ent_name = "";
    uint32_t ent_mode = 0;
    uint64_t ent_size = 0;
    
    if (!export_path.empty()) {
        // Verify we have a valid directory handle
        if (_handle_dir == nullptr) {
            Debug_printv("Invalid directory handle");
            return nullptr;
        }
        
        auto nfs = getNFS();
        struct nfsdirent *ent;
        do {
            ent = nfs_readdir(nfs, _handle_dir);
            if (ent == nullptr) {
                ent_name.clear();
                break;
            }
            ent_name = ent->name;
            ent_mode = ent->mode;
            ent_size = ent->size;
            // Skip current/parent directory entries
        } while (ent->name[0] == '.' && (ent->name[1] == '\0' || (ent->name[1] == '.' && ent->name[2] == '\0')));
    } else {
        if (entry_index < exports.size()) {
            ent_name = exports[entry_index];
            ent_mode = S_IFDIR;
        } else {
            ent_name.clear();
        }
        entry_index++;
    }

    if (!ent_name.empty()) {
        auto file = new NFSMFile(url + "/" + ent_name);
        file->extension = " " + file->extension;

        // Set size and type information
        file->is_dir = S_ISDIR(ent_mode);
        if (file->is_dir) {
            file->size = 0;
        } else {
            file->size = ent_size;
        }

        return file;
    }

    closeDir();
    return nullptr;
}


bool NFSMFile::readEntry( std::string filename )
{
    auto nfs = getNFS();
    if (!nfs || filename.empty()) {
        return false;
    }

    std::string searchPath = file_path.substr(0, file_path.find_last_of('/'));
    if (searchPath.empty()) {
        searchPath = ".";
    }

    Debug_printv( "path[%s] filename[%s]", searchPath.c_str(), filename.c_str());

    struct nfsdir* dirHandle = nullptr;
    if (nfs_opendir(nfs, searchPath.c_str(), &dirHandle) < 0) {
        return false;
    }

    struct nfsdirent *ent;
    bool found = false;

    while ((ent = nfs_readdir(nfs, dirHandle)) != nullptr) {
        std::string entryFilename = ent->name;

        // Skip hidden files and directory entries
        if (entryFilename[0] == '.') {
            continue;
        }

        Debug_printv("export[%s] path[%s] filename[%s] entry.filename[%.16s]", export_path.c_str(), searchPath.c_str(), filename.c_str(), entryFilename.c_str());

        // Check for matches
        if (filename == "*") {
            name = entryFilename;
            rebuildUrl();
            found = true;
            break;
        }
        else if (filename == entryFilename) {
            found = true;
            break;
        }
        else if (mstr::compare(entryFilename, filename)) {
            Debug_printv( "Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str() );
            name = entryFilename;
            rebuildUrl();
            found = true;
            break;
        }
    }

    nfs_closedir(nfs, dirHandle);

    if (!found) {
        Debug_printv( "Not Found! file[%s]", filename.c_str() );
    }

    return found;
}



/********************************************************
 * MStream implementations
 ********************************************************/
uint32_t NFSMStream::write(const uint8_t *buf, uint32_t size) {
    if (!buf) {
        Debug_printv("Null buffer");
        _error = EINVAL;
        return 0;
    }

    auto nfs = getNFS();
    if (!isOpen() || !nfs || !_handle) {
        Debug_printv("Stream not open or invalid handles");
        _error = EBADF;
        return 0;
    }

    if (size == 0) {
        return 0;
    }

    int result = nfs_write(nfs, _handle, (const void*)buf, size);

    if (result < 0) {
        Debug_printv("NFS write error: %s (rc=%d)", nfs_get_error(nfs), result);
        _error = -result;
        return 0;
    }

    _position += result;
    if (_position > _size) {
        _size = _position;
    }

    return result;
};


bool NFSMStream::open(std::ios_base::openmode mode) {
    Debug_printv("Opening NFS stream url[%s] mode[%d]", url.c_str(), mode);

    if(isOpen()) {
        Debug_printv("Stream already open");
        return true;
    }

    // Validate URL
    if (url.empty()) {
        Debug_printv("Empty URL");
        _error = EINVAL;
        return false;
    }

    // Parse URL to get host, port, export and file path
    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->scheme != "nfs") {
        Debug_printv("Invalid NFS URL: %s", url.c_str());
        _error = EINVAL;
        return false;
    }

    std::string server = parser->host;
    uint16_t nfs_port = parser->port.empty() ? 2049 : std::stoi(parser->port);

    // Obtain NFS session via SessionBroker
    _session = SessionBroker::obtain<NFSMSession>(server, nfs_port);
    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain NFS session for %s:%d", server.c_str(), nfs_port);
        _error = EACCES;
        return false;
    }

    // Parse path to get export and file path
    std::string export_path, file_path;
    parseNFSPath(parser->path, export_path, file_path);
    
    // Store export for getNFS() to use the correct context
    _export = export_path;

    if (file_path.empty()) {
        Debug_printv("No file path specified in URL");
        _error = EINVAL;
        return false;
    }

    // Determine NFS open mode
    int nfs_mode = 0;
    if (mode & std::ios_base::in && mode & std::ios_base::out) {
        nfs_mode = O_RDWR;
    } else if (mode & std::ios_base::out) {
        nfs_mode = O_WRONLY;
        if (mode & std::ios_base::trunc) {
            nfs_mode |= O_CREAT | O_TRUNC;
        }
        if (mode & std::ios_base::app) {
            nfs_mode |= O_APPEND;
        }
    } else {
        nfs_mode = O_RDONLY;
    }

    Debug_printv("NFS open mode[0x%X] export[%s] path[%s]", nfs_mode, export_path.c_str(), file_path.c_str());

    // Get export context if we have a specific export
    if (!export_path.empty()) {
        _export_context = _session->getExportContext(export_path);
        if (!_export_context) {
            Debug_printv("Failed to get export context for: %s", export_path.c_str());
            _error = EACCES;
            return false;
        }
    }

    auto nfs = getNFS();
    if (!nfs) {
        Debug_printv("Failed to get NFS context");
        _error = EACCES;
        return false;
    }

    // Open the file with the file_path
    if (nfs_open(nfs, file_path.c_str(), nfs_mode, &_handle) < 0) {
        Debug_printv("Failed to open file: %s", nfs_get_error(nfs));
        _error = EACCES;
        return false;
    }

    // Get file size using NFS stat
    struct nfs_stat_64 st;
    if (nfs_fstat64(nfs, _handle, &st) == 0) {
        _size = st.nfs_size;
    } else {
        Debug_printv("Warning: Could not get file size: %s", nfs_get_error(nfs));
        _size = 0;
    }

    _position = 0;
    return true;
};

void NFSMStream::close() {
    if(isOpen()) {
        auto nfs = getNFS();
        if (nfs && _handle) {
            nfs_close(nfs, _handle);
        }
        _handle = nullptr;
        _position = 0;
        _size = 0;
    }
};

uint32_t NFSMStream::read(uint8_t* buf, uint32_t size) {
    if (!buf) {
        Debug_printv("Null buffer");
        _error = EINVAL;
        return 0;
    }

    auto nfs = getNFS();
    if (!isOpen() || !nfs || !_handle) {
        Debug_printv("Stream not open or invalid handles");
        _error = EBADF;
        return 0;
    }

    if (size == 0) {
        return 0;
    }

    int bytesRead = nfs_read(nfs, _handle, (void*)buf, size);

    if (bytesRead < 0) {
        Debug_printv("NFS read error: %s (rc=%d)", nfs_get_error(nfs), bytesRead);
        _error = -bytesRead;
        return 0;
    }

    _position += bytesRead;
    return bytesRead;
};

bool NFSMStream::seek(uint32_t pos) {
    auto nfs = getNFS();
    if (!isOpen() || !nfs || !_handle) {
        Debug_printv("Stream not open");
        _error = EBADF;
        return false;
    }

    uint64_t result = nfs_lseek(nfs, _handle, pos, SEEK_SET, NULL);
    if (result == (uint64_t)-1) {
        Debug_printv("NFS seek error: %s", nfs_get_error(nfs));
        _error = errno;
        return false;
    }

    _position = pos;
    return true;
};

bool NFSMStream::seek(uint32_t pos, int mode) {
    auto nfs = getNFS();
    if (!isOpen() || !nfs || !_handle) {
        Debug_printv("Stream not open");
        _error = EBADF;
        return false;
    }

    uint64_t result = nfs_lseek(nfs, _handle, pos, mode, NULL);
    if (result == (uint64_t)-1) {
        Debug_printv("NFS seek error: %s", nfs_get_error(nfs));
        _error = errno;
        return false;
    }

    // Update position based on actual result from lseek
    _position = (uint32_t)result;

    return true;
}

bool NFSMStream::isOpen() {
    return _session != nullptr && _session->isConnected() && _handle != nullptr;
}

/********************************************************
 * NFSHandle implementations
 ********************************************************/


NFSHandle::~NFSHandle() {
    dispose();
}

void NFSHandle::dispose() {
    if (_handle != nullptr && _session && _session->isConnected()) {
        auto nfs = _session->getContext();
        if (nfs) {
            nfs_close(nfs, _handle);
        }
        _handle = nullptr;
    }
    _session.reset();
}

void NFSHandle::obtain(std::string m_path, int nfs_mode) {
    // Parse the URL to extract all components
    // Expected format: nfs://server/export/path/to/file
    auto parser = PeoplesUrlParser::parseURL(m_path);
    if (!parser || parser->scheme != "nfs") {
        Debug_printv("Invalid NFS URL: %s", m_path.c_str());
        dispose();
        return;
    }

    std::string server = parser->host;
    uint16_t nfs_port = parser->port.empty() ? 2049 : std::stoi(parser->port);
    std::string export_path, filepath;

    if (!parseNFSPath(parser->path, export_path, filepath)) {
        Debug_printv("Invalid NFS path: %s", parser->path.c_str());
        dispose();
        return;
    }

    Debug_printv("Connecting to server[%s] export[%s] filepath[%s]",
                 server.c_str(), export_path.c_str(), filepath.c_str());

    // Obtain NFS session via SessionBroker
    _session = SessionBroker::obtain<NFSMSession>(server, nfs_port);
    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain NFS session for %s:%d", server.c_str(), nfs_port);
        dispose();
        return;
    }

    auto nfs = _session->getContext();
    if (!nfs) {
        Debug_printv("Failed to get NFS context");
        dispose();
        return;
    }

    // Open the file
    if (nfs_open(nfs, filepath.c_str(), nfs_mode, &_handle) < 0) {
        Debug_printv("Failed to open file %s: %s", filepath.c_str(), nfs_get_error(nfs));
        dispose();
        return;
    }

    Debug_printv("Successfully opened NFS file: %s", m_path.c_str());
}
