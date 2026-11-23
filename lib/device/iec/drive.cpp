
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

#if defined(BUILD_IEC)

#include "drive.h"
#include "drive_filename.h"
#include "drive/dos/cbm_dos.h"
#include "drive/dos/open_spec.h"

#include <algorithm>
#include <new>
#include <cstring>
#include <ctime>
#include <sstream>
#include <unordered_map>

#include "make_unique.h"

#include "meatloaf.h"
#include "meat_session.h"
#include "network/http.h"
#include "mlConfig.h"
#include "fuji.h"
#include "fnFsSD.h"
#include "fnWiFi.h"
#include "fnSystem.h"
#include "led.h"
#include "utils.h"
#include "display.h"
#include "time_converter.h"

#include "meat_media.h"
#include "media/hd/partition_select.h"
#ifndef DISABLE_TAPE   // MEATLOAF-GATE
#include "media/tape/tap.h"
#endif
#include "qrmanager.h"
#include "../../www/ws/activity.h"

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"
//#include "../../../include/version.h"
#include "../../../include/cbm_defines.h"
#include "Esp.h"

// Buffering data when reading/writing streams because during regular (non-fastloader)
// transmissions, the read/write functions are called for each single byte at a time and
// reading/writing MStream one byte at a time can be time consuming.
// To be safe, BUFFER_SIZE should always be >=256
#define BUFFER_SIZE 512

static inline void *psram_malloc(size_t sz) {
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}


#if 1
static const char *getCStringLog(std::string s)
{
    size_t i, len = s.length();
    for(i=0; i<len && isprint(s[i]); i++);

    if( i==len )
        return s.c_str();
    else
        {
        static const char* digits = "0123456789ABCDEF";
        static std::string ss;
        ss.clear();
        for(i=0; i<len; i++)
            {
            if( isprint(s[i]) ) 
                ss += s[i];
            else
                {
                uint8_t d = (uint8_t) s[i];
                ss += "[";
                ss += digits[d/16];
                ss += digits[d&15];
                ss += "]";
                }
            }
        
        return ss.c_str();
    }
}
#else
#define getCStringLog(s) (s).c_str()
#endif



#ifdef MEATLOAF_CARD_DETECT_HOTPLUG
static bool isSdPath(const std::string &url)
{
    return url == "sd:" || mstr::startsWith(url, "sd:/") ||
           url == "/sd" || mstr::startsWith(url, "/sd/");
}

static bool isSdBacked(const MFile *file)
{
    // A media file can be several containers deep. The physical SD source is
    // therefore not necessarily the outer object's own URL.
    for (int depth = 0; file != nullptr && depth < 16; depth++)
    {
        if (file->scheme == "sd" || isSdPath(file->url))
            return true;

        if (file->sourceFile == file)
            break;
        file = file->sourceFile;
    }
    return false;
}
#endif


// -------------------------------------------------------------------------------------------------


iecChannelHandler::iecChannelHandler(iecDrive *drive)
{ 
    m_drive = drive;
    m_data = (uint8_t*)psram_malloc(BUFFER_SIZE);
    m_len = 0;
    m_ptr = 0;
}


iecChannelHandler::~iecChannelHandler()
{ 
    free(m_data);
}


uint8_t iecChannelHandler::read(uint8_t *data, uint8_t n)
{
    // if buffer is empty then re-fill it
    if( m_ptr >= m_len )
    {
        // stream already confirmed exhausted — don't call readBufferData again
        if( m_eos ) return 0;

        m_ptr = 0;
        m_len = 0;

        uint8_t st = readBufferData();
        if( st!=ST_OK )
        {
            m_drive->setStatusCode(st);
            return 0;
        }
        // If readBufferData filled nothing, stream is exhausted — latch EOS here
        // rather than relying on m_stream->eos() which can be transiently inconsistent
        // (e.g. HTTP complete() flag racing with the ESP-IDF event loop).
        if( m_len == 0 )
        {
            //Debug_printv("EOS latched: m_ptr[%d] m_len[%d]", m_ptr, m_len);
            m_eos = true;
            return 0;
        }
    }

    // read data from buffer
    if( m_ptr < m_len )
    {
        if( n==1 )
        {
            // common case during regular (non-fastloader) load
            data[0] = m_data[m_ptr++];
            m_position++;
            return 1;
        }
        else
        {
            // copy as much data as possible
            n = std::min((size_t) n, (size_t) (m_len - m_ptr));
            memcpy(data, m_data + m_ptr, n);
            m_ptr += n;
            m_position += n;
            return n;
        }
    }
    else
    {
        return 0;
    }
}


uint8_t iecChannelHandler::write(uint8_t *data, uint8_t n)
{
    // if buffer is full then empty it
    if( m_len+n > BUFFER_SIZE )
    {
        uint8_t st = writeBufferData();
        if( st!=ST_OK )
        {
            m_drive->setStatusCode(st);
            return 0;
        }

        m_ptr = 0;
        m_len = 0;
    }

    // write data to buffer
    if( n==1 )
    {
        // common case during regular (non-fastloader) save
        m_data[m_len++] = data[0];
    }
    else
    {
        // copy data
        memcpy(m_data + m_len, data, n);
        m_len += n;
    }

    m_position += n;
    return n;
}


// -------------------------------------------------------------------------------------------------


iecChannelHandlerFile::iecChannelHandlerFile(iecDrive *drive, std::shared_ptr<MStream> stream, int fixLoadAddress) : iecChannelHandler(drive)
{
    m_stream = stream;
    m_fixLoadAddress = fixLoadAddress;
    m_timeStart = esp_timer_get_time();
    m_byteCount = 0;
    m_transportTimeUS = 0;
}


iecChannelHandlerFile::~iecChannelHandlerFile()
{
    double seconds = (esp_timer_get_time()-m_timeStart) / 1000000.0;

    if( m_stream->mode == std::ios_base::out && m_len>0 )
        writeBufferData();

    m_stream->close();
    Debug_printv("Stream closed.");

    // Media image writes report errors (e.g. DISK FULL) at close time, when
    // the final block and directory entry are committed.
    if( m_stream->mode == std::ios_base::out && m_stream->error() )
        m_drive->setStatusCode(m_stream->error());

    // If a .config file was just saved, drop the cached lookups so its
    // base_url redirect takes effect immediately instead of after the TTL.
    if( m_stream->mode == std::ios_base::out && mstr::endsWith(m_stream->url, ".config") )
        MFSOwner::invalidateConfigCache();

    double cps = m_byteCount / seconds;
    Debug_printv("%s %lu bytes in %0.2f seconds @ %0.2f B/s", m_stream->mode == std::ios_base::in ? "Sent" : "Received", m_byteCount, seconds, cps);

    // "Sent" (mode == in) is a LOAD from the C64's perspective.
    if( m_stream->mode == std::ios_base::in && m_byteCount > 0 )
    {
        std::string basename = m_stream->url;
        size_t slash = basename.find_last_of('/');
        if (slash != std::string::npos)
            basename = basename.substr(slash + 1);

        char loadMsg[160];
        snprintf(loadMsg, sizeof(loadMsg), "%s (%lu bytes @ %.0f B/s)", basename.c_str(), m_byteCount, cps);
        notify_activity(m_drive->activitySource(), "load", loadMsg);
    }

    double tseconds = m_transportTimeUS / 1000000.0;
    cps = m_byteCount / (seconds-tseconds);
    Debug_printv("Transport (network/sd) took %0.3f seconds, pure IEC transfers @ %0.2f B/s", tseconds, cps);

#ifdef ENABLE_DISPLAY
    LEDS.idle();
    //Debug_printv("Stop Activity");
#endif

  //delete m_stream;
}

// For network streams (read-write mode), forward writes directly to the
// stream instead of buffering in the shared m_data[] read buffer.
// This is critical for full-mode HTTP: PRINT# commands must reach
// HTTPMStream::write() before the C64 can GET# back the response.
// ALSO: after forwarding a command, reset the IEC buffer so the next
// readBufferData() call (from the subsequent GET# or read) fills from
// the stream's freshly-positioned cursor.  Without this, m_eos locks
// the buffer permanently and mode-switch commands (r-h, r-b, status)
// can never re-fill from a different region of the response.
uint8_t iecChannelHandlerFile::write(uint8_t *data, uint8_t n) {
    if (m_stream && (m_stream->mode & std::ios_base::out)) {
        uint64_t t = esp_timer_get_time();
        size_t written = m_stream->write(data, n);
        m_transportTimeUS += (esp_timer_get_time() - t);
        m_byteCount += written;
        // This path returns without reaching iecChannelHandler::write(), so
        // the channel position has to be advanced here too.
        m_position += written;
        // Clear EOS and reset the IEC buffer so the next readBufferData()
        // refills from the stream's freshly-positioned cursor.
        // Mode-switch commands (r-h, r-b, status) reposition the stream's
        // internal _responseBufPos — stale m_data bytes must be discarded.
        m_ptr = 0;
        m_len = 0;
        m_eos = false;
        // Zero the buffer so stale "Co" bytes from a prior response
        // can't leak into the next readBufferData refill.
        memset(m_data, 0, BUFFER_SIZE);
        return written;
    }
    return iecChannelHandler::write(data, n);
}


uint8_t iecChannelHandlerFile::writeBufferData()
{
    /*
    // if m_stream is within a disk image then m_stream->mode does not get initialized properly!
    if( m_stream->mode != std::ios_base::out )
        return ST_FILE_TYPE_MISMATCH;
    else
    */
    {
        Debug_printv("bufferSize[%d]", m_len);
        uint64_t t = esp_timer_get_time();
        size_t n = m_stream->write(m_data, m_len);
        m_transportTimeUS += (esp_timer_get_time()-t);
        m_byteCount += n;
        if( n<m_len )
        {
            Debug_printv("Error: write failed: n[%d] < m_len[%d]", n, m_len);
            return ST_WRITE_VERIFY;
        }
    }

    return ST_OK;
}


uint8_t iecChannelHandlerFile::readBufferData()
{
    /*
    // if m_stream is within a disk image then m_stream->mode does not get initialized properly!
    if( m_stream->mode != std::ios_base::in )
        return ST_FILE_TYPE_MISMATCH;
    else
    */
    {
        //Debug_printv("size[%lu] avail[%lu] pos[%lu] eos[%d] error[%d] m_len=%d m_eos=%d", m_stream->size(), m_stream->available(), m_stream->position(), m_stream->eos(), m_stream->error(), m_len, m_eos);
        Debug_printv("size[%lu] avail[%lu] pos[%lu]", m_stream->size(), m_stream->available(), m_stream->position());
        // if (m_stream->size() == 0)
        //     return ST_FILE_NOT_FOUND;

#ifdef ENABLE_DISPLAY
        if ( m_stream->size() > 0)
        {
            // send progress percentage
            uint8_t percent = (m_stream->position() * 100) / m_stream->size();
            LEDS.progress = percent;
        }
        else
        {
            // we don't know the size of the stream, so just show activity without progress
            LEDS.speed = 100;
            LEDS.activity = true;
        }
#endif
        fnLedManager.toggle(eLed::LED_BUS);

        // try to fill buffer
        m_len = 0;
        do 
        {
            uint64_t t = esp_timer_get_time();
            uint32_t got = m_stream->read(m_data+m_len, BUFFER_SIZE-m_len);
            m_transportTimeUS += (esp_timer_get_time()-t);

            if (got == 0) {
                // Network streams in full-mode HTTP may return 0 when a
                // mode region is exhausted (headers done, body waiting).
                // Just break — the C64's next mode-switch command will
                // clear m_eos and trigger a fresh refill.
                break;
            }
            m_len += got;

            // if m_fixLoadAddress is set, adjust the first two bytes
            if( m_fixLoadAddress>=0 && m_stream->position()==0 && m_len>=2 )
            {
                m_data[0] = (m_fixLoadAddress & 0x00FF);
                m_data[1] = (m_fixLoadAddress & 0xFF00) >> 8;
                m_fixLoadAddress = -1;
            }

            if (m_stream->error()) {
                Debug_printv("Error: read failed: got[%d]", got);
                return ST_DRIVE_NOT_READY;
            }
        } while( m_len<BUFFER_SIZE && !m_stream->eos() );

        m_byteCount += m_len;

        // If we filled nothing, the stream is exhausted (do-while broke on eos/error).
        // Latch without re-querying m_stream->eos() — HTTP complete() can be transiently false.
        if( m_len == 0 )
            m_eos = true;
    }

    //Debug_printv("bufferSize[%d]", m_len);
    return ST_OK;
}

// -------------------------------------------------------------------------------------------------


iecChannelHandlerDir::iecChannelHandlerDir(iecDrive *drive, MFile *dir) : iecChannelHandler(drive)
{
    m_dir = dir;
    m_headerLine = 1;
    
    std::string scheme = m_dir->scheme;
    std::string url = m_dir->host;
    std::string path = m_dir->path;
    std::string archive = m_dir->media_archive;
    std::string image = m_dir->media_image;
    // Where we are INSIDE the image. `path` above is the container's path and
    // never shows it, so a listing of os/temporary looked identical to one of
    // the image root.
    std::string inImage = m_dir->pathInStream;
    if (archive.size() > 0) {
        mstr::replaceAll(path, archive, "");
        mstr::drop(path, 1);
    }
    if (image.size() > 0) {
        mstr::replaceAll(path, image, "");
        mstr::drop(path, 1);
    }
    mstr::replaceAll(path, "//", "/");

    scheme = mstr::toPETSCII2(scheme);
    url = mstr::toPETSCII2(url);
    path = mstr::toPETSCII2(path);
    archive = mstr::toPETSCII2(archive);
    image = mstr::toPETSCII2(image);
    inImage = mstr::toPETSCII2(inImage);

    m_headers.clear();
    if( url.size()>0 )     addExtraInfo(scheme, url);
    if( path.size()>1 )    addExtraInfo("PATH", path);
    if( archive.size()>1 ) addExtraInfo("ARCHIVE", archive);
    if( image.size()>0 )   addExtraInfo("IMAGE", image);
    if( inImage.size()>0 ) addExtraInfo("DIR", inImage);
    if( m_headers.size()>0 ) m_headers.push_back("NFO ----------------");

    // If SD Card is available and we are at the root path show it as a directory at the top
    if(m_dir->url.size() < 2 ) {
        if( fnSDFAT.running() )
            m_headers.push_back("DIR SD");

#ifndef MIN_CONFIG
        // This will be used to browse the network
        m_headers.push_back("DIR NETWORK");
#endif
    }

#ifdef ENABLE_DISPLAY
    Debug_printv("Start Activity");
    LEDS.speed = 100;
    LEDS.activity = true;
#endif
}


iecChannelHandlerDir::~iecChannelHandlerDir()
{
    delete m_dir;

#ifdef ENABLE_DISPLAY
    LEDS.idle();
    Debug_printv("Stop Activity");
#endif
}


void iecChannelHandlerDir::addExtraInfo(std::string title, std::string text)
{
    { std::string h; h.reserve(6 + title.size()); h = "NFO ["; h += title; h += ']'; m_headers.push_back(h); }
    while( text.size()>0 )
    {
        std::string s = text.substr(0, 16);
        std::string h; h.reserve(4 + s.size()); h = "NFO "; h += s;
        m_headers.push_back(h);
        text.erase(0, s.size());
    }
}


uint8_t iecChannelHandlerDir::writeBufferData()
{
    return ST_FILE_TYPE_MISMATCH;
}


uint8_t iecChannelHandlerDir::readBufferData()
{
    fnLedManager.toggle(eLed::LED_BUS);

    if( m_headerLine==1 )
    {
        // main header line
        m_data[0] = 0x01;   // Load Address low byte
        m_data[1] = 0x08;   // Load Address high byte
        m_data[2] = 1;      // BASIC line pointer low byte
        m_data[3] = 1;      // BASIC line pointer high byte
        m_data[4] = m_dir->media_partition;      // Partition number low byte
        m_data[5] = 0;      // Partition number high byte
        m_data[6] = 18;     // REVERSE ON
        m_data[7] = '"';
        Debug_printv("header[%s] id[%s]", m_dir->media_header.c_str(), m_dir->media_id.c_str());
        // media_header/media_id are UTF-8, like every other name in Meatloaf,
        // and are converted here for the same reasons the entry names below
        // are. Literals assigned to them are therefore written LOWERCASE, so
        // toPETSCII2 lands in the unshifted range the C64 draws as capitals.
        // Not PRODUCT_ID: that is uppercase because the status channel and the
        // serial banner take it raw, and this path converts.
        std::string name = m_dir->media_header.size() ? m_dir->media_header : "meatloaf cbm";
        if ( !m_dir->isCBM )
            name = U8Char::encodeACE(name);
        name = mstr::toPETSCII2( name );
        size_t n = std::min(16, (int) name.size());
        memcpy(m_data+8, name.data(), n);
        while( n<16 ) { m_data[8+n] = ' '; n++; }
        m_data[24] = '"';
        m_data[25] = ' ';
        std::string id;
        if ( m_dir->media_id.size() )
        {
            mstr::replaceAll(m_dir->media_id, "{{id}}", mstr::format("%02i", m_drive->id()));
            id = mstr::toPETSCII2( m_dir->media_id );
        }
        else
        {
            // Drive # as ID in browser mode. Lowercase for the same reason.
            id = mstr::toPETSCII2( mstr::format("%02i 2a", m_drive->id()) );
        }
        // Exactly 5 chars ("ID" + DOS version) to keep the browser line aligned
        if ( id.size() > 5 )
            id = id.substr(0, 5);
        else if ( id.size() < 5 )
            id += std::string(5 - id.size(), ' ');
        memcpy(m_data+26, id.data(), 5);
        m_data[31] = 0;
        m_len = 32;
        m_headerLine++;
    }
    else if( m_headerLine-2 < m_headers.size() )
    {
        // Send Extra INFO
        m_data[0] = 1;  // BASIC line pointer low byte
        m_data[1] = 1;  // BASIC line pointer high byte
        m_data[2] = 0;  // Block count low byte
        m_data[3] = 0;  // Block count high byte

        std::string name = m_headers[m_headerLine-2];
        std::string ext  = name.substr(0, 3);

        name = "   \"" + name.substr(4, 16) + "\"";
        if( name.size()<21 ) name += std::string(21-name.size(), ' ');
        name += " " + ext + "  ";
        strcpy((char *) m_data+4, name.c_str());
        m_data[31] = 0;
        m_len = 32;
        m_headerLine++;
    }
    else if( m_headerLine < 0xFF )
    {
        // file entries
        std::unique_ptr<MFile> entry;
        bool skip = false;
        do 
        { 
            skip = false;
            entry = std::unique_ptr<MFile>( m_dir->getNextFileInDir() ); 
            if( entry!=nullptr )
            {
                Debug_printv("[%s][%s]", entry->name.c_str(), entry->pathInStream.c_str());

                // filter directory listing here
                // by name pattern
                // by type (DIR/PRG/SEQ/USR/DEL)
                // by size
                // by date

                if ( entry->is_hidden )
                    skip = true;
                else if ( mstr::isJunk(entry->name) )
                    skip = true;

                //Debug_printv("name[%s] skip[%d]", entry->name.c_str(), skip);
            }
        }
        while( entry!=nullptr && skip == true );

        if( entry != nullptr )
        {
            //Debug_printv( ANSI_WHITE_BOLD "blocks[%lu] name[%s] ext[%s]", entry->blocks(), entry->name.c_str(), entry->extension.c_str());

            // directory entry
            uint16_t blocks = entry->blocks();
            m_data[m_len++] = 1;  // BASIC line pointer low byte
            m_data[m_len++] = 1;  // BASIC line pointer high byte
            m_data[m_len++] = blocks&255;  // Block count low byte
            m_data[m_len++] = blocks/256;  // Block count high byte
            if( blocks<10 )    m_data[m_len++] = ' ';
            if( blocks<100 )   m_data[m_len++] = ' ';
            if( blocks<1000 )  m_data[m_len++] = ' ';
            m_data[m_len++] = '"';

            // File name
            std::string name = entry->name;
            std::string ext = entry->extension;
            if ( !mstr::contains(entry->pathInStream, name.c_str()) && entry->pathInStream.size() )
                name = entry->pathInStream;
            if ( ext[0] == '_' )
                ext.clear();

            // Set the extension, then convert to PETSCII. Only the EXTENSION
            // handling depends on where the entry came from - the name is
            // UTF-8 either way and is converted unconditionally, below.
            if ( !m_dir->isCBM )
            {
                Debug_printv("original name[%s] ext[%s]", name.c_str(), ext.c_str());
                // A host filename can hold characters PETSCII has no code for;
                // ACE keeps them typable. Never do this to a CBM name - see
                // the isCBM comment in meatloaf.h.
                name = U8Char::encodeACE(name);

                ext  = U8Char::encodeACE(ext);
                ext  = mstr::toPETSCII2( ext );
                if( ext.size() )  // Enhanced directory entries with real file extension
                {
                    if( ext.size() >= 3 )
                        ext = ext.substr(0, 3);
                    else
                        ext += std::string(3 - ext.size(), ' ');
                }
                else if( entry->isDirectory() )
                {
                    ext = "DIR";
                }
                else
                {
                    ext = "PRG";
                }
                ext = " " + ext + " "; // Clear closed and locked status display
            }
            else
            {
                // unescape slashes
                mstr::replaceAll(name, "\\", "/");

                // Normalize the extension to exactly 5 chars ("* + ext + <"
                // status field). decodeType() returns a 4-char value like
                // " PRG", but the fixed 5-byte memcpy below would then copy the
                // std::string NUL terminator into the middle of the directory
                // line, corrupting the C64 BASIC listing (truncates on relink).
                if( ext.size() < 5 )
                    ext += std::string(5 - ext.size(), ' ');
                else if( ext.size() > 5 )
                    ext = ext.substr(0, 5);
            }

            // The IEC boundary: everything upstream of here is UTF-8. A CBM
            // name round-trips exactly (toUTF8 at the producer, toPETSCII2
            // here); a host name has been ACE-encoded above so it is plain
            // ASCII by now. Note UTF-8 LOWERCASE maps to the unshifted PETSCII
            // range the C64 renders as capitals - which is why literal strings
            // destined for a listing are written in lowercase.
            name = mstr::toPETSCII2( name );
            // Debug_printv("converted name[%s] ext[%s]", name.c_str(), ext.c_str());

#if 0
            // C64 compatibale filename (16+3 chars)
            {
                size_t n = std::min(16, (int) name.size());
                memcpy(m_data+m_len, name.data(), n);
                m_len += n;
                m_data[m_len++] = '"';

                // Extension gap
                n = 17-n;
                while(n-->0) m_data[m_len++] = ' ';

                // Extension
                memcpy(m_data+m_len, ext.data(), 3);
                m_len+=3;
                while( m_len<31 ) m_data[m_len++] = ' ';
                m_data[31] = 0;
                m_len = 32;
            }
#else
            // Full long filename (up to 30 chars)
            {
                size_t n = (int) name.size();
                memcpy(m_data+m_len, name.data(), n);

                m_len += n;
                m_data[m_len++] = '"';

                // Extension gap
                if (n<16)
                {
                    n = 16-n;
                    while(n-->0) m_data[m_len++] = ' ';
                }
                // else
                //     m_data[m_len++] = ' ';

                // C64 Media includes closed and locked status in extension
                // * + Extension + <
                memcpy(m_data+m_len, ext.data(), 5);
                m_len+=5;
                m_data[m_len++] = ' ';
                m_data[m_len++] = 0;

                //Debug_printv("data[%s] name[%s] ext[%s]", m_data, name.c_str(), ext.c_str());
            }
#endif
        }
        else
        {
            if (m_dir->media_partition != 0xFF) // Only show BLOCKS FREE footer if a partition is selected
            {
                // no more entries => footer
#ifdef IEC_FP_DOLPHIN
                // DolphinDos' MultiDubTwo copy program needs the "BLOCKS FREE" footer line, otherwise it aborts when reading source
                uint32_t free = m_dir->media_image.size() ? m_dir->media_blocks_free : std::min((int) m_dir->getAvailableSpace()/254, 65535);
                m_data[0] = 1;          // BASIC line pointer low byte
                m_data[1] = 1;          // BASIC line pointer high byte
                m_data[2] = free&255;   // Block count low byte
                m_data[3] = free/256;   // Block count high byte
                strcpy((char *) m_data+4, "BLOCKS FREE.");
#else
                uint32_t free = m_dir->media_image.size() ? m_dir->media_blocks_free : 0;
                m_data[0] = 1;          // BASIC line pointer low byte
                m_data[1] = 1;          // BASIC line pointer high byte
                m_data[2] = free&255;   // Block count low byte
                m_data[3] = free/256;   // Block count high byte
                if( m_dir->media_image.size() )
                    strcpy((char *) m_data+4, "BLOCKS FREE.");
                else
                    sprintf((char *) m_data+4, CBM_DELETE CBM_DELETE "%sBYTES FREE.", mstr::formatBytes(m_dir->getAvailableSpace()).c_str());
#endif
                int n = 4+strlen((char *) m_data+4);
                while( n<29 ) m_data[n++]=' ';

                // no more entries => end of program
                m_data[29] = 0;  // Terminate footer line
                m_data[30] = 0;  // End of program
                m_data[31] = 0;  // End of program
                m_len = 32;
            }
            else
            {
                // No footer line. m_data still holds the LAST directory entry,
                // so only zeroing the tail would send that line a second time
                // (a single partition listed twice under $=P). Send just the
                // end-of-program link.
                m_data[0] = 0;
                m_data[1] = 0;
                m_len = 2;
            }

            m_headerLine = 0xFF;
        }
    }

    return ST_OK;
}


// -------------------------------------------------------------------------------------------------


iecDrive::iecDrive(uint8_t devnum) : SystemFileDevice(devnum)
{
  //Debug_printv("id[%d]", devnum);
}


iecDrive::~iecDrive()
{
    for(int i=0; i<16; i++)
        if( m_channels[i]!=nullptr )
            close(i);
}

void iecDrive::begin()
{
    SystemFileDevice::begin();

    //Debug_printv("id[%d]", id());
    m_host = nullptr;

    set_cwd("/");
    m_memory.setROM("dos1541"); // Default to 1541 ROM

    m_vdrive = NULL;

    setStatusCode(ST_DOSVERSION);
    m_numOpenChannels = 0;
    for(int i=0; i<16; i++)
        m_channels[i] = nullptr;
#ifdef MEATLOAF_CARD_DETECT_HOTPLUG
    m_sdMediaGeneration = fnSDFAT.media_generation();
#endif

    // NOTE: reloadConfig() is NOT called here. begin() runs during bus attach
    // (main_setup() -> SYSTEM_BUS.setup()), which happens before fnWiFi.start().
    // A persisted network URL (fsp://, http://, ...) would call set_cwd() ->
    // getaddrinfo() before the LWIP tcpip task exists, aborting with
    // "assert failed: tcpip_send_msg_wait_sem ... Invalid mbox". Callers must
    // invoke reloadConfig() explicitly once WiFi is up (see main.cpp).
}

bool iecDrive::open(uint8_t channel, const char *cname, uint8_t nameLen)
{
    Debug_printv("iecDrive::open(#%d, %d, \"%s\")", m_devnr, channel, cname);

    // A fresh OPEN after a card change must never reuse the previous card's
    // D64/VDrive or directory channel. Continue the OPEN from root after the
    // stale state has been discarded.
#ifdef MEATLOAF_CARD_DETECT_HOTPLUG
    synchronizeSdMedia();
#endif

    // The DOS is never swapped while it is executing; this is one of the two
    // points where a pending change of media kind is applied.
    ensureDos();

#ifdef ENABLE_DISPLAY
    LEDS.activity = true;
#endif
    fnLedManager.toggle(eLed::LED_BUS);

//#ifdef USE_VDRIVE
    if( Meatloaf.use_vdrive )
    {
        if (m_vdrive!=nullptr && (strncmp(cname, "//", 2)==0 || strncmp(cname, "ML:", 3)==0 || strstr(cname, "://")!=NULL) )
        {
            Debug_printv("Closing VDrive");
            delete m_vdrive;
            m_vdrive = NULL;
        }
        if( m_vdrive!=nullptr )
        {
            bool ok = m_vdrive->openFile(channel, cname, nameLen);
            Debug_printv("File opened on VDrive");
            setStatusCode(ok ? ST_OK : ST_VDRIVE);
            m_timeStart = esp_timer_get_time();
            m_byteCount = 0;
            return ok;
        }
    }

//#endif
    {
        // determine file name, mode (read/write) and overwrite flag
        OpenRequest req = parseOpenSpec(channel, std::string(cname));
        std::string name = req.name;
        const std::ios_base::openmode mode = req.mode;
        const bool overwrite = req.overwrite;

        if ( m_cwd == nullptr )
        {
            Debug_printv("opening channel[%d] m_cwd[NULL] name[%s] mode[%s]", channel, name.c_str(), 
                    mode==std::ios_base::out ? (overwrite ? "replace" : "write") : "read");
        }
        else
        {
            Debug_printv("opening channel[%d] m_cwd[%s] name[%s] mode[%s]", channel, m_cwd->url.c_str(), name.c_str(), 
                    mode==std::ios_base::out ? (overwrite ? "replace" : "write") : "read");
        }

        if( name.length()==0 )
        {
            Debug_printv("Error: empty file name");
            setStatusCode(ST_SYNTAX_BAD_NAME);
        }
        else
        {
            if( m_channels[channel] )
            {
                Debug_printv("channel[%d] is already in use, closing it before re-opening", channel);
                close(channel);
            }

            // "#" is DIRECT ACCESS: the channel gets the CONTAINER's own byte
            // stream instead of a file inside it, so B-P and B-R can roam the
            // whole image. A real drive takes a buffer number ("#3"); there is
            // one stream per channel here, so a trailing number is accepted
            // and ignored. Anything else after the '#' is a filename.
            if( req.direct_access )
            {
                if( m_cwd == nullptr )
                {
                    setStatusCode(ST_DRIVE_NOT_READY);
                    return false;
                }

                // NEVER plain out: opening a container for write TRUNCATES the
                // image. Writing to it has to be read-modify-write.
                std::ios_base::openmode container_mode =
                    (mode & std::ios_base::out) ? (std::ios_base::in | std::ios_base::out)
                                                : std::ios_base::in;

                // A fresh MFile on the cwd's own url carries no pathInStream,
                // so getSourceStream() selects no entry: seekCalled stays false
                // and reads pipe raw container bytes through readContainer(),
                // which is exactly the direct-access view. getSourceStream()
                // also builds a FRESH stream per open, so this cannot disturb
                // the ImageBroker instance a directory listing is using.
                std::unique_ptr<MFile> image(MFSOwner::File(m_cwd->url));
                auto stream = image ? image->getSourceStream(container_mode) : nullptr;
                if( stream == nullptr || !stream->isOpen() )
                {
                    Debug_printv("Error: no container stream for direct access [%s]",
                                 m_cwd->url.c_str());
                    setStatusCode(ST_DRIVE_NOT_READY);
                    return false;
                }

                m_channels[channel] = new iecChannelHandlerFile(this, stream);
                m_channels[channel]->setName(m_cwd->url + " (direct)");

                // Nothing sets _size for the no-entry view, so the container's
                // real length is taken off the raw file -- MFSOwner::File(url,
                // true) forces defaultFS, the established "give me the bytes"
                // primitive, so the name is not extension-sniffed back into
                // another D64MFile. Remembered on the channel because the block
                // window below overwrites the stream's size, and F-P has to be
                // able to put it back.
                uint32_t full_size = 0;
                {
                    std::unique_ptr<MFile> raw(MFSOwner::File(m_cwd->url, true));
                    auto raw_stream = raw ? raw->getSourceStream() : nullptr;
                    if( raw_stream != nullptr )
                        full_size = raw_stream->size();
                }
                m_channels[channel]->setFullSize(full_size);

                // Direct access reads ONE block at a time, so the channel
                // starts as a window on block 0 rather than on the whole
                // image. B-R/U1 move it, B-P addresses within it, F-P leaves
                // block mode entirely.
                stream->setSize(stream->block_size);
                m_channels[channel]->setBlockWindow(0, 0, stream->block_size);

                m_numOpenChannels++;
                setStatusCode(ST_OK);
                Debug_printv("Direct access channel[%d] on [%s] block size[%lu]",
                             channel, m_cwd->url.c_str(), (unsigned long) stream->block_size);
                return true;
            }

            // "0:" and "1:" select a drive inside a Commodore DOS device;
            // they are not part of the directory pattern and must not be
            // confused with CMD's alphanumeric filters.
            iecNormalizeDirectoryDrivePrefix(name);

            // Handle CMD-style directory filters by preserving them in URL
            bool wasDirListing = false;
            if ( name[0] == '$' ) {
                // Check if this is a CMD-style filter (e.g., $=P, $GAME*, $=P:GAME*)
                if (name.length() > 1 && (name[1] == '=' || isalnum(name[1]))) {
                    // This looks like a CMD filter - preserve it for the server
                    Debug_printv("CMD filter detected: [%s]", name.c_str());
                    // Don't clear the name - let the server handle CMD filtering
                } else {
                    // Plain "$" - traditional directory listing
                    name.clear();
                    wasDirListing = true;
                }
            }

            // get file
            Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Changing directory to [%s][%s] hex[%s]", m_cwd->url.c_str(), name.c_str(), mstr::toHex(name).c_str());
            name = mstr::toUTF8(name);
            name = U8Char::decodeACE(name);
            MFile *f;
            if ( name == "*" && !m_cwd->isDirectory() )
            {
                // m_cwd already points directly at a file (e.g. CD'd onto a file
                // instead of its parent directory) — LOAD"*" should load that file
                // rather than wildcard-matching within it.
                f = MFSOwner::File(m_cwd->url);
            }
            else
            {
                f = m_cwd->cd(name);
            }
            if (f == nullptr)
            {
                Debug_printv("Error: could not find file system for URL [%s]", name.c_str());
                setStatusCode(ST_FILE_NOT_FOUND);
            }
            else
            {
                bool is_dir = f->isDirectory();
                Debug_printv("isdir[%d] url[%s][%s]", is_dir, f->url.c_str(), f->pathInStream.c_str());

                if (is_dir)
                {
                    if( mode == std::ios_base::in )
                    {
                        // // reading directory
                        // bool isProperDir = false;
                        // std::unique_ptr<MFile> entry(f->getNextFileInDir());
                        // Debug_printv("First entry in directory [%s] is [%s] cwd[%s]", f->url.c_str(), entry==nullptr ? "NULL" : entry->name.c_str(), m_cwd->url.c_str());
                        // if( entry==nullptr )
                        // {
                        //     // if we can't open the file stream then assume this is an empty directory
                        //     std::shared_ptr<MStream> s = f->getSourceStream(mode);
                        //     //if( s==nullptr || !s->isOpen() ) isProperDir = true;
                        //     Debug_printv("stream for directory [%s] is [%s]", f->url.c_str(), s==nullptr ? "NULL" : (s->isOpen() ? "OPEN" : "CLOSED"));
                        //     if( s!=nullptr)
                        //     {
                        //         if( s->isOpen() )
                        //             isProperDir = true;
                        //     }
                        //     //delete s;
                        // }
                        // else
                        // {
                        //     Debug_printv("Directory [%s] first entry is [%s]", f->url.c_str(), entry->name.c_str());
                        //     delete entry;
                        //     isProperDir = true;
                        // }

                        // Debug_printv("isProperDir[%d]", isProperDir);
                        // if( isProperDir )
                        {
                            Debug_printv("Opening directory for reading [%s]", f->url.c_str());
                            // regular directory
                            if (!f->rewindDirectory())
                            //if (!f->rewindDirectory(filter, sort))
                            {
                                Debug_printv("Error: could not set current working directory to [%s]. Permission denied.", f->url.c_str());
                                setStatusCode(ST_PERMISSION_DENIED);
                            }
                            else
                            {
                                MFile* normalized = MFSOwner::File(f->url);
                                if (!mstr::startsWith(f->url, normalized->url.c_str()))
                                {
                                    delete f;
                                    f = normalized;
                                }
                                else
                                {
                                    delete normalized;
                                }

                                m_channels[channel] = new iecChannelHandlerDir(this, f);
                                m_channels[channel]->setName(f->fullUrl());
                                m_numOpenChannels++;
                                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Change Directory Here! channel[%d] numChannels[%d] dir[%s]", channel, m_numOpenChannels, f->url.c_str());
                                //m_cwd.reset(MFSOwner::File(f->url));
                                set_cwd(f->url, true);
                                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "f.url[%s] m_cwd[%s]", f->url.c_str(), m_cwd->url.c_str());
                                Debug_printv("Reading directory [%s]", f->url.c_str());
                                setStatusCode(ST_OK);
                            }
                            f = nullptr; // f will be deleted in iecChannelHandlerDir destructor
                        }
                        // else
                        // {
                        //     // can't read file entries => treat directory as file
                        //     Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Treating directory as file [%s]", f->url.c_str());
                        //     m_cwd.reset(MFSOwner::File(f->url));
                        //     Debug_printv("Treating directory as file [%s]", f->url.c_str());
                        // }
                    }
                    else
                    {
                        // cannot write to directory
                        Debug_printv("Error: attempt to write to directory [%s]", f->url.c_str());
                        setStatusCode(ST_WRITE_PROTECT_ON);
                        delete f;
                        f = nullptr;
                    }
                }

                if( f != nullptr )
                {
                    // opening a file
                    if( (mode == std::ios_base::in) && !f->exists() )
                    {
                        Debug_printv("Error: file doesn't exist [%s]", f->url.c_str());
                        setStatusCode(ST_FILE_NOT_FOUND);
                    }
                    else if( (mode == std::ios_base::out) && f->media_image.size()>0 && (!f->isWritable || f->pathInStream.empty()) )
                    {
                        // Files INSIDE writable disk media (D64 etc. on flash/SD) can be
                        // written; overwriting the image itself or media on read-only
                        // sources is still refused.
                        Debug_printv("Error: media not writable [%s]", f->url.c_str());
                        setStatusCode(ST_WRITE_PROTECT_ON);
                    }
                    else if( (mode == std::ios_base::out) && f->exists() && !overwrite && f->type != MFILE_OTHER )
                    {
                        // MFILE_OTHER (QR://, HASH://, …) are written in place to update
                        // their generated data, so the "already exists" guard (which
                        // would need @ to overwrite a real file) does not apply.
                        Debug_printv("Error: file exists [%s]", f->url.c_str());
                        setStatusCode(ST_FILE_EXISTS);
                    }
                    else
                    {
                        // VDrive needs full path including pathInStream
                        std::string full_path = f->url;
                        if ( f->pathInStream.size() ) {
                            full_path += '/'; full_path += f->pathInStream;
                        }

                        //Debug_printv( ANSI_RED_BOLD_HIGH_INTENSITY "VDrive Opening file [%s] mode[%s]", full_path.c_str(), mode==std::ios_base::in ? "read" : "write");
                        if( Meatloaf.use_vdrive && !is_dir && (m_vdrive=VDrive::create(m_devnr-8, full_path.c_str()))!=nullptr )
                        {
                            Debug_printv("Created VDrive for URL %s. Loading directory.", full_path.c_str());
                            delete f;
                            return m_vdrive->openFile(channel, "$");
                        }

                        // Clear first so a reason left by an earlier request
                        // can't be attached to this failure.
                        http_clear_tls_error();
                        std::shared_ptr<MStream> new_stream = f->getSourceStream(mode);

                        if( new_stream==nullptr )
                        {
                            Debug_printv("Error: could not get stream for file [%s]", f->url.c_str());
                            if( http_had_tls_error() )
                                setStatusCode(ST_DRIVE_NOT_READY, http_last_tls_status());
                            else
                                setStatusCode(ST_DRIVE_NOT_READY);
                        }
                        // else if( (mode == std::ios_base::in) && new_stream->size()==0 && !is_dir )
                        // {
                        //     Debug_printv("Error: file length is zero [%s]", f->url.c_str());
                        //     //delete new_stream;
                        //     setStatusCode(ST_FILE_NOT_FOUND);
                        // }
                        else if( !new_stream->isOpen() )
                        {
                            Debug_printv("Error: could not open file stream [%s]", f->url.c_str());
                            //delete new_stream;
                            if( http_had_tls_error() )
                                setStatusCode(ST_DRIVE_NOT_READY, http_last_tls_status());
                            else
                                setStatusCode(ST_DRIVE_NOT_READY);
                        }
                        else
                        {
                            Debug_printv("Stream created for file [%s] pathInStream[%s]", f->url.c_str(), f->pathInStream.c_str());
                            // new_stream will be deleted in iecChannelHandlerFile destructor
                            m_channels[channel] = new iecChannelHandlerFile(this, new_stream, is_dir ? 0x0801 : -1); // 0x0801 = overrides load address to start of basic for C64
                            // fullUrl(), not url: for a file inside a container url
                            // names the CONTAINER, and the entry is in pathInStream.
                            m_channels[channel]->setName(f->fullUrl());
                            m_numOpenChannels++;
                            setStatusCode(ST_OK);

                            Debug_printv("isDir[%d] isRandomAccess[%d] isBrowsable[%d]", is_dir, new_stream->isRandomAccess(), new_stream->isBrowsable());
                            if ( f->type == MFILE_OTHER )
                            {
                                // QR://, HASH://, retropixels://, etc. are file-like
                                // resources, not locations — opening (or writing) one
                                // acts on it in place; never move the current directory.
                                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "MFILE_OTHER, leaving cwd unchanged [%s]", f->url.c_str() );
                            }
                            else if ( new_stream->isRandomAccess() || new_stream->isBrowsable() )
                            {
                                // This was a directory.  Set m_cwd to the directory
                                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "dir url[%s]", f->url.c_str() );
                                //m_cwd.reset(MFSOwner::File(f->url));
                                set_cwd(f->url, true);
                            }
                            else if (wasDirListing && f->url == m_cwd->url)
                            {
                                // A plain "$" listing degenerated into re-opening the file m_cwd
                                // already points at (cwd is parked directly on a file, not a
                                // directory) — just a look, not a navigation, so leave cwd alone.
                                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "dir listing on file, not changing cwd [%s]", f->url.c_str() );
                            }
                            else
                            {
                                // This was a file.  Set m_cwd to the directory containing it.
                                // If the file is inside a container (D64/D81/DNP etc.), pathInStream
                                // is non-empty and f->url is the container — stay there.
                                // Otherwise f->base() gives the parent directory on the filesystem.
                                if (f->pathInStream.size()) {
                                    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "file in container, staying at [%s]", f->url.c_str() );
                                    set_cwd(f->url, true);
                                } else {
                                    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "file base[%s]", f->base().c_str() );
                                    //m_cwd.reset(MFSOwner::File(f->base()));
                                    set_cwd(f->base(), true);
                                }
                            }

                            // {
                            //     // Debug_printv( "url[%s] pathInStream[%s]", f->url.c_str(), f->pathInStream.c_str() );
                            //     // if( new_stream->has_subdirs )
                            //     // {
                            //     //     Filesystem supports sub directories => set m_cwd to parent directory of file
                            //     //     Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Subdir Change Directory Here! stream[%s] > f[%s]", new_stream->url.c_str(), f->url.c_str() );
                            //     //     m_cwd.reset(MFSOwner::File(f->url));
                            //     // }
                            //     // else
                            //     // {
                            //     //     Handles media files that may have '/' as part of the filename
                            //     //     f = MFSOwner::File( new_stream->url );
                            //     //     Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Change Directory Here! stream[%s] > f[%s]", new_stream->url.c_str(), f->url.c_str() );
                            //     //     m_cwd.reset(MFSOwner::File(f->sourceFile));
                            //     // }
                            // }
                        }

                        if ( m_statusCode != ST_OK && m_statusCode != ST_DRIVE_NOT_READY )
                        {
                            if (f->pathInStream.size()) {
                                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "file in container, staying at [%s]", f->url.c_str() );
                                set_cwd(f->url);
                            } else {
                                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Change Directory Here! url[%s] > base[%s]", f->url.c_str(), f->base().c_str() );
                                set_cwd(f->base());
                            }
                        }
                        mlConfig.save();
                    }

#ifdef DEBUG
                    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "m_cwd[%s]", m_cwd==nullptr ? "NULL" : m_cwd->url.c_str());
                    m_cwd->dump();
#endif
                }
                
                delete f;
            }
        }

        return m_channels[channel]!=NULL;
    }
}


void iecDrive::close(uint8_t channel)
{
    //Debug_printv("iecDrive::close(#%d, %d)", m_devnr, channel);

//#ifdef USE_VDRIVE
    if( Meatloaf.use_vdrive &&  m_vdrive!=nullptr )
    {
        double seconds = (esp_timer_get_time()-m_timeStart) / 1000000.0;
        m_vdrive->closeFile(channel);
        Debug_printv("File closed on VDrive");
        double cps = m_byteCount / seconds;
        Debug_printv("Transferred %lu bytes in %0.2f seconds @ %0.2f B/s", m_byteCount, seconds, cps);
    }
    else 
//#endif
    if( m_channels[channel] != nullptr )
    {
        delete m_channels[channel];
        m_channels[channel] = nullptr;
        if( m_numOpenChannels>0 ) m_numOpenChannels--;
        //Debug_printv("Channel %d closed.", channel);
        ImageBroker::validate();
#ifdef DEBUG
        ImageBroker::dump();
        SessionBroker::dump();
        Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "id[%d] cwd[%s]", m_devnr, m_cwd==nullptr ? "NULL" : m_cwd->url.c_str());
        m_cwd->dump();
        Debug_memory();
#endif
    }

#ifdef ENABLE_DISPLAY
    LEDS.activity = false;
#endif
}


uint8_t iecDrive::write(uint8_t channel, uint8_t *data, uint8_t dataLen, bool eoi)
{
#ifdef MEATLOAF_CARD_DETECT_HOTPLUG
    if (synchronizeSdMedia())
    {
        setStatusCode(ST_DRIVE_NOT_READY);
        return 0;
    }
#endif

//#ifdef USE_VDRIVE
    if( Meatloaf.use_vdrive && m_vdrive!=nullptr )
    {
        size_t n = dataLen;
        if( !m_vdrive->write(channel, data, &n) )
            setStatusCode(ST_VDRIVE);

        if( (m_byteCount+n)/256 > m_byteCount/256 ) { printf("."); fflush(stdout); }
        m_byteCount += n;

        return n;
    }
    else
//#endif
    {
      iecChannelHandler *handler = m_channels[channel];
        if( handler==nullptr )
        {
            if( m_statusCode==ST_OK ) setStatusCode(ST_FILE_NOT_OPEN);
            return 0;
        }
        else
            return handler->write(data, dataLen);
    }
}


uint8_t iecDrive::read(uint8_t channel, uint8_t *data, uint8_t maxDataLen, bool *eoi)
{ 
#ifdef MEATLOAF_CARD_DETECT_HOTPLUG
    if (synchronizeSdMedia())
    {
        setStatusCode(ST_DRIVE_NOT_READY);
        return 0;
    }
#endif

    //Debug_printv("channel[%d] maxDataLen[%d]", channel, maxDataLen);
//#ifdef USE_VDRIVE
    if( Meatloaf.use_vdrive && m_vdrive!=nullptr )
    {
        size_t n = maxDataLen;
        if( !m_vdrive->read(channel, data, &n, eoi) )
            setStatusCode(ST_VDRIVE);

        if( (m_byteCount+n)/256 > m_byteCount/256 ) { printf("."); fflush(stdout); }
        m_byteCount += n;
    
        return n;
    }
    else
//#endif
    { 
        iecChannelHandler *handler = m_channels[channel];
        if( handler==nullptr )
        {
            Debug_printv("Error: attempt to read from unopened channel[%d]", channel);
            if( m_statusCode==ST_OK ) setStatusCode(ST_FILE_NOT_OPEN);
            return 0;
        }
        else
        {
            uint8_t bytes_read = handler->read(data, maxDataLen);
            if( m_statusCode==ST_FILE_NOT_FOUND)
            {
                // base() is the container's PARENT directory for anything
                // hosted by an image - it knows nothing of pathInStream - so
                // backing out here would eject the drive from the image over a
                // file that was merely absent. C64 OS probes for an optional
                // /TEMPORARY/:UPDATER; that miss used to move the drive to the
                // image's directory on SD and every open after it failed.
                bool inContainer = m_cwd->sourceFile != nullptr &&
                                   !m_cwd->sourceFile->pathInStream.empty();
                if( !inContainer )
                {
                    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Subdir Change Directory Here! stream[%s] > base[%s]", m_cwd->url.c_str(), m_cwd->base().c_str());
                    set_cwd(m_cwd->base());
                }
            }

            //Debug_printv("Read %d bytes from channel[%d]", bytes_read, channel);
            return bytes_read;
        }
        
    }
}

// we override the "executeData" because some commands may include NUL or CR characters which 


// Move a channel's pointer, for B-P and F-P.
//
//   B-P (within_block)  the pointer WITHIN the block B-R/U1 selected, 0-255.
//                       It addresses the block, so it counts from the block's
//                       byte 0 even when B-R left the readable window
//                       starting at byte 1.
//   F-P                 an absolute offset. On a direct-access channel it
//                       re-bases the block window onto the block containing
//                       that offset, so "one block at a time" still holds.
bool iecDrive::positionChannel(uint32_t chan, uint32_t pos, bool within_block)
{
    if( chan > 15 )
    {
        setStatusCode(ST_SYNTAX_UNKNOWN);
        return false;
    }

    iecChannelHandler *channel = m_channels[chan];
    if( channel == nullptr )
    {
        setStatusCode(ST_FILE_NOT_OPEN);
        return false;
    }

    // A directory channel generates its listing and has no stream.
    auto stream = channel->getStream();
    if( stream == nullptr )
    {
        setStatusCode(ST_SYNTAX_INVALID);
        return false;
    }

    uint32_t target = pos;

    if( within_block )
    {
        // No block selected means no origin to count from -- B-P is not a
        // file-channel command, F-P is.
        if( !channel->hasBlockWindow() )
        {
            setStatusCode(ST_SYNTAX_INVALID);
            return false;
        }

        if( pos >= stream->block_size )
        {
            setStatusCode(ST_SYNTAX_UNKNOWN);
            return false;
        }

        // Only the pointer moves; the window's end is whatever B-R/U1 set.
        target = channel->blockBase() + pos;

        if( !stream->position(target) )
        {
            setStatusCode(ST_SYNTAX_UNKNOWN);
            return false;
        }

        channel->repositioned(target);
        setStatusCode(ST_OK);
        return true;
    }

    // F-P seeks anywhere in the file or container, so on a direct-access
    // channel it LEAVES block mode and restores the container's full extent --
    // otherwise the block window would stop the read 256 bytes later. B-R/U1
    // put the channel back into block mode.
    if( channel->hasBlockWindow() )
    {
        if( channel->fullSize() > 0 )
            stream->setSize(channel->fullSize());

        channel->clearBlockWindow();
    }

    if( !stream->position(target) )
    {
        setStatusCode(ST_SYNTAX_UNKNOWN);
        return false;
    }

    channel->repositioned(target);
    setStatusCode(ST_OK);
    return true;
}


// Point a channel at a disk block, for B-R/B-W and U1/U2.
//
// There is no separate 256-byte block buffer here the way a real drive has
// one: a channel IS a stream at an offset. So a block command is a seek --
// the data then moves through ordinary reads and writes on that channel,
// which is also what makes it work from the console.
//
// `extra` is the offset within the block to land on: B-R uses 1 because CBM
// treats byte 0 as the count of valid bytes, U1 uses 0 and gives the whole
// 256. Sets the status and returns false on every failure.
bool iecDrive::seekChannelToBlock(uint8_t channel_num, uint8_t track, uint8_t sector,
                                  uint8_t extra, bool for_write)
{
    // m_channels has 16 entries and the parsed channel is a full byte.
    if( channel_num > 15 )
    {
        setStatusCode(ST_SYNTAX_UNKNOWN);
        return false;
    }

    iecChannelHandler *channel = m_channels[channel_num];
    if( channel == nullptr )
    {
        setStatusCode(ST_FILE_NOT_OPEN);
        return false;
    }

    // A directory channel generates its listing and has no stream.
    auto stream = channel->getStream();
    if( stream == nullptr )
    {
        setStatusCode(ST_SYNTAX_INVALID);
        return false;
    }

    if( for_write && !(stream->mode & std::ios_base::out) )
    {
        // Opened read-only: OPEN <ch>,8,<sa>,"#,S,W" gets a writable one.
        setStatusCode(ST_WRITE_PROTECT_ON, track, sector);
        return false;
    }

    int32_t offset = stream->sectorByteOffset(track, sector);
    if( offset < 0 )
    {
        setStatusCode(ST_ILLEGAL_TS_COMMAND, track, sector);
        return false;
    }

    uint32_t base   = (uint32_t) offset;
    uint32_t start  = base + extra;
    uint32_t length = stream->block_size;

    // B-R/B-W reserve byte 0 of the block as the count of valid bytes, so the
    // window is that many bytes starting at byte 1. U1/U2 have no count byte
    // and give the whole block. Reading the count costs one container read,
    // and the seek below puts the stream back where the caller expects it.
    if( extra > 0 )
    {
        if( !stream->position(base) )
        {
            setStatusCode(ST_ILLEGAL_TS_COMMAND, track, sector);
            return false;
        }

        uint8_t count = 0;
        if( stream->read(&count, 1) != 1 )
        {
            setStatusCode(ST_READ_NO_DATA, track, sector);
            return false;
        }
        length = count;
    }

    // The window is expressed to the stream as its size: that is what stops a
    // read at the end of the block, since the reader asks for BUFFER_SIZE.
    stream->setSize(start + length);

    // position() seeks AND records where it is, which seekSector() cannot do.
    if( !stream->position(start) )
    {
        setStatusCode(ST_ILLEGAL_TS_COMMAND, track, sector);
        return false;
    }

    // The channel holds bytes read AHEAD of the stream; without this the next
    // read serves them instead of the block just seeked to.
    channel->setBlockWindow(base, start, length);

    setStatusCode(ST_OK);
    return true;
}


// may not work in the text-based "execute" function. Note that while commands handled here
// are all text-based, M-R and M-W commands passed on to the VDrive may contain binary data.
void iecDrive::executeData(const uint8_t *data, uint8_t dataLen)
{
    // Commands issued after a swap operate from root, never in the image that
    // belonged to the removed card.
#ifdef MEATLOAF_CARD_DETECT_HOTPLUG
    synchronizeSdMedia();
#endif

#ifdef ENABLE_DISPLAY
    LEDS.activity = true;
#endif

    // create regular string from the data we were passed
    std::string command = std::string((const char *) data, dataLen);
    if( command.length()>0 && command.back()==0x0d ) command = command.substr(0, command.length()-1);

    Debug_printv("iecDrive::execute(#%d, \"%s\", %d)", m_devnr, getCStringLog(command), dataLen);

    // set status code to OK, failing commands below will set it to the appropriate error code
    IECFileDevice::clearStatus();
    setStatusCode(ST_OK);
    if ( dataLen == 0 )
        return;

    // Activate/Deactivate VDrive mode
    if( command=="VD+" || command=="VD-" )
    {
        Meatloaf.use_vdrive = (command[2]=='+');
        printf("VDrive %sctivated!\r\n", Meatloaf.use_vdrive ? "A" : "Dea");
        return;
    }

//#ifdef USE_VDRIVE
    // check whether we are currently operating in "virtual drive" mode
    if( Meatloaf.use_vdrive && m_vdrive!=nullptr )
    {
        if( (command=="CD_" || command=="CD:_" || command=="CD.." || command=="CD:.." || command=="CD^" || command=="CD:^") || 
            (mstr::startsWith(command, "CD:") && (mstr::contains(command, "ML:") || mstr::contains(command, "://"))) )
        {
            // exit out of virtual drive
            Debug_printv("Closing VDrive");
            delete m_vdrive;
            m_vdrive = NULL;

            // if we're just going up one directory then we're done, otherwise continue
            if( command=="CD_" || command=="CD:_" || command=="CD.." || command=="CD:.." )
                return;
        }
        else
        {
            // execute command within virtual drive
            if( m_vdrive->execute((const char *) data, dataLen)==0 )
            {
                setStatusCode(ST_VDRIVE);
            }
            
            // when executing commands that read data into a buffer or reposition
            // the pointer we need to clear our read buffer of the channel for which
            // this command is issued, otherwise remaining characters in the buffer 
            // will be prefixed to the data from the new record or buffer location
            if( data[0]=='P' && dataLen>=2 )
                clearReadBuffer(data[1] & 0x0f);
            else if( memcmp(data, "U1", 2)==0 || memcmp(data, "B-P", 3)==0 || memcmp(data, "B-R", 3)==0 )
                {
                int i = data[0]=='U' ? 2 : 3;
                while( i<dataLen && !isdigit(data[i]) ) i++;
                if( i<dataLen )
                    {
                    uint8_t channel = data[i]-'0';
                    if( i+1<dataLen && isdigit(data[i+1]) )
                        channel = 10*channel + (data[i+1]-'0');

                    clearReadBuffer(channel);
                    }
                }
            
            return;
        }
    }
//#endif

    // Everything from here on is the DOS: CBM 2.6, SD2IEC and Meatloaf commands.
    ensureDos();
    if ( m_dos == nullptr )
        return;
    m_dos->execute(command, data, dataLen);
}


void iecDrive::setStatusCode(uint8_t code, const std::string &msg)
{
    setStatusCode(code);
    m_statusMessage = msg;   // set after: the call above clears it
}


void iecDrive::setStatusCode(uint8_t code, uint8_t trk, uint8_t sec)
{
    //Debug_printv("code[%d]", code);
    m_statusCode = code;
    m_statusTrk  = trk;
    m_statusSec  = sec;
    m_statusMessage.clear();

    // clear current status buffer to force a call to getStatus()
    clearStatus();

#ifdef ENABLE_DISPLAY
    LEDS.status( code );
#endif

    char statusMsg[64];
    getStatus(statusMsg, sizeof(statusMsg));
    notify_activity(activitySource(), "status", statusMsg);
    Debug_printv("status: %s", getCStringLog(std::string(statusMsg)));
}


std::string iecDrive::consoleExecDos(const std::string &command, bool *isError)
{
    // executeData() is virtual: for device 30 this reaches iecMeatloaf, which
    // answers FujiNet commands by filling the status buffer directly.
    executeData((const uint8_t *) command.data(), (uint8_t) std::min<size_t>(command.size(), 255));

    return consoleStatus(isError);
}


std::string iecDrive::consoleStatus(bool *isError)
{
    // Sample the error state before consuming the status - getStatusData()
    // resets the code to OK, just as a channel-15 read does.
    if (isError != nullptr)
        *isError = hasError();

    // Read the status the way the bus master would: a reply already pushed
    // into the status buffer wins (iecMeatloaf answers FujiNet commands with
    // setStatus()), and only an empty buffer falls through to getStatusData().
    char buffer[IECFILEDEVICE_STATUS_BUFFER_SIZE];
    std::string status;
    uint8_t len = peekStatus(buffer, sizeof(buffer));
    if( len > 0 )
    {
        status.assign(buffer, len);
        clearStatus();   // consume it, as a channel-15 read would
    }
    else
    {
        bool eoi = false;
        len = getStatusData(buffer, sizeof(buffer), &eoi);
        status.assign(buffer, len);
    }

    while (!status.empty() && (status.back() == '\r' || status.back() == '\n'))
        status.pop_back();

    return status;
}


std::vector<iecDrive::ChannelInfo> iecDrive::consoleChannels()
{
    std::vector<ChannelInfo> open_channels;

    // VDrive keeps its own channel table and offers no per-channel query, so
    // there is nothing to walk in that mode.  The caller compares the row
    // count against getNumOpenChannels() so the gap is visible rather than
    // silently reported as "no channels open".
    if( Meatloaf.use_vdrive && m_vdrive!=nullptr )
        return open_channels;

    for(uint8_t channel = 0; channel <= 15; channel++)
    {
        iecChannelHandler *handler = m_channels[channel];
        if( handler==nullptr ) continue;

        ChannelInfo info;
        info.channel    = channel;
        info.name       = handler->name();
        info.has_stream = false;
        info.size       = 0;
        // Where the READER is, not where the stream is -- see
        // iecChannelHandler::position(). A directory channel has a real
        // position too, even though it has no stream to give a size.
        info.position   = handler->position();

        // A directory channel generates its listing rather than reading one,
        // so it has no stream and no size to report.
        auto stream = handler->getStream();
        if( stream != nullptr )
        {
            info.has_stream = true;
            info.size       = stream->size();
        }

        // A direct-access channel is a window on one block: report the block's
        // readable length and the pointer WITHIN it, which is what B-P speaks.
        // Reporting the stream's size there would show the window's end offset
        // in the image, which is not a size at all.
        if( handler->hasBlockWindow() )
        {
            info.has_stream = true;
            info.size       = handler->blockLength();
            info.position   = handler->position() >= handler->blockBase()
                                ? handler->position() - handler->blockBase()
                                : 0;
        }

        open_channels.push_back(info);
    }

    return open_channels;
}


bool iecDrive::consoleOpen(uint8_t channel, const std::string &name)
{
    // open() is virtual, so device 30 reaches iecMeatloaf's override.  The
    // length is a uint8_t on the bus too, so a longer name is not a case the
    // drive could ever see from a C64.
    return open(channel, name.c_str(), (uint8_t) std::min<size_t>(name.size(), 255));
}


uint8_t iecDrive::consoleRead(uint8_t channel, uint8_t *buffer, uint8_t bufferSize)
{
    // The non-VDrive path never writes eoi - end of file is a return of 0 -
    // but read() takes the pointer, so give it somewhere real to write.
    bool eoi = false;
    return read(channel, buffer, bufferSize, &eoi);
}


uint8_t iecDrive::consoleWrite(uint8_t channel, const uint8_t *buffer, uint8_t bufferSize, bool eoi)
{
    // write() takes a non-const pointer because the bus handler hands it its
    // own receive buffer; nothing in the path modifies it.
    return write(channel, const_cast<uint8_t *>(buffer), bufferSize, eoi);
}


void iecDrive::consoleClose(uint8_t channel)
{
    // close() is a no-op for a channel that is not open, on both the VDrive
    // and the MStream path, so the console's "close everything" can simply
    // sweep 0-15 rather than needing a per-channel query VDrive does not have.
    close(channel);
}


bool iecDrive::hasError()
{
    return (m_statusCode>=20) && (m_statusCode!=ST_DOSVERSION);
}


bool iecDrive::hasMemExeError()
{
    return (m_statusCode==ST_VDRIVE) && (m_vdrive!=NULL) && (m_vdrive->getStatusCode()==99);
}


uint8_t iecDrive::getNumOpenChannels() 
{ 
//#ifdef USE_VDRIVE
    if ( Meatloaf.use_vdrive )
        return m_vdrive!=nullptr ? m_vdrive->getNumOpenChannels() : m_numOpenChannels;
//#else
    return m_numOpenChannels;
//#endif
}


uint8_t iecDrive::getStatusData(char *buffer, uint8_t bufferSize, bool *eoi)
{ 
    uint8_t res;
    Debug_printv("iecDrive::getStatus(#%d)", m_devnr);

    // if we have an active VDrive then just return its status
    if( m_vdrive!=NULL )
        {
        m_statusCode = ST_OK;
        res = m_vdrive->getStatusBuffer(buffer, bufferSize, eoi);
        }
    else
        {
        // IECFileDevice::getStatusData will in turn call iecDrive::getStatus()
        getStatus(buffer, bufferSize);
        res = strlen(buffer);
        *eoi = true;
        }

    m_statusCode = ST_OK;
    m_statusTrk  = 0;
    m_statusSec  = 0;
    
    #ifdef ENABLE_DISPLAY
    LEDS.status( ST_OK );
    #endif

    Debug_printv("status: %s", getCStringLog(std::string(buffer, res)));
    return res;
}


void iecDrive::getStatus(char *buffer, uint8_t bufferSize)
{
    // A caller-supplied message wins - it carries detail the numeric code
    // cannot.  See setStatusCode(code, msg).
    if( !m_statusMessage.empty() )
    {
        snprintf(buffer, bufferSize, "%02d,%s,%02d,%02d\r",
                 m_statusCode, m_statusMessage.c_str(), m_statusTrk, m_statusSec);
        return;
    }

    const char *msg = NULL;
    std::string dosVersionMsg;  // must outlive the switch below - msg points into it
    switch( m_statusCode )
    {
        case ST_OK                  : msg = " OK"; break;
        case ST_SCRATCHED           : msg = "FILES SCRATCHED"; break;
        case ST_PARTITION_SELECTED  : msg = "PARTITION SELECTED"; break;
        case ST_PARTITION_ILLEGAL   : msg = "ILLEGAL PARTITION"; break;

        case ST_READ_NO_HEADER      :
        case ST_READ_NO_SYNC        :
        case ST_READ_NO_DATA        :
        case ST_READ_CHECKSUM       :
        case ST_READ_ERROR_GCR      : 
            msg = "READ ERROR"; break;

        case ST_WRITE_VERIFY        : msg = "WRITE ERROR"; break;
        case ST_WRITE_PROTECT_ON    : msg = "WRITE PROTECT"; break;
        // 30 became reachable when CP<n> started distinguishing "not a
        // partition number" from "no such partition"; without a case here it
        // fell through to "UNKNOWN ERROR".
        case ST_SYNTAX_UNKNOWN      : msg = "SYNTAX ERROR"; break;
        case ST_SYNTAX_INVALID      : msg = "INVALID COMMAND"; break;
        case ST_SYNTAX_BAD_NAME     : msg = "INVALID FILENAME"; break;
        case ST_FILE_NOT_FOUND      : msg = "FILE NOT FOUND"; break;
        case ST_FILE_NOT_OPEN       : msg = "FILE NOT OPEN"; break;
        case ST_FILE_EXISTS         : msg = "FILE EXISTS"; break;
        // 66 became reachable when the block commands started validating
        // track/sector; without a case here it printed "UNKNOWN ERROR", the
        // same gap code 30 had.
        case ST_ILLEGAL_TS_COMMAND  : msg = "ILLEGAL TRACK OR SECTOR"; break;
        case ST_DOSVERSION          :
            dosVersionMsg = std::string(PRODUCT_ID " ") + ESP.getFirmwareVersion().c_str();
            msg = dosVersionMsg.c_str();
            break;
        case ST_NO_CHANNEL          : msg = "NO CHANNEL"; break;
        case ST_DIR_ERROR           : msg = "DIR ERROR"; break;
        case ST_DISK_FULL           : msg = "DISK FULL"; break;
        case ST_DRIVE_NOT_READY     : msg = "DRIVE NOT READY"; break;
        case ST_FILE_TYPE_MISMATCH  : msg = "FILE TYPE MISMATCH"; break;
        case ST_PERMISSION_DENIED   : msg = "PERMISSION DENIED"; break;
        default                     : msg = "UNKNOWN ERROR"; break;
    }

    snprintf(buffer, bufferSize, "%02d,%s,%02d,%02d\r", m_statusCode, msg, m_statusTrk, m_statusSec);
}


void iecDrive::reset()
{
    Debug_printv("iecDrive::reset(#%d)", m_devnr);
    setStatusCode(ST_DOSVERSION);

    // A C64-side RESET restores the bus's own end()/sleep state (see
    // IECBusHandler::task()'s RESET handling); do the same for a device put
    // to sleep at runtime via `iec sleep <id>` — restore it to its persisted
    // enabled state (a config-disabled device correctly stays disabled).
    restoreActiveFromConfig();

    // close all open channels
    for(int i=0; i<16; i++)
        if( m_channels[i]!=nullptr )
        close(i);
    m_numOpenChannels = 0;
    m_memory.reset();

    //#ifdef USE_VDRIVE
    if( m_vdrive!=nullptr ) 
    {
        m_vdrive->closeAllChannels();
        m_vdrive->execute("UJ", 2, false);
    }

    //#endif

    SystemFileDevice::reset();

    // clear all image and session brokers
    ImageBroker::clear();
    SessionBroker::clear();

#ifdef ENABLE_DISPLAY
    LEDS.idle();
    Debug_printv("Stop Activity");
#endif

    Debug_memory();
}


#ifndef DISABLE_TAPE   // MEATLOAF-GATE
// Path of the ".tap"/".dmp"/".htap" container within 'path', or "" if none
static std::string tape_container_of(const std::string &path)
{
    static const char *exts[] = { ".tap", ".dmp", ".htap" };
    std::string lower = path;
    for (auto &c : lower)
        c = tolower(c);

    for (auto ext : exts)
    {
        size_t elen = strlen(ext);
        size_t p = lower.find(ext);
        while (p != std::string::npos)
        {
            size_t end = p + elen;
            if (end == lower.size() || lower[end] == '/')
                return path.substr(0, end);
            p = lower.find(ext, p + 1);
        }
    }
    return "";
}

// Tape image commands: "T-C <ms|MMM:SS>" sets the tape counter (read
// position by time), "T-I" scans the tape and generates its .idx file
void iecDrive::tapeCommand(std::string command)
{
    std::string container = tape_container_of(m_cwd != nullptr ? m_cwd->url : "");
    if (container.empty())
    {
        Debug_printv("Not on a tape image [%s]", m_cwd != nullptr ? m_cwd->url.c_str() : "");
        setStatusCode(ST_SYNTAX_INVALID);
        return;
    }

    if (command[2] == 'C')
    {
        // Argument after "T-C", optionally separated by ':' or space
        std::string arg = command.substr(3);
        if (!arg.empty() && (arg[0] == ':' || arg[0] == ' '))
            arg = arg.substr(1);
        arg = mstr::toUTF8(arg);
        mstr::trim(arg);

        auto image = ImageBroker::obtain<TAPMStream>("tap", container);
        if (image == nullptr)
        {
            setStatusCode(ST_DRIVE_NOT_READY);
            return;
        }
        setStatusCode(image->setCounter(arg) ? ST_OK : ST_SYNTAX_INVALID);
        return;
    }

    // T-I: build the .idx index file
    std::unique_ptr<MFile> f(MFSOwner::File(container));
    if (f == nullptr)
    {
        setStatusCode(ST_DRIVE_NOT_READY);
        return;
    }
    // tape extensions always resolve to a TAPMFile
    TAPMFile *tf = static_cast<TAPMFile *>(f.get());
    setStatusCode(tf->buildIndex() ? ST_OK : ST_WRITE_VERIFY);
}
#endif // !DISABLE_TAPE

// CMD "CP<n>" - change the selected partition of the mounted image. Works on
// a CMD HD/FD image (DHD, D1M/D2M/D4M) and on an IDE64 CFS image (.hdd); the
// per-format bound on which partitions actually EXIST lives in hdpart::select().
//
// Three failures, three codes. The split is by WHAT FAILED, not by how far out
// of range the number was:
//
//   31 INVALID COMMAND (ST_SYNTAX_INVALID)
//                    - there is no partition table here at all, so CP is not a
//                      command this image understands. Nothing to do with the
//                      number, which is never even looked at.
//   30 SYNTAX ERROR (ST_SYNTAX_UNKNOWN)
//                    - the argument is not a partition number: it did not
//                      parse, or it cannot fit the one-byte partition number a
//                      CBM drive uses (CP256 and up).
//   77 SELECTED PARTITION ILLEGAL
//                    - it IS a well-formed partition number (0-255) but this
//                      image has no such partition. That covers CP0, which is
//                      reserved for "the currently selected partition" and is
//                      therefore never a real one, and every unused number up
//                      to 255 - including 17-255 on a CFS image, whose table
//                      only ever holds 16.
//
// Note ST_SYNTAX_UNKNOWN is 30 and ST_SYNTAX_INVALID is 31 (include/
// cbm_defines.h) - the names do not read in that order, so check the value
// rather than the name when touching this.
void iecDrive::changePartition(int pnum)
{
    hdpart::Target t = hdpart::targetFor(m_cwd != nullptr ? m_cwd->url : "");
    Debug_printv("change partition pnum[%d] container[%s]", pnum, t.container.c_str());

    if (!t)
    {
        setStatusCode(ST_SYNTAX_INVALID);   // 31 - CP means nothing here
        return;
    }

    // Not a partition number: unparseable (the caller's -1 sentinel) or beyond
    // the one-byte range a partition number can occupy.
    if (pnum < 0 || pnum > 255)
    {
        setStatusCode(ST_SYNTAX_UNKNOWN);   // 30 - not a partition number
        return;
    }

    if (hdpart::select(t, pnum))
    {
        // The new partition's root becomes the working directory
        set_cwd(t.container, true);
        setStatusCode(ST_PARTITION_SELECTED, pnum);
    }
    else
    {
        setStatusCode(ST_PARTITION_ILLEGAL, pnum);
    }
}

// Records which DOS the working directory wants. Never swaps it: this runs
// from set_cwd(), which the DOS itself calls (CD, CP), so the swap is left to
// ensureDos() at the next point where no DOS code is on the stack.
void iecDrive::selectDos()
{
    m_dosKind = hdpart::targetFor(m_cwd != nullptr ? m_cwd->url : "").kind;
}

// The only place m_dos is created or replaced. Called at the top of open() and
// in executeData() before the DOS runs.
void iecDrive::ensureDos()
{
    if ( m_dos != nullptr && m_dos->kind() == m_dosKind )
        return;

    // Every kind is served by CbmDos until the media-specific DOSes exist.
    CbmDos *dos = new (std::nothrow) CbmDos(*this);
    if ( dos == nullptr )
    {
        Debug_printv("Error: could not allocate DOS");
        return;   // keep the current one, if any
    }
    m_dos.reset(dos);
}

void iecDrive::dosSetStatus(uint8_t code, uint8_t trk, uint8_t sec) { setStatusCode(code, trk, sec); }
void iecDrive::dosSetStatusMessage(uint8_t code, const std::string &msg) { setStatusCode(code, msg); }
void iecDrive::dosSetStatusData(const char *data, uint8_t len) { SystemFileDevice::setStatus(data, len); }
uint8_t iecDrive::dosStatusCode() { return m_statusCode; }
MFile *iecDrive::dosCwd() { return m_cwd.get(); }
void iecDrive::dosSetCwd(const std::string &path) { set_cwd(path); }
iecChannelHandler *iecDrive::dosChannel(uint32_t n) { return n < 16 ? m_channels[n] : nullptr; }
void iecDrive::dosClearReadBuffer(uint8_t channel) { clearReadBuffer(channel); }
bool iecDrive::dosPositionChannel(uint32_t ch, uint32_t pos, bool within_block) { return positionChannel(ch, pos, within_block); }
bool iecDrive::dosSeekChannelToBlock(uint8_t ch, uint8_t t, uint8_t s, uint8_t extra, bool for_write) { return seekChannelToBlock(ch, t, s, extra, for_write); }
void iecDrive::dosChangePartition(int pnum) { changePartition(pnum); }
#ifndef DISABLE_TAPE   // MEATLOAF-GATE
void iecDrive::dosTapeCommand(const std::string &command) { tapeCommand(command); }
#else
void iecDrive::dosTapeCommand(const std::string &command) { (void) command; }
#endif
driveMemory &iecDrive::dosMemory() { return m_memory; }
void iecDrive::dosEnableFastLoader(uint8_t family, bool enable) { enableFastLoader(family, enable); }
void iecDrive::dosReset() { reset(); }
uint8_t iecDrive::dosDeviceNumber() { return m_devnr; }
bool iecDrive::dosUseVDrive() { return Meatloaf.use_vdrive; }
bool iecDrive::dosCreateDiskImage(const std::string &url, const std::string &diskname) { return VDrive::createDiskImage(url.c_str(), NULL, diskname.c_str(), false); }

void iecDrive::set_cwd(std::string path, bool verified)
{
#ifdef MEATLOAF_CARD_DETECT_HOTPLUG
    synchronizeSdMedia();
#endif

    // Isolate path
    if ( mstr::startsWith(path, ":") || mstr::startsWith(path, " ") )
        path = mstr::drop(path, 1);

    if ( !path.size() )
        path = "/";

    Debug_printv("path[%s]", path.c_str());

    if (m_cwd == nullptr) {
        Debug_printv("m_cwd is null, resetting to root");
        m_cwd.reset(MFSOwner::File("/", true));
    }

    std::string old_url = m_cwd->url;

#ifdef IEC_SUPPORT_SECTOROPS
    // The container may be about to change; a cached block stream on the old
    // one would serve the wrong disk.
    releaseSectorStream();
#endif

    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Changing directory to [%s][%s]", m_cwd ? m_cwd->url.c_str() : "<null>", path.c_str());
    if ( m_cwd->url == path)
    {
        Debug_printv("Already in directory [%s]", path.c_str());
        setStatusCode(ST_OK);
        return;
    }

    // When verified=true, the caller already checked that the path is valid.
    // Skip expensive verification (isDirectory, exists, getSourceStream) that
    // creates redundant HTTP connections for network URLs.
    if (verified) {
        MFile *n = MFSOwner::File(path);
        if (n != nullptr) {
            m_cwd.reset(n);
            selectDos();
            setStatusCode(ST_OK);
            persistConfig();
            notify_activity(activitySource(), "path", m_cwd->url);
        } else {
            setStatusCode(ST_FILE_NOT_FOUND);
        }
        return;
    }

    MFile *n = m_cwd ? m_cwd->cd( path ) : nullptr;
    if( n != nullptr )
    {
        // bug in HTTPMFile: must call isDirectory() before getSourceStream(), otherwise
        // getSourceStream() call will hang for several seconds for HTTP files
        bool isDirectory = n->isDirectory();

        // Only create a stream for non-directory files (needed for VDrive check).
        // For directories, isDirectory alone is sufficient for the conditions below,
        // and creating a stream is expensive (extracts data from archives, etc.)
        bool haveStream = false;
        if ( !isDirectory && n->exists() ) {
            std::shared_ptr<MStream> s = n->getSourceStream();
            haveStream = (s != nullptr);
        }

        Debug_printv("url[%s] isDirectory[%i] haveStream[%i]", n->url.c_str(), isDirectory, haveStream);

//#ifdef USE_VDRIVE
        if( Meatloaf.use_vdrive )
        {
            // Build the full path for VDrive: for entries inside containers (e.g. a D81 inside a
            // ZIP), n->url is the container URL and n->pathInStream is the entry path.
            // VDrive uses MFSOwner::File() internally, so the combined URL resolves correctly.
            std::string vdrive_url = n->url;
            if (!n->pathInStream.empty()) {
                vdrive_url += '/'; vdrive_url += n->pathInStream;
            }

            if( n->exists() && !isDirectory && haveStream &&
                (m_vdrive=VDrive::create(m_devnr-8, vdrive_url.c_str()))!=nullptr )
            {
                // we were able to creata a VDrive => this is a valid disk image
                Debug_printv("Created VDrive for URL %s", vdrive_url.c_str());
                setStatusCode(ST_OK);
                delete n;
            }
            else if( n->exists() && (isDirectory || haveStream) )
            {
                m_cwd.reset( n );
                setStatusCode(ST_OK);
            }
            else
            {
                Debug_printv("Could not create VDrive for URL %s", n->url.c_str());
                setStatusCode(ST_FILE_NOT_FOUND);
                delete n;
            }
        }
        else if( n->exists() && (isDirectory || haveStream) )
        {
            m_cwd.reset( n );
            setStatusCode(ST_OK);
        }
        else
        {
            Debug_printv("invalid directory");
            setStatusCode(ST_FILE_NOT_FOUND);
            delete n;
        }
    }
    else
        setStatusCode(ST_SYNTAX_INVALID);

    // Don't persist if new working directory is OTHER
    // QR://, HASH://, etc. are not persistent across reboots
    if (m_cwd->type != MFILE_OTHER)
        persistConfig();

    if (m_cwd->url != old_url)
        notify_activity(activitySource(), "path", m_cwd->url);

    selectDos();
}


#ifdef MEATLOAF_CARD_DETECT_HOTPLUG
bool iecDrive::synchronizeSdMedia()
{
    const uint32_t currentGeneration = fnSDFAT.media_generation();

    // Generation zero is used by boards without generation-aware Card Detect.
    // Keeping this path inert preserves their existing behaviour.
    if (currentGeneration == 0)
        return false;

    if (m_sdMediaGeneration == currentGeneration)
        return false;

    m_sdMediaGeneration = currentGeneration;
    const bool sdDirectory = isSdBacked(m_cwd.get());

    // Absolute SD paths can be opened while the drive is in another directory.
    // Close those channels too, while preserving channels on other media.
    for (auto &channel : m_channels)
    {
        if (channel == nullptr || !isSdPath(channel->name()))
            continue;

        delete channel;
        channel = nullptr;
        if (m_numOpenChannels > 0)
            --m_numOpenChannels;
    }

    if (!sdDirectory)
        return false;

    Debug_printf("SD medium changed: discarding drive #%d context [%s]\r\n",
                 m_devnr, m_cwd->url.c_str());

    if (m_vdrive != nullptr)
    {
        delete m_vdrive;
        m_vdrive = nullptr;
    }

#ifdef IEC_SUPPORT_SECTOROPS
    releaseSectorStream();
#endif

    m_cwd.reset(MFSOwner::File("/", true));
    selectDos();
    setStatusCode(ST_OK);
    notify_activity(activitySource(), "path", "/");
    return true;
}
#endif


/* Mount Disk
    We determine the type of image based on the filename extension.
    If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
    Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t iecDrive::mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    Debug_printv("filename[%s], disksize[%lu] disktype[%d]", filename, disksize, disk_type);

#ifdef IEC_SUPPORT_SECTOROPS
    // a different medium: drop the cached block stream and announce the change
    releaseSectorStream();
#endif

    std::string url;

    if ( !mstr::contains(filename, ":") )
    {
        if (this->m_host) 
            url = this->m_host->get_basepath();
        
        mstr::toLower(url);
        if ( url == "sd" )
            url = "//sd";
    }
    url += filename;

    Debug_printv("DRIVE[#%d] URL[%s] MOUNT[%s]", m_devnr, url.c_str(), filename);

    // open is expecting PETSCII
    //url = mstr::toPETSCII2(url);
    //this->open( 0, url.c_str() );
    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Reset directory to [%s]", url.c_str());
    //m_cwd.reset(m_cwd->cd(url));
    set_cwd(url);

    return MediaType::discover_mediatype(filename); // MEDIATYPE_UNKNOWN
}


// Unmount disk file
void iecDrive::unmount()
{
#ifdef IEC_SUPPORT_SECTOROPS
    releaseSectorStream();
#endif

    Debug_printv("DRIVE[#%d] UNMOUNT\r\n", m_devnr);

    selectDos();

    // if (m_cwd != nullptr)
    // {
    //   //m_cwd->unmount();
    //   device_active = false;
    // }
}


// Persist this drive's settings to mlConfig (devices.iec.<id>).
// Only writes the keys the drive owns; other keys in the entry
// (mode, media_stack, ...) are preserved.
void iecDrive::persistConfig()
{
    // json_object_at, not operator[]: indexing through a stale non-object
    // node anywhere along this path is type_error.305, an abort().
    auto &iec = json_object_at(json_object_at(mlConfig.data(), "devices"), "iec");
    auto &entry = json_object_at(iec, std::to_string(m_devnr).c_str());
    entry["enabled"] = isActive() ? 1 : 0;
    if (!entry.contains("type"))
        entry["type"] = "drive";  // don't clobber subclass types (e.g. "meatloaf")
    entry["url"] = m_cwd ? m_cwd->url : "/";
}


// Apply this drive's settings from mlConfig (devices.iec.<id>).
// Missing entry or keys leave the current state unchanged.
// Returns true if a network-scheme URL restore was deferred (WiFi not connected yet).
bool iecDrive::reloadConfig()
{
    const psram_json &devices = mlConfig["devices"];
    if (!devices.contains("iec"))
        return false;

    const psram_json &iec = devices["iec"];
    std::string key = std::to_string(m_devnr);
    if (!iec.contains(key))
        return false;

    // json_int/json_str, not value(): a per-drive entry that is not an
    // object, or an `enabled`/`url` of the wrong type, is an abort() under
    // -fno-exceptions -- and this runs at boot from main_setup().
    const psram_json &entry = iec[key];
    bool wasActive = isActive();
    setActive(json_int(entry, "enabled", 1) != 0);
    if (isActive() != wasActive)
        notify_activity(activitySource(), isActive() ? "active" : "disabled");

    std::string url = json_str(entry, "url");
    if (!url.empty() && (m_cwd == nullptr || m_cwd->url != url))
    {
        // Network-scheme URLs (fsp://, http://, ...) need a live route. Attempting
        // one before WiFi has an IP fails immediately (no DNS/route yet) and just
        // wastes heap churn at boot. Skip for now; main_setup() retries this same
        // reloadConfig() once WiFi actually connects.
        if (mstr::contains(url, "://") && !fnWiFi.connected())
            return true;
        set_cwd(url);
    }
    return false;
}

// Cheap, RESET-safe counterpart to reloadConfig(): only restores the
// persisted enabled flag, never touches m_cwd/set_cwd() so it can't do
// network/MFile work. See drive.h for why that matters when called from
// the real-time IEC bus task's RESET handling.
void iecDrive::restoreActiveFromConfig()
{
    const psram_json &devices = mlConfig["devices"];
    if (!devices.contains("iec"))
        return;

    const psram_json &iec = devices["iec"];
    std::string key = std::to_string(m_devnr);
    if (!iec.contains(key))
        return;

    bool wasActive = isActive();
    // Same guard as reloadConfig(), and it matters more here: this one is
    // reached from the real-time IEC bus task's RESET handling.
    setActive(json_int(iec[key], "enabled", 1) != 0);
    if (isActive() != wasActive)
        notify_activity(activitySource(), isActive() ? "active" : "disabled");
}


#ifdef IEC_SUPPORT_SECTOROPS

// The container the working directory lives in, opened once and kept.
//
// VDrive is not always the thing holding the disk: a media image reached
// through the MFile chain (mounted over the network, nested inside an archive,
// a format VDrive does not know) has no VDrive behind it at all, and a fast
// loader asking for a block would otherwise get nothing. The decoded stream
// answers the same questions -- seekSector(), getSectorCount(), getTrackCount()
// are all on MStream -- so it serves as the fallback.
//
// Three things about this are deliberate:
//
//   - `m_cwd->url` is used, not fullUrl(). For anything inside a container the
//     url IS the container's path, which is exactly what is wanted here; the
//     position within it belongs to pathInStream and must not come along, or
//     getSourceStream() would select a FILE instead of handing back the whole
//     medium.
//   - The stream is CACHED. A loader reads hundreds of blocks, and building a
//     fresh MFile and stream per sector would re-resolve the whole path each
//     time -- over a network mount that is a round trip per block.
//   - It is opened in|out, never plain out, which would truncate the image.
//
// Compiled only where a software loader can reach it. Epyx's sector ops are on
// every board, but they only ever ran against VDrive, and the tightest ESP32
// has no flash left for a second path they do not need.
#ifdef IEC_IMPL_SOFTLOAD
std::shared_ptr<MStream> iecDrive::sectorStream()
{
    if ( m_cwd == nullptr )
        return nullptr;

    if ( m_sectorStream != nullptr && m_sectorStreamUrl == m_cwd->url )
        return m_sectorStream;

    m_sectorStream = nullptr;
    m_sectorStreamUrl.clear();

    std::unique_ptr<MFile> f( MFSOwner::File( m_cwd->url ) );
    if ( f == nullptr )
        return nullptr;

    auto s = f->getSourceStream( std::ios_base::in | std::ios_base::out );
    if ( s == nullptr || !s->isOpen() )
        return nullptr;

    m_sectorStream = s;
    m_sectorStreamUrl = m_cwd->url;
    return m_sectorStream;
}


// Called wherever the medium behind this drive can change -- set_cwd(),
// mount(), unmount(). Dropping the cached block stream and bumping the
// generation counter are the same event, so they live together: a loader
// waiting for a disk swap is waiting for exactly this.
void iecDrive::releaseSectorStream()
{
    m_sectorStream = nullptr;
    m_sectorStreamUrl.clear();
    m_mediaGeneration++;
}
#endif


bool iecDrive::epyxReadSector(uint8_t track, uint8_t sector, uint8_t *buffer)
{
    if ( m_vdrive != nullptr )
        return m_vdrive->readSector(track, sector, buffer);

#ifndef IEC_IMPL_SOFTLOAD
    return false;
#else
    auto s = sectorStream();
    if ( s == nullptr || !s->seekSector(track, sector, 0) )
        return false;

    // read() hands back at most one block, so a short return is normal and
    // the loop is what makes 256 bytes arrive
    uint32_t got = 0;
    while ( got < 256 )
    {
        uint32_t n = s->read(buffer + got, 256 - got);
        if ( n == 0 ) break;
        got += n;
    }

    return got == 256;
#endif
}


bool iecDrive::epyxWriteSector(uint8_t track, uint8_t sector, uint8_t *buffer)
{
    if ( m_vdrive != nullptr )
        return m_vdrive->writeSector(track, sector, buffer);

#ifndef IEC_IMPL_SOFTLOAD
    return false;
#else
    // No writability test beyond the attempt: isWritable is a property of
    // MFile, not of the stream, and a read-only medium answers with a short
    // write anyway.
    auto s = sectorStream();
    if ( s == nullptr || !s->seekSector(track, sector, 0) )
        return false;

    return s->write(buffer, 256) == 256;
#endif
}


#ifdef IEC_IMPL_SOFTLOAD
uint8_t iecDrive::sectorsPerTrack(uint8_t track)
{
    if ( m_vdrive != nullptr )
        return m_vdrive->sectorsPerTrack(track);

    auto s = sectorStream();
    if ( s == nullptr )
        return 0;

    uint16_t n = s->getSectorCount(track);
    return (n > 0 && n < 256) ? (uint8_t) n : 0;
}


uint8_t iecDrive::imageType()
{
    if ( m_vdrive != nullptr )
        return m_vdrive->imageFormat();

    auto s = sectorStream();
    if ( s == nullptr )
        return IEC_IMG_NONE;

    // There is no image-type field to read, so it comes from the geometry.
    // Only the three shapes a loader actually branches on are reported; a
    // medium that is not one of them answers IEC_IMG_NONE, which is what makes
    // a loader like Ultraboot decline it rather than address it wrongly.
    uint16_t tracks  = s->getTrackCount();
    uint16_t sectors = s->getSectorCount(1);

    if ( tracks >= 35 && tracks <= 42 && sectors == 21 ) return IEC_IMG_1541;
    if ( tracks >= 70 && tracks <= 84 && sectors == 21 ) return IEC_IMG_1571;
    if ( tracks == 80 && sectors == 40 )                 return IEC_IMG_1581;

    return IEC_IMG_NONE;
}
#endif
#endif


#ifdef IEC_SUPPORT_SOFTLOAD
bool iecDrive::startFastLoader(uint8_t variant, uint8_t param, uint8_t rxtx, const uint8_t *cmd, uint8_t cmdLen, uint16_t crc)
{
    uint16_t address = cmdLen>=5 ? (cmd[3] | (cmd[4]<<8)) : 0;

    // Every M-E that reaches the software fast loader dispatch lands here,
    // including the ones that matched nothing -- that is the case worth
    // reporting, since its CRC is what identifies a loader we do not know yet.
    if( variant == IEC_FLV_NONE )
        Debug_printv("M-E address[%04X] crc[%04X] - no known fast loader", address, crc);
    else
        Debug_printv("M-E address[%04X] crc[%04X] - %s", address, crc, iecFastLoadName(variant));

    bool started = SystemFileDevice::startFastLoader(variant, param, rxtx, cmd, cmdLen, crc);
    if( variant != IEC_FLV_NONE && !started )
        Debug_printv("%s is detected but not implemented - falling back", iecFastLoadName(variant));

    return started;
}
#endif



#endif /* BUILD_IEC */
