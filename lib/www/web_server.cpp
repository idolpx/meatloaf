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

#include "web_server.h"

#include "../../include/debug.h"

#include "string_utils.h"

#ifdef ENABLE_CONSOLE
#include "../lib/console/ESP32Console.h"
#endif

#include "proxy/proxy.h"
#include "template/template.h"
#include "webdav/handler.h"
#include "webdav/body_capture.h"
#include "ws/ws.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
//#include "esp_log.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <sys/socket.h>
#include <fcntl.h>
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

#ifndef MIN
#define MIN(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })
#endif
#ifndef MAX
#define MAX(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#endif

HttpServer httpServer;

// Static member definition (required by C++ ODR)
httpd_handle_t HttpServer::s_server = nullptr;

// File-scope state shared between handlers and file-serving methods
static std::string httpdocs;
static std::string uri;
static std::string queryString;

// Forward declarations for file-serving helpers (defined below start_server)
static char *get_extension(const char *filename);
static const char *find_mimetype_str(const char *extension);
static void set_file_content_type(httpd_req_t *req, const char *filepath);
static void send_file(httpd_req_t *req, const char *filename);
static void send_file_parsed(httpd_req_t *req, const char *filename);

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

static esp_err_t get_handler(httpd_req_t *httpd_req)
{
    Debug_printv("uri[%s]", httpd_req->uri);

    uri = mstr::urlDecode(httpd_req->uri);
    queryString = uri.substr(uri.find("?") + 1);
    uri = uri.substr(0, uri.find("?"));

    // If a proxy base is active, forward this request to the remote host.
    // The full request path (including query string) is appended to the base origin.
    if (!proxy_base_url().empty()) {
        std::string full_url = proxy_base_url() + mstr::urlDecode(httpd_req->uri);
        return proxy_fetch(httpd_req, full_url.c_str());
    }

    send_file(httpd_req, uri.c_str());
    return ESP_OK;
}

static esp_err_t post_handler(httpd_req_t *httpd_req)
{
    Debug_printv("uri[%s]", httpd_req->uri);

    char content[100];
    size_t recv_size = MIN(httpd_req->content_len, sizeof(content));

    int ret = httpd_req_recv(httpd_req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            httpd_resp_send_408(httpd_req);
        return ESP_FAIL;
    }

    const char resp[] = "URI POST Response";
    httpd_resp_send(httpd_req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void HttpServer::custom_global_ctx_free(void *ctx)
{
    // keep this commented for the moment to avoid warning.
    // serverstate * ctx_state = (serverstate *)ctx;
}

// Set SO_LINGER(0) on each accepted socket so closing sends TCP RST instead
// of going through TIME_WAIT. Without this, rapid sequential WebDAV requests
// (e.g. Windows Explorer navigating directories) accumulate sockets in
// TIME_WAIT and exhaust CONFIG_LWIP_MAX_SOCKETS (10) within ~10 requests.
// TCP_NODELAY disables Nagle: httpd sends status/headers/chunk framing as
// separate small writes, and Nagle holding those for the client's delayed
// ACK adds latency to every WebDAV round trip (PROPFIND/PUT/PROPPATCH per
// file during bulk transfers).
static esp_err_t httpd_open_fn(httpd_handle_t hd, int sockfd)
{
    struct linger so_linger = { .l_onoff = 1, .l_linger = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    int nodelay = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    // Install the WebDAV raw-body capture (a session recv override) so the
    // WebDAV handler can read chunked request bodies that esp_http_server
    // cannot deliver. Bounded and reset per request; harmless on other
    // connections (see body_capture.h).
    WebDav::body_capture_install(hd, sockfd);
    return ESP_OK;
}

httpd_handle_t HttpServer::start_server(serverstate &state)
{
    if (!fnWiFi.connected())
    {
        Debug_println("WiFi not connected - aborting web server startup");
        return nullptr;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = 12;
    config.core_id = 0;
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.max_resp_headers = 16;
    config.max_open_sockets = 10;
    config.keep_alive_enable = false;
    // Purge the least-recently-used idle session when the pool is full so a
    // new client can always connect. Browsers hold ~6 idle keep-alive
    // connections; without this, browser + WebDAV client together fill all
    // 10 slots and lock everyone else out.
    config.lru_purge_enable = true;
    config.open_fn = httpd_open_fn;
    config.uri_match_fn = httpd_uri_match_wildcard;

    config.global_user_ctx = (void *)&state;
    config.global_user_ctx_free_fn = (httpd_free_ctx_fn_t)custom_global_ctx_free;

    Debug_printf("Starting web server on port %d\r\n", config.server_port);

    httpd_uri_t uri_get = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = get_handler,
        .user_ctx = NULL,
        .is_websocket = false
    };

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

    esp_err_t start_err = httpd_start(&(state.hServer), &config);
    if (start_err == ESP_OK)
    {
        ws_register(state.hServer);
        proxy_register(state.hServer);
        //webdav_register(state.hServer, "/dav", "/");
        webdav_register(state.hServer);

        httpd_register_uri_handler(state.hServer, &uri_get);
        httpd_register_uri_handler(state.hServer, &uri_post);

        s_server = state.hServer;
        printf(ANSI_GREEN_BOLD "WWW/WS/WebDAV Server Started!" ANSI_RESET "\r\n");
    }
    else
    {
        state.hServer = NULL;
        printf(ANSI_RED_BOLD "WWW/WS/WebDAV Server FAILED to start!" ANSI_RESET
               " err=%s free_internal=%u largest_internal_block=%u\r\n",
               esp_err_to_name(start_err),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }

    return state.hServer;
}

static const char *find_mimetype_str(const char *extension)
{
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

static char *get_extension(const char *filename)
{
    char *result = strrchr(filename, '.');
    if (result != NULL)
        return ++result;
    return NULL;
}

static void set_file_content_type(httpd_req_t *req, const char *filepath)
{
    char *dot = get_extension(filepath);
    const char *mimetype = find_mimetype_str(dot);
    httpd_resp_set_type(req, mimetype);
}

static void send_file(httpd_req_t *req, const char *filename)
{
    std::string fpath;
    Debug_printv("filename[%s]", filename);
    Debug_memory();
    if ( !exists(filename) )
    {
        while (*filename == '/')
            filename++;
    }
    fpath = filename;
    if ( !exists(fpath) )
        fpath = httpdocs + std::string(filename);

    std::string default_index = "index.html";
    if (fpath.size() == 1 && fpath[0] == '/')
        fpath = httpdocs + default_index;
    else if (fpath.back() == '/')
        fpath += default_index;
    else {
        if (make_mfile(fpath)->isDirectory())
            fpath += "/" + default_index;
    }

    Debug_printv("fpath[%s]", fpath.c_str());

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");

    if (is_parsable(get_extension(filename)))
        return send_file_parsed(req, fpath.c_str());

    std::string content_type_path = fpath;
    if (!mstr::endsWith(fpath, ".gz")) {
        std::string gz_path = fpath + ".gz";
        if (exists(gz_path)) {
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            fpath = gz_path;
        }
    }

    auto mfile = make_mfile(fpath);
    auto stream = mfile->getSourceStream(std::ios_base::in);

    if (!stream || !stream->isOpen())
    {
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
        HttpServer::send_http_error(req, 404);
        return;
    }

    set_file_content_type(req, content_type_path.c_str());

    uint32_t fsize = stream->size();

    if (fsize > 0)
    {
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

    char *buf = (char *)psram_malloc(http_SEND_BUFF_SIZE);
    if (buf == nullptr)
    {
        stream->close();
        HttpServer::send_http_error(req, 500);
        return;
    }
    uint32_t count;
    while ((count = stream->read((uint8_t *)buf, http_SEND_BUFF_SIZE)) > 0)
        httpd_resp_send_chunk(req, buf, count);
    httpd_resp_send_chunk(req, NULL, 0);
    stream->close();
    free(buf);
}

static void send_file_parsed(httpd_req_t *req, const char *filename)
{
    Debug_printv("filename[%s]", filename);

    auto mfile = make_mfile(std::string(filename));
    auto stream = mfile->getSourceStream(std::ios_base::in);

    if (!stream || !stream->isOpen())
    {
        Debug_printv("Failed to open file for parsing: [%s]", filename);
        httpd_resp_set_type(req, "text/plain");
        char msg[16];
        snprintf(msg, sizeof(msg), "Error %d", 404);
        httpd_resp_send(req, msg, strlen(msg));
        return;
    }

    set_file_content_type(req, filename);

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
                    if (pos < pending.size())
                        httpd_resp_send_chunk(req, pending.c_str() + pos, pending.size() - pos);
                    pos = pending.size();
                }
                else
                {
                    size_t safe_end = pending.size() > 0 ? pending.size() - 1 : 0;
                    if (safe_end > pos)
                    {
                        httpd_resp_send_chunk(req, pending.c_str() + pos, safe_end - pos);
                        pos = safe_end;
                    }
                }
                break;
            }

            size_t close = pending.find("}}", open + 2);

            if (close == std::string::npos)
            {
                if (open > pos)
                    httpd_resp_send_chunk(req, pending.c_str() + pos, open - pos);
                pos = open;

                if (done)
                {
                    httpd_resp_send_chunk(req, pending.c_str() + pos, pending.size() - pos);
                    pos = pending.size();
                }
                break;
            }

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
    httpd_resp_send_chunk(req, nullptr, 0);
    stream->close();
}

void HttpServer::send_http_error(httpd_req_t *req, int errnum)
{
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

    std::string gz_path = error_path + ".gz";
    if (exists(gz_path)) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        set_file_content_type(req, error_path.c_str());
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
}

void HttpServer::start()
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

    start_server(state);
}

// ---- Lazy start ------------------------------------------------------------
// At idle, port 80 is held by a bare listen socket serviced by a small task —
// no httpd task (8 KB stack) and no per-socket session buffers exist until the
// server is actually accessed. On the first connection the listener closes its
// socket, starts the real httpd server, and transparently proxies the already-
// accepted connection(s) to it over loopback (CONFIG_LWIP_NETIF_LOOPBACK=y).
// Proxying keeps the handover invisible to browsers (which open several
// parallel connections at once) and to WebDAV clients (which do not follow
// redirects), so the very first request is served normally.

int HttpServer::_lazy_sock = -1;
TaskHandle_t HttpServer::_lazy_task = NULL;

// Pipe one accepted connection to the real (now running) server. Reads the
// request headers, rewrites Connection to "close" so the exchange ends after
// one response (the client then reconnects directly to the real server), and
// pumps bytes both ways until either side closes.
static void lazy_proxy_client(int client, int wait_ms)
{
    struct timeval tv = { .tv_sec = wait_ms / 1000, .tv_usec = (wait_ms % 1000) * 1000 };
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[512];
    int n = recv(client, buf, sizeof(buf), 0);
    if (n <= 0)
    {
        // No request bytes (speculative browser preconnect, port probe,
        // telnet): just close; the client reconnects to the real server.
        close(client);
        return;
    }

    std::string req(buf, n);

    // Headers can span several segments; keep reading briefly until we have
    // them all (needed to patch the Connection header below).
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 4096)
    {
        n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        req.append(buf, n);
    }

    // Force the proxied exchange to end after one response by rewriting the
    // Connection header (only when the full header block was captured).
    size_t hdr_end = req.find("\r\n\r\n");
    if (hdr_end != std::string::npos)
    {
        size_t c = req.find("\r\nConnection:");
        if (c == std::string::npos)
            c = req.find("\r\nconnection:");
        if (c != std::string::npos && c < hdr_end)
        {
            size_t eol = req.find("\r\n", c + 2);
            req.replace(c, eol - c, "\r\nConnection: close");
        }
        else
        {
            req.insert(hdr_end + 2, "Connection: close\r\n");
        }
    }

    // Connect to the real server through loopback on our own address.
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = inet_addr(fnSystem.Net.get_ip4_address_str().c_str());
    int upstream = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (upstream < 0 || connect(upstream, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        Debug_printv("lazy proxy connect failed: errno %d", errno);
        if (upstream >= 0)
            close(upstream);
        close(client);
        return;
    }

    send(upstream, req.c_str(), req.size(), 0);

    // Pump bytes both ways until either side closes or traffic idles out.
    while (true)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(client, &fds);
        FD_SET(upstream, &fds);
        struct timeval idle = { .tv_sec = 10, .tv_usec = 0 };
        if (select(MAX(client, upstream) + 1, &fds, NULL, NULL, &idle) <= 0)
            break;

        if (FD_ISSET(client, &fds))
        {
            n = recv(client, buf, sizeof(buf), 0);
            if (n <= 0 || send(upstream, buf, n, 0) < 0)
                break;
        }
        if (FD_ISSET(upstream, &fds))
        {
            n = recv(upstream, buf, sizeof(buf), 0);
            if (n <= 0 || send(client, buf, n, 0) < 0)
                break;
        }
    }
    close(upstream);
    close(client);
}

void HttpServer::lazy_task(void *pv)
{
    HttpServer *self = (HttpServer *)pv;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(80);

    int client = -1;
    int extra[4];
    int extras = 0;
    _lazy_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_lazy_sock >= 0)
    {
        // Accepted clients inherit SO_REUSEADDR from the listen socket; both
        // their pcbs and httpd's listen socket need it set so httpd can bind
        // port 80 while those connections are still established.
        int enable = 1;
        setsockopt(_lazy_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

        if (bind(_lazy_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            listen(_lazy_sock, 4) == 0)
        {
            struct sockaddr_in source;
            socklen_t len = sizeof(source);
            client = accept(_lazy_sock, (struct sockaddr *)&source, &len);
            if (client >= 0)
            {
                // Browsers open several parallel connections; grab any that
                // are already queued so they don't get RST when the listen
                // socket closes below.
                fcntl(_lazy_sock, F_SETFL, O_NONBLOCK);
                while (extras < (int)(sizeof(extra) / sizeof(extra[0])))
                {
                    len = sizeof(source);
                    int s = accept(_lazy_sock, (struct sockaddr *)&source, &len);
                    if (s < 0)
                        break;
                    extra[extras++] = s;
                }
            }
        }
    }

    // Free port 80 before starting the real server. stop() also closes this
    // socket to shut the lazy listener down; accept() then returns -1.
    if (_lazy_sock >= 0)
    {
        close(_lazy_sock);
        _lazy_sock = -1;
    }

    if (client >= 0)
    {
        // Real httpd takes over port 80, then the already-accepted
        // connections are piped to it.
        self->start();

        lazy_proxy_client(client, 1000);
        for (int i = 0; i < extras; i++)
            lazy_proxy_client(extra[i], 250);
    }

    _lazy_task = NULL;
    vTaskDelete(NULL);
}

void HttpServer::startOnDemand()
{
    if (state.hServer != NULL || _lazy_task != NULL)
        return;

    if (xTaskCreatePinnedToCore(lazy_task, "www_lazy", 6144, this, 5, &_lazy_task, 0) == pdTRUE)
    {
        printf(ANSI_GREEN_BOLD "WWW/WS/WebDAV Server on standby (starts on first request)" ANSI_RESET "\r\n");
    }
    else
    {
        _lazy_task = NULL;
        start();
    }
}

void HttpServer::stop()
{
    // Shut down the lazy listener if the server was never accessed. Closing
    // the socket unblocks accept() and lazy_task exits on its own.
    if (_lazy_sock >= 0)
    {
        close(_lazy_sock);
        _lazy_sock = -1;
    }

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
