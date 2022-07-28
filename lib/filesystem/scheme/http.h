// HTTP:// - Hypertext Transfer Protocol

#ifndef MEATFILE_DEFINES_FSHTTP_H
#define MEATFILE_DEFINES_FSHTTP_H

#include "fnHttpClient.h"

#include "meat_io.h"
#include "../../include/global_defines.h"


/********************************************************
 * File implementations
 ********************************************************/


class HttpFile: public MFile {

public:
    HttpFile(std::string path): MFile(path) {};

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
    bool m_isOpen;
    size_t m_length;
    size_t m_bytesAvailable = 0;
    size_t m_position = 0;
       
//    WiFiClient m_file;
	fnHttpClient m_http;
};


class HttpIStream: public MIStream {

public:
    HttpIStream(std::string path) {
        m_http.set_header("user-agent", USER_AGENT);
        //m_http.setUserAgent(USER_AGENT);
        //m_http.setTimeout(10000);
        //m_http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        //m_http.setRedirectLimit(10);
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
    bool m_isOpen;
    size_t m_bytesAvailable = 0;
    size_t m_length = 0;
    size_t m_position = 0;
    bool isFriendlySkipper = false;

//    WiFiClient m_file;
	fnHttpClient m_http;
};


class HttpOStream: public MOStream {

public:
    // MStream methods
    HttpOStream(std::string path) {
        m_http.set_header("user-agent", USER_AGENT);
        //m_http.setTimeout(10000);
        //m_http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        //m_http.setRedirectLimit(10);
        //m_http.setReuse(true);

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
	fnHttpClient m_http;
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