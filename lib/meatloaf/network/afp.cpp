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

#include "afp.h"
#include "meatloaf.h"
#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

// Static member definitions
bool AFPMSession::_afp_initialized = false;
std::vector<std::string> AFPMFile::volumes;

// File-local AFP background thread handle.
static pthread_t s_afp_loop_thread;


/********************************************************
 * AFP library global initialisation
 *
 * afpfs-ng requires a background DSI event loop running in
 * its own thread before any server connections are made.
 * We start it exactly once, lazily, on the first connect.
 ********************************************************/

static void afp_log_for_client(void* /*priv*/,
                                enum loglevels /*level*/,
                                int /*logtype*/,
                                const char* message)
{
    Debug_printv("afpfs-ng: %s", message);
}

static struct libafpclient afp_client_callbacks = {
    /* unmount_volume    */ nullptr,
    /* log_for_client    */ afp_log_for_client,
    /* forced_ending_hook*/ nullptr,
    /* scan_extra_fds    */ nullptr,
    /* loop_started      */ nullptr,
};

static bool afp_global_init()
{
    init_uams();
    libafpclient_register(&afp_client_callbacks);
    if (afp_main_quick_startup(&s_afp_loop_thread) != 0) {
        Debug_printv("AFP: failed to start afpfs-ng main loop");
        return false;
    }
    afp_wait_for_started_loop();
    Debug_printv("AFP: main loop started");
    return true;
}


/********************************************************
 * Helper Functions
 ********************************************************/

// Parse an AFP path of the form "/VolumeName[/rest/of/path]"
// into volume_name and file_path (AFP-absolute, leading '/').
bool parseAFPPath(const std::string& path,
                  std::string& volume_name,
                  std::string& file_path)
{
    // path arrives with a leading '/' from PeoplesUrlParser
    size_t start = (path.size() > 0 && path[0] == '/') ? 1 : 0;
    size_t sep   = path.find('/', start);

    if (sep == std::string::npos) {
        // Only volume name, no file path
        volume_name = path.substr(start);
        file_path   = "/";
    } else {
        volume_name = path.substr(start, sep - start);
        file_path   = path.substr(sep);   // keeps leading '/'
        if (file_path.empty()) file_path = "/";
    }
    return true;
}


/********************************************************
 * AFPMSession implementations
 ********************************************************/

bool AFPMSession::connect()
{
    if (connected) return true;

    // One-time global init of the afpfs-ng DSI event loop.
    if (!_afp_initialized) {
        if (!afp_global_init()) {
            connected = false;
            return false;
        }
        _afp_initialized = true;
    }

    // Build connection request.
    struct afp_connection_request req;
    memset(&req, 0, sizeof(req));
    afp_default_url(&req.url);

    req.uam_mask            = default_uams_mask();
    req.url.protocol        = TCPIP;
    req.url.port            = (int)port;
    req.url.requested_version = AFP_MAX_SUPPORTED_VERSION;

    strncpy(req.url.servername, host.c_str(), sizeof(req.url.servername) - 1);
    if (!_user.empty())
        strncpy(req.url.username, _user.c_str(), sizeof(req.url.username) - 1);
    if (!_password.empty())
        strncpy(req.url.password, _password.c_str(), sizeof(req.url.password) - 1);

    _server = afp_server_full_connect(nullptr, &req);
    if (!_server) {
        Debug_printv("AFP: failed to connect to %s:%d", host.c_str(), (int)port);
        connected = false;
        return false;
    }

    // Refresh volume list from server.
    if (afp_getsrvrparms(_server) != 0) {
        Debug_printv("AFP: afp_getsrvrparms failed for %s", host.c_str());
        // Non-fatal — we may still connect; list will be empty.
    }

    connected = true;
    updateActivity();
    Debug_printv("AFP: connected to %s:%d (%d volumes)", host.c_str(), (int)port,
                 (int)_server->num_volumes);
    return true;
}

void AFPMSession::disconnect()
{
    if (!_server) {
        connected = false;
        return;
    }

    // Unmount all mounted volumes.
    afp_unmount_all_volumes(_server);
    _mounted_volumes.clear();

    // Log out and free server resources.
    afp_logout(_server, 1 /*wait*/);
    afp_free_server(&_server);
    _server = nullptr;

    connected = false;
    Debug_printv("AFP: disconnected from %s:%d", host.c_str(), (int)port);
}

bool AFPMSession::keep_alive()
{
    if (!connected || !_server) return false;
    updateActivity();
    return true;
}

struct afp_volume* AFPMSession::getVolume(const std::string& volume_name)
{
    if (!_server || volume_name.empty()) return nullptr;

    // Return cached, already-mounted volume.
    auto it = _mounted_volumes.find(volume_name);
    if (it != _mounted_volumes.end() && it->second != nullptr)
        return it->second;

    // Find volume by name in the server's volume array.
    struct afp_volume* vol = find_volume_by_name(_server, volume_name.c_str());
    if (!vol) {
        Debug_printv("AFP: volume '%s' not found on %s", volume_name.c_str(), host.c_str());
        return nullptr;
    }

    // Mount the volume.
    char mesg[256] = {};
    unsigned int mesg_len = 0;
    if (afp_connect_volume(vol, _server, mesg, &mesg_len, sizeof(mesg)) != 0) {
        Debug_printv("AFP: failed to mount volume '%s': %s", volume_name.c_str(), mesg);
        return nullptr;
    }

    _mounted_volumes[volume_name] = vol;
    Debug_printv("AFP: mounted volume '%s'", volume_name.c_str());
    return vol;
}

const std::vector<std::string>& AFPMSession::getVolumes()
{
    if (!_volumes_enumerated)
        enumerateVolumes();
    return _volumes_list;
}

void AFPMSession::enumerateVolumes()
{
    _volumes_list.clear();
    _volumes_enumerated = true;

    if (!_server) return;

    for (int i = 0; i < (int)_server->num_volumes; i++) {
        const char* vname = _server->volumes[i].volume_name_printable;
        if (vname && vname[0] != '\0') {
            Debug_printv("AFP: found volume: %s", vname);
            _volumes_list.push_back(vname);
        }
    }

    std::sort(_volumes_list.begin(), _volumes_list.end());
    Debug_printv("AFP: enumerated %zu volumes", _volumes_list.size());
}


/********************************************************
 * AFPMFile implementations
 ********************************************************/

bool AFPMFile::pathValid(std::string /*path*/)
{
    if (!_session || !_session->isConnected()) return false;
    // If a volume name is present we need a valid volume context.
    if (!volume_name.empty() && !_volume) return false;
    return true;
}

bool AFPMFile::isDirectory()
{
    if (is_dir > -1) return (bool)is_dir;

    // Root of server or root of volume = directory.
    if (file_path == "/" || file_path.empty()) return true;

    auto vol = getAFP();
    if (!vol) return false;

    struct stat st;
    if (ml_getattr(vol, file_path.c_str(), &st) != 0)
        return false;

    is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    return (bool)is_dir;
}

std::shared_ptr<MStream> AFPMFile::getSourceStream(std::ios_base::openmode mode)
{
    std::string requestUrl = buildRequestUrl();
    Debug_printv("AFP getSourceStream url[%s] mode[%d]", requestUrl.c_str(), (int)mode);
    return openStreamWithCache(
        requestUrl,
        mode,
        [](const std::string& openUrl, std::ios_base::openmode openMode) -> std::shared_ptr<MStream> {
            std::string mutableUrl = openUrl;
            auto stream = std::make_shared<AFPMStream>(mutableUrl);
            stream->open(openMode);
            return stream;
        });
}

std::shared_ptr<MStream> AFPMFile::getDecodedStream(std::shared_ptr<MStream> src)
{
    return src;
}

std::shared_ptr<MStream> AFPMFile::createStream(std::ios_base::openmode mode)
{
    auto stream = std::make_shared<AFPMStream>(url);
    stream->open(mode);
    return stream;
}

time_t AFPMFile::getLastWrite()
{
    auto vol = getAFP();
    if (!vol) return 0;
    struct stat st;
    if (ml_getattr(vol, file_path.c_str(), &st) != 0) return 0;
    return st.st_mtime;
}

time_t AFPMFile::getCreationTime()
{
    auto vol = getAFP();
    if (!vol) return 0;
    struct stat st;
    if (ml_getattr(vol, file_path.c_str(), &st) != 0) return 0;
    return st.st_ctime;
}

uint64_t AFPMFile::getAvailableSpace()
{
    auto vol = getAFP();
    if (!vol) return 0;

    struct statvfs sv;
    if (ml_statfs(vol, file_path.c_str(), &sv) != 0) {
        Debug_printv("AFP: ml_statfs failed");
        return 0;
    }
    return (uint64_t)sv.f_bavail * sv.f_bsize;
}

bool AFPMFile::mkDir()
{
    auto vol = getAFP();
    if (!vol) return false;
    return ml_mkdir(vol, file_path.c_str(), 0755) == 0;
}

bool AFPMFile::exists()
{
    if (m_isNull) return false;
    if (volume_name.empty()) return true;   // server root always exists

    auto vol = getAFP();
    if (!vol) return false;
    if (file_path == "/" || file_path.empty()) return true;

    struct stat st;
    return ml_getattr(vol, file_path.c_str(), &st) == 0;
}

bool AFPMFile::remove()
{
    auto vol = getAFP();
    if (!vol) return false;

    if (isDirectory())
        return ml_rmdir(vol, file_path.c_str()) == 0;
    return ml_unlink(vol, file_path.c_str()) == 0;
}

bool AFPMFile::rename(std::string dest)
{
    auto vol = getAFP();
    if (!vol || dest.empty()) return false;
    return ml_rename(vol, file_path.c_str(), dest.c_str()) == 0;
}

void AFPMFile::openDir(std::string apath)
{
    closeDir();

    if (!volume_name.empty()) {
        auto vol = getAFP();
        if (!vol) { dirOpened = false; return; }

        std::string dirPath = apath.empty() ? file_path : apath;
        if (dirPath.empty()) dirPath = "/";

        Debug_printv("AFP openDir: vol[%s] path[%s]", volume_name.c_str(), dirPath.c_str());
        if (ml_readdir(vol, dirPath.c_str(), &_dir_base) != 0) {
            Debug_printv("AFP: ml_readdir failed for %s", dirPath.c_str());
            _dir_base = nullptr;
        }
        _dir_iter = _dir_base;
        dirOpened = (_dir_base != nullptr);
    } else {
        // Server root — list volumes.
        dirOpened = true;
        entry_index = 0;
        volumes = _session->getVolumes();
    }
}

void AFPMFile::closeDir()
{
    if (_dir_base) {
        afp_ml_filebase_free(&_dir_base);
        _dir_base = nullptr;
        _dir_iter = nullptr;
    }
    dirOpened = false;
}

bool AFPMFile::rewindDirectory()
{
    if (!_session || !_session->isConnected()) return false;
    openDir(file_path);
    return dirOpened;
}

MFile* AFPMFile::getNextFileInDir()
{
    if (!dirOpened)
        rewindDirectory();
    if (!dirOpened) return nullptr;

    std::string ent_name;
    bool        ent_isdir = false;
    uint64_t    ent_size  = 0;

    if (!volume_name.empty()) {
        // Skip dot-entries.
        while (_dir_iter) {
            if (_dir_iter->name[0] != '.' ||
                (_dir_iter->name[1] != '\0' &&
                 !(_dir_iter->name[1] == '.' && _dir_iter->name[2] == '\0')))
                break;
            _dir_iter = _dir_iter->next;
        }

        if (!_dir_iter) {
            closeDir();
            return nullptr;
        }

        ent_name   = _dir_iter->name;
        ent_isdir  = (_dir_iter->isdir != 0);
        ent_size   = ent_isdir ? 0 : _dir_iter->size;
        _dir_iter  = _dir_iter->next;

    } else {
        // Volume enumeration at server root.
        if (entry_index >= (int)volumes.size()) {
            dirOpened = false;
            return nullptr;
        }
        ent_name  = volumes[entry_index++];
        ent_isdir = true;
        ent_size  = 0;
    }

    if (ent_name.empty()) {
        closeDir();
        return nullptr;
    }

    auto file = new AFPMFile(url + "/" + ent_name);
    file->name      = ent_name;
    file->extension = " " + file->extension;
    file->is_dir    = ent_isdir ? 1 : 0;
    file->size      = (uint32_t)ent_size;
    return file;
}

bool AFPMFile::readEntry(std::string filename)
{
    auto vol = getAFP();
    if (!vol || filename.empty()) return false;

    // Search in parent directory.
    std::string searchPath = file_path;
    size_t slash = searchPath.rfind('/');
    if (slash != std::string::npos && slash > 0)
        searchPath = searchPath.substr(0, slash);
    if (searchPath.empty()) searchPath = "/";

    Debug_printv("AFP readEntry: path[%s] filename[%s]", searchPath.c_str(), filename.c_str());

    struct afp_file_info* base = nullptr;
    if (ml_readdir(vol, searchPath.c_str(), &base) != 0 || !base)
        return false;

    bool found = false;
    for (struct afp_file_info* e = base; e; e = e->next) {
        std::string ename = e->name;
        if (filename == "*") {
            name = ename;
            rebuildUrl();
            found = true;
            break;
        } else if (mstr::compare(ename, filename)) {
            Debug_printv("AFP readEntry: matched '%s' -> '%s'", filename.c_str(), ename.c_str());
            name = ename;
            rebuildUrl();
            found = true;
            break;
        }
    }

    afp_ml_filebase_free(&base);
    if (!found) Debug_printv("AFP readEntry: not found '%s'", filename.c_str());
    return found;
}


/********************************************************
 * AFPMStream implementations
 ********************************************************/

bool AFPMStream::open(std::ios_base::openmode mode)
{
    if (isOpen()) return true;

    if (url.empty()) { _error = EINVAL; return false; }

    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->scheme != "afp") {
        Debug_printv("AFP: invalid URL '%s'", url.c_str());
        _error = EINVAL;
        return false;
    }

    std::string server = parser->host;
    uint16_t    afp_port = parser->port.empty() ? AFP_DEFAULT_PORT : std::stoi(parser->port);

    _session = SessionBroker::obtain<AFPMSession>(server, afp_port);
    if (!_session || !_session->isConnected()) {
        Debug_printv("AFP: no session for %s:%d", server.c_str(), (int)afp_port);
        _error = EACCES;
        return false;
    }

    // Credentials from URL.
    if (!parser->user.empty() || !parser->password.empty())
        _session->setCredentials(parser->user, parser->password);

    parseAFPPath(parser->path, _volume_name, _file_path);

    if (_volume_name.empty()) {
        Debug_printv("AFP open: no volume in URL");
        _error = EINVAL;
        return false;
    }
    if (_file_path == "/" || _file_path.empty()) {
        Debug_printv("AFP open: path resolves to volume root — not a file");
        _error = EISDIR;
        return false;
    }

    _volume = _session->getVolume(_volume_name);
    if (!_volume) {
        Debug_printv("AFP open: failed to get volume '%s'", _volume_name.c_str());
        _error = ENOENT;
        return false;
    }

    // Map openmode to POSIX flags.
    int flags = 0;
    if ((mode & std::ios_base::in) && (mode & std::ios_base::out))
        flags = O_RDWR;
    else if (mode & std::ios_base::out) {
        flags = O_WRONLY;
        if (mode & std::ios_base::trunc)  flags |= O_CREAT | O_TRUNC;
        if (mode & std::ios_base::app)    flags |= O_APPEND;
    } else {
        flags = O_RDONLY;
    }

    Debug_printv("AFP open: vol[%s] path[%s] flags[%d]", _volume_name.c_str(), _file_path.c_str(), flags);

    if (ml_open(_volume, _file_path.c_str(), flags, &_fp) != 0 || !_fp) {
        Debug_printv("AFP: ml_open failed for '%s'", _file_path.c_str());
        _error = EACCES;
        _fp = nullptr;
        return false;
    }

    // Get file size via stat.
    struct stat st;
    if (ml_getattr(_volume, _file_path.c_str(), &st) == 0)
        _size = (uint32_t)st.st_size;
    else
        _size = 0;

    _position = 0;
    _eof      = 0;
    if (_session) _session->acquireIO();
    return true;
}

void AFPMStream::close()
{
    if (!isOpen()) return;

    ml_close(_volume, _file_path.c_str(), _fp);
    _fp     = nullptr;
    _volume = nullptr;
    _position = 0;
    _size     = 0;
    _eof      = 0;

    if (_session) _session->releaseIO();
}

bool AFPMStream::isOpen()
{
    return _session && _session->isConnected() && _volume && _fp;
}

uint32_t AFPMStream::read(uint8_t* buf, uint32_t size)
{
    if (!buf || size == 0) return 0;
    if (!isOpen()) { _error = EBADF; return 0; }
    if (_eof) return 0;

    char* cbuf = reinterpret_cast<char*>(buf);
    int rc = ml_read(_volume, _file_path.c_str(), cbuf, size, (off_t)_position, _fp, &_eof);

    if (rc < 0) {
        Debug_printv("AFP: ml_read error %d", rc);
        _error = EIO;
        return 0;
    }

    _position += (uint32_t)rc;
    return (uint32_t)rc;
}

uint32_t AFPMStream::write(const uint8_t* buf, uint32_t size)
{
    if (!buf || size == 0) return 0;
    if (!isOpen()) { _error = EBADF; return 0; }

    const char* cbuf = reinterpret_cast<const char*>(buf);
    int rc = ml_write(_volume, _file_path.c_str(), cbuf, size, (off_t)_position,
                      _fp, 0, 0);
    if (rc < 0) {
        Debug_printv("AFP: ml_write error %d", rc);
        _error = EIO;
        return 0;
    }

    _position += (uint32_t)rc;
    if (_position > _size) _size = _position;
    return (uint32_t)rc;
}

bool AFPMStream::seek(uint32_t pos)
{
    if (!isOpen()) { _error = EBADF; return false; }
    if (pos > _size) return false;
    _position = pos;
    _eof = 0;
    return true;
}

bool AFPMStream::seek(uint32_t pos, int mode)
{
    if (!isOpen()) { _error = EBADF; return false; }

    uint32_t newpos;
    if (mode == SEEK_SET) {
        newpos = pos;
    } else if (mode == SEEK_CUR) {
        newpos = _position + pos;
    } else if (mode == SEEK_END) {
        newpos = _size + pos;
    } else {
        return false;
    }

    if (newpos > _size) return false;
    _position = newpos;
    _eof = 0;
    return true;
}


/********************************************************
 * AFPHandle implementations
 ********************************************************/

AFPHandle::~AFPHandle()
{
    dispose();
}

void AFPHandle::dispose()
{
    if (_fp && _session && _session->isConnected()) {
        auto vol = getVolume();
        if (vol) ml_close(vol, _file_path.c_str(), _fp);
    }
    _fp = nullptr;
    _session.reset();
}

struct afp_volume* AFPHandle::getVolume()
{
    if (!_session || !_session->isConnected() || _volume_name.empty()) return nullptr;
    return _session->getVolume(_volume_name);
}

void AFPHandle::obtain(std::string m_path, int flags)
{
    auto parser = PeoplesUrlParser::parseURL(m_path);
    if (!parser || parser->scheme != "afp") {
        Debug_printv("AFP: invalid URL '%s'", m_path.c_str());
        dispose();
        return;
    }

    std::string server = parser->host;
    uint16_t afp_port = parser->port.empty() ? AFP_DEFAULT_PORT : std::stoi(parser->port);

    _session = SessionBroker::obtain<AFPMSession>(server, afp_port);
    if (!_session || !_session->isConnected()) {
        Debug_printv("AFP: no session for %s:%d", server.c_str(), (int)afp_port);
        dispose();
        return;
    }

    parseAFPPath(parser->path, _volume_name, _file_path);

    auto vol = getVolume();
    if (!vol) {
        Debug_printv("AFP: failed to get volume '%s'", _volume_name.c_str());
        dispose();
        return;
    }

    if (ml_open(vol, _file_path.c_str(), flags, &_fp) != 0 || !_fp) {
        Debug_printv("AFP: ml_open failed for '%s'", _file_path.c_str());
        _fp = nullptr;
        dispose();
    }
}
