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

#ifndef MEATLOAF_FILE
#define MEATLOAF_FILE

#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include <algorithm>
#include <cstring>

#include "../../include/debug.h"

#include "peoples_url_parser.h"
#include "string_utils.h"
#include "U8Char.h"

#define _MEAT_NO_DATA_AVAIL (std::ios_base::eofbit)

static const std::ios_base::iostate ndabit = _MEAT_NO_DATA_AVAIL;

/********************************************************
 * Universal stream
 ********************************************************/

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

#define SA0 0b00001111
#define SA1 0b00011111
#define SA2 0b00101111
#define SA3 0b00111111
#define SA4 0b01001111
#define SA5 0b01011111
#define SA6 0b01101111
#define SA7 0b01111111
#define SA8 0b10001111
#define SA9 0b10011111
#define SA10 0b10101111
#define SA11 0b10111111
#define SA12 0b11001111
#define SA13 0b11011111
#define SA14 0b11101111
#define SA15 0b11111111
// SA for TCP:
// TCP_NON_BLOCKING = clear bit 4
// TCP_BLOCKING = set bit 4
// TCP_CLENT_SOCKET = clear bit 5
// TCP_SERVER_SOCKET = set bit 5


/********************************************************
 * Universal stream
 ********************************************************/
class MStream 
{
protected:
    uint32_t _size = 0;
    uint32_t _position = 0;
    uint8_t _load_address[2] = {0, 0};
    uint8_t _error = 0;

public:
    MStream(std::string path) {
        url = path;
        //Debug_printv("url[%s]", url.c_str());
    };
    virtual ~MStream() {
        //Debug_printv("dstr url[%s]", url.c_str());
    };

    std::ios_base::openmode mode;
    std::string url = "";

    bool has_subdirs = true;
    size_t block_size = 256;

    virtual std::unordered_map<std::string, std::string> info() {
        return {};
    }

    virtual uint32_t size() {
        return _size;
    };

    virtual uint32_t available() {
        if ( _position > _size )
            return 0;

        return _size - _position;
    };

    virtual uint32_t position() {
        return _position;
    }
    virtual bool position( uint32_t p) {
        _position = p;
        return seek( _position );
    }

    virtual size_t error() {
        return _error;
    }

    virtual bool eos()  {
        //Debug_printv("_size[%d] _available[%d] _position[%d]", _size, available(), _position);
        if ( available() <= 0 )
            return true;
        
        return false;
    }
    virtual void reset() 
    {
        _size = block_size;
        _position = 0;
    };
    
    virtual bool isOpen() = 0;
    virtual bool isBrowsable() { return false; };
    virtual bool isRandomAccess() { return false; };

    virtual bool open(std::ios_base::openmode mode) = 0;
    virtual void close() = 0;

    virtual uint32_t read(uint8_t* buf, uint32_t size) = 0;
    virtual uint32_t write(const uint8_t *buf, uint32_t size) = 0;

    virtual bool seek(uint32_t pos, int mode) {
        if(mode == SEEK_SET) {
            _position = pos;
        }
        else if(mode == SEEK_CUR) {
            _position = _position + pos;
        }
        else {
            _position = _size - pos;
        }
        return seek( _position );
    }
    virtual bool seek(uint32_t pos) = 0;

    // For files with a browsable random access directory structure
    // d64, d74, d81, dnp, etc.
    virtual bool seekPath(std::string path) {
        return false;
    };

    // For files with no directory structure
    // tap, crt, tar
    virtual std::string seekNextEntry() {
        return "";
    };

    virtual bool seekBlock( uint64_t index, uint8_t offset = 0 ) { return false; };
    virtual bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) { return false; };
    virtual bool seekSector( std::vector<uint8_t> trackSectorOffset ) { return false; };

// private:

//     // DEVICE
//     friend class FlashMFile;

//     // NETWORK
//     friend class HTTPMFile;
//     friend class TNFSMFile;

//     // SERVICE
//     friend class CSIPMFile;
//     friend class TCPMFile;

//     friend class QRMFile;
};

/**
 * Buffered wrapper around any MStream. Provides an input buffer
 * that is refilled from the underlying stream and an optional
 * lazy output buffer that collects writes and flushes when full
 * or on demand. This wrapper does not change existing stream
 * implementations; it delegates to them.
 */
class BufferedMStream : public MStream {
public:
    static const size_t DEFAULT_IN_BUF = 2048;
    static const size_t DEFAULT_OUT_BUF = 512;

    BufferedMStream(std::shared_ptr<MStream> src, size_t inBuf = DEFAULT_IN_BUF, size_t outBuf = 0)
        : MStream(src ? src->url : std::string()), inner(src), inBufSize(inBuf), outBufSize(outBuf)
    {
        inBufData.resize(inBufSize);
        if (outBufSize)
            outBufData.resize(outBufSize);
        _size = inner ? inner->size() : 0;
        _position = inner ? inner->position() : 0;
    }

    virtual ~BufferedMStream() {
        flushOut();
    }

    bool isOpen() override {
        return inner && inner->isOpen();
    }

    bool isBrowsable() override { return inner ? inner->isBrowsable() : false; }
    bool isRandomAccess() override { return inner ? inner->isRandomAccess() : false; }

    bool open(std::ios_base::openmode m) override {
        if (!inner) return false;
        mode = m;
        return inner->open(m);
    }

    void close() override {
        flushOut();
        if (inner) inner->close();
    }

    uint32_t read(uint8_t* buf, uint32_t size) override {
        if (!inner || !isOpen() || size == 0) return 0;

        // Determine how many bytes we are allowed to return.
        // If caller requests more than one byte, reserve one byte
        // in the underlying stream (used to signal EOF). When the
        // caller asks for exactly one byte, return that last byte.
        uint32_t total = 0;
        uint32_t allowed = size;

        if (size > 1) {
            // sync underlying position so available() is accurate
            if (!inner->seek(_position)) return 0;
            uint32_t availAll = inner->available();
            if (availAll == 0) return 0; // nothing available
            if (availAll <= 1) return 0; // only the final signaling byte remains, keep it
            allowed = std::min<uint32_t>(size, availAll - 1);
        } else {
            // size == 1, we should deliver one byte even if it's the last
            allowed = 1;
        }

        while (total < allowed) {
            if (_position >= inStart && _position < inEnd) {
                uint32_t offset = (uint32_t)(_position - inStart);
                uint32_t avail = (uint32_t)(inEnd - _position);
                uint32_t toCopy = std::min<uint32_t>(avail, allowed - total);
                // When reserving one byte globally, ensure we don't consume
                // the last byte from this buffer chunk if it represents the
                // final allowed byte. The global 'allowed' cap above already
                // enforces this, so standard copy is fine.
                if (toCopy > 0) {
                    memcpy(buf + total, inBufData.data() + offset, toCopy);
                    total += toCopy;
                    _position += toCopy;
                }
                // If nothing copied because buffer had only the reserved byte,
                // fall through to refill logic.
                if (toCopy == 0)
                    ;
                else
                    continue;
            }

            // refill buffer from underlying stream starting at _position
            if (!inner->seek(_position)) {
                break;
            }
            int rc = inner->read(inBufData.data(), (uint32_t)inBufSize);
            if (rc == 0) break;
            if ((uint32_t)rc == (uint32_t)_MEAT_NO_DATA_AVAIL) {
                break;
            }
            if (rc < 0) break;

            inStart = _position;
            inEnd = inStart + rc;

            // If we're reserving one byte globally and this refill only
            // produced a single byte, do not consume it now.
            if (size > 1 && (uint32_t)rc <= 1) {
                break;
            }
        }

        return total;
    }

    uint32_t write(const uint8_t* buf, uint32_t size) override {
        if (!inner || !isOpen()) return 0;

        // if no out buffering requested, write directly
        if (outBufSize == 0) {
            if (!inner->seek(_position)) return 0;
            uint32_t rc = inner->write(buf, size);
            _position += rc;
            _size = std::max<uint32_t>(_size, _position);
            return rc;
        }

        uint32_t written = 0;
        while (written < size) {
            uint32_t space = (uint32_t)(outBufSize - outFill);
            if (space == 0) {
                if (!flushOut()) break;
                space = (uint32_t)(outBufSize - outFill);
                if (space == 0) break;
            }
            uint32_t toCopy = std::min<uint32_t>(space, size - written);
            memcpy(outBufData.data() + outFill, buf + written, toCopy);
            outFill += toCopy;
            written += toCopy;
            _position += toCopy;
            _size = std::max<uint32_t>(_size, _position);
        }

        return written;
    }

    bool seek(uint32_t pos) override {
        // seeking for write must flush pending output
        if (outFill > 0) {
            if (!flushOut()) return false;
        }

        // if seek within input buffer, just update position
        if (pos >= inStart && pos < inEnd) {
            _position = pos;
            return true;
        }

        // invalidate input buffer and seek underlying
        inStart = inEnd = 0;
        _position = pos;
        if (inner) return inner->seek(pos);
        return false;
    }

    // delegate other optional ops
    bool seekPath(std::string path) override { return inner ? inner->seekPath(path) : false; }
    std::string seekNextEntry() override { return inner ? inner->seekNextEntry() : std::string(); }
    bool seekBlock(uint64_t index, uint8_t offset = 0) override { return inner ? inner->seekBlock(index, offset) : false; }
    bool seekSector(uint8_t track, uint8_t sector, uint8_t offset = 0) override { return inner ? inner->seekSector(track, sector, offset) : false; }

    // flush any pending output buffer
    bool flushOut() {
        if (!inner) return false;
        if (outFill == 0) return true;

        // ensure underlying at correct position for write
        uint32_t writePos = _position - outFill;
        if (!inner->seek(writePos)) return false;
        uint32_t rc = inner->write(outBufData.data(), outFill);
        if (rc != outFill) return false;
        outFill = 0;
        return true;
    }

protected:
    std::shared_ptr<MStream> inner;

    // input buffer
    std::vector<uint8_t> inBufData;
    size_t inBufSize = 0;
    uint32_t inStart = 0; // absolute stream position of buffer start
    uint32_t inEnd = 0;   // absolute stream position of buffer end (one past last)

    // optional output buffer
    std::vector<uint8_t> outBufData;
    size_t outBufSize = 0;
    uint32_t outFill = 0;
};


/********************************************************
 * Universal file
 ********************************************************/

class MFile : public PeoplesUrlParser {
public:
    MFile() {}; // only for local FS!!!
    MFile(nullptr_t null) : m_isNull(true) {};
    MFile(std::string path);
    MFile(std::string path, std::string name);
    MFile(MFile* path, std::string name);

    virtual ~MFile() {
        if(sourceFile != nullptr) {
        //     Debug_printv("WARNING: sourceFile null in '%s' destructor. This MFile was obviously not initialized properly!", url.c_str());
        // }
        // else {
            //Debug_printv("Deleting: [%s]", this->url.c_str());
            delete sourceFile;
        }
        //Debug_printv("dtor path[%s]", path.c_str());
    };

    std::string type;

    bool isPETSCII = false;
    bool isWritable = false;
    std::string media_header;
    std::string media_id;
    std::string media_archive;
    std::string media_image;
    uint16_t media_blocks_free = 0;
    uint16_t media_block_size = 256;

    bool operator!=(nullptr_t ptr);

    // bool copyTo(MFile* dst);

    // has to return OPENED stream
    virtual std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in);
    virtual std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) = 0;
    virtual std::shared_ptr<MStream> createStream(std::ios_base::openmode) { return nullptr; };

    virtual bool format(std::string header_info) { return false; };

    MFile* cd(std::string newDir);
    MFile* cdParent(std::string = "");
    MFile* cdLocalParent(std::string);
    MFile* cdRoot(std::string);
    MFile* cdLocalRoot(std::string);

    virtual bool isDirectory() {
        if (sourceFile == nullptr)
        {
            Debug_printv("null sourceFile for path[%s]", path.c_str());
            return true;
        }

        //Debug_printv("pathInStream[%s] sourcePathInStream[%s]", pathInStream.c_str(), sourceFile->pathInStream.c_str());
        if (pathInStream.empty() || pathInStream == "/")
            return true;
        else
            return false;
    };
    virtual bool rewindDirectory() { return false; };
    virtual MFile* getNextFileInDir() { return nullptr; };

    virtual bool mkDir() { return false; };
    virtual bool rmDir() { return false; };
    virtual bool exists();
    virtual bool remove() { return false; };
    virtual bool rename(std::string dest) { return false; };
    virtual time_t getLastWrite() { return 0; };
    virtual time_t getCreationTime() { return 0; };
    virtual uint64_t getAvailableSpace();

    virtual uint32_t blocks() {
        //Debug_printv("size[%d] media_block_size[%d]", size, media_block_size);
        if ( size > 0 && size < media_block_size )
            return 1;
        else
            return ( size / media_block_size );
    }

    virtual bool isText() {
        return mstr::isText(extension);
    }

    MFile* sourceFile = nullptr;
    std::string pathInStream;

    uint32_t size = 0;
    uint32_t _exists = true;

    void dump() {
        static int index = 0;
        printf("index[%d] url[%s][%s][%s]\r\n", index, url.c_str(), pathInStream.c_str(), type.c_str());
        index++;
        if ( sourceFile != nullptr )
            sourceFile->dump();
        index--;
    }

protected:
    bool m_isNull;

friend class MFSOwner;
};


/********************************************************
 * Filesystem instance
 * it knows how to create a MFile instance!
 ********************************************************/

class MFileSystem {
public:
    MFileSystem(const char* symbol);
    virtual ~MFileSystem() = 0;

    bool isRootFS = false;
    bool vdrive_compatible = false;

    virtual bool handles(std::string path) = 0;
    virtual MFile* getFile(std::string path) = 0;

    virtual bool mount() { return true; }
    virtual bool umount() { return true; }

    bool isMounted() {
        return _is_mounted;
    }

    // Determine file type by file contents
    static std::string byContent(const char* header) 
    {
        std::string extension;

        // Determine file type by file header
        // https://en.wikipedia.org/wiki/List_of_file_signatures
        if ( mstr::startsWith(header, "\x04\x01") ||
             mstr::startsWith(header, "\x08\x01") )
            extension = ".prg";
        else if ( mstr::startsWith(header, "C64File") )
            extension = ".p00";
        else if ( mstr::startsWith(header, "C64-TAPE-RAW") )
            extension = ".tap";
        else if ( mstr::startsWith(header, "CUTE32-HIRES") )
            extension = ".htap";
        else if ( mstr::startsWith(header, "C64 tape image file") )
            extension = ".t64";
        else if ( mstr::startsWith(header, "tapecartImage") )
            extension = ".tcrt";
        else if ( mstr::startsWith(header, "C64 CARTRIDGE\x20\x20\x20") )
            extension = ".crt";
        else if ( mstr::startsWith(header, "GCR-1541") )
            extension = ".g64";
        else if ( mstr::startsWith(header, "GCR-1571") )
            extension = ".g71";
        else if ( mstr::startsWith(header, "MFM-1581") )
            extension = ".g81";
        else if ( mstr::startsWith(header, "MNIB-1541-RAW") )
            extension = ".nib";
        else if ( mstr::startsWith((header + 1), "MNIB-1541-RAW") )
            extension = ".nbz";
        else if ( mstr::startsWith(header, "P64-1541") )
            extension = ".p64";
        else if ( mstr::startsWith(header, "P64-1581") )
            extension = ".p81";
        else if ( mstr::startsWith(header, "SCP") )
            extension = ".scp";

        if ( mstr::startsWith(header, "\x00\x60") )
            extension = ".koa";
        else if ( mstr::startsWith(header, "PSID") )
            extension = ".psid";


        else if ( mstr::startsWith(header, "PK\x03\x04") ||
                  mstr::startsWith(header, "PK\x05\x06") ||
                  mstr::startsWith(header, "PK\x07\x08") )
            extension = ".zip";
        else if ( mstr::startsWith(header, "Rar!\x1A\x07"))
            extension = ".rar";

        return extension;
    }

    // Determine file type by file size
    static std::string bySize(size_t size) 
    {
        std::string extension;

        switch(size) {
            case 174848: // 35 tracks no errors
            case 175531: // 35 w/ errors
            case 196608: // 40 tracks no errors
            case 197376: // 40 w/ errors
            case 205312: // 42 tracks no errors
            case 206114: // 42 w/ errors
                extension = ".d64";
                break;

            case 349696: // 70 tracks no errors
            case 351062: // 70 w/ errors
                extension = ".d71";
                break;

            case 533248:
                extension = ".d80";
                break;

            case 819200:  // 80 tracks no errors
            case 822400:  // 80 w/ errors
            case 829440:  // 81 tracks no errors
                extension = ".d81";
                break;

            case 1066496:
                extension = ".d82";
                break;

            case 1392640: // 136 sectors per track (deprecated)
            case 1474560: // 144 sectors per track
                extension = ".d8b";
                break;

            case 5013504: // D9060
            case 7520256: // D9090
                extension = ".d90";
                break;

            case 10003: // Koala image
                extension = ".koa";
                break;

            default:
                break;
        }

        return extension;
    }

    static bool isIgnored(const char* ext)
    {
        std::vector<std::string> ignored = { ".prg", ".php" };
        for ( const auto &e : ignored )
        {
            if ( mstr::endsWith(ext, e.c_str(), false) )
                return true;
        }
        return false;
    }

    static bool byExtension(const char* ext, std::string fileName) {
        return mstr::endsWith(fileName, ext, false);
    }

    static bool byExtension(const std::vector<std::string> &ext, std::string fileName, bool wildcard = false) {
        if ( fileName.size() )
        {
            // exclude hidden files/folders
            if (fileName[0] == '.')
                return false;

            std::string extension;
            if ( fileName.find(".") != std::string::npos )
                extension = fileName.substr(fileName.find_last_of("."));
            else
                return false;

            if ( isIgnored(extension.c_str()))
                return false;

            for ( const auto &e : ext )
            {
                //Debug_printv("comparing [%s][%s] to [%s] wildcard[%d]", fileName.c_str(), extension.c_str(), e.c_str(), wildcard);
                if ( wildcard )
                {
                    if ( mstr::compare(e, extension, false) )
                        return true;
                }
                else
                {
                    if ( mstr::endsWith(extension, e.c_str(), false) )
                        return true;
                }
            }
        }

        return false;
    }

protected:
    const char* symbol = nullptr;
    bool _is_mounted = false;

    friend class MFSOwner;
};


/********************************************************
 * MFile factory
 ********************************************************/

class MFSOwner {
public:
    static std::vector<MFileSystem*> availableFS;
    static std::vector<MFileSystem*> availableOther;

    static MFile* File(std::string name, bool default_fs = false);
    static MFile* File(std::shared_ptr<MFile> file);
    static MFile* File(MFile* file);
    //static MFile* SourceFile(MFile* file);
    static MFile* NewFile(std::string name);

    static MFileSystem* scanPathLeft(std::vector<std::string> paths, std::vector<std::string>::iterator &pathIterator);

    static std::string existsLocal( std::string path );
    static MFileSystem* findParentFS(std::vector<std::string>::iterator &begin, std::vector<std::string>::iterator &pathIterator);


    static bool mount(std::string name);
    static bool umount(std::string name);
};

/********************************************************
 * Meat namespace, standard C++ buffers and streams
 ********************************************************/

namespace Meat {
    struct _Unique_mf {
        typedef std::unique_ptr<MFile> _Single_file;
    };

    // Creates a unique_ptr<MFile> for a given url

    /**
    *  @brief  Creates a unique_ptr<MFile> instance froma given url
    *  @param  url  The url to the file.
    *  @return  @c unique_ptr<MFile>
    */
    template<class MFile>
        typename _Unique_mf::_Single_file
        New(std::string url) {
            return std::unique_ptr<MFile>(MFSOwner::File(url));
        }

    /**
    *  @brief  Creates a unique_ptr<MFile> instance froma given url
    *  @param  url  The url to the file.
    *  @return  @c unique_ptr<MFile>
    */
    template<class MFile>
        typename _Unique_mf::_Single_file
        New(const char* url) {
            return std::unique_ptr<MFile>(MFSOwner::File(std::string(url)));
        }

    /**
    *  @brief  Creates a unique_ptr<MFile> instance from a given MFile
    *  @param  file  The url to the file.
    *  @return  @c unique_ptr<MFile>
    */
    template<class MFile>
        typename _Unique_mf::_Single_file
        New(MFile* mFile) {
            return std::unique_ptr<MFile>(MFSOwner::File(mFile->url));
        }

    /**
    *  @brief  Wraps MFile* into unique_ptr<MFile> so it closes itself as required
    *  @param  file  The url to the file.
    *  @return  @c unique_ptr<MFile>
    */
    template<class MFile>
        typename _Unique_mf::_Single_file
        Wrap(MFile* file) {
            return std::unique_ptr<MFile>(file);
        }

}


/********************************************************
 * Utility implementations
 ********************************************************/

// class FileBroker {
//     //static std::unordered_map<std::string, MFile*> file_repo;
//     static std::unordered_map<std::string, MFile*> file_repo;
// public:
//     static MFile* obtain(std::string url) {
//         if(file_repo.find(url)!=file_repo.end())
//         {
//             Debug_printv("Reusing Existing MFile url[%s]", url.c_str());
//             return file_repo.at(url);
//         }

//         return nullptr;
//     }

//     static void add(std::string url, MFile* newFile) {
//         file_repo.insert(std::make_pair(url, newFile));
//     }

//     static void dispose(std::string url) {
//         if(file_repo.find(url)!=file_repo.end()) {
//             auto toDelete = file_repo.at(url);
//             file_repo.erase(url);
//             delete toDelete;
//         }
//         Debug_printv("streams[%d]", file_repo.size());
//     }

//     static void validate() {
        
//     }

//     static void clear() {
//         // std::for_each(file_repo.begin(), file_repo.end(), [](auto& pair) {
//         //     delete pair.second;
//         // });
//         file_repo.clear();
//     }
// };

// class StreamBroker {
//     static std::unordered_map<std::string, std::shared_ptr<MStream>> stream_repo;
// public:
//     template<class T> static std::shared_ptr<T> obtain(std::string url, std::ios_base::openmode mode) 
//     {
//         //Debug_printv("streams[%d] url[%s]", stream_repo.size(), url.c_str());

//         // obviously you have to supply sourceFile.url to this function!
//         if(stream_repo.find(url)!=stream_repo.end())
//         {
//             Debug_printv("Reusing Existing Stream url[%s]", url.c_str());
//             return stream_repo.at(url);
//         }

//         // create and add stream to broker if not found
//         Debug_printv("Creating New Stream url[%s]", url.c_str());
//         auto newFile = MFSOwner::File(url);

//         std::shared_ptr<T> newStream = newFile->createStream(mode);

//         if ( newStream != nullptr )
//         {
//             // Are we at the root of the filesystem?
//             if ( newFile->pathInStream == "")
//             {
//                 Debug_printv("ROOT FILESYSTEM... CACHING [%s]", url.c_str());
//                 stream_repo.insert(std::make_pair(url, newStream));
//             }
//             else
//             {
//                 Debug_printv("SINGLE FILE... DON'T CACHE [%s]", url.c_str());
//             }
            
//             return newStream;
//         }

//         delete newFile;
//         return nullptr;
//     }

//     static std::shared_ptr<MStream> obtain(std::string url, std::ios_base::openmode mode) {
//         return obtain<MStream>(url, mode);
//     }

//     static void dispose(std::string url) {
//         if(stream_repo.find(url)!=stream_repo.end()) {
//             auto toDelete = stream_repo.at(url);
//             stream_repo.erase(url);
//             //delete toDelete;
//         }
//         Debug_printv("streams[%d]", stream_repo.size());
//     }

//     static void validate() {
        
//     }

//     static void clear() {
//         // std::for_each(file_repo.begin(), file_repo.end(), [](auto& pair) {
//         //     delete pair.second;
//         // });
//         stream_repo.clear();
//     }
// };

#endif // MEATLOAF_FILE
