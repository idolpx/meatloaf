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

#include "http.h"

#include <esp_idf_version.h>
#include <algorithm>

#include "meatloaf.h"
#include "meat_session.h"

#include "../../../include/debug.h"
//#include "../../../include/global_defines.h"

/********************************************************
 * HTTPMSession implementation
 ********************************************************/

HTTPMSession::HTTPMSession(std::string host, uint16_t port)
    : MSession(host, port), _user_agent(USER_AGENT) {
    Debug_printv("HTTPMSession created for %s:%d", host.c_str(), port);

    // Initialize HTTP client configuration
    memset(&_config, 0, sizeof(_config));
    
    // Set host and port - required for esp_http_client_init
    _config.host = host.c_str();
    _config.port = port;
    _config.path = "/";  // Default path
    
    _config.auth_type = HTTP_AUTH_TYPE_BASIC;
    _config.user_agent = _user_agent.c_str();
    _config.method = HTTP_METHOD_GET;
    _config.timeout_ms = 10000;
    _config.disable_auto_redirect = false;
    _config.max_redirection_count = 10;
    _config.event_handler = nullptr; // Will be set by individual clients
    _config.user_data = nullptr;
    _config.keep_alive_enable = _keep_alive_enabled;
    _config.keep_alive_idle = 5;
    _config.keep_alive_interval = 5;

    // For HTTPS, we might need additional SSL configuration
    if (isSecure()) {
        _config.transport_type = HTTP_TRANSPORT_OVER_SSL;
        _config.skip_cert_common_name_check = true; // For development/testing
    } else {
        _config.transport_type = HTTP_TRANSPORT_OVER_TCP;
    }
}

HTTPMSession::~HTTPMSession() {
    Debug_printv("HTTPMSession destroyed for %s:%d", host.c_str(), port);
    disconnect();
}

bool HTTPMSession::connect() {
    if (connected) {
        return true;
    }

    Debug_printv("HTTPMSession connecting to %s:%d", host.c_str(), port);

    // HTTP doesn't maintain persistent connections like other protocols
    // We consider it "connected" as long as the server is reachable
    // The actual connection establishment happens in individual HTTP requests

    connected = true;
    updateActivity();
    return true;
}

void HTTPMSession::disconnect() {
    if (!connected) {
        return;
    }

    Debug_printv("HTTPMSession disconnecting from %s:%d", host.c_str(), port);

    // HTTP doesn't maintain persistent connections to close
    // Individual HTTP clients will handle their own cleanup

    connected = false;
}

bool HTTPMSession::keep_alive() {
    if (!connected) {
        return false;
    }

    // HTTP keep-alive is handled at the individual request level
    // by the esp_http_client with keep_alive_enable = true
    updateActivity();
    return true;
}

esp_http_client_config_t* HTTPMSession::getClientConfig() {
    return &_config;
}

/********************************************************
 * MFile implementations
 ********************************************************/

std::shared_ptr<MeatHttpClient> HTTPMFile::fromHeader() {
    if(client == nullptr) {
        //Debug_printv("Client was not present, creating");
        client = std::make_shared<MeatHttpClient>();

        // let's just get the headers so we have some info
        //Debug_printv("Client requesting head");

        // if (mstr::endsWith(url, "*") || mstr::endsWith(url, "$")) {
        //     url = mstr::dropLast(url, 1);
        //     Debug_printv("url[%s]", url.c_str());
        // }

        //Debug_printv("before head url[%s]", url.c_str());
        client->HEAD(url);
        //Debug_printv("after head url[%s]", client->url.c_str());
        if (client->wasRedirected)
            resetURL(client->url);
    }
    return client;
}


std::shared_ptr<MStream> HTTPMFile::getSourceStream(std::ios_base::openmode mode) {
    // has to return OPENED stream
    //Debug_printv("Input stream requested: [%s]", url.c_str());

    // headers have to be supplied here, they might be set on this channel using
    // i.e. CBM DOS commands like
    // H+:Accept: */*
    // H+:Accept-Encoding: gzip, deflate
    // here you add them all to a map, like this:
    std::map<std::string, std::string> headers;
    // headers["Accept"] = "*/*";
    // headers["Accept-Encoding"] = "gzip, deflate";
    // etc.
    if ( pathInStream.size() )
        url += "/" + pathInStream;

    std::shared_ptr<MStream> istream = std::make_shared<HTTPMStream>(url, mode);
    //auto istream = StreamBroker::obtain<HTTPMStream>(url, mode);
    istream->open(mode);
    resetURL(istream->url);
    size = istream->size();

    return istream;
}

std::shared_ptr<MStream> HTTPMFile::getDecodedStream(std::shared_ptr<MStream> is) {
    return is; // DUMMY return value - we've overriden istreamfunction, so this one won't be used
}

std::shared_ptr<MStream> HTTPMFile::createStream(std::ios_base::openmode mode)
{
    std::shared_ptr<MStream> istream = std::make_shared<HTTPMStream>(url);
    istream->open(mode);
    return istream;
}

bool HTTPMFile::isDirectory() {
    // if(fromHeader()->m_isDirectory)
    //     return true;

    // if(fromHeader()->m_isWebDAV) {
    //     // try webdav PROPFIND to get a listing
    //     return true;
    // }
    // else
        // otherwise return false
        return false;
}

time_t HTTPMFile::getLastWrite() {
    if(fromHeader()->m_isWebDAV) {
        return 0;
    }
    else
    // take from webdav PROPFIND or fallback to Last-Modified
        return 0; 
}

time_t HTTPMFile::getCreationTime() {
    if(fromHeader()->m_isWebDAV) {
        return 0;
    }
    else
    // take from webdav PROPFIND or fallback to Last-Modified
        return 0; 
}

bool HTTPMFile::exists() {
    return fromHeader()->_exists;
    return true;
}

bool HTTPMFile::remove() {
    if(fromHeader()->m_isWebDAV) {
        // PROPPATCH allows deletion
        return false;
    }
    return false;
}

bool HTTPMFile::mkDir() {
    if(fromHeader()->m_isWebDAV) {
        // MKCOL creates dir
        return false;
    }
    return false;
}

bool HTTPMFile::rewindDirectory() {
    if(fromHeader()->m_isWebDAV) { 
        // we can try if this is webdav, then
        // PROPFIND allows listing dir
        return false;
    }
    return false; 
};

MFile* HTTPMFile::getNextFileInDir() { 
    Debug_printv("");
    if(fromHeader()->m_isWebDAV) {
        // we can try if this is webdav, then
        // PROPFIND allows listing dir
        return nullptr;
    }
    return nullptr; 
};


bool HTTPMFile::isText() {
    return fromHeader()->isText;
}

/********************************************************
 * Istream impls
 ********************************************************/
bool HTTPMStream::open(std::ios_base::openmode mode) {
    if (isOpen()) {
        close();
    }

    this->mode = mode;

    // Parse URL to get session
    auto parser = PeoplesUrlParser::parseURL(url);
    if (!parser || (parser->scheme != "http" && parser->scheme != "https")) {
        Debug_printv("Invalid HTTP URL: %s", url.c_str());
        _error = EINVAL;
        return false;
    }

    // Get session - use default ports if not specified
    uint16_t http_port = parser->port.empty() ? (parser->scheme == "https" ? 443 : 80) : std::stoi(parser->port);
    _session = SessionBroker::obtain<HTTPMSession>(parser->host, http_port);
    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to get HTTP session for %s:%d", parser->host.c_str(), http_port);
        _error = ECONNREFUSED;
        return false;
    }

    // Initialize HTTP client with session config
    _http.init(_session);

    bool r = false;

    if(mode == (std::ios_base::out | std::ios_base::app))
        r = _http.PUT(url);
    else if(mode == std::ios_base::out)
        r = _http.POST(url);
    else
        r = _http.GET(url);

    _size = ( _http._range_size > 0) ? _http._range_size : _http._size;
    if ( _http.wasRedirected )
        url = _http.url;

    //Debug_printv("r[%d] size[%d] url[%s] hurl[%s]", r, _size, url.c_str(), _http.url.c_str());

    return r;
}

void HTTPMStream::close() {
    //Debug_printv("CLOSE called explicitly on this HTTP stream!");
    _http.close();
}

bool HTTPMStream::seek(uint32_t pos) {
    if ( !_http._is_open )
    {
        Debug_printv("error");
        _error = 1;
        return false;
    }

    return _http.seek(pos);
}

uint32_t HTTPMStream::read(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    if ( size > 0 )
    {
        if ( size > available() )
            size = available();

        bytesRead = _http.read(buf, size);
        _position += bytesRead;
        _error = _http._error;
    }

    return bytesRead;
};

uint32_t HTTPMStream::write(const uint8_t *buf, uint32_t size) {
    uint32_t bytesWritten = _http.write(buf, size);
    _position += bytesWritten;
    return bytesWritten;
}


bool HTTPMStream::isOpen() {
    return _http._is_open;
};


/********************************************************
 * Meat HTTP client impls
 ********************************************************/
bool MeatHttpClient::GET(std::string dstUrl) {
    Debug_printv("GET url[%s]", dstUrl.c_str());
    return open(dstUrl, HTTP_METHOD_GET);
}

bool MeatHttpClient::POST(std::string dstUrl) {
    Debug_printv("POST url[%s]", dstUrl.c_str());
    return open(dstUrl, HTTP_METHOD_POST);
}

bool MeatHttpClient::PUT(std::string dstUrl) {
    Debug_printv("PUT url[%s]", dstUrl.c_str());
    return open(dstUrl, HTTP_METHOD_PUT);
}

bool MeatHttpClient::HEAD(std::string dstUrl) {
    Debug_printv("HEAD url[%s]", dstUrl.c_str());
    bool rc = open(dstUrl, HTTP_METHOD_HEAD);
    return rc;
}

bool MeatHttpClient::open(std::string dstUrl, esp_http_client_method_t meth) {
    url = dstUrl;
    lastMethod = meth;
    _error = 0;

    return processRedirectsAndOpen(0);
};

bool MeatHttpClient::processRedirectsAndOpen(uint32_t position, uint32_t size) {
    wasRedirected = false;
    m_isDirectory = false;

    uint8_t redirects = 0;
    do {
        if (redirects++ > 10) {
            Debug_printv("too many redirects");
            return false;
        }

        //Debug_printv("opening url[%s] from position:%lu", url.c_str(), position);
        lastRC = openAndFetchHeaders(lastMethod, position, size);
    } while (lastRC >= 300 && lastRC <= 399);

    if (lastRC == 206)
    {
        isFriendlySkipper = true;
    }
    else if(lastRC > 399 || _error != 0)
    {
        Debug_printv("opening stream failed, httpCode=%d", lastRC);
        _error = lastRC;
        _is_open = false;
        return false;
    }

    _is_open = true;
    _exists = true;
    _position = position;

    //Debug_printv("size[%lu] avail[%lu] isFriendlySkipper[%d] isText[%d] httpCode[%d] method[%d]", _size, available(), isFriendlySkipper, isText, lastRC, lastMethod);

    return true;
}


// void MeatHttpClient::cancel() {
//     if(_http != nullptr) {
//         if ( _is_open ) {
//             esp_http_client_cancel_request(_http);
//         }
//         Debug_printv("HTTP Cancel");
//     }
//     _is_open = false;
// }

void MeatHttpClient::close() {
    if(_http != nullptr) {
        if ( _is_open ) {
            esp_http_client_close(_http);
        }
        esp_http_client_cleanup(_http);
        Debug_printv("HTTP Close and Cleanup");
        _http = nullptr;
    }
    _is_open = false;
}

void MeatHttpClient::setOnHeader(const std::function<int(char*, char*)> &lambda) {
    onHeader = lambda;
}

bool MeatHttpClient::seek(uint32_t pos) {

    if(isFriendlySkipper) {

        if (_is_open) {
            // Read to end of the stream
            //Debug_printv("Skipping to end!");
            while(1)
            {
                        char c[HTTP_BLOCK_SIZE];
            int bytes = esp_http_client_read(_http, c, HTTP_BLOCK_SIZE);
            if ( bytes < HTTP_BLOCK_SIZE )
                    break;
            }

            // esp_err_t err;
            // int *len = 0;
            // err = esp_http_client_flush_response(_http, len);
            // if(err != ESP_OK)
            //     return false;
        }

        uint32_t delta = pos - _position;

        bool op = processRedirectsAndOpen(pos);

        //Debug_printv("SEEK in HTTPMStream %s: range request RC=%d", url.c_str(), lastRC);

        if(!op)
            return false;

        if ( delta > HTTP_BLOCK_SIZE )
        {
            // flush the rest
            //Debug_printv("_position[%lu] pos[%lu] available[%lu]", _position, pos, available());

            int rc = 0;
            do {
                char c;
                rc = esp_http_client_read(_http, &c, 1);
                if(rc == -1)
                    return false;
            } while(rc);

            // esp_err_t err;
            // int *len = 0;
            // err = esp_http_client_flush_response(_http, len);
            // if(err != ESP_OK)
            //     return false;

            if ( !processRedirectsAndOpen(pos) )
                return false;
        }

         // 200 = range not supported! according to https://developer.mozilla.org/en-US/docs/Web/HTTP/Range_requests
        if( lastRC == 206 )
        {
            //Debug_printv("Seek successful");
            _position = pos;
            return true;
        }
    }

    if ( lastMethod == HTTP_METHOD_GET ) 
    {
        //Debug_printv("Server doesn't support resume, reading from start and discarding");
        // server doesn't support resume, so...
        if( pos < _position || pos == 0 ) {
            // skipping backward let's simply reopen the stream...
            close();
            init();

            if ( !open(url, lastMethod) )
                return false;

            // and read pos bytes - requires some buffer
            for(int i = 0; i<pos; i++) {
                char c;
                int rc = esp_http_client_read(_http, &c, 1);
                if(rc == -1)
                    return false;
            }
        }
        else {
            auto delta = pos-_position;
            // skipping forward let's skip a proper amount of bytes - requires some buffer
            for(int i = 0; i<delta; i++) {
                char c;
                int rc = esp_http_client_read(_http, &c, 1);
                if(rc == -1)
                return false;
            }
        }

        _position = pos;
        Debug_printv("stream opened[%s]", url.c_str());

        return true;
    }
    else
        return false;
}

uint32_t MeatHttpClient::read(uint8_t* buf, uint32_t size) {

    if (!_is_open) {
        Debug_printv("Opening HTTP Stream!");
        processRedirectsAndOpen(0, size);
    }

    if (_is_open) {
        //Debug_printv("Reading HTTP Stream!");
        auto bytesRead = esp_http_client_read(_http, (char *)buf, size);
        
        if (bytesRead >= 0) {
            _position+=bytesRead;

            if (bytesRead < size)
                openAndFetchHeaders(lastMethod, _position);
        }
        if (bytesRead < 0)
            return 0;

        //Debug_printv("size[%d] bytesRead[%d] _position[%d]", size, bytesRead, _position);
        return bytesRead;
    }
    return 0;
};

uint32_t MeatHttpClient::write(const uint8_t* buf, uint32_t size) {
    if (!_is_open) 
    {
        if ( setHeader( (char *)buf ) )
            return size;
    }
    else
    {
        auto bytesWritten= esp_http_client_write(_http, (char *)buf, size );
        _position+=bytesWritten;
        return bytesWritten;        
    }
    return 0;
};

int MeatHttpClient::openAndFetchHeaders(esp_http_client_method_t method, uint32_t position, uint32_t size) {

    if ( url.size() < 5)
        return 0;

    // // If there's an active request, finish it cleanly before starting a new one
    // // This allows keep-alive to reuse the TCP connection
    // if (_http != nullptr && _is_open) {
    //     // Check if there's unread data and flush it efficiently
    //     if (!esp_http_client_is_complete_data_received(_http)) {
    //         // Flush remaining data in chunks to complete the request
    //         char discard_buffer[256];  // Stack allocated for thread safety
    //         int bytes_read;
            
    //         do {
    //             int chunk_size = sizeof(discard_buffer);
                
    //             bytes_read = esp_http_client_read(_http, discard_buffer, chunk_size);
    //         } while (bytes_read > 0);
    //     }
        
    //     // Don't call esp_http_client_close() - that breaks keep-alive
    //     _is_open = false;
    // }

    // Set URL and Method
    mstr::replaceAll(url, " ", "%20");
    //Debug_printv("method[%d] url[%s]", method, url.c_str());
    esp_http_client_set_url(_http, url.c_str());
    esp_http_client_set_method(_http, method);

    // Set Headers
    for (const auto& pair : headers) {
        Debug_printv("%s:%s", pair.first.c_str(), pair.second.c_str());
        std::cout << pair.first << ": " << pair.second << std::endl;
        esp_http_client_set_header(_http, pair.first.c_str(), pair.second.c_str());
    }

    // Set Range Header
    if ( method == HTTP_METHOD_GET )
    {
        char str[40];
        snprintf(str, sizeof str, "bytes=%lu-%lu", position, (position + size + 5));
        esp_http_client_set_header(_http, "Range", str);
        //Debug_printv("seeking range[%s] url[%s]", str, url.c_str());
    }

    // POST
    // const char *post_data = "{\"field1\":\"value1\"}";
    // esp_http_client_set_post_field(client, post_data, strlen(post_data));

    //Debug_printv("--- PRE OPEN");
    int status = 0;
    esp_err_t rc;

    rc = esp_http_client_open(_http, 0);
    if (rc == ESP_OK)
    {
        //Debug_printv("--- PRE FETCH HEADERS");

        int64_t lengthResp = esp_http_client_fetch_headers(_http);
        if(_size == -1 && lengthResp > 0) {
            // only if we aren't chunked!
            _size = lengthResp;
            _position = position;
        }

        status = esp_http_client_get_status_code(_http);
        //Debug_printv("after open rc[%d] status[%d]", rc, status);
        return status;

        //Debug_printv("--- PRE GET STATUS CODE");
    }

    Debug_printv("Connection failed...");
    return 0;
}

esp_err_t MeatHttpClient::_http_event_handler(esp_http_client_event_t *evt)
{
    MeatHttpClient* meatClient = (MeatHttpClient*)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR: // This event occurs when there are any errors during execution
            Debug_printv("HTTP_EVENT_ERROR");
            meatClient->_error = 1;
            break;

        case HTTP_EVENT_ON_CONNECTED: // Once the HTTP has been connected to the server, no data exchange has been performed
            // Debug_printv("HTTP_EVENT_ON_CONNECTED");
            break;

        case HTTP_EVENT_HEADER_SENT: // After sending all the headers to the server
            // Debug_printv("HTTP_EVENT_HEADER_SENT");
            break;

        case HTTP_EVENT_ON_HEADER: // Occurs when receiving each header sent from the server
            // Does this server support resume?
            // Accept-Ranges: bytes

            // if (mstr::equals("Accept-Ranges", evt->header_key, false))
            // {
            //     if(meatClient != nullptr) {
            //         meatClient->isFriendlySkipper = mstr::equals("bytes", evt->header_value,false);
            //         Debug_printv("Accept-Ranges: %s",evt->header_value);
            //     }
            // }
            if (mstr::equals("Content-Range", evt->header_key, false))
            {
                //Debug_printv("Content-Range: %s",evt->header_value);
                if(meatClient != nullptr) {
                    meatClient->isFriendlySkipper = true;
                    auto cr = util_tokenize(evt->header_value, '/');
                    if( cr.size() > 1 )
                        meatClient->_range_size = std::stoi(cr[1]);
                }
                //Debug_printv("size[%lu] isFriendlySkipper[%d]", meatClient->_range_size, meatClient->isFriendlySkipper);
            }
            // what can we do UTF8<->PETSCII on this stream?
            if (mstr::equals("Content-Type", evt->header_key, false))
            {
                std::string asString = evt->header_value;
                bool isText = mstr::isText(asString);

                if(meatClient != nullptr) {
                    meatClient->isText = isText;
                    //Debug_printv("* Content info present '%s', isText=%d!, type=%s", evt->header_value, isText, asString.c_str());
                }        
            }
            else if(mstr::equals("Last-Modified", evt->header_key, false))
            {
                // Last-Modified, value=Thu, 03 Dec 1992 08:37:20 - may be used to get file date
            }
            else if(mstr::equals("Content-Disposition", evt->header_key, false))
            {
                // Content-Disposition, value=attachment; filename*=UTF-8''GeckOS-c64.d64
                // we can set isText from real file extension, too!
                std::string value = evt->header_value;
                if ( mstr::contains( value, (char *)"index.prg" ) )
                {
                    //Debug_printv("HTTP Directory Listing [%s]", meatClient->url.c_str());
                    meatClient->m_isDirectory = true;
                }
            }
            else if(mstr::equals("Content-Length", evt->header_key, false))
            {
                //Debug_printv("* Content len present '%s'", evt->header_value);
                meatClient->_size = std::stoi(evt->header_value);
            }
            else if(mstr::equals("Location", evt->header_key, false))
            {
                Debug_printv("* This page redirects from '%s' to '%s'", meatClient->url.c_str(), evt->header_value);
                //if ( mstr::compare(evt->header_value, "*://*") )
                if ( mstr::contains(evt->header_value, (char *)"://") )
                {
                    //Debug_printv("match");
                    meatClient->url = evt->header_value;
                }
                else
                {
                    //Debug_printv("no match");
                    if ( mstr::startsWith(evt->header_value, (char *)"/") )
                    {
                        // Absolute path redirect
                        auto u = PeoplesUrlParser::parseURL( meatClient->url );
                        meatClient->url = u->root() + evt->header_value;
                    }
                    else
                    {
                        // Relative path redirect
                        meatClient->url += evt->header_value;
                    }
                }

                Debug_printv("new url '%s'", meatClient->url.c_str());
                meatClient->wasRedirected = true;
            }

            // Allow override in lambda
            meatClient->onHeader(evt->header_key, evt->header_value);

            break;

#if __cplusplus > 201703L
//#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(5, 3, 0)
        case HTTP_EVENT_REDIRECT:

            Debug_printv("* This page redirects from '%s' to '%s'", meatClient->url.c_str(), evt->header_value);
            if ( mstr::startsWith(evt->header_value, (char *)"http") )
            {
                //Debug_printv("match");
                meatClient->url = evt->header_value;
            }
            else
            {
                //Debug_printv("no match");
                meatClient->url += evt->header_value;                    
            }
            meatClient->wasRedirected = true;
            break;
#endif

        case HTTP_EVENT_ON_DATA: // Occurs multiple times when receiving body data from the server. MAY BE SKIPPED IF BODY IS EMPTY!
            //Debug_printv("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            {
                // int status = esp_http_client_get_status_code(meatClient->_http);

                // if ((status == HttpStatus_Found || status == HttpStatus_MovedPermanently || status == 303) /*&& client->_redirect_count < (client->_max_redirects - 1)*/)
                // {
                //     //Debug_printv("HTTP_EVENT_ON_DATA: Redirect response body, ignoring");
                // }
                // else {
                //     //Debug_printv("HTTP_EVENT_ON_DATA: Got response body");
                // }


                if (esp_http_client_is_chunked_response(evt->client)) {
                    int len;
                    esp_http_client_get_chunk_length(evt->client, &len);
                    meatClient->_size = len;
                    //Debug_printv("HTTP_EVENT_ON_DATA: Got chunked response, chunklen=%d, contentlen[%d]", len, meatClient->_size);
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH: 
            // Occurs when finish a HTTP session
            // This may get called more than once if esp_http_client decides to retry in order to handle a redirect or auth response
            //Debug_printv("HTTP_EVENT_ON_FINISH %u\r\n", uxTaskGetStackHighWaterMark(nullptr));
            // Keep track of how many times we "finish" reading a response from the server
            //Debug_printv("HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED: // The connection has been disconnected
            //Debug_printv("HTTP_EVENT_DISCONNECTED");
            //meatClient->m_bytesAvailable = 0;
            break;
    }
    return ESP_OK;
}
