
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

#if defined(BUILD_IEC) || defined(BUILD_GPIB)

#ifdef BUILD_GPIB
#define IECFILEDEVICE_STATUS_BUFFER_SIZE GPIBFILEDEVICE_STATUS_BUFFER_SIZE
#endif

#include "drive.h"

#include <cstring>
#include <sstream>
#include <unordered_map>

#include "make_unique.h"

#include "meatloaf.h"
#include "meat_session.h"
#include "fuji.h"
#include "fnFsSD.h"
#include "led.h"
#include "utils.h"
#include "display.h"
#include "time_converter.h"

#include "meat_media.h"
#include "qrmanager.h"

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"
#include "../../../include/version.h"
#include "../../../include/cbm_defines.h"

// Buffering data when reading/writing streams because during regular (non-fastloader)
// transmissions, the read/write functions are called for each single byte at a time and
// reading/writing MStream one byte at a time can be time consuming.
// To be safe, BUFFER_SIZE should always be >=256
#define BUFFER_SIZE 512


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


static bool isMatch(std::string name, std::string pattern)
{
    signed char found = -1;
    
    for(uint8_t i=0; found<0; i++)
    {
        if( pattern[i]=='*' )
            found = 1;
        else if( pattern[i]==0 || name[i]==0 )
            found = (pattern[i]==name[i]) ? 1 : 0;
        else if( pattern[i]!='?' && pattern[i]!=name[i] && !(name[i]=='~' && (pattern[i] & 0xFF)==0xFF) )
            found = 0;
    }

    return found==1;
}


// -------------------------------------------------------------------------------------------------


iecChannelHandler::iecChannelHandler(iecDrive *drive)
{ 
    m_drive = drive;
    m_data = new uint8_t[BUFFER_SIZE];
    m_len = 0;
    m_ptr = 0;
}


iecChannelHandler::~iecChannelHandler()
{ 
    delete [] m_data; 
}


uint8_t iecChannelHandler::read(uint8_t *data, uint8_t n)
{
    // if buffer is empty then re-fill it
    if( m_ptr >= m_len )
    {
        m_ptr = 0;
        m_len = 0;

        uint8_t st = readBufferData();
        if( st!=ST_OK )
        {
            m_drive->setStatusCode(st);
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
            return 1;
        }
        else
        {
            // copy as much data as possible
            n = std::min((size_t) n, (size_t) (m_len - m_ptr));
            memcpy(data, m_data + m_ptr, n);
            m_ptr += n;
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

    double cps = m_byteCount / seconds;
    Debug_printv("%s %lu bytes in %0.2f seconds @ %0.2f B/s", m_stream->mode == std::ios_base::in ? "Sent" : "Received", m_byteCount, seconds, cps);

    double tseconds = m_transportTimeUS / 1000000.0;
    cps = m_byteCount / (seconds-tseconds);
    Debug_printv("Transport (network/sd) took %0.3f seconds, pure IEC transfers @ %0.2f B/s", tseconds, cps);

#ifdef ENABLE_DISPLAY
    LEDS.idle();
    //Debug_printv("Stop Activity");
#endif

  //delete m_stream;
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
        Debug_printv("size[%lu] avail[%lu] pos[%lu]", m_stream->size(), m_stream->available(), m_stream->position());
        if (m_stream->size() == 0)
            return ST_FILE_NOT_FOUND;

#ifdef ENABLE_DISPLAY
        // send progress percentage
        uint8_t percent = (m_stream->position() * 100) / m_stream->size();
        LEDS.progress = percent;
#endif
        fnLedManager.toggle(eLed::LED_BUS);

        if( m_fixLoadAddress>=0 && m_stream->position()==0 )
        {
            uint64_t t = esp_timer_get_time();
            m_len = m_stream->read(m_data, BUFFER_SIZE);
            m_transportTimeUS += (esp_timer_get_time()-t);
            if( m_len>=2 )
                {
                m_data[0] = (m_fixLoadAddress & 0x00FF);
                m_data[1] = (m_fixLoadAddress & 0xFF00) >> 8;
                }
            m_fixLoadAddress = -1;
        }
        else
            m_len = 0;

        // try to fill buffer
        while( m_len<BUFFER_SIZE && !m_stream->eos() )
        {
            uint64_t t = esp_timer_get_time();
            m_len += m_stream->read(m_data+m_len, BUFFER_SIZE-m_len);
            m_transportTimeUS += (esp_timer_get_time()-t);
            if (m_stream->error())
                    return ST_DRIVE_NOT_READY;
        }

        m_byteCount += m_len;
    }

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
    if (image.size() > 0) {
        mstr::replaceAll(path, image, "");
        mstr::drop(path, 1);
    }
    if (archive.size() > 0) {
        mstr::replaceAll(path, archive, "");
        mstr::drop(path, 1);
    }

    scheme = mstr::toPETSCII2(scheme);
    url = mstr::toPETSCII2(url);
    path = mstr::toPETSCII2(path);
    archive = mstr::toPETSCII2(archive);
    image = mstr::toPETSCII2(image);
    
    m_headers.clear();
    if( url.size()>0 )     addExtraInfo(scheme, url);
    if( path.size()>1 )    addExtraInfo("PATH", path);
    if( archive.size()>1 ) addExtraInfo("ARCHIVE", archive);
    if( image.size()>0 )   addExtraInfo("IMAGE", image);
    if( m_headers.size()>0 ) m_headers.push_back("NFO ----------------");

    // If SD Card is available and we are at the root path show it as a directory at the top
    if(m_dir->url.size() < 2 ) {
        if( fnSDFAT.running() )
            m_headers.push_back("DIR SD");
    
        // This will be used to browse the network
        // m_headers.push_back("DIR NETWORK");
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
    m_headers.push_back("NFO ["+title+"]");
    while( text.size()>0 )
    {
        std::string s = text.substr(0, 16);
        m_headers.push_back("NFO "+s);
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
        std::string name = m_dir->media_header.size() ? m_dir->media_header : PRODUCT_ID;
        size_t n = std::min(16, (int) name.size());
        memcpy(m_data+8, name.data(), n);
        while( n<16 ) { m_data[8+n] = ' '; n++; }
        m_data[24] = '"';
        m_data[25] = ' ';
        if ( m_dir->media_id.size() )
        {
            mstr::replaceAll(m_dir->media_id, "{{id}}", mstr::format("%02i", m_drive->id()));
            // Make sure media_id is at most 5 chars to fit in the browser line
            if (m_dir->media_id.size() > 5) {
                m_dir->media_id = m_dir->media_id.substr(0, 5);
            } else if ( m_dir->media_id.size() < 5 ) {
                // pad with spaces if media_id is less than 5 chars to keep the browser line aligned
                m_dir->media_id += std::string(5 - m_dir->media_id.size(), ' ');
            }
            memcpy(m_data+26, m_dir->media_id.c_str(), 5); // Use ID and DOS version from media file
        }
        else
        {
            sprintf((char *) m_data+26, "%02i 2A", m_drive->id()); // Use drive # as ID in browser mode
        }
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

        // skip over files starting with "."
        do 
        { 
            entry = std::unique_ptr<MFile>( m_dir->getNextFileInDir() ); 
            if( entry!=nullptr )
            {
                Debug_printv("[%s][%s]", entry->name.c_str(), entry->pathInStream.c_str());
            }
        }
        while( entry!=nullptr && entry->name[0] == '.' );

        if( entry != nullptr )
        {
            //Debug_printv( ANSI_WHITE_BOLD "blocks[%lu] name[%s] ext[%s]", entry->blocks(), entry->name.c_str(), entry->extension.c_str());

            // directory entry
            uint16_t blocks = entry->blocks();
            m_data[m_len++] = 1;  // BASIC line pointer low byte
            m_data[m_len++] = 1;  // BASIC line pointer low byte
            m_data[m_len++] = blocks&255;  // Block count low byte
            m_data[m_len++] = blocks/256;  // Block count high byte
            if( blocks<10 )    m_data[m_len++] = ' ';
            if( blocks<100 )   m_data[m_len++] = ' ';
            if( blocks<1000 )  m_data[m_len++] = ' ';

            // Extension
            std::string ext = entry->extension;
            mstr::ltrim(ext);
            if( entry->isDirectory() )
            {
                ext = "dir";
            }
            else if( ext.length()>0 )  // Enhanced directory entries with real file extension
            {
                if( ext.size()>3 )
                    ext = ext.substr(0, 3);
                else
                    ext += std::string(3-ext.size(), ' ');
            }
            else
            {
                ext = "prg";
            }

            // File name
            std::string name = entry->name;
            if ( !mstr::contains(entry->pathInStream, name.c_str()) && entry->pathInStream.size() )
                name = entry->pathInStream;

            if ( !m_dir->isPETSCII )
            {
                name = U8Char::encodeACE(name);
                name = mstr::toPETSCII2( name );
                ext  = U8Char::encodeACE(ext);
                ext  = mstr::toPETSCII2( ext );
            }
            mstr::replaceAll(name, "\\", "/");
            m_data[m_len++] = '"';

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
                  n = 17-n;
                  while(n-->0) m_data[m_len++] = ' ';
                }
              else
                m_data[m_len++] = ' ';

              // Extension
              memcpy(m_data+m_len, ext.data(), 3);
              m_len+=3;
              m_data[m_len++] = ' ';
              m_data[m_len++] = 0;
            }
#endif
        }
        else
        {
            // no more entries => footer
#ifdef IEC_FP_DOLPHIN
            // DolphinDos' MultiDubTwo copy program needs the "BLOCKS FREE" footer line, otherwise it aborts when reading source
            uint32_t free = m_dir->media_image.size() ? m_dir->media_blocks_free : std::min((int) m_dir->getAvailableSpace()/254, 65535);
            m_data[0] = 1;
            m_data[1] = 1;
            m_data[2] = free&255;
            m_data[3] = free/256;
            strcpy((char *) m_data+4, "BLOCKS FREE.");
#else
            uint32_t free = m_dir->media_image.size() ? m_dir->media_blocks_free : 0;
            m_data[0] = 1;
            m_data[1] = 1;
            m_data[2] = free&255;
            m_data[3] = free/256;
            if( m_dir->media_image.size() )
                strcpy((char *) m_data+4, "BLOCKS FREE.");
            else
                sprintf((char *) m_data+4, CBM_DELETE CBM_DELETE "%sBYTES FREE.", mstr::formatBytes(m_dir->getAvailableSpace()).c_str());
#endif
            int n = 4+strlen((char *) m_data+4);
            while( n<29 ) m_data[n++]=' ';
            m_data[29] = 0;
            m_data[30] = 0;
            m_data[31] = 0;
            m_len = 32;
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
    m_statusCode = ST_DOSVERSION;
    m_statusTrk  = 0;
    m_numOpenChannels = 0;
    m_cwd.reset( MFSOwner::File("/", true) );

    m_memory.setROM("dos1541"); // Default to 1541 ROM

    m_vdrive = NULL;

    for(int i=0; i<16; i++) 
        m_channels[i] = nullptr;
}

bool iecDrive::open(uint8_t channel, const char *cname, uint8_t nameLen)
{
    Debug_printv("iecDrive::open(#%d, %d, \"%s\")", m_devnr, channel, cname);

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
        // TODO: Use CBM DOS command parser here
        // determine file name
        std::string name;
        std::vector<std::string> pt = util_tokenize(std::string(cname), ',');
        if( pt.size()>0 ) name = pt[0];

        // determine file mode (read/write)
        bool overwrite = false;
        std::ios_base::openmode mode = std::ios_base::in;
        if( channel==1 ) mode = std::ios_base::out;
        if( pt.size() >= 3 )
        {
            if( pt[2]=="R" )
                mode = std::ios_base::in;
            else if( pt[2]=="W" )
                mode = std::ios_base::out;
        }

        if( mstr::startsWith(name, "@") )
        {
            overwrite = true;
            name = mstr::drop(name, 1);
        }

        if( mstr::startsWith(name, ":") )
        {
            overwrite = true;
            name = mstr::drop(name, 1);
        }

        if( mstr::startsWith(name, "0:") )
            name = mstr::drop(name, 2);

        if( mstr::startsWith(name, "CD") )
        {
            name = mstr::drop(name, 2);
        }

        // file name officially ends at first "shifted space" (0xA0) character
        size_t i = name.find('\xa0');
        if( i != std::string::npos ) name = name.substr(0, i);

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

            // Handle CMD-style directory filters by preserving them in URL
            if ( name[0] == '$' ) {
                // Check if this is a CMD-style filter (e.g., $=P, $GAME*, $=P:GAME*)
                if (name.length() > 1 && (name[1] == '=' || isalnum(name[1]))) {
                    // This looks like a CMD filter - preserve it for the server
                    Debug_printv("CMD filter detected: [%s]", name.c_str());
                    // Don't clear the name - let the server handle CMD filtering
                } else {
                    // Plain "$" - traditional directory listing
                    name.clear();
                }
            }

            // get file
            Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Changing directory to [%s][%s] hex[%s]", m_cwd->url.c_str(), name.c_str(), mstr::toHex(name).c_str());
            name = mstr::toUTF8(name);
            name = U8Char::decodeACE(name);
            MFile *f = m_cwd->cd(name);
            bool is_dir = f->isDirectory();
            Debug_printv("isdir[%d] url[%s][%s]", is_dir, f->url.c_str(), f->pathInStream.c_str());

            if (f == nullptr)  // || f->url.empty() )
            {
                Debug_printv("Error: could not find file system for URL [%s]", name.c_str());
                setStatusCode(ST_FILE_NOT_FOUND);
                if (f != nullptr) delete f;
                f = nullptr;
            }
            else if (is_dir)
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
                            m_numOpenChannels++;
                            Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Change Directory Here! channel[%d] numChannels[%d] dir[%s]", channel, m_numOpenChannels, f->url.c_str());
                            m_cwd.reset(MFSOwner::File(f->url));
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
                else if( (mode == std::ios_base::out) && f->media_image.size()>0 )
                {
                    Debug_printv("Error: writing to files on disk media not supported [%s]", f->url.c_str());
                    setStatusCode(ST_WRITE_PROTECT_ON);
                }
                else if( (mode == std::ios_base::out) && f->exists() && !overwrite )
                {
                    Debug_printv("Error: file exists [%s]", f->url.c_str());
                    setStatusCode(ST_FILE_EXISTS);
                }
                else
                {
                    if( Meatloaf.use_vdrive && !is_dir && (m_vdrive=VDrive::create(m_devnr-8, f->url.c_str()))!=nullptr )
                    {
                        Debug_printv("Created VDrive for URL %s. Loading directory.", f->url.c_str());
                        delete f;
                        return m_vdrive->openFile(channel, "$");
                    }

                    std::shared_ptr<MStream> new_stream = f->getSourceStream(mode);
                    
                    if( new_stream==nullptr )
                    {
                        Debug_printv("Error: could not get stream for file [%s]", f->url.c_str());
                        setStatusCode(ST_DRIVE_NOT_READY);
                    }
                    else if( (mode == std::ios_base::in) && new_stream->size()==0 && !is_dir )
                    {
                        Debug_printv("Error: file length is zero [%s]", f->url.c_str());
                        //delete new_stream;
                        setStatusCode(ST_FILE_NOT_FOUND);
                    }
                    else if( !new_stream->isOpen() )
                    {
                        Debug_printv("Error: could not open file stream [%s]", f->url.c_str());
                        //delete new_stream;
                        setStatusCode(ST_DRIVE_NOT_READY);
                    }
                    else
                    {
                        Debug_printv("Stream created for file [%s] pathInStream[%s]", f->url.c_str(), f->pathInStream.c_str());
                        // new_stream will be deleted in iecChannelHandlerFile destructor
                        m_channels[channel] = new iecChannelHandlerFile(this, new_stream, is_dir ? 0x0801 : -1); // 0x0801 = overrides load address to start of basic for C64
                        m_numOpenChannels++;
                        setStatusCode(ST_OK);

                        Debug_printv("isDir[%d] isRandomAccess[%d] isBrowsable[%d]", is_dir, new_stream->isRandomAccess(), new_stream->isBrowsable());
                        if ( new_stream->isRandomAccess() || new_stream->isBrowsable() )
                        {
                            // This was a directory.  Set m_cwd to the directory
                            Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "dir url[%s]", f->url.c_str() );
                            m_cwd.reset(MFSOwner::File(f->url));
                        }
                        else
                        {
                            // This was a file.  Set m_cwd to the parent directory
                            Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "file base[%s]", f->base().c_str() );
                            m_cwd.reset(MFSOwner::File(f->base()));
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
                        Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Change Directory Here! url[%s] > base[%s]", f->url.c_str(), f->base().c_str() );
                        m_cwd.reset(MFSOwner::File(f->base()));
                    }
                }

                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "m_cwd[%s]", m_cwd==nullptr ? "NULL" : m_cwd->url.c_str());
                m_cwd->dump();
            }
            
            delete f;
        }

        return m_channels[channel]!=NULL;
    }
}


void iecDrive::close(uint8_t channel)
{
    //Debug_printv("iecDrive::close(#%d, %d)", m_devnr, channel);

  // 1541 drive clears status when closing a channel
  IECFileDevice::clearStatus();
  setStatusCode(ST_OK);

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
        ImageBroker::dump();
        Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "id[%d] cwd[%s]", m_devnr, m_cwd==nullptr ? "NULL" : m_cwd->url.c_str());
        Debug_memory();
        m_cwd->dump();
    }

#ifdef ENABLE_DISPLAY
    LEDS.activity = false;
#endif
}


uint8_t iecDrive::write(uint8_t channel, uint8_t *data, uint8_t dataLen, bool eoi)
{
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
            if( m_statusCode==ST_OK ) setStatusCode(ST_FILE_NOT_OPEN);
            return 0;
        }
        else
        {
            uint8_t bytes_read = handler->read(data, maxDataLen);
            if( m_statusCode==ST_FILE_NOT_FOUND)
            {
                Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Subdir Change Directory Here! stream[%s] > base[%s]", m_cwd->url.c_str(), m_cwd->base().c_str());
                m_cwd.reset( MFSOwner::File(m_cwd->base()) );
            }

            return bytes_read;
        }
        
    }
}

// we override the "executeData" because some commands may include NUL or CR characters which 
// may not work in the text-based "execute" function. Note that while commands handled here
// are all text-based, M-R and M-W commands passed on to the VDrive may contain binary data.
void iecDrive::executeData(const uint8_t *data, uint8_t dataLen)
{
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
            setStatusCode(ST_VDRIVE);
          
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

    if( mstr::startsWith(command, "CD") )
    {
        set_cwd(mstr::drop(command, 2));
        return;
    }
#ifdef IEC_FP_JIFFY
    else if( command=="EJ+" || command=="EJ-" )
    {
        enableFastLoader(IEC_FP_JIFFY, command[2]=='+');
        return;
    }
#endif  
#ifdef IEC_FP_EPYX
    else if( command=="EE+" || command=="EE-" )
    {
        enableFastLoader(IEC_FP_EPYX, command[2]=='+');
        return;
    }
#endif  
#ifdef IEC_FP_AR6
    else if( command=="EA+" || command=="EA-" )
    {
        enableFastLoader(IEC_FP_AR6, command[2]=='+');
        return;
    }
#endif
#ifdef IEC_FP_FC3
    else if( command=="EF+" || command=="EF-" )
    {
        enableFastLoader(IEC_FP_FC3, command[2]=='+');
        return;
    }
#endif
#ifdef IEC_FP_DOLPHIN
    else if( command=="ED+" || command=="ED-" )
    {
        enableFastLoader(IEC_FP_DOLPHIN, command[2]=='+');
        return;
    }
    else if( command == "M-R\xfa\x02\x03" )
    {
        // hack: DolphinDos' MultiDubTwo copy program reads 02FA-02FC to determine
        // number of free blocks => pretend we have 664 (0298h) blocks available
        m_statusCode = ST_OK;
        uint8_t data[3] = {0x98, 0, 0x02};
        setStatus((char *) data, 3);
        return;
    }
#endif
    // else
    // {
    //   setStatusCode(ST_SYNTAX_INVALID);
    //   //Debug_printv("Invalid command");
    // }

    // Drive level commands
    // CBM DOS 2.6
    uint8_t media = 0; 
    // N:, NEW:, N0:, NEW0:, S:, S0, I:, I0:, etc
    size_t colon_position = command.find_first_of(':');
    if (colon_position == std::string::npos)
        colon_position = 0;

    if (colon_position > 1)
    {
        media = atoi((char *) &command[colon_position - 1]);
    }
    Debug_printv("media[%d] colon_position[%d] command[%s]", media, colon_position, command.c_str());

    switch ( command[0] )
    {
        case 'B':
            if (command[1] == '-')
            {
                // B-P buffer pointer
                if (command[2] == 'P')
                {
                    command = mstr::drop(command, 3);
                    mstr::trim(command);
                    mstr::replaceAll(command, "  ", " ");
                    std::vector<uint8_t> pti = util_tokenize_uint8(command);
                    Debug_printv("command[%s] channel[%d] position[%d]", command.c_str(), pti[0], pti[1]);

                    auto channel = m_channels[pti[0]];
                    if ( channel != nullptr )
                    {
                        auto stream = channel->getStream();
                        stream->position( pti[1] );
                        //setStatusCode(ST_OK);
                    }
                    return;
                }
                // B-R read block
                else if (command[2] == 'R')
                {
                    Debug_printv( "read block");
                    command = mstr::drop(command, 3);
                    mstr::trim(command);
                    mstr::replaceAll(command, "  ", " ");
                    std::vector<uint8_t> pti = util_tokenize_uint8(command);
                    Debug_printv("command[%s] channel[%d] media[%d] track[%d] sector[%d]", command.c_str(), pti[0], pti[1], pti[2], pti[3]);

                    auto channel = m_channels[pti[0]];
                    if ( channel != nullptr )
                    {
                        auto stream = channel->getStream();
                        stream->seekSector( pti[2], pti[3] );
                        uint8_t size;
                        stream->read(&size, 1);
                        uint8_t *data = (uint8_t *) malloc(size);
                        //memset(data, 0, size);
                        stream->read(data, size);
                        setStatus((char *) data, size);
                        free(data);
                        return;
                    }
                }
                // B-W write block
                else if (command[2] == 'W')
                {
                    Debug_printv( "write block");
                }
                // B-A allocate block in BAM
                else if (command[2] == 'A')
                {
                    Debug_printv( "allocate bit in BAM");
                }
                // B-F free block in BAM
                else if (command[2] == 'F')
                {
                    Debug_printv( "free bit in BAM");
                }
                // B-E block execute
                else if (command[2] == 'E')
                {
                    Debug_printv( "block execute");
                }
            }
            //Error(ERROR_31_SYNTAX_ERROR);
            Debug_printv( "block/buffer");
        break;
        case 'C':
            if ( command[1] != 'D' && colon_position)
            {
                //Copy(); // Copy File
                Debug_printv( "copy file");
            }
        break;
        case 'D':
            Debug_printv( "duplicate disk");
            //Error(ERROR_31_SYNTAX_ERROR);	// DI, DR, DW not implemented yet
        break;
        case 'I':
            // Initialize
            Debug_printv( "initialize");
            reset();
            return;
        break;
        case 'M':
            if ( command[1] == '-' ) // Memory
            {
                if (command[2] == 'R') // M-R memory read
                {
                    command = mstr::drop(command, 3); // Drop M-R
                    std::string code = mstr::toHex(command);
                    uint16_t address = (command[0] | command[1] << 8);
                    uint8_t size = command[2];
                    Debug_printv("Memory Read [%s]", code.c_str());
                    Debug_printv("address[%04X] size[%d]", address, size);

                    uint8_t data[size] = { 0 };
                    m_memory.read(address, data, size);
                    setStatus((char *) data, size);
                    Debug_printv("address[%04X] data[%s]", address, mstr::toHex(data, size).c_str());
                }
                else if (command[2] == 'W') // M-W memory write
                {
                    command = mstr::drop(command, 3); // Drop M-W
                    uint16_t address = (command[0] | command[1] << 8);
                    size_t size = command[2]; // Limited to 34 data bytes per command

                    command = mstr::drop(command, 3); // Drop address, size

                    //std::string code = mstr::toHex((const uint8_t *)command.c_str(), size);
                    //Debug_printv("Memory Write [%04X][%d]:[%s]", address, size, code.c_str());

                    m_memory.write(address, (const uint8_t *)command.c_str(), size);
                }
                else if (command[2] == 'E') // M-E memory execute
                {
                    command = mstr::drop(command, 3); // Drop M-E
                    std::string code = mstr::toHex(command);
                    uint16_t address = (command[0] | command[1] << 8);
                    Debug_printv("Memory Execute address[%04X][%s]", address, code.c_str());

                    // Compare m_mw_hash to known software fastload hashes
                    Debug_printv("Final M-W Hash [%02X]", m_memory.mw_hash);

                    m_memory.execute(address);

                    // Execute detected fast loader
                }
                //setStatusCode(ST_OK);
                return;
            }
        break;
        case 'N':
        {
//#ifdef USE_VDRIVE
            if ( Meatloaf.use_vdrive )
                {
                string params = command.substr(colon_position+1);
                size_t dot = params.find('.');

                if( dot==string::npos )
                {
                    // must have extension to determine image type
                    setStatusCode(ST_SYNTAX_INVALID);
                }
                else
                {
                    string filename, diskname, ext;
                    size_t comma = params.find(',');
                    if( comma==string::npos )
                    {
                        filename = mstr::toUTF8(params);
                        diskname = params.substr(0,dot);
                    }
                    else
                    {
                        filename = mstr::toUTF8(params.substr(0, comma));
                        diskname = params.substr(comma+1);
                    }

                    fnLedManager.set(eLed::LED_BUS, true);
                    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Changing directory to [%s][%s]", m_cwd->url.c_str(), filename.c_str());
                    MFile *f = m_cwd->cd(filename);
                    if( f->exists() )
                        setStatusCode(ST_FILE_EXISTS);
                    else if( !f->isWritable )
                        setStatusCode(ST_WRITE_PROTECT_ON);
                    else if( !VDrive::createDiskImage(f->url.c_str(), NULL, diskname.c_str(), false) )
                        setStatusCode(ST_WRITE_VERIFY);
                    else
                        setStatusCode(ST_OK);
                    fnLedManager.set(eLed::LED_BUS, false);
                }
            }
            else
            {
//#else
                //New();
                Debug_printv( "new (format)");
                command = mstr::toUTF8(command.substr(colon_position + 1));
                if (!m_cwd->format(command))
                    setStatusCode(ST_WRITE_VERIFY);

                return;
            }
//#endif
        }
        break;
        case 'R':
            if ( command[1] != 'D' && colon_position ) // Rename
            {
                Debug_printv( "rename file");
                // Rename();
                command = command.substr(colon_position + 1);
                command = mstr::toUTF8(command);
                auto parts = mstr::split(command, '=');

                if( parts.size() != 2 )
                {
                setStatusCode(ST_SYNTAX_INVALID);
                return;
                }

                uint8_t n = 0;
                MFile *dir = MFSOwner::File(m_cwd->url);
                if( dir!=nullptr )
                {
                    if( dir->isDirectory() )
                        {
                        std::unique_ptr<MFile> entry;
                        while( (entry=std::unique_ptr<MFile>(m_cwd->getNextFileInDir()))!=nullptr )
                            {
                            if( isMatch(entry->name, parts[1]) )
                                {
                                entry->rename(parts[0]);
                                Debug_printv("Renamed '%s' to '%s'", parts[1].c_str(), parts[0].c_str());
                                n++;
                                break;
                                }
                            }
                        }
                    delete dir;
                }

                if ( n == 0 )
                    setStatusCode(ST_FILE_NOT_FOUND);

                return;
            }
        break;
        case 'S': // Scratch
            if ( colon_position )
            {
                Debug_printv( "scratch");
                //Scratch();
                // SCRATCH (delete)
                uint8_t n = 0;
                command = command.substr(colon_position + 1);

                MFile *dir = MFSOwner::File(m_cwd->url);
                if( dir!=nullptr )
                {
                    if( dir->isDirectory() )
                        {
                        std::unique_ptr<MFile> entry;
                        while( (entry=std::unique_ptr<MFile>(m_cwd->getNextFileInDir()))!=nullptr )
                            {
                            if( !entry->isDirectory() && isMatch(mstr::toPETSCII2(entry->name), command) )
                                {
                                Debug_printv("DELETING %s", entry->name.c_str());
                                if( entry->remove() ) n++;
                                }
                            //else Debug_printv("NOT DELETING %s", entry->name.c_str());
                            }
                        }

                    delete dir;
                }

                setStatusCode(ST_SCRATCHED, n);
                return;
            }
        break;
        case 'U':
            if (command[1] == '1' || command[1] == '2' || command[1] == 'A' || command[1] == 'B') // U1/UA, U2/UB
            {
                // Block Read if 1/A, Write if 2/B
                bool read = (command[1] == '1' || command[1] == 'A');
                command = mstr::drop(command, 3);
                mstr::trim(command);
                mstr::replaceAll(command, "  ", " ");
                std::vector<uint8_t> pti = util_tokenize_uint8(command);
                Debug_printv("command[%s] channel[%d] media[%d] track[%d] sector[%d]", command.c_str(), pti[0], pti[1], pti[2], pti[3]);

                auto channel = m_channels[pti[0]];
                if ( channel != nullptr )
                {
                    auto stream = channel->getStream();
                    stream->seekSector( pti[2], pti[3] );
                    if ( read )
                    {
                        Debug_printv("Block-Read");
                        return;
                    }

                    // Write Block
                    Debug_printv("Block-Write");
                    return;
                }
            }
            else if ((command[1] >= '3' && command[1] <= '9') || (command[1] >= 'C' && command[1] <= 'H')) // U3-U9, UC-UH
            {
                uint8_t offset = command[1] - '3';
                if (offset > 5)
                {
                    //Debug_printv("U%C [%02x] C[%02x]", command[1], command[1], 'C');
                    offset = command[1] - 'C';
                }
                offset = (offset * 3);
                //Debug_printv( "U%c, offset %02x", command[1], offset);
                m_memory.execute(0x0500 + offset);
                return;
            }
            else if ((command[1] == '9' || command[1] == 'I') && command.size() == 2) // U9, UI
            {
                Debug_printv( "warm reset");
                setStatusCode(ST_DOSVERSION);
                m_memory.execute(0xFFFA);
                return;
            }
            else if (command[1] == 'J') // UJ
            {
                Debug_printv( "cold reset");
                reset();
                return;
            }
            else if (command[1] == 0xCA) // U{Shift-J}
            {
                if ( command[2] == '+')
                {
                    Debug_printv( "reboot");
                    fnSystem.reboot();
                    return;
                }
                Debug_printv( "hard reset");
                reset();
                m_cwd.reset(m_cwd->cd("^")); // reset to flash root
                return;
            }
            else if (command[1] == 'I' && command.size() == 3) // UI+/-
            {
                if (command[2] == '-')
                {
                    Debug_printv( "VIC-20 Bus Speed");
                    // Set IEC Data Valid timing to 20us
                }
                else
                {
                    Debug_printv( "C64 Bus Speed");
                    // Set IEC Data Valid timing to 60us
                }
                //setStatusCode(ST_OK);
                return;
            }
        break;
        case 'V':
            Debug_printv( "validate bam");
        break;
        default:
            //Error(ERROR_31_SYNTAX_ERROR);
        break;
    }

    // SD2IEC Commands
    // http://www.n2dvm.com/UIEC.pdf
    switch ( command[0] )
    {
        case 'C':
            if ( command[1] == 'P') // Change Partition
            {
                Debug_printv( "change partition");
                //ChangeDevice();
                return;
            }
            // else if ( command[1] == 'D') // Change Directory
            // {
            //     set_cwd(mstr::drop(command, 2));
            // }
        break;
        case 'E':
            if (command[1] == '-')
            {
                Debug_printv( "eeprom");
            }
        break;
        case 'G':
            Debug_printv( "get partition info");
            //Error(ERROR_31_SYNTAX_ERROR);	// G-P not implemented yet
        break;
        case 'M':
            if ( command[1] == 'D') // Make Directory
            {
                string path = mstr::toUTF8(command.substr(colon_position + 1));
                Debug_printv( "make directory path[%s]", path.c_str());
                MFile *f = m_cwd->cd( path );
                if( f!=nullptr )
                {
                    bool created = false;
                    if( f->sourceFile!=nullptr )
                    {
                        if( f->sourceFile->exists() )
                        {
                            Debug_printv("directory exists");
                            setStatusCode(ST_FILE_EXISTS);
                        }
                        else if( !f->sourceFile->isWritable )
                        {
                            Debug_printv("not writable");
                            setStatusCode(ST_WRITE_PROTECT_ON);
                        }
                        else if( f->format("meatloaf,01") )
                        {
                            Debug_printv("format ok");
                            created = true;
                        }
                    }

                    if ( !created )
                    {
                        if( f->exists() )
                        {
                            Debug_printv("directory exists");
                            setStatusCode(ST_FILE_EXISTS);
                        }
                        else if( !f->isWritable )
                        {
                            Debug_printv("not writable");
                            setStatusCode(ST_WRITE_PROTECT_ON);
                        }
                        else if( f->mkDir() )
                        {
                            Debug_printv("mkdir ok");
                        }
                        else
                        {
                            Debug_printv("make directory failed");
                            setStatusCode(ST_WRITE_VERIFY);
                        }
                    }

                    delete f;
                }
                else
                {
                    Debug_printv("make directory failed");
                    setStatusCode(ST_WRITE_VERIFY);
                }
                return;
            }
        break;
        case 'P':
            {
                command = mstr::drop(command, 1);
                std::vector<uint8_t> pti = util_tokenize_uint8(command);
                Debug_printv("position channel[%d] hi[%d] mid[%d] low[%d]", pti[0], pti[1], pti[2], pti[3]);
                auto channel = m_channels[(uint8_t)pti[0]];
                if ( channel != nullptr )
                {
                    auto stream = channel->getStream();
                    uint32_t pos = (pti[1] * 65536) + (pti[2] * 256) + pti[3];
                    stream->seek( pos );
                }
                else
                {
                    setStatusCode(ST_NO_CHANNEL);
                }
                return;
            }
        break;
        case 'R':
            if ( command[1] == 'D') // Remove Directory
            {
                Debug_printv( "remove directory");
                string path = mstr::toUTF8(command.substr(colon_position + 1));
                MFile *f = m_cwd->cd( path );
                if( f!=nullptr )
                    {
                    if( !f->exists() )
                        setStatusCode(ST_FILE_NOT_FOUND);
                    else if( !f->isWritable )
                        setStatusCode(ST_WRITE_PROTECT_ON);
                    else if( !f->rmDir() )
                        setStatusCode(ST_WRITE_VERIFY);

                    delete f;
                    }
                return;
            }
        break;
        case 'S':
            if (command[1] == '-')
            {
                // Swap drive number 
                Debug_printv( "swap drive number");
                //Error(ERROR_31_SYNTAX_ERROR);
                break;
            }
        break;
        case 'T':
            if (command[1] == '-')
            {
                time_t tt = time(nullptr);
                struct tm *tinfo = localtime(&tt);
                
                uint8_t buf[IECFILEDEVICE_STATUS_BUFFER_SIZE];
                size_t len = 0;

                //Error(ERROR_31_SYNTAX_ERROR);	// T-R and T-W not implemented yet
                if (command[2] == 'R')
                {
                    if(command[3] == 'A')
                    {
                        Debug_printv("read ascii format");
                        len = TimeConverter::toAsciiTime(tinfo, buf, IECFILEDEVICE_STATUS_BUFFER_SIZE);
                    }
                    else if(command[3] == 'I')
                    {
                        Debug_printv("read ISO-8601 format");
                        len = TimeConverter::toIsoTime(tinfo, buf, IECFILEDEVICE_STATUS_BUFFER_SIZE);
                    }
                    else if(command[3] == 'D')
                    {
                        Debug_printv("read decimal format");
                        len = TimeConverter::toDecimalTime(tinfo, buf, IECFILEDEVICE_STATUS_BUFFER_SIZE);
                    }
                    else if(command[3] == 'B')
                    {
                        Debug_printv("read BCD format");
                        len = TimeConverter::toBcdTime(tinfo, buf, IECFILEDEVICE_STATUS_BUFFER_SIZE);
                    }
                    else if(command[3] == 'Z')
                    {
                        std::string tz;
                        char* tz_value = getenv("TZ");
                        if (tz_value != nullptr) {
                            tz = tz_value;
                        } else {
                            tz = "UTC"; // Set a default value
                        }
                        Debug_printv("read TIMEZONE [%s]", tz.c_str());
                        tz = mstr::toPETSCII2(tz);
                        len = tz.size();
                        memcpy(buf, tz.data(), len);
                    }
                    SystemFileDevice::setStatus((const char *) buf, len);
                }
                else if (command[2] == 'W')
                {
                    if(command[3] == 'A')
                    {
                        Debug_printv("write ascii format");
                    }
                    else if(command[3] == 'I')
                    {
                        Debug_printv("write ISO-8601 format");
                    }
                    else if(command[3] == 'D')
                    {
                        Debug_printv("write decimal format");
                    }
                    else if(command[3] == 'B')
                    {
                        Debug_printv("write BCD format");
                    }
                    else if(command[3] == 'Z')
                    {
                        if (colon_position)
                            command = command.substr(colon_position + 1);
                        else
                            command = mstr::drop(command, 4);
                        command = mstr::toUTF8(command);
                        mstr::trim(command);
                        mstr::replaceAll(command, " ", "_");
                        Debug_printv("write TIMEZONE [%s]", command.c_str());

                        setenv("TZ", command.c_str(), 1);
                        tzset(); // Assign the local timezone from setenv
                        
                    }
                }
                return;
            }
        break;
        case 'W':
            // Save out current options?
            //OPEN1, 9, 15, "XW":CLOSE1
            Debug_printv( "user 1a2b");
        break;
        case 'X':
            Debug_printv( "xtended commands");
            // X{0-4}
            // XE+ / XE-
            // XB+ / XB-
            // X{0-7}={0-15}
            // XD?
            // XJ+ / XJ-
            // XF+ / XF-
            // X
            // XR:{name}
            if (command[1] == 'R' && colon_position)
            {
                command = command.substr(colon_position + 1);
                Debug_printv("rom[%s]", command.c_str());
                command = mstr::toUTF8(command);
                if (!m_memory.setROM(command))
                {
                    setStatusCode(ST_FILE_NOT_FOUND);
                    break;
                }
                //setStatusCode(ST_OK);
                return;
            }
            // XS:{name} / XS
            // XW
            // X?
            //Extended();
        break;
        case '/':

        break;
        default:
            //Error(ERROR_31_SYNTAX_ERROR);
        break;
    }

    // Meatloaf Extended Commands
    command = mstr::toUTF8(command);
    auto pt = util_tokenize(command, ':');
    if ( pt[0] == "auth" )
    {
        if( pt.size() == 2 )
        {
            auto creds = util_tokenize(pt[1], ',');
            if( creds.size() == 2 )
            {
                Debug_printv("Setting authorization for url[%s] user[%s] pass[%s]", m_cwd->url.c_str(), creds[0].c_str(), creds[1].c_str());
                m_cwd->user = creds[0];
                m_cwd->password = creds[1];
                m_cwd->rebuildUrl();
                //setStatusCode(ST_OK);
                return;
            }
        }
    }

    setStatusCode(ST_SYNTAX_INVALID);
}


void iecDrive::setStatusCode(uint8_t code, uint8_t trk)
{
    //Debug_printv("code[%d]", code);
    m_statusCode = code;
    m_statusTrk  = trk;

    // clear current status buffer to force a call to getStatus()
    clearStatus();

#ifdef ENABLE_DISPLAY
    LEDS.status( code );
#endif
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
  
#ifdef ENABLE_DISPLAY
  LEDS.status( ST_OK );
#endif

  Debug_printv("status: %s", getCStringLog(std::string(buffer, res)));
  return res;
}


void iecDrive::getStatus(char *buffer, uint8_t bufferSize)
{
    const char *msg = NULL;
    switch( m_statusCode )
    {
        case ST_OK                  : msg = " OK"; break;
        case ST_SCRATCHED           : msg = "FILES SCRATCHED"; break;
        case ST_WRITE_VERIFY        : msg = "WRITE ERROR"; break;
        case ST_WRITE_PROTECT_ON    : msg = "WRITE PROTECT"; break;
        case ST_SYNTAX_INVALID      : msg = "INVALID COMMAND"; break;
        case ST_SYNTAX_BAD_NAME     : msg = "INVALID FILENAME"; break;
        case ST_FILE_NOT_FOUND      : msg = "FILE NOT FOUND"; break;
        case ST_FILE_NOT_OPEN       : msg = "FILE NOT OPEN"; break;
        case ST_FILE_EXISTS         : msg = "FILE EXISTS"; break;
        case ST_DOSVERSION          : msg = PRODUCT_ID " " FW_VERSION; break;
        case ST_NO_CHANNEL          : msg = "NO CHANNEL"; break;
        case ST_DRIVE_NOT_READY     : msg = "DRIVE NOT READY"; break;
        case ST_FILE_TYPE_MISMATCH  : msg = "FILE TYPE MISMATCH"; break;
        case ST_PERMISSION_DENIED   : msg = "PERMISSION DENIED"; break;
        default                     : msg = "UNKNOWN ERROR"; break;
    }

    snprintf(buffer, bufferSize, "%02d,%s,%02d,00\r", m_statusCode, msg, m_statusTrk);
}


void iecDrive::reset()
{
    Debug_printv("iecDrive::reset(#%d)", m_devnr);
    setStatusCode(ST_DOSVERSION);

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


void iecDrive::set_cwd(std::string path)
{
    // Isolate path
    if ( mstr::startsWith(path, ":") || mstr::startsWith(path, " ") )
        path = mstr::drop(path, 1);

    if ( !path.size() )
        path = "/";

    Debug_printv("path[%s]", path.c_str());
    path = mstr::toUTF8( path );

    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Changing directory to [%s][%s]", m_cwd->url.c_str(), path.c_str());
    MFile *n = m_cwd->cd( path );
    if( n != nullptr )
    {
        // bug in HTTPMFile: must call isDirectory() before getSourceStream(), otherwise 
        // getSourceStream() call will hang for several seconds for HTTP files
        bool isDirectory = n->isDirectory();

        // check whether we can get a stream
        std::shared_ptr<MStream> s = n->exists() ? n->getSourceStream() : nullptr;
        bool haveStream = (s!=nullptr);
        //if( s ) delete s;

        Debug_printv("url[%s] isDirectory[%i] haveStream[%i]", n->url.c_str(), isDirectory, haveStream);

//#ifdef USE_VDRIVE
        if( Meatloaf.use_vdrive )
        {
            if( n->exists() && !isDirectory && haveStream &&
                (m_vdrive=VDrive::create(m_devnr-8, n->url.c_str()))!=nullptr )
            {
                // we were able to creata a VDrive => this is a valid disk image
                Debug_printv("Created VDrive for URL %s", n->url.c_str());
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
            return;
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
}


/* Mount Disk
   We determine the type of image based on the filename extension.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t iecDrive::mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    Debug_printv("filename[%s], disksize[%lu] disktype[%d]", filename, disksize, disk_type);
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
    m_cwd.reset(m_cwd->cd(url));

    return MediaType::discover_mediatype(filename); // MEDIATYPE_UNKNOWN
}


// Unmount disk file
void iecDrive::unmount()
{
    Debug_printv("DRIVE[#%d] UNMOUNT\r\n", m_devnr);

    // if (m_cwd != nullptr)
    // {
    //   //m_cwd->unmount();
    //   device_active = false;
    // }
}


#if defined(IEC_FP_EPYX) && defined(IEC_FP_EPYX_SECTOROPS)
bool iecDrive::epyxReadSector(uint8_t track, uint8_t sector, uint8_t *buffer)
{
    return m_vdrive==nullptr ? false : m_vdrive->readSector(track, sector, buffer);
}


bool iecDrive::epyxWriteSector(uint8_t track, uint8_t sector, uint8_t *buffer)
{
    return m_vdrive==nullptr ? false : m_vdrive->writeSector(track, sector, buffer);
}
#endif



#endif /* BUILD_IEC */
