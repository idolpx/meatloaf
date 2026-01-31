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

#include "sftp.h"

// Only compile SFTP support if libssh has it available
#ifdef WITH_SFTP

#include "meatloaf.h"
#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>


/********************************************************
 * SFTPMSession Implementation
 ********************************************************/

SFTPMSession::SFTPMSession(std::string host, uint16_t port)
    : MSession(host, port)
{
    Debug_printv("SFTPMSession created for %s:%d", host.c_str(), port);
}

SFTPMSession::~SFTPMSession() {
    Debug_printv("SFTPMSession destroyed for %s:%d", host.c_str(), port);
    disconnect();
}

void SFTPMSession::setCredentials(const std::string& user, const std::string& pass) {
    username = user;
    password = pass;
}

void SFTPMSession::setPrivateKey(const std::string& keypath) {
    private_key_path = keypath;
}

bool SFTPMSession::connect() {
    if (connected) {
        Debug_printv("Already connected to %s:%d", host.c_str(), port);
        return true;
    }

    // Create SSH session
    ssh_handle = ssh_new();
    if (ssh_handle == nullptr) {
        Debug_printv("Failed to create SSH session");
        return false;
    }

    // Set connection options
    ssh_options_set(ssh_handle, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(ssh_handle, SSH_OPTIONS_PORT, &port);
    
    if (!username.empty()) {
        ssh_options_set(ssh_handle, SSH_OPTIONS_USER, username.c_str());
    }

    // Connect to SSH server
    int rc = ssh_connect(ssh_handle);
    if (rc != SSH_OK) {
        Debug_printv("Failed to connect to SSH server %s:%d - %s", 
                     host.c_str(), port, ssh_get_error(ssh_handle));
        ssh_free(ssh_handle);
        ssh_handle = nullptr;
        return false;
    }

    Debug_printv("Connected to SSH server %s:%d", host.c_str(), port);

    // Authenticate
    bool authenticated = false;
    
    // Try public key authentication first if key is provided
    if (!private_key_path.empty()) {
        authenticated = authenticatePublicKey();
    }
    
    // Try password authentication if public key failed or not provided
    if (!authenticated && !password.empty()) {
        authenticated = authenticatePassword();
    }

    // Try "none" authentication as fallback (some servers allow this)
    if (!authenticated) {
        rc = ssh_userauth_none(ssh_handle, nullptr);
        if (rc == SSH_AUTH_SUCCESS) {
            authenticated = true;
            Debug_printv("Authenticated with 'none' method");
        }
    }

    if (!authenticated) {
        Debug_printv("Authentication failed for %s:%d", host.c_str(), port);
        ssh_disconnect(ssh_handle);
        ssh_free(ssh_handle);
        ssh_handle = nullptr;
        return false;
    }

    // Initialize SFTP session
    sftp_handle = sftp_new(ssh_handle);
    if (sftp_handle == nullptr) {
        Debug_printv("Failed to create SFTP session: %s", ssh_get_error(ssh_handle));
        ssh_disconnect(ssh_handle);
        ssh_free(ssh_handle);
        ssh_handle = nullptr;
        return false;
    }

    rc = sftp_init(sftp_handle);
    if (rc != SSH_OK) {
        Debug_printv("Failed to initialize SFTP: %s", ssh_get_error(ssh_handle));
        sftp_free(sftp_handle);
        sftp_handle = nullptr;
        ssh_disconnect(ssh_handle);
        ssh_free(ssh_handle);
        ssh_handle = nullptr;
        return false;
    }

    Debug_printv("Successfully initialized SFTP session for %s:%d", host.c_str(), port);
    connected = true;
    updateActivity();
    return true;
}

bool SFTPMSession::authenticatePassword() {
    if (password.empty() || ssh_handle == nullptr) {
        return false;
    }

    int rc = ssh_userauth_password(ssh_handle, nullptr, password.c_str());
    if (rc == SSH_AUTH_SUCCESS) {
        Debug_printv("Authenticated with password");
        return true;
    }

    Debug_printv("Password authentication failed: %s", ssh_get_error(ssh_handle));
    return false;
}

bool SFTPMSession::authenticatePublicKey() {
    if (private_key_path.empty() || ssh_handle == nullptr) {
        return false;
    }

    // Try auto authentication with default keys
    int rc = ssh_userauth_publickey_auto(ssh_handle, nullptr, nullptr);
    if (rc == SSH_AUTH_SUCCESS) {
        Debug_printv("Authenticated with public key (auto)");
        return true;
    }

    Debug_printv("Public key authentication failed: %s", ssh_get_error(ssh_handle));
    return false;
}

void SFTPMSession::disconnect() {
    if (!connected) {
        return;
    }

    if (sftp_handle != nullptr) {
        Debug_printv("Closing SFTP session for %s:%d", host.c_str(), port);
        sftp_free(sftp_handle);
        sftp_handle = nullptr;
    }

    if (ssh_handle != nullptr) {
        Debug_printv("Disconnecting SSH session for %s:%d", host.c_str(), port);
        ssh_disconnect(ssh_handle);
        ssh_free(ssh_handle);
        ssh_handle = nullptr;
    }

    connected = false;
}

bool SFTPMSession::keep_alive() {
    if (!connected || !ssh_handle) {
        return false;
    }

    // Check if connection is still alive
    if (ssh_is_connected(ssh_handle)) {
        updateActivity();
        return true;
    }

    Debug_printv("Keep-alive failed for %s:%d", host.c_str(), port);
    connected = false;
    return false;
}


/********************************************************
 * SFTPMFile Implementation
 ********************************************************/

SFTPMFile::SFTPMFile(std::string path): MFile(path) {
    Debug_printv("SFTPMFile created: url[%s] host[%s] path[%s]", 
                 url.c_str(), host.c_str(), this->path.c_str());

    // Parse credentials from URL if present (sftp://user:pass@host/path)
    std::string username, password;
    size_t at_pos = host.find('@');
    if (at_pos != std::string::npos) {
        std::string userinfo = host.substr(0, at_pos);
        host = host.substr(at_pos + 1);
        
        size_t colon_pos = userinfo.find(':');
        if (colon_pos != std::string::npos) {
            username = userinfo.substr(0, colon_pos);
            password = userinfo.substr(colon_pos + 1);
        } else {
            username = userinfo;
        }
    }

    // Obtain or create SFTP session via SessionBroker
    uint16_t sftp_port = port.empty() ? 22 : std::stoi(port);
    _session = SessionBroker::obtain<SFTPMSession>(host, sftp_port);

    if (!_session) {
        Debug_printv("Failed to obtain SFTP session for %s:%d", host.c_str(), sftp_port);
        m_isNull = true;
        return;
    }

    // Set credentials if provided
    if (!username.empty()) {
        _session->setCredentials(username, password);
    }

    if (!_session->isConnected()) {
        Debug_printv("SFTP session not connected, will connect on first access");
    }

    // Validate path
    if (!pathValid(this->path)) {
        m_isNull = true;
    } else {
        m_isNull = false;
    }

    Debug_printv("SFTP path[%s] valid[%d]", this->path.c_str(), !m_isNull);
}

SFTPMFile::~SFTPMFile() {
    closeDir();
    
    if (current_attrs) {
        sftp_attributes_free(current_attrs);
        current_attrs = nullptr;
    }

    _session.reset();
}

bool SFTPMFile::pathValid(std::string path) {
    if (path.empty() || path == "/") {
        return true;
    }

    // Ensure session is connected
    if (!_session || !_session->connect()) {
        Debug_printv("Failed to connect SFTP session");
        return false;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return false;
    }

    // Check if path exists
    sftp_attributes attrs = sftp_stat(sftp, path.c_str());
    if (attrs == nullptr) {
        Debug_printv("Path does not exist: %s", path.c_str());
        return false;
    }

    sftp_attributes_free(attrs);
    return true;
}

bool SFTPMFile::isDirectory() {
    if (is_dir > -1) return is_dir;
    if (!_session || !_session->connect()) {
        return false;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return false;
    }

    std::string full_path = path;
    if (full_path.empty()) {
        full_path = "/";
    }

    sftp_attributes attrs = sftp_stat(sftp, full_path.c_str());
    if (attrs == nullptr) {
        return false;
    }

    bool is_dir = (attrs->type == SSH_FILEXFER_TYPE_DIRECTORY);
    sftp_attributes_free(attrs);
    
    return is_dir;
}

bool SFTPMFile::exists() {
    return pathValid(path);
}

time_t SFTPMFile::getLastWrite() {
    if (!_session || !_session->connect()) {
        return 0;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return 0;
    }

    sftp_attributes attrs = sftp_stat(sftp, path.c_str());
    if (attrs == nullptr) {
        return 0;
    }

    time_t mtime = attrs->mtime;
    sftp_attributes_free(attrs);
    
    return mtime;
}

time_t SFTPMFile::getCreationTime() {
    if (!_session || !_session->connect()) {
        return 0;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return 0;
    }

    sftp_attributes attrs = sftp_stat(sftp, path.c_str());
    if (attrs == nullptr) {
        return 0;
    }

    time_t atime = attrs->atime;
    sftp_attributes_free(attrs);
    
    return atime;
}

uint64_t SFTPMFile::getAvailableSpace() {
    // SFTP doesn't provide a standard way to get disk space
    // Would need statvfs extension which is not always available
    return 0;
}

void SFTPMFile::openDir(std::string dirpath) {
    if (dirOpened) {
        return;
    }

    if (!_session || !_session->connect()) {
        Debug_printv("Failed to connect SFTP session");
        return;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return;
    }

    if (dirpath.empty()) {
        dirpath = "/";
    }

    _dir_handle = sftp_opendir(sftp, dirpath.c_str());
    if (_dir_handle == nullptr) {
        Debug_printv("Failed to open directory: %s - error: %d", 
                     dirpath.c_str(), sftp_get_error(sftp));
        return;
    }

    dirOpened = true;
    Debug_printv("Opened directory: %s", dirpath.c_str());
}

void SFTPMFile::closeDir() {
    if (!dirOpened || _dir_handle == nullptr) {
        return;
    }

    sftp_closedir(_dir_handle);
    _dir_handle = nullptr;
    dirOpened = false;
    
    Debug_printv("Closed directory");
}

bool SFTPMFile::rewindDirectory() {
    closeDir();
    openDir(path);
    return dirOpened;
}

MFile* SFTPMFile::getNextFileInDir() {
    if (!dirOpened) {
        openDir(path);
    }

    if (!_dir_handle) {
        return nullptr;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return nullptr;
    }

    sftp_attributes attrs = sftp_readdir(sftp, _dir_handle);
    if (attrs == nullptr) {
        // End of directory
        dirOpened = false;
        return nullptr;
    }

    // Skip . and ..
    while (attrs && (strcmp(attrs->name, ".") == 0 || strcmp(attrs->name, "..") == 0)) {
        sftp_attributes_free(attrs);
        attrs = sftp_readdir(sftp, _dir_handle);
    }

    if (attrs == nullptr) {
        dirOpened = false;
        return nullptr;
    }

    // Build full path for the entry
    std::string entry_path = url;
    if (!mstr::endsWith(entry_path, "/")) {
        entry_path += "/";
    }
    entry_path += attrs->name;

    Debug_printv("Directory entry: %s (type=%d)", entry_path.c_str(), attrs->type);

    SFTPMFile* file = new SFTPMFile(entry_path);
    
    // Store attributes for later use
    if (file->current_attrs) {
        sftp_attributes_free(file->current_attrs);
    }
    file->current_attrs = attrs;

    return file;
}

bool SFTPMFile::mkDir() {
    if (!_session || !_session->connect()) {
        return false;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return false;
    }

    int rc = sftp_mkdir(sftp, path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (rc != SSH_OK) {
        Debug_printv("Failed to create directory: %s - error: %d", 
                     path.c_str(), sftp_get_error(sftp));
        return false;
    }

    Debug_printv("Created directory: %s", path.c_str());
    return true;
}

bool SFTPMFile::remove() {
    if (!_session || !_session->connect()) {
        return false;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return false;
    }

    int rc;
    if (isDirectory()) {
        rc = sftp_rmdir(sftp, path.c_str());
    } else {
        rc = sftp_unlink(sftp, path.c_str());
    }

    if (rc != SSH_OK) {
        Debug_printv("Failed to remove: %s - error: %d", 
                     path.c_str(), sftp_get_error(sftp));
        return false;
    }

    Debug_printv("Removed: %s", path.c_str());
    return true;
}

bool SFTPMFile::rename(std::string dest) {
    if (!_session || !_session->connect()) {
        return false;
    }

    sftp_session sftp = getSFTPSession();
    if (!sftp) {
        return false;
    }

    int rc = sftp_rename(sftp, path.c_str(), dest.c_str());
    if (rc != SSH_OK) {
        Debug_printv("Failed to rename %s to %s - error: %d", 
                     path.c_str(), dest.c_str(), sftp_get_error(sftp));
        return false;
    }

    Debug_printv("Renamed %s to %s", path.c_str(), dest.c_str());
    return true;
}

std::shared_ptr<MStream> SFTPMFile::getSourceStream(std::ios_base::openmode mode) {
    return createStream(mode);
}

std::shared_ptr<MStream> SFTPMFile::getDecodedStream(std::shared_ptr<MStream> src) {
    return src;
}

std::shared_ptr<MStream> SFTPMFile::createStream(std::ios_base::openmode mode) {
    return std::make_shared<SFTPMStream>(url);
}


/********************************************************
 * SFTPMStream Implementation
 ********************************************************/

bool SFTPMStream::isOpen() {
    return _file_handle != nullptr;
}

uint32_t SFTPMStream::mapOpenMode(std::ios_base::openmode mode) {
    uint32_t access = 0;

    if (mode & std::ios_base::in) {
        access |= O_RDONLY;
    }
    if (mode & std::ios_base::out) {
        if (mode & std::ios_base::in) {
            access = O_RDWR;
        } else {
            access = O_WRONLY;
        }
        if (mode & std::ios_base::trunc) {
            access |= O_CREAT | O_TRUNC;
        } else if (mode & std::ios_base::app) {
            access |= O_CREAT | O_APPEND;
        } else {
            access |= O_CREAT;
        }
    }

    return access;
}

mode_t SFTPMStream::mapFileMode(std::ios_base::openmode mode) {
    // Default file creation mode
    return S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
}

bool SFTPMStream::open(std::ios_base::openmode mode) {
    if (isOpen()) {
        return true;
    }

    Debug_printv("Opening SFTP stream: %s", url.c_str());

    // Parse URL to get components
    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser) {
        Debug_printv("Failed to parse URL: %s", url.c_str());
        return false;
    }
    
    // Parse credentials from URL if present
    std::string username, password;
    std::string host_str = parser->host;
    size_t at_pos = host_str.find('@');
    if (at_pos != std::string::npos) {
        std::string userinfo = host_str.substr(0, at_pos);
        host_str = host_str.substr(at_pos + 1);
        
        size_t colon_pos = userinfo.find(':');
        if (colon_pos != std::string::npos) {
            username = userinfo.substr(0, colon_pos);
            password = userinfo.substr(colon_pos + 1);
        } else {
            username = userinfo;
        }
    }

    // Obtain or create SFTP session
    uint16_t sftp_port = parser->port.empty() ? 22 : std::stoi(parser->port);
    _session = SessionBroker::obtain<SFTPMSession>(host_str, sftp_port);

    if (!_session) {
        Debug_printv("Failed to obtain SFTP session");
        return false;
    }

    // Set credentials if provided
    if (!username.empty()) {
        _session->setCredentials(username, password);
    }

    if (!_session->connect()) {
        Debug_printv("Failed to connect SFTP session");
        return false;
    }

    sftp_session sftp = _session->getSFTPSession();
    if (!sftp) {
        Debug_printv("No SFTP session available");
        return false;
    }

    // Open the file
    uint32_t access = mapOpenMode(mode);
    mode_t file_mode = mapFileMode(mode);

    _file_handle = sftp_open(sftp, parser->path.c_str(), access, file_mode);
    if (_file_handle == nullptr) {
        Debug_printv("Failed to open file: %s - error: %d", 
                     parser->path.c_str(), sftp_get_error(sftp));
        return false;
    }

    this->mode = mode;
    _position = 0;
    
    // Get file size
    sftp_attributes attrs = sftp_fstat(_file_handle);
    if (attrs) {
        _size = attrs->size;
        sftp_attributes_free(attrs);
    }

    Debug_printv("Opened SFTP file: %s (size=%u)", parser->path.c_str(), _size);
    return true;
}

void SFTPMStream::close() {
    if (!isOpen()) {
        return;
    }

    Debug_printv("Closing SFTP stream");

    if (_file_handle) {
        sftp_close(_file_handle);
        _file_handle = nullptr;
    }

    _session.reset();
}

uint32_t SFTPMStream::read(uint8_t* buf, uint32_t size) {
    if (!isOpen() && !open(std::ios_base::in)) {
        return 0;
    }

    ssize_t bytes = sftp_read(_file_handle, buf, size);
    if (bytes < 0) {
        Debug_printv("Read error: %d", sftp_get_error(_session->getSFTPSession()));
        return 0;
    }

    _position += bytes;
    return bytes;
}

uint32_t SFTPMStream::write(const uint8_t *buf, uint32_t size) {
    if (!isOpen() && !open(std::ios_base::out)) {
        return 0;
    }

    ssize_t bytes = sftp_write(_file_handle, buf, size);
    if (bytes < 0) {
        Debug_printv("Write error: %d", sftp_get_error(_session->getSFTPSession()));
        return 0;
    }

    _position += bytes;
    if (_position > _size) {
        _size = _position;
    }
    
    return bytes;
}

bool SFTPMStream::seek(uint32_t pos) {
    return seek(pos, SEEK_SET);
}

bool SFTPMStream::seek(uint32_t pos, int mode) {
    if (!isOpen()) {
        return false;
    }

    uint64_t new_pos = pos;
    
    if (mode == SEEK_CUR) {
        new_pos = _position + pos;
    } else if (mode == SEEK_END) {
        new_pos = _size + pos;
    }

    int rc = sftp_seek64(_file_handle, new_pos);
    if (rc != 0) {
        Debug_printv("Seek failed to position %llu", new_pos);
        return false;
    }

    _position = new_pos;
    return true;
}

#endif // WITH_SFTP
