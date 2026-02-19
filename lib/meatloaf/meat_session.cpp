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

#include <archive.h>

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

/********************************************************
 * CachedFile Implementation
 ********************************************************/

MSession::CachedFile::CachedFile(uint32_t s)
    : size(s), dirty(false), m_data(nullptr)
{
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    m_useHimem = false;
    memset(&m_handle, 0, sizeof(m_handle));
#endif
}

MSession::CachedFile::CachedFile(uint8_t* d, uint32_t s)
    : size(s), dirty(false), m_data(d)
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
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    if (m_useHimem) return true;
#endif
    return m_data != nullptr;
}

uint32_t MSession::CachedFile::read(uint32_t offset, uint8_t* buf, uint32_t count) {
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

bool MSession::CachedFile::loadFromArchive(struct archive* a, uint32_t fileSize) {
    size = fileSize;
    if (!allocate()) return false;

    Debug_printv("Reading %u bytes from archive", fileSize);

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    if (m_useHimem) {
        uint32_t remaining = fileSize;
        uint32_t pageStart = 0;
        while (remaining > 0) {
            uint32_t s = std::min(remaining, (uint32_t)ESP_HIMEM_BLKSZ);

            uint8_t* ptr;
            ESP_ERROR_CHECK(esp_himem_map(m_handle, s_range, pageStart, 0, ESP_HIMEM_BLKSZ, 0, (void**)&ptr));
            uint32_t r = archive_read_data(a, ptr, s);
            ESP_ERROR_CHECK(esp_himem_unmap(s_range, ptr, ESP_HIMEM_BLKSZ));

            if (archive_errno(a) != ARCHIVE_OK || r != s) {
                if (archive_errno(a) != ARCHIVE_OK) {
                    Debug_printv("archive read error %i: %s", archive_errno(a), archive_error_string(a));
                } else {
                    Debug_printv("expected to read %u bytes from archive, got %u", s, r);
                }
                freeStorage();
                return false;
            }

            pageStart += s;
            remaining -= s;
        }
        return true;
    }
#endif
    uint32_t r = archive_read_data(a, m_data, fileSize);
    if (archive_errno(a) != ARCHIVE_OK || r != fileSize) {
        if (archive_errno(a) != ARCHIVE_OK) {
            Debug_printv("archive read error %i: %s", archive_errno(a), archive_error_string(a));
        } else {
            Debug_printv("expected to read %u bytes from archive, got %u", fileSize, r);
        }
        freeStorage();
        return false;
    }
    return true;
}
