#include "http.h"


/********************************************************
 * File impls
 ********************************************************/

bool HttpFile::isDirectory() {
    // hey, why not?
    // try webdav PROPFIND to get a listing
    return false;
}

MIStream* HttpFile::inputStream() {
    // has to return OPENED stream
    //Debug_printv("Input stream requested: [%s]", url.c_str());
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
    return ostream;
    if ( ostream->open() )
        return ostream;
    else
        return nullptr;    
}

time_t HttpFile::getLastWrite() {
    return 0; // might be taken from Last-Modified, probably not worth it
}

time_t HttpFile::getCreationTime() {
    return 0; // might be taken from Last-Modified, probably not worth it
}

bool HttpFile::exists() {
    // Debug_printv("[%s]", url.c_str());
    // // we may try open the stream to check if it exists
    // std::unique_ptr<MIStream> test(inputStream());
    // // remember that MIStream destuctor should close the stream!
    // return test->isOpen();

    // try webdav PROPFIND to get a listing

    return true;
}

size_t HttpFile::size() {
    // we may take content-lenght from header if exists

    // try webdav PROPFIND to get a listing

    std::unique_ptr<MIStream> test(inputStream());
    size_t size = 0;
    if(test->isOpen())
        size = test->available();

    test->close();

    return size;
}

// we can try if this is webdav, then
// PROPFIND allows listing dir
// PROPPATCH allows deletion
// MKCOL creates dir






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

bool HttpOStream::isOpen() {
    return m_isOpen;
};

bool HttpOStream::open() {
    mstr::replaceAll(url, "HTTP:", "http:");
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .user_agent = USER_AGENT,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 10000,
        .disable_auto_redirect = false,
        .max_redirection_count = 10,
        .event_handler = _http_event_handler,
        .user_data = this,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 10
    };

    m_http = esp_http_client_init(&config);
    esp_err_t initOk = esp_http_client_open(m_http, 0); // or open? It's not entirely clear...
    m_length = esp_http_client_fetch_headers(m_http);
    m_bytesAvailable = m_length;

    int httpCode = esp_http_client_get_status_code(m_http);
    if(httpCode != HttpStatus_Ok) {
        Debug_printv("opening stream failed, httpCode=%d", httpCode);
        close();
        return false;
    }

    m_isOpen = true;
    m_position = 0;

    //Debug_printv("length=%d isFriendlySkipper=[%d] isText=[%d]", m_length, isFriendlySkipper, isText);

    return true;
};

bool HttpOStream::seek(size_t pos) {
    if(pos==m_position)
        return true;

    if(isFriendlySkipper) {
        char str[40];

        // Range: bytes=91536-(91536+255)
        snprintf(str, sizeof str, "bytes=%lu-%lu", (unsigned long)pos, ((unsigned long)pos + 255));
        esp_http_client_set_header(m_http, "range", str);
        esp_http_client_set_method(m_http, HTTP_METHOD_PUT);

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
    } else {
        // resume not supported, there's really nothing we could do...
        return false;
    }
}

size_t HttpOStream::write(const uint8_t* buf, size_t size) {
    auto bytesRead= esp_http_client_read(m_http, (char *)buf, size );
    m_bytesAvailable -= bytesRead;
    m_position+=bytesRead;
    return bytesRead;
};

void HttpOStream::close() {
    if(m_http != nullptr) {
        if(m_isOpen) {
            esp_http_client_close(m_http);
        }
        esp_http_client_cleanup(m_http);
        m_http = nullptr;
    }
    m_isOpen = false;
}

esp_err_t HttpOStream::_http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read

    HttpOStream* istream = (HttpOStream*)evt->user_data;

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
                if(istream != nullptr) {
                    istream->isFriendlySkipper = strcmp("bytes", evt->header_value)==0;
                    //Debug_printv("* Ranges info present '%s', comparison=%d!",evt->header_value, strcmp("bytes", evt->header_value)==0);
                }
            }
            // what can we do UTF8<->PETSCII on this stream?
            else if (strcmp("Content-Type", evt->header_key)==0)
            {
                std::string asString = evt->header_value;
                bool isText = mstr::isText(asString);


                if(istream != nullptr) {
                    istream->isText = isText;
                    //Debug_printv("* Content info present '%s', isText=%d!", evt->header_value, isText);
                }        
            }
            else if(strcmp("Last-Modified", evt->header_key)==0)
            {
            }
            else {
                // Debug_printv("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
                // Last-Modified, value=Thu, 03 Dec 1992 08:37:20 - may be used to get file date
            }

            break;
        case HTTP_EVENT_ON_DATA: // Occurs multiple times when receiving body data from the server. MAY BE SKIPPED IF BODY IS EMPTY!
            Debug_printv("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            // if (!esp_http_client_is_chunked_response(evt->client)) {
            //     I-
            //     if (evt->user_data) {
            //         memcpy(evt->user_data + output_len, evt->data, evt->data_len);
            //     } else {
            //         if (output_buffer == NULL) {
            //             output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
            //             output_len = 0;
            //             if (output_buffer == NULL) {
            //                 ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
            //                 return ESP_FAIL;
            //             }
            //         }
            //         memcpy(output_buffer + output_len, evt->data, evt->data_len);
            //     }
            //     output_len += evt->data_len;
            // }

            break;




// z fnHttpClient.cpp
// #ifdef VERBOSE_HTTP
//         Debug_printv("HTTP_EVENT_ON_DATA %u\n", uxTaskGetStackHighWaterMark(nullptr));
// #endif
//         // Don't do any of this if we're told to ignore the response
//         if (client->_ignore_response_body == true)
//             break;

//         // esp_http_client will automatically retry redirects, so ignore all but the last attemp
//         int status = esp_http_client_get_status_code(client->_handle);
//         if ((status == HttpStatus_Found || status == HttpStatus_MovedPermanently) && client->_redirect_count < (client->_max_redirects - 1))
//         {
// #ifdef VERBOSE_HTTP
//             Debug_println("HTTP_EVENT_ON_DATA: Ignoring redirect response");
// #endif
//             break;
//         }
//         /*
//          If auth type is set to NONE, esp_http_client will automatically retry auth failures by attempting to set the auth type to
//          BASIC or DIGEST depending on the server response code. Ignore this attempt.
//         */
//         if (status == HttpStatus_Unauthorized && client->_auth_type == HTTP_AUTH_TYPE_NONE && client->_redirect_count == 0)
//         {
// #ifdef VERBOSE_HTTP
//             Debug_println("HTTP_EVENT_ON_DATA: Ignoring UNAUTHORIZED response");
// #endif
//             break;
//         }

//         // Check if this is our first time this event has been triggered
//         if (client->_transaction_begin == true)
//         {
//             client->_transaction_begin = false;
//             client->_transaction_done = false;
//             // Let the main thread know we're done reading headers and have moved on to the data
//             xTaskNotifyGive(client->_taskh_consumer);
//         }

//         // Wait to be told we can fill the buffer
// #ifdef VERBOSE_HTTP
//         Debug_println("HTTP_EVENT_ON_DATA: Waiting to start reading");
// #endif
//         ulTaskNotifyTake(1, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_CONSUMER_TASK));

// #ifdef VERBOSE_HTTP
//        Debug_printv("HTTP_EVENT_ON_DATA: Data: %p, Datalen: %d\n", evt->data, evt->data_len);
// #endif

//         client->_buffer_pos = 0;
//         client->_buffer_len = (evt->data_len > DEFAULT_HTTP_BUF_SIZE) ? DEFAULT_HTTP_BUF_SIZE : evt->data_len;
//         memcpy(client->_buffer, evt->data, client->_buffer_len);

//         // Now let the reader know there's data in the buffer
//         xTaskNotifyGive(client->_taskh_consumer);
//         break;













        case HTTP_EVENT_ON_FINISH: // Occurs when finish a HTTP session
            // This may get called more than once if esp_http_client decides to retry in order to handle a redirect or auth response
            //Debug_printv("HTTP_EVENT_ON_FINISH %u\n", uxTaskGetStackHighWaterMark(nullptr));
            // Keep track of how many times we "finish" reading a response from the server

            Debug_printv("HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED: // The connection has been disconnected
            Debug_printv("HTTP_EVENT_DISCONNECTED");
            // int mbedtls_err = 0;
            // esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            // if (err != 0) {
            //     ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            //     ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            // }
            // if (output_buffer != NULL) {
            //     free(output_buffer);
            //     output_buffer = NULL;
            // }
            // output_len = 0;
            break;
    }
    return ESP_OK;
}




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

bool HttpIStream::isOpen() {
    return m_isOpen;
};

bool HttpIStream::open() {
    mstr::replaceAll(url, "HTTP:", "http:");
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .user_agent = USER_AGENT,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .disable_auto_redirect = false,
        .max_redirection_count = 10,
        .event_handler = _http_event_handler,
        .user_data = this,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 10
    };

    m_http = esp_http_client_init(&config);
    esp_err_t initOk = esp_http_client_open(m_http, 0); // or open? It's not entirely clear...
    m_length = esp_http_client_fetch_headers(m_http);
    m_bytesAvailable = m_length;

    int httpCode = esp_http_client_get_status_code(m_http);

    while(httpCode == HttpStatus_MovedPermanently || httpCode == HttpStatus_Found)
    {
        int discarded = 0;
        esp_http_client_flush_response(m_http, &discarded);
        Debug_printv("Got redirect, httpCode=%d, flushed=%d", httpCode, discarded);
        m_length = esp_http_client_fetch_headers(m_http);
        m_bytesAvailable = m_length;
        httpCode = esp_http_client_get_status_code(m_http);
    }
    
    if(httpCode != HttpStatus_Ok) {
        Debug_printv("opening stream failed, httpCode=%d", httpCode);
        close();
        return false;
    }

    m_isOpen = true;
    m_position = 0;

    //Debug_printv("length=%d isFriendlySkipper=[%d] isText=[%d]", m_length, isFriendlySkipper, isText);

    return true;
};

bool HttpIStream::seek(size_t pos) {
    if(pos==m_position)
        return true;

    if(isFriendlySkipper) {
        char str[40];

        // Range: bytes=91536-(91536+255)
        snprintf(str, sizeof str, "bytes=%lu-%lu", (unsigned long)pos, ((unsigned long)pos + 255));
        esp_http_client_set_header(m_http, "range", str);
        esp_http_client_set_method(m_http, HTTP_METHOD_GET);

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

size_t HttpIStream::read(uint8_t* buf, size_t size) {
    if (m_isOpen) {
        auto bytesRead= esp_http_client_read(m_http, (char *)buf, size );
        m_bytesAvailable -= bytesRead;
        m_position+=bytesRead;
        return bytesRead;        
    }
    return 0;
};

void HttpIStream::close() {
    if(m_http != nullptr) {
        if(m_isOpen) {
            esp_http_client_close(m_http);
        }
        esp_http_client_cleanup(m_http);
        m_http = nullptr;
    }
    m_isOpen = false;
}

esp_err_t HttpIStream::_http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read

    HttpIStream* istream = (HttpIStream*)evt->user_data;

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
                if(istream != nullptr) {
                    istream->isFriendlySkipper = strcmp("bytes", evt->header_value)==0;
                    //Debug_printv("* Ranges info present '%s', comparison=%d!",evt->header_value, strcmp("bytes", evt->header_value)==0);
                }
            }
            // what can we do UTF8<->PETSCII on this stream?
            else if (strcmp("Content-Type", evt->header_key)==0)
            {
                std::string asString = evt->header_value;
                bool isText = mstr::isText(asString);

                if(istream != nullptr) {
                    istream->isText = isText;
                    //Debug_printv("* Content info present '%s', isText=%d!", evt->header_value, isText);
                }        
            }
            else if(strcmp("Last-Modified", evt->header_key)==0)
            {
            }
            else if(strcmp("Content-Length", evt->header_key)==0)
            {
                Debug_printv("* Content len present '%s'", evt->header_value);
            }
            else {
                // Debug_printv("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
                // Last-Modified, value=Thu, 03 Dec 1992 08:37:20 - may be used to get file date
            }

            break;
        case HTTP_EVENT_ON_DATA: // Occurs multiple times when receiving body data from the server. MAY BE SKIPPED IF BODY IS EMPTY!
            Debug_printv("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            {
                int status = esp_http_client_get_status_code(istream->m_http);
                if ((status == HttpStatus_Found || status == HttpStatus_MovedPermanently) /*&& client->_redirect_count < (client->_max_redirects - 1)*/)
                {
                    Debug_printv("HTTP_EVENT_ON_DATA: Ignoring redirect response");

                    break;
                }
                if (!esp_http_client_is_chunked_response(evt->client)) {
                    Debug_printv("HTTP_EVENT_ON_DATA: Got chunked response");
                }
            }


            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            // if (!esp_http_client_is_chunked_response(evt->client)) {
            //     I-
            //     if (evt->user_data) {
            //         memcpy(evt->user_data + output_len, evt->data, evt->data_len);
            //     } else {
            //         if (output_buffer == NULL) {
            //             output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
            //             output_len = 0;
            //             if (output_buffer == NULL) {
            //                 ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
            //                 return ESP_FAIL;
            //             }
            //         }
            //         memcpy(output_buffer + output_len, evt->data, evt->data_len);
            //     }
            //     output_len += evt->data_len;
            // }

            break;




// z fnHttpClient.cpp
// #ifdef VERBOSE_HTTP
//         Debug_printv("HTTP_EVENT_ON_DATA %u\n", uxTaskGetStackHighWaterMark(nullptr));
// #endif
//         // Don't do any of this if we're told to ignore the response
//         if (client->_ignore_response_body == true)
//             break;

//         // esp_http_client will automatically retry redirects, so ignore all but the last attemp
//         int status = esp_http_client_get_status_code(client->_handle);
//         if ((status == HttpStatus_Found || status == HttpStatus_MovedPermanently) && client->_redirect_count < (client->_max_redirects - 1))
//         {
// #ifdef VERBOSE_HTTP
//             Debug_println("HTTP_EVENT_ON_DATA: Ignoring redirect response");
// #endif
//             break;
//         }
//         /*
//          If auth type is set to NONE, esp_http_client will automatically retry auth failures by attempting to set the auth type to
//          BASIC or DIGEST depending on the server response code. Ignore this attempt.
//         */
//         if (status == HttpStatus_Unauthorized && client->_auth_type == HTTP_AUTH_TYPE_NONE && client->_redirect_count == 0)
//         {
// #ifdef VERBOSE_HTTP
//             Debug_println("HTTP_EVENT_ON_DATA: Ignoring UNAUTHORIZED response");
// #endif
//             break;
//         }

//         // Check if this is our first time this event has been triggered
//         if (client->_transaction_begin == true)
//         {
//             client->_transaction_begin = false;
//             client->_transaction_done = false;
//             // Let the main thread know we're done reading headers and have moved on to the data
//             xTaskNotifyGive(client->_taskh_consumer);
//         }

//         // Wait to be told we can fill the buffer
// #ifdef VERBOSE_HTTP
//         Debug_println("HTTP_EVENT_ON_DATA: Waiting to start reading");
// #endif
//         ulTaskNotifyTake(1, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_CONSUMER_TASK));

// #ifdef VERBOSE_HTTP
//        Debug_printv("HTTP_EVENT_ON_DATA: Data: %p, Datalen: %d\n", evt->data, evt->data_len);
// #endif

//         client->_buffer_pos = 0;
//         client->_buffer_len = (evt->data_len > DEFAULT_HTTP_BUF_SIZE) ? DEFAULT_HTTP_BUF_SIZE : evt->data_len;
//         memcpy(client->_buffer, evt->data, client->_buffer_len);

//         // Now let the reader know there's data in the buffer
//         xTaskNotifyGive(client->_taskh_consumer);
//         break;













        case HTTP_EVENT_ON_FINISH: // Occurs when finish a HTTP session
            // This may get called more than once if esp_http_client decides to retry in order to handle a redirect or auth response
            //Debug_printv("HTTP_EVENT_ON_FINISH %u\n", uxTaskGetStackHighWaterMark(nullptr));
            // Keep track of how many times we "finish" reading a response from the server

            Debug_printv("HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED: // The connection has been disconnected
            Debug_printv("HTTP_EVENT_DISCONNECTED");
            // int mbedtls_err = 0;
            // esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            // if (err != 0) {
            //     ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            //     ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            // }
            // if (output_buffer != NULL) {
            //     free(output_buffer);
            //     output_buffer = NULL;
            // }
            // output_len = 0;
            break;
    }
    return ESP_OK;
}
