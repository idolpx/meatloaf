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
        share_path = "/" + path.substr(pos + 1);
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
    if (!_smb) {
        return false;
    }

    return true;
}

bool SMBMFile::isDirectory()
{
    Debug_printv("path[%s] len[%d]", share_path.c_str(), share_path.size());
    if(share_path=="/" || share_path.empty())
        return true;

    if (!_smb) {
        return false;
    }

    struct smb2_stat_64 st;
    if (smb2_stat(_smb, std::string(basepath + share_path).c_str(), &st) < 0) {
        return false;
    }

    return st.smb2_type == SMB2_TYPE_DIRECTORY;
}

std::shared_ptr<MStream> SMBMFile::getSourceStream(std::ios_base::openmode mode)
{
    std::shared_ptr<MStream> istream = std::make_shared<SMBMStream>(path);
    istream->open(mode);
    return istream;
}

std::shared_ptr<MStream> SMBMFile::getDecodedStream(std::shared_ptr<MStream> is) {
    return is; // we don't have to process this stream in any way, just return the original stream
}

std::shared_ptr<MStream> SMBMFile::createStream(std::ios_base::openmode mode)
{
    std::shared_ptr<MStream> istream = std::make_shared<SMBMStream>(path);
    istream->open(mode);
    return istream;
}

time_t SMBMFile::getLastWrite()
{
    if (!_smb) {
        return 0;
    }

    struct smb2_stat_64 st;
    if (smb2_stat(_smb, std::string(basepath + share_path).c_str(), &st) < 0) {
        return 0;
    }

    return st.smb2_mtime;
}

time_t SMBMFile::getCreationTime()
{
    if (!_smb) {
        return 0;
    }

    struct smb2_stat_64 st;
    if (smb2_stat(_smb, std::string(basepath + share_path).c_str(), &st) < 0) {
        return 0;
    }

    return st.smb2_ctime;
}

bool SMBMFile::mkDir()
{
    if (m_isNull || !_smb) {
        return false;
    }

    int rc = smb2_mkdir(_smb, std::string(basepath + share_path).c_str());
    return (rc == 0);
}

bool SMBMFile::exists()
{
    if (m_isNull || !_smb) {
        return false;
    }
    if (share_path=="/" || share_path=="") {
        return true;
    }

    struct smb2_stat_64 st;
    int rc = smb2_stat(_smb, std::string(basepath + share_path).c_str(), &st);
    return (rc == 0);
}


bool SMBMFile::remove() {
    if (!_smb) {
        return false;
    }

    // Check if it's a directory or file
    if (isDirectory()) {
        return smb2_rmdir(_smb, std::string(basepath + share_path).c_str()) == 0;
    } else {
        return smb2_unlink(_smb, std::string(basepath + share_path).c_str()) == 0;
    }
}


bool SMBMFile::rename(std::string pathTo) {
    if(pathTo.empty() || !_smb) {
        return false;
    }

    int rc = smb2_rename(_smb, std::string(basepath + share_path).c_str(), std::string(basepath + pathTo).c_str());
    return (rc == 0);
}


void SMBMFile::openDir(std::string apath)
{
    if (!isDirectory() || !_smb) {
        dirOpened = false;
        return;
    }

    // Close any previously opened directory
    closeDir();

    // Open the directory for listing
    std::string dirPath = apath.empty() ? share_path : apath;
    _handle_dir = smb2_opendir(_smb, dirPath.c_str());
    dirOpened = (_handle_dir != nullptr);

    Debug_printv("openDir path[%s] opened[%d]", dirPath.c_str(), dirOpened);
}


void SMBMFile::closeDir()
{
    if(dirOpened && _handle_dir) {
        smb2_closedir(_smb, _handle_dir);
        _handle_dir = nullptr;
        dirOpened = false;
    }
}


bool SMBMFile::rewindDirectory()
{
    if (!_smb || !dirOpened) {
        return false;
    }

    // Close and reopen directory to reset position
    closeDir();
    openDir(share_path);

    return dirOpened;
}


MFile* SMBMFile::getNextFileInDir()
{
    if(!dirOpened || !_handle_dir || !_smb) {
        openDir(std::string(basepath + share_path).c_str());
        if (!dirOpened) {
            return nullptr;
        }
    }

    struct smb2dirent *ent;
    do {
        ent = smb2_readdir(_smb, _handle_dir);
        if (ent == nullptr) {
            closeDir();
            return nullptr;
        }
        // Skip hidden files and current/parent directory entries
    } while (ent->name[0] == '.' && (ent->name[1] == '\0' || (ent->name[1] == '.' && ent->name[2] == '\0')));

    if (ent != nullptr) {

        Debug_printv("url[%s] entry[%s]", mRawUrl.c_str(), ent->name);

        auto file = new SMBMFile(url + "/" + ent->name);
        file->extension = " " + file->extension;

        // Set size and type information
        if (ent->st.smb2_type == SMB2_TYPE_DIRECTORY) {
            file->size = 0;
        } else {
            file->size = ent->st.smb2_size;
        }

        return file;
    }

    closeDir();
    return nullptr;
}


bool SMBMFile::readEntry( std::string filename )
{
    if (!_smb || filename.empty()) {
        return false;
    }

    std::string searchPath = share_path.substr(0, share_path.find_last_of('/'));
    if (searchPath.empty()) {
        searchPath = "/";
    }

    Debug_printv( "path[%s] filename[%s]", searchPath.c_str(), filename.c_str());

    struct smb2dir* dirHandle = smb2_opendir(_smb, searchPath.c_str());
    if (dirHandle == nullptr) {
        return false;
    }

    struct smb2dirent *ent;
    bool found = false;

    while ((ent = smb2_readdir(_smb, dirHandle)) != nullptr) {
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

    smb2_closedir(_smb, dirHandle);

    if (!found) {
        Debug_printv( "Not Found! file[%s]", filename.c_str() );
    }

    return found;
}



/********************************************************
 * MStream implementations
 ********************************************************/
uint32_t SMBMStream::write(const uint8_t *buf, uint32_t size) {
    if (!isOpen() || !buf || !handle->_smb || !handle->_handle) {
        return 0;
    }

    int result = smb2_write(handle->_smb, handle->_handle, buf, size);

    if (result < 0) {
        Debug_printv("write rc=%d\r\n", result);
        return 0;
    }

    _position += result;
    if (_position > _size) {
        _size = _position;
    }

    return result;
};


/********************************************************
 * MIStreams implementations
 ********************************************************/


bool SMBMStream::open(std::ios_base::openmode mode) {
    if(isOpen())
        return true;

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

    handle->obtain(localPath, smb_mode);

    if(isOpen()) {
        // Get file size using SMB2 stat
        struct smb2_stat_64 st;
        if (smb2_fstat(handle->_smb, handle->_handle, &st) == 0) {
            _size = st.smb2_size;
        }
        return true;
    }
    return false;
};

void SMBMStream::close() {
    if(isOpen()) {
        handle->dispose();
        _position = 0;
    }
};

uint32_t SMBMStream::read(uint8_t* buf, uint32_t size) {
    if (!isOpen() || !buf || !handle->_smb || !handle->_handle) {
        Debug_printv("Not open or invalid handles");
        return 0;
    }

    int bytesRead = smb2_read(handle->_smb, handle->_handle, buf, size);

    if (bytesRead < 0) {
        Debug_printv("read rc=%d\r\n", bytesRead);
        return 0;
    }

    _position += bytesRead;
    return bytesRead;
};

bool SMBMStream::seek(uint32_t pos) {
    if (!isOpen() || !handle->_smb || !handle->_handle) {
        Debug_printv("Not open");
        return false;
    }

    int64_t result = smb2_lseek(handle->_smb, handle->_handle, pos, SEEK_SET, NULL);
    if (result < 0) {
        return false;
    }

    _position = pos;
    return true;
};

bool SMBMStream::seek(uint32_t pos, int mode) {
    if (!isOpen() || !handle->_smb || !handle->_handle) {
        Debug_printv("Not open");
        return false;
    }

    int64_t result = smb2_lseek(handle->_smb, handle->_handle, pos, mode, NULL);
    if (result < 0) {
        return false;
    }

    // Update position based on seek mode
    switch(mode) {
        case SEEK_SET:
            _position = pos;
            break;
        case SEEK_CUR:
            _position += pos;
            break;
        case SEEK_END:
            _position = _size + pos;
            break;
    }

    return true;
}

bool SMBMStream::isOpen() {
    return handle != nullptr && handle->_smb != nullptr && handle->_handle != nullptr;
}

/********************************************************
 * SMBHandle implementations
 ********************************************************/


SMBHandle::~SMBHandle() {
    dispose();
}

void SMBHandle::dispose() {
    if (_handle != nullptr && _smb != nullptr) {
        smb2_close(_smb, _handle);
        _handle = nullptr;
    }
    if (_smb != nullptr) {
        smb2_destroy_context(_smb);
        _smb = nullptr;
    }
}

void SMBHandle::obtain(std::string m_path, int smb_mode) {
    dispose(); // Clean up any existing connection

    // Create SMB2 context
    _smb = smb2_init_context();
    if (!_smb) {
        Debug_printv("Failed to init SMB2 context");
        return;
    }

    // Parse the path to extract server, share, and file path
    // Expected format: smb://server/share/path/to/file
    std::string server, share, filepath;
    if (!parseSMBPath(m_path, share, filepath)) {
        Debug_printv("Invalid SMB path: %s", m_path.c_str());
        dispose();
        return;
    }

    // Set SMB2 version
    smb2_set_version(_smb, SMB2_VERSION_ANY);

    // Connect to server
    if (smb2_connect_share(_smb, server.c_str(), share.c_str(), nullptr) < 0) {
        Debug_printv("Failed to connect to %s/%s: %s", server.c_str(), share.c_str(), smb2_get_error(_smb));
        dispose();
        return;
    }

    // Open the file
    _handle = smb2_open(_smb, filepath.c_str(), smb_mode);
    if (!_handle) {
        Debug_printv("Failed to open file %s: %s", filepath.c_str(), smb2_get_error(_smb));
        dispose();
        return;
    }

    Debug_printv("Successfully opened SMB file: %s", m_path.c_str());
}
