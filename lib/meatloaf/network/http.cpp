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
#include <esp_crt_bundle.h>
#include <algorithm>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "meatloaf.h"
#include "meat_session.h"

#include "../../../include/debug.h"
//#include "../../../include/global_defines.h"

/********************************************************
 * HTTPRequestContext implementation
 ********************************************************/

void HTTPRequestContext::setMethod(const std::string& m) {
    method = m;
    mstr::toUpper(method);
}

void HTTPRequestContext::setHeader(const std::string& name, const std::string& value) {
    std::string key = name;
    mstr::toLower(key);
    headers[key] = {value};  // replaces any existing values for this key
}

void HTTPRequestContext::appendHeader(const std::string& name, const std::string& value) {
    std::string key = name;
    mstr::toLower(key);
    headers[key].push_back(value);
}

void HTTPRequestContext::setBody(const std::string& b) {
    body = b;
}

void HTTPRequestContext::appendBody(const std::string& b) {
    body += b;
}

void HTTPRequestContext::clear() {
    method = "GET";
    headers.clear();
    body.clear();
    responseHeaders.clear();
    responseStatus = 0;
    responseConsumed = false;
}

bool HTTPRequestContext::sendRequest(std::shared_ptr<HTTPMSession> session) {
    if (!session || !session->client) {
        responseStatus = -1;
        return false;
    }

    auto& client = *session->client;

    // Reset response header buffer and status
    responseHeaders.clear();
    responseStatus = 0;

    // Set up onHeader callback to capture response headers
    // The callback is called during HTTP_EVENT_ON_HEADER with key and value
    client.setOnHeader([this](char* key, char* value) -> int {
        if (key && value) {
            std::string headerLine = std::string(key) + ": " + std::string(value);
            responseHeaders.push_back(headerLine);
            Debug_printv("Captured response header: %s", headerLine.c_str());
        }
        return 0;
    });

    // Apply request headers to MeatHttpClient's header map
    for (const auto& [key, values] : headers) {
        for (const auto& val : values) {
            std::string headerLine = key + ":" + val;
            client.setHeader(headerLine);
        }
    }

    // Set body if present
    if (!body.empty()) {
        client.postBuffer.clear();
        client.postBuffer.insert(client.postBuffer.end(), body.begin(), body.end());
    }

    // Determine method and send
    bool result = false;
    if (method == "GET") {
        result = client.GET(client.url);
    } else if (method == "POST") {
        result = client.POST(client.url);
        // For POST, close() sends the buffered body and captures response
        if (result && !client.postBuffer.empty()) {
            client.close();
            // After close, preservedPostResponse holds the body
            responseStatus = client.lastRC;
        }
    } else if (method == "PUT") {
        result = client.PUT(client.url);
        if (result && !client.postBuffer.empty()) {
            client.close();
            responseStatus = client.lastRC;
        }
    } else if (method == "HEAD") {
        result = client.HEAD(client.url);
    } else {
        Debug_printv("Unsupported method: %s", method.c_str());
        responseStatus = -1;
        return false;
    }

    // Capture response status from the perform result
    if (responseStatus == 0) {
        responseStatus = client.lastRC;
    }

    return result;
}

std::string HTTPRequestContext::popResponseHeader() {
    if (!_headerRemainder.empty()) {
        std::string r = _headerRemainder;
        _headerRemainder.clear();
        return r;
    }
    if (responseHeaders.empty()) return {};
    std::string line = responseHeaders.front() + "\r\n";
    responseHeaders.erase(responseHeaders.begin());
    return line;
}

/********************************************************
 * Full-mode command handling
 ********************************************************/

bool HTTPMStream::handleCommand(const std::string& cmd) {
    Debug_printv("handleCommand: %s", cmd.c_str());

    // Trim trailing whitespace/newlines that C64 PRINT# may add
    std::string c = cmd;
    mstr::trim(c);
    mstr::toLower(c);

    if (c.empty()) return true;  // blank line is harmless

    // Activate full mode on first command
    if (fullMode == FullModeState::SIMPLE) {
        fullMode = FullModeState::BUILDING_REQUEST;
    }

    // "r-h" or "r-b" — switch response read mode
    if (c.size() >= 3 && c[0] == 'r' && c[1] == '-') {
        switch (c[2]) {
            case 'h':
                fullMode = FullModeState::RESPONSE_HEADERS;
                return true;
            case 'b':
                fullMode = FullModeState::RESPONSE_BODY;
                return true;
        }
        return false;
    }

    // "s" — send the request
    if (c == "s") {
        fullMode = FullModeState::RESPONSE_HEADERS;
        if (_session) {
            ctx.sendRequest(_session);
        }
        return true;
    }

    // "c" — clear context
    if (c == "c") {
        ctx.clear();
        fullMode = FullModeState::BUILDING_REQUEST;
        _statusRequested = false;
        return true;
    }

    // "status" — request HTTP status code
    if (c == "status") {
        _statusRequested = true;
        return true;
    }

    // Single-letter commands with arguments: "m <method>", "b <body>"
    if (c.size() >= 2 && c[1] == ' ') {
        char prefix = c[0];
        std::string arg = c.substr(2);
        mstr::trim(arg);

        switch (prefix) {
            case 'm':
                ctx.setMethod(arg);
                return true;
            case 'b':
                ctx.setBody(arg);
                return true;
            default:
                break;
        }
    }

    // "h <name>: <value>" and "h+ <name>: <value>" — set/append header
    if (c.size() >= 2 && c[0] == 'h') {
        bool append = (c.size() >= 2 && c[1] == '+');
        size_t start = append ? 2 : 1;
        if (c.size() > start && c[start] == ' ') start++;
        std::string rest = c.substr(start);
        auto colonPos = rest.find(':');
        if (colonPos != std::string::npos) {
            std::string name = rest.substr(0, colonPos);
            std::string value = rest.substr(colonPos + 1);
            mstr::trim(name);
            mstr::trim(value);
            if (append) {
                ctx.appendHeader(name, value);
            } else {
                ctx.setHeader(name, value);
            }
            return true;
        }
    }

    // "b+" — append body (no space required: "b+more text" or "b+ more text")
    if (c.size() >= 2 && c[0] == 'b' && c[1] == '+') {
        std::string rest = c.substr(2);
        if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
        ctx.appendBody(rest);
        return true;
    }

    Debug_printv("Unrecognized command in full mode: %s", c.c_str());
    return false;  // unknown command, silently ignored
}

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

    // Apply Content-Disposition filename if the server provided one
    if (_session && _session->client && !_session->client->contentDispositionFilename.empty()) {
        name = _session->client->contentDispositionFilename;
        Debug_printv("filename from Content-Disposition: %s", name.c_str());
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
    // if (isOpen()) {
    //     close();
    // }

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
    Debug_printv("HTTPMStream::close called, mode=%d, fullMode=%d", mode, (int)fullMode);

    // In full mode, don't trigger automatic POST send — that's for simple mode only
    if (fullMode != FullModeState::SIMPLE) {
        Debug_printv("Full mode close (state=%d), skipping automatic POST send", (int)fullMode);
        if (_session) {
            _session->releaseIO();
        }
        fullMode = FullModeState::SIMPLE;
        ctx.clear();
        return;
    }

    // Simple mode: original behavior
    bool isWriteMode = (mode & 0x10) || (mode == std::ios_base::out)
        || (mode == (std::ios_base::in | std::ios_base::out));
    if (isWriteMode && _session && _session->client) {
        Debug_printv("Closing write-mode stream, sending POST request");
        auto client = _session->client.get();
        client->close();
    }
    if (_session) {
        _session->releaseIO();
    }
}

bool HTTPMStream::seek(uint32_t pos) {
    if ( !_session->client->_is_open )
    {
        if ( !_session->client->reopen() ) {
            _error = 1;
            return false;
        }
    }

    return _session->client->seek(pos);
}

uint32_t HTTPMStream::read(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    if ( size > 0 )
    {
        // Full mode: handle "status" command — return status code string
        if (_statusRequested) {
            _statusRequested = false;
            std::string statusStr = std::to_string(ctx.responseStatus);
            uint32_t copyLen = std::min((uint32_t)statusStr.size(), size);
            memcpy(buf, statusStr.data(), copyLen);
            Debug_printv("Returning status: %s", statusStr.c_str());
            return copyLen;
        }

        // Full mode: RESPONSE_HEADERS — serve buffered header lines
        if (fullMode == FullModeState::RESPONSE_HEADERS) {
            std::string headerLine;
            if (ctx.hasMoreResponseHeaders()) {
                headerLine = ctx.popResponseHeader();
            } else {
                // End-of-headers marker: empty CRLF line
                headerLine = "\r\n";
                // Transition to body mode automatically
                fullMode = FullModeState::RESPONSE_BODY;
            }
            uint32_t copyLen = std::min((uint32_t)headerLine.size(), size);
            memcpy(buf, headerLine.data(), copyLen);

            // Buffer any remainder for the next read
            if (copyLen < headerLine.size()) {
                ctx._headerRemainder = headerLine.substr(copyLen);
            }

            _error = 0;
            Debug_printv("Serving header line: %s", headerLine.c_str());
            return copyLen;
        }

        // Full mode: RESPONSE_BODY — read from MeatHttpClient
        if (fullMode == FullModeState::RESPONSE_BODY) {
            if (_session && _session->client) {
                bytesRead = _session->client->read(buf, size);
                _position += bytesRead;
                _error = _session->client->_error;
                if (bytesRead == 0 && _session->client->complete()) {
                    ctx.responseConsumed = true;
                }
                return bytesRead;
            }
            return 0;
        }

        // Simple mode: original behavior (write-mode POST handling)
        bool isWriteMode = (mode & 0x10) || (mode == std::ios_base::out);
        if (isWriteMode && _session && _session->client) {
            Debug_printv("Switching from write mode to read mode");
            auto client = _session->client.get();

            bool hasResponse = (!client->postBuffer.empty()) ||
                              (!client->postResponse.empty()) ||
                              (!client->preservedPostResponse.empty());

            if (!hasResponse) {
                Debug_printv("POST already sent, reading from existing response");
            } else if (!client->postBuffer.empty()) {
                Debug_printv("Sending POST request...");
                client->close();
            }

            mode = std::ios_base::in;
            _position = 0;
        }

        if ( size > available() )
            size = available();

        if ( size > 0 )
        {
            bytesRead = _session->client->read(buf, size);
            _position += bytesRead;
            _error = _session->client->_error;
        }
    }

    return bytesRead;
};

uint32_t HTTPMStream::write(const uint8_t *buf, uint32_t size) {
    Debug_printv("HTTPMStream::write called, size=%u, fullMode=%d, client=%p",
        size, (int)fullMode, _session ? _session->client.get() : nullptr);

    // In full mode, all writes are commands
    if (fullMode != FullModeState::SIMPLE) {
        std::string data(reinterpret_cast<const char*>(buf), size);
        handleCommand(data);
        return size;  // always consume the data (even if unrecognized)
    }

    // Simple mode: check if incoming data looks like a command
    if (size > 0) {
        char first = (char)buf[0];
        if (first == 'm' || first == 'M' ||
            first == 'h' || first == 'H' ||
            first == 'b' || first == 'B' ||
            first == 's' || first == 'S' ||
            first == 'c' || first == 'C' ||
            first == 'k' || first == 'K' ||
            (size >= 2 && (first == 'r' || first == 'R') && (buf[1] == '-'))) {
            // First command detected — enter full mode
            fullMode = FullModeState::BUILDING_REQUEST;
            std::string cmd(reinterpret_cast<const char*>(buf), size);
            handleCommand(cmd);
            return size;
        }
    }

    // Simple mode: pass through to MeatHttpClient (original behavior — POST/PUT body buffering)
    if (_session && _session->client) {
        uint32_t bytesWritten = _session->client->write(buf, size);
        _position += bytesWritten;
        return bytesWritten;
    }
    return 0;
}


bool HTTPMStream::isOpen() {
    return _session && _session->client && _session->client->_is_open;
};


/********************************************************
 * Meat HTTP client impls
 ********************************************************/
bool MeatHttpClient::GET(std::string dstUrl) {
    Debug_printv("GET url[%s]", dstUrl.c_str());
    bool result = open(dstUrl, HTTP_METHOD_GET);
    //Debug_printv("GET result: %d, _is_open=%d", result, _is_open);
    return result;
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
    //Debug_printv("open called, url=%s, method=%d", dstUrl.c_str(), meth);
    url = dstUrl;
    lastMethod = meth;
    _error = 0;
    // Reset state for new file operation — MeatHttpClient is shared across files on the same host:port
    isFriendlySkipper = false;
    _size = 0;
    _range_size = 0;

    // Save POST response data before init() clears it
    // We want to return POST response data on subsequent GET operations
    uint32_t savedSize = 0;
    std::vector<uint8_t> savedResponse;
    //Debug_printv("open: postResponse.size=%u, preservedPostResponse.size=%u, preservedPostResponseSize=%u, method=%d",
    //    (uint32_t)postResponse.size(), (uint32_t)preservedPostResponse.size(), preservedPostResponseSize, meth);
    // Check both postResponse and preservedPostResponse
    if (!preservedPostResponse.empty() && meth == HTTP_METHOD_GET) {
        savedResponse = std::move(preservedPostResponse);
        savedSize = preservedPostResponseSize;
        preservedPostResponseSize = 0;  // Reset after use
        Debug_printv("Preserving POST response: %u bytes", savedSize);
    } else if (!postResponse.empty() && meth == HTTP_METHOD_GET) {
        savedResponse = std::move(postResponse);
        savedSize = (uint32_t)savedResponse.size();
        Debug_printv("Preserving POST response from postResponse: %u bytes", savedSize);
    }

    // Always reset the HTTP client handle before a new request.
    // After HEAD, _is_open is false (HEAD doesn't set it), so the conditional check
    // would skip init() for the subsequent GET — leaving the handle with stale response
    // buffer state (raw_data != orig_raw_data) that causes esp_http_client_read() to block.
    init();

    // If we had a POST response saved, restore it for this GET
    if (!savedResponse.empty()) {
        postResponse = std::move(savedResponse);
        _is_open = true;
        _size = savedSize;
        _position = 0;
        Debug_printv("Restored POST response: %u bytes", _size);
        return true;
    }

    //Debug_printv("open: calling processRedirectsAndOpen");
    bool result = processRedirectsAndOpen(0);
    //Debug_printv("open: processRedirectsAndOpen returned %d, _is_open=%d", result, _is_open);
    return result;
};

bool MeatHttpClient::reopen() {
    return open(url, lastMethod);
}

bool MeatHttpClient::processRedirectsAndOpen(uint32_t position, uint32_t size) {
    //Debug_printv("processRedirectsAndOpen: position=%u, size=%u", position, size);
    wasRedirected = false;
    m_isDirectory = false;

    uint8_t redirects = 0;
    uint8_t connectRetries = 0;
    do {
        if (redirects++ > 10) {
            Debug_printv("too many redirects");
            return false;
        }

        //Debug_printv("opening url[%s] from position:%lu", url.c_str(), position);
        lastRC = openAndFetchHeaders(lastMethod, position, size);

        // openAndFetchHeaders returns 0 when esp_http_client_open() fails (connection error).
        // EAI_AGAIN (202) means the DNS query is still in flight — retry a few times with a
        // short delay to let the resolver complete before giving up.
        if (lastRC == 0) {
            if (connectRetries++ < 3) {
                Debug_printv("connection failed, retrying (%d/3)...", connectRetries);
                vTaskDelay(pdMS_TO_TICKS(500));
                init(); // reinitialize the client handle before retrying
                redirects--; // don't count this as a redirect
                continue;
            }
            Debug_printv("connection failed after retries");
            return false;
        }
        connectRetries = 0; // reset on success

        if (lastRC >= 300 && lastRC <= 399) {
            // Location header handler already updated `url` to the redirect target.
            // The next openAndFetchHeaders() call will drain+close the 3xx response
            // body before opening the new connection — no cleanup() needed here.
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
        // Only set _exists=true for successful responses (2xx)
        // For 404 and other errors, the file doesn't exist
        _exists = false;
        // Clear any pending POST body data
        postBuffer.clear();

        // Reset client on error
        init();

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
    //Debug_printv("close called, _http=%p, _is_open=%d, postBuffer.size=%u, postResponse.size=%u",
    //    _http, _is_open, (uint32_t)postBuffer.size(), (uint32_t)postResponse.size());
    if(_http != nullptr) {
        if ( _is_open ) {
            // For POST/PUT, we need to send the buffered body data
            if (!postBuffer.empty()) {
                Debug_printv("Sending POST body (%u bytes) from buffer", (uint32_t)postBuffer.size());
                // Set the post field data
                esp_http_client_set_post_field(_http, (char*)postBuffer.data(), postBuffer.size());
                // Complete the request (sends body and receives response)
                // Response body data is captured via HTTP_EVENT_ON_DATA
                int performResult = esp_http_client_perform(_http);
                Debug_printv("esp_http_client_perform result: %d", performResult);
                Debug_printv("POST response captured via events: %u bytes", (uint32_t)postResponse.size());

                // Set _size from the captured response
                _size = (uint32_t)postResponse.size();
                postBuffer.clear();
            }
            esp_http_client_close(_http);
        }
        esp_http_client_cleanup(_http);
        Debug_printv("HTTP Close and Cleanup");
        _http = nullptr;
    }
    // ALWAYS preserve postResponse so it survives subsequent operations, even if called multiple times
    if (!postResponse.empty()) {
        uint32_t responseSize = (uint32_t)postResponse.size();
        preservedPostResponse = std::move(postResponse);
        // Also preserve _size for the next operation
        if (preservedPostResponseSize == 0) {
            preservedPostResponseSize = _size;
        }
        Debug_printv("PRESERVED: responseSize=%u, preservedPostResponse.size=%u, preservedPostResponseSize=%u",
            responseSize, (uint32_t)preservedPostResponse.size(), preservedPostResponseSize);
    } else {
        Debug_printv("NOT PRESERVED: postResponse is empty");
    }
    // Note: _is_open stays true if we have postResponse to read
    // It will be set to false when postResponse is exhausted
}

void MeatHttpClient::setOnHeader(const std::function<int(char*, char*)> &lambda) {
    onHeader = lambda;
}

bool MeatHttpClient::seek(uint32_t pos) {

    if(isFriendlySkipper) {

        if (_is_open) {
            // Drain remaining bytes from the current range response.
            flush(0);
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
            if ( !reopen() )
                return false;

            // skip pos bytes in HTTP_BLOCK_SIZE chunks to avoid per-byte UART contention
            uint32_t remaining = pos;
            flush ( remaining );
        }
        else {
            auto delta = pos - _position;
            // skip forward in HTTP_BLOCK_SIZE chunks to avoid per-byte UART contention
            uint32_t remaining = delta;
        }

        _position = pos;
        Debug_printv("stream opened[%s]", url.c_str());

        return true;
    }
    else
        return false;
}

bool MeatHttpClient::flush(uint32_t numBytes) {
    Debug_printv("flush called, numBytes=%u, _http=%p", numBytes, _http);
    if (_http == nullptr) {
        Debug_printv("flush: _http is null");
        return false;
    }

    // For POST/PUT, the buffered body must be sent before draining the response.
    // Let close() handle sending the POST body - flush() only drains the response here.
    if (numBytes == 0) {
        // Drain the remaining response body so the connection is clean
        int bytes = 0;
        esp_http_client_flush_response(_http, &bytes);
        Debug_printv("Flushed %d bytes to complete response", bytes);
        return true;
    }

    uint32_t flushed = 0;
    while (numBytes > 0) {
        char buf[HTTP_BLOCK_SIZE];
        uint32_t toRead = (numBytes < HTTP_BLOCK_SIZE) ? numBytes : HTTP_BLOCK_SIZE;
        int rc = esp_http_client_read(_http, buf, (int)toRead);
        if (rc < 0) {
            Debug_printv("flush: read error %d", rc);
            return false;
        }

        numBytes -= (uint32_t)rc;
        flushed += (uint32_t)rc;
    }
    return true;
}

uint32_t MeatHttpClient::read(uint8_t* buf, uint32_t size) {
    // Check if we have a POST response buffer to read from
    if (!postResponse.empty()) {
        uint32_t available = (uint32_t)postResponse.size() - _position;
        if (available == 0) {
            // All response data consumed, mark as complete
            _is_open = false;
            postResponse.clear();
            Debug_printv("POST response fully consumed");
            return 0;
        }
        uint32_t toRead = (size < available) ? size : available;
        memcpy(buf, postResponse.data() + _position, toRead);
        _position += toRead;
        Debug_printv("Read %u bytes from POST response buffer (pos=%u/%u)", toRead, _position, (uint32_t)postResponse.size());
        return toRead;
    }

    if (!_is_open) {
        Debug_printv("Opening HTTP Stream!");
        processRedirectsAndOpen(0, size);
    }

    if (_is_open) {
        //Debug_printv("Reading HTTP Stream!");
        auto bytesRead = esp_http_client_read(_http, (char *)buf, size);

        if (bytesRead >= 0) {
            _position+=bytesRead;

            // Only restart the request (for range-based paging) when:
            // - We got a partial read (bytesRead > 0 but < size), AND
            // - The response is NOT chunked (chunked has no fixed size; 0-byte
            //   read means EOF, not "range exhausted"), AND
            // - We haven't already consumed all expected bytes (guard against
            //   requesting a range past EOF, which causes a 416 response; the
            //   server may keep the keep-alive connection open without a
            //   Content-Length on the error body, causing esp_http_client_read()
            //   to block forever waiting for a body that never arrives).
            if (bytesRead > 0 && bytesRead < size && !esp_http_client_is_chunked_response(_http)) {
                uint32_t totalSize = (_range_size > 0) ? _range_size : 0;
                if (totalSize == 0 || _position < totalSize)
                    openAndFetchHeaders(lastMethod, _position);
            }
        }

        //Debug_printv("size[%d] bytesRead[%d] _position[%d]", size, bytesRead, _position);
        if (bytesRead < 0)
            return 0;

        return bytesRead;
    }
    return 0;
};

uint32_t MeatHttpClient::write(const uint8_t* buf, uint32_t size) {
    Debug_printv("MeatHttpClient::write called, _is_open=%d, size=%u", _is_open, size);
    if (!_is_open)
    {
        Debug_printv("Client not open, trying header parsing");
        if ( setHeader( (char *)buf ) )
            return size;
    }
    else
    {
        // For POST/PUT, buffer the data for sending later
        // We'll send it all at once when close() is called
        postBuffer.insert(postBuffer.end(), buf, buf + size);
        Debug_printv("Buffered %u bytes, total buffer size: %u", size, (uint32_t)postBuffer.size());
        _position += size;
        return size;
    }
    return 0;
};

int MeatHttpClient::openAndFetchHeaders(esp_http_client_method_t method, uint32_t position, uint32_t size) {

    //Debug_printv("openAndFetchHeaders: method=%d, position=%u, size=%u, url=%s", method, position, size, url.c_str());

    if ( url.size() < 5)
        return 0;

    // static esp_err_t esp_http_client_prepare(esp_http_client_handle_t client)
    // {
    // // Reset the response buffer before each new request to ensure raw_data == orig_raw_data.
    // esp_http_client_cached_buf_cleanup(client->response->buffer);
    // // Also unconditionally reset raw_len.
    // client->response->buffer->raw_len = 0;

    // Set URL and Method
    mstr::replaceAll(url, " ", "%20");
    //Debug_printv("method[%d] url[%s]", method, url.c_str());
    esp_http_client_set_url(_http, url.c_str());
    esp_http_client_set_method(_http, method);

    // Set Headers
    for (const auto& pair : headers) {
        Debug_printv("%s:%s", pair.first.c_str(), pair.second.c_str());
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
        // For GET/HEAD, fetch headers immediately
        if (method == HTTP_METHOD_GET || method == HTTP_METHOD_HEAD)
        {
            int64_t lengthResp = esp_http_client_fetch_headers(_http);
            if (_size == 0 && lengthResp > 0) {
                _size = (uint32_t)lengthResp;
                _position = position;
            }
            status = esp_http_client_get_status_code(_http);
            if (method != HTTP_METHOD_HEAD) {
                _is_open = true;
            }
            return status;
        }
        else
        {
            // For POST/PUT, openAndFetchHeaders returns without fetching response
            // This allows write() to send body data first
            _is_open = true;
            return 200;
        }
    }

    Debug_printv("Connection failed...");
    return 0;
}

// Parse filename from Content-Disposition header.
// Handles both RFC 5987 (filename*=UTF-8''name) and plain (filename="name" or filename=name).
// RFC 5987 form takes precedence when both are present.
static std::string parse_content_disposition_filename(const char *value)
{
    if (!value) return {};
    std::string v = value;
    std::string lower = v;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Try filename*= (RFC 5987) first: charset'language'percent-encoded-value
    auto star = lower.find("filename*=");
    if (star != std::string::npos) {
        std::string rest = v.substr(star + 10); // skip "filename*="
        // Skip optional charset'language' prefix (e.g. "UTF-8''")
        auto q1 = rest.find('\'');
        if (q1 != std::string::npos) {
            auto q2 = rest.find('\'', q1 + 1);
            if (q2 != std::string::npos)
                rest = rest.substr(q2 + 1);
        }
        mstr::trim(rest);
        if (!rest.empty() && rest.front() == '"') rest = rest.substr(1);
        auto end = rest.find_first_of(";\"\r\n");
        if (end != std::string::npos) rest = rest.substr(0, end);
        // Percent-decode
        std::string out;
        for (size_t i = 0; i < rest.size(); ) {
            if (rest[i] == '%' && i + 2 < rest.size()) {
                char hex[3] = { rest[i+1], rest[i+2], '\0' };
                out += (char)strtol(hex, nullptr, 16);
                i += 3;
            } else {
                out += rest[i++];
            }
        }
        if (!out.empty()) return out;
    }

    // Fallback: plain filename=
    auto pos = lower.find("filename=");
    if (pos == std::string::npos) return {};
    std::string fname = v.substr(pos + 9); // skip "filename="
    mstr::trim(fname);
    if (!fname.empty() && fname.front() == '"') {
        fname = fname.substr(1);
        auto close = fname.find('"');
        if (close != std::string::npos) fname = fname.substr(0, close);
    } else {
        auto end = fname.find_first_of(";\r\n");
        if (end != std::string::npos) fname = fname.substr(0, end);
        mstr::trim(fname);
    }
    return fname;
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
                std::string value = evt->header_value;
                if ( mstr::contains( value, (char *)"index.prg" ) )
                {
                    meatClient->m_isDirectory = true;
                }
                std::string fname = parse_content_disposition_filename(evt->header_value);
                if (!fname.empty()) {
                    meatClient->contentDispositionFilename = fname;
                    Debug_printv("Content-Disposition filename: %s", fname.c_str());
                }
            }
            else if(mstr::equals("Content-Length", evt->header_key, false))
            {
                //Debug_printv("* Content len present '%s'", evt->header_value);
                meatClient->_size = atol(evt->header_value);
            }
            else if(mstr::equals("Location", evt->header_key, false))
            {
                //Debug_printv("* This page redirects from '%s' to '%s'", meatClient->url.c_str(), evt->header_value);
                //if ( mstr::compare(evt->header_value, "*://*") )
                if ( mstr::contains(evt->header_value, (char *)"://") )
                {
                    //Debug_printv("match");
                    auto origParsed = PeoplesUrlParser::parseURL(meatClient->url);
                    auto newParsed = PeoplesUrlParser::parseURL(evt->header_value);
                    // Re-inject credentials if the redirect URL has none but the original did,
                    // and both URLs target the same host (e.g. redirect strips user:pass@host).
                    if (newParsed->user.empty() && !origParsed->user.empty() && newParsed->host == origParsed->host) {
                        newParsed->user = origParsed->user;
                        newParsed->password = origParsed->password;
                        meatClient->url = newParsed->rebuildUrl();
                    } else {
                        meatClient->url = evt->header_value;
                    }
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

                //Debug_printv("new url '%s'", meatClient->url.c_str());
                meatClient->wasRedirected = true;
            }

            // Allow override in lambda
            meatClient->onHeader(evt->header_key, evt->header_value);

            break;

#if __cplusplus > 201703L
//#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(5, 3, 0)
        case HTTP_EVENT_REDIRECT:

            Debug_printv("* This page redirects from '%s' to '%s'", meatClient->url.c_str(), evt->header_value);
            if ( mstr::startsWith(evt->header_value, (char *)"://") )
            {
                //Debug_printv("match");
                auto origParsed = PeoplesUrlParser::parseURL(meatClient->url);
                auto newParsed = PeoplesUrlParser::parseURL(evt->header_value);
                // Re-inject credentials if the redirect URL has none but the original did,
                // and both URLs target the same host (e.g. redirect strips user:pass@host).
                if (newParsed->user.empty() && !origParsed->user.empty() && newParsed->host == origParsed->host) {
                    newParsed->user = origParsed->user;
                    newParsed->password = origParsed->password;
                    meatClient->url = newParsed->rebuildUrl();
                } else {
                    meatClient->url = evt->header_value;
                }
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
            //Debug_printv("HTTP_EVENT_ON_DATA: len=%d", evt->data_len);
            if (evt->data_len > 0 && evt->data != nullptr) {
                // Append response data to postResponse buffer for POST responses
                if (meatClient->lastMethod == HTTP_METHOD_POST || meatClient->lastMethod == HTTP_METHOD_PUT) {
                    meatClient->postResponse.insert(
                        meatClient->postResponse.end(),
                        (uint8_t*)evt->data,
                        (uint8_t*)evt->data + evt->data_len
                    );
                    //Debug_printv("HTTP_EVENT_ON_DATA: saved %d bytes to postResponse (total %u)",
                    //    evt->data_len, (uint32_t)meatClient->postResponse.size());
                }
            }
            if (esp_http_client_is_chunked_response(evt->client)) {
                meatClient->_size += evt->data_len;
                //Debug_printv("HTTP_EVENT_ON_DATA: chunked, added %d, total _size=%lu", evt->data_len, meatClient->_size);
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

#include <fstream>
#include <sstream>
#include <sys/stat.h>


#include <esp_heap_caps.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static bool g_http_insecure = false;
static char *g_ca_cert = nullptr;
static size_t g_ca_cert_len = 0;
static bool g_ca_cert_loaded = false;

void http_set_insecure(bool v) { g_http_insecure = v; }

static void load_ca_cert_from_flash()
{
    if (g_ca_cert_loaded) return;
    g_ca_cert_loaded = true;
    const char *ca_path = "/.sys/cert.pem";
    struct stat st;
    if (stat(ca_path, &st) != 0 || st.st_size <= 0) return;
    int fd = open(ca_path, O_RDONLY);
    if (fd < 0) return;
    g_ca_cert_len = (size_t)st.st_size;
    g_ca_cert = (char *)heap_caps_malloc(g_ca_cert_len + 1, MALLOC_CAP_SPIRAM);
    if (!g_ca_cert) g_ca_cert = (char *)malloc(g_ca_cert_len + 1);
    if (g_ca_cert) {
        ssize_t n = read(fd, g_ca_cert, g_ca_cert_len);
        g_ca_cert[n > 0 ? (size_t)n : 0] = '\0';
        Debug_printv("CA cert loaded from %s (%d bytes)", ca_path, (int)g_ca_cert_len);
    }
    close(fd);
}

void MeatHttpClient::init() {
    // Clean up existing client if present to prevent handle leak
    if (_http != nullptr) {
        esp_http_client_cleanup(_http);
        _http = nullptr;
    }
    _is_open = false;

    esp_http_client_config_t config;
    memset(&config, 0, sizeof(config));
    // Use the actual request URL so esp_http_client_init allocates the correct transport
    // (TCP for http://, TCP+TLS for https://) without a later scheme-switch in set_url.
    // Fall back to plain HTTP when url is not yet set (constructor call).
    config.url = url.empty() ? "http://localhost/" : url.c_str();
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
    config.skip_cert_common_name_check = true;


    if (!g_http_insecure) {
        // Set CERT.PEM source here flash or bundled
        // load_ca_cert_from_flash();
        // if (g_ca_cert && g_ca_cert_len > 0) {
        //     config.cert_pem = g_ca_cert;
        // }
        // else 
        {
            config.crt_bundle_attach = esp_crt_bundle_attach;
        }
        config.skip_cert_common_name_check = false;
    }

    //Debug_printv("HTTP Init url[%s]", url.c_str());
    _http = esp_http_client_init(&config);
}
