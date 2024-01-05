#include "httpd_server.h"

#include "../../include/debug.h"

#include "string_utils.h"
// #include "meat_io.h"
// #include "meat_stream.h"

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <sys/stat.h>

#include <webdav_server.h>
#include <request.h>
#include <response.h>

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

long file_size(FILE *fd)
{
    struct stat stat_buf;
    int rc = fstat((int)fd, &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

bool exists(std::string path)
{
    Debug_printv( "path[%s]", path.c_str() );
    struct stat st;
    int i = stat(path.c_str(), &st);
    return (i == 0);
}

/* Our URI handler function to be called during GET /uri request */
esp_err_t cHttpdServer::get_handler(httpd_req_t *httpd_req)
{
    //Debug_printv("url[%s]", httpd_req->uri);

    std::string uri = mstr::urlDecode(httpd_req->uri);
    if (uri == "/")
    {
        uri = "/index.html";
    }

    send_file(httpd_req, uri.c_str());

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

esp_err_t cHttpdServer::webdav_handler(httpd_req_t *httpd_req)
{
    WebDav::Server *server = (WebDav::Server *)httpd_req->user_ctx;
    WebDav::Request req(httpd_req);
    WebDav::Response resp(httpd_req);
    int ret;

    Debug_printv("url[%s]", httpd_req->uri);

    if (!req.parseRequest())
    {
        resp.setStatus(400);
        resp.flushHeaders();
        resp.closeBody();
        return ESP_OK;
    }

    httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Methods", "*");

    Debug_printv("%s[%s]", http_method_str((enum http_method)httpd_req->method), httpd_req->uri);

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
        break;
    case HTTP_HEAD:
        ret = server->doHead(req, resp);
        break;
    case HTTP_LOCK:
        ret = server->doLock(req, resp);
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
        ret = ESP_ERR_HTTPD_INVALID_REQ;
        break;
    }

    resp.setStatus(ret);
    resp.flushHeaders();
    resp.closeBody();

    Debug_printv("ret[%d]", ret);

    return ret;
}

void cHttpdServer::webdav_register(httpd_handle_t server, const char *root_uri, const char *root_path)
{
    WebDav::Server *webDavServer = new WebDav::Server(root_uri, root_path);

    char *uri;
    asprintf(&uri, "%s/?*", root_uri);

    httpd_uri_t uri_dav =
        {
            .uri = uri,
            .method = http_method(0),
            .handler = webdav_handler,
            .user_ctx = webDavServer,
            .is_websocket = false};

    http_method methods[] = {
        HTTP_COPY,
        HTTP_DELETE,
        HTTP_GET,
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
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.max_resp_headers = 16;

    // config.core_id = 0; // Pin to CPU core 0
    //  Keep a reference to our object
    config.global_user_ctx = (void *)&state;

    // Set our own global_user_ctx free function, otherwise the library will free an object we don't want freed
    config.global_user_ctx_free_fn = (httpd_free_ctx_fn_t)custom_global_ctx_free;
    config.uri_match_fn = httpd_uri_match_wildcard;

    Debug_printf("Starting web server on port %d\r\n", config.server_port);
    // Debug_printf("Starting web server on port %d, CPU Core %d\r\n", config.server_port, config.core_id);

    // esp_log_level_set("httpd_uri", ESP_LOG_DEBUG);

    /* URI handler structure for GET /uri */
    httpd_uri_t uri_get = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = get_handler,
        .user_ctx = NULL,
        .is_websocket = false};

    /* URI handler structure for POST /uri */
    httpd_uri_t uri_post = {
        .uri = "/*",
        .method = HTTP_POST,
        .handler = post_handler,
        .user_ctx = NULL,
        .is_websocket = false};

    if (httpd_start(&(state.hServer), &config) == ESP_OK)
    {
        /* Register URI handlers */
        webdav_register(state.hServer, "/dav", "/sd");

        // Default handlers
        httpd_register_uri_handler(state.hServer, &uri_get);
        httpd_register_uri_handler(state.hServer, &uri_post);

        Serial.println(ANSI_GREEN_BOLD "WWW/WebDAV Server Started!" ANSI_RESET);
    }
    else
    {
        state.hServer = NULL;
        Serial.println(ANSI_RED_BOLD "WWW/WebDAV Server FAILED to start!" ANSI_RESET);
    }

    return state.hServer;
}

const char *cHttpdServer::find_mimetype_str(const char *extension)
{
    static std::map<std::string, std::string> mime_map{
        {"css", "text/css"},
        {"txt", "text/plain"},
        {"js", "text/javascript"},
        {"xml", "text/xml; charset=\"utf-8\""},

        {"gif", "image/gif"},
        {"ico", "image/x-icon"},
        {"jpg", "image/jpeg"},
        {"png", "image/png"},
        {"svg", "image/svg+xml"},

        {"atascii", "application/octet-stream"},
        {"bin", "application/octet-stream"},
        {"json", "application/json"},
        {"pdf", "application/pdf"}};

    if (extension != NULL)
    {
        std::map<std::string, std::string>::iterator mmatch;

        mmatch = mime_map.find(extension);
        if (mmatch != mime_map.end())
            return mmatch->second.c_str();
    }
    return NULL;
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
    if (dot != NULL)
    {
        const char *mimetype = find_mimetype_str(dot);
        if (mimetype)
            httpd_resp_set_type(req, mimetype);
    }
}

// Send content of given file out to client
void cHttpdServer::send_file(httpd_req_t *req, const char *filename)
{
    // Build the full file path
    std::string fpath = http_FILE_ROOT;
    // Trim any '/' prefix before adding it to the base directory
    while (*filename == '/')
        filename++;
    fpath += filename;

    // Handle file differently if it's one of the types we parse
    if (is_parsable(get_extension(filename)))
        return send_file_parsed(req, fpath.c_str());

    // Retrieve server state
    serverstate *pState = (serverstate *)httpd_get_global_user_ctx(req->handle);
    FILE *file = pState->_FS->file_open(fpath.c_str());

    Debug_printv("filename[%s]", filename);
    // auto file = MFSOwner::File( fpath );
    // if (!file->exists())
    if (file == nullptr)
    {
        Debug_printf("Failed to open file for sending: '%s'\r\n", fpath.c_str());
        return_http_error(req, http_err_fileopen);
    }
    else
    {
        // auto istream = file->getSourceStream();

        // Set the response content type
        set_file_content_type(req, fpath.c_str());
        // Set the expected length of the content
        char hdrval[10];
        //snprintf(hdrval, 10, "%d", istream->size());
        snprintf(hdrval, 10, "%ld", FileSystem::filesize(file));
        httpd_resp_set_hdr(req, "Content-Length", hdrval);

        // Send the file content out in chunks
        char *buf = (char *)malloc(http_SEND_BUFF_SIZE);
        size_t count = 0;
        do
        {
            // count = istream->read( (uint8_t *)buf, http_SEND_BUFF_SIZE );
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

    http_err err = http_err_noerrr;

    //Debug_printv("filename[%s]", filename);

    // Retrieve server state
    serverstate *pState = (serverstate *)httpd_get_global_user_ctx(req->handle);
    FILE *file = pState->_FS->file_open(filename);

    // auto file = MFSOwner::File( filename );
    // if (!file->exists())
    if (file == nullptr)
    {
        Debug_println("Failed to open file for parsing");
        err = http_err_fileopen;
    }
    else
    {
        // auto istream = file->getSourceStream();

        // Set the response content type
        set_file_content_type(req, filename);
        // We're going to load the whole thing into memory, so watch out for big files!
        //size_t sz = istream->size() + 1;
        size_t sz = FileSystem::filesize(file) + 1;
        char *buf = (char *)calloc(sz, 1);
        if (buf == NULL)
        {
            Debug_printf("Couldn't allocate %u bytes to load file contents!\r\n", sz);
            err = http_err_memory;
        }
        else
        {
            // istream->read( (uint8_t *)buf, sz );
            fread(buf, 1, sz, file);
            std::string contents(buf);
            free(buf);
            contents = parse_contents(contents);

            httpd_resp_send(req, contents.c_str(), contents.length());
        }
        fclose(file);
    }

    if (err != http_err_noerrr)
        return_http_error(req, err);
}

// Send some meaningful(?) error message to client
void cHttpdServer::return_http_error(httpd_req_t *req, http_err errnum)
{
    const char *message;

    switch (errnum)
    {
    case http_err_fileopen:
        message = MSG_ERR_OPENING_FILE;
        break;
    case http_err_memory:
        message = MSG_ERR_OUT_OF_MEMORY;
        break;
    default:
        message = MSG_ERR_UNEXPECTED_HTTPD;
        break;
    }
    httpd_resp_send(req, message, strlen(message));
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
        Debug_println("Stopping web service");
        httpd_stop(state.hServer);
        state._FS = nullptr;
        state.hServer = nullptr;
    }
}