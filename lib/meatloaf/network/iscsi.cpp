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

#include "iscsi.h"
#include "meatloaf.h"
#include "../../../include/debug.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

// -----------------------------------------------------------------------
// Initiator IQN used for all connections from Meatloaf
// -----------------------------------------------------------------------
static constexpr const char* MEATLOAF_INITIATOR = "iqn.2005-03.org.meatloaf:initiator.1";


// -----------------------------------------------------------------------
// parseISCSIPath
// -----------------------------------------------------------------------
bool parseISCSIPath(const std::string& path, std::string& target_iqn, int& lun_number)
{
    target_iqn  = "";
    lun_number  = -1;

    // Strip leading '/'
    std::string p = path;
    if (!p.empty() && p[0] == '/') {
        p = p.substr(1);
    }

    // Strip trailing '/'
    while (!p.empty() && p.back() == '/') {
        p.pop_back();
    }

    if (p.empty()) {
        return true;  // Root: list targets
    }

    // Find the separator between target IQN and LUN
    auto sep = p.find('/');
    if (sep == std::string::npos) {
        // Only a target IQN, no LUN: list LUNs for this target
        target_iqn = p;
        return true;
    }

    target_iqn = p.substr(0, sep);
    std::string lun_str = p.substr(sep + 1);

    if (!lun_str.empty()) {
        char* end = nullptr;
        long val = std::strtol(lun_str.c_str(), &end, 10);
        lun_number = (end != lun_str.c_str()) ? (int)val : 0;
    }

    return true;
}


/********************************************************
 * ISCSIMSession
 ********************************************************/

void ISCSIMSession::enumerateTargets()
{
    _targets_list.clear();
    _targets_enumerated = true;

    struct iscsi_context* ctx = iscsi_create_context(MEATLOAF_INITIATOR);
    if (!ctx) {
        Debug_printv("Failed to create iSCSI discovery context for %s", portal().c_str());
        return;
    }

    iscsi_set_session_type(ctx, ISCSI_SESSION_DISCOVERY);
    iscsi_set_header_digest(ctx, ISCSI_HEADER_DIGEST_NONE_CRC32C);

    if (iscsi_connect_sync(ctx, portal().c_str()) != 0) {
        Debug_printv("iSCSI discovery connect to %s failed: %s", portal().c_str(), iscsi_get_error(ctx));
        iscsi_destroy_context(ctx);
        return;
    }

    if (iscsi_login_sync(ctx) != 0) {
        Debug_printv("iSCSI discovery login to %s failed: %s", portal().c_str(), iscsi_get_error(ctx));
        iscsi_disconnect(ctx);
        iscsi_destroy_context(ctx);
        return;
    }

    struct iscsi_discovery_address* da = iscsi_discovery_sync(ctx);
    if (da) {
        for (struct iscsi_discovery_address* a = da; a; a = a->next) {
            if (a->target_name) {
                Debug_printv("Discovered iSCSI target: %s", a->target_name);
                _targets_list.push_back(a->target_name);
            }
        }
        iscsi_free_discovery_data(ctx, da);
    } else {
        Debug_printv("iSCSI discovery returned no targets from %s: %s",
                     portal().c_str(), iscsi_get_error(ctx));
    }

    iscsi_logout_sync(ctx);
    iscsi_disconnect(ctx);
    iscsi_destroy_context(ctx);

    std::sort(_targets_list.begin(), _targets_list.end());
    Debug_printv("iSCSI discovery complete: %zu targets at %s", _targets_list.size(), portal().c_str());
}


/********************************************************
 * ISCSIMFile – helpers
 ********************************************************/

void ISCSIMFile::openDir(std::string /*apath*/)
{
    _dir_index       = 0;
    _luns_enumerated = false;
    _lun_list.clear();

    if (target_iqn.empty()) {
        // Root: use discovery
        _dirOpened = true;
    } else {
        // Target level: enumerate LUNs
        enumerateLuns();
        _dirOpened = true;
    }
}

void ISCSIMFile::closeDir()
{
    _dirOpened       = false;
    _dir_index       = 0;
    _lun_list.clear();
    _luns_enumerated = false;
}

// Connect to the target on LUN 0 and run REPORT LUNS to discover available LUNs.
// Falls back to a single LUN 0 if REPORT LUNS fails.
void ISCSIMFile::enumerateLuns()
{
    if (_luns_enumerated) return;
    _luns_enumerated = true;
    _lun_list.clear();

    if (target_iqn.empty() || !_session) return;

    struct iscsi_context* ctx = iscsi_create_context(MEATLOAF_INITIATOR);
    if (!ctx) return;

    // Set CHAP credentials if provided in the URL
    if (!user.empty()) {
        std::string chap_user = user;
        std::string chap_pass = password;
        // iSCSI URL uses % to separate user from password
        auto pct = user.find('%');
        if (pct != std::string::npos) {
            chap_user = user.substr(0, pct);
            chap_pass = user.substr(pct + 1);
        }
        if (!chap_user.empty()) {
            iscsi_set_initiator_username_pwd(ctx, chap_user.c_str(), chap_pass.c_str());
        }
    }

    iscsi_set_session_type(ctx, ISCSI_SESSION_NORMAL);
    iscsi_set_targetname(ctx, target_iqn.c_str());
    iscsi_set_header_digest(ctx, ISCSI_HEADER_DIGEST_NONE_CRC32C);

    // Connect to LUN 0 to run REPORT LUNS
    if (iscsi_full_connect_sync(ctx, _session->portal().c_str(), 0) != 0) {
        Debug_printv("Cannot enumerate LUNs for %s: %s", target_iqn.c_str(), iscsi_get_error(ctx));
        iscsi_destroy_context(ctx);
        // Fall back: assume LUN 0 exists
        _lun_list.push_back(0);
        return;
    }

    // REPORT LUNS (report type 0, allocate up to 4096 bytes = 511 LUNs)
    struct scsi_task* task = iscsi_reportluns_sync(ctx, 0, 4096);
    if (task && task->status == SCSI_STATUS_GOOD) {
        struct scsi_reportluns_list* luns =
            reinterpret_cast<struct scsi_reportluns_list*>(scsi_datain_unmarshall(task));
        if (luns) {
            for (uint32_t i = 0; i < luns->num; i++) {
                Debug_printv("Found LUN %u on target %s", luns->luns[i], target_iqn.c_str());
                _lun_list.push_back(luns->luns[i]);
            }
        }
        scsi_free_scsi_task(task);
    } else {
        if (task) scsi_free_scsi_task(task);
        Debug_printv("REPORT LUNS failed for %s, defaulting to LUN 0", target_iqn.c_str());
        _lun_list.push_back(0);
    }

    iscsi_logout_sync(ctx);
    iscsi_destroy_context(ctx);

    if (_lun_list.empty()) {
        _lun_list.push_back(0);
    }

    std::sort(_lun_list.begin(), _lun_list.end());
}


/********************************************************
 * ISCSIMFile – MFile interface
 ********************************************************/

bool ISCSIMFile::isDirectory()
{
    if (is_dir > -1) return (bool)is_dir;
    // Root or target-level paths are directories; LUN paths are files
    return (lun_number < 0);
}

std::shared_ptr<MStream> ISCSIMFile::getSourceStream(std::ios_base::openmode mode)
{
    std::string requestUrl = buildRequestUrl();
    Debug_printv("url[%s] mode[%d]", requestUrl.c_str(), mode);
    return openStreamWithCache(
        requestUrl,
        mode,
        [](const std::string& openUrl, std::ios_base::openmode openMode) -> std::shared_ptr<MStream> {
            std::string mutableUrl = openUrl;
            auto stream = std::make_shared<ISCSIMStream>(mutableUrl);
            stream->open(openMode);
            return stream;
        });
}

std::shared_ptr<MStream> ISCSIMFile::getDecodedStream(std::shared_ptr<MStream> is)
{
    return is;
}

std::shared_ptr<MStream> ISCSIMFile::createStream(std::ios_base::openmode mode)
{
    auto stream = std::make_shared<ISCSIMStream>(url);
    stream->open(mode);
    return stream;
}

uint64_t ISCSIMFile::getAvailableSpace()
{
    // Only meaningful for LUN-level entries; not implemented for directories
    return 0;
}

bool ISCSIMFile::exists()
{
    if (m_isNull) return false;
    if (!_session) return false;

    if (target_iqn.empty()) {
        return true;  // Root always exists if session is up
    }

    if (lun_number < 0) {
        // Target directory: check whether this IQN is in the discovery list
        const auto& targets = _session->getTargets();
        return std::find(targets.begin(), targets.end(), target_iqn) != targets.end();
    }

    // LUN: try a quick connect; if it succeeds the LUN exists
    struct iscsi_context* ctx = iscsi_create_context(MEATLOAF_INITIATOR);
    if (!ctx) return false;

    iscsi_set_session_type(ctx, ISCSI_SESSION_NORMAL);
    iscsi_set_targetname(ctx, target_iqn.c_str());
    iscsi_set_header_digest(ctx, ISCSI_HEADER_DIGEST_NONE_CRC32C);

    bool ok = (iscsi_full_connect_sync(ctx, _session->portal().c_str(), lun_number) == 0);
    if (ok) {
        iscsi_logout_sync(ctx);
    }
    iscsi_destroy_context(ctx);
    return ok;
}

bool ISCSIMFile::rewindDirectory()
{
    openDir(this->path);
    return _dirOpened;
}

MFile* ISCSIMFile::getNextFileInDir()
{
    if (!_dirOpened) rewindDirectory();
    if (!_dirOpened) return nullptr;

    if (target_iqn.empty()) {
        // Root: iterate discovered target IQNs
        const auto& targets = _session->getTargets();
        if (_dir_index >= (int)targets.size()) {
            closeDir();
            return nullptr;
        }
        const std::string& iqn = targets[_dir_index++];
        std::string entry_url  = url + "/" + iqn;
        auto* file = new ISCSIMFile(entry_url);
        file->is_dir = 1;
        file->size   = 0;
        file->name   = iqn;
        return file;
    }

    // Target level: iterate LUN numbers
    if (!_luns_enumerated) enumerateLuns();
    if (_dir_index >= (int)_lun_list.size()) {
        closeDir();
        return nullptr;
    }
    uint16_t lun = _lun_list[_dir_index++];
    std::string lun_str  = std::to_string(lun);
    std::string entry_url = url + "/" + lun_str;
    auto* file = new ISCSIMFile(entry_url);
    file->is_dir = 0;
    // Size is determined when the stream is opened; set 0 here
    file->size = 0;
    file->name = lun_str;
    return file;
}

bool ISCSIMFile::readEntry(std::string filename)
{
    // Not applicable for iSCSI (no filename-based lookup within a LUN)
    return false;
}


/********************************************************
 * ISCSIMStream – block-level I/O
 ********************************************************/

bool ISCSIMStream::readBlock(uint32_t lba)
{
    if (!_ctx) return false;

    struct scsi_task* task = iscsi_read10_sync(_ctx, _lun, lba,
                                               _block_size, (int)_block_size,
                                               0, 0, 0, 0, 0);
    if (!task) {
        Debug_printv("iscsi_read10_sync failed at LBA %u: %s", lba, iscsi_get_error(_ctx));
        return false;
    }
    if (task->status != SCSI_STATUS_GOOD) {
        Debug_printv("SCSI READ10 bad status %d at LBA %u", task->status, lba);
        scsi_free_scsi_task(task);
        return false;
    }

    uint32_t bytes = (uint32_t)task->datain.size;
    if (bytes < _block_size) {
        Debug_printv("Short read: got %u bytes, expected %u", bytes, _block_size);
        scsi_free_scsi_task(task);
        return false;
    }

    memcpy(_block_buf.data(), task->datain.data, _block_size);
    scsi_free_scsi_task(task);
    _buf_lba = (int64_t)lba;
    return true;
}

bool ISCSIMStream::writeBlock(uint32_t lba, const uint8_t* src)
{
    if (!_ctx || !src) return false;

    struct scsi_task* task = iscsi_write10_sync(_ctx, _lun, lba,
                                                (unsigned char*)src,
                                                _block_size, (int)_block_size,
                                                0, 0, 0, 0, 0);
    if (!task) {
        Debug_printv("iscsi_write10_sync failed at LBA %u: %s", lba, iscsi_get_error(_ctx));
        return false;
    }
    if (task->status != SCSI_STATUS_GOOD) {
        Debug_printv("SCSI WRITE10 bad status %d at LBA %u", task->status, lba);
        scsi_free_scsi_task(task);
        return false;
    }
    scsi_free_scsi_task(task);
    if (_buf_lba == (int64_t)lba) {
        memcpy(_block_buf.data(), src, _block_size);  // Keep cache consistent
    }
    return true;
}

bool ISCSIMStream::open(std::ios_base::openmode mode)
{
    if (isOpen()) return true;

    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || parser->scheme != "iscsi") {
        Debug_printv("Invalid iSCSI URL: %s", url.c_str());
        _error = EINVAL;
        return false;
    }

    std::string target_iqn;
    int lun_number;
    parseISCSIPath(parser->path, target_iqn, lun_number);

    if (target_iqn.empty() || lun_number < 0) {
        Debug_printv("iSCSI stream URL must specify target and LUN: %s", url.c_str());
        _error = EINVAL;
        return false;
    }

    _ctx = iscsi_create_context(MEATLOAF_INITIATOR);
    if (!_ctx) {
        Debug_printv("Failed to create iSCSI context");
        _error = ENOMEM;
        return false;
    }

    // CHAP credentials: iSCSI URL uses user%password notation
    std::string chap_user = parser->user;
    std::string chap_pass = parser->password;
    {
        auto pct = chap_user.find('%');
        if (pct != std::string::npos) {
            chap_pass = chap_user.substr(pct + 1);
            chap_user = chap_user.substr(0, pct);
        }
    }
    if (!chap_user.empty()) {
        iscsi_set_initiator_username_pwd(_ctx, chap_user.c_str(), chap_pass.c_str());
    }

    iscsi_set_session_type(_ctx, ISCSI_SESSION_NORMAL);
    iscsi_set_targetname(_ctx, target_iqn.c_str());
    iscsi_set_header_digest(_ctx, ISCSI_HEADER_DIGEST_NONE_CRC32C);

    std::string portal_str = parser->host + ":" +
                             (parser->port.empty() ? "3260" : parser->port);

    _lun = lun_number;
    if (iscsi_full_connect_sync(_ctx, portal_str.c_str(), _lun) != 0) {
        Debug_printv("iSCSI connect to %s lun %d failed: %s",
                     target_iqn.c_str(), _lun, iscsi_get_error(_ctx));
        iscsi_destroy_context(_ctx);
        _ctx   = nullptr;
        _error = EACCES;
        return false;
    }

    // Query LUN capacity
    struct scsi_task* task = iscsi_readcapacity10_sync(_ctx, _lun, 0, 0);
    if (task && task->status == SCSI_STATUS_GOOD) {
        struct scsi_readcapacity10* rc10 =
            reinterpret_cast<struct scsi_readcapacity10*>(scsi_datain_unmarshall(task));
        if (rc10 && rc10->block_size > 0) {
            _block_size = rc10->block_size;
            uint64_t num_blocks = (uint64_t)rc10->lba + 1;  // lba = last LBA index
            uint64_t byte_size  = num_blocks * _block_size;
            // Clamp to uint32_t max (~4 GB); sufficient for C64 disk images
            _size = (byte_size > UINT32_MAX) ? UINT32_MAX : (uint32_t)byte_size;
            Debug_printv("iSCSI LUN %d: %u blocks × %u bytes = %u total bytes",
                         _lun, (unsigned)num_blocks, _block_size, _size);
        }
        scsi_free_scsi_task(task);
    } else {
        if (task) scsi_free_scsi_task(task);
        Debug_printv("READCAPACITY10 failed for %s lun %d, size unknown", target_iqn.c_str(), _lun);
    }

    _block_buf.assign(_block_size, 0);
    _buf_lba   = -1;
    _position  = 0;
    _connected = true;
    return true;
}

void ISCSIMStream::close()
{
    if (!isOpen()) return;

    if (_ctx) {
        iscsi_logout_sync(_ctx);
        iscsi_destroy_context(_ctx);
        _ctx = nullptr;
    }
    _connected = false;
    _position  = 0;
    _size      = 0;
    _buf_lba   = -1;
    _block_buf.clear();
}

uint32_t ISCSIMStream::read(uint8_t* buf, uint32_t size)
{
    if (!isOpen() || !buf || size == 0) return 0;

    uint32_t total_read = 0;

    while (size > 0 && _position < _size) {
        uint32_t lba          = _position / _block_size;
        uint32_t block_offset = _position % _block_size;

        if ((int64_t)lba != _buf_lba) {
            if (!readBlock(lba)) {
                _error = EIO;
                break;
            }
        }

        uint32_t avail_in_block = _block_size - block_offset;
        uint32_t remaining_in_file = _size - _position;
        uint32_t to_copy = std::min({size, avail_in_block, remaining_in_file});

        memcpy(buf, _block_buf.data() + block_offset, to_copy);

        buf        += to_copy;
        _position  += to_copy;
        total_read += to_copy;
        size       -= to_copy;
    }

    return total_read;
}

uint32_t ISCSIMStream::write(const uint8_t* buf, uint32_t size)
{
    if (!isOpen() || !buf || size == 0) return 0;

    uint32_t total_written = 0;

    while (size > 0) {
        uint32_t lba          = _position / _block_size;
        uint32_t block_offset = _position % _block_size;

        if (block_offset == 0 && size >= _block_size) {
            // Fully aligned, whole-block write
            if (!writeBlock(lba, buf)) {
                _error = EIO;
                break;
            }
            buf            += _block_size;
            _position      += _block_size;
            total_written  += _block_size;
            size           -= _block_size;
        } else {
            // Partial block: read-modify-write
            if ((int64_t)lba != _buf_lba) {
                if (!readBlock(lba)) {
                    _error = EIO;
                    break;
                }
            }

            uint32_t avail_in_block = _block_size - block_offset;
            uint32_t to_copy = std::min(size, avail_in_block);

            memcpy(_block_buf.data() + block_offset, buf, to_copy);
            _buf_lba = (int64_t)lba;  // Buffer now contains the modified block

            if (!writeBlock(lba, _block_buf.data())) {
                _error = EIO;
                break;
            }

            buf           += to_copy;
            _position     += to_copy;
            total_written += to_copy;
            size          -= to_copy;
        }

        if (_position > _size) {
            _size = _position;
        }
    }

    return total_written;
}

bool ISCSIMStream::seek(uint32_t pos)
{
    if (pos > _size) return false;
    _position = pos;
    // Don't invalidate _buf_lba: the cached block may still be needed
    return true;
}

bool ISCSIMStream::seek(uint32_t pos, int whence)
{
    uint32_t new_pos;
    if (whence == SEEK_SET) {
        new_pos = pos;
    } else if (whence == SEEK_CUR) {
        new_pos = _position + pos;
    } else {  // SEEK_END
        new_pos = (_size >= pos) ? (_size - pos) : 0;
    }
    return seek(new_pos);
}
