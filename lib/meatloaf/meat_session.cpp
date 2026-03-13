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

#include "meat_session.h"

#include "meatloaf.h"

#include <sys/stat.h>
#include <cstdio>
#include <cerrno>

// Initialize static members — SessionBroker
std::unordered_map<std::string, std::shared_ptr<MSession>> SessionBroker::session_repo;
std::chrono::steady_clock::time_point SessionBroker::last_keep_alive_check = std::chrono::steady_clock::now();
bool SessionBroker::task_running = false;
bool SessionBroker::system_shutdown = false;
SemaphoreHandle_t SessionBroker::_mutex = nullptr;

// Initialize static members — CachedFile HIMEM
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
esp_himem_rangehandle_t MSession::CachedFile::s_range;
int MSession::CachedFile::s_rangeUsed = 0;
#endif

// Initialize static members — CachedFile RAM cache
std::unordered_map<std::string, std::shared_ptr<MSession::CachedFile>> MSession::CachedFile::s_ramCache;

/********************************************************
 * CachedFileStream — RAM-backed CachedFile as an MStream
 ********************************************************/

namespace {
class CachedFileStream : public MStream {
public:
    CachedFileStream(std::shared_ptr<MSession::CachedFile> cf)
        : MStream(""), m_cf(cf)
    {
        _size = cf->size;
        _position = 0;
    }

    bool isOpen() override { return m_cf && m_cf->isAllocated(); }
    bool open(std::ios_base::openmode) override { return isOpen(); }
    void close() override {}
    bool isRandomAccess() override { return true; }
    bool isBrowsable() override { return false; }

    uint32_t read(uint8_t* buf, uint32_t count) override {
        if (!m_cf) return 0;
        uint32_t n = m_cf->read(_position, buf, count);
        _position += n;
        return n;
    }

    uint32_t write(const uint8_t* buf, uint32_t count) override {
        if (!m_cf) return 0;
        uint32_t n = m_cf->write(_position, buf, count);
        _position += n;
        return n;
    }

    bool seek(uint32_t pos) override {
        if (!m_cf || pos > _size) return false;
        _position = pos;
        return true;
    }

private:
    std::shared_ptr<MSession::CachedFile> m_cf;
};
} // namespace

/********************************************************
 * CachedFile SD helpers (file-scoped)
 ********************************************************/

static bool session_ensure_dir(const std::string& path) {
    if (path.empty()) return true;
    struct stat info;
    if (stat(path.c_str(), &info) == 0) return S_ISDIR(info.st_mode);
    if (mkdir(path.c_str(), 0755) == 0) return true;
    return errno == EEXIST;
}

static std::string session_parent_dir(const std::string& path) {
    size_t pos = path.rfind('/');
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
}

/********************************************************
 * CachedFile Implementation
 ********************************************************/

MSession::CachedFile::CachedFile(uint32_t s)
    : size(s), dirty(false), m_data(nullptr), m_store(Store::RAM)
{
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    m_useHimem = false;
    memset(&m_handle, 0, sizeof(m_handle));
#endif
}

MSession::CachedFile::CachedFile(uint8_t* d, uint32_t s)
    : size(s), dirty(false), m_data(d), m_store(Store::RAM)
{
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    m_useHimem = false;
    memset(&m_handle, 0, sizeof(m_handle));
#endif
}

MSession::CachedFile::~CachedFile() {
    freeStorage();
}

void MSession::CachedFile::freeStorage() {
    if (m_store == Store::SD) return;  // SD cache is persistent; don't delete on destruction
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    if (m_useHimem) {
        ESP_ERROR_CHECK(esp_himem_free(m_handle));
        Debug_printv("HIMEM available after free: %lu", (uint32_t)esp_himem_get_free_size());

        if (s_rangeUsed > 0) s_rangeUsed--;
        if (s_rangeUsed == 0) esp_himem_free_map_range(s_range);
        m_useHimem = false;
        return;
    }
#endif
    if (m_data) {
        free(m_data);
        m_data = nullptr;
    }
}

bool MSession::CachedFile::allocate() {
    if (m_store == Store::SD) {
        // For SD, just ensure the parent directory exists
        std::string parentDir = session_parent_dir(m_sdPath);
        return session_ensure_dir(parentDir);
    }
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    // Try HIMEM first
    uint32_t himemSize = (size / ESP_HIMEM_BLKSZ) * ESP_HIMEM_BLKSZ;
    if (size > himemSize) himemSize += ESP_HIMEM_BLKSZ;

    Debug_printv("HIMEM physical size: %lu", (uint32_t)esp_himem_get_phys_size());
    Debug_printv("HIMEM available before alloc: %lu", (uint32_t)esp_himem_get_free_size());

    esp_err_t status = esp_himem_alloc(himemSize, &m_handle);
    if (status == ESP_OK) {
        // Allocate mapped range if not yet created
        if (s_rangeUsed == 0) {
            status = esp_himem_alloc_map_range(ESP_HIMEM_BLKSZ, &s_range);
            if (status != ESP_OK) {
                Debug_printv("Unable to allocate mapped range for HIMEM: %s", esp_err_to_name(status));
                ESP_ERROR_CHECK(esp_himem_free(m_handle));
                // Fall through to heap allocation
                goto heap_alloc;
            }
        }
        s_rangeUsed++;
        m_useHimem = true;
        Debug_printv("HIMEM available after alloc: %lu", (uint32_t)esp_himem_get_free_size());
        return true;
    } else {
        Debug_printv("Unable to allocate HIMEM memory: %s, falling back to heap", esp_err_to_name(status));
    }

heap_alloc:
#endif
    m_data = (uint8_t*)malloc(size);
    if (!m_data) {
        Debug_printv("Failed to allocate %u bytes on heap", size);
        return false;
    }
    return true;
}

bool MSession::CachedFile::isAllocated() const {
    if (m_store == Store::SD) {
        if (m_sdPath.empty()) return false;
        struct stat info;
        return stat(m_sdPath.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    }
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    if (m_useHimem) return true;
#endif
    return m_data != nullptr;
}

uint32_t MSession::CachedFile::read(uint32_t offset, uint8_t* buf, uint32_t count) {
    if (m_store == Store::SD) {
        if (m_sdPath.empty() || count == 0) return 0;
        FILE* f = fopen(m_sdPath.c_str(), "rb");
        if (!f) return 0;
        if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return 0; }
        size_t r = fread(buf, 1, count, f);
        fclose(f);
        return (uint32_t)r;
    }
    if (offset >= size) return 0;
    if (offset + count > size) count = size - offset;
    if (count == 0) return 0;

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    if (m_useHimem) {
        uint32_t numRead = 0;
        while (count > 0) {
            uint32_t pageStart = (offset / ESP_HIMEM_BLKSZ) * ESP_HIMEM_BLKSZ;
            uint32_t pageOffset = offset - pageStart;
            uint32_t n = std::min(count, (uint32_t)ESP_HIMEM_BLKSZ - pageOffset);

            uint8_t* ptr;
            ESP_ERROR_CHECK(esp_himem_map(m_handle, s_range, pageStart, 0, ESP_HIMEM_BLKSZ, 0, (void**)&ptr));
            memcpy(buf + numRead, ptr + pageOffset, n);
            ESP_ERROR_CHECK(esp_himem_unmap(s_range, ptr, ESP_HIMEM_BLKSZ));

            count -= n;
            numRead += n;
            offset += n;
        }
        return numRead;
    }
#endif
    memcpy(buf, m_data + offset, count);
    return count;
}

uint32_t MSession::CachedFile::write(uint32_t offset, const uint8_t* buf, uint32_t count) {
    if (m_store == Store::SD) {
        if (m_sdPath.empty() || count == 0) return 0;
        // File must already exist (created by loadFromStream); open for update
        FILE* f = fopen(m_sdPath.c_str(), "r+b");
        if (!f) return 0;
        if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return 0; }
        size_t w = fwrite(buf, 1, count, f);
        fclose(f);
        uint32_t newEnd = offset + (uint32_t)w;
        if (newEnd > size) size = newEnd;
        dirty = true;
        return (uint32_t)w;
    }
    if (offset >= size) return 0;
    if (offset + count > size) count = size - offset;
    if (count == 0) return 0;

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    if (m_useHimem) {
        uint32_t numWritten = 0;
        while (count > 0) {
            uint32_t pageStart = (offset / ESP_HIMEM_BLKSZ) * ESP_HIMEM_BLKSZ;
            uint32_t pageOffset = offset - pageStart;
            uint32_t n = std::min(count, (uint32_t)ESP_HIMEM_BLKSZ - pageOffset);

            uint8_t* ptr;
            ESP_ERROR_CHECK(esp_himem_map(m_handle, s_range, pageStart, 0, ESP_HIMEM_BLKSZ, 0, (void**)&ptr));
            memcpy(ptr + pageOffset, buf + numWritten, n);
            ESP_ERROR_CHECK(esp_himem_unmap(s_range, ptr, ESP_HIMEM_BLKSZ));

            count -= n;
            numWritten += n;
            offset += n;
        }
        return numWritten;
    }
#endif
    memcpy(m_data + offset, buf, count);
    return count;
}

bool MSession::CachedFile::loadFromStream(MStream* stream, uint32_t fileSize) {
    if (m_store == Store::SD) {
        std::string parentDir = session_parent_dir(m_sdPath);
        if (!parentDir.empty() && !session_ensure_dir(parentDir)) {
            Debug_printv("Failed to create SD cache dir: %s", parentDir.c_str());
            return false;
        }
        FILE* f = fopen(m_sdPath.c_str(), "wb");
        if (!f) {
            Debug_printv("Failed to open SD cache file for writing: %s", m_sdPath.c_str());
            return false;
        }
        uint8_t buf[1024];
        uint32_t total = 0;
        uint32_t remaining = fileSize;
        while (true) {
            uint32_t toRead = (remaining > 0)
                ? std::min(remaining, (uint32_t)sizeof(buf))
                : (uint32_t)sizeof(buf);
            uint32_t r = stream->read(buf, toRead);
            if (r == 0) break;
            fwrite(buf, 1, r, f);
            total += r;
            if (remaining > 0) {
                remaining -= r;
                if (remaining == 0) break;
            }
        }
        fclose(f);
        size = total;
        Debug_printv("Cached %u bytes to SD: %s", total, m_sdPath.c_str());
        return total > 0 || fileSize == 0;
    }
    // RAM path: if fileSize is unknown (0), buffer into a vector first
    if (fileSize == 0) {
        std::vector<uint8_t> buf;
        uint8_t tmp[1024];
        while (true) {
            uint32_t r = stream->read(tmp, sizeof(tmp));
            if (r == 0) break;
            buf.insert(buf.end(), tmp, tmp + r);
        }
        if (buf.empty()) return false;
        size = (uint32_t)buf.size();
        if (!allocate()) return false;
        write(0, buf.data(), size);
        Debug_printv("Cached %u bytes to RAM", size);
        return true;
    }
    return loadViaReader(fileSize, [stream](uint8_t* buf, uint32_t n) {
        return stream->read(buf, n);
    });
}

std::shared_ptr<MSession::CachedFile> MSession::CachedFile::forSD(const std::string& sdPath) {
    auto cf = std::make_shared<CachedFile>(0u);
    cf->m_store = Store::SD;
    cf->m_sdPath = sdPath;
    return cf;
}

std::shared_ptr<MStream> MSession::CachedFile::openStream(std::ios_base::openmode mode) {
    if (m_store == Store::RAM) {
        if (!isAllocated()) return nullptr;
        return std::make_shared<CachedFileStream>(shared_from_this());
    }
    // SD-backed
    if (m_sdPath.empty()) return nullptr;
    std::unique_ptr<MFile> f(MFSOwner::File(m_sdPath));
    if (!f) return nullptr;
    auto stream = f->getSourceStream(mode);
    if (stream) size = stream->size();
    return stream;
}

std::shared_ptr<MSession::CachedFile> MSession::CachedFile::getRAMCached(const std::string& key) {
    auto it = s_ramCache.find(key);
    return (it != s_ramCache.end()) ? it->second : nullptr;
}

void MSession::CachedFile::setRAMCached(const std::string& key, std::shared_ptr<CachedFile> cf) {
    s_ramCache[key] = cf;
    Debug_printv("RAM cache stored: %s (%u bytes)", key.c_str(), cf->size);
}

void MSession::CachedFile::clearRAMCached(const std::string& key) {
    if (key.empty()) {
        Debug_printv("Clearing entire RAM cache (%zu entries)", s_ramCache.size());
        s_ramCache.clear();
    } else {
        s_ramCache.erase(key);
        Debug_printv("Cleared RAM cache for: %s", key.c_str());
    }
}
