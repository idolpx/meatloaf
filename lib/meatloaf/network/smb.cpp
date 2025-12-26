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

#include "smb.h"


#include "meatloaf.h"

#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Static members for share enumeration
std::vector<std::string> SMBMSession::_shares_temp;
int SMBMSession::_enum_finished = 0;
std::vector<std::string> SMBMFile::shares;

/********************************************************
 * Helper Functions
 ********************************************************/

bool parseSMBPath(const std::string& path, std::string& share, std::string& share_path) {

    // Find server
    size_t pos = path.find('/', 1);
    if (pos == std::string::npos) {
        share = path.substr(1);
        share_path = "";
    } else {
        share = path.substr(1, pos - 1);
        share_path = path.substr(pos + 1);
    }

    return true;
}


/********************************************************
 * MFile implementations
 ********************************************************/

bool SMBMFile::pathValid(std::string path)
{
    if (path.empty() || path.length() > PATH_MAX) {
        return false;
    }

    // Check if SMB context is available
    if (!getSMB()) {
        return false;
    }

    return true;
}

bool SMBMFile::isDirectory()
{
    //Debug_printv("path[%s] len[%d]", share_path.c_str(), share_path.size());
    if(share_path=="/" || share_path.empty())
        return true;

    auto smb = getSMB();
    if (!smb) {
        return false;
    }

    struct smb2_stat_64 st;
    if (smb2_stat(smb, std::string(basepath + share_path).c_str(), &st) < 0) {
        return false;
    }

    return st.smb2_type == SMB2_TYPE_DIRECTORY;
}

std::shared_ptr<MStream> SMBMFile::getSourceStream(std::ios_base::openmode mode) {
    // Add pathInStream to URL if specified
    if ( pathInStream.size() )
        url += "/" + pathInStream;

    Debug_printv("url[%s] mode[%d]", url.c_str(), mode);
    std::shared_ptr<MStream> istream = std::make_shared<SMBMStream>(url);
    istream->open(mode);
    return istream;
}

std::shared_ptr<MStream> SMBMFile::getDecodedStream(std::shared_ptr<MStream> is) {
    return is; // we don't have to process this stream in any way, just return the original stream
}

std::shared_ptr<MStream> SMBMFile::createStream(std::ios_base::openmode mode)
{
    std::shared_ptr<MStream> istream = std::make_shared<SMBMStream>(url);
    istream->open(mode);
    return istream;
}

time_t SMBMFile::getLastWrite()
{
    auto smb = getSMB();
    if (!smb) {
        return 0;
    }

    struct smb2_stat_64 st;
    if (smb2_stat(smb, std::string(basepath + share_path).c_str(), &st) < 0) {
        return 0;
    }

    return st.smb2_mtime;
}

time_t SMBMFile::getCreationTime()
{
    auto smb = getSMB();
    if (!smb) {
        return 0;
    }

    struct smb2_stat_64 st;
    if (smb2_stat(smb, std::string(basepath + share_path).c_str(), &st) < 0) {
        return 0;
    }

    return st.smb2_ctime;
}

uint64_t SMBMFile::getAvailableSpace()
{
    auto smb = getSMB();
    if (!smb) {
        Debug_printv("SMB context not available");
        return 0;
    }

    // Get bytes free
    struct smb2_statvfs status;
    smb2_statvfs(smb, share_path.c_str(), &status);
    //Debug_printv("size[%llu] blocks[%llu] free[%llu] avail[%llu]", status.f_bsize, status.f_blocks, status.f_bfree, status.f_bavail);
    return (status.f_bfree * status.f_bsize);
}

bool SMBMFile::mkDir()
{
    auto smb = getSMB();
    if (m_isNull || !smb) {
        return false;
    }

    int rc = smb2_mkdir(smb, std::string(basepath + share_path).c_str());
    return (rc == 0);
}

bool SMBMFile::exists()
{
    auto smb = getSMB();
    if (m_isNull || !smb) {
        return false;
    }
    if (share_path=="/" || share_path=="") {
        return true;
    }

    struct smb2_stat_64 st;
    int rc = smb2_stat(smb, std::string(basepath + share_path).c_str(), &st);
    return (rc == 0);
}


bool SMBMFile::remove() {
    auto smb = getSMB();
    if (!smb) {
        return false;
    }

    // Check if it's a directory or file
    if (isDirectory()) {
        return smb2_rmdir(smb, std::string(basepath + share_path).c_str()) == 0;
    } else {
        return smb2_unlink(smb, std::string(basepath + share_path).c_str()) == 0;
    }
}


bool SMBMFile::rename(std::string pathTo) {
    auto smb = getSMB();
    if(pathTo.empty() || !smb) {
        return false;
    }

    int rc = smb2_rename(smb, std::string(basepath + share_path).c_str(), std::string(basepath + pathTo).c_str());
    return (rc == 0);
}


void SMBMFile::openDir(std::string apath)
{
    auto smb = getSMB();
    if (!isDirectory() || !smb) {
        Debug_printv("openDir failed: isDirectory[%d] smb[%p]", isDirectory(), smb);
        dirOpened = false;
        return;
    }

    // Close any previously opened directory
    closeDir();

    // Open the directory for listing
    std::string dirPath = apath.empty() ? share_path : apath;
    
    // For smb2_opendir, we can use empty string for root or "."
    // Try empty string first (root of share)
    if (dirPath.empty()) {
        dirPath = "";
    }
    
    Debug_printv("openDir calling smb2_opendir with path[%s] (empty=%d)", dirPath.c_str(), dirPath.empty());
    _handle_dir = smb2_opendir(smb, dirPath.empty() ? "" : dirPath.c_str());
    if (!_handle_dir) {
        Debug_printv("smb2_opendir failed: %s", smb2_get_error(smb));
    }
    dirOpened = (_handle_dir != nullptr);
    entry_index = 0;

    Debug_printv("openDir path[%s] opened[%d]", dirPath.c_str(), dirOpened);
}


void SMBMFile::closeDir()
{
    auto smb = getSMB();
    if(smb && _handle_dir) {
        smb2_closedir(smb, _handle_dir);
    }
    _handle_dir = nullptr;
    dirOpened = false;
}


bool SMBMFile::rewindDirectory()
{
    auto smb = getSMB();
    if (!smb) {
        return false;
    }

    if (!share.empty()) {
        // Close and reopen directory to reset position
        openDir(share_path);
    } else {
        dirOpened = true;
        entry_index = 0;
        shares = _session->getShares();
    }

    //Debug_printv("dirOpened[%d] entry_index[%d] share_path[%s]", dirOpened, entry_index, share_path.c_str());
    return dirOpened;
}


MFile* SMBMFile::getNextFileInDir()
{
    //Debug_printv("dirOpened[%d] entry_index[%d] share[%s] share_path[%s]", dirOpened, entry_index, share.c_str(), share_path.c_str());
    if (!dirOpened) {
        rewindDirectory();
    }

    std::string ent_name = "";
    uint32_t ent_type = 0;
    uint64_t ent_size = 0;
    if (!share.empty()) {
        auto smb = getSMB();
        struct smb2dirent *ent;
        do {
            ent = smb2_readdir(smb, _handle_dir);
            if (ent == nullptr) {
                ent_name.clear();
                break;
            }
            ent_name = ent->name;
            ent_type = ent->st.smb2_type;
            ent_size = ent->st.smb2_size;
            // Skip hidden files and current/parent directory entries
        } while (ent->name[0] == '.' && (ent->name[1] == '\0' || (ent->name[1] == '.' && ent->name[2] == '\0')));
        //Debug_printv("FILES ent_name[%s] ent_type[%d] ent_size[%llu]", ent_name.c_str(), ent_type, ent_size);
    } else {
        if (entry_index < shares.size()) {
            ent_name = shares[entry_index];
            ent_type = SMB2_TYPE_DIRECTORY;
        } else {
            ent_name.clear();
        }
        entry_index++;
        //Debug_printv("SHARES ent_name[%s] ent_type[%d] ent_size[%llu]", ent_name.c_str(), ent_type, ent_size);
    }

    if (!ent_name.empty()) {

        //Debug_printv("url[%s] entry[%s] index[%d]", mRawUrl.c_str(), ent_name.c_str(), entry_index);

        auto file = new SMBMFile(url + "/" + ent_name);
        file->extension = " " + file->extension;

        // Set size and type information
        if (ent_type == SMB2_TYPE_DIRECTORY) {
            file->size = 0;
        } else {
            file->size = ent_size;
        }

        return file;
    }

    closeDir();
    return nullptr;
}


bool SMBMFile::readEntry( std::string filename )
{
    auto smb = getSMB();
    if (!smb || filename.empty()) {
        return false;
    }

    std::string searchPath = share_path.substr(0, share_path.find_last_of('/'));
    if (searchPath.empty()) {
        searchPath = "/";
    }

    Debug_printv( "path[%s] filename[%s]", searchPath.c_str(), filename.c_str());

    struct smb2dir* dirHandle = smb2_opendir(smb, searchPath.c_str());
    if (dirHandle == nullptr) {
        return false;
    }

    struct smb2dirent *ent;
    bool found = false;

    while ((ent = smb2_readdir(smb, dirHandle)) != nullptr) {
        std::string entryFilename = ent->name;

        // Skip hidden files and directory entries
        if (entryFilename[0] == '.') {
            continue;
        }

        Debug_printv("share[%s] path[%s] filename[%s] entry.filename[%.16s]", share.c_str(), searchPath.c_str(), filename.c_str(), entryFilename.c_str());

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
        else if (mstr::compare(filename, entryFilename)) {
            Debug_printv( "Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str() );
            name = entryFilename;
            rebuildUrl();
            found = true;
            found = true;
            break;
        }
    }

    smb2_closedir(smb, dirHandle);

    if (!found) {
        Debug_printv( "Not Found! file[%s]", filename.c_str() );
    }

    return found;
}



/********************************************************
 * MStream implementations
 ********************************************************/
uint32_t SMBMStream::write(const uint8_t *buf, uint32_t size) {
    if (!buf) {
        Debug_printv("Null buffer");
        _error = EINVAL;
        return 0;
    }

    auto smb = getSMB();
    if (!isOpen() || !smb || !_handle) {
        Debug_printv("Stream not open or invalid handles");
        _error = EBADF;
        return 0;
    }

    if (size == 0) {
        return 0;
    }

    int result = smb2_write(smb, _handle, buf, size);

    if (result < 0) {
        Debug_printv("SMB write error: %s (rc=%d)", smb2_get_error(smb), result);
        _error = -result;
        return 0;
    }

    _position += result;
    if (_position > _size) {
        _size = _position;
    }

    return result;
};


bool SMBMStream::open(std::ios_base::openmode mode) {
    Debug_printv("Opening SMB stream url[%s] mode[%d]", url.c_str(), mode);

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

    // Parse URL to get host, port, share and file path
    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->scheme != "smb") {
        Debug_printv("Invalid SMB URL: %s", url.c_str());
        _error = EINVAL;
        return false;
    }

    std::string server = parser->host;
    std::string user = parser->user;
    std::string password = parser->password;
    uint16_t smb_port = parser->port.empty() ? 445 : std::stoi(parser->port);

    // Obtain SMB session via SessionBroker
    _session = SessionBroker::obtain<SMBMSession>(server, smb_port);
    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain SMB session for %s:%d", server.c_str(), smb_port);
        _error = EACCES;
        return false;
    }

    // Set credentials on the session if available
    if (!user.empty() || !password.empty()) {
        _session->setCredentials(user, password);
    }

    // Parse path to get share and file path
    std::string share, share_path;
    parseSMBPath(parser->path, share, share_path);
    
    // Store share for getSMB() to use the correct context
    _share = share;

    if (share_path.empty()) {
        Debug_printv("No file path specified in URL");
        _error = EINVAL;
        return false;
    }

    // Determine SMB2 open mode
    int smb_mode = 0;
    if (mode & std::ios_base::in && mode & std::ios_base::out) {
        smb_mode = O_RDWR;
    } else if (mode & std::ios_base::out) {
        smb_mode = O_WRONLY;
        if (mode & std::ios_base::trunc) {
            smb_mode |= O_CREAT | O_TRUNC;
        }
        if (mode & std::ios_base::app) {
            smb_mode |= O_APPEND;
        }
    } else {
        smb_mode = O_RDONLY;
    }

    Debug_printv("SMB open mode[0x%X] share[%s] path[%s]", smb_mode, share.c_str(), share_path.c_str());

    // Get share context if we have a specific share
    if (!share.empty()) {
        _share_context = _session->getShareContext(share);
        if (!_share_context) {
            Debug_printv("Failed to get share context for: %s", share.c_str());
            _error = EACCES;
            return false;
        }
    }

    auto smb = getSMB();
    if (!smb) {
        Debug_printv("Failed to get SMB context");
        _error = EACCES;
        return false;
    }

    // Open the file with the share_path as-is
    _handle = smb2_open(smb, share_path.c_str(), smb_mode);
    if (!_handle) {
        Debug_printv("Failed to open file: %s", smb2_get_error(smb));
        _error = EACCES;
        return false;
    }

    // Get file size using SMB2 stat
    struct smb2_stat_64 st;
    if (smb2_fstat(smb, _handle, &st) == 0) {
        _size = st.smb2_size;
        Debug_printv("File size: %u bytes", _size);
    } else {
        Debug_printv("Warning: Could not get file size: %s", smb2_get_error(smb));
        _size = 0;
    }

    _position = 0;
    return true;
};

void SMBMStream::close() {
    if(isOpen()) {
        auto smb = getSMB();
        if (smb && _handle) {
            smb2_close(smb, _handle);
        }
        _handle = nullptr;
        _position = 0;
        _size = 0;
    }
};

uint32_t SMBMStream::read(uint8_t* buf, uint32_t size) {
    if (!buf) {
        Debug_printv("Null buffer");
        _error = EINVAL;
        return 0;
    }

    auto smb = getSMB();
    if (!isOpen() || !smb || !_handle) {
        Debug_printv("Stream not open or invalid handles");
        _error = EBADF;
        return 0;
    }

    if (size == 0) {
        return 0;
    }

    int bytesRead = smb2_read(smb, _handle, buf, size);

    if (bytesRead < 0) {
        Debug_printv("SMB read error: %s (rc=%d)", smb2_get_error(smb), bytesRead);
        _error = -bytesRead;
        return 0;
    }

    _position += bytesRead;
    return bytesRead;
};

bool SMBMStream::seek(uint32_t pos) {
    auto smb = getSMB();
    if (!isOpen() || !smb || !_handle) {
        Debug_printv("Stream not open");
        _error = EBADF;
        return false;
    }

    int64_t result = smb2_lseek(smb, _handle, pos, SEEK_SET, NULL);
    if (result < 0) {
        Debug_printv("SMB seek error: %s (rc=%lld)", smb2_get_error(smb), result);
        _error = -result;
        return false;
    }

    _position = pos;
    return true;
};

bool SMBMStream::seek(uint32_t pos, int mode) {
    auto smb = getSMB();
    if (!isOpen() || !smb || !_handle) {
        Debug_printv("Stream not open");
        _error = EBADF;
        return false;
    }

    int64_t result = smb2_lseek(smb, _handle, pos, mode, NULL);
    if (result < 0) {
        Debug_printv("SMB seek error: %s (rc=%lld)", smb2_get_error(smb), result);
        _error = -result;
        return false;
    }

    // Update position based on actual result from lseek
    _position = (uint32_t)result;

    return true;
}

bool SMBMStream::isOpen() {
    return _session != nullptr && _session->isConnected() && _handle != nullptr;
}

/********************************************************
 * SMBHandle implementations
 ********************************************************/


SMBHandle::~SMBHandle() {
    dispose();
}

void SMBHandle::dispose() {
    if (_handle != nullptr && _session && _session->isConnected()) {
        auto smb = _session->getContext();
        if (smb) {
            smb2_close(smb, _handle);
        }
        _handle = nullptr;
    }
    _session.reset();
}

void SMBHandle::obtain(std::string m_path, int smb_mode) {
    // Parse the URL to extract all components
    // Expected format: smb://[user[:password]@]server/share/path/to/file
    auto parser = PeoplesUrlParser::parseURL(m_path);
    if (!parser || parser->scheme != "smb") {
        Debug_printv("Invalid SMB URL: %s", m_path.c_str());
        dispose();
        return;
    }

    std::string server = parser->host;
    std::string user = parser->user;
    std::string password = parser->password;
    uint16_t smb_port = parser->port.empty() ? 445 : std::stoi(parser->port);
    std::string share, filepath;

    if (!parseSMBPath(parser->path, share, filepath)) {
        Debug_printv("Invalid SMB path: %s", parser->path.c_str());
        dispose();
        return;
    }

    Debug_printv("Connecting to server[%s] share[%s] filepath[%s] user[%s]",
                 server.c_str(), share.c_str(), filepath.c_str(), user.c_str());

    // Obtain SMB session via SessionBroker
    _session = SessionBroker::obtain<SMBMSession>(server, smb_port);
    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain SMB session for %s:%d", server.c_str(), smb_port);
        dispose();
        return;
    }

    auto smb = _session->getContext();
    if (!smb) {
        Debug_printv("Failed to get SMB context");
        dispose();
        return;
    }

    // Open the file
    _handle = smb2_open(smb, filepath.c_str(), smb_mode);
    if (!_handle) {
        Debug_printv("Failed to open file %s: %s", filepath.c_str(), smb2_get_error(smb));
        dispose();
        return;
    }

    Debug_printv("Successfully opened SMB file: %s", m_path.c_str());
}
