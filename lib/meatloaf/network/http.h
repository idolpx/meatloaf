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

// HTTP://  - Hypertext Transfer Protocol
// HTTPS:// - Hypertext Transfer Protocol Secure
// https://buger.dread.cz/simple-esp8266-https-client-without-verification-of-certificate-fingerprint.html
// https://forum.arduino.cc/t/esp8266-httpclient-library-for-https/495245
// 


#ifndef MEATLOAF_SCHEME_HTTP
#define MEATLOAF_SCHEME_HTTP

#include "meatloaf.h"

#include <esp_http_client.h>
#include <functional>
#include <map>

#include "../../../include/debug.h"
#include "../../include/global_defines.h"

//#include "../../include/version.h"
//#define PRODUCT_ID "MEATLOAF CBM"
//#define PLATFORM_DETAILS "C64; 6510; 2; NTSC; EN;" // Make configurable. This will help server side to select appropriate content.
//#define USER_AGENT "MEATLOAF/" FN_VERSION_FULL " (" PLATFORM_DETAILS ")"


#include "utils.h"

#define HTTP_BLOCK_SIZE 256


class MeatHttpClient {
    esp_http_client_handle_t _http = nullptr;
    static esp_err_t _http_event_handler(esp_http_client_event_t *evt);
    int openAndFetchHeaders(esp_http_client_method_t method, uint32_t position, uint32_t size = HTTP_BLOCK_SIZE);
    esp_http_client_method_t lastMethod;
    std::function<int(char*, char*)> onHeader = [] (char* key, char* value){ 
        //Debug_printv("HTTP_EVENT_ON_HEADER, key=%s, value=%s", key, value);
        return 0; 
    };

    std::map<std::string, std::string> headers;

public:

    MeatHttpClient() {
        init();
    }

    void init() {
        esp_http_client_config_t config;
        memset(&config, 0, sizeof(config));
        config.url = "https://api.meatloaf.cc/?$";
        config.auth_type = HTTP_AUTH_TYPE_BASIC;
        config.user_agent = USER_AGENT;
        config.method = HTTP_METHOD_GET;
        config.timeout_ms = 10000;
        config.disable_auto_redirect = disableAutoRedirect;
        config.max_redirection_count = 10;
        config.event_handler = _http_event_handler;
        config.user_data = this;
        config.keep_alive_enable = true;
        config.keep_alive_idle = 5;
        config.keep_alive_interval = 5;

        //Debug_printv("HTTP Init url[%s]", url.c_str());
        _http = esp_http_client_init(&config);
    }

    ~MeatHttpClient() {
        close();
    }

    bool setHeader(const std::string header) {
        auto h = util_tokenize(header, ':');
        if ( h.size() == 2)
        {
            headers.insert( std::pair<std::string, std::string>(h[0], h[1]) );
            return true;
        }
        return false;
    }
    std::string readHeader(std::string header)
    {
        return headers[header];
    }

    bool GET(std::string url);
    bool POST(std::string url);
    bool PUT(std::string url);
    bool HEAD(std::string url);

    bool processRedirectsAndOpen(uint32_t position, uint32_t size = HTTP_BLOCK_SIZE);
    bool open(std::string url, esp_http_client_method_t meth);
    //void cancel();
    void close();
    void setOnHeader(const std::function<int(char*, char*)> &f);
    bool seek(uint32_t pos);
    uint32_t read(uint8_t* buf, uint32_t size);
    uint32_t write(const uint8_t* buf, uint32_t size);

    bool _is_open = false;
    bool _exists = false;

    uint32_t available() {
        return _size - _position;
    }

    bool complete() {
        return esp_http_client_is_complete_data_received(_http);
    }

    uint32_t _size = 0;
    uint32_t _range_size = 0;
    uint32_t _position = 0;
    size_t _error = 0;

    bool m_isWebDAV = false;
    bool m_isDirectory = false;
    bool isText = false;
    bool isFriendlySkipper = false;
    bool disableAutoRedirect = false;
    bool wasRedirected = false;
    std::string url;

    int lastRC = 0;
};

/********************************************************
 * File implementations
 ********************************************************/


class HTTPMFile: public MFile {
    std::shared_ptr<MeatHttpClient> fromHeader();
    std::shared_ptr<MeatHttpClient> client = nullptr;

public:
    HTTPMFile() {
        Debug_printv("C++, if you try to call this, be damned!");
    };
    HTTPMFile(std::string path): MFile(path) { 
        // Debug_printv("constructing http file from url [%s]", url.c_str());
    };
    HTTPMFile(std::string path, std::string filename): MFile(path) {};
    ~HTTPMFile() override {
        // if(client != nullptr)
        //     delete client;
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;

    bool remove() override ;
    bool isText() override ;
    bool rename(std::string dest) { return false; };
    
    //void addHeader(const String& name, const String& value, bool first = false, bool replace = true);
};


/********************************************************
 * Streams
 ********************************************************/

class HTTPMStream: public MStream {

public:
    HTTPMStream(std::string path): MStream(path) {
        //url = path;
    };
    HTTPMStream(std::string path, std::ios_base::openmode m): MStream(path) {
        //url = path;
        mode = m;
    };

    ~HTTPMStream() {
        close();
    };

    // MStream methods
    bool isOpen() override;
    // bool isBrowsable() override { return false; };
    // bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    // bool eos() override {
    //     Debug_printv("complete[%d]", _http.complete());
    //     return _http.complete();
    // }

    virtual bool seek(uint32_t pos);

    virtual bool seekPath(std::string path) override {
        Debug_printv( "path[%s]", path.c_str() );
        return false;
    }

    MeatHttpClient _http;

private:
    friend class HTTPMFile;
};



/********************************************************
 * FS
 ********************************************************/

class HTTPMFileSystem: public MFileSystem 
{
public:
    HTTPMFileSystem(): MFileSystem("http") {
        isRootFS = true;
    };

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"http:", false) )
            return true;

        if ( mstr::equals(name, (char *)"https:", false) )
            return true;
            
        return false;
    }

    MFile* getFile(std::string path) override {
        return new HTTPMFile(path);
    }
};


#endif /* MEATLOAF_SCHEME_HTTP */
