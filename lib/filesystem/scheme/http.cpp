#include "http.h"

/********************************************************
 * File impls
 ********************************************************/

bool HttpFile::isDirectory() {
    // hey, why not?
    return false;
}

MIStream* HttpFile::inputStream() {
    // has to return OPENED stream
    Debug_printv("[%s]", url.c_str());
    MIStream* istream = new HttpIStream(url);
    istream->open();
    return istream;
}

MIStream* HttpFile::createIStream(std::shared_ptr<MIStream> is) {
    return is.get(); // we've overriden istreamfunction, so this one won't be used
}

MOStream* HttpFile::outputStream() {
    // has to return OPENED stream
    MOStream* ostream = new HttpOStream(url);
    ostream->open();
    return ostream;
}

time_t HttpFile::getLastWrite() {
    return 0; // might be taken from Last-Modified, probably not worth it
}

time_t HttpFile::getCreationTime() {
    return 0; // might be taken from Last-Modified, probably not worth it
}

bool HttpFile::exists() {
    Debug_printv("[%s]", url.c_str());
    // we may try open the stream to check if it exists
    std::unique_ptr<MIStream> test(inputStream());
    // remember that MIStream destuctor should close the stream!
    return test->isOpen();
}

size_t HttpFile::size() {
    // we may take content-lenght from header if exists
    std::unique_ptr<MIStream> test(inputStream());

    size_t size = 0;

    if(test->isOpen())
        size = test->available();

    test->close();

    return size;
}



// void HttpFile::addHeader(const String& name, const String& value, bool first, bool replace) {
//     //m_http.addHeader
// }


/********************************************************
 * Ostream impls
 ********************************************************/

size_t HttpOStream::size() {
    return m_length;
};

size_t HttpOStream::available() {
    return m_bytesAvailable;
};

size_t HttpOStream::position() {
    return m_position;
}

bool HttpOStream::seek(size_t pos) {
    if(pos==m_position)
        return true;

    if(isFriendlySkipper) {
        char str[40];

        // Range: bytes=91536-(91536+255)
        snprintf(str, sizeof str, "bytes=%lu-%lu", (unsigned long)pos, ((unsigned long)pos + 255));
        esp_http_client_set_header(m_http, "range", str);
        esp_http_client_set_method(m_http, HTTP_METHOD_GET);
        esp_err_t initOk = esp_http_client_perform(m_http); // or open? It's not entirely clear...

        Debug_printv("SEEK ing in input %s: someRc=%d", url.c_str(), initOk);
        if(initOk != ESP_OK)
            return false;

        int httpCode = esp_http_client_get_status_code(m_http);
        Debug_printv("httpCode[%d] str[%s]", httpCode, str);
        if(httpCode != 200 || httpCode != 206)
            return false;

        Debug_printv("stream opened[%s]", url.c_str());

        m_position = pos;
        m_bytesAvailable = m_length-pos;
        return true;
    } else {
        if(pos<m_position) {
            // skipping backward and range not supported, let's simply reopen the stream...
            esp_http_client_close(m_http);
            bool op = open();
            if(!op)
                return false;
        }

        m_position = 0;
        // ... and then read until we reach pos
        while(m_position < pos) {
            // auto bytesRead= esp_http_client_read(m_http, (char *)buf, size );
        }
        m_bytesAvailable = m_length-pos;

        return true;
    }
}

void HttpOStream::close() {
    esp_http_client_close(m_http);
    esp_http_client_cleanup(m_http);
}

bool HttpOStream::open() {
    //mstr::replaceAll(url, "HTTP:", "http:");
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .user_agent = USER_AGENT,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 10000,
        .disable_auto_redirect = false,
        .max_redirection_count = 10,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 10,
    };
    m_http = esp_http_client_init(&config);
    esp_err_t initOk = esp_http_client_perform(m_http); // or open? It's not entirely clear...

    Debug_printv("upening %s: result=%d", url.c_str(), initOk);
    if(initOk != ESP_OK)
        return false;

    int httpCode = esp_http_client_get_status_code(m_http);
    Debug_printv("httpCode=%d", httpCode);
    if(httpCode != 200)
        return false;

    m_isOpen = true;
    m_position = 0;

    esp_http_client_fetch_headers(m_http);

    // Let's get the length of the payload
    m_length = esp_http_client_get_content_length(m_http);
    m_bytesAvailable = m_length;

    // Does this server support resume?
    // Accept-Ranges: bytes
    char* ranges;
    esp_http_client_get_header(m_http, "accept-ranges", &ranges);
    isFriendlySkipper = strcmp("bytes", ranges);

    // Let's see if it's plain text, so we can do UTF8-PETSCII magic!
    char* ct;
    esp_http_client_get_header(m_http, "content-type", &ct);
    std::string asString = ct;
    isText = mstr::isText(asString);

    Debug_printv("length=%d isFriendlySkipper=[%d] content_type=[%s]", m_length, isFriendlySkipper, ct);

    return true;
};

size_t HttpOStream::write(const uint8_t* buf, size_t size) {
    auto bytesRead= esp_http_client_read(m_http, (char *)buf, size );
    m_bytesAvailable -= bytesRead;
    m_position+=bytesRead;
    return bytesRead;
};

bool HttpOStream::isOpen() {
    return m_isOpen;
};




/********************************************************
 * Istream impls
 ********************************************************/
size_t HttpIStream::size() {
    return m_length;
};

size_t HttpIStream::available() {
    return m_bytesAvailable;
};

size_t HttpIStream::position() {
    return m_position;
}

bool HttpIStream::seek(size_t pos) {
    if(pos==m_position)
        return true;

    if(isFriendlySkipper) {
        char str[40];

        // Range: bytes=91536-(91536+255)
        snprintf(str, sizeof str, "bytes=%lu-%lu", (unsigned long)pos, ((unsigned long)pos + 255));
        esp_http_client_set_header(m_http, "range", str);
        esp_http_client_set_method(m_http, HTTP_METHOD_GET);
        esp_err_t initOk = esp_http_client_perform(m_http); // or open? It's not entirely clear...

        Debug_printv("SEEK ing in input %s: someRc=%d", url.c_str(), initOk);
        if(initOk != ESP_OK)
            return false;

        int httpCode = esp_http_client_get_status_code(m_http);
        Debug_printv("httpCode[%d] str[%s]", httpCode, str);
        if(httpCode != 200 || httpCode != 206)
            return false;

        Debug_printv("stream opened[%s]", url.c_str());

        m_position = pos;
        m_bytesAvailable = m_length-pos;
        return true;
    } else {
        if(pos<m_position) {
            // skipping backward and range not supported, let's simply reopen the stream...
            esp_http_client_close(m_http);
            bool op = open();
            if(!op)
                return false;
        }

        m_position = 0;
        // ... and then read until we reach pos
        while(m_position < pos) {
            // auto bytesRead= esp_http_client_read(m_http, (char *)buf, size );
        }
        m_bytesAvailable = m_length-pos;

        return true;
    }
}

void HttpIStream::close() {
    esp_http_client_close(m_http);
    esp_http_client_cleanup(m_http);
}

bool HttpIStream::open() {
    //mstr::replaceAll(url, "HTTP:", "http:");
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .user_agent = USER_AGENT,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .disable_auto_redirect = false,
        .max_redirection_count = 10,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 10,
    };
    m_http = esp_http_client_init(&config);
    esp_err_t initOk = esp_http_client_perform(m_http); // or open? It's not entirely clear...

    Debug_printv("upening %s: result=%d", url.c_str(), initOk);
    if(initOk != ESP_OK)
        return false;

    int httpCode = esp_http_client_get_status_code(m_http);
    Debug_printv("httpCode=%d", httpCode);
    if(httpCode != 200)
        return false;

    m_isOpen = true;
    m_position = 0;

    esp_http_client_fetch_headers(m_http);

    // Let's get the length of the payload
    m_length = esp_http_client_get_content_length(m_http);
    m_bytesAvailable = m_length;

    // Does this server support resume?
    // Accept-Ranges: bytes
    char* ranges;
    esp_http_client_get_header(m_http, "accept-ranges", &ranges);
    isFriendlySkipper = strcmp("bytes", ranges);

    // Let's see if it's plain text, so we can do UTF8-PETSCII magic!
    char* ct;
    esp_http_client_get_header(m_http, "content-type", &ct);
    std::string asString = ct;
    isText = mstr::isText(asString);

    Debug_printv("length=%d isFriendlySkipper=[%d] content_type=[%s]", m_length, isFriendlySkipper, ct);

    return true;
};

size_t HttpIStream::read(uint8_t* buf, size_t size) {
    auto bytesRead= esp_http_client_read(m_http, (char *)buf, size );
    m_bytesAvailable -= bytesRead;
    m_position+=bytesRead;
    return bytesRead;
};

bool HttpIStream::isOpen() {
    return m_isOpen;
};
