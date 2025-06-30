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

// WEBDAV:// - WebDAV

#ifndef MEATLOAF_SCHEME_WEBDAV
#define MEATLOAF_SCHEME_WEBDAV

#include "http.h"

#include "meatloaf.h"
#include "../../include/global_defines.h"


/********************************************************
 * File implementations
 ********************************************************/


class WebDAVMFile: public HTTPMFile {

public:
    WebDAVMFile(std::string path): HTTPMFile(path) {};

    bool isDirectory() override;
    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override { return false; };
    MFile* getNextFileInDir() override { return nullptr; };
    bool mkDir() override { return false; };
    bool exists() override ;

    bool remove() override { return false; };
    bool rename(std::string dest) { return false; };
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    //void addHeader(const String& name, const String& value, bool first = false, bool replace = true);
};


/********************************************************
 * Streams
 ********************************************************/

class WebDAVMStream: public HTTPMStream {
public:
WebDAVMStream(std::string& path): HTTPMStream(path) {
        url = path;
    }
    ~WebDAVMStream() {
        close();
    }

    void close() override;

    // MStream methods
    uint32_t position() override;
    uint32_t available() override;
    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;
    bool isOpen();

protected:
    std::string url;
    bool _is_open;

//    WiFiClient m_file;
	MeatHttpClient m_http;
};


/********************************************************
 * FS
 ********************************************************/

class WebDAVFileSystem: public MFileSystem 
{
public:
    WebDAVFileSystem(): MFileSystem("webdav") {};

    bool handles(std::string name) {
        std::string pattern = "webdav:";
        return mstr::equals(name, pattern, false);
    }

    MFile* getFile(std::string path) override {
        return new WebDAVMFile(path);
    }
};


#endif // MEATLOAF_SCHEME_WEBDAV