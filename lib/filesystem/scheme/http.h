// HTTP:// - Hypertext Transfer Protocol

#ifndef MEATFILE_DEFINES_FSHTTP_H
#define MEATFILE_DEFINES_FSHTTP_H

#include "meat_io.h"
#include "../../include/global_defines.h"
#include <esp_http_client.h>


/********************************************************
 * File implementations
 ********************************************************/


class HttpFile: public MFile {

public:
    HttpFile(std::string path): MFile(path) {};
    HttpFile(std::string path, std::string filename): MFile(path) {};

    bool isDirectory() override;
    MIStream* inputStream() override ; // has to return OPENED stream
    MOStream* outputStream() override ; // has to return OPENED stream
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override { return false; };
    MFile* getNextFileInDir() override { return nullptr; };
    bool mkDir() override { return false; };
    bool exists() override ;
    size_t size() override ;
    bool remove() override { return false; };
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
    }
    ~HttpIOStream() {
        close();
    }

    void close() override;
    bool open() override;

    // MStream methods
    size_t position() override;
    size_t available() override;
    size_t read(uint8_t* buf, size_t size) override;
    size_t write(const uint8_t *buf, size_t size) override;
    bool isOpen();

protected:
    std::string url;
    bool m_isOpen = false;
    size_t m_length = 0;
    size_t m_bytesAvailable = 0;
    size_t m_position = 0;
    esp_http_client_handle_t m_http = nullptr;
       
//    WiFiClient m_file;
//	  fnHttpClient m_http;
//    MHttpClient m_http;
};


class HttpIStream: public MIStream {

    static esp_err_t _http_event_handler(esp_http_client_event_t *evt);

public:
    HttpIStream(std::string path) {
        url = path;
    }
    ~HttpIStream() {
        close();
    }

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
    std::string url;
    bool m_isOpen = false;
    size_t m_bytesAvailable = 0;
    size_t m_length = 0;
    size_t m_position = 0;
    bool isFriendlySkipper = false;

//    WiFiClient m_file;
	// fnHttpClient m_http;
    // MHttpClient m_http;
    esp_http_client_handle_t m_http = nullptr;

};


class HttpOStream: public MOStream {
    static esp_err_t _http_event_handler(esp_http_client_event_t *evt);

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
    bool m_isOpen = false;
    size_t m_bytesAvailable = 0;
    size_t m_length = 0;
    size_t m_position = 0;
    bool isFriendlySkipper = false;
    
//    WiFiClient m_file;
    //WiFiClient m_client;
    // MHttpClient m_http;
	// fnHttpClient m_http;
    esp_http_client_handle_t m_http = nullptr;

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
        std::string pattern = "http:";
        return mstr::equals(name, pattern, false);
    }
public:
    HttpFileSystem(): MFileSystem("http") {};
};


#endif