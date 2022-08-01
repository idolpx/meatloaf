#include "http.h"

/********************************************************
 * File impls
 ********************************************************/
// char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

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
        esp_http_client_set_method(m_http, HTTP_METHOD_PUT);
        esp_err_t initOk = esp_http_client_perform(m_http); // or open? It's not entirely clear...

        Debug_printv("SEEK in HttpOStream %s: RC=%d", url.c_str(), initOk);
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
        // can't resume upload, there's nothing we can do
        return false;
    }
}

void HttpOStream::close() {
    esp_http_client_close(m_http);
    esp_http_client_cleanup(m_http);
    m_isOpen = false;
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
        // .event_handler = _http_event_handler,
        // .user_data = local_response_buffer,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 10
    };
    m_http = esp_http_client_init(&config);
    esp_err_t initOk = esp_http_client_perform(m_http); // or open? It's not entirely clear...

    Debug_printv("opening HttpOStream %s: result=%d", url.c_str(), initOk);
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

        Debug_printv("SEEK in HttpIStream %s: RC=%d", url.c_str(), initOk);
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
        // server doesn't support resume, so...
        if(pos<m_position || pos == 0) {
            // skipping backward let's simply reopen the stream...
            esp_http_client_close(m_http);
            bool op = open();
            if(!op)
                return false;

            // and read pos bytes
            esp_http_client_read(m_http, nullptr, pos);
        }
        else {
            // skipping forward let's skip a proper amount of bytes
            esp_http_client_read(m_http, nullptr, pos-m_position);
        }

        m_bytesAvailable = m_length-pos;

        return true;
    }
}

void HttpIStream::close() {
    esp_http_client_close(m_http);
    esp_http_client_cleanup(m_http);
    m_isOpen = false;
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

// esp_err_t _http_event_handler(esp_http_client_event_t *evt)
// {
//     static char *output_buffer;  // Buffer to store response of http request from event handler
//     static int output_len;       // Stores number of bytes read
//     switch(evt->event_id) {
//         case HTTP_EVENT_ERROR:
//             ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
//             break;
//         case HTTP_EVENT_ON_CONNECTED:
//             ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
//             break;
//         case HTTP_EVENT_HEADER_SENT:
//             ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
//             break;
//         case HTTP_EVENT_ON_HEADER:
//             ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
//             break;
//         case HTTP_EVENT_ON_DATA:
//             ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
//             /*
//              *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
//              *  However, event handler can also be used in case chunked encoding is used.
//              */
//             if (!esp_http_client_is_chunked_response(evt->client)) {
//                 // If user_data buffer is configured, copy the response into the buffer
//                 if (evt->user_data) {
//                     memcpy(evt->user_data + output_len, evt->data, evt->data_len);
//                 } else {
//                     if (output_buffer == NULL) {
//                         output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
//                         output_len = 0;
//                         if (output_buffer == NULL) {
//                             ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
//                             return ESP_FAIL;
//                         }
//                     }
//                     memcpy(output_buffer + output_len, evt->data, evt->data_len);
//                 }
//                 output_len += evt->data_len;
//             }

//             break;
//         case HTTP_EVENT_ON_FINISH:
//             ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
//             if (output_buffer != NULL) {
//                 // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
//                 // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
//                 free(output_buffer);
//                 output_buffer = NULL;
//             }
//             output_len = 0;
//             break;
//         case HTTP_EVENT_DISCONNECTED:
//             ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
//             int mbedtls_err = 0;
//             esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
//             if (err != 0) {
//                 ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
//                 ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
//             }
//             if (output_buffer != NULL) {
//                 free(output_buffer);
//                 output_buffer = NULL;
//             }
//             output_len = 0;
//             break;
//         case HTTP_EVENT_REDIRECT:
//             ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
//             esp_http_client_set_header(evt->client, "From", "user@example.com");
//             esp_http_client_set_header(evt->client, "Accept", "text/html");
//             break;
//     }
//     return ESP_OK;
// }