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

#ifndef MIN_CONFIG
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "httpd_server.h"

#include "../../include/debug.h"

#include "string_utils.h"

#ifdef ENABLE_CONSOLE
#include "../lib/console/ESP32Console.h"
#endif

#include "httpd_ws.h"
#include "httpd_webdav.h"
#include "httpd_proxy.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
//#include "esp_log.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <sys/socket.h>
#include <sstream>

#include "meatloaf.h"
#include "device/flash.h"

// Prefer PSRAM for large I/O buffers to keep internal RAM free.
// Falls back to internal heap if PSRAM is unavailable or exhausted.
static inline void *psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"

#include "template.h"

#define MIN(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

cHttpdServer oHttpdServer;

// Static member definitions (required by C++ ODR — declarations in the header are not enough)
std::string cHttpdServer::httpdocs;
std::string cHttpdServer::uri;
std::string cHttpdServer::queryString;
httpd_handle_t cHttpdServer::s_server = nullptr;


static std::unique_ptr<MFile> make_mfile(const std::string &path)
{
    if (path.find("://") != std::string::npos)
        return std::unique_ptr<MFile>(MFSOwner::File(path));
    return std::make_unique<FlashMFile>(path);
}

bool exists(std::string path)
{
    return make_mfile(path)->exists();
}

/* Our URI handler function to be called during GET /uri request */
esp_err_t cHttpdServer::get_handler(httpd_req_t *httpd_req)
{
    Debug_printv("uri[%s]", httpd_req->uri);


    uri = mstr::urlDecode(httpd_req->uri);
    // if (uri == "/")
    // {
    //     uri = "/index.html";
    // }
    //Debug_printv("uri[%s]", uri.c_str());

    // Remove query string from uri
    queryString = uri.substr(uri.find("?") + 1);
    uri = uri.substr(0, uri.find("?"));

    // // Ignore OSX/WIN junk files
    // if (mstr::isJunk(uri))
    // {
    //     send_http_error(httpd_req, 404);
    // }
    // else
    {
        send_file(httpd_req, uri.c_str());
    }

    return ESP_OK;
}

/* Our URI handler function to be called during POST /uri request */
esp_err_t cHttpdServer::post_handler(httpd_req_t *httpd_req)
{
    Debug_printv("uri[%s]", httpd_req->uri);

    /* Destination buffer for content of HTTP POST request.
     * httpd_req_recv() accepts char* only, but content could
     * as well be any binary data (needs type casting).
     * In case of string data, null termination will be absent, and
     * content length would give length of string */
    char content[100];

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(httpd_req->content_len, sizeof(content));

    int ret = httpd_req_recv(httpd_req, content, recv_size);
    if (ret <= 0)
    { /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(httpd_req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    /* Send a simple response */
    const char resp[] = "URI POST Response";
    httpd_resp_send(httpd_req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void cHttpdServer::custom_global_ctx_free(void *ctx)
{
    // keep this commented for the moment to avoid warning.
    // serverstate * ctx_state = (serverstate *)ctx;
    // We could do something fancy here, but we don't need to do anything
}

// Set SO_LINGER(0) on each accepted socket so closing sends TCP RST instead
// of going through TIME_WAIT. Without this, rapid sequential WebDAV requests
// (e.g. Windows Explorer navigating directories) accumulate sockets in
// TIME_WAIT and exhaust CONFIG_LWIP_MAX_SOCKETS (10) within ~10 requests.
static esp_err_t httpd_open_fn(httpd_handle_t hd, int sockfd)
{
    struct linger so_linger = { .l_onoff = 1, .l_linger = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    return ESP_OK;
}

httpd_handle_t cHttpdServer::start_server(serverstate &state)
{
    if (!fnWiFi.connected())
    {
        Debug_println("WiFi not connected - aborting web server startup");
        return nullptr;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = 12; // Bump this higher than fnService loop
    config.core_id = 0; // Pin to CPU core 0
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.max_resp_headers = 16;
    config.max_open_sockets = 10;      // Leave headroom in lwIP pool for WiFi/mDNS/listen socket
    config.keep_alive_enable = false; // Send Connection: close so WebDAV clients free sockets immediately
    config.open_fn = httpd_open_fn;   // SO_LINGER=0: RST on close, bypasses TIME_WAIT
    config.uri_match_fn = httpd_uri_match_wildcard;

    //  Keep a reference to our object
    config.global_user_ctx = (void *)&state;

    // Set our own global_user_ctx free function, otherwise the library will free an object we don't want freed
    config.global_user_ctx_free_fn = (httpd_free_ctx_fn_t)custom_global_ctx_free;

    Debug_printf("Starting web server on port %d\r\n", config.server_port);
    // Debug_printf("Starting web server on port %d, CPU Core %d\r\n", config.server_port, config.core_id);

    // esp_log_level_set("httpd_uri", ESP_LOG_DEBUG);

    /* URI handler structure for GET /uri */
    httpd_uri_t uri_get = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = get_handler,
        .user_ctx = NULL,
        .is_websocket = false
    };

    /* URI handler structure for POST /uri */
    httpd_uri_t uri_post = {
        .uri = "/*",
        .method = HTTP_POST,
        .handler = post_handler,
        .user_ctx = NULL,
        .is_websocket = false
    };

    // Set the default folder for http docs
    httpdocs = WWW_ROOT "/";
    if (exists("/sd" WWW_ROOT))
        httpdocs = "/sd" WWW_ROOT "/";

    if (httpd_start(&(state.hServer), &config) == ESP_OK)
    {
        // Register proxy handlers
        proxy_register(state.hServer);

        // Register WebSocket handlers
        websocket_register(state.hServer);
        
        // Register WebDAV handlers
        //webdav_register(state.hServer, "/dav", "/");
        webdav_register(state.hServer);

        // Default handlers
        httpd_register_uri_handler(state.hServer, &uri_get);
        httpd_register_uri_handler(state.hServer, &uri_post);

        s_server = state.hServer;
        printf(ANSI_GREEN_BOLD "WWW/PROXY/WS/WebDAV Server Started!" ANSI_RESET "\r\n");
    }
    else
    {
        state.hServer = NULL;
        printf(ANSI_RED_BOLD "WWW/PROXY/WS/WebDAV Server FAILED to start!" ANSI_RESET "\r\n");
    }

    return state.hServer;
}

const char *cHttpdServer::find_mimetype_str(const char *extension)
{
    // Plain array in flash — no heap allocation, no map-node overhead.
    static const struct { const char *ext; const char *mime; } mime_table[] = {
        { "html", HTTPD_TYPE_TEXT       },
        { "htm",  HTTPD_TYPE_TEXT       },
        { "css",  "text/css"            },
        { "txt",  "text/plain"          },
        { "js",   "text/javascript"     },
        { "xml",  "text/xml"            },
        { "gif",  "image/gif"           },
        { "ico",  "image/x-icon"        },
        { "jpg",  "image/jpeg"          },
        { "png",  "image/png"           },
        { "svg",  "image/svg+xml"       },
        { "json", HTTPD_TYPE_JSON       },
        { "pdf",  "application/pdf"     },
        { "gz",   "application/x-gzip" },
    };

    if (extension != NULL)
    {
        for (size_t i = 0; i < sizeof(mime_table) / sizeof(mime_table[0]); i++)
        {
            if (strcmp(extension, mime_table[i].ext) == 0)
                return mime_table[i].mime;
        }
    }
    return HTTPD_TYPE_OCTET;
}

char *cHttpdServer::get_extension(const char *filename)
{
    char *result = strrchr(filename, '.');
    if (result != NULL)
        return ++result;
    return NULL;
}

// Set the response content type based on the file being sent.
// Just using the file extension
// If nothing is set here, the default is 'text/html'
void cHttpdServer::set_file_content_type(httpd_req_t *req, const char *filepath)
{
    // Find the current file extension
    char *dot = get_extension(filepath);
    // if (dot != NULL)
    // {
        const char *mimetype = find_mimetype_str(dot);
        // if (mimetype)
        // {
            httpd_resp_set_type(req, mimetype);
            //Debug_printv("mimetype[%s]", mimetype);
        //}
    //}
}

// Send content of given file out to client
void cHttpdServer::send_file(httpd_req_t *req, const char *filename)
{
    // Build the full file path
    std::string fpath;
    // Trim any '/' prefix before adding it to the base directory
    Debug_printv("filename[%s]", filename);
    if ( !exists(filename) )
    {
        while (*filename == '/')
            filename++;
    }
    fpath = filename;
    if ( !exists(fpath) )
        fpath = httpdocs + std::string(filename);

    // If filename is just '/', serve index.html
    std::string default_index = "index.html";
    if (fpath.size() == 1 && fpath[0] == '/')
        fpath = httpdocs + default_index;

    // If filename is a directory, look for index.html within it
    else if (fpath.back() == '/')
        fpath += default_index;
    else {
        if (make_mfile(fpath)->isDirectory())
            fpath += "/" + default_index;
    }

    Debug_printv("fpath[%s]", fpath.c_str());

    // Allow CORS for all files
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");

    // Handle file differently if it's one of the types we parse
    if (is_parsable(get_extension(filename)))
        return send_file_parsed(req, fpath.c_str());

    // Prefer pre-compressed .gz version if it exists. Content-Type must still
    // reflect the original file (e.g. text/javascript for app.js.gz), so save
    // fpath before potentially switching to the .gz variant.
    std::string content_type_path = fpath;
    if (!mstr::endsWith(fpath, ".gz")) { // Don't try to .gz a .gz file
        std::string gz_path = fpath + ".gz";
        if (exists(gz_path)) {
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            fpath = gz_path;
        }
    }

    // Try flash first (make_mfile uses FlashMFile for bare paths, preserving .gz passthrough).
    auto mfile = make_mfile(fpath);
    auto stream = mfile->getSourceStream(std::ios_base::in);

    if (!stream || !stream->isOpen())
    {
        // Flash failed. Try MFSOwner::File on the original request URI so that
        // .config redirects are applied (e.g. base_url=ftp://zimmers.net means
        // /zimmers.net/pub/00INDEX is fetched from ftp://zimmers.net/pub/00INDEX).
        // MFSOwner already prevents .config files themselves from being redirected,
        // so requesting /some/path/.config still returns 404 when not in flash.
        auto mfile_net = std::unique_ptr<MFile>(MFSOwner::File(uri));
        if (mfile_net) {
            auto net_stream = mfile_net->getSourceStream(std::ios_base::in);
            if (net_stream && net_stream->isOpen()) {
                mfile = std::move(mfile_net);
                stream = net_stream;
                content_type_path = uri;
            }
        }
    }

    if (!stream || !stream->isOpen())
    {
        Debug_printv("Failed to open file for sending: [%s]", fpath.c_str());
        send_http_error(req, 404);
        return;
    }

    // Set the response content type from the uncompressed filename
    set_file_content_type(req, content_type_path.c_str());

    uint32_t fsize = stream->size();

    if (fsize > 0)
    {
        // Allocate a PSRAM buffer for the full file and send with Content-Length.
        char *buf = (char *)psram_malloc((size_t)fsize);
        if (buf != nullptr)
        {
            size_t got = stream->read((uint8_t *)buf, fsize);
            stream->close();
            httpd_resp_send(req, buf, (ssize_t)got);
            free(buf);
            return;
        }
    }

    // Fallback: size unknown or PSRAM exhausted — use chunked transfer
    char *buf = (char *)psram_malloc(http_SEND_BUFF_SIZE);
    if (buf == nullptr)
    {
        stream->close();
        send_http_error(req, 500);
        return;
    }
    uint32_t count;
    while ((count = stream->read((uint8_t *)buf, http_SEND_BUFF_SIZE)) > 0)
        httpd_resp_send_chunk(req, buf, count);
    httpd_resp_send_chunk(req, NULL, 0);
    stream->close();
    free(buf);
}

// Send file content after parsing for replaceable strings.
// Streams in http_SEND_BUFF_SIZE chunks to avoid loading the entire file into RAM.
// Handles {{ tag }} substitution even when a tag spans a chunk boundary.
void cHttpdServer::send_file_parsed(httpd_req_t *req, const char *filename)
{
    Debug_printv("filename[%s]", filename);

    auto mfile = make_mfile(std::string(filename));
    auto stream = mfile->getSourceStream(std::ios_base::in);

    if (!stream || !stream->isOpen())
    {
        Debug_printv("Failed to open file for parsing: [%s]", filename);
        // Do NOT call send_http_error() — error pages route through send_file_parsed() too
        httpd_resp_set_type(req, "text/plain");
        char msg[16];
        snprintf(msg, sizeof(msg), "Error %d", 404);
        httpd_resp_send(req, msg, strlen(msg));
        return;
    }

    set_file_content_type(req, filename);

    // Read buffer in PSRAM to keep internal RAM free.
    char *buf = (char *)psram_malloc(http_SEND_BUFF_SIZE);
    if (buf == nullptr)
    {
        stream->close();
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Error 500", 9);
        return;
    }

    std::string pending;
    bool done = false;

    while (!done)
    {
        size_t count = stream->read((uint8_t *)buf, http_SEND_BUFF_SIZE);
        if (count > 0)
            pending.append(buf, count);
        else
            done = true;

        size_t pos = 0;
        while (true)
        {
            size_t open = pending.find("{{", pos);

            if (open == std::string::npos)
            {
                if (done)
                {
                    // EOF — flush everything remaining
                    if (pos < pending.size())
                        httpd_resp_send_chunk(req, pending.c_str() + pos, pending.size() - pos);
                    pos = pending.size();
                }
                else
                {
                    // Keep the last character: it could be the leading '{' of '{{'
                    size_t safe_end = pending.size() > 0 ? pending.size() - 1 : 0;
                    if (safe_end > pos)
                    {
                        httpd_resp_send_chunk(req, pending.c_str() + pos, safe_end - pos);
                        pos = safe_end;
                    }
                }
                break;
            }

            // Found '{{' — look for closing '}}'
            size_t close = pending.find("}}", open + 2);

            if (close == std::string::npos)
            {
                // Closing tag not yet in buffer; send pre-tag text and wait for more data
                if (open > pos)
                    httpd_resp_send_chunk(req, pending.c_str() + pos, open - pos);
                pos = open;

                if (done)
                {
                    // Unclosed tag at EOF — emit as literal
                    httpd_resp_send_chunk(req, pending.c_str() + pos, pending.size() - pos);
                    pos = pending.size();
                }
                break;
            }

            // Complete {{ tag }} found — send pre-tag text then substitution
            if (open > pos)
                httpd_resp_send_chunk(req, pending.c_str() + pos, open - pos);

            std::string tag = pending.substr(open + 2, close - open - 2);
            std::string substitution = substitute_tag(tag);
            if (!substitution.empty())
                httpd_resp_send_chunk(req, substitution.c_str(), substitution.size());

            pos = close + 2;
        }

        pending.erase(0, pos);
    }

    free(buf);
    httpd_resp_send_chunk(req, nullptr, 0); // terminate chunked response
    stream->close();
}

// Send some meaningful(?) error message to client.
// NOTE: Must NOT call send_file()
void cHttpdServer::send_http_error(httpd_req_t *req, int errnum)
{
    // Set the HTTP status code before sending the body.
    switch (errnum) {
        case 404: httpd_resp_set_status(req, HTTPD_404); break;
        case 500: httpd_resp_set_status(req, HTTPD_500); break;
        default: {
            static char _status[32];
            snprintf(_status, sizeof(_status), "%d Error", errnum);
            httpd_resp_set_status(req, _status);
            break;
        }
    }

    std::string error_path = httpdocs + "error/" + std::to_string(errnum) + ".html";
    Debug_printv("Error %d, looking for error page [%s]", errnum, error_path.c_str());

    // Prefer pre-compressed error page if available. Template substitution
    // cannot run on compressed content, so serve it directly in that case.
    std::string gz_path = error_path + ".gz";
    if (exists(gz_path)) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        set_file_content_type(req, error_path.c_str()); // text/html from .html name
        auto mfGz = make_mfile(gz_path);
        auto gzStream = mfGz->getSourceStream(std::ios_base::in);
        if (gzStream && gzStream->isOpen()) {
            char *buf = (char *)psram_malloc(http_SEND_BUFF_SIZE);
            if (buf != nullptr) {
                uint32_t count;
                while ((count = gzStream->read((uint8_t *)buf, http_SEND_BUFF_SIZE)) > 0)
                    httpd_resp_send_chunk(req, buf, count);
                httpd_resp_send_chunk(req, nullptr, 0);
                free(buf);
            }
            gzStream->close();
            return;
        }
    }

    send_file_parsed(req, error_path.c_str());

    // serverstate *pState = (serverstate *)httpd_get_global_user_ctx(req->handle);
    // FILE *file = pState->_FS->file_open(error_path.c_str());

    // if (file != nullptr)
    // {
    //     set_file_content_type(req, error_path.c_str());
    //     char *buf = (char *)psram_malloc(http_SEND_BUFF_SIZE);
    //     if (buf != nullptr)
    //     {
    //         size_t count = 0;
    //         do {
    //             count = fread(buf, 1, http_SEND_BUFF_SIZE, file);
    //             httpd_resp_send_chunk(req, buf, count);
    //         } while (count > 0);
    //         free(buf);
    //     }
    //     fclose(file);
    // }
    // else
    // {
    //     // Fallback: plain text — no further recursion possible
    //     httpd_resp_set_type(req, "text/plain");
    //     char msg[16];
    //     snprintf(msg, sizeof(msg), "Error %d", errnum);
    //     httpd_resp_send(req, msg, strlen(msg));
    // }
}

/* Set up and start the web server
 */
void cHttpdServer::start()
{
    if (state.hServer != NULL)
    {
        Debug_println("httpServiceInit: We already have a web server handle - aborting");
        return;
    }

    // Register event notifications to let us know when WiFi is up/down
    // Missing the constants used here.  Need to find that...
    // esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &(state.hServer));
    // esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &(state.hServer));

    // Go ahead and attempt starting the server for the first time
    start_server(state);
}

void cHttpdServer::stop()
{
    if (state.hServer != nullptr)
    {
        Debug_print("Stopping web service...");
        if (httpd_stop(state.hServer) == ESP_OK)
        {
            Debug_println("done!");
        }
        else
        {
            Debug_println("error!");
        }
        state.hServer = nullptr;
        s_server = nullptr;
    }
}

#endif