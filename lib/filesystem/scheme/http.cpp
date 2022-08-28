#include "http.h"

/********************************************************
 * File impls
 ********************************************************/

MeatHttpClient* HttpFile::formHeader() {
    if(client == nullptr) {
        Debug_printv("Client was not present, creating");
        client = new MeatHttpClient();

        // let's just get the headers so we have some info
        Debug_printv("Client requesting head");
        client->HEAD(url);
    }
    return client;
}

bool HttpFile::isDirectory() {
    if(formHeader()->m_isWebDAV) {
        // try webdav PROPFIND to get a listing
        return false;
    }
    else
        // otherwise return false
        return false;
}

MStream* HttpFile::inputStream() {
    // has to return OPENED stream
    //Debug_printv("Input stream requested: [%s]", url.c_str());
    MStream* istream = new HttpIStream(url);
    istream->open();
    return istream;
}

MStream* HttpFile::outputStream() {
    // has to return OPENED stream
    MStream* ostream = new HttpIStream(url);
    return ostream;
}

MStream* HttpFile::createIStream(std::shared_ptr<MStream> is) {
    return is.get(); // DUMMY return value - we've overriden istreamfunction, so this one won't be used
}

time_t HttpFile::getLastWrite() {
    if(formHeader()->m_isWebDAV) {
        return 0;
    }
    else
    // take from webdav PROPFIND or fallback to Last-Modified
        return 0; 
}

time_t HttpFile::getCreationTime() {
    if(formHeader()->m_isWebDAV) {
        return 0;
    }
    else
    // take from webdav PROPFIND or fallback to Last-Modified
        return 0; 
}

bool HttpFile::exists() {
    return formHeader()->m_exists;
}

size_t HttpFile::size() {
    if(formHeader()->m_isWebDAV) {
        // take from webdav PROPFIND
        return 0;
    }
    else
        // fallback to what we had from the header
        return formHeader()->m_length;
}

bool HttpFile::remove() {
    if(formHeader()->m_isWebDAV) {
        // PROPPATCH allows deletion
        return false;
    }
    return false;
}

bool HttpFile::mkDir() {
    if(formHeader()->m_isWebDAV) {
        // MKCOL creates dir
        return false;
    }
    return false;
}

bool HttpFile::rewindDirectory() {
    if(formHeader()->m_isWebDAV) { 
        // we can try if this is webdav, then
        // PROPFIND allows listing dir
        return false;
    }
    return false; 
};

MFile* HttpFile::getNextFileInDir() { 
    if(formHeader()->m_isWebDAV) {
        // we can try if this is webdav, then
        // PROPFIND allows listing dir
        return nullptr;
    }
    return nullptr; 
};

bool HttpFile::isText() {
    return formHeader()->isText;
}

/********************************************************
 * Istream impls
 ********************************************************/
bool HttpIStream::open() {
    return m_http.GET(url);
};

void HttpIStream::close() {
    m_http.close();
}

bool HttpIStream::seek(size_t pos) {
    return m_http.seek(pos);
}

size_t HttpIStream::read(uint8_t* buf, size_t size) {
    return m_http.read(buf, size);
};

size_t HttpIStream::write(const uint8_t *buf, size_t size) {
    return -1;
}



bool HttpIStream::isOpen() {
    return m_http.m_isOpen;
};

size_t HttpIStream::size() {
    return m_http.m_length;
};

size_t HttpIStream::available() {
    return m_http.m_bytesAvailable;
};

size_t HttpIStream::position() {
    return m_http.m_position;
}

/********************************************************
 * Meat HTTP client impls
 ********************************************************/
bool MeatHttpClient::GET(std::string dstUrl) {
    return open(dstUrl, HTTP_METHOD_GET);
}

bool MeatHttpClient::POST(std::string dstUrl) {
    return open(dstUrl, HTTP_METHOD_POST);
}

bool MeatHttpClient::PUT(std::string dstUrl) {
    return open(dstUrl, HTTP_METHOD_PUT);
}

bool MeatHttpClient::HEAD(std::string dstUrl) {
    bool rc = open(dstUrl, HTTP_METHOD_HEAD);
    close();
    return rc;
}

bool MeatHttpClient::open(std::string dstUrl, esp_http_client_method_t meth) {
    url = dstUrl;
    wasRedirected = false;

    Debug_printv("url[%s]", url.c_str());
    int httpCode = tryOpen(meth);

    while(httpCode == HttpStatus_MovedPermanently || httpCode == HttpStatus_Found || httpCode == 303)
    {
        Debug_printv("--- Page moved, doing redirect");
        httpCode = tryOpen(meth);
        wasRedirected = true;
    }
    
    if(httpCode != HttpStatus_Ok && httpCode != 301) {
        Debug_printv("opening stream failed, httpCode=%d", httpCode);
        close();
        return false;
    }


    // TODO - set m_isWebDAV somehow
    m_isOpen = true;
    m_exists = true;
    m_position = 0;
    lastMethod = meth;

    Debug_printv("length=%d isFriendlySkipper=[%d] isText=[%d], httpCode=[%d]", m_length, isFriendlySkipper, isText, httpCode);

    return true;
};

void MeatHttpClient::close() {
    if(m_http != nullptr) {
        if(m_isOpen) {
            esp_http_client_close(m_http);
        }
        esp_http_client_cleanup(m_http);
        m_http = nullptr;
    }
    m_isOpen = false;
}

void MeatHttpClient::setOnHeader(const std::function<int(char*, char*)> &lambda) {
    onHeader = lambda;
}

bool MeatHttpClient::seek(size_t pos) {
    if(pos==m_position)
        return true;

    if(isFriendlySkipper) {
        char str[40];

        // Range: bytes=91536-(91536+255)
        snprintf(str, sizeof str, "bytes=%lu-%lu", (unsigned long)pos, ((unsigned long)pos + 255));
        esp_http_client_set_header(m_http, "range", str);
        esp_http_client_set_method(m_http, lastMethod);

        esp_err_t initOk = esp_http_client_open(m_http, 0); // or open? It's not entirely clear...

        Debug_printv("SEEK in HttpIStream %s: RC=%d", url.c_str(), initOk);
        if(initOk != ESP_OK)
            return false;

        esp_http_client_fetch_headers(m_http);
        int httpCode = esp_http_client_get_status_code(m_http);
        Debug_printv("httpCode=[%d] request range=[%s]", httpCode, str);
        if(httpCode != HttpStatus_Ok || httpCode != 206)
            return false;

        Debug_printv("stream opened[%s]", url.c_str());

        m_position = pos;
        m_bytesAvailable = m_length-pos;
        return true;
    } else if(lastMethod == HTTP_METHOD_GET) {
        // server doesn't support resume, so...
        if(pos<m_position || pos == 0) {
            // skipping backward let's simply reopen the stream...
            esp_http_client_close(m_http);
            bool op = open(url, lastMethod);
            if(!op)
                return false;

            // and read pos bytes - requires some buffer
            esp_http_client_read(m_http, nullptr, pos);
        }
        else {
            // skipping forward let's skip a proper amount of bytes - requires some buffer
            esp_http_client_read(m_http, nullptr, pos-m_position);
        }

        m_bytesAvailable = m_length-pos;
        m_position = pos;

        return true;
    }
    else
        return false;
}

size_t MeatHttpClient::read(uint8_t* buf, size_t size) {
    if (m_isOpen) {
        auto bytesRead= esp_http_client_read(m_http, (char *)buf, size );
        m_bytesAvailable -= bytesRead;
        m_position+=bytesRead;
        return bytesRead;        
    }
    return 0;
};

size_t MeatHttpClient::write(const uint8_t* buf, size_t size) {
    if (m_isOpen) {
        auto bytesWritten= esp_http_client_write(m_http, (char *)buf, size );
        m_bytesAvailable -= bytesWritten;
        m_position+=bytesWritten;
        return bytesWritten;        
    }
    return 0;
};

int MeatHttpClient::tryOpen(esp_http_client_method_t meth) {

    Debug_printv("url[%s]", url.c_str());
    if ( url.size() < 5)
        return 0;

    //mstr::replaceAll(url, "HTTP:", "http:");
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .user_agent = USER_AGENT,
        .method = meth,
        .timeout_ms = 10000,
        .max_redirection_count = 10,
        .event_handler = _http_event_handler,
        .user_data = this,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 10
    };

    m_http = esp_http_client_init(&config);

    // Debug_printv("--- PRE OPEN")

    esp_err_t initOk = esp_http_client_open(m_http, 0); // or open? It's not entirely clear...

    if(initOk == ESP_FAIL)
        return 0;

    // Debug_printv("--- PRE FETCH HEADERS")
    m_length = -1;
    m_bytesAvailable = 0;

    int lengthResp = esp_http_client_fetch_headers(m_http);
    if(lengthResp > 0) {
        // only if we aren't chunked!
        m_length = lengthResp;
        m_bytesAvailable = m_length;
    }

    // Debug_printv("--- PRE GET STATUS CODE")

    return esp_http_client_get_status_code(m_http);
}

esp_err_t MeatHttpClient::_http_event_handler(esp_http_client_event_t *evt)
{
    MeatHttpClient* meatClient = (MeatHttpClient*)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR: // This event occurs when there are any errors during execution
            Debug_printv("HTTP_EVENT_ERROR");
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
            if (strcmp("Accept-Ranges", evt->header_key)==0)
            {
                if(meatClient != nullptr) {
                    meatClient->isFriendlySkipper = strcmp("bytes", evt->header_value)==0;
                    //Debug_printv("* Ranges info present '%s', comparison=%d!",evt->header_value, strcmp("bytes", evt->header_value)==0);
                }
            }
            // what can we do UTF8<->PETSCII on this stream?
            else if (strcmp("Content-Type", evt->header_key)==0)
            {
                std::string asString = evt->header_value;
                bool isText = mstr::isText(asString);

                if(meatClient != nullptr) {
                    meatClient->isText = isText;
                    //Debug_printv("* Content info present '%s', isText=%d!", evt->header_value, isText);
                }        
            }
            else if(strcmp("Last-Modified", evt->header_key)==0)
            {
                // Last-Modified, value=Thu, 03 Dec 1992 08:37:20 - may be used to get file date
            }
            else if(strcmp("Content-Length", evt->header_key)==0)
            {
                Debug_printv("* Content len present '%s'", evt->header_value);
            }
            else if(strcmp("Location", evt->header_key)==0)
            {
                Debug_printv("* This page redirects from '%s' to '%s'", meatClient->url.c_str(), evt->header_value);
                if ( mstr::startsWith(evt->header_value, (char *)"http") )
                {
                    Debug_printv("match");
                    meatClient->url = evt->header_value;
                }
                else
                {
                    Debug_printv("no match");
                    meatClient->url += evt->header_value;                    
                }

                Debug_printv("new url '%s'", meatClient->url.c_str());
            }

            // Allow override in lambda
            meatClient->onHeader(evt->header_key, evt->header_value);

            break;

        case HTTP_EVENT_ON_DATA: // Occurs multiple times when receiving body data from the server. MAY BE SKIPPED IF BODY IS EMPTY!
            //Debug_printv("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            {
                int status = esp_http_client_get_status_code(meatClient->m_http);

                if ((status == HttpStatus_Found || status == HttpStatus_MovedPermanently || status == 303) /*&& client->_redirect_count < (client->_max_redirects - 1)*/)
                {
                    //Debug_printv("HTTP_EVENT_ON_DATA: Redirect response body, ignoring");
                }
                else {
                    //Debug_printv("HTTP_EVENT_ON_DATA: Got response body");
                }


                if (esp_http_client_is_chunked_response(evt->client)) {
                    int len;
                    esp_http_client_get_chunk_length(evt->client, &len);
                    //meatClient->m_length += len;
                    meatClient->m_bytesAvailable = len;
                    //Debug_printv("HTTP_EVENT_ON_DATA: Got chunked response, chunklen=%d, contentlen[%d]", len, meatClient->m_length);
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH: 
            // Occurs when finish a HTTP session
            // This may get called more than once if esp_http_client decides to retry in order to handle a redirect or auth response
            //Debug_printv("HTTP_EVENT_ON_FINISH %u\n", uxTaskGetStackHighWaterMark(nullptr));
            // Keep track of how many times we "finish" reading a response from the server
            //Debug_printv("HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED: // The connection has been disconnected
            //Debug_printv("HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}
