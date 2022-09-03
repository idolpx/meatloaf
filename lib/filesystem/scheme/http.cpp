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

MStream* HttpFile::meatStream() {
    // has to return OPENED stream
    //Debug_printv("Input stream requested: [%s]", url.c_str());
    MStream* istream = new HttpIStream(url);
    istream->open();
    return istream;
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
    Debug_printv("CLOSE called explicitly on this HTTP stream!");    
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
    return open(dstUrl, HTTP_METHOD_HEAD);
}

bool MeatHttpClient::attemptRequestWithRedirect(int range) {
    m_position = 0;
    wasRedirected = false;
    Debug_printv("requesting url[%s] from position:%d", url.c_str(), range);
    lastRC = performRequestFetchHeaders(range);

    while(lastRC == HttpStatus_MovedPermanently || lastRC == HttpStatus_Found || lastRC == 303)
    {
        Debug_printv("--- Page moved, doing redirect to [%s]", url.c_str());
        lastRC = performRequestFetchHeaders(range);
        wasRedirected = true;
    }
    
    if(lastRC != HttpStatus_Ok && lastRC != 301 && lastRC != 206) {
        Debug_printv("opening stream failed, httpCode=%d", lastRC);
        close();
        return false;
    }

    // TODO - set m_isWebDAV somehow
    m_isOpen = true;
    m_exists = true;

    Debug_printv("request successful, length=%d isFriendlySkipper=[%d] isText=[%d], httpCode=[%d]", m_length, isFriendlySkipper, isText, lastRC);

    return true;

}

bool MeatHttpClient::open(std::string dstUrl, esp_http_client_method_t meth) {
    Debug_printv("OPEN called! dstUrl[%s] meth[%d]", dstUrl.c_str(), meth);

    url = dstUrl;

    if ( url.size() < 5)
        return false;

    lastMethod = meth;

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
        .keep_alive_interval = 1
    };

    m_http = esp_http_client_init(&config);

    return attemptRequestWithRedirect(0);
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
    Debug_printv("pos[%d]", pos);

    if(pos==m_position)
        return true;

    if(isFriendlySkipper) {
        //esp_http_client_close(m_http);
        
        Debug_printv("seek will attempt range request...");

        bool op = attemptRequestWithRedirect(pos);

        Debug_printv("attemptRequestWithRedirect %s: returned=%d", url.c_str(), lastRC);
        
        if(!op)
            return false;

         // 200 = range not supported! according to https://developer.mozilla.org/en-US/docs/Web/HTTP/Range_requests
        if(lastRC == 206){
            Debug_printv("Seek successful");

            m_position = pos;
            //m_bytesAvailable = m_length-pos;
            return true;
        }
    }

    if(lastMethod == HTTP_METHOD_GET) {
        Debug_printv("Server doesn't support resume, reading from start and discarding");
        // server doesn't support resume, so...
        if(pos<m_position || pos == 0) {
            // skipping backward let's simply reopen the stream...
            esp_http_client_close(m_http);
            bool op = open(url, lastMethod);
            if(!op)
                return false;

            // and read pos bytes - requires some buffer
            for(int i = 0; i<pos; i++) {
                char c;
                int rc = esp_http_client_read_response(m_http, &c, 1);
                if(rc == -1)
                    return false;
            }
        }
        else {
            auto delta = pos-m_position;
            // skipping forward let's skip a proper amount of bytes - requires some buffer
            for(int i = 0; i<delta; i++) {
                char c;
                int rc = esp_http_client_read_response(m_http, &c, 1);
                if(rc == -1)
                    return false;
            }
        }

        m_bytesAvailable = m_length-pos;
        m_position = pos;
        Debug_printv("stream opened[%s]", url.c_str());

        return true;
    }
    else
        return false;
}

size_t MeatHttpClient::read(uint8_t* buf, size_t size) {
    if (m_isOpen) {
        auto bytesRead= esp_http_client_read_response(m_http, (char *)buf, size );

        Debug_printf("%d bytes were available for reading\n", bytesRead);

        if(bytesRead>0) {
            // for(int i=0; i<bytesRead; i++) {
            //     Debug_printf("%c", buf[i]);
            // }

            Debug_printf("  ");

            for(int i=0; i<bytesRead; i++) {
                Debug_printf("%.2X ", buf[i]);
            }

            m_bytesAvailable -= bytesRead;
            m_position+=bytesRead;
        }
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


int MeatHttpClient::performRequestFetchHeaders(int resume) {
    esp_http_client_set_method(m_http, lastMethod);
    esp_http_client_set_url(m_http, url.c_str());

    if(resume > 0) {
        char str[40];
        snprintf(str, sizeof str, "bytes=%lu-", (unsigned long)resume);
        esp_http_client_set_header(m_http, "range", str);
    }

    // Debug_printv("--- PRE PERFORM")

    //esp_err_t initOk = esp_http_client_open(m_http, 0); // or open? It's not entirely clear...
    m_bytesAvailable = 0;

    esp_err_t initOk = esp_http_client_perform(m_http); // or open? It's not entirely clear...

    if(initOk == ESP_FAIL)
        return 0;


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

            if (mstr::equals("Accept-Ranges", evt->header_key, false))
            {
                if(meatClient != nullptr) {
                    meatClient->isFriendlySkipper = mstr::equals("bytes", evt->header_value,false);
                    //Debug_printv("* Ranges info present '%s', comparison=%d!",evt->header_value, strcmp("bytes", evt->header_value)==0);
                }
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

            }
            else if(mstr::equals("Content-Length", evt->header_key, false))
            {
                // 20:06:42.981 > [lib/filesystem/scheme/http.h:19] operator()(): HTTP_EVENT_ON_HEADER, key=Content-Length, value=83200
                int leng = atoi(evt->header_value);
                if(meatClient->m_length == 0)
                    meatClient->m_length = leng;
                meatClient->m_bytesAvailable = leng;
                //Debug_printv("* Content len present '%d'", meatClient->m_length);
            }
            else if(mstr::equals("Location", evt->header_key, false))
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
            Debug_printv("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            Debug_printv("data[%s]", evt->data);
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
            Debug_printv("HTTP_EVENT_DISCONNECTED");
            meatClient->m_isOpen = false;
            break;
    }
    return ESP_OK;
}
