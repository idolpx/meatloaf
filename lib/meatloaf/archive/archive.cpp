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

#include <string.h>
#include <archive.h>
#include <archive_entry.h>

#include "../meatloaf.h"

ssize_t cb_read(struct archive *, void *userData, const void **buff)
{
  // Returns pointer and size of next block of data from archive.
  // The read callback returns the number of bytes read, zero for end-of-file, or a negative failure code as above.
  // It also returns a pointer to the block of data read.
  // https://github.com/libarchive/libarchive/wiki/LibarchiveIO
  Archive *a = (Archive *) userData;
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
  //Debug_printv("bytes[%lld]", request);
  Archive *a = (Archive *) userData;

  if (a->m_archive)
  {
      bool rc = a->m_srcStream->seek(request, SEEK_CUR);
      return (rc) ? request : ARCHIVE_WARN;
  }
  else
  {
      Debug_printv("ERROR! skip failed");
      return ARCHIVE_FATAL;
  }
}

bool Archive::open(std::ios_base::openmode mode)
{
  // close the archive if it was already open
  close();

  Debug_printv("Archive::open");
  m_srcBuffer = new uint8_t[m_buffSize];
  m_archive   = archive_read_new();

  archive_read_support_filter_all(m_archive);
  archive_read_support_format_all(m_archive);
  archive_read_set_read_callback(m_archive, cb_read);
  archive_read_set_skip_callback(m_archive, cb_skip);
  archive_read_set_callback_data(m_archive, this);

  Debug_printv("Calling archive_read_open1");
  int r = archive_read_open1(m_archive);
  if( r != ARCHIVE_OK )
    {
      Debug_printv("Error opening archive: %d!", r);
      archive_read_free(m_archive);
      m_archive = NULL;
    }

  return isOpen();
}

void Archive::close()
{
  if( m_archive!=NULL )
    {
      archive_read_close(m_archive);
      archive_read_free(m_archive);
      m_archive = NULL;
    }
}

/********************************************************
 * Streams implementations
 ********************************************************/

int ArchiveMStream::s_rangeUsed = 0;
esp_himem_rangehandle_t ArchiveMStream::s_range;


bool ArchiveMStream::open(std::ios_base::openmode mode)
{
  m_mode = mode;
  return m_archive->open(mode);
}

void ArchiveMStream::close()
    {
  m_archive->close();

  if (m_haveData > 0)
        {
    if (m_dirty)
    {

      m_dirty = false;
    }

    ESP_ERROR_CHECK(esp_himem_free(m_data));
    Debug_printv("HIMEM available after free: %lu ", (uint32_t)esp_himem_get_free_size());

    // if this was the last archive in use then free up the mapped range
    if (s_rangeUsed > 0)
      s_rangeUsed--;
    if (s_rangeUsed == 0)
      esp_himem_free_map_range(s_range);
  }

  m_haveData = 0;
}

bool ArchiveMStream::isOpen()
{
  return m_archive->isOpen();
}

void ArchiveMStream::readArchiveData()
{
  if (m_archive->isOpen() && m_haveData == 0)
  {
    // allocate HIMEM memory for archive data (size must be multiple of ESP_HIMEM_BLKSZ);
    uint32_t size = (_size / ESP_HIMEM_BLKSZ) * ESP_HIMEM_BLKSZ;
    if (_size > size)
      size += ESP_HIMEM_BLKSZ;

    Debug_printv("HIMEM physical size: %lu", (uint32_t)esp_himem_get_phys_size());
    Debug_printv("HIMEM available before alloc: %lu ", (uint32_t)esp_himem_get_free_size());

    esp_err_t status = esp_himem_alloc(size, &m_data);
    if (status == ESP_OK)
      m_haveData = 1;
        else
        {
      Debug_printv("Unable to allocate HIMEM memory: %s", esp_err_to_name(status));
      m_haveData = -1;
      return;
    }

    // if mapped range is not yet created then create it now
    if (s_rangeUsed == 0)
    {
      esp_err_t status = esp_himem_alloc_map_range(ESP_HIMEM_BLKSZ, &s_range);
      if (status != ESP_OK)
      {
        Debug_printv("Unable to allocate mapped range for HIMEM: %s", esp_err_to_name(status));
        ESP_ERROR_CHECK(esp_himem_free(m_data));
        m_haveData = -1;
        return;
        }
    }

    Debug_printv("HIMEM available after alloc : %lu ", (uint32_t)esp_himem_get_free_size());

    // increment mapped range usage counter
    s_rangeUsed++;

    Debug_printv("reading %lu bytes from archive", _size);

    size = _size;
    uint32_t pageStart = 0;
    while (size > 0)
    {
      archive *a = m_archive->getArchive();
      uint32_t s = std::min(size, (uint32_t)ESP_HIMEM_BLKSZ);

      uint8_t *ptr;
      ESP_ERROR_CHECK(esp_himem_map(m_data, s_range, pageStart, 0, ESP_HIMEM_BLKSZ, 0, (void **)&ptr));
      uint32_t r = archive_read_data(a, ptr, s);
      ESP_ERROR_CHECK(esp_himem_unmap(s_range, ptr, ESP_HIMEM_BLKSZ));
      if (archive_errno(a) != ARCHIVE_OK || r != s)
      {
        if (archive_errno(a) != ARCHIVE_OK)
        {
          Debug_printv("archive read error %i: %s", archive_errno(a), archive_error_string(a));
        }
        else
        {
          Debug_printv("expected to read %lu bytes from archive, got %lu", s, r);
        }

        ESP_ERROR_CHECK(esp_himem_free(m_data));
        m_haveData = -1;
        return;
      }

      pageStart += s;
      size -= s;
    }
  }
}

uint32_t ArchiveMStream::read(uint8_t *buf, uint32_t size)
{
  readArchiveData();

  if (m_haveData > 0)
  {
    //Debug_printv("calling read, buff size=[%ld]", size);

    if (_position + size > _size)
      size = _size - _position;
    uint32_t numRead = 0;
    while (size > 0)
    {
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
  }
  else
    return 0;
}

uint32_t ArchiveMStream::write(const uint8_t *buf, uint32_t size)
{
  readArchiveData();

  // NOTE: this function can NOT write past the end of the extracted file,
  //       i.e. it can NOT extend the size of a file, only modify existing data
  //       However, most disk images (D64, D81, G64 etc) have a fixed size anyways.
  if (m_haveData > 0)
  {
    // Debug_printv("calling write, size=[%ld]", size);

    if (_position + size > _size)
      size = _size - _position;
    uint32_t numWritten = 0;
    while (size > 0)
    {
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

    // remember that data was written so we can re-zip the archive
    if (numWritten > 0)
      m_dirty = true;

    // Debug_printv("wrote [%lu] bytes", numWritten);
    return numWritten;
  }
  else
    return 0;
}

bool ArchiveMStream::seekEntry( std::string filename )
{
    //Debug_printv( "filename[%s] size[%d]", filename.c_str(), filename.size());

    // Read Directory Entries
  if ((filename.size() > 0) && (m_archive->isOpen() || m_archive->open(std::ios::in)))
    {
    archive *a = m_archive->getArchive();

        bool found = false;
        bool wildcard =  ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );
        while ( archive_read_next_header(a, &entry) == ARCHIVE_OK )
        {
            entry_index++;

            // Check filetype
            const mode_t type = archive_entry_filetype(entry);
            if ( S_ISREG(type) )
            {
                std::string entryFilename = basename(archive_entry_pathname(entry));

                Debug_printv("filename[%s] entry.filename[%.16s]", filename.c_str(), entryFilename.c_str());

                // Read Entry From Stream
                if ( filename == entryFilename ) // Match exact
                {
                    found = true;
                }
                else if ( wildcard ) // Wildcard Match
                {
                    if (filename == "*") // Match first entry
                    {
                        filename = entryFilename;
                        found = true;
                    }
                    else if ( mstr::compare(filename, entryFilename) ) // X?XX?X* Wildcard match
                    {
                        // Set filename to this filename
                        Debug_printv( "Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str() );
                        found = true;
                    }
                }

                if ( found )
                {
                    _size = archive_entry_size(entry);
                    return true;
                }
            }
        }

        Debug_printv( "Not Found! file[%s]", filename.c_str() );
    }

    entry = nullptr;
    return false;
}

uint32_t ArchiveMStream::readFile(uint8_t *buf, uint32_t size)
{
    uint32_t bytesRead = 0;
    bytesRead += read(buf, size);

    return bytesRead;
}

bool ArchiveMStream::seekPath(std::string path)
{
    Debug_printv("seekPath called for path: %s", path.c_str());

    seekCalled = true;

    entry_index = 0;

    if ( seekEntry( path ) )
    {
        Debug_printv("entry[%s]", archive_entry_pathname(entry));
        return true;
    }

    return false;
}

bool ArchiveMStream::seek(uint32_t pos)
{
  if( pos < _size )
    {
      _position = pos;
      return true;
    }
  else
    return false;
}

/********************************************************
 * Files implementations
 ********************************************************/

// archive file is always a directory
bool ArchiveMFile::isDirectory()
{
    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if ( pathInStream == "" )
        return true;
    else
        return false;
}

bool ArchiveMFile::rewindDirectory()
{
  // Debug_printv("prepare dir listing");

  delete m_archive;
  m_archive = nullptr;

  std::shared_ptr<MStream> dirStream = std::shared_ptr<MStream>(sourceFile->getSourceStream());
    if (dirStream == nullptr)
    {
        Debug_printv("dirStream is null");
        return false;
    }
  else if (!dirStream->isOpen())
    {
    Debug_printv("failed to open archive");
    return false;
  }
  else
  {
    m_archive = new Archive(dirStream);
    return m_archive->open(std::ios::in);
  }
}

MFile *ArchiveMFile::getNextFileInDir()
{
  if (m_archive != nullptr || rewindDirectory())
  {
    struct archive_entry *entry;

    std::string filename;
    do
    {
      if (archive_read_next_header(m_archive->getArchive(), &entry) != ARCHIVE_OK)
            break;

        filename = basename(archive_entry_pathname(entry));
      // Debug_printv("size[%d] empty[%d] pathInStream[%s] filename[%s]", filename.size(), filename.empty(), pathInStream.c_str(), filename.c_str());
    } while (filename.empty()); // Skip empty filenames

    // Debug_printv("getNextFileInDir calling archive_read_next_header");
    if (filename.size() > 0)
    {
        MFile *file = MFSOwner::File(sourceFile->url + "/" + filename);
        file->size = archive_entry_size(entry);
        file->_exists = true;
        return file;
    }
    else
    {
        Debug_printv( "END OF DIRECTORY");
      delete m_archive;
      m_archive = nullptr;
    }
    }

  return nullptr;
}
