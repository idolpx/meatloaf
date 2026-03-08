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
    : MSession((port == 443 ? "https://" : "http://") + host + ":" + std::to_string(port), host, port)
{
    Debug_printv("HTTPMSession created for %s:%d", host.c_str(), port);

    // MeatHttpClient handles its own initialization and configuration
    client = std::make_shared<MeatHttpClient>();

    keep_alive_interval = 0; // Disable keep-alive timer — HTTP sessions reused for same host:port
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

    connected = true;
    updateActivity();
    return true;
}

void HTTPMSession::disconnect() {
    if (!connected) {
        return;
    }

    Debug_printv("HTTPMSession disconnecting from %s:%d", host.c_str(), port);

    connected = false;
}

bool HTTPMSession::keep_alive() {
    if (!connected) {
        return false;
    }

    return true;
}

/********************************************************
 * MFile implementations
 ********************************************************/

std::shared_ptr<MeatHttpClient> HTTPMFile::getClient() {
    auto client = _session->client;
    if(!_headersFetched) {
        client->HEAD(url);
        if (client->wasRedirected)
            resetURL(client->url);
        _headersFetched = true;
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
    std::string requestUrl = buildRequestUrl();
    Debug_printv("Request URL: %s", requestUrl.c_str());
    auto istream = openStreamWithCache(
        requestUrl,
        mode,
        [](const std::string& openUrl, std::ios_base::openmode openMode) -> std::shared_ptr<MStream> {
            std::string mutableUrl = openUrl;
            auto stream = std::make_shared<HTTPMStream>(mutableUrl, openMode);
            stream->open(openMode);
            return stream;
        });

    if (istream != nullptr && mstr::startsWith(istream->url, "http")) {
        resetURL(istream->url);
    }

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
    // if (is_dir > -1) return is_dir;
    // if(getClient()->m_isDirectory)
    //     return true;

    // if(getClient()->m_isWebDAV) {
    //     // try webdav PROPFIND to get a listing
    //     return true;
    // }
    // else
        // otherwise return false
        return false;
}

time_t HTTPMFile::getLastWrite() {
    if(getClient()->m_isWebDAV) {
        return 0;
    }
    else
    // take from webdav PROPFIND or fallback to Last-Modified
        return 0; 
}

time_t HTTPMFile::getCreationTime() {
    if(getClient()->m_isWebDAV) {
        return 0;
    }
    else
    // take from webdav PROPFIND or fallback to Last-Modified
        return 0; 
}

bool HTTPMFile::exists() {
    return getClient()->_exists;
    return true;
}

bool HTTPMFile::remove() {
    if(getClient()->m_isWebDAV) {
        // PROPPATCH allows deletion
        return false;
    }
    return false;
}

bool HTTPMFile::mkDir() {
    if(getClient()->m_isWebDAV) {
        // MKCOL creates dir
        return false;
    }
    return false;
}

bool HTTPMFile::rewindDirectory() {
    if(getClient()->m_isWebDAV) { 
        // we can try if this is webdav, then
        // PROPFIND allows listing dir
        return false;
    }
    return false; 
};

MFile* HTTPMFile::getNextFileInDir() { 
    Debug_printv("");
    if(getClient()->m_isWebDAV) {
        // we can try if this is webdav, then
        // PROPFIND allows listing dir
        return nullptr;
    }
    return nullptr; 
};


bool HTTPMFile::isText() {
    return getClient()->isText;
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

    auto& client = *_session->client;
    bool r = false;

    if(mode == (std::ios_base::out | std::ios_base::app))
        r = client.PUT(url);
    else if(mode == std::ios_base::out)
        r = client.POST(url);
    else
        r = client.GET(url);

    _size = ( client._range_size > 0) ? client._range_size : client._size;
    if ( client.wasRedirected )
        url = client.url;

    //Debug_printv("r[%d] size[%d] url[%s] hurl[%s]", r, _size, url.c_str(), client.url.c_str());

    if (r && _session) {
        _session->acquireIO();
    }

    return r;
}

void HTTPMStream::close() {
    //Debug_printv("CLOSE called explicitly on this HTTP stream!");
    if (_session) {
        _session->releaseIO();
    }
    // client stays alive in session for reuse
}

bool HTTPMStream::seek(uint32_t pos) {
    if ( !_session->client->_is_open )
    {
        Debug_printv("error");
        _error = 1;
        return false;
    }

    return _session->client->seek(pos);
}

uint32_t HTTPMStream::read(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    if ( size > 0 )
    {
        if ( size > available() )
            size = available();

        bytesRead = _session->client->read(buf, size);
        _position += bytesRead;
        _error = _session->client->_error;
    }

    return bytesRead;
};

uint32_t HTTPMStream::write(const uint8_t *buf, uint32_t size) {
    uint32_t bytesWritten = _session->client->write(buf, size);
    _position += bytesWritten;
    return bytesWritten;
}


bool HTTPMStream::isOpen() {
    return _session && _session->client && _session->client->_is_open;
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
    // Reset state for new file operation — MeatHttpClient is shared across files on the same host:port
    isFriendlySkipper = false;
    _size = 0;
    _range_size = 0;
    // Reinitialize the handle so each new file access starts with a clean raw_data buffer.
    // esp_http_client_close() alone does NOT reset raw_data/orig_raw_data — only cleanup() does.
    // If the previous range response left unread body bytes in raw_data (e.g., partial read before
    // a new open()), the next fetch_headers() triggers http_on_body which asserts raw_data == orig_raw_data.
    init();

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

        // openAndFetchHeaders returns 0 when esp_http_client_open() fails (connection error).
        // Without this check, processRedirectsAndOpen incorrectly sets _is_open = true below.
        if (lastRC == 0) {
            Debug_printv("connection failed");
            return false;
        }

        if (lastRC >= 300 && lastRC <= 399) {
            // Re-initialize the handle before following the redirect. The 3xx response body
            // may have been cached in raw_data; a fresh handle ensures http_on_body won't
            // assert raw_data == orig_raw_data when the redirect target's body arrives.
            // The Location header handler has already updated `url` with the redirect target.
            init();
        }
    } while (lastRC >= 300 && lastRC <= 399);

    if (lastRC == 206)
    {
        isFriendlySkipper = true;
    }
    else if (lastRC == 520)
    {
        // Unknown Error from Cloudflare, often happens when trying to do range requests on servers that don't support them, so we can try to recover by doing a normal GET and skipping the bytes ourselves
        Debug_printv("Received 520, trying to recover by doing a normal GET");
    }
    else if (lastRC > 399 || _error != 0)
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
            // Drain remaining bytes from the current range response.
            // Read in HTTP_BLOCK_SIZE chunks to minimize esp_http_client_read call overhead.
            while(1)
            {
                char c[HTTP_BLOCK_SIZE];
                int bytes = esp_http_client_read(_http, c, HTTP_BLOCK_SIZE);
                if ( bytes < HTTP_BLOCK_SIZE )
                    break;
            }
        }

        // Make a single range request directly to the target position.
        // After this call we are positioned at exactly pos and ready to read.
        if ( !processRedirectsAndOpen(pos) )
            return false;

        // 200 = range not supported per https://developer.mozilla.org/en-US/docs/Web/HTTP/Range_requests
        if( lastRC == 206 )
        {
            _position = pos;
            return true;
        }
        // Server returned 200 (doesn't support ranges) — fall through to sequential seek.
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

    // Close any previous active request before starting a new one.
    // For GET/PUT/POST: esp_http_client_close() is sufficient because the response buffer
    // (raw_data/raw_len) is cleaned up by esp_http_client_read() during normal body reads.
    // For HEAD: see below — we use init() instead of close() to handle this case.
    if (_http != nullptr && _is_open) {
        esp_http_client_close(_http);
        _is_open = false;
    }

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
        snprintf(str, sizeof str, "bytes=%" PRIu32 "-%" PRIu32, position, (position + size + 5));
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
        // _size is set by the Content-Length event handler; lengthResp here is
        // redundant for normal responses, but covers chunked encoding (no Content-Length header).
        if (_size == 0 && lengthResp > 0) {
            _size = (uint32_t)lengthResp;
            _position = position;
        }

        status = esp_http_client_get_status_code(_http);
        //Debug_printv("after open rc[%d] status[%d]", rc, status);

        // For HEAD: fully reinitialize the client after fetching headers.
        // esp_http_client_close() alone does NOT reset raw_data/orig_raw_data in the
        // response buffer. If body data was cached during fetch_headers (e.g., from an
        // internally-followed 3xx redirect response body), it persists across close().
        // The next request's http_on_body then asserts raw_data == orig_raw_data → CRASH.
        // init() calls esp_http_client_cleanup() which frees orig_raw_data and zeros the
        // buffer pointers, giving the next request a fully clean slate.
        // _is_open stays false for HEAD — the subsequent GET opens a fresh connection.
        if (method == HTTP_METHOD_HEAD) {
            init();
        } else {
            _is_open = true;
        }

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
            if ( mstr::equals("DAV", evt->header_key, false) )
            {
                if ( mstr::contains(evt->header_value, (char *)"1") )
                {
                    Debug_printv("WebDAV support detected!");
                    if(meatClient != nullptr) {
                        meatClient->m_isWebDAV = true;
                    }
                }
            }
            else if (mstr::equals("Content-Range", evt->header_key, false))
            {
                //Debug_printv("Content-Range: %s",evt->header_value);
                if(meatClient != nullptr) {
                    meatClient->isFriendlySkipper = true;
                    auto cr = util_tokenize(evt->header_value, '/');
                    if( cr.size() > 1 )
                        meatClient->_range_size = std::stoul(cr[1]);
                }
                //Debug_printv("size[%lu] isFriendlySkipper[%d]", meatClient->_range_size, meatClient->isFriendlySkipper);
            }
            // what can we do UTF8<->PETSCII on this stream?
            else if (mstr::equals("Content-Type", evt->header_key, false))
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
                meatClient->_size = atol(evt->header_value);
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
