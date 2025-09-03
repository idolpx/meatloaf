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

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "httpd_server.h"

#include "../../include/debug.h"

#include "string_utils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
//#include "esp_log.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <sys/stat.h>
//#include <cstdlib>
#include <sstream>

// WebDAV
#include "webdav/webdav_server.h"
#include "webdav/request.h"
#include "webdav/response.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"

#include "fsFlash.h"
#include "fnFsSD.h"

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

std::string httpdocs;

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

long file_size(FILE *fd)
{
    struct stat stat_buf;
    int rc = fstat((int)fd, &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

bool exists(std::string path)
{
    //Debug_printv( "path[%s]", path.c_str() );
    struct stat st;
    int i = stat(path.c_str(), &st);
    return (i == 0);
}

/* Our URI handler function to be called during GET /uri request */
esp_err_t cHttpdServer::get_handler(httpd_req_t *httpd_req)
{
    //Debug_printv("uri[%s]", httpd_req->uri);


    std::string uri = mstr::urlDecode(httpd_req->uri);
    if (uri == "/")
    {
        uri = "/index.html";
    }
    //Debug_printv("uri[%s]", uri.c_str());

    // Remove query string from uri
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
    Debug_printv("url[%s]", httpd_req->uri);

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


esp_err_t cHttpdServer::websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        Debug_printv("Handshake done, the new connection was opened");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        Debug_printv("httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    Debug_printv("frame len is %d", ws_pkt.len);
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            Debug_printv("Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            Debug_printv("httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        Debug_printv("Got packet with message: %s", ws_pkt.payload);

        Debug_printv("Packet type: %d", ws_pkt.type);

        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
            strcmp((char*)ws_pkt.payload,"Trigger async") == 0) {
            free(buf);
            return websocket_trigger_async_send(req->handle, req);
        }
    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        Debug_printv("httpd_ws_send_frame failed with %d", ret);
    }
    free(buf);
    return ret;
}

esp_err_t cHttpdServer::websocket_trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = (async_resp_arg *)malloc(sizeof(struct async_resp_arg));
    if (resp_arg == NULL) {
        return ESP_ERR_NO_MEM;
    }
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    esp_err_t ret = httpd_queue_work(handle, websocket_async_send, resp_arg);
    if (ret != ESP_OK) {
        free(resp_arg);
    }
    return ret;
}

void cHttpdServer::websocket_async_send(void *arg)
{
    static const char * data = "Async data";
    struct async_resp_arg *resp_arg = (async_resp_arg *)arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)data;
    ws_pkt.len = strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

esp_err_t cHttpdServer::webdav_handler(httpd_req_t *httpd_req)
{
    WebDav::Server *server = (WebDav::Server *)httpd_req->user_ctx;
    WebDav::Request req(httpd_req);
    WebDav::Response resp(httpd_req);
    int ret;

    //Debug_printv("url[%s]", httpd_req->uri);

    // // Ignore OSX/WIN junk files
    // std::string uri = httpd_req->uri;
    // if ( mstr::isJunk(uri) )
    // {
    //     resp.setStatus(404); // Not Found
    //     resp.flushHeaders();
    //     resp.closeBody();
    //     return ESP_OK;
    // }

    if ( !req.parseRequest() )
    {
        resp.setStatus(400); // Bad Request
        resp.flushHeaders();
        resp.closeBody();
        return ESP_OK;
    }

    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Origin", "*");
    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Headers", "*");
    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Methods", "*");

    Debug_printv("%d %s[%s]", httpd_req->method, http_method_str((enum http_method)httpd_req->method), httpd_req->uri);

    switch (httpd_req->method)
    {
    case HTTP_COPY:
        ret = server->doCopy(req, resp);
        break;
    case HTTP_DELETE:
        ret = server->doDelete(req, resp);
        break;
    case HTTP_GET:
        ret = server->doGet(req, resp);
        if ( ret == 200 )
            return ESP_OK;
        break;
    case HTTP_HEAD:
        ret = server->doHead(req, resp);
        break;
    case HTTP_LOCK:
        ret = server->doLock(req, resp);
        if ( ret == 200 )
            return ESP_OK;
        break;
    case HTTP_MKCOL:
        ret = server->doMkcol(req, resp);
        break;
    case HTTP_MOVE:
        ret = server->doMove(req, resp);
        break;
    case HTTP_OPTIONS:
        ret = server->doOptions(req, resp);
        break;
    case HTTP_PROPFIND:
        ret = server->doPropfind(req, resp);
        if (ret == 207)
            return ESP_OK;
        break;
    case HTTP_PROPPATCH:
        ret = server->doProppatch(req, resp);
        break;
    case HTTP_PUT:
        ret = server->doPut(req, resp);
        break;
    case HTTP_UNLOCK:
        ret = server->doUnlock(req, resp);
        break;
    default:
        return ESP_ERR_HTTPD_INVALID_REQ;
        break;
    }

    resp.setStatus(ret);

    if ( (ret > 399) & (httpd_req->method != HTTP_HEAD) )
    {
        // Send error page
        send_http_error(httpd_req, ret);
    }
    else
    {
        // Send empty response
        resp.flushHeaders();
        resp.closeBody();
    }

    Debug_printv("ret[%d]", ret);

    return ESP_OK;
}

void cHttpdServer::websocket_register(httpd_handle_t server)
{
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };

    httpd_register_uri_handler(server, &ws);
}

void cHttpdServer::webdav_register(httpd_handle_t server, const char *root_uri, const char *root_path)
{
    WebDav::Server *webDavServer = new WebDav::Server(root_uri, root_path);

    char *uri;
    if ( strlen(root_uri ) > 1 )
        asprintf(&uri, "%s/?*", root_uri);
    else
        asprintf(&uri, "/?*");

    httpd_uri_t uri_dav = {
        .uri = uri,
        .method = http_method(0),
        .handler = webdav_handler,
        .user_ctx = webDavServer,
        .is_websocket = false
    };

    http_method methods[] = {
        HTTP_COPY,
        HTTP_DELETE,
//        HTTP_GET,
        HTTP_HEAD,
        HTTP_LOCK,
        HTTP_MKCOL,
        HTTP_MOVE,
        HTTP_OPTIONS,
        HTTP_PROPFIND,
        HTTP_PROPPATCH,
        HTTP_PUT,
        HTTP_UNLOCK,
    };

    for (int i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
    {
        uri_dav.method = methods[i];
        httpd_register_uri_handler(server, &uri_dav);
    }
}

void cHttpdServer::custom_global_ctx_free(void *ctx)
{
    // keep this commented for the moment to avoid warning.
    // serverstate * ctx_state = (serverstate *)ctx;
    // We could do something fancy here, but we don't need to do anything
}

httpd_handle_t cHttpdServer::start_server(serverstate &state)
{
    if (!fnWiFi.connected())
    {
        Debug_println("WiFi not connected - aborting web server startup");
        return nullptr;
    }

    // Set filesystem where we expect to find our static files
    state._FS = &fsFlash;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = 12; // Bump this higher than fnService loop
    config.core_id = 0; // Pin to CPU core 0
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.max_resp_headers = 16;
    config.keep_alive_enable = true;
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
        /* Register URI handlers */
        websocket_register(state.hServer);
        //webdav_register(state.hServer, "/dav", "/");
        webdav_register(state.hServer);

        // Default handlers
        httpd_register_uri_handler(state.hServer, &uri_get);
        httpd_register_uri_handler(state.hServer, &uri_post);

        printf(ANSI_GREEN_BOLD "WWW/WS/WebDAV Server Started!" ANSI_RESET "\r\n");
    }
    else
    {
        state.hServer = NULL;
        printf(ANSI_RED_BOLD "WWW/WS/WebDAV Server FAILED to start!" ANSI_RESET "\r\n");
    }

    return state.hServer;
}

const char *cHttpdServer::find_mimetype_str(const char *extension)
{
    static std::map<std::string, std::string> mime_map
    {
        {"html", HTTPD_TYPE_TEXT},
        {"htm", HTTPD_TYPE_TEXT},

        {"css", "text/css"},
        {"txt", "text/plain"},
        {"js",  "text/javascript"},
        {"xml", "text/xml"},

        {"gif", "image/gif"},
        {"ico", "image/x-icon"},
        {"jpg", "image/jpeg"},
        {"png", "image/png"},
        {"svg", "image/svg+xml"},

        // {"ttf", "application/x-font-ttf"},
        // {"otf", "application/x-font-opentype"},
        // {"woff", "application/font-woff"},
        // {"woff2", "application/font-woff2"},
        // {"eot", "application/vnd.ms-fontobject"},
        // {"sfnt", "application/font-sfnt"},

        // {"atascii", HTTPD_TYPE_OCTET},
        // {"bin", HTTPD_TYPE_OCTET},
        {"json", HTTPD_TYPE_JSON},
        {"pdf", "application/pdf"},

        // {"zip", "application/zip"},
        {"gz", "application/x-gzip"}
        // {"appcache", "text/cache-manifest"}
    };

    //Debug_printv("extension[%s]", extension);

    if (extension != NULL)
    {
        std::map<std::string, std::string>::iterator mmatch;

        mmatch = mime_map.find(extension);
        if (mmatch != mime_map.end())
            return mmatch->second.c_str();
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
    std::string fpath = httpdocs;
    // Trim any '/' prefix before adding it to the base directory
    if ( !exists(filename) )
    {
        while (*filename == '/')
            filename++;
        fpath += filename;
    }
    else
    {
        fpath = filename;
    }

    // Handle file differently if it's one of the types we parse
    if (is_parsable(get_extension(filename)))
        return send_file_parsed(req, fpath.c_str());

    // Retrieve server state
    serverstate *pState = (serverstate *)httpd_get_global_user_ctx(req->handle);
    FILE *file = pState->_FS->file_open(fpath.c_str());

    //Debug_printv("filename[%s]", filename);
    if (file == nullptr)
    {
        Debug_printv("Failed to open file for sending: [%s]", fpath.c_str());
        send_http_error(req, 404);
    }
    else
    {
        // Set the response content type
        set_file_content_type(req, fpath.c_str());

        // Set the expected length of the content
        char hdrval[10];
        snprintf(hdrval, 10, "%ld", FileSystem::filesize(file));
        httpd_resp_set_hdr(req, "Content-Length", hdrval);

        // Send the file content out in chunks
        char *buf = (char *)malloc(http_SEND_BUFF_SIZE);
        size_t count = 0;
        do
        {
            count = fread(buf, 1, http_SEND_BUFF_SIZE, file);
            httpd_resp_send_chunk(req, buf, count);
        } while (count > 0);
        fclose(file);
        free(buf);
    }
}

// Send file content after parsing for replaceable strings
void cHttpdServer::send_file_parsed(httpd_req_t *req, const char *filename)
{
    // Note that we don't add FNWS_FILE_ROOT as it should've been done in send_file()

    int err = 200;

    //Debug_printv("filename[%s]", filename);

    // Retrieve server state
    serverstate *pState = (serverstate *)httpd_get_global_user_ctx(req->handle);
    FILE *file = pState->_FS->file_open(filename);

    if (file == nullptr)
    {
        Debug_printv("Failed to open file for parsing: [%s]", filename);
        err = 404;
    }
    else
    {
        // Set the response content type
        set_file_content_type(req, filename);

        // We're going to load the whole thing into memory, so watch out for big files!
        size_t sz = FileSystem::filesize(file) + 1;
        char *buf = (char *)calloc(sz, 1);
        if (buf == NULL)
        {
            Debug_printf("Couldn't allocate %u bytes to load file contents!\r\n", sz);
            err = 500;
        }
        else
        {
            fread(buf, 1, sz, file);
            std::string contents(buf);
            free(buf);
            contents = parse_contents(contents);

            httpd_resp_send(req, contents.c_str(), contents.length());
        }
        fclose(file);
    }

    if (err != 200)
        send_http_error(req, err);
}

// Send some meaningful(?) error message to client
void cHttpdServer::send_http_error(httpd_req_t *req, int errnum)
{
    std::ostringstream error_page;

    error_page << httpdocs << "error/" << errnum << ".html";

    if ( exists(error_page.str()) )
        send_file(req, error_page.str().c_str());
    else
        httpd_resp_send(req, NULL, 0);
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
        state._FS = nullptr;
        state.hServer = nullptr;
    }
}