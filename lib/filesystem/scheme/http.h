// HTTP:// - Hypertext Transfer Protocol

#ifndef MEATFILESYSTEM_SCHEME_HTTP
#define MEATFILESYSTEM_SCHEME_HTTP

#include "meat_io.h"
#include "../../include/global_defines.h"
#include <esp_http_client.h>
#include <functional>


class MeatHttpClient {
    esp_http_client_handle_t m_http = nullptr;
    static esp_err_t _http_event_handler(esp_http_client_event_t *evt);
    int tryOpen(esp_http_client_method_t meth);
    esp_http_client_method_t lastMethod;
    std::function<int(char*, char*)> onHeader = [] (char* key, char* value){ 
        Debug_printv("HTTP_EVENT_ON_HEADER, key=%s, value=%s", key, value);
        return 0; 
    };

public:
    bool GET(std::string url);
    bool POST(std::string url);
    bool PUT(std::string url);
    bool HEAD(std::string url);

    bool open(std::string url, esp_http_client_method_t meth);
    void close();
    void setOnHeader(const std::function<int(char*, char*)> &f);
    bool seek(size_t pos);
    size_t read(uint8_t* buf, size_t size);
    size_t write(const uint8_t* buf, size_t size);
    bool m_isOpen = false;
    size_t m_length = 0;
    size_t m_bytesAvailable = 0;
    size_t m_position = 0;
    bool isText = false;
    bool isFriendlySkipper = false;
    bool wasRedirected = false;
    std::string url;

};

/********************************************************
 * File implementations
 ********************************************************/


class HttpFile: public MFile {

public:
    HttpFile() {
        Debug_printv("C++, if you try to call this, be damned!");
    };
    HttpFile(std::string path): MFile(path) { 
        Debug_printv("url[%s]", url.c_str());
     };
    HttpFile(std::string path, std::string filename): MFile(path) {};

    bool isDirectory() override;
    MIStream* inputStream() override ; // has to return OPENED stream
    MOStream* outputStream() override ; // has to return OPENED stream
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;
    size_t size() override ;
    bool remove() override ;
    bool rename(std::string dest) { return false; };
    MIStream* createIStream(std::shared_ptr<MIStream> src);
    //void addHeader(const String& name, const String& value, bool first = false, bool replace = true);
};


/********************************************************
 * Streams
 ********************************************************/

class HttpIOStream: public MIStream, MOStream {

public:
    HttpIOStream(std::string& path) {
        url = path;
    };
    ~HttpIOStream() {
        close();
    };

    void close() override;
    bool open() override;

    // MStream methods
    size_t position() override;
    size_t available() override;
    size_t read(uint8_t* buf, size_t size) override;
    size_t write(const uint8_t *buf, size_t size) override;
    bool isOpen();

protected:
    MeatHttpClient m_http;
    std::string url;
};


class HttpIStream: public MIStream {

public:
    HttpIStream(std::string path) {
        url = path;
    };
    ~HttpIStream() {
        close();
    };

    // MStream methods
    size_t size() override;
    size_t available() override;     
    size_t position() override;

    virtual bool seek(size_t pos);

    void close() override;
    bool open() override;

    // MIStream methods
    size_t read(uint8_t* buf, size_t size) override;
    bool isOpen();

protected:
    MeatHttpClient m_http;
    std::string url;

};


class HttpOStream: public MOStream {

public:
    // MStream methods
    HttpOStream(std::string path) {
        url = path;
    }
    ~HttpOStream() {
        close();
    }

    // MStream methods
    size_t size() override;
    size_t available() override;     
    size_t position() override;

    virtual bool seek(size_t pos);

    void close() override;
    bool open() override;


    // MOStream methods
    size_t write(const uint8_t *buf, size_t size) override;
    bool isOpen();

protected:
    std::string url;
    MeatHttpClient m_http;
};


/********************************************************
 * FS
 ********************************************************/

class HttpFileSystem: public MFileSystem 
{
    MFile* getFile(std::string path) override {
        return new HttpFile(path);
    }

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"http:", false) )
            return true;

        if ( mstr::equals(name, (char *)"https:", false) )
            return true;
            
        return false;
    }
public:
    HttpFileSystem(): MFileSystem("http") {};
};


#endif /* MEATFILESYSTEM_SCHEME_HTTP */
