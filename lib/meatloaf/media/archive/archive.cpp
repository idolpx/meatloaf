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

#include "archive.h"

#include <archive.h>
#include <archive_entry.h>
#include <string.h>

#include "meatloaf.h"

// HIMEM is only available on original ESP32 with SPIRAM (not S2, S3, C3, etc.)
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
// Include HIMEM allocator for LZMA decompression (.7z files)
#include <esp32_himem_allocator.h>
#define USE_ESP32_HIMEM 1
#endif

// int cb_open(struct archive *, void *userData)
// {
//     Archive *a = (Archive *) userData;

//     // maybe we can use open for something? Check if stream is open?
//     a->m_srcStream->seek(0, SEEK_CUR); // move to beginning of stream

//     return (ARCHIVE_OK);
// }

// int cb_close(struct archive *, void *userData)
// {
//     //ArchiveMStreamData *src_str = (ArchiveMStreamData *)userData;
    
//     //Debug_printv("Libarch wants to close, but we do nothing here...");

//     // do we want to close srcStream here???
//     return (ARCHIVE_OK);
// }

ssize_t cb_read(struct archive *, void *userData, const void **buff) {
    // Returns pointer and size of next block of data from archive.
    // The read callback returns the number of bytes read, zero for end-of-file,
    // or a negative failure code as above. It also returns a pointer to the
    // block of data read.
    // https://github.com/libarchive/libarchive/wiki/LibarchiveIO
    Archive *a = (Archive *)userData;
    *buff = a->m_srcBuffer;
    return a->m_archive==NULL ? 0 : a->m_srcStream->read(a->m_srcBuffer, a->m_buffSize);
}


int64_t cb_skip(struct archive *, void *userData, int64_t request)
{
    // It must return the number of bytes actually skipped, or a negative failure code if skipping cannot be done.
    // It can skip fewer bytes than requested but must never skip more.
    // Only positive/forward skips will ever be requested.
    // If skipping is not provided or fails, libarchive will call the read() function and simply ignore any data that it does not need.
    //
    // * Skips at most request bytes from archive and returns the skipped amount.
    // * This may skip fewer bytes than requested; it may even skip zero bytes.
    // * If you do skip fewer bytes than requested, libarchive will invoke your
    // * read callback and discard data as necessary to make up the full skip.
    //
    // https://github.com/libarchive/libarchive/wiki/LibarchiveIO
    Archive *a = (Archive *) userData;

    if (a->m_archive)
    {
        uint32_t old_pos = a->m_srcStream->position();
        bool rc = a->m_srcStream->seek(request, SEEK_CUR);
        if (rc) {
            // Return actual bytes skipped (may differ from request if seek is clamped)
            int64_t skipped = a->m_srcStream->position() - old_pos;
            Debug_printv("skip request[%lld] old_pos[%u] new_pos[%u] skipped[%lld]",
                         request, old_pos, a->m_srcStream->position(), skipped);
            return skipped;
        }
        Debug_printv("ERROR! skip failed: request[%lld]", request);
        return ARCHIVE_WARN;
    }
    else
    {
        Debug_printv("ERROR! skip failed - no archive");
        return ARCHIVE_FATAL;
    }
}


int64_t cb_seek(struct archive *, void *userData, int64_t offset, int whence)
{
    Archive *a = (Archive *) userData;

    if (a->m_archive)
    {
        bool rc = a->m_srcStream->seek(offset, whence);
        if (rc) {
            // Must return the resulting absolute position, not the offset
            // This is critical for .7z files which require accurate positioning
            int64_t pos = a->m_srcStream->position();
            Debug_printv("seek offset[%lld] whence[%d] -> pos[%lld]", offset, whence, pos);
            return pos;
        }
        Debug_printv("ERROR! seek failed: offset[%lld] whence[%d]", offset, whence);
        return ARCHIVE_WARN;
    }
    else
    {
        Debug_printv("ERROR! seek failed - no archive");
        return ARCHIVE_FATAL;
    }
}



bool Archive::open(std::ios_base::openmode mode) {
    // close the archive if it was already open
    close();

    Debug_printv("Archive::open [%s]", m_srcStream->url.c_str());

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
    // Initialize HIMEM allocator for LZMA decompression (.7z support)
    static bool himem_initialized = false;
    if (!himem_initialized) {
        esp32_himem_allocator_init();
        himem_initialized = true;
    }
#endif

    m_srcBuffer = new uint8_t[m_buffSize];
    m_archive = archive_read_new();
    m_srcStream->seek(0, SEEK_SET);

    archive_read_support_filter_all(m_archive);
    archive_read_support_format_all(m_archive);

    //archive_read_set_open_callback(m_archive, cb_open);
    //archive_read_set_close_callback(m_archive, cb_close);
    archive_read_set_read_callback(m_archive, cb_read);
    archive_read_set_skip_callback(m_archive, cb_skip);
    archive_read_set_seek_callback(m_archive, cb_seek);
    archive_read_set_callback_data(m_archive, this);

    Debug_printv("Calling archive_read_open1");
    int r = archive_read_open1(m_archive);
    if (r != ARCHIVE_OK) {
        Debug_printv("Error opening archive: %d! [%s]", r, archive_error_string(m_archive));
        archive_read_free(m_archive);
        m_archive = NULL;
    }

    return isOpen();
}

void Archive::close() {
    if (m_archive != NULL) {
        archive_read_close(m_archive);
        archive_read_free(m_archive);
        m_archive = NULL;
    }
}

/********************************************************
 * Streams implementations
 ********************************************************/

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
int ArchiveMStream::s_rangeUsed = 0;
esp_himem_rangehandle_t ArchiveMStream::s_range;
#endif

bool ArchiveMStream::open(std::ios_base::openmode mode) {
    m_mode = mode;
    return m_archive->open(mode);
}

void ArchiveMStream::close() {
    m_archive->close();

    if (m_haveData > 0) {
        if (m_dirty) {
            m_dirty = false;
        }

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
        ESP_ERROR_CHECK(esp_himem_free(m_data));
        Debug_printv("HIMEM available after free: %lu ", (uint32_t)esp_himem_get_free_size());

        // if this was the last archive in use then free up the mapped range
        if (s_rangeUsed > 0) s_rangeUsed--;
        if (s_rangeUsed == 0) esp_himem_free_map_range(s_range);
#else
        delete[] m_data;
#endif
    }

    m_haveData = 0;
}

bool ArchiveMStream::isOpen() { return m_archive->isOpen(); }

void ArchiveMStream::readArchiveData() {
    if (m_archive->isOpen() && m_haveData == 0) {

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
        // allocate HIMEM memory for archive data (size must be multiple of
        // ESP_HIMEM_BLKSZ);
        uint32_t size = (_size / ESP_HIMEM_BLKSZ) * ESP_HIMEM_BLKSZ;
        if (_size > size) size += ESP_HIMEM_BLKSZ;

        Debug_printv("HIMEM physical size: %lu", (uint32_t)esp_himem_get_phys_size());
        Debug_printv("HIMEM available before alloc: %lu ", (uint32_t)esp_himem_get_free_size());

        esp_err_t status = esp_himem_alloc(size, &m_data);
        if (status == ESP_OK)
            m_haveData = 1;
        else {
            Debug_printv("Unable to allocate HIMEM memory: %s", esp_err_to_name(status));
            m_haveData = -1;
            return;
        }

        // if mapped range is not yet created then create it now
        if (s_rangeUsed == 0) {
            esp_err_t status =
                esp_himem_alloc_map_range(ESP_HIMEM_BLKSZ, &s_range);
            if (status != ESP_OK) {
                Debug_printv("Unable to allocate mapped range for HIMEM: %s", esp_err_to_name(status));
                ESP_ERROR_CHECK(esp_himem_free(m_data));
                m_haveData = -1;
                return;
            }
        }

        Debug_printv("HIMEM available after alloc : %lu ", (uint32_t)esp_himem_get_free_size());

        // increment mapped range usage counter
        s_rangeUsed++;
#else
        m_data = new uint8_t[_size];
        m_haveData = 1;
#endif

        Debug_printv("reading %lu bytes from archive", _size);
        archive *a = m_archive->getArchive();

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
        size = _size;
        uint32_t pageStart = 0;
        while (size > 0) {
            
            uint32_t s = std::min(size, (uint32_t)ESP_HIMEM_BLKSZ);

            uint8_t *ptr;
            ESP_ERROR_CHECK(esp_himem_map(m_data, s_range, pageStart, 0, ESP_HIMEM_BLKSZ, 0, (void **)&ptr));
            uint32_t r = archive_read_data(a, ptr, s);
            ESP_ERROR_CHECK(esp_himem_unmap(s_range, ptr, ESP_HIMEM_BLKSZ));
            if (archive_errno(a) != ARCHIVE_OK || r != s) {
                if (archive_errno(a) != ARCHIVE_OK) {
                    Debug_printv("archive read error %i: %s", archive_errno(a), archive_error_string(a));
                } else {
                    Debug_printv(
                        "expected to read %lu bytes from archive, got %lu", s, r);
                }

                ESP_ERROR_CHECK(esp_himem_free(m_data));
                m_haveData = -1;
                return;
            }

            pageStart += s;
            size -= s;
        }
#else
        uint32_t r = archive_read_data(a, m_data, _size);
        if (archive_errno(a) != ARCHIVE_OK || r != _size) {
            if (archive_errno(a) != ARCHIVE_OK) {
                Debug_printv("archive read error %i: %s", archive_errno(a), archive_error_string(a));
            } else {
                Debug_printv("expected to read %lu bytes from archive, got %lu", _size, r);
            }
            delete[] m_data;
            m_haveData = -1;
            return;
        }
#endif
    }
}

uint32_t ArchiveMStream::read(uint8_t *buf, uint32_t size) {
    readArchiveData();

    if (m_haveData > 0) {
        // Debug_printv("calling read, buff size=[%ld]", size);

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
        if (_position + size > _size) size = _size - _position;
        uint32_t numRead = 0;
        while (size > 0) {

            uint32_t pageStart = (_position / ESP_HIMEM_BLKSZ) * ESP_HIMEM_BLKSZ;
            uint32_t offset = _position - pageStart;
            uint32_t n = std::min(size, (uint32_t)ESP_HIMEM_BLKSZ - offset);

            uint8_t *ptr;
            ESP_ERROR_CHECK(esp_himem_map(m_data, s_range, pageStart, 0, ESP_HIMEM_BLKSZ, 0, (void **)&ptr));
            memcpy(buf + numRead, ptr + offset, n);
            ESP_ERROR_CHECK(esp_himem_unmap(s_range, ptr, ESP_HIMEM_BLKSZ));
            size -= n;
            numRead += n;
            _position += n;
        }

        // Debug_printv("read [%lu] bytes", numRead);
        return numRead;

#else
        memcpy(buf, m_data + _position, size);
        _position += size;
        return size;
#endif
    } else
        return 0;
}

uint32_t ArchiveMStream::write(const uint8_t *buf, uint32_t size) {
    readArchiveData();

    // NOTE: this function can NOT write past the end of the extracted file,
    //       i.e. it can NOT extend the size of a file, only modify existing
    //       data However, most disk images (D64, D81, G64 etc) have a fixed
    //       size anyways.
    if (m_haveData > 0) {
        // Debug_printv("calling write, size=[%ld]", size);

        if (_position + size > _size) size = _size - _position;
        uint32_t numWritten = 0;

#if defined(CONFIG_IDF_TARGET_ESP32) && defined(CONFIG_SPIRAM)
        while (size > 0) {
            uint32_t pageStart = (_position / ESP_HIMEM_BLKSZ) * ESP_HIMEM_BLKSZ;
            uint32_t offset = _position - pageStart;
            uint32_t n = std::min(size, (uint32_t)ESP_HIMEM_BLKSZ - offset);

            uint8_t *ptr;
            ESP_ERROR_CHECK(esp_himem_map(m_data, s_range, pageStart, 0, ESP_HIMEM_BLKSZ, 0, (void **)&ptr));
            memcpy(ptr + offset, buf + numWritten, n);
            ESP_ERROR_CHECK(esp_himem_unmap(s_range, ptr, ESP_HIMEM_BLKSZ));
            size -= n;
            numWritten += n;
            _position += n;
        }
#else
        memcpy(m_data + _position, buf, size);
        _position += size;
        numWritten = size;
#endif

        // remember that data was written so we can re-zip the archive
        if (numWritten > 0) m_dirty = true;

        // Debug_printv("wrote [%lu] bytes", numWritten);
        return numWritten;
    } else
        return 0;
}

bool ArchiveMStream::seek(uint32_t pos) {
    readArchiveData();

    //Debug_printv("pos[%lu]", pos);

    if (m_haveData > 0) {
        if (pos < _size) {
            _position = pos;
            return true;
        } else
            return false;

    } else
        return false;
}

bool ArchiveMStream::seekEntry(std::string filename)
{
    // Read Directory Entries
    if (filename.size())
    {
        m_archive->open( std::ios_base::in );

        size_t index = 1;
        //mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (seekEntry(index))
        {
            std::string entryFilename = entry.filename;
            //uint8_t i = entryFilename.find_first_of(0xA0);
            //entryFilename = entryFilename.substr(0, i);
            //mstr::rtrimA0(entryFilename);
            //entryFilename = mstr::toUTF8(entryFilename);

            //Debug_printv("filename[%s] entry.filename[%s]", filename.c_str(), entryFilename.c_str());

            if ( mstr::compareFilename(filename, entryFilename, wildcard) )
            {
                return true;
            }

            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}


bool ArchiveMStream::seekEntry( uint16_t index )
{
    //Debug_printv("entry_count[%d] entry_index[%d] index[%d]", entry_count, entry_index, index);

    if ( !m_archive->isOpen() )
        return false;

    index--;

    entry.filename.clear();
    entry.size = 0;

    archive *a = m_archive->getArchive();

    if ( archive_read_next_header(a, &a_entry) != ARCHIVE_OK )
        return false;

    // Check filetype
    const mode_t type = archive_entry_filetype(a_entry);
    if ( S_ISREG(type) ) {
        entry.filename = basename(archive_entry_pathname(a_entry));
        entry.size = archive_entry_size(a_entry);
    }

    entry_index = index + 1;

    //Debug_printv("entry_index[%d] filename[%s] size[%lu]", entry_index, entry.filename.c_str(), entry.size);
    return true;
}


uint32_t ArchiveMStream::readFile(uint8_t *buf, uint32_t size) 
{
    uint32_t bytesRead = 0;
    bytesRead += read(buf, size);

    Debug_printv("size[%lu] bytesRead[%lu] _position[%lu]", size, bytesRead, _position);
    return bytesRead;
}

bool ArchiveMStream::seekPath(std::string path) {
    Debug_printv("seekPath called for path: %s", path.c_str());

    seekCalled = true;

    entry_index = 0;

    if (seekEntry(path)) {
        Debug_printv("entry[%s]", entry.filename.c_str());
        _size = entry.size;
        _position = 0;

        Debug_printv("File Size: size[%ld] available[%ld] position[%ld]", _size, available(), _position);
        return true;
    }

    Debug_printv("Not found! [%s]", path.c_str());
    return false;
}



/********************************************************
 * Files implementations
 ********************************************************/

bool ArchiveMFile::rewindDirectory()
{
    Debug_printv("url[%s] sourceFile->url[%s]", url.c_str(), sourceFile->url.c_str());
    auto image = ImageBroker::obtain<ArchiveMStream>("archive", url);
    if (image == nullptr)
        return false;

    dirIsOpen = true;
    image->m_archive->open( std::ios_base::in );
    image->resetEntryCounter();

    media_archive = name;

    return true;
}

MFile *ArchiveMFile::getNextFileInDir()
{
    bool r = false;

    if (!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<ArchiveMStream>("archive", url);
    if (image == nullptr)
        goto exit;

    do
    {
        r = image->getNextImageEntry();
    } while (r && image->entry.filename.empty()); // Don't want empty entries

    if (r)
    {
        std::string filename = image->entry.filename;
        //uint8_t i = filename.find_first_of(0xA0);
        //filename = filename.substr(0, i);
        // mstr::rtrimA0(filename);
        //mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (sourceFile->url + "/" + filename).c_str() );

        auto file = MFSOwner::File(sourceFile->url + "/" + filename);
        file->size = image->entry.size;
        //Debug_printv("entry[%s] ext[%s] size[%lu]", file->name.c_str(), file->extension.c_str(), file->size);

        return file;
    }

exit:
    dirIsOpen = false;
    image->m_archive->close();
    //Debug_printv( "END OF DIRECTORY" );
    return nullptr;
}
