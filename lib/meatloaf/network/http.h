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

#ifndef MEATLOAF_SCHEME_HTTP
#define MEATLOAF_SCHEME_HTTP

void http_set_insecure(bool v);

#include "meatloaf.h"
#include "meat_session.h"
#include "service/mdns.h"

#include <esp_http_client.h>
#include <functional>
#include <map>

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"

#include "../../../include/version.h"
#define USER_AGENT "MEATLOAF/" FN_VERSION_FULL " (" PLATFORM_DETAILS ")"

#include "utils.h"

#define HTTP_BLOCK_SIZE 256

class HTTPMSession;

class HTTPRequestContext {
public:
    std::string method = "GET";
    std::map<std::string, std::vector<std::string>> headers;
    std::string body;

    std::vector<std::string> responseHeaders;
    int responseStatus = 0;
    bool responseConsumed = false;

    void setMethod(const std::string& m);
    void setHeader(const std::string& name, const std::string& value);
    void appendHeader(const std::string& name, const std::string& value);
    void setBody(const std::string& b);
    void appendBody(const std::string& b);
    void clear();

    bool sendRequest(std::shared_ptr<HTTPMSession> session);

    bool hasMoreResponseHeaders() const { return !responseHeaders.empty(); }
    std::string popResponseHeader();

    void errorToIecStatus(int& errOut, std::string& msgOut) const;
    bool isHttpError() const { return responseStatus < 200 || responseStatus >= 300; }
};

class MeatHttpClient {
    esp_http_client_handle_t _http = nullptr;
    static esp_err_t _http_event_handler(esp_http_client_event_t *evt);
    int openAndFetchHeaders(esp_http_client_method_t method, uint32_t position, uint32_t size = HTTP_BLOCK_SIZE);
    esp_http_client_method_t lastMethod;

    std::function<int(char*, char*)> onHeader = [] (char*, char*){ return 0; };
    std::map<std::string, std::string> headers;

public:
    std::vector<uint8_t> postBuffer;
    std::vector<uint8_t> postResponse;
    std::vector<uint8_t> preservedPostResponse;
    uint32_t preservedPostResponseSize = 0;

    MeatHttpClient() { init(); }
    void init();

    ~MeatHttpClient() {
        if (_http != nullptr) {
            esp_http_client_cleanup(_http);
            _http = nullptr;
        }
    }

    bool setHeader(const std::string header) {
        auto h = util_tokenize(header, ':');
        if (h.size() == 2) { headers[h[0]] = h[1]; return true; }
        return false;
    }
    std::string getHeader(std::string header) { return headers[header]; }

    bool GET(std::string url);
    bool POST(std::string url);
    bool PUT(std::string url);
    bool HEAD(std::string url);

    bool processRedirectsAndOpen(uint32_t position, uint32_t size = HTTP_BLOCK_SIZE);
    bool open(std::string url, esp_http_client_method_t meth);
    bool reopen();
    void close();
    void setOnHeader(const std::function<int(char*, char*)> &f);
    bool seek(uint32_t pos);
    bool flush(uint32_t numBytes = 0);
    uint32_t read(uint8_t* buf, uint32_t size);
    uint32_t write(const uint8_t* buf, uint32_t size);

    bool _is_open = false;
    bool _exists = false;

    uint32_t available() { return _size - _position; }

    bool complete() {
        if (_http == nullptr) {
            if (!preservedPostResponse.empty())
                return _position >= (uint32_t)preservedPostResponse.size();
            return true;
        }
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
    bool disableAutoRedirect = true;
    bool wasRedirected = false;
    std::string url;
    std::string contentDispositionFilename;

    bool _performPending = false;
    int lastRC = 0;
};

class HTTPMSession : public MSession {
public:
    HTTPMSession(std::string host, uint16_t port = 80);
    ~HTTPMSession() override;

    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    static std::string getScheme() { return "http"; }
    bool isSecure() const { return this->port == 443; }

    std::shared_ptr<MeatHttpClient> client;
};

class HTTPMFile: public MFile {
    std::shared_ptr<MeatHttpClient> getClient();
    std::shared_ptr<HTTPMSession> _session = nullptr;
    bool _headersFetched = false;

public:
    HTTPMFile() {};
    HTTPMFile(std::string path): MFile(path) {
        uint16_t http_port = port.empty() ? (scheme == "https" ? 443 : 80) : std::stoi(port);
        _session = SessionBroker::obtain<HTTPMSession>(host, http_port);
        if (!_session || !_session->isConnected()) {
            m_isNull = true;
            return;
        }
    };
    HTTPMFile(std::string path, std::string filename): MFile(path) {};
    ~HTTPMFile() override { if (_session) _session.reset(); }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    time_t getLastWrite() override;
    time_t getCreationTime() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override;
    bool exists() override;
    bool remove() override;
    bool isText() override;
    bool rename(std::string dest) { return false; };
};

class HTTPMStream: public MStream {
public:
    enum class FullModeState {
        SIMPLE,
        BUILDING_REQUEST,
        RESPONSE_HEADERS,
        RESPONSE_BODY
    };

    HTTPMStream(std::string path): MStream(path) {};
    HTTPMStream(std::string path, std::ios_base::openmode m): MStream(path) { mode = m; };
    ~HTTPMStream() { close(); };

    bool isOpen() override;
    bool isNetwork() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    uint32_t available() override {
        if (_queuedSend) return 0;
        if (!_responseBuffer.empty()) {
            uint32_t cnt = (uint32_t)_responseBuffer.size();
            if (_responseBufPos >= cnt) return 0;
            return cnt - _responseBufPos;
        }
        if (_session && _session->client && !_session->client->postResponse.empty()) {
            uint32_t respAvail = (uint32_t)_session->client->postResponse.size() - _session->client->_position;
            if (respAvail > 0) return respAvail;
        }
        if (_size > _position) return _size - _position;
        if (isOpen() && _session && _session->client && !_session->client->complete())
            return HTTP_BLOCK_SIZE;
        return 0;
    }

    virtual bool seek(uint32_t pos);
    virtual bool seekPath(std::string path) override { return false; }

    bool handleCommand(const std::string& cmd);
    bool isFullMode() const { return fullMode != FullModeState::SIMPLE; }

protected:
    std::shared_ptr<HTTPMSession> _session = nullptr;

private:
    friend class HTTPMFile;

    HTTPRequestContext ctx;
    FullModeState fullMode = FullModeState::SIMPLE;
    bool _statusRequested = false;
    std::string _statusBuffer;
    uint32_t _statusPos = 0;

    std::string _cmdLine;
    bool _queuedSend = false;

    // Buffer the whole HTTP response after sendRequest() so the IEC
    // handler's aggressive readBufferData() pre-fetch can't consume
    // headers before the C64 requests them via "r-h"/"r-b"/"status".
    // Layout: [status\r][header1\r]...[headerN\r][\r][body bytes]
    std::vector<uint8_t> _responseBuffer;
    uint32_t _responseBufPos = 0;
    uint32_t _statusEnd = 0;       // one past the last byte of "200\r"
    uint32_t _headersEnd = 0;      // one past the last byte of "\r" blank line

    // Body bytes captured during open() so _queuedSend doesn't need a
    // live HTTP connection to build the response buffer.
    std::vector<uint8_t> _bodyCapture;
};


class HTTPMFileSystem: public MFileSystem
{
public:
    HTTPMFileSystem(): MFileSystem("http") {
        isRootFS = true;
        service_type = "_http._tcp";
    };

    bool handles(std::string name) {
        if (mstr::equals(name, (char *)"http:", false)) return true;
        if (mstr::equals(name, (char *)"https:", false)) return true;
        return false;
    }

    MFile* getFile(std::string path) override {
        auto parser = PeoplesUrlParser::parseURL(path);
        if (parser->host.empty()) {
            path = "mdns://" + service_type;
            return new MDNSMFile(path);
        }
        return new HTTPMFile(path);
    }
};

#endif /* MEATLOAF_SCHEME_HTTP */
